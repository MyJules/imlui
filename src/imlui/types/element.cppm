// Element: base class of C++ element types registered with Engine::registerType<T>(name).
//
// One instance exists per element table (created by the Lua constructor, destroyed when Lua collects
// the table or the state closes). Before each render() the declared properties are synced from the
// element table into the instance through its setters; an element that isn't rendered is synced before a
// method called from Lua runs on it and when a parent asks for it (Frame::Child::as<T>()).
//
// An element can also ask for tick() at the start of every frame (setTicking(true)), whether or not it is
// rendered, e.g. a Timer on a hidden page. Its properties are synced before each tick().
module;

#include <algorithm>
#include <vector>

export module imlui.types.element;

import imlui.types.frame;
import imlui.types.script_object;

export namespace imlui {

class Element : public ScriptObject {
public:
    Element() = default;
    ~Element() override; ///< stops ticking

    /// Draws the element (Material widgets, the draw list) and its children (frame.renderChildren()).
    /// Called on every rendered frame the element is visible, after the property sync.
    virtual void render(Frame& frame) = 0;

    /// Called once, right after the first property sync (the first render, method call from Lua or parent
    /// access, whichever comes first), so before the first render().
    virtual void componentComplete();

    /// Called right before the instance is deleted: its element table was collected, or the Lua state
    /// is closing (hot reload, shutdown). The Lua link is already gone, so notify()/emit() do nothing.
    virtual void destroyed();

    /// While ticking is on (setTicking(true)): called at the start of every rendered frame, before the tree
    /// renders, whether or not the element is rendered, with its properties synced first. `now` is the
    /// ImGui time in seconds. Frames are rendered on demand: request the frame you need (requestFrameAfter).
    virtual void tick(double now);

    /// Whether tick() is called every frame.
    [[nodiscard]] bool isTicking() const;

    /// The ticking elements, in the order they started ticking (TypeRegistry runs them).
    [[nodiscard]] static std::vector<Element*> tickingElements();
    /// True while `element` is alive and ticking (a pointer from tickingElements() may have died since).
    [[nodiscard]] static bool isTicking(const Element* element);

protected:
    /// Turns tick() on or off. The destructor turns it off.
    void setTicking(bool on);
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

std::vector<Element*> ticking; // main thread only, like everything that touches Lua

} // namespace

Element::~Element() { setTicking(false); }

void Element::componentComplete() {}
void Element::destroyed() {}
void Element::tick(double) {}

bool Element::isTicking() const { return isTicking(this); }

std::vector<Element*> Element::tickingElements() { return ticking; }

bool Element::isTicking(const Element* element) {
    return std::find(ticking.begin(), ticking.end(), element) != ticking.end();
}

void Element::setTicking(bool on) {
    const auto it = std::find(ticking.begin(), ticking.end(), this);
    if (on && it == ticking.end()) ticking.push_back(this);
    if (!on && it != ticking.end()) ticking.erase(it);
}

} // namespace imlui
