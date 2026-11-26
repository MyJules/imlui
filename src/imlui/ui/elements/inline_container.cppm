// InlineContainer: Repeater, If, Loader and Responsive when one is the root element, as a typed element.
//
// These containers are implemented in Lua (src/resources/imlui.lua): they are `__inline` and give their children
// through `__children(self)` (the delegates, the branch that holds, the loaded item). A parent splices
// their children into its own, so they are never rendered themselves, except as the root, where this
// renders their children top to bottom. Their constructors register `id` like every element (ui.registerId), and
// when one drops children (a rebuilt delegate, a replaced item) the next ui.__tick collects garbage, so a dropped
// ticking element (Timer) that nothing else references stops right away.
module;

export module imlui.ui.elements.inline_container;

import std;

import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The C++ side of the inline containers Repeater, If, Loader and Responsive. They are implemented in Lua
/// (`__inline`, `__children`): parents splice their children in, so this only renders as the root element.
///
/// Raw:     model, delegate, condition, otherwise, item and the size-class props (read on the Lua side)
class InlineContainer final : public Element {
public:
    static void declare(TypeBuilder<InlineContainer>& type);

    void render(Frame& frame) override;
};

} // namespace imlui

module :private;

namespace imlui {

void InlineContainer::declare(TypeBuilder<InlineContainer>& type) {
    // One class for four Lua-side containers: the doc of the name being registered.
    const std::string& name = type.info().name();
    std::string doc;
    if (name == "Repeater")
        doc = "Builds one child per item of `model` with `delegate(item, index, ids)` (`ids`: the delegate's own id "
              "scope); an optional `key(item, index)` keeps delegates by key instead of by index.";
    else if (name == "If")
        doc = "Shows its children while `condition` is truthy, else the `otherwise` element (if any).";
    else if (name == "Loader")
        doc = "Shows `item`: an element, or a function returning one.";
    else if (name == "Responsive")
        doc = "Shows the element given for the window's size class (`compact`, `medium`, `expanded`, `large`, "
              "`extraLarge`): an element, a function returning one, or `false`.";
    type.doc(doc + "\nAn inline container: its parent lays its children out as its own (a `Repeater` in a `Row` "
                   "flows horizontally). `visible = false` hides all of them.");
}

void InlineContainer::render(Frame& frame) { frame.renderChildren(); }

} // namespace imlui
