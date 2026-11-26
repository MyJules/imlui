// TypeRegistry: owns the registered C++ types and singletons and binds them to Lua.
//
// install(state) runs for every new Lua state (each hot reload) before the DSL loads, and sets up:
//
//  * `require("imlui.types")`: { names(), isType(name), attach(element), meta(name), tick(now), tickingCount(),
//    parents }. The DSL's constructors call attach() for registered types, which creates the C++ instance right
//    away and stores it in the element table as `__native`, a full userdata whose __gc deletes the instance. The
//    table gets the metatable all elements of its type share (meta(name)): its __index finds the type's methods,
//    so `element:reset()` works, and `element.parent` in `parents` (weak keys and values, filled by the DSL). The
//    instance holds only a weak link back to the table, so collecting the table destroys the instance.
//  * one global per singleton: a userdata whose __index reads properties (getters) and methods, and
//    whose __newindex writes properties (setters) and stores `on*` signal handlers.
//  * a sentinel whose __gc detaches the singletons when the state closes.
//
// One C++ type may be registered under several names (aliases: registerType<Column>("Column") and
// registerType<Column>("Item")). Each name is its own type to Lua; an instance belongs to the name it was
// created for.
//
// The registry is what the Renderer dispatches to (Renderer::Types): rendering an element of a registered
// type syncs the declared properties from the element table into the instance (setters run only on a changed
// value; nil resets a property to its default), and then calls Element::render(). Elements that aren't rendered are
// synced lazily: before a method called from Lua runs on them, and when a parent or another element asks for them
// (sync(), Frame::Child::as<T>()). componentComplete() runs once, right after the first sync. Ticking
// elements (Element::setTicking) are synced and ticked at the start of every frame by tick(now), which
// ui.__tick calls, rendered or not. While a frame renders, an element is synced once: a layout's questions about a
// child (fillsWidth, intrinsicSize, Frame::Child::as), its render and its methods called from bindings share the
// sync, unless Lua code that may have
// changed tables ran in between (luaui::changes(): a handler, a write-back), or the element's own declared fields
// changed since (a fingerprint of their raw values, which catches a write that bypassed the count: a raw
// lua_setfield from C++, a binding that writes), which syncs it again. A debug build reports such a bypass.
// Across frames, a sync reads only the dynamic properties (bindings, binds, tables) again while the fingerprint and
// the theme are what they were at the last full sync: the static ones (plain values) would convert to what the
// instance holds already.
//
// An element's identity is its instance's ScriptObject::objectId() (elementId()): the renderer's ImGui ID scope and
// layout cache key, never reused, unlike the table's address.
// Main thread only.
export module imlui.types.registry;

import std;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.theme.theme;
import imlui.ui.layout_box;
import imlui.ui.renderer;
import imlui.types.script_object;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_info;
import imlui.types.type_builder;

export namespace imlui {

class TypeRegistry final : public Renderer::Types {
public:
    TypeRegistry();
    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

    /// Registers element type T as `name` (replacing an earlier type of that name). T derives from
    /// Element, is default-constructible, and may have `static void declare(TypeBuilder<T>&)`.
    /// Takes effect right away, in installed Lua states too: elements created from then on get the new type
    /// (an element that already has an instance keeps it, with the type it was created as).
    template <class T>
        requires std::derived_from<T, Element> && std::default_initializable<T>
    TypeInfo& registerType(std::string name) {
        auto info = std::make_unique<TypeInfo>(std::move(name));
        info->setFactory([] { return std::unique_ptr<Element>{std::make_unique<T>()}; });
        declareType<T>(*info);
        return addType(std::move(info));
    }

    /// Exposes `object` to Lua as the global `name` (replacing an earlier singleton of that name). The object isn't
    /// owned: it should outlive every Lua state the registry installs into (in practice: the Engine's run()); if it
    /// dies first, Lua access to the global raises "singleton '<name>' was destroyed". Prefer the shared_ptr
    /// overload when the object has no natural owner that outlives the engine.
    template <class T>
        requires std::derived_from<T, ScriptObject>
    TypeInfo& registerSingleton(std::string name, T& object) {
        auto info = std::make_unique<TypeInfo>(name);
        declareType<T>(*info);
        return addSingleton(std::move(name), object, std::move(info), nullptr);
    }

    /// Exposes `object` to Lua as the global `name` and keeps it alive as long as the registry (the Engine)
    /// exists, so it outlives every Lua state. Throws std::invalid_argument for a null pointer.
    template <class T>
        requires std::derived_from<T, ScriptObject>
    TypeInfo& registerSingleton(std::string name, std::shared_ptr<T> object) {
        if (!object) throw std::invalid_argument{"registerSingleton(\"" + name + "\"): null object"};
        auto info = std::make_unique<TypeInfo>(name);
        declareType<T>(*info);
        ScriptObject& ref = *object;
        return addSingleton(std::move(name), ref, std::move(info), std::shared_ptr<ScriptObject>{std::move(object)});
    }

    /// Installs the types and singletons into a new Lua state (before src/resources/imlui.lua is loaded).
    void install(lua::State* state);
    /// Points the singletons back at `state`, which was installed before (after a failed reload).
    void activate(lua::State* state);
    /// Detaches the singletons linked to `state`. Runs automatically when an installed state closes.
    void detachState(lua::State* state);

    [[nodiscard]] const TypeInfo* find(std::string_view name) const;
    /// Registered element type names, sorted (what `ui.types()` returns).
    [[nodiscard]] std::vector<std::string> typeNames() const;

    // Renderer::Types: the Renderer renders every element through these.
    [[nodiscard]] Kind kind(const luaui::Node& node) const override;
    void renderElement(Renderer& renderer, const luaui::Node& node) override;
    [[nodiscard]] bool fillsWidth(Renderer& renderer, const luaui::Node& node) override;
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(Renderer& renderer, const luaui::Node& node,
                                                      float maxWidth) override;
    [[nodiscard]] std::uint64_t elementId(const luaui::Node& node) override;

    /// The instance of the element table `node`, created now if the table has none yet. Then syncs
    /// the declared properties into it (and calls componentComplete() after the first sync). nullptr if
    /// `node` isn't a registered type.
    Element* sync(const luaui::Node& node) const;

private:
    /// Runs T::declare. A T whose only declare() is inherited from a base (it takes TypeBuilder<Base>&) would
    /// silently lose its members, so that is an error: give T its own declare() that calls
    /// `type.extends<Base>()`.
    template <class T>
    static void declareType(TypeInfo& info) {
        if constexpr (requires(TypeBuilder<T>& b) { T::declare(b); }) {
            TypeBuilder<T> builder{info};
            T::declare(builder);
        } else {
            static_assert(
                !requires { &T::declare; },
                "T::declare must take TypeBuilder<T>&; a subclass declares its own and calls "
                "type.extends<Base>() in it");
        }
    }

