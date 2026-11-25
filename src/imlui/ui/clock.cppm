// The app clock: one monotonic time in seconds behind ui.time() (native.time), the Lua timers, the Timer element,
// the snackbar and the frame requests. Unlike ImGui::GetTime(), which advances only when a frame starts, it is
// current outside a frame too (a function posted after an idle period, a script loading), so a time taken there
// isn't already in the past when the next frame comes.
// Main thread only.
export module imlui.ui.clock;

import std;

export namespace imlui {

/// Seconds on the app clock: monotonic, counted from its first use (a steady clock unless a source is set).
[[nodiscard]] double clockTime();

/// Replaces the clock's source (tests: a simulated time the harness advances); an empty function restores the steady
/// clock. The source must never go back.
void setClockSource(std::function<double()> source);

} // namespace imlui

module :private;

namespace imlui {

namespace {

std::function<double()> clockSource;

double steadyTime() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

} // namespace

double clockTime() { return clockSource ? clockSource() : steadyTime(); }

void setClockSource(std::function<double()> source) { clockSource = std::move(source); }

} // namespace imlui
