// Timer: a non-visual element that emits onTriggered every `interval` ms while it's rendered,
// as a typed element (TypeBuilder). It asks for a frame at its next due time, so it keeps ticking
// while the app otherwise sleeps.
module;

export module imlui.ui.elements.timer;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Timer { interval = 1000, running = true, repeat = true, triggeredOnStart = false, onTriggered = fn }
class Timer final : public Element {
public:
    static void declare(TypeBuilder<Timer>& type);

    void render(Frame& frame) override;

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

void Timer::render(Frame& frame) {
    if (!m_running) {
        m_started = false;
        return;
    }
    const double now = ImGui::GetTime();
    const double interval = m_interval / 1000.0;
    if (!m_started) {
        m_started = true;
        m_last = now;
        frame.requestFrameAfter(interval);
        if (m_triggeredOnStart) emit("onTriggered");
        return;
    }
    if (now - m_last >= interval) {
        m_last = now;
        if (!m_repeat) {
            m_running = false;
            notify("running"); // two-way: a one-shot writes running = false back
        } else {
            frame.requestFrameAfter(interval);
        }
        emit("onTriggered");
        return;
    }
    frame.requestFrameAfter(interval - (now - m_last));
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