    struct Singleton {
        std::string name;
        ScriptObject* object;
        const TypeInfo* type;
        std::shared_ptr<ScriptObject> owner;                ///< set by the shared_ptr overload
        std::shared_ptr<const TypeInfo::Defaults> defaults; ///< captured at registration
        /// Whether `object` still lives: a by-reference singleton may be destroyed while registered.
        ScriptObject::Handle life;
    };

    /// sync() while rendering: an element already synced this frame isn't synced again while nothing that could
    /// change its table ran since (luaui::changes()).
    Element* renderSync(const luaui::Node& node) const;

    TypeInfo& addType(std::unique_ptr<TypeInfo> info);
    TypeInfo& addSingleton(std::string name, ScriptObject& object, std::unique_ptr<TypeInfo> info,
                           std::shared_ptr<ScriptObject> owner);

    /// What Frames use to resolve typed children (Frame::Child::as): renderSync(node).
    Frame::InstanceLookup m_instances;
    /// Never shrinks: Lua states hold raw pointers to these until they close.
    std::vector<std::unique_ptr<TypeInfo>> m_infos;
    /// The current element types by name (a re-registration replaces).
    std::map<std::string, const TypeInfo*, std::less<>> m_types;
    std::vector<Singleton> m_singletons;
};

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

constexpr const char* instanceMetaKey = "imlui.instance";
constexpr const char* singletonMetaKey = "imlui.singleton";
constexpr const char* methodsKey = "imlui.methods";   ///< { [TypeInfo*] = { name = closure } }
constexpr const char* typeMetaKey = "imlui.typemeta"; ///< { [TypeInfo*] = element table metatable }
constexpr const char* parentsKey = "imlui.parents";   ///< { [element] = parent }, weak keys and values
constexpr const char* singletonsKey = "imlui.singletons";
constexpr const char* sentinelKey = "imlui.types.sentinel";
constexpr const char* markerKey = "__imlui";

/// The userdata behind `__native` and behind a singleton global. Its __gc destroys it (and, for an owned
/// element, the instance).
struct Box {
    ScriptObject* object = nullptr;
    Element* element = nullptr; ///< set for owned element instances
    const TypeInfo* type = nullptr;
    /// What nil resets each property to: the type's (TypeInfo::sharedDefaults), a singleton's own. Null: none.
    std::shared_ptr<const TypeInfo::Defaults> defaults;
    bool owned = false;
    /// A singleton's: whether `object` (which the host owns) still lives. liveObject() reads it.
    std::optional<ScriptObject::Handle> life;
    bool complete = false; ///< componentComplete() was called
    bool syncing = false;  ///< a sync is running (a binding that calls a method of its own element)
    // When the last sync ended: an ImGui frame (-1 outside one) and luaui::changes(), and what the declared fields
    // of the element table held when it started (fingerprint()). See Sync::OncePerFrame.
    int syncedFrame = -1;
    std::uint64_t syncedChanges = 0;
    std::uint64_t syncedFields = 0;
    // What a sync may skip (see syncInstance): the instance holds every static property of the table as it was at
    // syncedFields (`staticSynced`: the last full sync ran to its end), resolved under theme generation
    // `syncedTheme`. Only the properties in `dynamicFields` (their raw value may resolve to something else with no
    // write to the table: a binding, a bind, a table) are read again while that holds.
    bool staticSynced = false;
    std::uint64_t syncedTheme = 0;
    std::vector<std::uint32_t> dynamicFields; ///< indices into TypeInfo::properties()
    /// What layouts asked this frame (fillsWidth, intrinsicSize): a container composes its children's sizes, and its
    /// parent asks it too, so each element answers once a frame per question. See LayoutMemo.
    struct LayoutMemo {
        struct Size {
            float maxWidth;
            bool shrinkToFit; ///< asked in an auto-sized window's box (wrapping text measures differently there)
            std::optional<ImVec2> size;
        };
        int frame = -1;
        std::uint64_t changes = 0;
        std::optional<bool> fills;
        std::array<Size, 4> sizes{}; ///< the latest answers, oldest first
        std::size_t count = 0;

        [[nodiscard]] const Size* find(float maxWidth, bool shrinkToFit) const {
            for (std::size_t i = 0; i < count; ++i)
                if (sizes[i].maxWidth == maxWidth && sizes[i].shrinkToFit == shrinkToFit) return &sizes[i];
            return nullptr;
        }
        void add(const Size& size) {
            if (count == sizes.size())
                std::shift_left(sizes.begin(), sizes.end(), 1);
            else
                ++count;
            sizes[count - 1] = size;
        }
    } memo;
};

/// The memo of `b`, emptied when it is from another frame or Lua that may have changed tables ran since
/// (luaui::changes(), like the sync skip). A sync empties it too (syncInstance).
Box::LayoutMemo& freshMemo(Box& b) {
    const int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    if (b.memo.frame != frame || b.memo.changes != luaui::changes()) {
        b.memo = {};
        b.memo.frame = frame;
        b.memo.changes = luaui::changes();
    }
    return b.memo;
}

/// Whether syncInstance may skip an element synced earlier in the frame.
enum class Sync {
    Always,
    /// Layouts ask about a child (fillsWidth, intrinsicSize) and then render it, and a Row asks twice: one sync a
    /// frame is enough while nothing that could change its table ran in between (luaui::changes() stood still: a
    /// handler, a write-back or a script between frames moves it) and its declared fields hold what they held at
    /// the sync (fingerprint(): a write that bypassed the count is synced anyway, and reported in debug builds).
    /// Only while rendering a frame.
    OncePerFrame,
};

#ifdef NDEBUG
constexpr bool reportBypassedWrites = false;
#else
/// Debug builds report a declared field that changed while luaui::changes() stood still: the code that wrote it
/// bypassed the count, so its writes to other tables (which bindings may read) aren't seen either.
constexpr bool reportBypassedWrites = true;
#endif

/// Mixes `v` into the hash `h` (splitmix64's finalizer on the sum).
std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    std::uint64_t z = h + v + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

/// Mixes the value on top of the stack into `h`: nil, booleans, numbers and strings by value, anything else
/// (tables, functions, bind objects, userdata) by identity.
std::uint64_t mixValue(lua::State* state, std::uint64_t h) {
    const lua::Type t = lua::type(state, -1);
    h = mix(h, static_cast<std::uint64_t>(t));
    switch (t) {
    case lua::Type::Nil: return h;
    case lua::Type::Boolean: return mix(h, lua::toboolean(state, -1) ? 1 : 0);
    case lua::Type::Number:
        if (lua::isinteger(state, -1)) return mix(h, static_cast<std::uint64_t>(lua::tointeger(state, -1)));
        return mix(h, std::bit_cast<std::uint64_t>(static_cast<double>(lua::tonumber(state, -1))));
    case lua::Type::String: return mix(h, std::hash<std::string_view>{}(lua::tostring(state, -1)));
    default: return mix(h, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(lua::topointer(state, -1))));
    }
}

