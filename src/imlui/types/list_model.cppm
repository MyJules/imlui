// ListModel: rows that live in C++ (log lines, query results, a file list), handed to Lua lazily, Qt-style. A
// Repeater's or ListView's `model` takes one like a Lua list: `#model` is count() (no row is converted for it),
// `model[i]` converts row i (1-based) with pushItem() when it's first asked for, and changes are row-level
// notifications instead of a new list that the prelude has to compare item by item.
//
//   class Logs : public imlui::ListModel {           // or imlui::VectorModel<LogLine> (imlui.types.vector_model)
//       std::size_t count() const override;
//       void pushItem(lua::State* s, std::size_t row) const override;   // pushes exactly one value
//   };
//   m_logs.rowsInserted(first, n);                   // main thread, after the change
//
// Each model has one Lua value per Lua state: a full userdata (`ListModel<T>` in the Lua definitions) whose __index
// gives the rows, __len the count, and the methods count, key, revision, hasKeys and set (a write, when the model
// takes one: setFromLua). The value holds no strong
// reference to the model (unless it was pushed from a std::shared_ptr): a model that dies reads as empty from then on.
// Nothing in C++ points into a Lua state, so a model may outlive any number of hot reloads.
//
// Row identity. The model gives every row an id (a number of its own, never reused) and keeps it while the row only
// moves: an insertion or removal above it, rowsMoved, rowsRemapped. rowsChanged and reset give rows new ids. Each Lua
// value caches the items it converted by row id (weak values), so an unchanged row reads as the same item table for
// as long as something (a delegate) holds it, wherever the row moved: that's what lets the prelude keep delegates
// (an unkeyed delegate by index, a keyed one by key, both while their item is the same). The ids are kept only once
// Lua read a row: a model nobody displayed costs nothing per row.
//
// Signals. Each notification is also a Lua signal (onRowsInserted(first, count), 1-based, ...: see signalNames) that
// scripts listen to with `connect(model, "onRowsInserted", fn)`: the states where a script connected to a model are
// listed (each leaves the list as it closes), and a notification calls the listeners of the model's value in each,
// right after the change. A model nobody connected to costs a flag check.
//
// The change log. changesSince(revision) gives the notifications after a revision, for a consumer that keeps its own
// per-row state (a ListView's measured heights) and follows the rows as they move instead of by index. The log starts
// with the first call and keeps the last maxLoggedChanges notifications.
//
// Main thread only: count(), pushItem(), key(), the notifications and pushing. A worker hands its rows back with
// runAsync's `done` or handle().post (or VectorModel::appender()).
export module imlui.types.list_model;

import std;

import lua;
import imlui.lua.errors;
import imlui.lua.listeners;
import imlui.types.lua_convert;
import imlui.types.lua_types;
import imlui.types.script_object;
import imlui.ui.frame_request;

export namespace imlui {

class ListModel : public ScriptObject {
public:
    /// A row's stable identity for keyed delegates: an integer or a string.
    using Key = std::variant<std::int64_t, std::string>;

    ListModel();
    ~ListModel() override;
    ListModel(const ListModel&) = delete;
    ListModel& operator=(const ListModel&) = delete;
    ListModel(ListModel&&) = delete;
    ListModel& operator=(ListModel&&) = delete;

    /// The number of rows.
    [[nodiscard]] virtual std::size_t count() const = 0;
    /// Pushes exactly one value, row `row`'s item (0-based, < count()), usually a struct through LuaConvert. Called
    /// only when Lua reads a row it hasn't cached. Must not call into Lua scripts. An exception becomes a Lua error at
    /// the read.
    virtual void pushItem(lua::State* state, std::size_t row) const = 0;
    /// Whether key() identifies the rows: the containers then keep delegates by key without a Lua `key` function.
    [[nodiscard]] virtual bool hasKeys() const;
    /// Row `row`'s key (0-based, < count()); nullopt when it has none (the row's item is its key then).
    [[nodiscard]] virtual std::optional<Key> key(std::size_t row) const;
    /// `model:set(i, v)` from Lua: replaces row `row` (0-based, < count()) with the Lua value at `idx`, and notifies.
    /// Throws (a Lua error at the call) for a value that doesn't convert or a model that takes no writes: the default
    /// throws "the model is read-only". VectorModel<T> takes them once setScriptWritable(true) allowed it.
    virtual void setFromLua(lua::State* state, std::size_t row, int idx);

