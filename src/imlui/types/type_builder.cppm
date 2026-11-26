// TypeBuilder<T>: declares what a C++ type exposes to Lua, from its static `declare()`:
//
//   static void declare(imlui::TypeBuilder<Gauge>& type) {
//       type.doc("A gauge needle.")                               // first: the type's doc
//           .property("value", &Gauge::value, &Gauge::setValue)   // read/write; nil resets to the initial value
//           .doc("The needle's position.")                        // the doc of the member declared right before
//           .property("max", &Gauge::max, &Gauge::setMax, 1.0)     // read/write; nil resets to 1.0
//           .property("peak", &Gauge::peak)                       // read-only
//           .signal("onPeak")                                    // or typed: .signal<double>("onPeak", {"value"})
//           .method("reset", &Gauge::reset)
//           .method("setRange", &Gauge::setRange, {"min", "max"}) // parameter names for the Lua definitions
//           .rawProperty<double>("size");                       // read from the table by render(): definitions only
//   }
//
// The docs are for the Lua definitions (the editor's hover text): Markdown, several lines allowed.
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
import imlui.types.lua_types;
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

/// The parameters of a method or signal with argument types `A...`, named `names` (`argN` where left out).
template <class... A>
std::vector<TypeInfo::Param> params(const std::string& where, const std::vector<std::string>& names) {
    if (!names.empty() && names.size() != sizeof...(A))
        throw std::invalid_argument{where + ": " + std::to_string(names.size()) + " parameter names for " +
                                    std::to_string(sizeof...(A)) + " parameters"};
    std::vector<TypeInfo::Param> out;
    [[maybe_unused]] std::size_t i = 0;
    ((out.push_back({names.empty() ? "arg" + std::to_string(i + 1) : names[i], luaTypeDescriber<A>()}), ++i), ...);
    return out;
}