/// Whether the element table at `tbl` has a metatable of its own (a script's), not the one its type's elements share
/// (luaui::sharedMetaMarker): property reads then go through its __index for a field the table doesn't hold.
bool hasOwnMeta(lua::State* state, int tbl) {
    if (lua::getmetatable(state, tbl) == 0) return false;
    lua::pushcstring(state, luaui::sharedMetaMarker);
    const bool own = lua::rawget(state, -2) == lua::Type::Nil;
    lua::pop(state, 2);
    return own;
}

/// A hash of what the property sync reads from the element table at `tbl` before resolving anything: the raw value
/// of every declared, writable property (and the positional [1] the default property may take), and the table's
/// metatable. Raw reads (no metamethods), so it never runs Lua and can't raise. What a binding or a nested table
/// holds isn't in it: a change there is seen through luaui::changes(), and every sync re-reads those properties
/// (dynamicFields).
std::uint64_t fingerprint(lua::State* state, int tbl, const TypeInfo& info) {
    lua::checkstack(state, 2);
    std::uint64_t h = 0;
    for (const TypeInfo::Property& p : info.properties()) {
        if (!p.assign) continue;
        lua::pushcstring(state, p.name.c_str());
        lua::rawget(state, tbl);
        h = mixValue(state, h);
        lua::pop(state);
    }
    if (!info.defaultProperty().empty()) {
        lua::rawgeti(state, tbl, 1);
        h = mixValue(state, h);
        lua::pop(state);
    }
    // setmetatable() is no write to a field, but an __index of the table's own answers for the fields it doesn't hold.
    if (lua::getmetatable(state, tbl) != 0) {
        h = mixValue(state, h);
        lua::pop(state);
    }
    return h;
}

/// Collects into `out` the declared, writable properties of the element table at `tbl` whose raw value may resolve to
/// something else while the table itself doesn't change, so that a sync must read them again although the fingerprint
/// matches: a binding (a function, called every sync), a bind or any other table (a nested table changed in place: a
/// bind's source, a color, a model), a userdata (its contents), and, when the table has a metatable of its own, any
/// field the table doesn't hold (its __index answers). A nil, boolean, number or string is static: its property
/// converts the same way until the table's field is written, which the fingerprint sees.
void collectDynamicFields(lua::State* state, int tbl, const TypeInfo& info, std::vector<std::uint32_t>& out) {
    out.clear();
    lua::checkstack(state, 2);
    const bool ownMeta = hasOwnMeta(state, tbl);
    const auto& properties = info.properties();
    for (std::size_t i = 0; i < properties.size(); ++i) {
        const TypeInfo::Property& p = properties[i];
        if (!p.assign) continue;
        lua::pushcstring(state, p.name.c_str());
        const lua::Type t = lua::rawget(state, tbl);
        lua::pop(state);
        bool dynamic = false;
        switch (t) {
        case lua::Type::Boolean:
        case lua::Type::Number:
        case lua::Type::String: break;
        // (The default property's positional [1] is passed on unresolved, by identity: in the fingerprint.)
        case lua::Type::Nil: dynamic = ownMeta; break;
        default: dynamic = true; break; // functions, tables (binds), userdata, threads
        }
        if (dynamic) out.push_back(static_cast<std::uint32_t>(i));
    }
}

/// The current ImGui frame, -1 without a context.
int currentFrame() { return ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1; }

/// Runs the Box destructor and leaves an empty Box in the userdata memory, so a finalized userdata that
/// is still reachable (resurrected by another finalizer) reads as "no object" instead of freed memory.
void clearBox(Box* b) {
    std::destroy_at(b);
    std::construct_at(b);
}

void* asVoid(const void* p) { return const_cast<void*>(p); }

/// The object of `b`, or nullptr once it is gone: a singleton the host destroyed (or replaced, which destroyed it)
/// while this state was installed.
ScriptObject* liveObject(Box& b) {
    if (b.object && b.life && !b.life->alive()) b.object = nullptr;
    return b.object;
}

/// Raises the error for a singleton global whose object is gone.
[[noreturn]] void raiseDeadSingleton(lua::State* state, const Box* b) {
    lua::error(state, b && b->type ? "singleton '" + b->type->name() + "' was destroyed" : std::string{"singleton is gone"});
}

/// The box metatables (instance and singleton) of the installed Lua states, by address: boxAt() recognizes a box by
/// its metatable with no table read (it runs several times per element and frame). install() adds the ones it creates
/// and hands them to its sentinel, whose finalizer removes them: when the state closes (lua_close runs finalizers
/// before it frees any memory), or earlier if another install() replaced the sentinel. So an address here is never
/// one a freed table's memory was reused at. A box whose metatable isn't here (a finalizer that runs after the
/// sentinel's at close) is recognized by its metatable's marker field instead. A list (not a set): two installs
/// never share a metatable, but each removes exactly what it added.
std::vector<const void*> boxMetas; // main thread only, like all Lua code

/// The Box at `idx`, or nullptr if the value isn't one of ours.
Box* boxAt(lua::State* state, int idx) {
    if (lua::type(state, idx) != lua::Type::UserData || lua::getmetatable(state, idx) == 0) return nullptr;
    if (std::ranges::find(boxMetas, lua::topointer(state, -1)) != boxMetas.end()) {
        lua::pop(state);
        return static_cast<Box*>(lua::touserdata(state, idx));
    }
    lua::pushcstring(state, markerKey);
    lua::rawget(state, -2);
    const bool ours = lua::toboolean(state, -1);
    lua::pop(state, 2);
    return ours ? static_cast<Box*>(lua::touserdata(state, idx)) : nullptr;
}

/// The Box of an element table (`__native`) or of a singleton userdata.
Box* selfBox(lua::State* state, int idx) {
    if (lua::istable(state, idx)) {
        lua::pushcstring(state, "__native");
        lua::rawget(state, idx);
        Box* b = boxAt(state, -1);
        lua::pop(state);
        return b;
    }
    return boxAt(state, idx);
}

void pushRegistryTable(lua::State* state, const char* key) {
    if (lua::getfield(state, lua::registryIndex, key) == lua::Type::Table) return;
    lua::pop(state);
    lua::newtable(state);
    lua::pushvalue(state, -1);
    lua::setfield(state, lua::registryIndex, key);
}

/// Pushes registry[key][info] (nil if absent).
void pushPerType(lua::State* state, const char* key, const TypeInfo* info) {
    pushRegistryTable(state, key);
    lua::rawgetp(state, -1, info);
    lua::remove(state, -2);
}

