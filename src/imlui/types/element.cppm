// Element: base class of C++ element types registered with Engine::registerType<T>(name).
//
// One instance exists per element table (created by the Lua constructor, destroyed when Lua collects
// the table or the state closes). Before each render() the declared properties are synced from the
// element table into the instance through its setters; an element that isn't rendered is synced before a
// method called from Lua runs on it and when a parent asks for it (Frame::Child::as<T>()).
export module imlui.types.element;

import imlui.types.frame;
import imlui.types.script_object;

export namespace imlui {

class Element : public ScriptObject {
public:
    /// Draws the element (Material widgets, the draw list) and its children (frame.renderChildren()).
    /// Called on every rendered frame the element is visible, after the property sync.
    virtual void render(Frame& frame) = 0;

    /// Called once, right after the first property sync (the first render, method call from Lua or parent
    /// access, whichever comes first), so before the first render().
    virtual void componentComplete();

    /// Called right before the instance is deleted: its element table was collected, or the Lua state
    /// is closing (hot reload, shutdown). The Lua link is already gone, so notify()/emit() do nothing.
    virtual void destroyed();
};

} // namespace imlui

module :private;

namespace imlui {

void Element::componentComplete() {}
void Element::destroyed() {}

} // namespace imlui
