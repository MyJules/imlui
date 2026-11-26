// The raw props of the stacking containers, declared for the Lua definitions (TypeBuilder::rawProperty): the
// `spacing`, `align` and `cull` that Renderer::renderStack (Frame::renderStack, Frame::stackSize) reads from the
// element table of every element it stacks the children of, whether or not the type declares them as properties.
// Templates, so they live in the interface (CODE_STYLE).
export module imlui.ui.elements.stack_props;

import std;

import imlui.types.type_builder;

export namespace imlui {

/// Declares raw `cull` (Renderer::Culling reads it): whether the children out of view are skipped.
template <class T>
TypeBuilder<T>& rawCullProp(TypeBuilder<T>& type) {
    return type.template rawProperty<bool>("cull").doc(
        "`false` renders every child, also those out of view. By default (`true`) a child out of view whose size is "
        "known is skipped and only its size is laid out (its bindings don't run); positions and the scroll range are "
        "the same either way. `ui.setCulling(false)` turns it off everywhere.");
}

/// Declares raw `spacing`, `align` and `cull`, for a type that stacks its children with Frame::renderStack and
/// doesn't declare `spacing` and `align` as properties.
template <class T>
TypeBuilder<T>& rawStackProps(TypeBuilder<T>& type) {
    type.template rawProperty<std::optional<double>>("spacing")
        .doc("The gap between visible children (dp). Unset: ImGui's item spacing.")
        .template rawProperty<std::string>("align")
        .doc("Each child's horizontal position by its width: `\"start\"` (default), `\"center\"` or `\"end\"`. A "
             "child that fills isn't moved.");
    return rawCullProp(type);
}

} // namespace imlui
