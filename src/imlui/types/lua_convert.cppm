// LuaConvert<T>: how a C++ value crosses into and out of Lua.
//
// The type system (properties, method arguments and results, signal arguments, LuaFunction calls) moves values with
// this trait. Each specialization provides
//
//   static constexpr std::string_view name;        // for error messages: "number", "string", "{x, y}" ...
//   static void push(lua::State*, const T&);       // C++ -> Lua: pushes exactly one value
//   static bool check(lua::State*, int idx);       // can the value at idx become a T?
//   static T get(lua::State*, int idx);            // Lua -> C++ (only called after check())
//   static std::string mismatch(lua::State*, int idx); // optional: why check() failed (see below)
//   static std::string luaType(LuaTypes&);         // optional: the LuaLS type expression (see below)
//   static bool equals(lua::State*, int idx, const T&); // optional: in-place comparison (see below)
//
// Push-only types (std::string_view, const char*) leave out check/get. Built in: bool, integers, floats, strings,
// Color, std::optional, std::vector, std::map / std::unordered_map with string keys, std::pair, std::tuple,
// std::variant (std::monostate is nil), enums described by EnumNames<E>, and structs described by LuaFields<T>
// (imlui::LuaFunction's lives in imlui.types.lua_function). They compose: a vector of structs whose fields are
// maps of variants converts. Specialize it for other types in your own module:
//
//   template <> struct imlui::LuaConvert<Vec2> {
//       static constexpr std::string_view name = "{x, y}";
//       static void push(lua::State* s, const Vec2& v) { ... }
//       static bool check(lua::State* s, int idx) { return lua::istable(s, idx); }
//       static Vec2 get(lua::State* s, int idx) { ... }
//   };
//
// Tables are read raw (no metamethods), so check() never runs Lua code and never raises. A sequence (std::vector,
// std::pair, std::tuple) is a table whose keys are all integers 1..n; a map's keys are all strings. So `{}` is both an
// empty sequence and an empty map (a std::variant takes whichever alternative comes first), `{x = 1}` is no sequence
// and `{1, 2}` no map. Tables nested deeper than LuaNesting::maxDepth (200) levels fail check(): a table
// that contains itself (a cycle) would otherwise recurse until the C++ stack overflows.
//
// In-place comparison. The property sync converts a value only to compare it with what the getter returns, so a table
// property (a plot's 10k samples) would be converted, and allocated, every frame while it doesn't change. luaEquals<T>()
// asks LuaConvert<T>::equals() first when the specialization has one: true only when check() would pass and get()
// return a value equal to the one given, read without building it (false when unsure: the caller then converts as
// usual). bool, numbers, strings, std::optional, std::vector, std::variant and LuaFunction have one; luaEquals falls
// back to check() and get() for the others (structs, maps, tuples).
//
// Error messages. Where a value doesn't convert (a method argument, a property, a LuaFunction result), the message
// comes from luaMismatch<T>(): LuaConvert<T>::mismatch() when the specialization has one, else
// "<name> expected, got <Lua type>". The composite ones (optional, vector, maps, pair, tuple, variant, structs, enums)
// have one and recurse into the element that failed, prefixing its path: "[3].x: number expected, got string",
// ".method: 'trial'|'wheel' expected, got string \"fast\"". A path starts with '.' or '['; luaMismatchAt() joins it
// to where the value was ("Plot.points" + "[3].x: ..."). A specialization of your own may implement mismatch() the
// same way (it is only called after check() returned false, and must not raise), or leave it out.
//
// Editor definitions. luaTypeOf<T>() is the LuaLS type expression of T's values, for the generated Lua definitions
// (imlui.types.lua_definitions): LuaConvert<T>::luaType(LuaTypes&) or luaType() when the specialization has one, else
// one derived from `name` (LuaTypes::fromName: the Lua built-in type names stay, anything else is `any`). The built-in
// ones compose: "integer[]", "table<string, Vec2>", "[number, string]", "integer|string", "Future<Report>". A struct
// whose LuaFields has a `name` that is a Lua identifier is declared once as a `---@class` in the LuaTypes, and an enum
// whose EnumNames has one as an `---@alias`; unnamed ones are written inline ("{ x: number, y: number }",
// "\"low\"|\"high\"").
//
// Templates, so everything lives in the interface (CODE_STYLE: templates are the exception).
export module imlui.types.lua_convert;

import std;

import lua;
import imlui.color;
import imlui.theme.theme;
import imlui.lua.node;
import imlui.lua.value;
import imlui.types.lua_types;

