// TypeBuilder<T>: declares what a C++ type exposes to Lua, from its static `declare()`:
//
//   static void declare(imlui::TypeBuilder<Gauge>& type) {
//       type.property("value", &Gauge::value, &Gauge::setValue)   // read/write
//           .property("peak", &Gauge::peak)                       // read-only
//           .signal("onPeak")
//           .method("reset", &Gauge::reset);
//   }
//
// Getters are anything std::invoke can call with a `const T&` (usually `V (T::*)() const`), setters
// anything it can call with `(T&, V)`. V must have a LuaConvert<V> specialization. Methods are member
// function pointers; their arguments and result convert through LuaConvert as well.
//
// A template, so it lives in the interface (CODE_STYLE).
module;

#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

export module imlui.types.type_builder;

import lua;
import imlui.types.lua_convert;
import imlui.types.object;
import imlui.types.type_info;

namespace imlui::detail {

template <class F>
struct MethodTraits;

template <class C, class R, class... A>
struct MethodTraits<R (C::*)(A...)> {
    using Result = R;
    using Args = std::tuple<std::decay_t<A>...>;
};
template <class C, class R, class... A>
struct MethodTraits<R (C::*)(A...) const> : MethodTraits<R (C::*)(A...)> {};
template <class C, class R, class... A>
struct MethodTraits<R (C::*)(A...) noexcept> : MethodTraits<R (C::*)(A...)> {};
template <class C, class R, class... A>
struct MethodTraits<R (C::*)(A...) const noexcept> : MethodTraits<R (C::*)(A...)> {};

template <class V>
inline constexpr bool isOptional = false;
template <class V>
inline constexpr bool isOptional<std::optional<V>> = true;

/// "bad argument #2 to 'Gauge:setRange' (number expected, got string)"
inline void pushArgError(lua::State* s, const std::string& where, int arg, std::string_view expected, int idx) {
    std::string msg = "bad argument #" + std::to_string(arg) + " to '" + where + "' (";
    msg += expected;
    msg += " expected, got ";
    msg += lua::typeName(s, idx);
    msg += ")";
    lua::pushstring(s, msg);
}

template <class T, class Fn, class... A, std::size_t... I>
int invokeMethod(const Fn& fn, T& self, lua::State* s, int first, const std::string& where, std::tuple<A...>*,
                 std::index_sequence<I...>) {
    const bool ok[] = {LuaConvert<A>::check(s, first + static_cast<int>(I))..., true};
    for (std::size_t i = 0; i < sizeof...(A); ++i) {
        if (!ok[i]) {
            constexpr std::string_view names[] = {LuaConvert<A>::name..., {}};
            const int idx = first + static_cast<int>(i);
            pushArgError(s, where, static_cast<int>(i) + 1, names[i], idx);
            return -1;
        }
    }
    std::tuple<A...> args{LuaConvert<A>::get(s, first + static_cast<int>(I))...};
    using R = typename MethodTraits<Fn>::Result;
    if constexpr (std::is_void_v<R>) {
        std::invoke(fn, self, std::move(std::get<I>(args))...);
        return 0;
    } else {
        LuaConvert<std::remove_cvref_t<R>>::push(s, std::invoke(fn, self, std::move(std::get<I>(args))...));
        return 1;
    }
}

} // namespace imlui::detail

export namespace imlui {

/// T derives from Object. (Checked in the constructor: T is still incomplete where `declare` names
/// TypeBuilder<T> inside its own class.)
template <class T>
class TypeBuilder {
public:
    explicit TypeBuilder(TypeInfo& info) : m_info{info} {
        static_assert(std::derived_from<T, Object>, "TypeBuilder<T>: T must derive from imlui::Object");
    }

    /// A read/write property. The setter is called (by the per-frame sync, or `obj.name = v` on a
    /// singleton) only when the converted value differs from the getter's.
    template <class Getter, class Setter>
    TypeBuilder& property(std::string name, Getter getter, Setter setter) {
        using V = ValueOf<Getter>;
        static_assert(LuaReadable<V>, "a writable property needs a LuaConvert<V> with check() and get()");
        static_assert(std::is_invocable_v<const Setter&, T&, V&&>, "the setter must accept the getter's type");
        TypeInfo::Property p = readOnly<Getter>(std::move(name), getter);
        p.nullable = detail::isOptional<V>;
        p.assign = [getter, setter](lua::State* s, int idx, Object& o) -> TypeInfo::Assign {
            if (!LuaConvert<V>::check(s, idx)) return TypeInfo::Assign::TypeMismatch;
            V v = LuaConvert<V>::get(s, idx);
            T& self = static_cast<T&>(o);
            if constexpr (std::equality_comparable<V>) {
                if (std::invoke(getter, std::as_const(self)) == v) return TypeInfo::Assign::Unchanged;
            }
            std::invoke(setter, self, std::move(v));
            return TypeInfo::Assign::Changed;
        };
        m_info.addProperty(std::move(p));
        return *this;
    }

    /// A read-only property: readable from Lua (singletons, notify write-back), never synced.
    template <class Getter>
    TypeBuilder& property(std::string name, Getter getter) {
        m_info.addProperty(readOnly<Getter>(std::move(name), getter));
        return *this;
    }

    /// Declares a signal (an `on*` handler name), emitted with Object::emit(name, args...).
    TypeBuilder& signal(std::string name) {
        m_info.addSignal(std::move(name));
        return *this;
    }

    /// A method callable from Lua as `element:name(args...)` (or `singleton:name(...)`).
    template <class Fn>
        requires std::is_member_function_pointer_v<Fn>
    TypeBuilder& method(std::string name, Fn fn) {
        using Args = typename detail::MethodTraits<Fn>::Args;
        std::string where = m_info.name() + ":" + name;
        m_info.addMethod({std::move(name), [fn, where](lua::State* s, Object& o, int first) -> int {
                              try {
                                  return detail::invokeMethod(
                                      fn, static_cast<T&>(o), s, first, where, static_cast<Args*>(nullptr),
                                      std::make_index_sequence<std::tuple_size_v<Args>>{});
                              } catch (const std::exception& e) {
                                  lua::pushstring(s, where + ": " + e.what());
                                  return -1;
                              }
                          }});
        return *this;
    }

    /// The element takes no layout space (like Timer or Dialog).
    TypeBuilder& nonVisual() {
        m_info.setVisual(false);
        return *this;
    }

    /// The property set by a positional first value that isn't a child: `Gauge { 0.5 }`.
    TypeBuilder& defaultProperty(std::string name) {
        m_info.setDefaultProperty(std::move(name));
        return *this;
    }

    [[nodiscard]] TypeInfo& info() { return m_info; }

private:
    template <class Getter>
    using ValueOf = std::remove_cvref_t<std::invoke_result_t<const Getter&, const T&>>;

    template <class Getter>
    TypeInfo::Property readOnly(std::string name, Getter getter) {
        using V = ValueOf<Getter>;
        static_assert(LuaPushable<V>, "a property type needs a LuaConvert<V> specialization");
        TypeInfo::Property p;
        p.name = std::move(name);
        p.typeName = LuaConvert<V>::name;
        p.push = [getter](lua::State* s, const Object& o) {
            LuaConvert<V>::push(s, std::invoke(getter, static_cast<const T&>(o)));
        };
        return p;
    }

    TypeInfo& m_info;
};

} // namespace imlui
