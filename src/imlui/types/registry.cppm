// TypeRegistry: owns the registered C++ types and singletons and binds them to Lua.
//
// install(state) runs for every new Lua state (each hot reload) before the DSL loads, and sets up:
//
//  * `require("imlui.types")`: { names(), isType(name), attach(element) }. The DSL's constructors call
//    attach() for registered types, which creates the C++ instance right away and stores it in the
//    element table as `__native`, a full userdata whose __gc deletes the instance. The table gets a
//    metatable whose __index is the type's methods table, so `element:reset()` works. The instance
//    holds only a weak link back to the table, so collecting the table destroys the instance.
//  * one global per singleton: a userdata whose __index reads properties (getters) and methods, and
//    whose __newindex writes properties (setters) and stores `on*` signal handlers.
//  * a sentinel whose __gc detaches the singletons when the state closes.
//
// Every registered element type is also a Renderer element whose handler syncs the declared
// properties from the element table into the instance (setters run only on a changed value), calls
// componentComplete() once, and then Element::render(). Main thread only.
module;

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module imlui.types.registry;

import lua;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.ui.renderer;
import imlui.types.object;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_info;
import imlui.types.type_builder;

export namespace imlui {

class TypeRegistry {
public:
    /// Registered element types become elements of `renderer`.
    explicit TypeRegistry(Renderer& renderer);
    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

    /// Registers element type T as `name` (replacing an earlier type of that name). T derives from
    /// Element, is default-constructible, and may have `static void declare(TypeBuilder<T>&)`.
    /// Takes effect for Lua states installed afterwards (the next hot reload, or the first load).
    template <class T>
        requires std::derived_from<T, Element> && std::default_initializable<T>
    TypeInfo& registerType(std::string name) {
        auto info = std::make_unique<TypeInfo>(std::move(name));
        info->setFactory([] { return std::unique_ptr<Element>{std::make_unique<T>()}; });
        if constexpr (requires(TypeBuilder<T>& b) { T::declare(b); }) {
            TypeBuilder<T> builder{*info};
            T::declare(builder);
        }
        return addType(std::move(info));
    }

    /// Exposes `object` to Lua as the global `name`. The object isn't owned and must outlive every Lua
    /// state the registry installs into (in practice: the Engine's run()).
    template <class T>
        requires std::derived_from<T, Object>
    TypeInfo& registerSingleton(std::string name, T& object) {
        auto info = std::make_unique<TypeInfo>(name);
        if constexpr (requires(TypeBuilder<T>& b) { T::declare(b); }) {
            TypeBuilder<T> builder{*info};
            T::declare(builder);
        }
        return addSingleton(std::move(name), object, std::move(info));
    }

    /// Installs the types and singletons into a new Lua state (before lua/imlui.lua is loaded).
    void install(lua::State* state);
    /// Points the singletons back at `state`, which was installed before (after a failed reload).
    void activate(lua::State* state);
    /// Detaches the singletons linked to `state`. Runs automatically when an installed state closes.
    void detachState(lua::State* state);

    [[nodiscard]] const TypeInfo* find(std::string_view name) const;
    /// Registered element type names, sorted (what `ui.types()` returns).
    [[nodiscard]] std::vector<std::string> typeNames() const;

    /// The instance of the element table `node`, created now if the table has none yet. Then syncs
    /// the declared properties into it. nullptr if `node` isn't a registered type.
    Element* sync(const luaui::Node& node) const;

private:
    struct Singleton {
        std::string name;
        Object* object;
        const TypeInfo* type;
    };

    TypeInfo& addType(std::unique_ptr<TypeInfo> info);
    TypeInfo& addSingleton(std::string name, Object& object, std::unique_ptr<TypeInfo> info);