// ------------------------------------------------------------ metamethods

int instanceGc(lua::State* state) {
    Box* b = boxAt(state, 1);
    if (!b) return 0;
    Element* e = b->owned ? b->element : nullptr;
    const TypeInfo* type = b->type;
    clearBox(b);
    if (!e) return 0;
    // A finalizer must not raise: an exception from destroyed() is reported, and the instance is deleted anyway.
    luaui::reportCxx(
        [e] {
            e->detach();
            e->cancelHandles(); // workers see the cancellation during destroyed() and the destructors
            e->destroyed();
        },
        type ? std::string_view{type->name()} : std::string_view{"element"}, ".destroyed()");
    delete e;
    return 0;
}

int singletonGc(lua::State* state) {
    if (Box* b = boxAt(state, 1)) clearBox(b); // the object itself belongs to the host
    return 0;
}

/// Resets property `index` of the object in `b` to its default. Unchanged when it has none.
void resetProperty(const Box& b, std::size_t index) {
    const TypeInfo::Property& p = b.type->properties()[index];
    if (p.reset && b.defaults && index < b.defaults->size() && (*b.defaults)[index])
        p.reset((*b.defaults)[index].get(), *b.object);
}

/// Writes declared property `index` of the element table `node` into b's object.
void syncProperty(lua::State* state, const Node& node, Box& b, std::size_t index) {
    const TypeInfo& info = *b.type;
    const TypeInfo::Property& p = info.properties()[index];
    node.push(p.name.c_str());
    if (lua::isnil(state, -1) && p.name == info.defaultProperty()) {
        lua::pop(state);
        lua::rawgeti(state, node.index(), 1); // `Gauge { 0.5 }`: a positional value that isn't a child
        if (lua::istable(state, -1)) {
            lua::pop(state);
            lua::pushnil(state);
        }
    }
    if (lua::isnil(state, -1) && !p.nullable) {
        lua::pop(state); // unset (or a binding that returned nil): back to the default
        resetProperty(b, index);
        return;
    }
    if (p.assign(state, -1, *b.object) == TypeInfo::Assign::TypeMismatch)
        luaui::reportError(info.name() + "." + p.name + ": " + std::string{p.typeName} + " expected, got " +
                           lua::typeName(state, -1));
    lua::pop(state);
}

/// The property loop of syncInstance: writes the declared properties of the element table at `tbl` into b's object,
/// all of them (`dynamicOnly` false), or only b.dynamicFields.
void syncProperties(lua::State* state, int tbl, Box& b, bool dynamicOnly) {
    const Node node{state, tbl};
    if (dynamicOnly) {
        for (const std::uint32_t i : b.dynamicFields)
            syncProperty(state, node, b, i);
        return;
    }
    const auto& properties = b.type->properties();
    for (std::size_t i = 0; i < properties.size(); ++i)
        if (properties[i].assign) syncProperty(state, node, b, i);
}

/// Arg 1: the Box (light userdata), arg 2: the element table, arg 3: dynamicOnly (a boolean). syncProperties in a Lua
/// function, so that it runs protected; a C++ exception it throws becomes a Lua error.
int syncPropertiesFn(lua::State* state) {
    Box& b = *static_cast<Box*>(lua::touserdata(state, 1));
    const bool dynamicOnly = lua::toboolean(state, 3);
    luaui::catchCxx(state, [&] { syncProperties(state, 2, b, dynamicOnly); }, b.type->name(), " property sync");
    return 0;
}

/// Syncs the declared properties of the element table at `tbl` into its instance, then calls
/// componentComplete() if this was the first sync. Skipped while a sync of the same element runs.
/// Never raises: an error in the sync (a metamethod of the table, a binding's source, ...) or an exception
/// from componentComplete() is reported.
void syncInstance(lua::State* state, int tbl, Box& b, Sync mode = Sync::Always) {
    if (b.syncing || !b.element) return;
    tbl = lua::absindex(state, tbl);
    const int frame = currentFrame();
    // Taken before the sync: a binding that writes a declared field of its own element during the sync makes the
    // next query sync again.
    const std::uint64_t fields = fingerprint(state, tbl, *b.type);
    if (mode == Sync::OncePerFrame && b.complete && frame >= 0 && b.syncedFrame == frame &&
        b.syncedChanges == luaui::changes()) {
        if (fields == b.syncedFields) return;
        if constexpr (reportBypassedWrites)
            luaui::reportError(b.type->name() +
                               ": a declared field of the element table changed without luaui::changes() counting "
                               "it (a raw write from C++, or a binding that writes); synced again. Write through "
                               "luaui::Node::set, or call luaui::noteChange() after the write");
    }
    // `parent` is read through the metatable from the parents table, so a raw field (a script assigned it) would
    // shadow it: dropped here. (A script's write counts as a change, so the element syncs again after it.)
    lua::pushcstring(state, "parent");
    if (lua::rawget(state, tbl) != lua::Type::Nil) {
        lua::pushcstring(state, "parent");
        lua::pushnil(state);
        lua::rawset(state, tbl);
        if constexpr (reportBypassedWrites)
            luaui::reportError(b.type->name() + ": 'parent' is read-only (its container sets it); the value assigned "
                                                "to it is dropped");
    }
    lua::pop(state);
    // A static property (a plain value) converts to the same value until its field is written, which changes the
    // fingerprint, or the theme changes (a color role). While neither happened since a full sync that ran to its end,
    // the instance holds them all already, and only the dynamic ones (bindings, binds, tables) are read again. (A
    // property the C++ side changed itself without notify() isn't reset to the table's value then: notify() writes
    // the table, so the next sync is full.)
    const std::uint64_t theme = themeGeneration();
    const bool dynamicOnly = b.complete && b.staticSynced && fields == b.syncedFields && theme == b.syncedTheme;
    if (!dynamicOnly) collectDynamicFields(state, tbl, *b.type, b.dynamicFields);
    bool ok = true;
    // Nothing to read (every property static and already applied): no protected call. The most common case on a
    // steady frame (plain Text, Icon, Button).
    if (!dynamicOnly || !b.dynamicFields.empty()) {
        b.staticSynced = false; // until this sync gets to its end
        b.syncing = true;
        // Protected: an error in the loop is reported, not raised, and can't skip the reset below (`syncing` left
        // set would skip every later sync of the element). (The table at `tbl` keeps `b` alive meanwhile.) It only
        // reads the table (and the bindings), so it doesn't count as a change itself.
        lua::checkstack(state, 5);
        lua::pushcfunction(state, &syncPropertiesFn);
        lua::pushlightuserdata(state, &b);
        lua::pushvalue(state, tbl);
        lua::pushboolean(state, dynamicOnly);
        // An error cut the loop short: some static properties may not be applied, so the next sync is a full one.
        ok = luaui::safeRead(state, 3, 0);
    }
    b.syncing = false;
    b.staticSynced = ok;
    b.syncedTheme = theme;
    b.memo = {}; // its answers may have changed with its properties
    b.syncedFrame = frame;
    b.syncedChanges = luaui::changes(); // before componentComplete(), whose handlers may change the table
    b.syncedFields = fields;
    if (!b.complete && b.element) {
        b.complete = true;
        Element* e = b.element;
        luaui::reportCxx([e] { e->componentComplete(); }, b.type->name(), ".componentComplete()");
    }
}

