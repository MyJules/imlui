// Frame requests for on-demand rendering.
//
// The engine only renders when something asks for a frame: input (a short
// settle period), animations that are still moving, timers that come due,
// or code that calls requestFrame(). All times are on a monotonic clock
// private to this module; callers pass relative delays in seconds.
// Main thread only.
export module imlui.ui.frame_request;

import std;

export namespace imlui {

/// Asks for a frame as soon as possible.
void requestFrame();

/// Asks for a frame `seconds` from now (<= 0 means as soon as possible).
void requestFrameAfter(double seconds);

/// Asks for frames continuously for the next `seconds` (the settle period after input).
void requestFramesFor(double seconds);

/// Seconds until a frame is due: 0 when one is due now, +infinity when nothing is pending.
[[nodiscard]] double secondsUntilFrame();

/// Called by the engine at the start of a frame: drops the requests that this frame serves.
/// Requests made while the frame renders stay pending for the next one.
void beginFrameRequests();

/// Drops all pending requests (tests, shutdown).
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
double continuousUntil = -infinity;
std::vector<double> pending; // absolute due times, sorted ascending
std::function<void()> wakeHandler;
bool loopAwake = true;

double clockNow() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

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
    if (seconds <= 0) {
        requestFrame();
        return;
    }
    const double due = clockNow() + seconds;
    auto it = std::lower_bound(pending.begin(), pending.end(), due - mergeWindow);
    if (it != pending.end() && *it <= due + mergeWindow) return; // already covered
    if (pending.size() >= maxPending) {
        // Keep the earliest ones; a requester that wants a later frame re-asks once those are served.
        if (it == pending.end()) return;
        pending.pop_back();
    }
    const bool earliest = it == pending.begin();
    pending.insert(it, due);
    if (earliest && !frameNow) wake();
}

void requestFramesFor(double seconds) {
    const double until = clockNow() + seconds;
    if (until <= continuousUntil) return;
    const bool wasIdle = clockNow() >= continuousUntil;
    continuousUntil = until;
    if (wasIdle && !frameNow) wake();
}

double secondsUntilFrame() {
    if (frameNow) return 0.0;
    const double now = clockNow();
    if (now < continuousUntil) return 0.0;
    if (pending.empty()) return infinity;
    return std::max(0.0, pending.front() - now);
}

void beginFrameRequests() {
    frameNow = false;
    const double now = clockNow();
    pending.erase(pending.begin(), std::upper_bound(pending.begin(), pending.end(), now));
}

void clearFrameRequests() {
    frameNow = false;
    continuousUntil = -infinity;
    pending.clear();
}

void setFrameWakeHandler(std::function<void()> handler) { wakeHandler = std::move(handler); }

void setLoopAwake(bool awake) { loopAwake = awake; }

} // namespace imlui