    Renderer& m_renderer;
    /// Never shrinks: Lua states hold raw pointers to these until they close.
    std::vector<std::unique_ptr<TypeInfo>> m_infos;
    std::vector<const TypeInfo*> m_types; ///< current element types (a re-registration replaces)
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
constexpr const char* singletonsKey = "imlui.singletons";
constexpr const char* sentinelKey = "imlui.types.sentinel";
constexpr const char* markerKey = "__imlui";

/// The userdata behind `__native` and behind a singleton global.
struct Box {
    Object* object = nullptr;
    Element* element = nullptr; ///< set for owned element instances
    const TypeInfo* type = nullptr;
    bool owned = false;
    bool complete = false; ///< componentComplete() was called
};

void* asVoid(const void* p) { return const_cast<void*>(p); }

/// The Box at `idx`, or nullptr if the value isn't one of ours.
Box* boxAt(lua::State* state, int idx) {
    if (lua::type(state, idx) != lua::Type::UserData || lua::getmetatable(state, idx) == 0) return nullptr;
    lua::pushstring(state, markerKey);
    lua::rawget(state, -2);
    const bool ours = lua::toboolean(state, -1);
    lua::pop(state, 2);
    return ours ? static_cast<Box*>(lua::touserdata(state, idx)) : nullptr;
}

/// The Box of an element table (`__native`) or of a singleton userdata.
Box* selfBox(lua::State* state, int idx) {
    if (lua::istable(state, idx)) {
        lua::pushstring(state, "__native");
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
    if (!b || !b->owned || !b->element) return 0;
    Element* e = b->element;
    b->element = nullptr;
    b->object = nullptr;
    e->detach();
    e->destroyed();
    delete e;
    return 0;
}

/// Pushes an error message for a missing property, or the property's value. True on error.
bool pushProperty(lua::State* state, const Box& b, std::string_view key) {
    const TypeInfo::Property* p = b.type->findProperty(key);
    if (!p) {
        lua::pushstring(state, b.type->name() + " has no property '" + std::string{key} + "'");
        return true;
    }
    p->push(state, *b.object);
    return false;
}

/// __get(box, name): the getter's value. Object::notify reads the current value through it.
int boxGet(lua::State* state) {
    Box* b = boxAt(state, 1);
    if (!b || !b->object) lua::error(state, "__get: not a live imlui object");
    if (pushProperty(state, *b, lua::tostring(state, 2))) lua::raise(state);
    return 1;
}

/// Upvalues: TypeInfo*, method index. Arg 1 is the element table or singleton userdata.
int methodTrampoline(lua::State* state) {
    const auto* info = static_cast<const TypeInfo*>(lua::touserdata(state, lua::upvalueindex(1)));
    const auto index = static_cast<std::size_t>(lua::tointeger(state, lua::upvalueindex(2)));
    Box* b = selfBox(state, 1);
    if (!b || b->type != info || !b->object) lua::error(state, "method called without its object (use obj:method())");
    const int n = info->methods()[index].call(state, *b->object, 2);
    if (n < 0) lua::raise(state);
    return n;
}

/// singleton.key
int singletonIndex(lua::State* state) {
    Box* b = boxAt(state, 1);
    if (!b || !b->object) return 0;
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

/// Does the work of singleton.key = value. Pushes an error message and returns true on error.
bool assignSingleton(lua::State* state, Box& b) {
    const std::string key{lua::tostring(state, 2)};
    if (const TypeInfo::Property* p = b.type->findProperty(key)) {
        if (!p->assign) {
            lua::pushstring(state, b.type->name() + "." + key + " is read-only");
            return true;
        }
        if (p->assign(state, 3, *b.object) == TypeInfo::Assign::TypeMismatch) {
            lua::pushstring(state, b.type->name() + "." + key + ": " + std::string{p->typeName} + " expected, got " +
                                       lua::typeName(state, 3));
            return true;
        }
        return false;
    }
    if (key.starts_with("on") && (lua::isfunction(state, 3) || lua::isnil(state, 3))) {
        lua::getiuservalue(state, 1, 1);
        lua::pushvalue(state, 2);
        lua::pushvalue(state, 3);
        lua::rawset(state, -3);
        lua::pop(state);
        return false;
    }
    lua::pushstring(state, b.type->name() + " has no property '" + key + "'" +
                               (key.starts_with("on") ? " (signal handlers must be functions)" : ""));
    return true;
}

int singletonNewIndex(lua::State* state) {
    Box* b = boxAt(state, 1);
    if (!b || !b->object) lua::error(state, "singleton is gone");
    if (lua::type(state, 2) != lua::Type::String) lua::error(state, "singleton keys are strings");
    if (assignSingleton(state, *b)) lua::raise(state);
    return 0;
}

int singletonToString(lua::State* state) {
    Box* b = boxAt(state, 1);
    lua::pushstring(state, b && b->type ? "singleton " + b->type->name() : std::string{"singleton"});
    return 1;
}

int sentinelGc(lua::State* state) {
    auto* registry = static_cast<TypeRegistry*>(lua::touserdata(state, lua::upvalueindex(1)));
    registry->detachState(state);
    return 0;
}

// ------------------------------------------------------------ instances

/// Creates the instance for the element table at `tbl`. Returns its box (nullptr on failure).
Box* attachInstance(lua::State* state, int tbl, const TypeInfo& info) {
    tbl = lua::absindex(state, tbl);
    lua::checkstack(state, 8);
    if (Box* existing = selfBox(state, tbl)) return existing;
    if (!info.factory()) return nullptr;

    // The userdata first, so a Lua memory error can't leak a constructed instance.
    Box* b = new (lua::newuserdata(state, sizeof(Box))) Box{};
    const int ud = lua::gettop(state);
    lua::getfield(state, lua::registryIndex, instanceMetaKey);
    lua::setmetatable(state, ud);
    try {
        b->element = info.factory()().release();
    } catch (const std::exception& e) {
        luaui::reportError(info.name() + ": constructor failed: " + e.what());
    }
    if (!b->element) {
        lua::pop(state);
        return nullptr;
    }
    b->object = b->element;
    b->type = &info;
    b->owned = true;

    lua::pushstring(state, "__native");
    lua::pushvalue(state, ud);
    lua::rawset(state, tbl);
    if (lua::getmetatable(state, tbl) == 0) {
        pushPerType(state, typeMetaKey, &info);
        lua::setmetatable(state, tbl);
    } else {
        lua::pop(state); // the table has its own metatable: keep it (methods unreachable via `:`)
    }
    b->element->attach(state, tbl, false);
    lua::pop(state); // ud
    return b;
}

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

TypeRegistry* upvalueRegistry(lua::State* state) {
    return static_cast<TypeRegistry*>(lua::touserdata(state, lua::upvalueindex(1)));
}

int libNames(lua::State* state) {
    const std::vector<std::string> names = upvalueRegistry(state)->typeNames();
    lua::createtable(state, static_cast<int>(names.size()), 0);
    lua::Integer i = 0;
    for (const auto& n : names) {
        lua::pushstring(state, n);
        lua::rawseti(state, -2, ++i);
    }
    return 1;
}

int libIsType(lua::State* state) {
    lua::pushboolean(state, lua::isstring(state, 1) && upvalueRegistry(state)->find(lua::tostring(state, 1)));
    return 1;
}

/// attach(element): creates the C++ instance of a registered element table. Returns the table.
int libAttach(lua::State* state) {
    if (lua::istable(state, 1)) {
        lua::getfield(state, 1, "__type");
        const TypeInfo* info = lua::isstring(state, -1) ? upvalueRegistry(state)->find(lua::tostring(state, -1)) : nullptr;
        lua::pop(state);
        if (info) attachInstance(state, 1, *info);
    }
    lua::settop(state, 1);
    return 1;
}

Box* syncBox(const Node& node, const TypeInfo& info) {
    lua::State* state = node.state();
    Box* b = selfBox(state, node.index());
    if (!b) b = attachInstance(state, node.index(), info);
    if (!b || !b->element) return nullptr;
    if (b->type != &info) {
        luaui::reportError("element of type '" + info.name() + "' carries an instance of '" + b->type->name() + "'");
        return nullptr;
    }
    for (const auto& p : info.properties()) {
        if (!p.assign) continue;
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
            lua::pop(state); // unset: keep the C++ value
            continue;
        }
        if (p.assign(state, -1, *b->object) == TypeInfo::Assign::TypeMismatch)
            luaui::reportError(info.name() + "." + p.name + ": " + std::string{p.typeName} + " expected, got " +
                               lua::typeName(state, -1));
        lua::pop(state);
    }
    return b;
}

void renderTyped(const TypeInfo& info, Renderer& renderer, const Node& node) {
    Box* b = syncBox(node, info);
    if (!b) return;
    Element* e = b->element;
    if (!b->complete) {
        b->complete = true;
        e->componentComplete();
    }
    Frame frame{renderer, node};
    e->render(frame);
}

} // namespace

TypeRegistry::TypeRegistry(Renderer& renderer) : m_renderer{renderer} {}

TypeInfo& TypeRegistry::addType(std::unique_ptr<TypeInfo> info) {
    TypeInfo& ref = *info;
    m_infos.push_back(std::move(info));
    std::erase_if(m_types, [&](const TypeInfo* t) { return t->name() == ref.name(); });
    m_types.push_back(&ref);
    m_renderer.registerElement(
        ref.name(), [&ref](Renderer& r, const Node& n) { renderTyped(ref, r, n); }, ref.visual());
    return ref;
}

TypeInfo& TypeRegistry::addSingleton(std::string name, Object& object, std::unique_ptr<TypeInfo> info) {
    TypeInfo& ref = *info;
    m_infos.push_back(std::move(info));
    std::erase_if(m_singletons, [&](const Singleton& s) { return s.name == name; });
    m_singletons.push_back({std::move(name), &object, &ref});
    return ref;
}

const TypeInfo* TypeRegistry::find(std::string_view name) const {
    for (const TypeInfo* t : m_types)
        if (t->name() == name) return t;
    return nullptr;
}

std::vector<std::string> TypeRegistry::typeNames() const {
    std::vector<std::string> out;
    out.reserve(m_types.size());
    for (const TypeInfo* t : m_types) out.push_back(t->name());
    std::sort(out.begin(), out.end());
    return out;
}

Element* TypeRegistry::sync(const Node& node) const {
    const TypeInfo* info = find(node.type());
    if (!info) return nullptr;
    Box* b = syncBox(node, *info);
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

    lua::createtable(state, 0, 6);
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

    // Per-type methods tables, and element-table metatables that fall back to them.
    pushRegistryTable(state, methodsKey);
    pushRegistryTable(state, typeMetaKey);
    for (const TypeInfo* t : m_types) {
        pushMethodsTable(state, *t);
        lua::pushvalue(state, -1);
        lua::rawsetp(state, top + 1, t);
        lua::createtable(state, 0, 1);
        lua::insert(state, -2);
        lua::setfield(state, -2, "__index");
        lua::rawsetp(state, top + 2, t);
    }
    for (const Singleton& s : m_singletons) {
        pushMethodsTable(state, *s.type);
        lua::rawsetp(state, top + 1, s.type);
    }
    lua::settop(state, top);

    // require("imlui.types")
    lua::createtable(state, 0, 3);
    const lua::CFunction lib[] = {&libNames, &libIsType, &libAttach};
    const char* libNamesList[] = {"names", "isType", "attach"};
    for (std::size_t i = 0; i < std::size(lib); ++i) {
        lua::pushlightuserdata(state, this);
        lua::pushcclosure(state, lib[i], 1);
        lua::setfield(state, -2, libNamesList[i]);
    }
    lua::getglobal(state, "package");
    lua::getfield(state, -1, "loaded");
    lua::pushvalue(state, -3);
    lua::setfield(state, -2, "imlui.types");
    lua::settop(state, top);

    // Singletons: a global userdata each, kept alive by the registry as well.
    pushRegistryTable(state, singletonsKey);
    for (const Singleton& s : m_singletons) {
        Box* b = new (lua::newuserdata(state, sizeof(Box), 1)) Box{};
        b->object = s.object;
        b->type = s.type;
        lua::getfield(state, lua::registryIndex, singletonMetaKey);
        lua::setmetatable(state, -2);
        lua::newtable(state); // signal handlers
        lua::setiuservalue(state, -2, 1);
        lua::pushvalue(state, -1);
        lua::setfield(state, top + 1, s.name.c_str());
        lua::pushvalue(state, -1);
        lua::setglobal(state, s.name.c_str());
        s.object->attach(state, -1, true);
        lua::pop(state);
    }
    lua::settop(state, top);

    // Detach the singletons when this state closes.
    lua::newuserdata(state, 1);
    lua::createtable(state, 0, 1);
    lua::pushlightuserdata(state, this);
    lua::pushcclosure(state, &sentinelGc, 1);
    lua::setfield(state, -2, "__gc");
    lua::setmetatable(state, -2);
    lua::setfield(state, lua::registryIndex, sentinelKey);
    lua::settop(state, top);
}

void TypeRegistry::activate(lua::State* state) {
    for (const Singleton& s : m_singletons) s.object->relink(state);
}

void TypeRegistry::detachState(lua::State* state) {
    for (const Singleton& s : m_singletons)
        if (s.object->luaState() == state) s.object->detach();
}

} // namespace imlui
