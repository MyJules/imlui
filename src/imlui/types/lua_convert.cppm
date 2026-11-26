// LuaConvert<T>: how a C++ value crosses into and out of Lua.
//
// The type system (properties, method arguments and results, signal arguments, LuaFunction calls) moves values with
// this trait. Each specialization provides
//
//   static constexpr std::string_view name;        // for error messages: "number", "string", "{x, y}" ...
//   static void push(lua::State*, const T&);       // C++ -> Lua: pushes exactly one value
//   static bool check(lua::State*, int idx);       // can the value at idx become a T?
//   static T get(lua::State*, int idx);            // Lua -> C++ (only called after check())
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
// Tables are read raw (no metamethods), so check() never runs Lua code and never raises.
//
// Templates, so everything lives in the interface (CODE_STYLE: templates are the exception).
export module imlui.types.lua_convert;

import std;

import lua;
import imlui.color;
import imlui.theme.theme;
import imlui.lua.node;
import imlui.lua.value;

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

/// Opt-in names for an enum, which then converts to and from its names as strings. Specialize it with a constexpr
/// list of {value, "name"} pairs, and optionally a `name` for error messages (the default lists the names):
///
///   template <> struct imlui::EnumNames<Quality> {
///       static constexpr std::pair<Quality, std::string_view> values[] = {{Quality::Low, "low"},
///                                                                          {Quality::High, "high"}};
///   };
///
/// A value pushes its name (a value without one pushes its underlying integer); reading accepts the names only.
template <class E>
struct EnumNames;

/// A struct field of LuaFields<T>: its Lua key and member pointer. Made by imlui::field().
template <class T, class M>
struct LuaField {
    const char* name; ///< NUL-terminated (a string literal)
    M T::* member;
};

/// A field of LuaFields<T>::fields: `imlui::field("x", &Vec2::x)`.
template <class T, class M>
constexpr LuaField<T, M> field(const char* name, M T::* member) {
    return {name, member};
}

/// Opt-in field list for a struct, which then converts to and from a table with those keys. Specialize it with a
/// constexpr tuple of fields, and optionally a `name` for error messages (the default lists the keys, "{x, y}"):
///
///   template <> struct imlui::LuaFields<Vec2> {
///       static constexpr std::tuple fields{imlui::field("x", &Vec2::x), imlui::field("y", &Vec2::y)};
///   };
///
/// T must be default-constructible. Reading starts from T{}: a missing (nil) field keeps its default member value,
/// except a std::optional field, which reads nil as std::nullopt. A present field of the wrong type fails check().
/// Keys that aren't fields are ignored. Pushing writes every field (a std::nullopt one stays absent).
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

template <>
struct LuaConvert<bool> {
    static constexpr std::string_view name = "boolean";
    static void push(lua::State* s, bool v) { lua::pushboolean(s, v); }
    /// Lua truthiness: every value converts (nil and false are false).
    static bool check(lua::State*, int) { return true; }
    static bool get(lua::State* s, int idx) { return lua::toboolean(s, idx); }
};

template <class T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
struct LuaConvert<T> {
    static constexpr std::string_view name = "integer";
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
};

template <std::floating_point T>
struct LuaConvert<T> {
    static constexpr std::string_view name = "number";
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
};

template <>
struct LuaConvert<std::string> {
    static constexpr std::string_view name = "string";
    static void push(lua::State* s, const std::string& v) { lua::pushstring(s, v); }
    /// Strings and numbers (numbers convert, like Node::string()).
    static bool check(lua::State* s, int idx) { return lua::isstring(s, idx); }
    static std::string get(lua::State* s, int idx) {
        if (lua::type(s, idx) == lua::Type::Number) return lua::toStringAny(s, idx); // doesn't convert in place
        return std::string{lua::tostring(s, idx)};
    }
};

/// Push only: a view can't own what it would read back.
template <>
struct LuaConvert<std::string_view> {
    static constexpr std::string_view name = "string";
    static void push(lua::State* s, std::string_view v) { lua::pushstring(s, v); }
};

/// Push only (string literals passed to ScriptObject::emit decay to this).
template <>
struct LuaConvert<const char*> {
    static constexpr std::string_view name = "string";
    static void push(lua::State* s, const char* v) {
        if (v) lua::pushstring(s, v);
        else lua::pushnil(s);
    }
};

/// Colors read like every color prop (role name, "role/alpha", hex, {r, g, b, a}) and push as "#RRGGBB[AA]".
template <>
struct LuaConvert<Color> {
    static constexpr std::string_view name = "color";
    static void push(lua::State* s, const Color& v) { lua::pushstring(s, toHex(v)); }
    static bool check(lua::State* s, int idx) {
        if (lua::type(s, idx) == lua::Type::String) return resolveColor(lua::tostring(s, idx)).has_value();
        return lua::istable(s, idx) && luaui::Node::colorFromTable(s, idx).has_value();
    }
    static Color get(lua::State* s, int idx) {
        if (lua::type(s, idx) == lua::Type::String) return resolveColor(lua::tostring(s, idx)).value_or(Color{});
        return luaui::Node::colorFromTable(s, idx).value_or(Color{});
    }
};

