// The main-thread queue: the one way other threads hand work to the UI.
//
// Lua, the element tree, notify() and emit() are main-thread only. A worker thread posts a function with post(),
// from anywhere, without an Engine&; the main thread runs it at its next loop iteration (runPosted()), outside any
// frame. While an Engine runs, it installs a wake handler (platform::Window::postEmptyEvent, an SDL user event) so a
// post wakes the sleeping loop: the post that finds the queue empty does, outside the queue's lock; those after it
// until the queue is run ride along.
//
// No loop running: functions are kept, in order, and run by the next runPosted() (an Engine's first loop
// iteration). Engine::run() drops what is still queued when it returns (the functions are destroyed on the main
// thread, without running).
//
// To post to an element or singleton that Lua may destroy before the function runs, use ScriptObject::handle()
// (module imlui.types.script_object): its post() runs the function only if the object is still alive.
//
// The queue is never destroyed (not even by static destruction), so post() stays safe from a worker thread that
// outlives main().
export module imlui.ui.main_thread;

import std;

import imlui.lua.errors;

export namespace imlui {

/// Thread-safe: queues `fn` to run on the main thread at the next runPosted() and wakes the loop (the wake
/// handler) when the queue was empty: a loop woken for the first function runs the ones posted after it too.
/// Functions run in the order they were posted. An empty `fn` is ignored.
void post(std::function<void()> fn);

/// Main thread: runs the functions queued so far, in order. A function that throws is reported as a script error
/// (the error overlay and the log) and the rest still run. Functions posted meanwhile (by a posted function too)
/// wait for the next call. Returns the number of functions run.
std::size_t runPosted();

/// Main thread: drops every queued function without running it. Their captures are destroyed here.
void clearPosted();

/// The number of queued functions. Thread-safe (but stale as soon as it returns).
[[nodiscard]] std::size_t postedCount();

/// Called after a post() that found the queue empty, on the posting thread, without the queue's lock, so the main loop
/// wakes up: must be thread-safe (the Engine installs platform::Window::postEmptyEvent while its loop runs) and must
/// not call setPostWakeHandler. Empty to disable. Main thread; waits for posting threads still inside the previous
/// handler, so once it returns that one is no longer called. Functions already queued when a handler is installed don't wake the
/// loop: run runPosted() after installing it (the Engine's loop does at once).
void setPostWakeHandler(std::function<void()> handler);

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// The queue's state. Allocated once and never destroyed: a worker thread may post while the process exits (static
/// destructors run while other threads still do, until the OS ends them), and must not find a destroyed mutex.
struct Queue {
    /// Guards the fields below. The handler is called without it (a post doesn't hold up the others, or the main
    /// thread taking the queue, while it wakes the loop); `waking` counts the threads inside it, and
    /// setPostWakeHandler waits for none before it replaces the handler (and the Engine destroys the window it wakes).
    std::mutex mutex;
    std::condition_variable woke; ///< `waking` went down
    std::vector<std::function<void()>> functions;
    std::function<void()> wakeHandler;
    std::size_t waking = 0;
};

Queue& queue() {
    static Queue* const instance = new Queue; // never deleted, see Queue
    return *instance;
}

} // namespace

void post(std::function<void()> fn) {
    if (!fn) return;
    Queue& q = queue();
    {
        std::scoped_lock lock{q.mutex};
        // Only the first function of a batch wakes the loop: the queue is non-empty after that until runPosted() takes
        // it (under this lock), and that run takes this function too. A post after the take finds it empty again and
        // wakes, so none is left waiting.
        const bool wake = q.functions.empty() && q.wakeHandler;
        q.functions.push_back(std::move(fn));
        if (!wake) return;
        ++q.waking; // the handler stays as it is until this thread is out of it (setPostWakeHandler waits)
    }
    // Leaves the handler also when it throws, or setPostWakeHandler would wait for ever.
    struct Leave {
        Queue& q;
        ~Leave() {
            {
                std::scoped_lock lock{q.mutex};
                --q.waking;
            }
            q.woke.notify_all();
        }
    } leave{q};
    q.wakeHandler();
}

std::size_t runPosted() {
    std::vector<std::function<void()>> posted;
    {
        Queue& q = queue();
        std::scoped_lock lock{q.mutex};
        posted.swap(q.functions);
    }
    for (auto& fn : posted)
        luaui::reportCxx(fn, "posted function");
    return posted.size();
}

void clearPosted() {
    std::vector<std::function<void()>> dropped;
    {
        Queue& q = queue();
        std::scoped_lock lock{q.mutex};
        dropped.swap(q.functions);
    }
    // Destroyed here, outside the lock: a capture's destructor may post again.
}

std::size_t postedCount() {
    Queue& q = queue();
    std::scoped_lock lock{q.mutex};
    return q.functions.size();
}

void setPostWakeHandler(std::function<void()> handler) {
    Queue& q = queue();
    std::unique_lock lock{q.mutex};
    q.woke.wait(lock, [&] { return q.waking == 0; });
    q.wakeHandler.swap(handler);
    lock.unlock();
    // The previous handler is destroyed here, outside the lock.
}

} // namespace imlui
