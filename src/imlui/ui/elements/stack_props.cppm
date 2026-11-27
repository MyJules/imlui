// The props of the stacking containers that Renderer::renderStack (Frame::renderStack, Frame::stackSize) reads from
// the element table of every element it stacks the children of: `spacing`, `align` and `cull`. A type that stacks its
// children and doesn't declare `spacing` and `align` itself (Window, Dialog, Section, Tab, ...) declares them with
// stackProps(): `spacing` and `cull` raw (TypeBuilder::rawProperty, for the Lua definitions), `align` as a typed
// property held by StackAlign, so the sync reports a name that isn't an Align once (when the field changes, like any
// property's), as it does for Column's. The templates live in the interface (CODE_STYLE); StackAlign is their
// part of the element.
export module imlui.ui.elements.stack_props;

import std;

import imlui.types.type_builder;
import imlui.ui.enum_names;
import imlui.ui.props;

export namespace imlui {

/// Declares raw `cull` (Renderer::Culling reads it): whether the children out of view are skipped.
template <class T>
TypeBuilder<T>& rawCullProp(TypeBuilder<T>& type) {
    return type.template rawProperty<bool>("cull").doc(
        "`false` renders every child, also those out of view. By default (`true`) a child out of view whose size is "
        "known is skipped and only its size is laid out (its bindings don't run); positions and the scroll range are "
        "the same either way. `ui.setCulling(false)` turns it off everywhere.");
}

/// The `align` of a type declared with stackProps(): a base of the element, holding the value its property syncs.
/// Renderer::renderStack places the children by the table's field (Renderer::stackAlign), which reads the same.
class StackAlign {
public:
    [[nodiscard]] Align align() const;
    void setAlign(Align align);

private:
    Align m_align = Align::Start;
};

/// Declares raw `spacing` and `cull` and the property `align` (StackAlign), for a type that stacks its children with
/// Frame::renderStack and doesn't declare `spacing` and `align` itself.
template <class T>
    requires std::derived_from<T, StackAlign>
TypeBuilder<T>& stackProps(TypeBuilder<T>& type) {
    type.template rawProperty<std::optional<double>>("spacing")
        .doc("The gap between visible children (dp). Unset: ImGui's item spacing.")
        .property("align", &StackAlign::align, &StackAlign::setAlign)
        .doc("Each child's horizontal position by its width: `\"start\"` (default), `\"center\"` or `\"end\"`. A "
             "child that fills isn't moved.");
    return rawCullProp(type);
}

} // namespace imlui

module :private;

namespace imlui {

Align StackAlign::align() const { return m_align; }
void StackAlign::setAlign(Align align) { m_align = align; }

} // namespace imlui
