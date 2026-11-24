// LuaConvert<T>: how a C++ value crosses into and out of Lua.
//
// The type system (properties, method arguments and results, signal arguments) moves values with
// this trait. Each specialization provides
//
//   static constexpr std::string_view name;        // for error messages: "number", "string", ...
//   static void push(lua::State*, const T&);       // C++ -> Lua: pushes exactly one value
//   static bool check(lua::State*, int idx);       // can the value at idx become a T?
//   static T get(lua::State*, int idx);            // Lua -> C++ (only called after check())
//
// Push-only types (std::string_view, const char*) leave out check/get. Specialize it for your own
// types in your own module:
//
//   template <> struct imlui::LuaConvert<Vec2> {
//       static constexpr std::string_view name = "{x, y}";
//       static void push(lua::State* s, const Vec2& v) { ... }
//       static bool check(lua::State* s, int idx) { return lua::istable(s, idx); }
//       static Vec2 get(lua::State* s, int idx) { ... }
//   };
//
// Templates, so everything lives in the interface (CODE_STYLE: templates are the exception).
module;

#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

export module imlui.types.lua_convert;

import lua;
import imlui.color;
import imlui.theme.theme;
import imlui.lua.node;

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
    static T get(lua::State* s, int idx) { return static_cast<T>(lua::tonumber(s, idx)); }
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
        return lua::istable(s, idx);
    }
    static Color get(lua::State* s, int idx) {
        if (lua::type(s, idx) == lua::Type::String) return resolveColor(lua::tostring(s, idx)).value_or(Color{});
        return luaui::Node::colorFromTable(s, idx).value_or(Color{});
    }
};

/// nil <-> std::nullopt.
template <class T>
struct LuaConvert<std::optional<T>> {
    static constexpr std::string_view name = LuaConvert<T>::name;
    static void push(lua::State* s, const std::optional<T>& v) {
        if (v) LuaConvert<T>::push(s, *v);
        else lua::pushnil(s);
    }
    static bool check(lua::State* s, int idx) { return lua::isnil(s, idx) || LuaConvert<T>::check(s, idx); }
    static std::optional<T> get(lua::State* s, int idx) {
        if (lua::isnil(s, idx)) return std::nullopt;
        return LuaConvert<T>::get(s, idx);
    }
};

/// A Lua sequence {v1, v2, ...}. Reading checks every element.
template <class T>
struct LuaConvert<std::vector<T>> {
    static constexpr std::string_view name = "table";
    static void push(lua::State* s, const std::vector<T>& v) {
        lua::createtable(s, static_cast<int>(v.size()), 0);
        lua::Integer i = 0;
        for (const auto& e : v) {
            LuaConvert<T>::push(s, e);
            lua::rawseti(s, -2, ++i);
        }
    }
    static bool check(lua::State* s, int idx) {
        if (!lua::istable(s, idx)) return false;
        idx = lua::absindex(s, idx);
        const lua::Integer n = lua::rawlen(s, idx);
        for (lua::Integer i = 1; i <= n; ++i) {
            lua::rawgeti(s, idx, i);
            const bool ok = LuaConvert<T>::check(s, -1);
            lua::pop(s);
            if (!ok) return false;
        }
        return true;
    }
    static std::vector<T> get(lua::State* s, int idx) {
        idx = lua::absindex(s, idx);
        const lua::Integer n = lua::rawlen(s, idx);
        std::vector<T> out;
        out.reserve(static_cast<std::size_t>(n));
        for (lua::Integer i = 1; i <= n; ++i) {
            lua::rawgeti(s, idx, i);
            out.push_back(LuaConvert<T>::get(s, -1));
            lua::pop(s);
        }
        return out;
    }
};

} // namespace imlui