/// __get(box, name): the getter's value. ScriptObject::notify reads the current value through it.
int boxGet(lua::State* state) {
    Box* b = boxAt(state, 1);
    if (!b || !liveObject(*b)) lua::error(state, "__get: not a live imlui object");
    const std::string_view key = lua::tostring(state, 2);
    const TypeInfo::Property* p = b->type->findProperty(key);
    if (!p) lua::error(state, b->type->name() + " has no property '" + std::string{key} + "'");
    p->push(state, *b->object);
    return 1;
}

/// Upvalues: TypeInfo*, method index. Arg 1 is the element table or singleton userdata.
int methodTrampoline(lua::State* state) {
    const auto* info = static_cast<const TypeInfo*>(lua::touserdata(state, lua::upvalueindex(1)));
    const auto index = static_cast<std::size_t>(lua::tointeger(state, lua::upvalueindex(2)));
    Box* b = selfBox(state, 1);
    if (!b || b->type != info) lua::error(state, "method called without its object (use obj:method())");
    if (!liveObject(*b)) {
        if (b->life) raiseDeadSingleton(state, b);
        lua::error(state, "method called on a destroyed object");
    }
    const TypeInfo::Method& method = info->methods()[index];
    return luaui::catchCxx(
        state,
        [&] {
            // An element that hasn't rendered yet (or not since its props changed) gets its current values first.
            // Called from a binding (a read), it reuses this frame's sync like a layout's questions do: nothing that
            // could change the table ran since. Called from anything else (a handler, a script), it syncs: Lua code
            // there may have changed what the bindings read without luaui::changes() seeing it.
            if (b->element && lua::istable(state, 1))
                syncInstance(state, 1, *b, luaui::reading() ? Sync::OncePerFrame : Sync::Always);
            if (!b->object) lua::error(state, "method called on a destroyed object");
            return method.call(state, *b->object, 2);
        },
        info->name(), " method"); // (TypeBuilder methods report their own exceptions, with the method name)
}

/// singleton.key
int singletonIndex(lua::State* state) {
    Box* b = boxAt(state, 1);
    if (!b || !liveObject(*b)) raiseDeadSingleton(state, b);
    const std::string_view key = lua::tostring(state, 2);
    if (const TypeInfo::Property* p = b->type->findProperty(key)) {
        p->push(state, *b->object);
        return 1;
    }
    if (b->type->findMethod(key)) {
        pushPerType(state, methodsKey, b->type);
        lua::pushvalue(state, 2);
        lua::rawget(state, -2);
        return 1;
    }
    lua::getiuservalue(state, 1, 1); // signal handlers
    lua::pushvalue(state, 2);
    lua::rawget(state, -2);
    return 1;
}

/// singleton.key = value
int singletonNewIndex(lua::State* state) {
    Box* b = boxAt(state, 1);
    if (!b || !liveObject(*b)) raiseDeadSingleton(state, b);
    if (lua::type(state, 2) != lua::Type::String) lua::error(state, "singleton keys are strings");
    const std::string key{lua::tostring(state, 2)};
    if (const TypeInfo::Property* p = b->type->findProperty(key)) {
        if (!p->assign) lua::error(state, b->type->name() + "." + key + " is read-only");
        if (lua::isnil(state, 3) && !p->nullable)
            resetProperty(*b, static_cast<std::size_t>(p - b->type->properties().data()));
        else if (p->assign(state, 3, *b->object) == TypeInfo::Assign::TypeMismatch)
            lua::error(state, b->type->name() + "." + key + ": " + std::string{p->typeName} + " expected, got " +
                                  lua::typeName(state, 3));
        return 0;
    }
    if (!key.starts_with("on") || !(lua::isfunction(state, 3) || lua::isnil(state, 3)))
        lua::error(state, b->type->name() + " has no property '" + key + "'" +
                              (key.starts_with("on") ? " (signal handlers must be functions)" : ""));
    lua::getiuservalue(state, 1, 1); // signal handlers
    lua::pushvalue(state, 2);
    lua::pushvalue(state, 3);
    lua::rawset(state, -3);
    return 0;
}

int singletonToString(lua::State* state) {
    Box* b = boxAt(state, 1);
    lua::pushstring(state, b && b->type ? "singleton " + b->type->name() : std::string{"singleton"});
    return 1;
}

int sentinelGc(lua::State* state) {
    auto* registry = static_cast<TypeRegistry*>(lua::touserdata(state, lua::upvalueindex(1)));
    for (const int i : {2, 3}) // the box metatables install() tracked, before lua_close frees them
        if (const auto it = std::ranges::find(boxMetas, lua::touserdata(state, lua::upvalueindex(i)));
            it != boxMetas.end())
            boxMetas.erase(it);
    registry->detachState(state);
    return 0;
}

// ------------------------------------------------------------ instances

/// Pushes a methods table { name = closure } for `info`.
void pushMethodsTable(lua::State* state, const TypeInfo& info) {
    lua::createtable(state, 0, static_cast<int>(info.methods().size()));
    for (std::size_t i = 0; i < info.methods().size(); ++i) {
        lua::pushlightuserdata(state, asVoid(&info));
        lua::pushinteger(state, static_cast<lua::Integer>(i));
        lua::pushcclosure(state, &methodTrampoline, 2);
        lua::setfield(state, -2, info.methods()[i].name.c_str());
    }
}

/// Pushes the parents table: { [element] = parent }, weak keys and values. The DSL sets the entries (`types.parents`,
/// see src/resources/imlui.lua), `element.parent` reads them through the type's metatable (typeIndex).
void pushParents(lua::State* state) {
    if (lua::getfield(state, lua::registryIndex, parentsKey) == lua::Type::Table) return;
    lua::pop(state);
    lua::newtable(state);
    lua::createtable(state, 0, 1);
    lua::pushstring(state, "kv");
    lua::setfield(state, -2, "__mode");
    lua::setmetatable(state, -2);
    lua::pushvalue(state, -1);
    lua::setfield(state, lua::registryIndex, parentsKey);
}

