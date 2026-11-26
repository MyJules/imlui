// Lua language-server definitions: LuaLS (sumneko) `---@meta` files for the scripts' editor, so they get completion
// and type checks for everything registered from C++. Two files:
//
//  * `imlui.lua`: the DSL prelude (state, bind, watch, component, Repeater, If, theme, timers, async/await, ui.*,
//    the `Element`, `ElementProps`, `Binding`, `Color` and `Future<T>` classes). Hand-written, shipped with the
//    library's files as imlui:/definitions/imlui.lua (luaPreludeDefinitions) and copied as it is.
//  * `types.lua`: generated from a TypeRegistry (luaTypeDefinitions): a `<Name>Props` class (what the constructor
//    takes: every writable property as a value, a binding function or a `bind`, and the `on*` handlers) and a `<Name>`
//    class (the element table: properties, handlers, methods) per element type, the constructor `<Name>(props)` (also
//    as `ui.<Name>`), a class and a global per singleton, the named structs and enums the members use, and `connect`
//    with an overload per type whose signal names are a `<Name>Signal` alias.
//
// Member types come from LuaConvert (luaTypeOf<V>, recorded by TypeBuilder in TypeInfo). Output is deterministic:
// types and singletons sorted by name, members in declaration order.
export module imlui.types.lua_definitions;

import std;

import imlui.lua.listeners;
import imlui.types.lua_types;
import imlui.types.type_info;
import imlui.types.registry;

export namespace imlui {

/// The resource the prelude's definitions are shipped as (src/resources/definitions/imlui.lua).
inline constexpr std::string_view luaPreludeDefinitions = "imlui:/definitions/imlui.lua";

/// The text of `types.lua` for the element types and singletons of `registry`.
[[nodiscard]] std::string luaTypeDefinitions(const TypeRegistry& registry);

/// Writes `imlui.lua` (`prelude`: the text of luaPreludeDefinitions) and `types.lua` (luaTypeDefinitions) into
/// `dir`, creating it. A file that already has that content isn't written again (its time stays, for build steps).
/// Returns "" on success, else what failed.
[[nodiscard]] std::string writeLuaDefinitions(const TypeRegistry& registry, const std::filesystem::path& dir,
                                              std::string_view prelude);

} // namespace imlui

module :private;

