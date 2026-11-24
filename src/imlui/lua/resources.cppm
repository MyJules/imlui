// The `imlui.resource` Lua library and the package.searchers entry that loads Lua modules from Resources.
export module imlui.lua.resources;

import std;

import lua;
import imlui.resources;
import imlui.resources.name;

export namespace imlui::luaui {

/// Sets up `state` to load Lua through `resources`:
///  * `require("imlui.resource")` is the native library behind `ui.resource`. Its functions take full names and
///    never raise: `resolve(base, name)`, `read(name)`, `load(name [, env])` (a text chunk named `@name`) and
///    `find(module, folder)` return the result or nil plus a message; `exists(name)` returns a boolean.
///  * a package.searchers entry, second after the preload searcher, finds modules in `imlui:/`: `imlui` is
///    `imlui:/imlui.lua`, `a.b` is `imlui:/a/b.lua` or `imlui:/a/b/init.lua`. src/resources/imlui.lua adds its
///    own entry in front of it for components next to the script.
/// `resources` must outlive the state. When `loaded` is given, the full name of every resource the state reads or
/// loads is added to it (the engine watches them for hot reload); it must outlive the state too.
void registerResources(lua::State* state, const Resources& resources, std::set<std::string>* loaded = nullptr);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

struct Context {
    const Resources* resources;
    std::set<std::string>* loaded;
};

/// The Resources and the loaded-name set: upvalues 1 and 2 of every function of the library.
Context context(lua::State* state) {
    return {static_cast<const Resources*>(lua::touserdata(state, lua::upvalueindex(1))),
            static_cast<std::set<std::string>*>(lua::touserdata(state, lua::upvalueindex(2)))};
}

void remember(const Context& ctx, std::string_view name) {
    if (ctx.loaded) ctx.loaded->emplace(name);
}

// Lua is compiled as C: its errors (a memory error while pushing a string, too) longjmp, which is undefined
// behaviour past a frame with a live non-trivial C++ object, and a C++ exception mustn't cross its frames. So the
// library functions do their work in guarded(), which catches exceptions, and push strings through pushValues(), a
// protected call; a failed push is raised once guarded()'s work and its objects are gone.

using Values = std::span<const std::optional<std::string_view>>;

/// pushValues()' protected part: pushes the Values that light userdata 1 points to.
int pushValuesFn(lua::State* state) {
    const auto& values = *static_cast<const Values*>(lua::touserdata(state, 1));
    for (const auto& value : values)
        if (value)
            lua::pushstring(state, *value);
        else
            lua::pushnil(state);
    return static_cast<int>(values.size());
}

/// Pushes `values` (nil for nullopt) and returns how many, or -1 with Lua's error on the stack when that failed (out
/// of memory). Protected, so it never raises past the caller's C++ objects.
int pushValues(lua::State* state, std::initializer_list<std::optional<std::string_view>> values) {
    const Values list{values.begin(), values.size()};
    lua::pushcfunction(state, &pushValuesFn);
    lua::pushlightuserdata(state, const_cast<Values*>(&list)); // only read through
    if (lua::pcall(state, 1, lua::multRet, 0) != lua::ok) return -1;
    return static_cast<int>(list.size());
}

/// Pushes nil and `message`; returns 2 (-1: see pushValues()).
int fail(lua::State* state, std::string_view message) { return pushValues(state, {std::nullopt, message}); }

/// Runs `work`, which returns how many values it pushed or -1 (see pushValues()), and returns that count; a C++
/// exception becomes nil and a message (pushed protected by fail(), so nothing raises inside the catch blocks).
/// Raises the error a failed push left, after the catch blocks. Called with no live non-trivial object in the
/// caller's frame.
template <class Work>
int guarded(lua::State* state, const Work& work) {
    int results = -1;
    try {
        results = work();
    } catch (const std::bad_alloc&) {
        results = fail(state, "out of memory");
    } catch (const std::exception&) {
        results = fail(state, "internal error");
    } catch (...) { // nothing may unwind through Lua's C frames
        results = fail(state, "internal error");
    }
    if (results < 0) lua::raise(state);
    return results;
}

std::string quotedName(std::string_view name) { return "'" + std::string{name} + "'"; }

/// Loads resource `name` as a text chunk named `@name`, with the table at `envIndex` (0: none) as its
/// environment. Pushes the function, or nil and a message; returns the number of values pushed (-1: see
/// pushValues()). Loading is protected (lua_load returns its errors), so nothing here raises.
int loadChunk(lua::State* state, const Context& ctx, std::string_view name, int envIndex) {
    const auto canonical = normalizeName(name);
    if (!canonical) return fail(state, "invalid resource name " + quotedName(name));
    const auto code = ctx.resources->read(*canonical); // Lua copies the text it needs
    if (!code) return fail(state, "resource " + quotedName(*canonical) + " not found");
    remember(ctx, *canonical);
    const std::string chunkName = "@" + *canonical;
    if (lua::loadbufferx(state, code->text(), chunkName.c_str(), "t") != lua::ok) {
        lua::pushnil(state);
        lua::insert(state, -2); // nil, message
        return 2;
    }
    if (envIndex != 0) {
        lua::pushvalue(state, envIndex);
        if (!lua::setupvalue(state, -2, 1)) lua::pop(state); // a chunk without upvalues uses no globals
    }
    return 1;
}

/// The full name of module `module` (`a.b`: `a/b.lua`, then `a/b/init.lua`) in `folder`, or nullopt; `tried`
/// gets one line per name looked at, in the format package.searchers expects.
std::optional<std::string> findModule(const Context& ctx, std::string_view module, std::string_view folder,
                                      std::string& tried) {
    std::string path{module};
    for (char& c : path)
        if (c == '.') c = '/';
    for (const char* suffix : {".lua", "/init.lua"}) {
        const auto name = resolveName(folder, path + suffix);
        if (!name) {
            tried += "\n\tno resource " + quotedName(path + suffix) + " in " + quotedName(folder);
            continue;
        }
        if (ctx.resources->exists(*name)) return name;
        tried += "\n\tno resource " + quotedName(*name);
    }
    return std::nullopt;
}

int resolveFn(lua::State* state) {
    const std::string_view base = lua::tostring(state, 1);
    const std::string_view name = lua::tostring(state, 2);
    return guarded(state, [&] {
        const auto full = resolveName(base, name);
        if (!full) return fail(state, "invalid resource name " + quotedName(name));
        return pushValues(state, {*full});
    });
}

int readFn(lua::State* state) {
    const std::string_view name = lua::tostring(state, 1);
    return guarded(state, [&] {
        const Context ctx = context(state);
        const auto canonical = normalizeName(name);
        if (!canonical) return fail(state, "invalid resource name " + quotedName(name));
        const auto bytes = ctx.resources->read(*canonical);
        if (!bytes) return fail(state, "resource " + quotedName(*canonical) + " not found");
        remember(ctx, *canonical);
        return pushValues(state, {bytes->text()}); // a copy
    });
}

int existsFn(lua::State* state) {
    const std::string_view name = lua::tostring(state, 1);
    bool exists = false;
    try {
        exists = context(state).resources->exists(name);
    } catch (const std::exception&) { // out of memory: reads as missing, exists() never raises
    }
    lua::pushboolean(state, exists);
    return 1;
}

int loadFn(lua::State* state) {
    const int envIndex = lua::istable(state, 2) ? 2 : 0;
    const std::string_view name = lua::tostring(state, 1);
    return guarded(state, [&] { return loadChunk(state, context(state), name, envIndex); });
}

int findFn(lua::State* state) {
    const std::string_view module = lua::tostring(state, 1);
    const std::string_view folder = lua::tostring(state, 2);
    return guarded(state, [&] {
        std::string tried;
        const auto name = findModule(context(state), module, folder, tried);
        if (!name) return fail(state, tried);
        return pushValues(state, {*name});
    });
}

/// The searcher's work. Pushes the loader and its name (returns 2) or the "not found" lines (returns 1), or leaves
/// the error to raise on the stack (returns -1).
int search(lua::State* state, std::string_view module) {
    const Context ctx = context(state);
    std::string tried;
    const auto name = findModule(ctx, module, "imlui:/", tried);
    if (!name) return pushValues(state, {tried});
    const int loaded = loadChunk(state, ctx, *name, 0);
    if (loaded < 0) return -1;
    if (loaded != 1) {
        const std::string message{lua::tostring(state, -1)}; // loadChunk's messages are strings
        lua::pop(state, 2);
        // On success the message is on top, on failure Lua's error: either is raised.
        pushValues(state,
                   {"error loading module " + quotedName(module) + " from " + quotedName(*name) + ":\n\t" + message});
        return -1;
    }
    return pushValues(state, {*name}) < 0 ? -1 : 2;
}

int searcher(lua::State* state) {
    const std::string_view module = lua::tostring(state, 1);
    int results = -1;
    try {
        results = search(state, module);
    } catch (const std::exception&) {
        pushValues(state, {"error loading module: out of memory"}); // raised, like a failed push
        results = -1;
    }
    if (results < 0) lua::raise(state); // no C++ object is alive here
    return results;
}

constexpr struct {
    const char* name;
    lua::CFunction fn;
} libraryFns[] = {
    {"resolve", &resolveFn}, {"read", &readFn}, {"exists", &existsFn}, {"load", &loadFn}, {"find", &findFn},
};

void pushContext(lua::State* state, const Context& ctx) {
    lua::pushlightuserdata(state, const_cast<Resources*>(ctx.resources)); // only ever read through
    lua::pushlightuserdata(state, ctx.loaded);
}

int openLibrary(lua::State* state) {
    const Context ctx = context(state);
    lua::createtable(state, 0, static_cast<int>(std::size(libraryFns)));
    for (const auto& f : libraryFns) {
        pushContext(state, ctx);
        lua::pushcclosure(state, f.fn, 2);
        lua::setfield(state, -2, f.name);
    }
    return 1;
}

} // namespace

void registerResources(lua::State* state, const Resources& resources, std::set<std::string>* loaded) {
    const Context ctx{&resources, loaded};
    lua::getglobal(state, "package");

    lua::getfield(state, -1, "preload");
    pushContext(state, ctx);
    lua::pushcclosure(state, &openLibrary, 2);
    lua::setfield(state, -2, "imlui.resource");
    lua::pop(state);

    // table.insert(package.searchers, 2, searcher): shift the others up by one.
    lua::getfield(state, -1, "searchers");
    for (auto i = lua::rawlen(state, -1); i >= 2; --i) {
        lua::rawgeti(state, -1, i);
        lua::rawseti(state, -2, i + 1);
    }
    pushContext(state, ctx);
    lua::pushcclosure(state, &searcher, 2);
    lua::rawseti(state, -2, 2);
    lua::pop(state, 2);
}

} // namespace imlui::luaui
