// Main-thread timers for C++: a function run once, on the main thread, after a delay on the app clock (imlui.ui.clock).
// They need no Lua state: what delay() (imlui.types.future_combinators) and other C++ code wait on.
//
//   const imlui::MainTimerId id = imlui::addMainTimer(0.5, [this] { blink(); }); // in half a second
//   imlui::removeMainTimer(id);                                                  // or never
//
// The Engine's loop runs the due ones at every loop iteration, after the posted functions (imlui.ui.main_thread),
// outside any frame, and sleeps no longer than until the next one is due (secondsUntilMainTimer), so a timer fires on
// time while the app idles, without rendering a frame for it. Timers due at the same time run in the order they were
// added. A timer runs at most one loop iteration after it came due, also when the clock skipped past it.
//
// Engine::run() drops the timers still waiting when it returns (destroyed on the main thread, without running). Tests
// drive them by hand: runDueMainTimers() on a simulated clock (test::HeadlessFrame does it in renderFrame and
// advanceTime).
// Main thread only.
export module imlui.ui.main_timers;

import std;

import imlui.lua.errors;
import imlui.ui.clock;

export namespace imlui {

/// Names a timer added with addMainTimer. Never 0, never reused.
using MainTimerId = std::uint64_t;

/// Runs fn() once, at the first runDueMainTimers() at least `seconds` from now on the app clock (<= 0, NaN: at the
/// next one; +infinity: never). An empty `fn` adds nothing (returns 0). An exception fn throws is reported as a script
/// error (`main timer: <what>`).
MainTimerId addMainTimer(double seconds, std::function<void()> fn);

/// Removes a timer that hasn't run yet; its function is destroyed here. False for one that ran, was removed, or 0.
bool removeMainTimer(MainTimerId id);

/// Runs the timers due now, earliest first (then in the order they were added). Timers the functions add are left for
/// a later call, even if already due. Returns how many ran.
std::size_t runDueMainTimers();

/// Seconds until the next timer is due: 0 when one is due now, +infinity when none waits.
[[nodiscard]] double secondsUntilMainTimer();

/// Drops every timer without running it (shutdown, tests).
void clearMainTimers();

/// The number of timers waiting.
[[nodiscard]] std::size_t mainTimerCount();

} // namespace imlui

module :private;

namespace imlui {

namespace {

struct MainTimer {
    double due = 0.0;
    std::function<void()> fn;
};

/// The timers. Allocated once and never destroyed: a timer's function may hold a future whose destruction runs code
/// (a dropped delay cancels itself), which must not meet half-destroyed maps during static destruction.
struct Timers {
    MainTimerId lastId = 0;
    std::map<MainTimerId, MainTimer> byId;
    std::set<std::pair<double, MainTimerId>> byDue; ///< (due, id): the run order
};

Timers& state() {
    static Timers* const instance = new Timers; // never deleted, see Timers
    return *instance;
}

} // namespace

MainTimerId addMainTimer(double seconds, std::function<void()> fn) {
    if (!fn) return 0;
    Timers& t = state();
    const MainTimerId id = ++t.lastId;
    if (std::isnan(seconds) || seconds < 0.0) seconds = 0.0;
    // +infinity: never due, kept (removeMainTimer still finds it) but secondsUntilMainTimer() stays infinite.
    const double due = clockTime() + seconds;
    t.byId.emplace(id, MainTimer{due, std::move(fn)});
    t.byDue.emplace(due, id);
    return id;
}

bool removeMainTimer(MainTimerId id) {
    Timers& t = state();
    const auto it = t.byId.find(id);
    if (it == t.byId.end()) return false;
    t.byDue.erase({it->second.due, id});
    const std::function<void()> fn = std::move(it->second.fn); // destroyed after the maps are consistent again
    t.byId.erase(it);
    return true;
}

std::size_t runDueMainTimers() {
    Timers& t = state();
    const double now = clockTime();
    const MainTimerId newest = t.lastId; // timers added from here on wait for the next call
    std::size_t ran = 0;
    for (;;) {
        // The earliest due timer that existed when the call began (one added meanwhile has a larger id).
        auto it = t.byDue.begin();
        while (it != t.byDue.end() && it->first <= now && it->second > newest)
            ++it;
        if (it == t.byDue.end() || it->first > now) break;
        const MainTimerId id = it->second;
        t.byDue.erase(it);
        const auto timer = t.byId.find(id);
        std::function<void()> fn = std::move(timer->second.fn);
        t.byId.erase(timer);
        luaui::reportCxx(fn, "main timer");
        ++ran;
    }
    return ran;
}

double secondsUntilMainTimer() {
    const Timers& t = state();
    if (t.byDue.empty()) return std::numeric_limits<double>::infinity();
    return std::max(0.0, t.byDue.begin()->first - clockTime());
}

void clearMainTimers() {
    Timers& t = state();
    std::map<MainTimerId, MainTimer> dropped;
    dropped.swap(t.byId);
    t.byDue.clear();
    // Destroyed here, with the maps empty: a capture's destructor may add or remove a timer (a dropped delay cancels
    // itself, which may start another).
}

std::size_t mainTimerCount() { return state().byId.size(); }

} // namespace imlui
