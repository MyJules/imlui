// TypeBuilder<T>: declares what a C++ type exposes to Lua, from its static `declare()`:
//
//   static void declare(imlui::TypeBuilder<Gauge>& type) {
//       type.property("value", &Gauge::value, &Gauge::setValue)   // read/write; nil resets to the initial value
//           .property("max", &Gauge::max, &Gauge::setMax, 1.0)     // read/write; nil resets to 1.0
//           .property("peak", &Gauge::peak)                       // read-only
//           .signal("onPeak")
//           .method("reset", &Gauge::reset);
//   }
//
// Getters are anything std::invoke can call with a `const T&` (usually `V (T::*)() const`), setters
// anything it can call with `(T&, V)`. V must have a LuaConvert<V> specialization. Methods are member
// function pointers; their arguments and result convert through LuaConvert as well. Pick one of an
// overloaded set with imlui::overload<Args...>(&T::fn) (imlui.types.overload).
//
// A template, so it lives in the interface (CODE_STYLE).
export module imlui.types.type_builder;

import std;

import lua;
import imlui.lua.errors;
import imlui.types.lua_convert;
import imlui.types.script_object;
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

/// Raises "bad argument #2 to 'Gauge:setRange' (number expected, got string)", or with the path to what failed inside
/// the value: "bad argument #1 to 'lab:crunch' (CrunchOptions.method: 'trial'|'wheel' expected, got string "fast")".
template <class A>
[[noreturn]] void argError(lua::State* s, const std::string& where, int arg, int idx) {
    std::string msg = "bad argument #" + std::to_string(arg) + " to '" + where + "' (";
    msg += luaMismatchOf(LuaConvert<A>::name, luaMismatch<A>(s, idx));
    msg += ")";
    lua::error(s, msg);
}

template <class T, class Fn, class... A, std::size_t... I>
int invokeMethod(const Fn& fn, T& self, lua::State* s, int first, [[maybe_unused]] const std::string& where,
                 std::tuple<A...>*, std::index_sequence<I...>) {
    // The first argument that doesn't convert raises (`&&` stops there).
    static_cast<void>(((LuaConvert<A>::check(s, first + static_cast<int>(I)) ||
                        (argError<A>(s, where, static_cast<int>(I) + 1, first + static_cast<int>(I)), false)) &&
                       ...));
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

/// Runs `f()` and returns its result; a C++ exception is reported as a script error ("where: what") and
/// `fallback` returned instead: a property hook that throws doesn't stop the property sync of the other properties.
/// A Lua error (lua::Raised, e.g. out of memory while pushing a value) passes through.
template <class R, class F>
R guarded(const std::string& where, R fallback, const F& f) {
    try {
        return f();
    } catch (const lua::Raised&) {
        throw;
    } catch (const std::exception& e) {
        luaui::reportError(where + ": " + e.what());
    } catch (...) {
        luaui::reportError(where + ": unknown exception");
    }
    return fallback;
}

} // namespace imlui::detail

export namespace imlui {

/// T derives from ScriptObject. (Checked in the constructor: T is still incomplete where `declare` names
/// TypeBuilder<T> inside its own class.)
template <class T>
class TypeBuilder {
public:
    explicit TypeBuilder(TypeInfo& info) : m_info{info} {
        static_assert(std::derived_from<T, ScriptObject>, "TypeBuilder<T>: T must derive from imlui::ScriptObject");
    }

    /// A read/write property. The setter is called (by the property sync, or `obj.name = v` on a
    /// singleton) only when the converted value differs from the getter's. A nil value resets the property
    /// to its default: the getter's value right after the object was constructed (std::optional properties:
    /// nil is std::nullopt).
    template <class Getter, class Setter>
    TypeBuilder& property(std::string name, Getter getter, Setter setter) {
        using V = ValueOf<Getter>;
        TypeInfo::Property p = readWrite(std::move(name), getter, setter);
        if constexpr (!detail::isOptional<V> && std::copy_constructible<V>) {
            std::string where = m_info.name() + "." + p.name;
            p.captureDefault = [getter, where](const ScriptObject& o) -> std::shared_ptr<const void> {
                return detail::guarded(where, std::shared_ptr<const void>{}, [&]() -> std::shared_ptr<const void> {
                    return std::make_shared<const V>(std::invoke(getter, static_cast<const T&>(o)));
                });
            };
        }
        m_info.addProperty(std::move(p));
        return *this;
    }

    /// A read/write property whose nil value resets it to `defaultValue` (instead of the getter's value
    /// after construction): `.property("interval", &Timer::interval, &Timer::setInterval, 1000.0)`.
    template <class Getter, class Setter, class D>
    TypeBuilder& property(std::string name, Getter getter, Setter setter, D&& defaultValue) {
        using V = ValueOf<Getter>;
        static_assert(!detail::isOptional<V>, "nil is std::nullopt for an optional property; it takes no default");
        static_assert(std::copy_constructible<V>, "a property default must be copyable");
        static_assert(std::constructible_from<V, D&&>, "the default must convert to the property's type");
        TypeInfo::Property p = readWrite(std::move(name), getter, setter);
        auto shared = std::make_shared<const V>(std::forward<D>(defaultValue));
        p.captureDefault = [shared](const ScriptObject&) -> std::shared_ptr<const void> { return shared; };
        m_info.addProperty(std::move(p));
        return *this;
    }