export namespace imlui {

/// Conversion trait between C++ values and Lua values. See the module comment for the protocol.
template <class T>
struct LuaConvert;

/// The LuaConvert type used for a value of type T passed by reference (ScriptObject::emit arguments):
/// decayed, with string literals and char arrays as `const char*`.
template <class T>
using LuaArgType = std::conditional_t<std::is_same_v<std::decay_t<T>, char*>, const char*, std::decay_t<T>>;

/// A type LuaConvert can push.
template <class T>
concept LuaPushable = requires(lua::State* s, const T& v) { LuaConvert<T>::push(s, v); };

/// A type LuaConvert can both push and read back.
template <class T>
concept LuaReadable = LuaPushable<T> && requires(lua::State* s, int idx) {
    { LuaConvert<T>::check(s, idx) } -> std::same_as<bool>;
    { LuaConvert<T>::get(s, idx) } -> std::convertible_to<T>;
};

/// Whether a Lua function is itself a value of T (LuaConvert<T>::takesFunction: imlui::LuaFunction's, forwarded by
/// std::optional and std::variant). The property sync then passes a function field on as the value instead of calling
/// it as a binding.
template <class T>
constexpr bool takesLuaFunction() {
    if constexpr (requires {
                      { LuaConvert<T>::takesFunction } -> std::convertible_to<bool>;
                  })
        return LuaConvert<T>::takesFunction;
    else
        return false;
}

/// Why the value at `idx` doesn't convert to T (call it after LuaConvert<T>::check() returned false):
/// LuaConvert<T>::mismatch() when the specialization has one, else "<name> expected, got <Lua type>". The text may
/// start with a path into the value ("[2].x: number expected, got string"); see luaMismatchAt().
template <class T>
std::string luaMismatch(lua::State* s, int idx) {
    if constexpr (requires {
                      { LuaConvert<T>::mismatch(s, idx) } -> std::convertible_to<std::string>;
                  }) {
        return LuaConvert<T>::mismatch(s, idx);
    } else {
        std::string out{LuaConvert<T>::name};
        out += " expected, got ";
        out += lua::typeName(s, idx);
        return out;
    }
}

/// Whether LuaConvert<T> compares in place (has equals()).
template <class T>
concept LuaComparable = requires(lua::State* s, int idx, const T& v) {
    { LuaConvert<T>::equals(s, idx, v) } -> std::same_as<bool>;
};

/// Whether the value at `idx` converts to a value equal to `v` (see the module comment): LuaConvert<T>::equals() when
/// the specialization has one, else check() and get() compared with ==. False when T can't be read or compared.
template <class T>
bool luaEquals(lua::State* s, int idx, const T& v) {
    if constexpr (LuaComparable<T>)
        return LuaConvert<T>::equals(s, idx, v);
    else if constexpr (LuaReadable<T> && std::equality_comparable<T>)
        return LuaConvert<T>::check(s, idx) && LuaConvert<T>::get(s, idx) == v;
    else
        return false;
}

/// The LuaLS type expression of T's values (see the module comment): LuaConvert<T>::luaType(types) or luaType() when
/// the specialization has one, else LuaTypes::fromName(LuaConvert<T>::name).
template <class T>
std::string luaTypeOf(LuaTypes& types) {
    if constexpr (requires {
                      { LuaConvert<T>::luaType(types) } -> std::convertible_to<std::string>;
                  })
        return LuaConvert<T>::luaType(types);
    else if constexpr (requires {
                           { LuaConvert<T>::luaType() } -> std::convertible_to<std::string>;
                       })
        return LuaConvert<T>::luaType();
    else
        return LuaTypes::fromName(LuaConvert<T>::name);
}

/// luaTypeOf<T> as a LuaTypes::Describe, which TypeInfo stores.
template <class T>
constexpr LuaTypes::Describe luaTypeDescriber() {
    return [](LuaTypes& types) { return luaTypeOf<T>(types); };
}

/// `why` (a luaMismatch() text) at `where`: "Gauge.value: number expected, got string", or with a path in `why`
/// "Plot.points[3].x: number expected, got string". An empty `where` gives `why` with a leading '.' dropped.
[[nodiscard]] std::string luaMismatchAt(std::string_view where, std::string_view why);

/// The mismatch text `why` of a value of type `typeName` with no place of its own (a method argument, a result): a
/// path is rooted at the type's name when that is a plain identifier (a struct's LuaFields name:
/// "CrunchOptions.method: ..."), else stands alone ("[2]: integer expected, got string").
[[nodiscard]] std::string luaMismatchOf(std::string_view typeName, std::string_view why);

/// Opt-in names for an enum, which then converts to and from its names as strings. Specialize it with a constexpr
/// list of {value, "name"} pairs, and optionally a `name` for error messages and the Lua definitions (the default
/// lists the names) and a `doc` for the definitions (the hover text of the `---@alias`):
///
///   template <> struct imlui::EnumNames<Quality> {
///       static constexpr std::string_view name = "Quality";
///       static constexpr std::string_view doc = "How carefully it is drawn.";
///       static constexpr std::pair<Quality, std::string_view> values[] = {{Quality::Low, "low"},
///                                                                          {Quality::High, "high"}};
///   };
///
/// A value pushes its name (a value without one pushes its underlying integer); reading accepts the names only.
template <class E>
struct EnumNames;

/// A struct field of LuaFields<T>: its Lua key, member pointer and doc. Made by imlui::field().
template <class T, class M>
struct LuaField {
    const char* name; ///< NUL-terminated (a string literal)
    M T::* member;
    /// What the field is, for the Lua definitions (the `---` lines above its `---@field`); "" when undocumented.
    std::string_view description;

    /// This field with `text` as its doc, for the Lua definitions: `imlui::field("x", &Vec2::x).doc("Across, dp.")`.
    [[nodiscard]] constexpr LuaField doc(std::string_view text) const {
        LuaField out = *this;
        out.description = text;
        return out;
    }
};

/// A field of LuaFields<T>::fields: `imlui::field("x", &Vec2::x)`, documented with `.doc("...")`.
template <class T, class M>
constexpr LuaField<T, M> field(const char* name, M T::* member) {
    return {name, member, {}};
}

/// Opt-in field list for a struct, which then converts to and from a table with those keys. Specialize it with a
/// constexpr tuple of fields, and optionally a `name` for error messages and the Lua definitions (the default lists
/// the keys, "{x, y}"), a `doc` for the definitions and `outputOnly` (see below):
///
///   template <> struct imlui::LuaFields<Vec2> {
///       static constexpr std::string_view name = "Vec2";
///       static constexpr std::string_view doc = "A point, in dp.";
///       static constexpr std::tuple fields{imlui::field("x", &Vec2::x).doc("Across."),
///                                          imlui::field("y", &Vec2::y).doc("Down.")};
///   };
///
/// T must be default-constructible. Reading starts from T{}: a missing (nil) field keeps its default member value,
/// except a std::optional field, which reads nil as std::nullopt. A present field of the wrong type fails check().
/// Keys that aren't fields are ignored. Pushing writes every field (a std::nullopt one stays absent).
///
/// In the Lua definitions a named struct is a `---@class` with the docs as hover text. Its fields are required
/// (`---@field id integer`) when the generator sees the struct only going to Lua (a result, a signal argument, a
/// read-only property, a model's rows: LuaTypes::Flow::Output), else optional (`---@field id? integer`), since a
/// table from Lua may leave fields out; a std::optional field is optional either way. `static constexpr bool
/// outputOnly = true;` makes them required wherever the struct is used (one that reaches Lua some way the generator
/// can't see).
template <class T>
struct LuaFields;

/// An enum with EnumNames<E>.
template <class E>
concept LuaNamedEnum = std::is_enum_v<E> && requires {
    { EnumNames<E>::values };
};

/// A struct with LuaFields<T>.
template <class T>
concept LuaStruct = std::is_class_v<T> && std::default_initializable<T> && requires {
    { LuaFields<T>::fields };
};

/// Enters one table level of a check() or mismatch() while it lives (a count per thread). ok() is false past maxDepth:
/// the table then fails check(). The built-in conversions of tables use it; a LuaConvert of your own that recurses into
/// nested tables should too (see the module comment).
class LuaNesting {
public:
    /// How deep check() and mismatch() follow nested tables.
    static constexpr int maxDepth = 200;