namespace imlui {

namespace fs = std::filesystem;

namespace {

std::string describe(LuaTypes::Describe d, LuaTypes& named) { return d ? d(named) : std::string{"any"}; }

/// "name: type" for a fun() parameter list; an optional type makes the parameter optional ("limit?: integer").
std::string param(std::string_view name, const std::string& type) {
    std::string out{name};
    if (type.ends_with('?')) out += "?";
    out += ": ";
    out += LuaTypes::required(type);
    return out;
}

/// A handler signature: "fun(self: Gauge, value: number)", "fun(...: any)" for an undeclared signal of a singleton.
std::string handler(const std::string& self, const std::optional<std::vector<TypeInfo::Param>>& args, LuaTypes& named) {
    std::vector<std::string> parts;
    if (!self.empty()) parts.push_back("self: " + self);
    if (!args) {
        parts.emplace_back("...: any");
    } else {
        for (const TypeInfo::Param& a : *args)
            parts.push_back(param(a.name, describe(a.type, named)));
    }
    std::string out = "fun(";
    for (std::size_t i = 0; i < parts.size(); ++i)
        out += (i ? ", " : "") + parts[i];
    return out + ")";
}

/// A method's field type: "fun(self: Gauge, min: number, max: number): Future<string>".
std::string methodType(const std::string& self, const TypeInfo::Method& m, LuaTypes& named) {
    std::string out = "fun(self: " + self;
    for (const TypeInfo::Param& p : m.params)
        out += ", " + param(p.name, describe(p.type, named));
    out += ")";
    if (m.result) out += ": " + LuaTypes::grouped(m.result(named));
    return out;
}

struct Signal {
    std::string name;
    std::string type; ///< the handler's fun() type
};

/// The declared signals and the properties' change signals (a declared one of the same name wins), in that order.
std::vector<Signal> signalsOf(const TypeInfo& info, const std::string& self, LuaTypes& named) {
    std::vector<Signal> out;
    std::set<std::string, std::less<>> seen;
    for (const TypeInfo::Signal& s : info.signalDeclarations())
        if (seen.insert(s.name).second) out.push_back({s.name, handler(self, s.args, named)});
    for (const TypeInfo::Property& p : info.properties()) {
        std::string name = luaui::changedSignal(p.name);
        if (!seen.insert(name).second) continue;
        out.push_back({std::move(name), handler(self, std::vector<TypeInfo::Param>{{"value", p.luaType}}, named)});
    }
    return out;
}

/// `---@alias <Name>Signal "onA"|"onB"`, or "" when the type has no signals.
std::string signalAlias(const std::string& alias, const std::vector<Signal>& signals) {
    if (signals.empty()) return {};
    std::string out = "---@alias " + alias + " ";
    for (std::size_t i = 0; i < signals.size(); ++i)
        out += (i ? "|" : "") + LuaTypes::quoted(signals[i].name);
    return out + "\n";
}

/// What a script may give a property in a constructor: the value, a binding (which may return nil to reset it) or a
/// `bind`. A property whose type takes functions (LuaFunction) gets the function itself, never as a binding.
std::string propType(const std::string& self, const TypeInfo::Property& p, const std::string& type) {
    const std::string value{LuaTypes::required(type)};
    if (p.takesFunction) return value + "|Binding";
    return value + "|(fun(self: " + self + "): " + LuaTypes::grouped(LuaTypes::optional(value)) + ")|Binding";
}

struct Connectable {
    std::string type;   ///< the target's class
    std::string alias;  ///< its signal alias
    std::string signal; ///< the handler type connect's fn gets
};

std::string elementDefinitions(const std::string& name, const TypeInfo& info, LuaTypes& named,
                               std::vector<Connectable>& connectables) {
    const std::vector<Signal> signals = signalsOf(info, name, named);
    const std::string alias = name + "Signal";
    std::string out = "-- " + name + "\n\n";
    out += signalAlias(alias, signals);
    if (!signals.empty()) {
        out += "\n";
        connectables.push_back({name, alias, "fun(self: " + name + ", ...: any)"});
    }

    out += "---@class " + name + "Props: ElementProps\n";
    for (const TypeInfo::Property& p : info.properties())
        if (p.assign) out += "---@field " + p.name + "? " + propType(name, p, describe(p.luaType, named)) + "\n";
    for (const Signal& s : signals)
        out += "---@field " + s.name + "? " + s.type + "\n";
    out += "\n";

    out += "---@class " + name + ": Element\n";
    for (const TypeInfo::Property& p : info.properties())
        out += "---@field " + p.name + " " + describe(p.luaType, named) + "\n";
    for (const Signal& s : signals)
        out += "---@field " + s.name + "? " + s.type + "\n";
    for (const TypeInfo::Method& m : info.methods())
        out += "---@field " + m.name + " " + methodType(name, m, named) + "\n";
    out += "\n";

    std::string props = name + "Props";
    if (const TypeInfo::Property* p = info.findProperty(info.defaultProperty()))
        props += "|" + LuaTypes::grouped(LuaTypes::required(describe(p->luaType, named)));
    out += "---@param props? " + props + "\n";
    out += "---@return " + name + "\n";
    out += "function " + name + "(props) end\n";
    out += "ui." + name + " = " + name + "\n\n";
    return out;
}

std::string singletonDefinitions(const std::string& name, const TypeInfo& info, LuaTypes& named,
                                 std::vector<Connectable>& connectables) {
    // Singleton handlers get no `self`: the singleton is a global anyway.
    const std::vector<Signal> signals = signalsOf(info, "", named);
    const std::string alias = name + "Signal";
    std::string out = "-- " + name + " (singleton)\n\n";
    out += signalAlias(alias, signals);
    if (!signals.empty()) {
        out += "\n";
        connectables.push_back({name, alias, "fun(...: any)"});
    }
    out += "---@class " + name + "\n";
    for (const TypeInfo::Property& p : info.properties())
        out += "---@field " + p.name + " " + describe(p.luaType, named) + "\n";
    for (const Signal& s : signals)
        out += "---@field " + s.name + "? " + s.type + "\n";
    for (const TypeInfo::Method& m : info.methods())
        out += "---@field " + m.name + " " + methodType(name, m, named) + "\n";
    out += name + " = {}\n\n";
    return out;
}

std::string connectDefinitions(const std::vector<Connectable>& connectables) {
    std::string out = "-- connect\n\n";
    out += "---Every signal name of the registered types and singletons (`connect` raises for any other).\n";
    if (connectables.empty()) {
        out += "---@alias ImluiSignal string\n\n";
    } else {
        out += "---@alias ImluiSignal ";
        for (std::size_t i = 0; i < connectables.size(); ++i)
            out += (i ? "|" : "") + connectables[i].alias;
        out += "\n\n";
    }
    out += "---Adds a listener to a signal of a C++ element or singleton: `fn` runs whenever `target` emits `signal`, "
           "after its handler.\n";
    out += "---@param target Element|table\n";
    out += "---@param signal ImluiSignal\n";
    out += "---@param fn function\n";
    out += "---@return fun() disconnect\n";
    for (const Connectable& c : connectables)
        out += "---@overload fun(target: " + c.type + ", signal: " + c.alias + ", fn: " + c.signal + "): fun()\n";
    out += "function connect(target, signal, fn) end\n";
    out += "ui.connect = connect\n";
    return out;
}

/// The file's text, or nullopt when it can't be read (missing).
std::optional<std::string> readFile(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// Writes `text` to `file` unless it has that content already. Returns "" or the error.
std::string writeFile(const fs::path& file, std::string_view text) {
    if (readFile(file) == text) return {};
    std::ofstream out{file, std::ios::binary | std::ios::trunc};
    if (out) out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (out) out.close();
    if (!out) return "can't write " + file.generic_string();
    return {};
}

} // namespace

std::string luaTypeDefinitions(const TypeRegistry& registry) {
    LuaTypes named;
    std::vector<Connectable> connectables;
    std::string elements;
    std::string skipped;
    for (const std::string& name : registry.typeNames()) {
        const TypeInfo* info = registry.find(name);
        if (!info) continue;
        if (!LuaTypes::isIdentifier(name)) {
            skipped += "-- skipped: element type " + LuaTypes::quoted(name) + " (not a Lua name)\n";
            continue;
        }
        elements += elementDefinitions(name, *info, named, connectables);
    }
    std::string singletons;
    for (const auto& [name, info] : registry.singletonTypes()) {
        if (!LuaTypes::isIdentifier(name)) {
            skipped += "-- skipped: singleton " + LuaTypes::quoted(name) + " (not a Lua name)\n";
            continue;
        }
        singletons += singletonDefinitions(name, *info, named, connectables);
    }

    std::string out = "---@meta _\n"
                      "-- LuaLS definitions of the C++ types registered with imlui: the element types (built-in and "
                      "the app's own)\n"
                      "-- and the singletons. Generated (--lua-defs <dir> or IMLUI_LUA_DEFS=<dir>); don't edit it, "
                      "generate it again.\n"
                      "-- The DSL (state, bind, Element, Future, ...) is in imlui.lua next to this file.\n\n";
    out += skipped;
    if (!skipped.empty()) out += "\n";
    if (!named.definitions().empty()) {
        out += "-- Value types\n\n";
        for (const auto& [name, declaration] : named.definitions())
            out += declaration + "\n";
    }
    out += elements;
    out += singletons;
    out += connectDefinitions(connectables);
    return out;
}

std::string writeLuaDefinitions(const TypeRegistry& registry, const fs::path& dir, std::string_view prelude) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return "can't create " + dir.generic_string() + ": " + ec.message();
    if (std::string error = writeFile(dir / "imlui.lua", prelude); !error.empty()) return error;
    return writeFile(dir / "types.lua", luaTypeDefinitions(registry));
}

} // namespace imlui