    /// A read-only property: readable from Lua (singletons, notify write-back), never synced.
    template <class Getter>
    TypeBuilder& property(std::string name, Getter getter) {
        m_info.addProperty(readOnly<Getter>(std::move(name), getter));
        return *this;
    }

    /// Declares a signal (an `on*` handler name), emitted with ScriptObject::emit(name, args...).
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
        // A C++ exception becomes the Lua error "Gauge:reset: what".
        m_info.addMethod({std::move(name), [fn, where](lua::State* s, ScriptObject& o, int first) -> int {
                              return luaui::catchCxx(
                                  s,
                                  [&] {
                                      return detail::invokeMethod(
                                          fn, static_cast<T&>(o), s, first, where, static_cast<Args*>(nullptr),
                                          std::make_index_sequence<std::tuple_size_v<Args>>{});
                                  },
                                  where);
                          }});
        return *this;
    }

    /// Declares the members of base class `Base` (its `declare(TypeBuilder<Base>&)`) on this type, for a
    /// subclass that extends its base: `TextArea::declare` calls `type.extends<TextField>()`, then adds its
    /// own members.
    template <class Base>
        requires(std::derived_from<T, Base> && !std::same_as<T, Base>)
    TypeBuilder& extends() {
        TypeBuilder<Base> base{m_info};
        Base::declare(base);
        return *this;
    }

    /// The element takes no layout space (like Timer or Dialog).
    TypeBuilder& nonVisual() {
        m_info.setVisual(false);
        return *this;
    }

    /// An unset `width` fills the available width (the element's own default, like `width = -1`). A Row gives such a
    /// child a share of its free width instead of letting it run to the row's edge.
    TypeBuilder& fillsWidth() {
        m_info.setFillsWidth(true);
        return *this;
    }

    /// The same, decided per element by `predicate(const T&)` on its synced instance: `.fillsWidthWhen(&Text::wrap)`
    /// (wrapping text takes a share of a Row), `.fillsWidthWhen(&Spacer::fill)`.
    template <class Predicate>
        requires std::is_invocable_r_v<bool, const Predicate&, const T&>
    TypeBuilder& fillsWidthWhen(Predicate predicate) {
        m_info.setFillsWidthWhen([predicate](const ScriptObject& o) {
            return static_cast<bool>(std::invoke(predicate, static_cast<const T&>(o)));
        });
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

    template <class Getter, class Setter>
    TypeInfo::Property readWrite(std::string name, Getter getter, Setter setter) {
        using V = ValueOf<Getter>;
        static_assert(LuaReadable<V>, "a writable property needs a LuaConvert<V> with check() and get()");
        static_assert(std::is_invocable_v<const Setter&, T&, V&&>, "the setter must accept the getter's type");
        TypeInfo::Property p = readOnly<Getter>(std::move(name), getter);
        p.nullable = detail::isOptional<V>;
        p.mismatch = &luaMismatch<V>;
        p.takesFunction = takesLuaFunction<V>();
        std::string where = m_info.name() + "." + p.name;
        p.assign = [getter, setter, where](lua::State* s, int idx, ScriptObject& o) -> TypeInfo::Assign {
            if (!LuaConvert<V>::check(s, idx)) return TypeInfo::Assign::TypeMismatch;
            return detail::guarded(where, TypeInfo::Assign::Unchanged, [&] {
                return store(getter, setter, static_cast<T&>(o), LuaConvert<V>::get(s, idx));
            });
        };
        if constexpr (!detail::isOptional<V> && std::copy_constructible<V>) {
            p.reset = [getter, setter, where](const void* value, ScriptObject& o) -> TypeInfo::Assign {
                return detail::guarded(where, TypeInfo::Assign::Unchanged, [&] {
                    return store(getter, setter, static_cast<T&>(o), V(*static_cast<const V*>(value)));
                });
            };
        }
        return p;
    }

    /// Calls the setter with `v` unless the getter already returns it.
    template <class Getter, class Setter, class V>
    static TypeInfo::Assign store(const Getter& getter, const Setter& setter, T& self, V v) {
        if constexpr (std::equality_comparable<V>) {
            if (std::invoke(getter, std::as_const(self)) == v) return TypeInfo::Assign::Unchanged;
        }
        std::invoke(setter, self, std::move(v));
        return TypeInfo::Assign::Changed;
    }

    template <class Getter>
    TypeInfo::Property readOnly(std::string name, Getter getter) {
        using V = ValueOf<Getter>;
        static_assert(LuaPushable<V>, "a property type needs a LuaConvert<V> specialization");
        TypeInfo::Property p;
        p.name = std::move(name);
        p.typeName = LuaConvert<V>::name;
        p.push = [getter, where = m_info.name() + "." + p.name](lua::State* s, const ScriptObject& o) {
            const int top = lua::gettop(s);
            const bool pushed = detail::guarded(where, false, [&] {
                LuaConvert<V>::push(s, std::invoke(getter, static_cast<const T&>(o)));
                return true;
            });
            if (!pushed) { // a getter that threw reads as nil
                lua::settop(s, top);
                lua::pushnil(s);
            }
        };
        return p;
    }

    TypeInfo& m_info;
};

} // namespace imlui
