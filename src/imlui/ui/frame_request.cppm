// Frame requests for on-demand rendering.
//
// The engine only renders when something asks for a frame: input (a short
// settle period), animations that are still moving, timers that come due,
// or code that calls requestFrame(). All times are on the app clock
// (imlui.ui.clock); callers pass relative delays in seconds.
// Main thread only.
export module imlui.ui.frame_request;

import std;

import imlui.ui.clock;

export namespace imlui {

/// Asks for a frame as soon as possible.
void requestFrame();

/// Asks for a frame `seconds` from now (<= 0 means as soon as possible). NaN and +infinity are ignored.
void requestFrameAfter(double seconds);

/// Asks for frames continuously for the next `seconds` (the settle period after input). NaN and infinities are
/// ignored.
void requestFramesFor(double seconds);

/// Seconds until a frame is due: 0 when one is due now, +infinity when nothing is pending. Never sooner than the
/// minimum frame interval after the last frame began (setMinFrameInterval).
[[nodiscard]] double secondsUntilFrame();

/// The shortest time between the starts of two frames (0, the default: no limit). With vsync on, presenting a frame
/// waits for the display, which paces an animation that asks for a frame every frame; with vsync off nothing does, so
/// the engine sets this instead. NaN, infinite and negative values are 0.
void setMinFrameInterval(double seconds);

/// Called by the engine at the start of a frame: drops the requests that this frame serves.
/// Requests made while the frame renders stay pending for the next one.
void beginFrameRequests();

/// Drops all pending requests and forgets the last frame's start (tests, shutdown). The minimum interval stays.
void clearFrameRequests();

/// Called when a frame is requested while the engine's loop is asleep (waiting for
/// events), so the host can wake it, e.g. with glfwPostEmptyEvent(). Empty to disable.
void setFrameWakeHandler(std::function<void()> handler);

/// Set by the engine: true while its loop runs, false while it waits for events.
/// Requests made while awake need no wake-up, the loop sees them before it waits.
void setLoopAwake(bool awake);

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr double infinity = std::numeric_limits<double>::infinity();
/// Requests closer than this are merged, so a requester that re-asks every frame doesn't pile up entries.
constexpr double mergeWindow = 0.004;
constexpr std::size_t maxPending = 64;

bool frameNow = false;
double minFrameInterval = 0.0;
double lastFrameStart = -infinity; ///< when beginFrameRequests() last ran
double continuousUntil = -infinity;
std::vector<double> pending; // absolute due times, sorted ascending
std::function<void()> wakeHandler;
bool loopAwake = true;

double clockNow() { return clockTime(); }

void wake() {
    if (!loopAwake && wakeHandler) wakeHandler();
}

} // namespace

void requestFrame() {
    if (frameNow) return;
    frameNow = true;
    wake();
}

void requestFrameAfter(double seconds) {
    if (std::isnan(seconds) || seconds == infinity) return;
    if (seconds <= 0) {
        requestFrame();
        return;
    }
    const double due = clockNow() + seconds;
    const auto at = std::lower_bound(pending.begin(), pending.end(), due - mergeWindow) - pending.begin();
    const auto index = static_cast<std::size_t>(at);
    if (index < pending.size() && pending[index] <= due + mergeWindow) return; // already covered
    if (pending.size() >= maxPending) {
        // Keep the earliest ones; a requester that wants a later frame re-asks once those are served.
        if (index == pending.size()) return;
        pending.pop_back(); // invalidates iterators: insert by index
    }
    const bool earliest = index == 0;
    pending.insert(pending.begin() + at, due);
    if (earliest && !frameNow) wake();
}

void requestFramesFor(double seconds) {
    if (!std::isfinite(seconds)) return;
    const double until = clockNow() + seconds;
    if (until <= continuousUntil) return;
    const bool wasIdle = clockNow() >= continuousUntil;
    continuousUntil = until;
    if (wasIdle && !frameNow) wake();
}

double secondsUntilFrame() {
    const double now = clockNow();
    double due = infinity;
    if (frameNow || now < continuousUntil)
        due = 0.0;
    else if (!pending.empty())
        due = std::max(0.0, pending.front() - now);
    if (due == infinity) return due;
    return std::max(due, lastFrameStart + minFrameInterval - now);
}

void setMinFrameInterval(double seconds) { minFrameInterval = std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0; }

void beginFrameRequests() {
    frameNow = false;
    const double now = clockNow();
    lastFrameStart = now;
    pending.erase(pending.begin(), std::upper_bound(pending.begin(), pending.end(), now));
}

void clearFrameRequests() {
    frameNow = false;
    lastFrameStart = -infinity;
    continuousUntil = -infinity;
    pending.clear();
}

void setFrameWakeHandler(std::function<void()> handler) { wakeHandler = std::move(handler); }

void setLoopAwake(bool awake) { loopAwake = awake; }

} // namespace imlui