    LuaNesting();
    ~LuaNesting();
    LuaNesting(const LuaNesting&) = delete;
    LuaNesting& operator=(const LuaNesting&) = delete;
    LuaNesting(LuaNesting&&) = delete;
    LuaNesting& operator=(LuaNesting&&) = delete;
    [[nodiscard]] bool ok() const { return m_ok; }

private:
    bool m_ok;
};

template <>
struct LuaConvert<bool> {
    static constexpr std::string_view name = "boolean";
    static std::string luaType() { return "boolean"; }
    static void push(lua::State* s, bool v) { lua::pushboolean(s, v); }
    /// Lua truthiness: every value converts (nil and false are false).
    static bool check(lua::State*, int) { return true; }
    static bool get(lua::State* s, int idx) { return lua::toboolean(s, idx); }
    static bool equals(lua::State* s, int idx, bool v) { return lua::toboolean(s, idx) == v; }
};

template <class T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
struct LuaConvert<T> {
    static constexpr std::string_view name = "integer";
    static std::string luaType() { return "integer"; }
    static void push(lua::State* s, T v) { lua::pushinteger(s, static_cast<lua::Integer>(v)); }
    /// Numbers and numeric strings, like Node::number().
    static bool check(lua::State* s, int idx) { return lua::isnumber(s, idx); }
    /// Truncates toward zero and clamps to T's range.
    static T get(lua::State* s, int idx) {
        if (lua::isinteger(s, idx)) {
            const lua::Integer i = lua::tointeger(s, idx);
            if constexpr (std::is_signed_v<T>) {
                if (i < static_cast<lua::Integer>(std::numeric_limits<T>::min())) return std::numeric_limits<T>::min();
                if (static_cast<long double>(i) > static_cast<long double>(std::numeric_limits<T>::max()))
                    return std::numeric_limits<T>::max();
            } else {
                if (i < 0) return 0;
                if (static_cast<unsigned long long>(i) > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
                    return std::numeric_limits<T>::max();
            }
            return static_cast<T>(i);
        }
        const double d = lua::tonumber(s, idx);
        if (!(d == d)) return T{}; // NaN
        if (d <= static_cast<double>(std::numeric_limits<T>::min())) return std::numeric_limits<T>::min();
        if (d >= static_cast<double>(std::numeric_limits<T>::max())) return std::numeric_limits<T>::max();
        return static_cast<T>(d);
    }
    static bool equals(lua::State* s, int idx, T v) { return check(s, idx) && get(s, idx) == v; }
};

template <std::floating_point T>
struct LuaConvert<T> {
    static constexpr std::string_view name = "number";
    static std::string luaType() { return "number"; }
    /// Whole numbers push as Lua integers, like luaui::pushValue (what Node::set and Node::emit use), so a
    /// value written back reads the same in Lua (`tostring(5)` is "5", not "5.0").
    static void push(lua::State* s, T v) {
        const auto d = static_cast<lua::Number>(v);
        constexpr auto limit = static_cast<lua::Number>(std::numeric_limits<lua::Integer>::max());
        if (d >= -limit && d < limit && static_cast<lua::Number>(static_cast<lua::Integer>(d)) == d)
            lua::pushinteger(s, static_cast<lua::Integer>(d));
        else
            lua::pushnumber(s, d);
    }
    static bool check(lua::State* s, int idx) { return lua::isnumber(s, idx); }
    /// A float is narrowed safely (luaui::toFloat: NaN reads as 0, out-of-range values clamp).
    static T get(lua::State* s, int idx) {
        if constexpr (std::is_same_v<T, float>)
            return luaui::toFloat(lua::tonumber(s, idx));
        else
            return static_cast<T>(lua::tonumber(s, idx));
    }
    static bool equals(lua::State* s, int idx, T v) { return check(s, idx) && get(s, idx) == v; }
};

template <>
struct LuaConvert<std::string> {
    static constexpr std::string_view name = "string";
    static std::string luaType() { return "string"; }
    static void push(lua::State* s, const std::string& v) { lua::pushstring(s, v); }
    /// Strings and numbers (numbers convert, like Node::string()).
    static bool check(lua::State* s, int idx) { return lua::isstring(s, idx); }
    static std::string get(lua::State* s, int idx) {
        if (lua::type(s, idx) == lua::Type::Number) return lua::toStringAny(s, idx); // doesn't convert in place
        return std::string{lua::tostring(s, idx)};
    }
    /// Compares the Lua string's bytes, without a copy.
    static bool equals(lua::State* s, int idx, const std::string& v) {
        switch (lua::type(s, idx)) {
        case lua::Type::String: return lua::tostring(s, idx) == v;
        case lua::Type::Number: return lua::toStringAny(s, idx) == v;
        default: return false;
        }
    }
};

/// Push only: a view can't own what it would read back.
template <>
struct LuaConvert<std::string_view> {
    static constexpr std::string_view name = "string";
    static std::string luaType() { return "string"; }
    static void push(lua::State* s, std::string_view v) { lua::pushstring(s, v); }
};

/// Push only (string literals passed to ScriptObject::emit decay to this).
template <>
struct LuaConvert<const char*> {
    static constexpr std::string_view name = "string";
    static std::string luaType() { return "string"; }
    static void push(lua::State* s, const char* v) {
        if (v) lua::pushstring(s, v);
        else lua::pushnil(s);
    }
};

/// Colors read like every color prop (role name, "role/alpha", hex, {r, g, b, a}) and push as "#RRGGBB[AA]".
template <>
struct LuaConvert<Color> {
    static constexpr std::string_view name = "color";
    /// The `Color` alias of the prelude definitions (a role, "role/alpha", hex or {r, g, b, a}).
    static std::string luaType() { return "Color"; }
    static void push(lua::State* s, const Color& v) { lua::pushstring(s, toHex(v)); }
    static bool check(lua::State* s, int idx) {
        if (lua::type(s, idx) == lua::Type::String) return resolveColor(lua::tostring(s, idx)).has_value();
        return lua::istable(s, idx) && luaui::Node::colorFromTable(s, idx).has_value();
    }
    static Color get(lua::State* s, int idx) {
        if (lua::type(s, idx) == lua::Type::String) return resolveColor(lua::tostring(s, idx)).value_or(Color{});
        return luaui::Node::colorFromTable(s, idx).value_or(Color{});
    }
    /// For a string, what's wrong with it ("Text.color: color expected, got unknown role 'pirmary'").
    static std::string mismatch(lua::State* s, int idx) {
        if (lua::type(s, idx) == lua::Type::String)
            return "color expected, got " + colorSpecProblem(lua::tostring(s, idx));
        return std::string{"color expected, got "} + lua::typeName(s, idx);
    }
};

/// nil <-> std::nullopt. An omitted argument (an index past the top) reads as nil too.
template <class T>
struct LuaConvert<std::optional<T>> {
    static constexpr std::string_view name = LuaConvert<T>::name;
    static constexpr bool takesFunction = takesLuaFunction<T>();
    static std::string luaType(LuaTypes& types) { return LuaTypes::optional(luaTypeOf<T>(types)); }
    static void push(lua::State* s, const std::optional<T>& v) {
        if (v) LuaConvert<T>::push(s, *v);
        else lua::pushnil(s);
    }
    static bool check(lua::State* s, int idx) { return lua::isnoneornil(s, idx) || LuaConvert<T>::check(s, idx); }
    static std::optional<T> get(lua::State* s, int idx) {
        if (lua::isnoneornil(s, idx)) return std::nullopt;
        return LuaConvert<T>::get(s, idx);
    }
    static std::string mismatch(lua::State* s, int idx) { return luaMismatch<T>(s, idx); }
    static bool equals(lua::State* s, int idx, const std::optional<T>& v)
        requires std::equality_comparable<T>
    {
        if (lua::isnoneornil(s, idx)) return !v.has_value();
        return v.has_value() && luaEquals<T>(s, idx, *v);
    }
};

} // namespace imlui

namespace imlui::lua_convert_detail {

/// Writes a type name into `out`, or only counts its length (`out` null): a name is built twice at compile time, once
/// to size its storage and once to fill it.
struct NameWriter {
    char* out = nullptr;
    std::size_t size = 0;
    constexpr NameWriter& operator<<(std::string_view text) {
        for (const char c : text) {
            if (out) out[size] = c;
            ++size;
        }
        return *this;
    }
};

/// The name `Build::write(NameWriter&)` writes, built at compile time and kept in static storage (builtName<Build>).
template <class Build>
constexpr std::size_t builtNameSize() {
    NameWriter w;
    Build::write(w);
    return w.size;
}
template <class Build>
constexpr std::array<char, builtNameSize<Build>() + 1> builtNameChars() {
    std::array<char, builtNameSize<Build>() + 1> chars{};
    NameWriter w{chars.data()};
    Build::write(w);
    return chars;
}
template <class Build>
inline constexpr std::array<char, builtNameSize<Build>() + 1> builtNameStorage = builtNameChars<Build>();
template <class Build>
inline constexpr std::string_view builtName{builtNameStorage<Build>.data(), builtNameSize<Build>()};

/// Writes `parts` separated by `separator`.
template <class... Parts>
constexpr void writeJoined(NameWriter& w, std::string_view separator, const Parts&... parts) {
    bool first = true;
    ((w << (first ? std::string_view{} : separator) << std::string_view{parts}, first = false), ...);
}

/// 'low'|'mid'|'high'
template <class E>
struct EnumName {
    static constexpr void write(NameWriter& w) {
        bool first = true;
        for (const auto& [value, text] : EnumNames<E>::values) {
            w << (first ? "'" : "|'") << text << "'";
            first = false;
        }
    }
};

/// {x, y}
template <class T>
struct StructName {
    static constexpr void write(NameWriter& w) {
        w << "{";
        std::apply([&](const auto&... f) { writeJoined(w, ", ", f.name...); }, LuaFields<T>::fields);
        w << "}";
    }
};

/// {integer...}
template <class T>
struct SequenceName {
    static constexpr void write(NameWriter& w) { w << "{" << LuaConvert<T>::name << "...}"; }
};

/// {[string] = integer}
template <class T>
struct MapName {
    static constexpr void write(NameWriter& w) { w << "{[string] = " << LuaConvert<T>::name << "}"; }
};

/// {integer, string}
template <class... Ts>
struct TupleName {
    static constexpr void write(NameWriter& w) {
        w << "{";
        writeJoined(w, ", ", LuaConvert<Ts>::name...);
        w << "}";
    }
};

/// integer|string
template <class... Ts>
struct VariantName {
    static constexpr void write(NameWriter& w) { writeJoined(w, "|", LuaConvert<Ts>::name...); }
};

template <class T>
inline constexpr bool isOptional = false;
template <class T>
inline constexpr bool isOptional<std::optional<T>> = true;

/// What mismatch() says past LuaNesting::maxDepth.
[[nodiscard]] std::string tooDeep();

/// The first key of the table at `tbl` (absolute) that isn't an integer in 1..max, described for a message
/// (`key "x"`, `key 0`, `a boolean key`); empty when every key is one. Raw (lua::next): never runs Lua code.
[[nodiscard]] std::string nonSequenceKey(lua::State* s, int tbl, lua::Integer max);
/// Whether every key of the table at `tbl` (absolute) is an integer in 1..max (holes allowed): a sequence of at most
/// `max` elements.
[[nodiscard]] bool sequenceKeys(lua::State* s, int tbl, lua::Integer max);
/// The key at `idx` described for a message, like nonSequenceKey().
[[nodiscard]] std::string describeKey(lua::State* s, int idx);
/// `"text"`, cut after 40 bytes (`"text..."`).
[[nodiscard]] std::string quoted(std::string_view text);
/// "<name> expected, got <Lua type>".
[[nodiscard]] std::string expectedGot(std::string_view name, lua::State* s, int idx);
/// Whether a mismatch text starts with a path ('.' or '[').
[[nodiscard]] bool hasPath(std::string_view why);
/// `segment` ("[3]", ".x") followed by `why`: "[3].x: ..." when `why` has a path, else "[3]: ...".
[[nodiscard]] std::string prefixPath(std::string_view segment, std::string_view why);
/// The path segment of a string key: ".name" for an identifier, else `["some key"]`.
[[nodiscard]] std::string keySegment(std::string_view key);

/// The mismatch of t[i] for the table at `tbl` (absolute), prefixed with "[i]".
template <class T>
std::string elementMismatch(lua::State* s, int tbl, lua::Integer i) {
    lua::rawgeti(s, tbl, i);
    std::string why = prefixPath("[" + std::to_string(i) + "]", luaMismatch<T>(s, -1));
    lua::pop(s);
    return why;
}

/// LuaConvert<T>::check of t[i] for the table at `tbl` (absolute), read raw.
template <class T>
bool checkElement(lua::State* s, int tbl, lua::Integer i) {
    lua::rawgeti(s, tbl, i);
    const bool ok = LuaConvert<T>::check(s, -1);
    lua::pop(s);
    return ok;
}

/// LuaConvert<T>::get of t[i] for the table at `tbl` (absolute), read raw.
template <class T>
T getElement(lua::State* s, int tbl, lua::Integer i) {
    lua::rawgeti(s, tbl, i);
    T v = LuaConvert<T>::get(s, -1);
    lua::pop(s);
    return v;
}

/// Sets `why` to t[i]'s mismatch and returns true when t[i] (the table at `tbl`, absolute) doesn't convert to T.
template <class T>
bool elementFails(lua::State* s, int tbl, lua::Integer i, std::string& why) {
    if (checkElement<T>(s, tbl, i)) return false;
    why = elementMismatch<T>(s, tbl, i);
    return true;
}

/// A table with string keys <-> a map keyed by std::string (std::map, std::unordered_map).
template <class Map>
struct MapConvert {
    using Value = typename Map::mapped_type;
    static constexpr std::string_view name = builtName<MapName<Value>>;
    static std::string luaType(LuaTypes& types) { return "table<string, " + luaTypeOf<Value>(types) + ">"; }
    static void push(lua::State* s, const Map& m) {
        lua::checkstack(s, 3);
        lua::createtable(s, 0, static_cast<int>(m.size()));
        for (const auto& [key, value] : m) {
            lua::pushstring(s, key);
            LuaConvert<Value>::push(s, value);
            lua::rawset(s, -3);
        }
    }
    /// Every key must be a string (a number key fails: `{ "a" }` is no map) and every value convert.
    static bool check(lua::State* s, int idx) {
        const LuaNesting nesting;
        if (!nesting.ok() || !lua::istable(s, idx) || !lua::checkstack(s, 3)) return false;
        idx = lua::absindex(s, idx);
        lua::pushnil(s);
        while (lua::next(s, idx) != 0) {
            if (lua::type(s, -2) != lua::Type::String || !LuaConvert<Value>::check(s, -1)) {
                lua::pop(s, 2);
                return false;
            }
            lua::pop(s);
        }
        return true;
    }
    /// The first key that isn't a string, or the first value that doesn't convert: `.x: number expected, got string`.
    static std::string mismatch(lua::State* s, int idx) {
        const LuaNesting nesting;
        if (!nesting.ok()) return tooDeep();
        if (!lua::istable(s, idx) || !lua::checkstack(s, 3)) return expectedGot(name, s, idx);
        idx = lua::absindex(s, idx);
        lua::pushnil(s);
        while (lua::next(s, idx) != 0) {
            std::string why;
            if (lua::type(s, -2) != lua::Type::String)
                why = std::string{name} + " expected, got table with " + describeKey(s, -2);
            else if (!LuaConvert<Value>::check(s, -1))
                why = prefixPath(keySegment(lua::tostring(s, -2)), luaMismatch<Value>(s, -1));
            if (!why.empty()) {
                lua::pop(s, 2);
                return why;
            }
            lua::pop(s);
        }
        return expectedGot(name, s, idx);
    }
    static Map get(lua::State* s, int idx) {
        lua::checkstack(s, 3);
        idx = lua::absindex(s, idx);
        Map out;
        lua::pushnil(s);
        while (lua::next(s, idx) != 0) {
            out.insert_or_assign(std::string{lua::tostring(s, -2)}, LuaConvert<Value>::get(s, -1));
            lua::pop(s);
        }
        return out;
    }
};

/// A Lua sequence {a, b, ...} <-> std::pair / std::tuple: element i converts as the i-th type. A longer sequence, or a
/// table with other keys, fails check(); missing trailing elements are nil (which a std::optional element takes).
template <class Tuple, class... Ts>
struct TupleConvert {
    static constexpr std::string_view name = builtName<TupleName<Ts...>>;
    /// A LuaLS tuple: "[integer, string]".
    static std::string luaType(LuaTypes& types) {
        std::string out = "[";
        bool first = true;
        ((out += (first ? "" : ", ") + luaTypeOf<Ts>(types), first = false), ...);
        return out + "]";
    }
    static void push(lua::State* s, const Tuple& v) {
        lua::checkstack(s, 2);
        lua::createtable(s, static_cast<int>(sizeof...(Ts)), 0);
        std::apply(
            [&](const auto&... e) {
                lua::Integer i = 0;
                ((LuaConvert<Ts>::push(s, e), lua::rawseti(s, -2, ++i)), ...);
            },
            v);
    }
    static bool check(lua::State* s, int idx) {
        const LuaNesting nesting;
        if (!nesting.ok() || !lua::istable(s, idx) || !lua::checkstack(s, 3)) return false;
        idx = lua::absindex(s, idx);
        if (!sequenceKeys(s, idx, static_cast<lua::Integer>(sizeof...(Ts)))) return false;
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (checkElement<Ts>(s, idx, static_cast<lua::Integer>(I + 1)) && ...);
        }(std::index_sequence_for<Ts...>{});
    }
    static std::string mismatch(lua::State* s, int idx) {
        const LuaNesting nesting;
        if (!nesting.ok()) return tooDeep();
        if (!lua::istable(s, idx) || !lua::checkstack(s, 3)) return expectedGot(name, s, idx);
        idx = lua::absindex(s, idx);
        if (std::string key = nonSequenceKey(s, idx, static_cast<lua::Integer>(sizeof...(Ts))); !key.empty())
            return std::string{name} + " expected, got table with " + key;
        std::string why;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            // `||` stops at the first element that doesn't convert.
            static_cast<void>((elementFails<Ts>(s, idx, static_cast<lua::Integer>(I + 1), why) || ...));
        }(std::index_sequence_for<Ts...>{});
        return why.empty() ? expectedGot(name, s, idx) : why;
    }
    static Tuple get(lua::State* s, int idx) {
        lua::checkstack(s, 2);
        idx = lua::absindex(s, idx);
        // Braced initialization: the elements are read in order.
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return Tuple{getElement<Ts>(s, idx, static_cast<lua::Integer>(I + 1))...};
        }(std::index_sequence_for<Ts...>{});
    }
};

} // namespace imlui::lua_convert_detail

