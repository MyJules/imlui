// The main-thread queue: the one way other threads hand work to the UI.
//
// Lua, the element tree, notify() and emit() are main-thread only. A worker thread posts a function with post(),
// from anywhere, without an Engine&; the main thread runs it at its next loop iteration (runPosted()), outside any
// frame. While an Engine runs, it installs a wake handler (glfwPostEmptyEvent) so a post wakes the sleeping loop.
//
// No loop running: functions are kept, in order, and run by the next runPosted() (an Engine's first loop
// iteration). Engine::run() drops what is still queued when it returns (the functions are destroyed on the main
// thread, without running).
//
// To post to an element or singleton that Lua may destroy before the function runs, use ScriptObject::handle()
// (module imlui.types.script_object): its post() runs the function only if the object is still alive.
export module imlui.ui.main_thread;

import std;

import imlui.lua.errors;

export namespace imlui {

/// Thread-safe: queues `fn` to run on the main thread at the next runPosted() and wakes the loop (the wake
/// handler). Functions run in the order they were posted. An empty `fn` is ignored.
void post(std::function<void()> fn);

/// Main thread: runs the functions queued so far, in order. A function that throws is reported as a script error
/// (the error overlay and the log) and the rest still run. Functions posted meanwhile (by a posted function too)
/// wait for the next call. Returns the number of functions run.
std::size_t runPosted();

/// Main thread: drops every queued function without running it. Their captures are destroyed here.
void clearPosted();

/// The number of queued functions. Thread-safe (but stale as soon as it returns).
[[nodiscard]] std::size_t postedCount();

/// Called after every post(), on the posting thread, so the main loop wakes up: must be thread-safe (the Engine
/// installs glfwPostEmptyEvent while its loop runs). Empty to disable. Main thread.
void setPostWakeHandler(std::function<void()> handler);

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Guards `queue` and `wakeHandler`. The handler is called with it held, so it can't be removed (and the window it
/// wakes destroyed) while a worker is inside it.
std::mutex mutex;
std::vector<std::function<void()>> queue;
std::function<void()> wakeHandler;

} // namespace

void post(std::function<void()> fn) {
    if (!fn) return;
    std::scoped_lock lock{mutex};
    queue.push_back(std::move(fn));
    if (wakeHandler) wakeHandler();
}

std::size_t runPosted() {
    std::vector<std::function<void()>> posted;
    {
        std::scoped_lock lock{mutex};
        posted.swap(queue);
    }
    for (auto& fn : posted)
        luaui::reportCxx(fn, "posted function");
    return posted.size();
}

void clearPosted() {
    std::vector<std::function<void()>> dropped;
    {
        std::scoped_lock lock{mutex};
        dropped.swap(queue);
    }
    // Destroyed here, outside the lock: a capture's destructor may post again.
}

std::size_t postedCount() {
    std::scoped_lock lock{mutex};
    return queue.size();
}

void setPostWakeHandler(std::function<void()> handler) {
    std::scoped_lock lock{mutex};
    wakeHandler = std::move(handler);
}

} // namespace imlui