template <class... A>
std::vector<TypeInfo::Param> tupleParams(const std::string& where, const std::vector<std::string>& names,
                                         std::tuple<A...>*) {
    return params<A...>(where, names);
}

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

    /// Documents (for the Lua definitions: Markdown, several lines allowed) the property, raw property, signal or
    /// method declared by the call right before, or the type itself when it is the first call in declare():
    /// `type.doc("A gauge.").property("value", ...).doc("The needle's position.")`. Anything else throws
    /// std::invalid_argument at registration: a second doc() for the same thing, or one after a call that declares
    /// no member (extends, defaultProperty, nonVisual, fillsWidth, ...). "" leaves it undocumented.
    TypeBuilder& doc(std::string text) {
        switch (m_docTarget) {
        case DocTarget::Type:
            // A base's declare() run by extends<Base>() documents the base, not this type.
            if (!m_inherited) m_info.setDoc(std::move(text));
            break;
        case DocTarget::Member:
            m_info.setMemberDoc(m_lastMember, std::move(text));
            break;
        case DocTarget::None:
            throw std::invalid_argument{m_info.name() +
                                        ": doc() documents the member declared right before it (or the type, as the "
                                        "first call in declare())"};
        }
        m_docTarget = DocTarget::None;
        return *this;
    }

    /// Documents member `name`, declared earlier, replacing its doc: for a member a subclass inherits through
    /// extends<Base>() (`type.extends<TextField>().docMember("text", "...")`). Throws std::invalid_argument when there
    /// is no such property, raw property, signal or method.
    TypeBuilder& docMember(std::string_view name, std::string text) {
        if (!m_info.setMemberDoc(name, std::move(text)))
            throw std::invalid_argument{m_info.name() + ": docMember(\"" + std::string{name} + "\"): no such member"};
        m_docTarget = DocTarget::None;
        return *this;
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
        declared(p.name);
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
        declared(p.name);
        m_info.addProperty(std::move(p));
        return *this;
    }

    /// A read-only property: readable from Lua (singletons, notify write-back), never synced.
    template <class Getter>
    TypeBuilder& property(std::string name, Getter getter) {
        declared(name);
        m_info.addProperty(readOnly<Getter>(std::move(name), getter));
        return *this;
    }

    /// Declares a raw property: a prop the element reads straight from its table while rendering (Frame::size,
    /// Frame::node(), Frame::withElementProp, Frame::attachMenu) or that the Lua constructor reads, instead of through
    /// a C++ property. It is for the Lua definitions only (the editor completes, checks and documents it, in
    /// `<Name>Props` and `<Name>`): never synced, no setter or getter, no change signal. `V`'s LuaConvert gives its
    /// type (luaTypeOf<V>): `.rawProperty<double>("size")`, `.rawProperty<std::optional<double>>("spacing")`. `read`
    /// says whether the element reads it through luaui::Node (a value, a binding or a bind(): RawRead::Bindable) or
    /// takes the field itself (RawRead::ValueOnly).
    template <class V>
    TypeBuilder& rawProperty(std::string name, RawRead read = RawRead::Bindable) {
        return raw(
            {.name = std::move(name), .doc = {}, .luaType = luaTypeDescriber<V>(), .luaTypeText = {}, .read = read});
    }

    /// The same with an explicit LuaLS type expression, for values no C++ type describes (an element, a table read
    /// lazily): `.rawProperty("menu", "Element|false", RawRead::ValueOnly)`, `.rawProperty("rows", "any[][]")`.
    TypeBuilder& rawProperty(std::string name, std::string luaType, RawRead read = RawRead::Bindable) {
        return raw(
            {.name = std::move(name), .doc = {}, .luaType = nullptr, .luaTypeText = std::move(luaType), .read = read});
    }

    /// Declares a signal (an `on*` handler name), emitted with ScriptObject::emit(name, args...). Its arguments are
    /// unknown to the Lua definitions (`fun(self, ...: any)`).
    TypeBuilder& signal(std::string name) {
        declared(name);
        m_info.addSignal(std::move(name));
        return *this;
    }

    /// A signal emitted with arguments of types `Args...` (after `self`), named `argNames` (or arg1, arg2, ...) in the
    /// Lua definitions: `.signal<double>("onPeak", {"value"})`. The types only document: emit() isn't checked against
    /// them. `signal<>("onReset")` declares one without arguments.
    template <class... Args>
    TypeBuilder& signal(std::string name, std::vector<std::string> argNames = {}) {
        auto args = detail::params<std::remove_cvref_t<Args>...>(m_info.name() + "." + name, argNames);
        declared(name);
        m_info.addSignal(std::move(name), std::move(args));
        return *this;
    }

    /// A method callable from Lua as `element:name(args...)` (or `singleton:name(...)`). `paramNames` names its
    /// parameters in the Lua definitions (arg1, arg2, ... where left out; else one name per parameter).
    template <class Fn>
        requires std::is_member_function_pointer_v<Fn>
    TypeBuilder& method(std::string name, Fn fn, std::vector<std::string> paramNames = {}) {
        using Args = typename detail::MethodTraits<Fn>::Args;
        using R = std::remove_cvref_t<typename detail::MethodTraits<Fn>::Result>;
        std::string where = m_info.name() + ":" + name;
        TypeInfo::Method m;
        m.name = std::move(name);
        m.params = detail::tupleParams(where, paramNames, static_cast<Args*>(nullptr));
        if constexpr (!std::is_void_v<R>) m.result = luaTypeDescriber<R>();
        // A C++ exception becomes the Lua error "Gauge:reset: what".
        m.call = [fn, where](lua::State* s, ScriptObject& o, int first) -> int {
            return luaui::catchCxx(
                s,
                [&] {
                    return detail::invokeMethod(fn, static_cast<T&>(o), s, first, where, static_cast<Args*>(nullptr),
                                                std::make_index_sequence<std::tuple_size_v<Args>>{});
                },
                where);
        };
        declared(m.name);
        m_info.addMethod(std::move(m));
        return *this;
    }

    /// Declares the members of base class `Base` (its `declare(TypeBuilder<Base>&)`) on this type, for a
    /// subclass that extends its base: `TextArea::declare` calls `type.extends<TextField>()`, then adds its
    /// own members. They keep the base's docs (docMember() replaces one); the base's type doc isn't this type's
    /// (document the subclass with doc() before extends()).
    template <class Base>
        requires(std::derived_from<T, Base> && !std::same_as<T, Base>)
    TypeBuilder& extends() {
        TypeBuilder<Base> base{m_info, typename TypeBuilder<Base>::Inherited{}};
        Base::declare(base);
        m_docTarget = DocTarget::None;
        return *this;
    }

    /// The element takes no layout space (like Timer or Dialog).
    TypeBuilder& nonVisual() {
        m_info.setVisual(false);
        m_docTarget = DocTarget::None;
        return *this;
    }

    /// An unset `width` fills the available width (the element's own default, like `width = -1`). A Row gives such a
    /// child a share of its free width instead of letting it run to the row's edge.
    TypeBuilder& fillsWidth() {
        m_info.setFillsWidth(true);
        m_docTarget = DocTarget::None;
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
        m_docTarget = DocTarget::None;
        return *this;
    }

    /// The property set by a positional first value that isn't a child: `Gauge { 0.5 }`.
    TypeBuilder& defaultProperty(std::string name) {
        m_info.setDefaultProperty(std::move(name));
        m_docTarget = DocTarget::None;
        return *this;
    }

    [[nodiscard]] TypeInfo& info() { return m_info; }

private:
    template <class>
    friend class TypeBuilder; // extends<Base>() makes an inherited TypeBuilder<Base>

    /// What doc() documents: the type (nothing declared yet), the member declared last, or nothing (it throws).
    enum class DocTarget { Type, Member, None };

    struct Inherited {};
    /// The builder extends<>() runs a base's declare() with: its type doc isn't the subclass's.
    TypeBuilder(TypeInfo& info, Inherited) : TypeBuilder{info} { m_inherited = true; }

    /// Member `name` was declared: a doc() right after documents it.
    void declared(const std::string& name) {
        m_lastMember = name;
        m_docTarget = DocTarget::Member;
    }

    TypeBuilder& raw(TypeInfo::RawProperty p) {
        if (p.luaTypeText.empty() && !p.luaType)
            throw std::invalid_argument{m_info.name() + ".rawProperty(\"" + p.name + "\"): no Lua type"};
        declared(p.name);
        m_info.addRawProperty(std::move(p));
        return *this;
    }

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
        p.luaType = luaTypeDescriber<V>();
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
    DocTarget m_docTarget = DocTarget::Type;
    std::string m_lastMember;
    bool m_inherited = false;
};

} // namespace imlui