export namespace imlui {

/// A Lua sequence {v1, v2, ...}: a table whose keys are all integers 1..n (`{}` is an empty one; `{x = 1}` and
/// `{1, x = 2}` are no sequences). Reading checks every element.
template <class T>
struct LuaConvert<std::vector<T>> {
    static constexpr std::string_view name = lua_convert_detail::builtName<lua_convert_detail::SequenceName<T>>;
    static std::string luaType(LuaTypes& types) { return LuaTypes::grouped(luaTypeOf<T>(types)) + "[]"; }
    static void push(lua::State* s, const std::vector<T>& v) {
        lua::checkstack(s, 2);
        lua::createtable(s, static_cast<int>(v.size()), 0);
        lua::Integer i = 0;
        for (const auto& e : v) {
            LuaConvert<T>::push(s, e);
            lua::rawseti(s, -2, ++i);
        }
    }
    static bool check(lua::State* s, int idx) {
        const LuaNesting nesting;
        if (!nesting.ok() || !lua::istable(s, idx) || !lua::checkstack(s, 3)) return false;
        idx = lua::absindex(s, idx);
        const lua::Integer n = lua::rawlen(s, idx);
        if (!lua_convert_detail::sequenceKeys(s, idx, n)) return false;
        for (lua::Integer i = 1; i <= n; ++i)
            if (!lua_convert_detail::checkElement<T>(s, idx, i)) return false;
        return true;
    }
    /// A key that isn't an index (`got table with key "x"`), or the first element that doesn't convert
    /// (`[3].x: number expected, got string`).
    static std::string mismatch(lua::State* s, int idx) {
        const LuaNesting nesting;
        if (!nesting.ok()) return lua_convert_detail::tooDeep();
        if (!lua::istable(s, idx) || !lua::checkstack(s, 3)) return lua_convert_detail::expectedGot(name, s, idx);
        idx = lua::absindex(s, idx);
        const lua::Integer n = lua::rawlen(s, idx);
        if (std::string key = lua_convert_detail::nonSequenceKey(s, idx, n); !key.empty())
            return std::string{name} + " expected, got table with " + key;
        for (lua::Integer i = 1; i <= n; ++i)
            if (!lua_convert_detail::checkElement<T>(s, idx, i))
                return lua_convert_detail::elementMismatch<T>(s, idx, i);
        return lua_convert_detail::expectedGot(name, s, idx);
    }
    static std::vector<T> get(lua::State* s, int idx) {
        lua::checkstack(s, 2);
        idx = lua::absindex(s, idx);
        const lua::Integer n = lua::rawlen(s, idx);
        std::vector<T> out;
        out.reserve(static_cast<std::size_t>(n));
        for (lua::Integer i = 1; i <= n; ++i)
            out.push_back(lua_convert_detail::getElement<T>(s, idx, i));
        return out;
    }
    /// One pass over the table (lua::next), comparing each element in place: a sequence of exactly v.size() elements
    /// (no holes), each equal to v's.
    static bool equals(lua::State* s, int idx, const std::vector<T>& v)
        requires std::equality_comparable<T>
    {
        if (!lua::istable(s, idx) || !lua::checkstack(s, 3)) return false;
        idx = lua::absindex(s, idx);
        const auto n = static_cast<lua::Integer>(v.size());
        if (lua::rawlen(s, idx) != n) return false;
        lua::Integer seen = 0;
        lua::pushnil(s);
        while (lua::next(s, idx) != 0) {
            const lua::Integer k = lua::isinteger(s, -2) ? lua::tointeger(s, -2) : 0;
            if (k < 1 || k > n || !luaEquals<T>(s, -1, v[static_cast<std::size_t>(k - 1)])) {
                lua::pop(s, 2);
                return false;
            }
            ++seen;
            lua::pop(s);
        }
        return seen == n;
    }
};

/// An enum with EnumNames<E>: its names, as strings.
template <LuaNamedEnum E>
struct LuaConvert<E> {
    static constexpr std::string_view name = [] {
        if constexpr (requires { std::string_view{EnumNames<E>::name}; })
            return std::string_view{EnumNames<E>::name};
        else
            return lua_convert_detail::builtName<lua_convert_detail::EnumName<E>>;
    }();
    /// A union of string literals, declared as an `---@alias` when EnumNames<E> has a `name` that is an identifier.
    static std::string luaType(LuaTypes& types) {
        std::string names;
        for (const auto& [value, text] : EnumNames<E>::values)
            names += (names.empty() ? "" : "|") + LuaTypes::quoted(text);
        if (names.empty()) names = "string";
        if constexpr (requires { std::string_view{EnumNames<E>::name}; }) {
            const std::string_view alias = EnumNames<E>::name;
            if (LuaTypes::isIdentifier(alias)) {
                if (types.claim(alias))
                    types.define(alias, LuaTypes::docLines(enumDoc()) + "---@alias " + std::string{alias} + " " +
                                            names + "\n");
                return std::string{alias};
            }
        }
        return names;
    }
    static void push(lua::State* s, E v) {
        for (const auto& [value, text] : EnumNames<E>::values) {
            if (value == v) {
                lua::pushstring(s, text);
                return;
            }
        }
        // No name: the underlying integer, so the value isn't lost (it doesn't read back).
        lua::pushinteger(s, static_cast<lua::Integer>(static_cast<std::underlying_type_t<E>>(v)));
    }
    /// One of the names, as a string (case-sensitive). Numbers don't convert.
    static bool check(lua::State* s, int idx) { return find(s, idx).has_value(); }
    static E get(lua::State* s, int idx) { return find(s, idx).value_or(E{}); }
    /// Shows the string it got and, for a named enum, its names too: `'low'|'high' expected, got string "medium"`,
    /// `Quality ('low'|'high') expected, got string "medium"`.
    static std::string mismatch(lua::State* s, int idx) {
        if (lua::type(s, idx) != lua::Type::String) return lua_convert_detail::expectedGot(name, s, idx);
        return expectedNames() + " expected, got string " + lua_convert_detail::quoted(lua::tostring(s, idx));
    }

private:
    /// What a mismatch says was expected: the names, after the enum's own name when it has one.
    static std::string expectedNames() {
        constexpr std::string_view names = lua_convert_detail::builtName<lua_convert_detail::EnumName<E>>;
        if (name == names) return std::string{names};
        return std::string{name} + " (" + std::string{names} + ")";
    }
    static constexpr std::string_view enumDoc() {
        if constexpr (requires { std::string_view{EnumNames<E>::doc}; })
            return EnumNames<E>::doc;
        else
            return {};
    }
    static std::optional<E> find(lua::State* s, int idx) {
        if (lua::type(s, idx) != lua::Type::String) return std::nullopt;
        const std::string_view text = lua::tostring(s, idx);
        for (const auto& [value, n] : EnumNames<E>::values)
            if (n == text) return value;
        return std::nullopt;
    }
};

/// A struct with LuaFields<T>: a table with its fields as string keys (see LuaFields).
template <LuaStruct T>
struct LuaConvert<T> {
    static constexpr std::string_view name = [] {
        if constexpr (requires { std::string_view{LuaFields<T>::name}; })
            return std::string_view{LuaFields<T>::name};
        else
            return lua_convert_detail::builtName<lua_convert_detail::StructName<T>>;
    }();
    /// A `---@class` with a field per LuaField when LuaFields<T> has a `name` that is an identifier, else an inline
    /// table type ("{ x: number, y: number }"). The class's fields are required when the struct is output only (see
    /// LuaFields), else optional: a missing one keeps its default. Records the use in the current flow (LuaTypes::use)
    /// and describes the fields in it, so the structs inside get it too (Output for an `outputOnly` one).
    static std::string luaType(LuaTypes& types) {
        if constexpr (requires { std::string_view{LuaFields<T>::name}; }) {
            const std::string_view cls = LuaFields<T>::name;
            if (LuaTypes::isIdentifier(cls)) {
                const LuaTypes::Flow outer = types.flow();
                if constexpr (declaredOutputOnly()) types.setFlow(LuaTypes::Flow::Output);
                const bool newUse = types.use(cls);
                if (types.claim(cls))
                    types.define(cls, declaration(cls, types));
                else if (newUse) // a use in another flow: the structs inside get it too
                    std::apply([&](const auto&... f) { (static_cast<void>(fieldLuaType(f, types)), ...); },
                               LuaFields<T>::fields);
                types.setFlow(outer);
                return std::string{cls};
            }
        }
        std::string out = "{ ";
        bool first = true;
        std::apply(
            [&](const auto&... f) {
                ((out += (first ? "" : ", ") + std::string{f.name} + ": " + fieldLuaType(f, types), first = false),
                 ...);
            },
            LuaFields<T>::fields);
        return out + " }";
    }
    static void push(lua::State* s, const T& v) {
        lua::checkstack(s, 2);
        constexpr auto count = std::tuple_size_v<std::remove_cvref_t<decltype(LuaFields<T>::fields)>>;
        lua::createtable(s, 0, static_cast<int>(count));
        std::apply([&](const auto&... f) { (pushField(s, v.*(f.member), f.name), ...); }, LuaFields<T>::fields);
    }
    /// A table whose fields convert (read raw). A missing (nil) field passes: it keeps its default.
    static bool check(lua::State* s, int idx) {
        const LuaNesting nesting;
        if (!nesting.ok() || !lua::istable(s, idx) || !lua::checkstack(s, 2)) return false;
        idx = lua::absindex(s, idx);
        return std::apply([&](const auto&... f) { return (checkField(s, idx, f) && ...); }, LuaFields<T>::fields);
    }
    /// The first present field that doesn't convert: `.from.x: number expected, got string`.
    static std::string mismatch(lua::State* s, int idx) {
        const LuaNesting nesting;
        if (!nesting.ok()) return lua_convert_detail::tooDeep();
        if (!lua::istable(s, idx) || !lua::checkstack(s, 2)) return lua_convert_detail::expectedGot(name, s, idx);
        idx = lua::absindex(s, idx);
        std::string why;
        std::apply([&](const auto&... f) { static_cast<void>((fieldFails(s, idx, f, why) || ...)); },
                   LuaFields<T>::fields);
        return why.empty() ? lua_convert_detail::expectedGot(name, s, idx) : why;
    }
    static T get(lua::State* s, int idx) {
        lua::checkstack(s, 2);
        idx = lua::absindex(s, idx);
        T out{};
        std::apply([&](const auto&... f) { (getField(s, idx, out.*(f.member), f.name), ...); }, LuaFields<T>::fields);
        return out;
    }

private:
    static constexpr bool declaredOutputOnly() {
        if constexpr (requires { bool{LuaFields<T>::outputOnly}; })
            return LuaFields<T>::outputOnly;
        else
            return false;
    }
    static constexpr std::string_view structDoc() {
        if constexpr (requires { std::string_view{LuaFields<T>::doc}; })
            return LuaFields<T>::doc;
        else
            return {};
    }
    /// The `---@class` of named struct `cls`: its doc, and a `---@field` with its doc per field.
    static std::string declaration(std::string_view cls, LuaTypes& types) {
        const bool required = declaredOutputOnly() || types.outputOnly(cls);
        std::string decl = LuaTypes::docLines(structDoc()) + "---@class " + std::string{cls} + "\n";
        std::apply([&](const auto&... f) { ((decl += fieldDeclaration(f, required, types)), ...); },
                   LuaFields<T>::fields);
        return decl;
    }
    /// "---@field name type", or "name? type" when it is optional (not `required`, or a std::optional field). A
    /// function type is in parentheses: after a line that ends in a bare `fun(...)`, LuaLS (3.19) looks for its return
    /// type on the next line and loses the doc there.
    template <class C, class M>
    static std::string fieldDeclaration(const LuaField<C, M>& f, bool required, LuaTypes& types) {
        const std::string type = fieldLuaType(f, types);
        const bool optional = !required || type.ends_with('?');
        std::string shown{LuaTypes::required(type)};
        if (shown.starts_with("fun(")) shown = LuaTypes::grouped(shown);
        return LuaTypes::docLines(f.description) + "---@field " + f.name + (optional ? "? " : " ") + shown + "\n";
    }
    template <class C, class M>
    static std::string fieldLuaType(const LuaField<C, M>&, LuaTypes& types) {
        return luaTypeOf<M>(types);
    }
    template <class M>
    static void pushField(lua::State* s, const M& v, const char* key) {
        LuaConvert<M>::push(s, v);
        lua::setfield(s, -2, key);
    }
    template <class C, class M>
    static bool checkField(lua::State* s, int tbl, const LuaField<C, M>& f) {
        const lua::Type t = lua::rawgetfield(s, tbl, f.name);
        const bool ok = t == lua::Type::Nil || LuaConvert<M>::check(s, -1);
        lua::pop(s);
        return ok;
    }
    /// Sets `why` and returns true when field `f` is present and doesn't convert.
    template <class C, class M>
    static bool fieldFails(lua::State* s, int tbl, const LuaField<C, M>& f, std::string& why) {
        const lua::Type t = lua::rawgetfield(s, tbl, f.name);
        if (t != lua::Type::Nil && !LuaConvert<M>::check(s, -1))
            why = lua_convert_detail::prefixPath(std::string{"."} + f.name, luaMismatch<M>(s, -1));
        lua::pop(s);
        return !why.empty();
    }
    /// A nil field keeps `out`'s default, unless it's a std::optional (nil is std::nullopt).
    template <class M>
    static void getField(lua::State* s, int tbl, M& out, const char* key) {
        const lua::Type t = lua::rawgetfield(s, tbl, key);
        if (t != lua::Type::Nil || lua_convert_detail::isOptional<M>) out = LuaConvert<M>::get(s, -1);
        lua::pop(s);
    }
};

/// A table with string keys (every key must be a string).
template <class T, class Compare, class Alloc>
struct LuaConvert<std::map<std::string, T, Compare, Alloc>>
    : lua_convert_detail::MapConvert<std::map<std::string, T, Compare, Alloc>> {};

/// A table with string keys (every key must be a string).
template <class T, class Hash, class Equal, class Alloc>
struct LuaConvert<std::unordered_map<std::string, T, Hash, Equal, Alloc>>
    : lua_convert_detail::MapConvert<std::unordered_map<std::string, T, Hash, Equal, Alloc>> {};

/// A sequence {first, second}.
template <class A, class B>
struct LuaConvert<std::pair<A, B>> : lua_convert_detail::TupleConvert<std::pair<A, B>, A, B> {};

/// A sequence {a, b, ...}.
template <class... Ts>
struct LuaConvert<std::tuple<Ts...>> : lua_convert_detail::TupleConvert<std::tuple<Ts...>, Ts...> {};

/// nil: the empty alternative of a std::variant.
template <>
struct LuaConvert<std::monostate> {
    static constexpr std::string_view name = "nil";
    static std::string luaType() { return "nil"; }
    static void push(lua::State* s, std::monostate) { lua::pushnil(s); }
    static bool check(lua::State* s, int idx) { return lua::isnoneornil(s, idx); }
    static std::monostate get(lua::State*, int) { return {}; }
};

/// Pushes the active alternative. Reading takes the first alternative whose check() passes, so list the specific ones
/// first: `bool` takes every value and `std::string` takes numbers. `{}` is both an empty sequence and an empty map:
/// whichever of those alternatives comes first takes it.
template <class... Ts>
struct LuaConvert<std::variant<Ts...>> {
    static constexpr std::string_view name = lua_convert_detail::builtName<lua_convert_detail::VariantName<Ts...>>;
    static constexpr bool takesFunction = (takesLuaFunction<Ts>() || ...);
    /// "integer|string"; with std::monostate, "nil" is one of the alternatives.
    static std::string luaType(LuaTypes& types) {
        std::string out;
        ((out += (out.empty() ? "" : "|") + LuaTypes::grouped(luaTypeOf<Ts>(types))), ...);
        return out;
    }
    static void push(lua::State* s, const std::variant<Ts...>& v) {
        std::visit([&]<class A>(const A& a) { LuaConvert<A>::push(s, a); }, v);
    }
    static bool check(lua::State* s, int idx) { return (LuaConvert<Ts>::check(s, idx) || ...); }
    /// When exactly one alternative failed inside the value (its mismatch has a path: the value had its shape), that
    /// one's: "[2]: number expected, got string". Otherwise "integer|{x, y} expected, got string".
    static std::string mismatch(lua::State* s, int idx) {
        std::string found;
        int inside = 0;
        const auto look = [&](std::string why) {
            if (lua_convert_detail::hasPath(why) && ++inside == 1) found = std::move(why);
        };
        (look(luaMismatch<Ts>(s, idx)), ...);
        return inside == 1 ? found : lua_convert_detail::expectedGot(name, s, idx);
    }
    static std::variant<Ts...> get(lua::State* s, int idx) {
        std::optional<std::variant<Ts...>> out;
        // `||` stops at the first alternative that passes.
        static_cast<void>((getAs<Ts>(s, idx, out) || ...));
        return out ? std::move(*out) : std::variant<Ts...>{};
    }
    /// get() takes the first alternative whose check() passes: equal when none before v's alternative takes the value
    /// and v's alternative compares equal to it.
    static bool equals(lua::State* s, int idx, const std::variant<Ts...>& v)
        requires(std::equality_comparable<Ts> && ...)
    {
        bool equal = false;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            // `||` stops at v's alternative, or at an earlier one that takes the value.
            static_cast<void>((equalsAt<I>(s, idx, v, equal) || ...));
        }(std::index_sequence_for<Ts...>{});
        return equal;
    }

private:
    /// equals() at alternative I: returns true when it decided (`equal` set).
    template <std::size_t I>
    static bool equalsAt(lua::State* s, int idx, const std::variant<Ts...>& v, bool& equal) {
        using A = std::variant_alternative_t<I, std::variant<Ts...>>;
        if (v.index() == I) {
            equal = luaEquals<A>(s, idx, std::get<I>(v));
            return true;
        }
        return LuaConvert<A>::check(s, idx); // an earlier alternative takes it: not equal (`equal` stays false)
    }