    // ---- Change notifications. Main thread, right after the change (count() already says the new count). Each one
    // bumps revision() and requests a frame; bindings read the model again on it.

    /// Rows [first, first + count) are new.
    void rowsInserted(std::size_t first, std::size_t count);
    /// The `count` rows that were at [first, first + count) are gone.
    void rowsRemoved(std::size_t first, std::size_t count);
    /// Rows [first, first + count) have new contents: their items are converted again, their delegates rebuilt.
    void rowsChanged(std::size_t first, std::size_t count);
    /// The `count` rows at [first, first + count) moved, unchanged, to start at `destination`, an index in the model
    /// after the move (destination + count <= count()).
    void rowsMoved(std::size_t first, std::size_t count, std::size_t destination);
    /// Every row was replaced (a sort, a refreshed query): new row r is old row `previous[r]` unchanged, wherever it
    /// was (its item and delegate are kept), or a new row (nullopt). `previous` has count() entries; an old row named
    /// twice is kept once (the second is new).
    void rowsRemapped(std::span<const std::optional<std::size_t>> previous);
    /// Anything may have changed: every row is new.
    void reset();

    /// Counts the notifications (and the bookkeeping repairs, see the module comment): the prelude's containers skip
    /// their work on a model whose revision didn't change.
    [[nodiscard]] std::uint64_t revision() const;

    /// One notification, as changesSince() gives it. Indices are 0-based, as in the notification.
    struct Change {
        enum class Kind { Inserted, Removed, Changed, Moved, Remapped, Reset };
        Kind kind = Kind::Reset;
        std::size_t first = 0;
        std::size_t count = 0;
        std::size_t destination = 0; ///< Moved
        /// Remapped: rowsRemapped's `previous` (new row r was old row previous[r]). Only the latest Remapped in the log
        /// keeps it: an older one reads as a Reset.
        std::shared_ptr<const std::vector<std::optional<std::size_t>>> previous;
    };
    /// How many notifications the change log keeps.
    static constexpr std::size_t maxLoggedChanges = 64;
    /// The notifications after revision `since` (a revision() read earlier), oldest first, each one revision; a repair
    /// of the bookkeeping (a missed notification) is a Reset. nullopt when the log doesn't hold them all: `since` is
    /// older than the log (the first call starts it, more than maxLoggedChanges came since) or isn't a revision.
    [[nodiscard]] std::optional<std::span<const Change>> changesSince(std::uint64_t since) const;

    // ---- Lua

    /// Pushes this model's Lua value in `state`: the same one each time (per state, while Lua holds it). It doesn't
    /// keep the model alive: once the model is destroyed, it reads as an empty model. LuaConvert uses it for a model
    /// (a getter returning a reference) or a pointer to one.
    void pushLua(lua::State* state) const;
    /// The same, keeping `model` alive while Lua holds the value (a method that returns a new model). Null: nil.
    static void pushShared(lua::State* state, std::shared_ptr<ListModel> model);
    /// The live model of the Lua value at `idx`; nullptr for another value or a model that is gone.
    [[nodiscard]] static ListModel* fromLua(lua::State* state, int idx);

    /// Row `row`'s id (0-based, < count()). Starts the bookkeeping on the first call. Used by the Lua value.
    [[nodiscard]] std::uint64_t rowId(std::size_t row) const;

    /// The Lua signals of the notifications, in Change::Kind order: onRowsInserted(first, count),
    /// onRowsRemoved(first, count), onRowsChanged(first, count), onRowsMoved(first, count, destination) (1-based,
    /// `destination` an index after the move, as in rowsMoved), onRowsRemapped() and onReset().
    static constexpr std::array<std::string_view, 6> signalNames{"onRowsInserted", "onRowsRemoved",  "onRowsChanged",
                                                                 "onRowsMoved",    "onRowsRemapped", "onReset"};

    /// `connect(model, signal, fn)` (the prelude): whether `signal` is one of signalNames; then this model calls the
    /// listeners in `state` from now on.
    [[nodiscard]] bool listen(lua::State* state, std::string_view signal) const;

private:
    /// A notification's end: logs `change`, moves the revision, requests a frame, calls the Lua listeners.
    void changed(Change change);
    /// Calls the listeners of `change`'s signal in every state that has some (see listen()).
    void emitChange(const Change& change) const;
    /// Logs a change (one revision), when the log was started.
    void record(Change change) const;
    /// New ids for every row (count() of them). `repair`: rows Lua may have cached get new ids, so the revision moves.
    void renumber(bool repair = true) const;
    /// Whether the bookkeeping holds `size` rows (else it's renumbered: a notification that doesn't match).
    [[nodiscard]] bool tracking(std::size_t size) const;

