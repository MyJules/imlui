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
///  * `require` finds nothing else: the stock file searchers are removed (the Lua-file one, which reads
///    `package.path`, on Windows `.\?.lua` and the like relative to the current folder, and the C and all-in-one
///    ones, which load DLLs found on `package.cpath`), `package.path` and `package.cpath` are "" (which also drops
///    the `LUA_PATH`/`LUA_CPATH` environment variables `openlibs` read) and `package.loadlib` is nil. A host is
///    path-free: its Lua comes from its resources only, so a mistyped `require "Buton"` is a "module not found"
///    error listing the resources looked for, never a stray `Buton.lua` from the working folder run in `_G` or a
///    planted `Buton.dll`. `package.preload` still works, for modules a host registers from C++.
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

/// Pushes `values` (nil for nullopt) and returns how many.
int pushValues(lua::State* state, std::initializer_list<std::optional<std::string_view>> values) {
    for (const auto& value : values)
        if (value)
            lua::pushstring(state, *value);
        else
            lua::pushnil(state);
    return static_cast<int>(values.size());
}

/// Pushes nil and `message`; returns 2.
int fail(lua::State* state, std::string_view message) { return pushValues(state, {std::nullopt, message}); }

/// Runs `work`, which returns how many values it pushed, and returns that count; a C++ exception becomes nil and a
/// message instead (the library's functions don't raise). A Lua error (out of memory while pushing) passes through.
template <class Work>
int guarded(lua::State* state, const Work& work) {
    try {
        return work();
    } catch (const lua::Raised&) {
        throw;
    } catch (const std::bad_alloc&) {
        return fail(state, "out of memory");
    } catch (...) {
        return fail(state, "internal error");
    }
}

std::string quotedName(std::string_view name) { return "'" + std::string{name} + "'"; }

/// Loads resource `name` as a text chunk named `@name`, with the table at `envIndex` (0: none) as its
/// environment. Pushes the function, or nil and a message; returns the number of values pushed. Loading is
/// protected (lua_load returns its errors), so only a memory error raises.
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

/// The searcher's work. Pushes the loader and its name (returns 2) or the "not found" lines (returns 1), or raises
/// the error that loading the module's chunk returned.
int search(lua::State* state, std::string_view module) {
    const Context ctx = context(state);
    std::string tried;
    const auto name = findModule(ctx, module, "imlui:/", tried);
    if (!name) return pushValues(state, {tried});
    if (loadChunk(state, ctx, *name, 0) != 1) // nil, message (loadChunk's messages are strings)
        lua::error(state, "error loading module " + quotedName(module) + " from " + quotedName(*name) + ":\n\t" +
                              std::string{lua::tostring(state, -1)});
    return pushValues(state, {*name}) + 1;
}

int searcher(lua::State* state) {
    const std::string_view module = lua::tostring(state, 1);
    try {
        return search(state, module);
    } catch (const lua::Raised&) {
        throw;
    } catch (...) {
        lua::error(state, "error loading module: out of memory");
    }
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

    // package.searchers = { <preload>, searcher }: the preload searcher (always first, see luaopen_package) stays,
    // the file, C and all-in-one searchers after it go (see the declaration), ours takes the second place.
    lua::getfield(state, -1, "searchers");
    for (auto i = lua::rawlen(state, -1); i >= 2; --i) {
        lua::pushnil(state);
        lua::rawseti(state, -2, i);
    }
    pushContext(state, ctx);
    lua::pushcclosure(state, &searcher, 2);
    lua::rawseti(state, -2, 2);
    lua::pop(state);

    // No searcher reads the paths any more, but package.searchpath does, and they'd show a script where Lua would
    // look: "" rather than nil, since the package library expects strings there. Overriding them after openlibs is
    // also what drops LUA_PATH and LUA_CPATH, which luaopen_package read from the environment.
    lua::pushstring(state, "");
    lua::setfield(state, -2, "path");
    lua::pushstring(state, "");
    lua::setfield(state, -2, "cpath");
    lua::pushnil(state); // loadlib(path, fn) loads any DLL and calls into it
    lua::setfield(state, -2, "loadlib");
    lua::pop(state);
}

} // namespace imlui::luaui