    template <class A>
    static bool getAs(lua::State* s, int idx, std::optional<std::variant<Ts...>>& out) {
        if (!LuaConvert<A>::check(s, idx)) return false;
        out.emplace(std::in_place_type<A>, LuaConvert<A>::get(s, idx));
        return true;
    }
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

bool isIdentifier(std::string_view text) {
    if (text.empty() || (text[0] >= '0' && text[0] <= '9')) return false;
    return std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

} // namespace

namespace {

thread_local int nestingDepth = 0; ///< LuaNesting levels entered on this thread

} // namespace

LuaNesting::LuaNesting() : m_ok{++nestingDepth <= maxDepth} {}
LuaNesting::~LuaNesting() { --nestingDepth; }

std::string luaMismatchAt(std::string_view where, std::string_view why) {
    std::string out{where};
    if (where.empty()) {
        out += why.starts_with('.') ? why.substr(1) : why;
        return out;
    }
    if (!lua_convert_detail::hasPath(why)) out += ": ";
    out += why;
    return out;
}

std::string luaMismatchOf(std::string_view typeName, std::string_view why) {
    const bool rooted = lua_convert_detail::hasPath(why) && isIdentifier(typeName);
    return luaMismatchAt(rooted ? typeName : std::string_view{}, why);
}

} // namespace imlui