    mutable std::vector<std::uint64_t> m_rowIds; ///< per row; kept once m_tracked
    mutable std::uint64_t m_nextRowId = 1;
    mutable bool m_tracked = false;
    mutable std::uint64_t m_revision = 0;
    mutable std::vector<Change> m_log;    ///< the last notifications, once m_logging
    mutable std::uint64_t m_logStart = 0; ///< the revision before m_log's first entry
    mutable bool m_logging = false;
    mutable bool m_listened = false; ///< a script connected to a signal (listen())
};

/// A model as a property value or method result (a getter returning `const VectorModel<T>&`, a method returning
/// `ListModel&`): its Lua value (ListModel::pushLua). `ListModel<T>` in the Lua definitions, T from `M::Item` when the
/// model type has one (VectorModel<T>), else `any`.
template <class M>
    requires std::derived_from<M, ListModel>
struct LuaConvert<M> {
    static constexpr std::string_view name = "ListModel";
    static std::string luaType(LuaTypes& types) {
        if constexpr (requires { typename M::Item; } && LuaPushable<typename M::Item>)
            return "ListModel<" + luaTypeOf<typename M::Item>(types) + ">";
        else
            return "ListModel<any>";
    }
    static void push(lua::State* s, const M& model) { model.pushLua(s); }
};

/// A pointer to a model: its Lua value, nil for nullptr.
template <class M>
    requires std::derived_from<M, ListModel>
struct LuaConvert<M*> {
    static constexpr std::string_view name = "ListModel";
    static std::string luaType(LuaTypes& types) { return LuaConvert<M>::luaType(types) + "?"; }
    static void push(lua::State* s, const M* model) {
        if (model)
            model->pushLua(s);
        else
            lua::pushnil(s);
    }
};

/// A shared model (a method that returns a new one): its Lua value, which keeps it alive while Lua holds it; nil for
/// nullptr.
template <class M>
    requires std::derived_from<M, ListModel>
struct LuaConvert<std::shared_ptr<M>> {
    static constexpr std::string_view name = "ListModel";
    static std::string luaType(LuaTypes& types) { return LuaConvert<M>::luaType(types) + "?"; }
    static void push(lua::State* s, const std::shared_ptr<M>& model) {
        ListModel::pushShared(s, std::static_pointer_cast<ListModel>(model));
    }
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Registry keys: the weak-valued table { [objectId] = Lua value } of the state's models, the values' metatable, and
/// the metatable of their item caches ({ __mode = "v" }).
constexpr const char* valuesKey = "imlui.listmodels";
constexpr const char* metaKey = "imlui.listmodel";
constexpr const char* cacheMetaKey = "imlui.listmodel.cache";

/// The userdata behind a model's Lua value. Its user value 1 is the item cache, { [row id] = item } (weak values).
struct ModelRef {
    const ListModel* model = nullptr;
    ScriptObject::Handle life;        ///< the model's: alive() until it is destroyed
    std::shared_ptr<ListModel> owner; ///< set when pushed from a shared_ptr
};

/// Pushes the state's table of model values, { [objectId] = value } with weak values, made on first use.
void pushValues(lua::State* state) {
    if (lua::getfield(state, lua::registryIndex, valuesKey) == lua::Type::Table) return;
    lua::pop(state);
    lua::newtable(state);
    lua::createtable(state, 0, 1);
    lua::pushcstring(state, "v");
    lua::setfield(state, -2, "__mode");
    lua::setmetatable(state, -2);
    lua::pushvalue(state, -1);
    lua::setfield(state, lua::registryIndex, valuesKey);
}

/// Pushes the item caches' metatable, { __mode = "v" }, made on first use.
void pushCacheMeta(lua::State* state) {
    if (lua::getfield(state, lua::registryIndex, cacheMetaKey) == lua::Type::Table) return;
    lua::pop(state);
    lua::createtable(state, 0, 1);
    lua::pushcstring(state, "v");
    lua::setfield(state, -2, "__mode");
    lua::pushvalue(state, -1);
    lua::setfield(state, lua::registryIndex, cacheMetaKey);
}

/// The tag whose address a model value holds in its first bytes, before its ModelRef. A value is recognized by its
/// size and this tag, like the registry's boxes (boxAt), not by its metatable: a script can reach the metatable's __gc
/// (debug.getmetatable) and call it on anything, and `debug.setmetatable` gives any userdata the metatable, but no Lua
/// code can write this address into a userdata's bytes. It also reads no table: refAt runs for every `#model`.
constexpr char modelTag = 0;
constexpr std::size_t refOffset = std::max(sizeof(const void*), alignof(ModelRef));
constexpr std::size_t refUserdataSize = refOffset + sizeof(ModelRef);
// Lua aligns a userdata's memory for LUAI_MAXALIGN: 8 bytes.
static_assert(alignof(ModelRef) <= 8 && refOffset % alignof(ModelRef) == 0);

/// Pushes a new model value's userdata (with its user value for the item cache) and returns its empty ModelRef.
ModelRef* newRef(lua::State* state) {
    auto* p = static_cast<std::byte*>(lua::newuserdata(state, refUserdataSize, 1));
    const void* tag = &modelTag;
    std::memcpy(p, &tag, sizeof tag);
    return std::construct_at(static_cast<ModelRef*>(static_cast<void*>(p + refOffset)));
}

/// The ModelRef at `idx`, or nullptr when the value isn't a model's.
ModelRef* refAt(lua::State* state, int idx) {
    if (lua::type(state, idx) != lua::Type::UserData ||
        lua::rawlen(state, idx) != static_cast<lua::Integer>(refUserdataSize))
        return nullptr;
    auto* p = static_cast<std::byte*>(lua::touserdata(state, idx));
    const void* tag = nullptr;
    std::memcpy(&tag, p, sizeof tag); // a foreign userdata of this size holds anything: read its first bytes as bytes
    return tag == &modelTag ? std::launder(static_cast<ModelRef*>(static_cast<void*>(p + refOffset))) : nullptr;
}

/// The live model of the value at `idx` (a method's self); nullptr when it is gone. Raises for another value.
const ListModel* selfModel(lua::State* state, int idx) {
    ModelRef* ref = refAt(state, idx);
    if (!ref) lua::error(state, "ListModel method called without its model (use model:method())");
    if (ref->model && !ref->life.alive()) ref->model = nullptr;
    return ref->model;
}

/// The 0-based row of the key at `idx` for `model`, or nullopt: an integral number 1..count().
std::optional<std::size_t> rowOf(lua::State* state, int idx, const ListModel& model) {
    lua::Integer i = 0;
    if (lua::isinteger(state, idx)) {
        i = lua::tointeger(state, idx);
    } else {
        if (lua::type(state, idx) != lua::Type::Number) return std::nullopt;
        const double d = lua::tonumber(state, idx);
        if (!(d >= 1.0 && d <= 9.0e15) || std::floor(d) != d) return std::nullopt;
        i = static_cast<lua::Integer>(d);
    }
    if (i < 1 || static_cast<std::uint64_t>(i) > model.count()) return std::nullopt;
    return static_cast<std::size_t>(i - 1);
}

/// Pushes row `row`'s item through the cache of the value at 1. Collectable items (tables, userdata, functions) are
/// cached by row id; strings, numbers and booleans compare by value anyway (and a weak table would never drop them).
void pushRow(lua::State* state, const ListModel& model, std::size_t row) {
    lua::checkstack(state, 4);
    const auto id = static_cast<lua::Integer>(model.rowId(row));
    lua::getiuservalue(state, 1, 1);
    const int cache = lua::gettop(state);
    if (lua::rawgeti(state, cache, id) != lua::Type::Nil) {
        lua::remove(state, cache);
        return;
    }
    lua::pop(state);
    model.pushItem(state, row);
    if (lua::gettop(state) != cache + 1) lua::error(state, "ListModel::pushItem must push exactly one value");
    const lua::Type t = lua::type(state, -1);
    if (t == lua::Type::Table || t == lua::Type::UserData || t == lua::Type::Function) {
        lua::pushvalue(state, -1);
        lua::rawseti(state, cache, id);
    }
    lua::remove(state, cache);
}

/// model[k]: row k's item (nil out of range or for a dead model), else a method.
int modelIndex(lua::State* state) {
    ModelRef* ref = refAt(state, 1);
    if (!ref) return 0;
    if (lua::type(state, 2) == lua::Type::String) {
        lua::pushvalue(state, 2);
        lua::rawget(state, lua::upvalueindex(1));
        return 1;
    }
    if (ref->model && !ref->life.alive()) ref->model = nullptr;
    const ListModel* model = ref->model;
    if (!model) return 0;
    return luaui::catchCxx(
        state,
        [&] {
            const std::optional<std::size_t> row = rowOf(state, 2, *model);
            if (!row) return 0;
            pushRow(state, *model, *row);
            return 1;
        },
        "ListModel", " item");
}

/// #model, model:count(): the row count (0 for a dead model).
int modelLen(lua::State* state) {
    const ListModel* model = selfModel(state, 1);
    const std::size_t n = model ? luaui::catchCxx(state, [&] { return model->count(); }, "ListModel:count") : 0;
    lua::pushinteger(state, static_cast<lua::Integer>(n));
    return 1;
}

/// model:revision(): ListModel::revision(); -1 once the model is gone.
int modelRevision(lua::State* state) {
    const ListModel* model = selfModel(state, 1);
    lua::pushinteger(state, model ? static_cast<lua::Integer>(model->revision() & 0x3fffffffffffffffull) : -1);
    return 1;
}

/// model:hasKeys()
int modelHasKeys(lua::State* state) {
    const ListModel* model = selfModel(state, 1);
    lua::pushboolean(state, model && luaui::catchCxx(state, [&] { return model->hasKeys(); }, "ListModel:hasKeys"));
    return 1;
}

/// model:key(i): row i's key, nil without one (or out of range).
int modelKey(lua::State* state) {
    const ListModel* model = selfModel(state, 1);
    if (!model) return 0;
    return luaui::catchCxx(
        state,
        [&] {
            const std::optional<std::size_t> row = rowOf(state, 2, *model);
            if (!row) return 0;
            const std::optional<ListModel::Key> key = model->key(*row);
            if (!key) return 0;
            if (const auto* i = std::get_if<std::int64_t>(&*key))
                lua::pushinteger(state, static_cast<lua::Integer>(*i));
            else
                lua::pushstring(state, std::get<std::string>(*key));
            return 1;
        },
        "ListModel:key");
}

int modelGc(lua::State* state) {
    ModelRef* ref = refAt(state, 1); // a script may call it, on anything (getmetatable hides it, debug doesn't)
    if (!ref) return 0;
    // Left as an empty ModelRef: a finalized value that is still reachable (resurrected), or one whose __gc a script
    // called, reads as a dead model, and a second call does nothing. A shared owner may destroy the model here; it
    // holds nothing of this state.
    const std::shared_ptr<ListModel> owner = std::move(ref->owner);
    std::destroy_at(ref);
    std::construct_at(ref);
    return 0;
}

int modelToString(lua::State* state) {
    const ModelRef* ref = refAt(state, 1);
    const bool alive = ref && ref->model && ref->life.alive();
    lua::pushstring(state, alive ? std::format("ListModel ({} rows)", ref->model->count()) : "ListModel (gone)");
    return 1;
}

/// model:set(i, v): row i (1-based) becomes v (ListModel::setFromLua). Raises out of range, for a dead model and for a
/// model that takes no writes.
int modelSet(lua::State* state) {
    const ListModel* model = selfModel(state, 1);
    if (!model) lua::error(state, "ListModel:set: the model was destroyed");
    const std::optional<std::size_t> row = rowOf(state, 2, *model);
    if (!row) {
        const std::string why =
            std::format("ListModel:set: index {} out of range (1..{})", lua::toStringAny(state, 2), model->count());
        lua::error(state, why.c_str());
    }
    lua::settop(state, 3);
    // The model is the caller's to change: the Lua value holds it as const only because reads don't change it.
    auto* writable = const_cast<ListModel*>(model);
    return luaui::catchCxx(
        state,
        [&] {
            writable->setFromLua(state, *row, 3);
            return 0;
        },
        "ListModel:set");
}

/// model:listen(signal) -> true | false, why: the prelude's connect() calls it before it adds a listener.
int modelListen(lua::State* state) {
    const ListModel* model = selfModel(state, 1);
    const std::string_view signal = lua::isstring(state, 2) ? lua::tostring(state, 2) : std::string_view{};
    if (!model) {
        lua::pushboolean(state, false);
        lua::pushcstring(state, "the model was destroyed");
        return 2;
    }
    if (!model->listen(state, signal)) {
        lua::pushboolean(state, false);
        lua::pushstring(state, std::format("ListModel has no signal '{}'", signal));
        return 2;
    }
    lua::pushboolean(state, true);
    return 1;
}

/// Pushes the metatable of the models' values, made on first use. `__imlui_model` is the methods table, which the
/// prelude uses to tell a model from a Lua list and to call count/revision/hasKeys/key without an __index lookup.
void pushMeta(lua::State* state) {
    if (lua::getfield(state, lua::registryIndex, metaKey) == lua::Type::Table) return;
    lua::pop(state);
    lua::createtable(state, 0, 9);
    const int meta = lua::gettop(state);
    lua::createtable(state, 0, 6); // methods
    const int methods = meta + 1;
    const std::array<std::pair<const char*, lua::CFunction>, 6> fns{{{"count", &modelLen},
                                                                     {"revision", &modelRevision},
                                                                     {"hasKeys", &modelHasKeys},
                                                                     {"key", &modelKey},
                                                                     {"set", &modelSet},
                                                                     {"listen", &modelListen}}};
    for (const auto& [name, fn] : fns) {
        lua::pushcfunction(state, fn);
        lua::setfield(state, methods, name);
    }
    lua::pushvalue(state, methods);
    lua::setfield(state, meta, "__imlui_model");
    // What getmetatable(model) returns: only `__imlui_model` (the prelude reads it), not the metamethods (a script
    // calling __gc would finalize a value that is still in use).
    lua::createtable(state, 0, 1);
    lua::pushvalue(state, methods);
    lua::setfield(state, -2, "__imlui_model");
    lua::setfield(state, meta, "__metatable");
    lua::pushcclosure(state, &modelIndex, 1); // the methods table is its upvalue
    lua::setfield(state, meta, "__index");
    lua::pushcfunction(state, &modelLen);
    lua::setfield(state, meta, "__len");
    lua::pushcfunction(state, &modelGc);
    lua::setfield(state, meta, "__gc");
    lua::pushcfunction(state, &modelToString);
    lua::setfield(state, meta, "__tostring");
    lua::pushcstring(state, "ListModel");
    lua::setfield(state, meta, "__name");
    lua::pushvalue(state, meta);
    lua::setfield(state, lua::registryIndex, metaKey);
}

/// Pushes the Lua value of `model` in `state`, made on first use; `owner` (when set) is kept by it.
void pushValue(lua::State* state, const ListModel& model, std::shared_ptr<ListModel> owner) {
    lua::checkstack(state, 6);
    pushValues(state);
    const int values = lua::gettop(state);
    const auto id = static_cast<lua::Integer>(model.objectId());
    if (lua::rawgeti(state, values, id) == lua::Type::UserData) {
        if (ModelRef* ref = refAt(state, -1); ref && ref->model == &model) {
            if (owner && !ref->owner) ref->owner = std::move(owner);
            lua::remove(state, values);
            return;
        }
    }
    lua::pop(state);
    // The userdata first, then what it holds: a memory error leaves no half-made value behind.
    ModelRef* ref = newRef(state);
    const int ud = lua::gettop(state);
    pushMeta(state);
    lua::setmetatable(state, ud);
    lua::newtable(state);
    pushCacheMeta(state);
    lua::setmetatable(state, -2);
    lua::setiuservalue(state, ud, 1);
    ref->model = &model;
    ref->life = const_cast<ListModel&>(model).handle(); // the handle is the model's liveness, not a change to it
    ref->owner = std::move(owner);
    lua::pushvalue(state, ud);
    lua::rawseti(state, values, id);
    lua::remove(state, values);
}

/// The states where a script connected to a model's signal (main threads): a notification looks for listeners there.
/// A state leaves it as it closes (closeSentinelKey's finalizer).
std::vector<lua::State*> listeningStates;

/// Registry key of the userdata whose finalizer takes its state off listeningStates.
constexpr const char* closeSentinelKey = "imlui.listmodel.listening";

int sentinelGc(lua::State* state) {
    std::erase(listeningStates, lua::mainthread(state));
    return 0;
}

/// Adds the main thread of `state` to listeningStates, once.
void addListeningState(lua::State* state) {
    lua::State* main = lua::mainthread(state);
    if (std::ranges::find(listeningStates, main) != listeningStates.end()) return;
    lua::checkstack(state, 3);
    lua::newuserdata(state, 1, 0);
    lua::createtable(state, 0, 1);
    lua::pushcfunction(state, &sentinelGc);
    lua::setfield(state, -2, "__gc");
    lua::setmetatable(state, -2);
    lua::setfield(state, lua::registryIndex, closeSentinelKey);
    listeningStates.push_back(main);
}

} // namespace

ListModel::ListModel() = default;
ListModel::~ListModel() = default;

bool ListModel::hasKeys() const { return false; }
std::optional<ListModel::Key> ListModel::key(std::size_t) const { return std::nullopt; }
void ListModel::setFromLua(lua::State*, std::size_t, int) { throw std::logic_error{"the model is read-only"}; }

std::uint64_t ListModel::revision() const { return m_revision; }

void ListModel::record(Change change) const {
    if (!m_logging) return;
    if (change.kind == Change::Kind::Remapped) {
        // One mapping kept (each is count() long): an older Remapped reads as a Reset.
        for (Change& c : m_log)
            if (c.kind == Change::Kind::Remapped) c = Change{};
    }
    m_log.push_back(std::move(change));
    if (m_log.size() > maxLoggedChanges) {
        const std::size_t drop = m_log.size() - maxLoggedChanges;
        m_log.erase(m_log.begin(), m_log.begin() + static_cast<std::ptrdiff_t>(drop));
        m_logStart += drop;
    }
}

std::optional<std::span<const ListModel::Change>> ListModel::changesSince(std::uint64_t since) const {
    if (!m_logging) {
        m_logging = true;
        m_log.clear();
        m_logStart = m_revision;
    }
    if (since < m_logStart || since > m_revision) return std::nullopt;
    return std::span<const Change>{m_log}.subspan(static_cast<std::size_t>(since - m_logStart));
}

void ListModel::changed(Change change) {
    const Change::Kind kind = change.kind;
    const std::size_t first = change.first, n = change.count, destination = change.destination;
    record(std::move(change));
    ++m_revision;
    requestFrame();
    if (m_listened) emitChange({kind, first, n, destination, nullptr}); // last: a listener may edit or destroy it
}

bool ListModel::listen(lua::State* state, std::string_view signal) const {
    if (std::ranges::find(signalNames, signal) == signalNames.end()) return false;
    addListeningState(state);
    m_listened = true;
    return true;
}

void ListModel::emitChange(const Change& change) const {
    const std::string signal{signalNames[static_cast<std::size_t>(change.kind)]};
    const ScriptObject::Handle life = const_cast<ListModel*>(this)->handle(); // its liveness, not a change to it
    const std::uint64_t id = objectId();
    // A copy: a listener may make another state listen, or close one.
    const std::vector<lua::State*> states = listeningStates;
    for (lua::State* state : states) {
        if (!life.alive()) return; // a listener destroyed the model
        if (std::ranges::find(listeningStates, state) == listeningStates.end()) continue;
        luaui::protectedCall(state, signal, [&] {
            lua::checkstack(state, 8);
            pushValues(state);
            if (lua::rawgeti(state, -1, static_cast<lua::Integer>(id)) != lua::Type::UserData) return;
            const ModelRef* ref = refAt(state, -1);
            if (!ref || ref->model != this) return;
            const int target = lua::gettop(state);
            int nargs = 0;
            if (change.kind != Change::Kind::Remapped && change.kind != Change::Kind::Reset) {
                lua::pushinteger(state, static_cast<lua::Integer>(change.first) + 1);
                lua::pushinteger(state, static_cast<lua::Integer>(change.count));
                nargs = 2;
                if (change.kind == Change::Kind::Moved) {
                    lua::pushinteger(state, static_cast<lua::Integer>(change.destination) + 1);
                    nargs = 3;
                }
            }
            luaui::callListeners(state, target, signal.c_str(), target + 1, nargs);
        });
    }
}

void ListModel::renumber(bool repair) const {
    const std::size_t n = count();
    m_rowIds.resize(n);
    for (std::uint64_t& id : m_rowIds)
        id = m_nextRowId++;
    if (repair) {
        record(Change{}); // a Reset: the notifications didn't follow the changes
        ++m_revision;     // the items read as new: the containers must look again
    }
}

bool ListModel::tracking(std::size_t size) const {
    if (!m_tracked) return false;
    if (m_rowIds.size() == size) return true;
    renumber(); // the notifications didn't follow the changes: start over
    return false;
}

std::uint64_t ListModel::rowId(std::size_t row) const {
    if (!m_tracked) {
        m_tracked = true;
        renumber(false); // nothing was cached before
    } else if (m_rowIds.size() != count()) {
        renumber(); // a change without its notification
    }
    return row < m_rowIds.size() ? m_rowIds[row] : 0;
}

void ListModel::rowsInserted(std::size_t first, std::size_t n) {
    const std::size_t total = count();
    if (n <= total && first <= total - n && tracking(total - n)) {
        std::vector<std::uint64_t> ids(n);
        for (std::uint64_t& id : ids)
            id = m_nextRowId++;
        m_rowIds.insert(m_rowIds.begin() + static_cast<std::ptrdiff_t>(first), ids.begin(), ids.end());
    } else if (m_tracked) {
        renumber();
    }
    changed({Change::Kind::Inserted, first, n, 0, nullptr});
}

void ListModel::rowsRemoved(std::size_t first, std::size_t n) {
    const std::size_t total = count();
    if (first <= total && tracking(total + n)) {
        const auto begin = m_rowIds.begin() + static_cast<std::ptrdiff_t>(first);
        m_rowIds.erase(begin, begin + static_cast<std::ptrdiff_t>(n));
    } else if (m_tracked) {
        renumber();
    }
    changed({Change::Kind::Removed, first, n, 0, nullptr});
}

void ListModel::rowsChanged(std::size_t first, std::size_t n) {
    const std::size_t total = count();
    if (n <= total && first <= total - n && tracking(total)) {
        for (std::size_t i = first; i < first + n; ++i)
            m_rowIds[i] = m_nextRowId++;
    } else if (m_tracked) {
        renumber();
    }
    changed({Change::Kind::Changed, first, n, 0, nullptr});
}

void ListModel::rowsMoved(std::size_t first, std::size_t n, std::size_t destination) {
    const std::size_t total = count();
    if (n <= total && first <= total - n && destination <= total - n && tracking(total)) {
        const auto at = [this](std::size_t i) { return m_rowIds.begin() + static_cast<std::ptrdiff_t>(i); };
        if (destination < first)
            std::rotate(at(destination), at(first), at(first + n));
        else if (destination > first)
            std::rotate(at(first), at(first + n), at(destination + n));
    } else if (m_tracked) {
        renumber();
    }
    changed({Change::Kind::Moved, first, n, destination, nullptr});
}

void ListModel::rowsRemapped(std::span<const std::optional<std::size_t>> previous) {
    const std::size_t total = count();
    if (previous.size() == total && m_tracked) {
        const std::vector<std::uint64_t> old = std::move(m_rowIds);
        std::vector<bool> taken(old.size(), false);
        m_rowIds.assign(total, 0);
        for (std::size_t r = 0; r < total; ++r) {
            const std::optional<std::size_t> p = previous[r];
            if (p && *p < old.size() && !taken[*p]) {
                taken[*p] = true;
                m_rowIds[r] = old[*p];
            } else {
                m_rowIds[r] = m_nextRowId++;
            }
        }
    } else if (m_tracked) {
        renumber();
    }
    changed({Change::Kind::Remapped, 0, previous.size(), 0,
             m_logging
                 ? std::make_shared<const std::vector<std::optional<std::size_t>>>(previous.begin(), previous.end())
                 : nullptr});
}

void ListModel::reset() {
    if (m_tracked) renumber();
    changed({Change::Kind::Reset, 0, 0, 0, nullptr});
}

void ListModel::pushLua(lua::State* state) const { pushValue(state, *this, nullptr); }

void ListModel::pushShared(lua::State* state, std::shared_ptr<ListModel> model) {
    if (!model) {
        lua::pushnil(state);
        return;
    }
    const ListModel& ref = *model;
    pushValue(state, ref, std::move(model));
}

ListModel* ListModel::fromLua(lua::State* state, int idx) {
    ModelRef* ref = refAt(state, idx);
    if (!ref || !ref->model || !ref->life.alive()) return nullptr;
    return const_cast<ListModel*>(ref->model);
}

} // namespace imlui