/// nil <-> std::nullopt. An omitted argument (an index past the top) reads as nil too.
template <class T>
struct LuaConvert<std::optional<T>> {
    static constexpr std::string_view name = LuaConvert<T>::name;
    static constexpr bool takesFunction = takesLuaFunction<T>();
    static void push(lua::State* s, const std::optional<T>& v) {
        if (v) LuaConvert<T>::push(s, *v);
        else lua::pushnil(s);
    }
    static bool check(lua::State* s, int idx) { return lua::isnoneornil(s, idx) || LuaConvert<T>::check(s, idx); }
    static std::optional<T> get(lua::State* s, int idx) {
        if (lua::isnoneornil(s, idx)) return std::nullopt;
        return LuaConvert<T>::get(s, idx);
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

/// A table with string keys <-> a map keyed by std::string (std::map, std::unordered_map).
template <class Map>
struct MapConvert {
    using Value = typename Map::mapped_type;
    static constexpr std::string_view name = builtName<MapName<Value>>;
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
        if (!lua::istable(s, idx) || !lua::checkstack(s, 3)) return false;
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

/// A Lua sequence {a, b, ...} <-> std::pair / std::tuple: element i converts as the i-th type. A longer sequence fails
/// check(); missing trailing elements are nil (which a std::optional element takes).
template <class Tuple, class... Ts>
struct TupleConvert {
    static constexpr std::string_view name = builtName<TupleName<Ts...>>;
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
        if (!lua::istable(s, idx) || !lua::checkstack(s, 2)) return false;
        idx = lua::absindex(s, idx);
        if (lua::rawlen(s, idx) > static_cast<lua::Integer>(sizeof...(Ts))) return false;
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (checkElement<Ts>(s, idx, static_cast<lua::Integer>(I + 1)) && ...);
        }(std::index_sequence_for<Ts...>{});
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

/// A Lua sequence {v1, v2, ...}. Reading checks every element.
template <class T>
struct LuaConvert<std::vector<T>> {
    static constexpr std::string_view name = lua_convert_detail::builtName<lua_convert_detail::SequenceName<T>>;
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
        if (!lua::istable(s, idx) || !lua::checkstack(s, 2)) return false;
        idx = lua::absindex(s, idx);
        const lua::Integer n = lua::rawlen(s, idx);
        for (lua::Integer i = 1; i <= n; ++i)
            if (!lua_convert_detail::checkElement<T>(s, idx, i)) return false;
        return true;
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

private:
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
    static void push(lua::State* s, const T& v) {
        lua::checkstack(s, 2);
        constexpr auto count = std::tuple_size_v<std::remove_cvref_t<decltype(LuaFields<T>::fields)>>;
        lua::createtable(s, 0, static_cast<int>(count));
        std::apply([&](const auto&... f) { (pushField(s, v.*(f.member), f.name), ...); }, LuaFields<T>::fields);
    }
    /// A table whose fields convert (read raw). A missing (nil) field passes: it keeps its default.
    static bool check(lua::State* s, int idx) {
        if (!lua::istable(s, idx) || !lua::checkstack(s, 2)) return false;
        idx = lua::absindex(s, idx);
        return std::apply([&](const auto&... f) { return (checkField(s, idx, f) && ...); }, LuaFields<T>::fields);
    }
    static T get(lua::State* s, int idx) {
        lua::checkstack(s, 2);
        idx = lua::absindex(s, idx);
        T out{};
        std::apply([&](const auto&... f) { (getField(s, idx, out.*(f.member), f.name), ...); }, LuaFields<T>::fields);
        return out;
    }

private:
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
    static void push(lua::State* s, std::monostate) { lua::pushnil(s); }
    static bool check(lua::State* s, int idx) { return lua::isnoneornil(s, idx); }
    static std::monostate get(lua::State*, int) { return {}; }
};

/// Pushes the active alternative. Reading takes the first alternative whose check() passes, so list the specific ones
/// first: `bool` takes every value and `std::string` takes numbers.
template <class... Ts>
struct LuaConvert<std::variant<Ts...>> {
    static constexpr std::string_view name = lua_convert_detail::builtName<lua_convert_detail::VariantName<Ts...>>;
    static constexpr bool takesFunction = (takesLuaFunction<Ts>() || ...);
    static void push(lua::State* s, const std::variant<Ts...>& v) {
        std::visit([&]<class A>(const A& a) { LuaConvert<A>::push(s, a); }, v);
    }
    static bool check(lua::State* s, int idx) { return (LuaConvert<Ts>::check(s, idx) || ...); }
    static std::variant<Ts...> get(lua::State* s, int idx) {
        std::optional<std::variant<Ts...>> out;
        // `||` stops at the first alternative that passes.
        static_cast<void>((getAs<Ts>(s, idx, out) || ...));
        return out ? std::move(*out) : std::variant<Ts...>{};
    }

private:
    template <class A>
    static bool getAs(lua::State* s, int idx, std::optional<std::variant<Ts...>>& out) {
        if (!LuaConvert<A>::check(s, idx)) return false;
        out.emplace(std::in_place_type<A>, LuaConvert<A>::get(s, idx));
        return true;
    }
};

} // namespace imlui