namespace imlui::lua_convert_detail {

std::string tooDeep() {
    return "table nested more than " + std::to_string(LuaNesting::maxDepth) + " levels deep (a cycle?)";
}

std::string nonSequenceKey(lua::State* s, int tbl, lua::Integer max) {
    lua::pushnil(s);
    while (lua::next(s, tbl) != 0) {
        if (!lua::isinteger(s, -2) || lua::tointeger(s, -2) < 1 || lua::tointeger(s, -2) > max) {
            std::string key = describeKey(s, -2);
            lua::pop(s, 2);
            return key;
        }
        lua::pop(s);
    }
    return {};
}

bool sequenceKeys(lua::State* s, int tbl, lua::Integer max) {
    lua::pushnil(s);
    while (lua::next(s, tbl) != 0) {
        if (!lua::isinteger(s, -2) || lua::tointeger(s, -2) < 1 || lua::tointeger(s, -2) > max) {
            lua::pop(s, 2);
            return false;
        }
        lua::pop(s);
    }
    return true;
}

std::string describeKey(lua::State* s, int idx) {
    switch (lua::type(s, idx)) {
    case lua::Type::String: return "key " + quoted(lua::tostring(s, idx));
    case lua::Type::Number:
        if (lua::isinteger(s, idx)) return "key " + std::to_string(lua::tointeger(s, idx));
        return std::format("key {}", lua::tonumber(s, idx));
    default: return std::string{"a "} + lua::typeName(s, idx) + " key";
    }
}

std::string quoted(std::string_view text) {
    constexpr std::size_t limit = 40;
    std::string out = "\"";
    out += text.substr(0, limit);
    out += text.size() > limit ? "...\"" : "\"";
    return out;
}

std::string expectedGot(std::string_view name, lua::State* s, int idx) {
    std::string out{name};
    out += " expected, got ";
    out += lua::typeName(s, idx);
    return out;
}

bool hasPath(std::string_view why) { return why.starts_with('.') || why.starts_with('['); }

std::string prefixPath(std::string_view segment, std::string_view why) {
    std::string out{segment};
    if (!hasPath(why)) out += ": ";
    out += why;
    return out;
}

std::string keySegment(std::string_view key) {
    if (isIdentifier(key)) return "." + std::string{key};
    return "[" + quoted(key) + "]";
}

} // namespace imlui::lua_convert_detail