/// __index of element tables (t, k): `parent` from the parents table, anything else from the methods table.
/// Upvalues: the methods table, the parents table, the string "parent".
int typeIndex(lua::State* state) {
    if (lua::rawequal(state, 2, lua::upvalueindex(3))) {
        lua::pushvalue(state, 1);
        lua::rawget(state, lua::upvalueindex(2));
    } else {
        lua::pushvalue(state, 2);
        lua::rawget(state, lua::upvalueindex(1));
    }
    return 1;
}

/// Pushes the metatable that every element table of type `info` shares ({ __index = typeIndex }: the type's methods,
/// and `parent`). Made on first use and kept in the registry (with the methods table), so a type registered after
/// the state was installed works too.
void pushTypeMeta(lua::State* state, const TypeInfo& info) {
    pushPerType(state, typeMetaKey, &info);
    if (!lua::isnil(state, -1)) return;
    lua::pop(state);
    pushMethodsTable(state, info);        // methods
    pushRegistryTable(state, methodsKey); // methods, M
    lua::pushvalue(state, -2);
    lua::rawsetp(state, -2, &info); // M[info] = methods
    lua::pop(state);                // methods
    pushParents(state);             // methods, parents
    lua::pushstring(state, "parent");
    lua::pushcclosure(state, &typeIndex, 3); // index
    lua::createtable(state, 0, 2);           // index, meta
    lua::insert(state, -2);                  // meta, index
    lua::setfield(state, -2, "__index");     // meta
    lua::pushboolean(state, true);
    lua::setfield(state, -2, luaui::sharedMetaMarker); // property reads skip it (Node::pushRaw)
    pushRegistryTable(state, typeMetaKey);
    lua::pushvalue(state, -2);
    lua::rawsetp(state, -2, &info); // registry[typeMetaKey][info] = meta
    lua::pop(state);                // meta
}

/// Whether the constructor of the element table at `tbl` failed before (attachInstance stored `__native = false`).
bool constructionFailed(lua::State* state, int tbl) {
    lua::pushcstring(state, "__native");
    lua::rawget(state, tbl);
    const bool failed = lua::isboolean(state, -1) && !lua::toboolean(state, -1);
    lua::pop(state);
    return failed;
}

/// Creates the instance for the element table at `tbl`. Returns its box (nullptr on failure). A constructor that
/// threw is remembered in the table (`__native = false`): the element isn't constructed again (and the failure
/// reported again) at every query, every frame.
Box* attachInstance(lua::State* state, int tbl, const TypeInfo& info) {
    tbl = lua::absindex(state, tbl);
    lua::checkstack(state, 8);
    // One read of `__native` for both questions (selfBox, constructionFailed): this runs for every element built.
    lua::pushcstring(state, "__native");
    lua::rawget(state, tbl);
    Box* existing = lua::isnil(state, -1) ? nullptr : boxAt(state, -1);
    const bool failed = lua::isboolean(state, -1) && !lua::toboolean(state, -1);
    lua::pop(state);
    if (existing) return existing;
    if (!info.factory() || failed) return nullptr;

    // The userdata first, so a Lua memory error can't leak a constructed instance.
    Box* b = new (lua::newuserdata(state, sizeof(Box))) Box{};
    const int ud = lua::gettop(state);
    lua::getfield(state, lua::registryIndex, instanceMetaKey);
    lua::setmetatable(state, ud);
    try {
        b->element = info.factory()().release();
        // Right after construction: what nil resets each property to.
        if (b->element) b->defaults = info.sharedDefaults(*b->element);
    } catch (const lua::Raised&) {
        delete b->element; // a Lua error (a constructor that called into Lua): not ours to report, it propagates
        b->element = nullptr;
        throw;
    } catch (const std::exception& e) {
        luaui::reportError(info.name() + ": constructor failed: " + e.what());
        delete b->element;
        b->element = nullptr;
    } catch (...) {
        luaui::reportError(info.name() + ": constructor failed: unknown C++ exception");
        delete b->element;
        b->element = nullptr;
    }
    if (!b->element) {
        lua::pop(state);
        lua::pushstring(state, "__native");
        lua::pushboolean(state, false);
        lua::rawset(state, tbl);
        return nullptr;
    }
    b->object = b->element;
    b->type = &info;
    b->owned = true;

    lua::pushstring(state, "__native");
    lua::pushvalue(state, ud);
    lua::rawset(state, tbl);
    if (lua::getmetatable(state, tbl) == 0) {
        pushTypeMeta(state, info);
        lua::setmetatable(state, tbl);
    } else {
        lua::pop(state); // the table has its own metatable: keep it (methods unreachable via `:`)
    }
    b->element->attach(state, tbl, false);
    lua::pop(state); // ud
    return b;
}

TypeRegistry* upvalueRegistry(lua::State* state) {
    return static_cast<TypeRegistry*>(lua::touserdata(state, lua::upvalueindex(1)));
}

int libNames(lua::State* state) {
    return luaui::catchCxx(
        state,
        [&] {
            const std::vector<std::string> names = upvalueRegistry(state)->typeNames();
            lua::createtable(state, static_cast<int>(names.size()), 0);
            lua::Integer i = 0;
            for (const auto& n : names) {
                lua::pushstring(state, n);
                lua::rawseti(state, -2, ++i);
            }
            return 1;
        },
        "imlui.types.names");
}

/// meta(name): the metatable every element table of type `name` shares (nil for an unknown type), for element
/// tables that get their instance later (the Lua-side containers).
int libMeta(lua::State* state) {
    return luaui::catchCxx(
        state,
        [&] {
            const TypeInfo* info =
                lua::isstring(state, 1) ? upvalueRegistry(state)->find(lua::tostring(state, 1)) : nullptr;
            if (info)
                pushTypeMeta(state, *info);
            else
                lua::pushnil(state);
            return 1;
        },
        "imlui.types.meta");
}

int libIsType(lua::State* state) {
    lua::pushboolean(state, lua::isstring(state, 1) && upvalueRegistry(state)->find(lua::tostring(state, 1)));
    return 1;
}

/// attach(element [, name]): creates the C++ instance of a registered element table. Returns the table. With `name`,
/// first sets `element.__type = name` (a raw set), the hash part sized once for it and `__native`: the DSL's
/// constructors pass it for a table without a metatable, so building an element resizes its table once, not twice;
/// the table then always gets the type's metatable.
int libAttach(lua::State* state) {
    return luaui::catchCxx(
        state,
        [&] {
            if (lua::istable(state, 1)) {
                if (lua::isstring(state, 2)) {
                    lua::reservehash(state, 1, 2);
                    lua::pushcstring(state, "__type");
                    lua::pushvalue(state, 2);
                    lua::rawset(state, 1);
                }
                lua::getfield(state, 1, "__type");
                const TypeInfo* info =
                    lua::isstring(state, -1) ? upvalueRegistry(state)->find(lua::tostring(state, -1)) : nullptr;
                lua::pop(state);
                if (info) attachInstance(state, 1, *info);
                // With `name`, the table gets the type's metatable even without an instance (no factory, a constructor
                // that failed), as the DSL's shareMeta would give it.
                if (info && lua::isstring(state, 2)) {
                    if (lua::getmetatable(state, 1) == 0) {
                        pushTypeMeta(state, *info);
                        lua::setmetatable(state, 1);
                    } else {
                        lua::pop(state);
                    }
                }
            }
            lua::settop(state, 1);
            return 1;
        },
        "imlui.types.attach");
}

