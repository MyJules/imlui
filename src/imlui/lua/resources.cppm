// The `imlui.resource` Lua library and the package.searchers entry that loads Lua modules from Resources.
module;

#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>

export module imlui.lua.resources;

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

/// Pushes nil and `message`; returns 2.
int fail(lua::State* state, std::string_view message) {
    lua::pushnil(state);
    lua::pushstring(state, message);
    return 2;
}

std::string quoted(std::string_view name) { return "'" + std::string{name} + "'"; }

/// Loads resource `name` as a text chunk named `@name`, with the table at `envIndex` (0: none) as its
/// environment. Pushes the function, or nil and a message; returns the number of values pushed.
int loadChunk(lua::State* state, const Context& ctx, std::string_view name, int envIndex) {
    const auto canonical = normalizeName(name);
    if (!canonical) return fail(state, "invalid resource name " + quoted(name));
    const auto code = ctx.resources->read(*canonical); // Lua copies the text it needs
    if (!code) return fail(state, "resource " + quoted(*canonical) + " not found");
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
            tried += "\n\tno resource " + quoted(path + suffix) + " in " + quoted(folder);
            continue;
        }
        if (ctx.resources->exists(*name)) return name;
        tried += "\n\tno resource " + quoted(*name);
    }
    return std::nullopt;
}

int resolveFn(lua::State* state) {
    const std::string_view base = lua::tostring(state, 1);
    const std::string_view name = lua::tostring(state, 2);
    const auto full = resolveName(base, name);
    if (!full) return fail(state, "invalid resource name " + quoted(name));
    lua::pushstring(state, *full);
    return 1;
}

int readFn(lua::State* state) {
    const Context ctx = context(state);
    const std::string_view name = lua::tostring(state, 1);
    const auto canonical = normalizeName(name);
    if (!canonical) return fail(state, "invalid resource name " + quoted(name));
    const auto bytes = ctx.resources->read(*canonical);
    if (!bytes) return fail(state, "resource " + quoted(*canonical) + " not found");
    remember(ctx, *canonical);
    lua::pushstring(state, bytes->text()); // a copy
    return 1;
}

int existsFn(lua::State* state) {
    lua::pushboolean(state, context(state).resources->exists(lua::tostring(state, 1)));
    return 1;
}

int loadFn(lua::State* state) {
    const int envIndex = lua::istable(state, 2) ? 2 : 0;
    return loadChunk(state, context(state), lua::tostring(state, 1), envIndex);
}

int findFn(lua::State* state) {
    std::string tried;
    const auto name = findModule(context(state), lua::tostring(state, 1), lua::tostring(state, 2), tried);
    if (!name) return fail(state, tried);
    lua::pushstring(state, *name);
    return 1;
}

/// The searcher's work. Pushes the loader and its name (returns 2), the "not found" lines (returns 1), or an error
/// message (returns -1). Kept apart so no C++ object is alive when the searcher raises (Lua longjmps).
int search(lua::State* state) {
    const Context ctx = context(state);
    const std::string module{lua::tostring(state, 1)};
    std::string tried;
    const auto name = findModule(ctx, module, "imlui:/", tried);
    if (!name) {
        lua::pushstring(state, tried);
        return 1;
    }
    if (loadChunk(state, ctx, *name, 0) != 1) {
        const std::string message = lua::toStringAny(state, -1);
        lua::pop(state, 2);
        lua::pushstring(state, "error loading module " + quoted(module) + " from " + quoted(*name) + ":\n\t" + message);
        return -1;
    }
    lua::pushstring(state, *name);
    return 2;
}

int searcher(lua::State* state) {
    const int results = search(state);
    if (results < 0) lua::raise(state);
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
