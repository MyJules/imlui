// InlineContainer: Repeater, If and Loader when one is the root element, as a typed element (TypeBuilder).
//
// These containers are implemented in Lua (lua/imlui.lua): they are `__inline` and give their children
// through `__children(self)` (the delegates, the branch that holds, the loaded item). A parent splices
// their children into its own, so they are never rendered themselves, except as the root, where this
// renders their children top to bottom.
module;

export module imlui.ui.elements.inline_container;

import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Registered as "Repeater", "If" and "Loader". Their props (model, delegate, condition, otherwise, item)
/// are read by their Lua `__children` functions.
class InlineContainer final : public Element {
public:
    static void declare(TypeBuilder<InlineContainer>& type);

    void render(Frame& frame) override;
};

} // namespace imlui

module :private;

namespace imlui {

void InlineContainer::declare(TypeBuilder<InlineContainer>&) {}

void InlineContainer::render(Frame& frame) { frame.renderChildren(); }

} // namespace imlui