/// tick(now): calls Element::tick(now) on the ticking elements of this state, each synced first, so a Timer
/// on a page that isn't rendered still fires and sees its current property values. ui.__tick calls it at
/// the start of every frame.
/// The loop of libTick. An exception from a tick() is reported, and the other elements still tick.
void tickElements(lua::State* state, double now) {
    for (const std::uint64_t id : Element::tickingElements()) {
        // A handler that ran in this loop may have destroyed this element (or a GC step collected it), and created
        // another one at its address: the id finds only this element, alive and still ticking.
        Element* e = Element::findTicking(id);
        if (!e || e->luaState() != state) continue;
        const int top = lua::gettop(state);
        if (!e->pushSelf()) continue; // the element table is being collected
        // The table is on the stack now, so the element can't be collected while it syncs and ticks.
        Box* b = selfBox(state, top + 1);
        if (b && b->element == e) {
            syncInstance(state, top + 1, *b);
            if (b->element == e && Element::findTicking(id) == e)
                luaui::reportCxx([e, now] { e->tick(now); }, b->type->name(), ".tick()");
        }
        lua::settop(state, top);
    }
}

int libTick(lua::State* state) {
    const double now = lua::tonumber(state, 1);
    luaui::catchCxx(state, [&] { tickElements(state, now); }, "imlui.types.tick");
    return 0;
}

/// tickingCount(): the number of ticking elements of this state. ui.__tick collects garbage after a container
/// dropped children only when there are some (a dropped Timer then stops right away).
int libTickingCount(lua::State* state) {
    lua::Integer n = 0;
    for (const std::uint64_t id : Element::tickingElements())
        if (const Element* e = Element::findTicking(id); e && e->luaState() == state) ++n;
    lua::pushinteger(state, n);
    return 1;
}

/// The instance box of the element table `node`, created now when the table has none yet and its `__type` is a
/// registered type (a table built without the constructor, `{ __type = "Gauge" }`, like at its first sync). nullptr
/// otherwise. Once there is a box, its type is the element's: the type it was created as (also after its name was
/// registered again), found without reading `__type` (a string) and looking it up, which the render path would do
/// several times per element and frame.
Box* instanceBox(const TypeRegistry& registry, const Node& node) {
    lua::State* state = node.state();
    if (Box* b = selfBox(state, node.index())) return b->element ? b : nullptr;
    const TypeInfo* info = registry.find(node.type());
    return info ? attachInstance(state, node.index(), *info) : nullptr;
}

/// instanceBox(), synced.
Box* syncBox(const TypeRegistry& registry, const Node& node, Sync mode) {
    Box* b = instanceBox(registry, node);
    if (!b) return nullptr;
    syncInstance(node.state(), node.index(), *b, mode);
    return b->element ? b : nullptr;
}

} // namespace

TypeRegistry::TypeRegistry() : m_instances{[this](const Node& node) -> ScriptObject* { return renderSync(node); }} {}

TypeInfo& TypeRegistry::addType(std::unique_ptr<TypeInfo> info) {
    TypeInfo& ref = *info;
    m_infos.push_back(std::move(info));
    m_types.insert_or_assign(ref.name(), &ref); // the last registration of a name wins
    return ref;
}

TypeInfo& TypeRegistry::addSingleton(std::string name, ScriptObject& object, std::unique_ptr<TypeInfo> info,
                                     std::shared_ptr<ScriptObject> owner) {
    TypeInfo& ref = *info;
    m_infos.push_back(std::move(info));
    // The singleton this one replaces leaves the installed states: unlinked if it lives on (its notify() no longer
    // reaches them, and it can't be left pointing at a state that closes later), and destroyed here if the registry
    // owned it. Their global sees that through its box (Box::life) and raises on access.
    for (const Singleton& s : m_singletons)
        if (s.name == name && s.object != &object && s.life.alive()) s.object->detach();
    std::erase_if(m_singletons, [&](const Singleton& s) { return s.name == name; });
    auto defaults = std::make_shared<const TypeInfo::Defaults>(ref.captureDefaults(object));
    ScriptObject::Handle life = object.handle();
    m_singletons.push_back({std::move(name), &object, &ref, std::move(owner), std::move(defaults), std::move(life)});
    return ref;
}

const TypeInfo* TypeRegistry::find(std::string_view name) const {
    const auto it = m_types.find(name);
    return it == m_types.end() ? nullptr : it->second;
}

std::vector<std::string> TypeRegistry::typeNames() const {
    std::vector<std::string> out;
    out.reserve(m_types.size());
    for (const auto& [name, info] : m_types)
        out.push_back(name);
    return out;
}

TypeRegistry::Kind TypeRegistry::kind(const Node& node) const {
    // An element with an instance is of its instance's type (see instanceBox), found without reading `__type`.
    const Box* b = selfBox(node.state(), node.index());
    const TypeInfo* info = b && b->element && b->type ? b->type : find(node.type());
    if (!info) return Kind::Unknown;
    return info->visual() ? Kind::Visual : Kind::NonVisual;
}

void TypeRegistry::renderElement(Renderer& renderer, const Node& node) {
    Box* b = syncBox(*this, node, Sync::OncePerFrame);
    if (!b) return;
    Frame frame{renderer, node, &m_instances, b->type->fillsWidth(b->element)};
    b->element->render(frame);
}

std::optional<ImVec2> TypeRegistry::intrinsicSize(Renderer& renderer, const Node& node, float maxWidth) {
    Box* b = instanceBox(*this, node);
    // No instance because its constructor threw: it renders nothing, so its size is known, zero. A layout that waited
    // for it to have a size (a Row's first frame, rendered unseen) would wait, and ask for frames, forever.
    if (!b) return constructionFailed(node.state(), node.index()) ? std::optional{ImVec2{}} : std::nullopt;
    if (!b->type->visual()) return std::nullopt;
    const TypeInfo* info = b->type;
    syncInstance(node.state(), node.index(), *b, Sync::OncePerFrame);
    if (!b->element) return std::nullopt;
    const bool shrink = layout::shrinkToFit();
    if (const auto* known = freshMemo(*b).find(maxWidth, shrink)) return known->size;
    const std::uint64_t changes = luaui::changes();
    const Frame frame{renderer, node, &m_instances, info->fillsWidth(b->element)};
    std::optional<ImVec2> size = b->element->intrinsicSize(frame, maxWidth);
    // Kept unless something reset the memo meanwhile (a sync of this element again, Lua that may change tables).
    if (b->element && b->memo.frame >= 0 && b->memo.changes == changes) b->memo.add({maxWidth, shrink, size});
    return size;
}

