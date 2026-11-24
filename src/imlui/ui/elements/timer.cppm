// Timer: a non-visual element that emits onTriggered every `interval` ms, as a typed element.
// It ticks from its creation, whether or not it's rendered (a Timer on a hidden page or an unselected
// Loader page keeps firing): Element::setTicking runs tick() at the start of every frame, with the
// properties synced first, so binding changes take effect even while it isn't rendered. It asks for a frame
// at its next due time, so it keeps ticking while the app otherwise sleeps. It stops when Lua collects its table;
// a Timer that a Repeater, Loader or Responsive drops is collected on the next frame when nothing else holds it
// (ui.__tick collects garbage after a drop).
export module imlui.ui.elements.timer;

import std;

import lua;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.frame_request;

export namespace imlui {

/// A timer. It ticks (Element::setTicking) whether or not it is rendered, so it fires on a hidden page too, until
/// its element table is collected (right away when a container drops it and nothing else references it).
/// Non-visual.
///
/// Props:   interval = 1000 (ms), running = true, repeat = true, triggeredOnStart = false
/// Methods: restart()
/// Signals: onTriggered(self)
/// Two-way: running (a one-shot writes false when it fires)
///
/// An interval that isn't a positive number (0, negative, NaN, infinity) is reported and the timer doesn't fire
/// until it gets a valid one. A one-shot whose `running` is a function binding can't write false back: it stays
/// stopped until the function has returned false and then true again (like ModalNavigationDrawer's `open`).
class Timer final : public Element {
public:
    static void declare(TypeBuilder<Timer>& type);

    Timer();

    void render(Frame& frame) override;
    void tick(double now) override;
    void destroyed() override;

    [[nodiscard]] double interval() const; ///< ms
    void setInterval(double ms);
    [[nodiscard]] bool running() const;
    /// Turning it on (from off) restarts the period.
    void setRunning(bool running);
    [[nodiscard]] bool repeat() const;
    void setRepeat(bool repeat);
    [[nodiscard]] bool triggeredOnStart() const;
    void setTriggeredOnStart(bool on);

    /// Starts it (again) now, also when it's already running: the period starts over, and triggeredOnStart
    /// fires. `running = false` then `true` before the next frame is no change (props sync once per frame);
    /// restart() is the way to start the period over right away.
    void restart();

private:
    /// Whether `running` is a function binding in the element table: it owns the value, a write-back can't
    /// change it.
    [[nodiscard]] bool runningIsFunction() const;

    double m_interval = 1000.0;
    bool m_running = true;
    bool m_repeat = true;
    bool m_triggeredOnStart = false;
    bool m_started = false; ///< m_last is valid: the timer has been running since then
    double m_last = 0.0;    ///< ImGui time of the start or the last trigger
    /// A one-shot fired while `running` is a function binding that returned true: stopped until it returns false.
    bool m_stoppedUnderBinding = false;
};

} // namespace imlui

module :private;

namespace imlui {

void Timer::declare(TypeBuilder<Timer>& type) {
    type.property("interval", &Timer::interval, &Timer::setInterval)
        .property("running", &Timer::running, &Timer::setRunning)
        .property("repeat", &Timer::repeat, &Timer::setRepeat)
        .property("triggeredOnStart", &Timer::triggeredOnStart, &Timer::setTriggeredOnStart)
        .method("restart", &Timer::restart)
        .signal("onTriggered")
        .nonVisual();
}

// Ticks for its whole life, also while stopped: a stopped timer still syncs every frame, so it sees
// `running` turn true (a binding) on a page that isn't rendered.
Timer::Timer() { setTicking(true); }

void Timer::destroyed() { setTicking(false); }

// Everything happens in tick(), which runs every frame before the tree renders.
void Timer::render(Frame&) {}

namespace {

[[nodiscard]] bool validInterval(double ms) { return std::isfinite(ms) && ms > 0.0; }

} // namespace

void Timer::tick(double now) {
    if (!m_running || m_stoppedUnderBinding || !validInterval(m_interval)) {
        m_started = false;
        return;
    }
    const double interval = m_interval / 1000.0;
    if (!m_started) {
        m_started = true;
        m_last = now;
        requestFrameAfter(interval);
        if (m_triggeredOnStart) emit("onTriggered");
        return;
    }
    if (now - m_last >= interval) {
        m_last = now;
        if (!m_repeat) {
            m_started = false;
            if (runningIsFunction()) {
                // The function returns its own value at every sync: remember the stop here instead.
                m_stoppedUnderBinding = true;
                emit("onRunningChanged", false);
            } else {
                m_running = false;
                notify("running"); // two-way: a one-shot writes running = false back
            }
        } else {
            requestFrameAfter(interval);
        }
        emit("onTriggered");
        return;
    }
    requestFrameAfter(interval - (now - m_last));
}

void Timer::restart() {
    m_started = false;
    m_stoppedUnderBinding = false;
    if (!m_running) {
        m_running = true;
        notify("running");
    }
    requestFrame(); // the next tick starts the period
}

bool Timer::runningIsFunction() const {
    lua::State* state = luaState();
    if (!state || !pushSelf()) return false;
    const bool function = lua::getfield(state, -1, "running") == lua::Type::Function;
    lua::pop(state, 2); // the value and the element table
    return function;
}

double Timer::interval() const { return m_interval; }
void Timer::setInterval(double ms) {
    // The sync calls the setter only on a changed value, but NaN never equals itself: report it once.
    const bool same = ms == m_interval || (std::isnan(ms) && std::isnan(m_interval));
    m_interval = ms;
    if (!same && !validInterval(ms))
        luaui::reportError(std::format("Timer.interval: {} ms is not a positive number, the timer doesn't fire", ms));
}
bool Timer::running() const { return m_running; }
void Timer::setRunning(bool running) {
    if (!running) m_stoppedUnderBinding = false;
    if (running && !m_running) m_started = false; // started again: the period starts over
    m_running = running;
}
bool Timer::repeat() const { return m_repeat; }
void Timer::setRepeat(bool repeat) { m_repeat = repeat; }
bool Timer::triggeredOnStart() const { return m_triggeredOnStart; }
void Timer::setTriggeredOnStart(bool on) { m_triggeredOnStart = on; }

} // namespace imlui
