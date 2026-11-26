// ModalNavigationDrawer: the Material 3 modal navigation drawer, as a typed element (non-visual).
module;

export module imlui.ui.elements.modal_navigation_drawer;

import lua;
import imlui.lua.node;
import imlui.widgets;
import imlui.skin;
import imlui.ui.navigation_scope;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.stack_props;

export namespace imlui {

/// A sheet over a scrim, above everything, with its children stacked in a scrolling region. Clicking the scrim
/// or pressing Escape closes it: `open` becomes false, then `onClosed` fires. Non-visual.
///
/// Props:   open = false, width = 360 (dp)
/// Raw:     spacing, align, cull
/// Signals: onClosed(self)
/// Two-way: open
///
/// `open` may be a plain value (`open = false`, opened with `drawer.open = true`: closing writes false back to
/// the element), a `bind(...)` (closing writes the bound state) or a function binding. A function owns the value,
/// so the drawer can't set it: closing hides the sheet and fires `onClosed`, and the drawer stays hidden until
/// the function has returned false (the handler updated the state it reads) and then true again.
class ModalNavigationDrawer final : public Element {
public:
    static void declare(TypeBuilder<ModalNavigationDrawer>& type);

    void render(Frame& frame) override;

    [[nodiscard]] bool open() const;
    void setOpen(bool open);
    [[nodiscard]] float width() const; ///< dp
    void setWidth(float width);

    /// True while the sheet is shown or sliding in: `open`, and not closed under a function binding that still
    /// returns true.
    [[nodiscard]] bool shown() const;

private:
    bool m_open = false;
    float m_width = 360.0f;
    /// Closed by the user while `open` is a function binding that returned true: hidden until it returns false.
    bool m_closedUnderBinding = false;
};

} // namespace imlui

module :private;

namespace imlui {

void ModalNavigationDrawer::declare(TypeBuilder<ModalNavigationDrawer>& type) {
    type.doc("A modal navigation drawer: a sheet sliding in from the left over a scrim, above the app content. Its "
             "children (`NavigationItem`s as drawer items, ...) stack in a scrolling region; `spacing` and `align` "
             "work as on a `Column`. Clicking the scrim or pressing Escape closes it.\n"
             "Non-visual: put it anywhere, e.g. in the `ApplicationWindow`.")
        .property("open", &ModalNavigationDrawer::open, &ModalNavigationDrawer::setOpen)
        .doc("Shown while true. **Two-way**: closing writes `false` back to a plain value or a `bind(...)`. A "
             "function binding owns the value: closing hides the sheet until the function has returned `false` "
             "and then `true` again.")
        .property("width", &ModalNavigationDrawer::width, &ModalNavigationDrawer::setWidth)
        .doc("The sheet's width (dp), at most 80% of the window.")
        .signal<>("onClosed")
        .doc("Called when the scrim or Escape closes the drawer, after `open` was updated.")
        .nonVisual();
    rawStackProps(type);
}

void ModalNavigationDrawer::render(Frame& frame) {
    bool open = shown();
    if (skin().beginModalDrawer("##drawer", &open, m_width)) {
        const NavigationScope scope{{}};
        frame.renderStack();
        skin().endModalDrawer();
    }
    if (open || !shown()) return; // the widget only ever closes the drawer

    // A function binding owns `open` and returns its own value at every sync, so the close is remembered
    // locally instead: `open` keeps the binding's value, and setOpen(false) ends the hiding.
    const luaui::Node& node = frame.node();
    const bool function = node.pushRaw("open") == lua::Type::Function;
    lua::pop(node.state());
    if (function) {
        m_closedUnderBinding = true;
    } else {
        m_open = false;
        notify("open"); // a plain value is replaced, a bind() writes through
    }
    emit("onClosed");
}

bool ModalNavigationDrawer::open() const { return m_open; }
void ModalNavigationDrawer::setOpen(bool open) {
    if (!open) m_closedUnderBinding = false;
    m_open = open;
}
float ModalNavigationDrawer::width() const { return m_width; }
void ModalNavigationDrawer::setWidth(float width) { m_width = width; }
bool ModalNavigationDrawer::shown() const { return m_open && !m_closedUnderBinding; }

} // namespace imlui