bool TypeRegistry::fillsWidth(Renderer& renderer, const Node& node) {
    Box* b = syncBox(*this, node, Sync::OncePerFrame);
    if (!b) {
        const TypeInfo* info = find(node.type()); // no instance (its constructor failed): what the type says
        return info && info->fillsWidth(nullptr);
    }
    if (b->type->fillsWidth(b->element)) return true;
    Box::LayoutMemo& memo = freshMemo(*b);
    if (memo.fills) return *memo.fills;
    const std::uint64_t changes = luaui::changes();
    const Frame frame{renderer, node, &m_instances, false};
    const bool fills = b->element->contentFillsWidth(frame);
    if (b->element && b->memo.frame >= 0 && b->memo.changes == changes) b->memo.fills = fills;
    return fills;
}

std::uint64_t TypeRegistry::elementId(const Node& node) {
    lua::checkstack(node.state(), 4);
    const Box* b = instanceBox(*this, node);
    return b ? b->element->objectId() : 0;
}

Element* TypeRegistry::sync(const Node& node) const {
    Box* b = syncBox(*this, node, Sync::Always);
    return b ? b->element : nullptr;
}

Element* TypeRegistry::renderSync(const Node& node) const {
    Box* b = syncBox(*this, node, Sync::OncePerFrame);
    return b ? b->element : nullptr;
}

void TypeRegistry::install(lua::State* state) {
    lua::checkstack(state, 16);
    const int top = lua::gettop(state);

    // Box metatables.
    lua::createtable(state, 0, 4);
    lua::pushcfunction(state, &instanceGc);
    lua::setfield(state, -2, "__gc");
    lua::pushcfunction(state, &boxGet);
    lua::setfield(state, -2, "__get");
    lua::pushboolean(state, true);
    lua::setfield(state, -2, markerKey);
    lua::pushstring(state, "imlui.instance");
    lua::setfield(state, -2, "__name");
    lua::setfield(state, lua::registryIndex, instanceMetaKey);

    lua::createtable(state, 0, 7);
    lua::pushcfunction(state, &singletonGc);
    lua::setfield(state, -2, "__gc");
    lua::pushcfunction(state, &singletonIndex);
    lua::setfield(state, -2, "__index");
    lua::pushcfunction(state, &singletonNewIndex);
    lua::setfield(state, -2, "__newindex");
    lua::pushcfunction(state, &boxGet);
    lua::setfield(state, -2, "__get");
    lua::pushcfunction(state, &singletonToString);
    lua::setfield(state, -2, "__tostring");
    lua::pushboolean(state, true);
    lua::setfield(state, -2, markerKey);
    lua::setfield(state, lua::registryIndex, singletonMetaKey);

    // Per-type methods tables, and element-table metatables that fall back to them. (A type registered later
    // gets them on first use.)
    for (const auto& [name, t] : m_types) {
        pushTypeMeta(state, *t);
        lua::pop(state);
    }
    pushRegistryTable(state, methodsKey);
    for (const Singleton& s : m_singletons) {
        pushMethodsTable(state, *s.type);
        lua::rawsetp(state, top + 1, s.type);
    }
    lua::settop(state, top);

    // require("imlui.types")
    lua::createtable(state, 0, 7);
    const lua::CFunction lib[] = {&libNames, &libIsType, &libAttach, &libMeta, &libTick, &libTickingCount};
    const char* libNamesList[] = {"names", "isType", "attach", "meta", "tick", "tickingCount"};
    for (std::size_t i = 0; i < std::size(lib); ++i) {
        lua::pushlightuserdata(state, this);
        lua::pushcclosure(state, lib[i], 1);
        lua::setfield(state, -2, libNamesList[i]);
    }
    pushParents(state);
    lua::setfield(state, -2, "parents");
    lua::getglobal(state, "package");
    lua::getfield(state, -1, "loaded");
    lua::pushvalue(state, -3);
    lua::setfield(state, -2, "imlui.types");
    lua::settop(state, top);

    // Singletons: a global userdata each, kept alive by the registry as well.
    pushRegistryTable(state, singletonsKey);
    for (const Singleton& s : m_singletons) {
        Box* b = new (lua::newuserdata(state, sizeof(Box), 1)) Box{};
        const bool alive = s.life.alive(); // a by-reference singleton destroyed already: its global raises on access
        b->object = alive ? s.object : nullptr;
        b->type = s.type;
        b->defaults = s.defaults;
        b->life = s.life;
        lua::getfield(state, lua::registryIndex, singletonMetaKey);
        lua::setmetatable(state, -2);
        lua::newtable(state); // signal handlers
        lua::setiuservalue(state, -2, 1);
        lua::pushvalue(state, -1);
        lua::setfield(state, top + 1, s.name.c_str());
        lua::pushvalue(state, -1);
        lua::setglobal(state, s.name.c_str());
        if (alive) s.object->attach(state, -1, true);
        lua::pop(state);
    }
    lua::settop(state, top);

    // Detach the singletons when this state closes, and stop recognizing its boxes by their metatables (boxMetas).
    std::array<void*, 2> metas{};
    for (std::size_t i = 0; i < metas.size(); ++i) {
        lua::getfield(state, lua::registryIndex, i == 0 ? instanceMetaKey : singletonMetaKey);
        metas[i] = const_cast<void*>(lua::topointer(state, -1));
        lua::pop(state);
    }
    lua::newuserdata(state, 1);
    lua::createtable(state, 0, 1);
    lua::pushlightuserdata(state, this);
    lua::pushlightuserdata(state, metas[0]);
    lua::pushlightuserdata(state, metas[1]);
    lua::pushcclosure(state, &sentinelGc, 3);
    lua::setfield(state, -2, "__gc");
    lua::setmetatable(state, -2);
    lua::setfield(state, lua::registryIndex, sentinelKey);
    lua::settop(state, top);
    // Last: only metatables a sentinel will remove again are tracked.
    for (void* meta : metas)
        if (meta) boxMetas.push_back(meta);
}

void TypeRegistry::activate(lua::State* state) {
    for (const Singleton& s : m_singletons)
        if (s.life.alive()) s.object->relink(state);
}

void TypeRegistry::detachState(lua::State* state) {
    for (const Singleton& s : m_singletons)
        if (s.life.alive() && s.object->luaState() == state) s.object->detach();
}

} // namespace imlui
