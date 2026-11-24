// overload<Args...> / constOverload<Args...>: pick one member function of an overloaded set by its
// parameter types, without a static_cast to the full member pointer type:
//
//   type.method("scale", imlui::overload<double>(&Shape::scale))
//       .method("scaleXY", imlui::overload<double, double>(&Shape::scale))
//       .property("size", imlui::constOverload<>(&Shape::size), imlui::overload<double>(&Shape::setSize));
//
// overload picks a non-const member, constOverload a const one. The return type and the class are
// deduced. constexpr function objects, so they live in the interface (CODE_STYLE).
export module imlui.types.overload;

export namespace imlui {

/// The function object behind imlui::overload<Args...>: accepts a non-const member `R (C::*)(Args...)`.
template <class... Args>
struct OverloadSelector {
    template <class C, class R>
    constexpr auto operator()(R (C::*fn)(Args...)) const noexcept {
        return fn;
    }
};

/// The function object behind imlui::constOverload<Args...>: accepts a const member `R (C::*)(Args...) const`.
template <class... Args>
struct ConstOverloadSelector {
    template <class C, class R>
    constexpr auto operator()(R (C::*fn)(Args...) const) const noexcept {
        return fn;
    }
};

/// `overload<int>(&T::set)`: the non-const overload of T::set taking exactly (int).
template <class... Args>
inline constexpr OverloadSelector<Args...> overload{};

/// `constOverload<>(&T::size)`: the const overload of T::size taking no arguments.
template <class... Args>
inline constexpr ConstOverloadSelector<Args...> constOverload{};

} // namespace imlui
