// Timer: a non-visual element that emits onTriggered every `interval` ms, as a typed element.
// It ticks from its creation, whether or not it's rendered (a Timer on a hidden page or an unselected
// Loader page keeps firing): Element::setTicking runs tick() at the start of every frame, with the
// properties synced first, so binding changes take effect even while it isn't rendered. It asks for a frame
// at its next due time, so it keeps ticking while the app otherwise sleeps.
module;

export module imlui.ui.elements.timer;

import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.frame_request;

export namespace imlui {

/// A timer. It ticks (Element::setTicking) whether or not it is rendered, so it fires on a hidden page too.
/// Non-visual.
///
/// Props:   interval = 1000 (ms), running = true, repeat = true, triggeredOnStart = false
/// Signals: onTriggered(self)
/// Two-way: running (a one-shot writes false when it fires)
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
    void setRunning(bool running);
    [[nodiscard]] bool repeat() const;
    void setRepeat(bool repeat);
    [[nodiscard]] bool triggeredOnStart() const;
    void setTriggeredOnStart(bool on);

private:
    double m_interval = 1000.0;
    bool m_running = true;
    bool m_repeat = true;
    bool m_triggeredOnStart = false;
    bool m_started = false; ///< m_last is valid: the timer has been running since then
    double m_last = 0.0;    ///< ImGui time of the start or the last trigger
};

} // namespace imlui

module :private;

namespace imlui {

void Timer::declare(TypeBuilder<Timer>& type) {
    type.property("interval", &Timer::interval, &Timer::setInterval)
        .property("running", &Timer::running, &Timer::setRunning)
        .property("repeat", &Timer::repeat, &Timer::setRepeat)
        .property("triggeredOnStart", &Timer::triggeredOnStart, &Timer::setTriggeredOnStart)
        .signal("onTriggered")
        .nonVisual();
}

// Ticks for its whole life, also while stopped: a stopped timer still syncs every frame, so it sees
// `running` turn true (a binding) on a page that isn't rendered.
Timer::Timer() { setTicking(true); }

void Timer::destroyed() { setTicking(false); }

// Everything happens in tick(), which runs every frame before the tree renders.
void Timer::render(Frame&) {}

void Timer::tick(double now) {
    if (!m_running) {
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
            m_running = false;
            notify("running"); // two-way: a one-shot writes running = false back
        } else {
            requestFrameAfter(interval);
        }
        emit("onTriggered");
        return;
    }
    requestFrameAfter(interval - (now - m_last));
}

double Timer::interval() const { return m_interval; }
void Timer::setInterval(double ms) { m_interval = ms; }
bool Timer::running() const { return m_running; }
void Timer::setRunning(bool running) { m_running = running; }
bool Timer::repeat() const { return m_repeat; }
void Timer::setRepeat(bool repeat) { m_repeat = repeat; }
bool Timer::triggeredOnStart() const { return m_triggeredOnStart; }
void Timer::setTriggeredOnStart(bool on) { m_triggeredOnStart = on; }

} // namespace imlui
