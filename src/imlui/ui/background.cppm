// The background runner: a process-wide pool of worker threads for work that must not block the main loop.
//
// runInBackground(work) queues `work` from any thread; a worker runs it as work(stopToken). The pool starts lazily:
// threads are created as work comes in, up to backgroundThreadLimit() (hardware_concurrency() - 1, clamped to
// [2, 8]), and then wait for more. Tasks run in the order they were queued, several at once.
//
// Work never touches Lua, the element tree, notify() or emit(): it hands results back with imlui::post (module
// imlui.ui.main_thread) or, for an object Lua may destroy, ScriptObject::runAsync / Handle::post. A task that throws is
// reported on the main thread ("background task: <what>", through imlui::post).
//
// Shutdown (shutdownBackground(), which Engine::run() calls when it returns):
//  1. the pool is detached: work queued from then on starts a new pool;
//  2. every task's stop token is stopped (running tasks see it; their stop callbacks run on the calling thread);
//  3. queued tasks are dropped: destroyed on the calling thread without running;
//  4. the workers end: they are joined if every running task returns within `wait`, otherwise they are detached and
//     each ends after its current task, holding the pool's state alive itself (nothing it uses is destroyed).
// So a task should poll its stop token and return soon after it stops. A pool never shut down (no Engine::run(), or
// work queued after it returned) is never joined: the OS ends its threads at process exit. Its state is never
// destroyed, so static destruction can't pull it from under a worker.
export module imlui.ui.background;

import std;

import imlui.lua.errors;
import imlui.ui.main_thread;

export namespace imlui {

/// How long shutdownBackground() waits for running tasks by default.
constexpr std::chrono::milliseconds backgroundShutdownWait{1000};

/// Thread-safe: queues `work` on the background pool (starting it, or another worker, if needed). It runs on a worker
/// thread as work(stopToken); the token is stopped by shutdownBackground(). An empty `work` is ignored.
void runInBackground(std::function<void(std::stop_token)> work);

/// Thread-safe (Engine::run() calls it on the main thread when it returns): stops every task's token, drops the
/// queued tasks (destroyed here, without running) and waits up to `wait` for the running ones to return. The workers
/// are joined when they all did, otherwise detached (each ends after its task). Work queued afterwards starts a new
/// pool. Must not be called from a background task.
void shutdownBackground(std::chrono::milliseconds wait = backgroundShutdownWait);

/// The most worker threads the pool runs: std::thread::hardware_concurrency() - 1, clamped to [2, 8].
[[nodiscard]] std::size_t backgroundThreadLimit();

/// Thread-safe: tasks queued or running in the current pool (stale as soon as it returns; for tests and stats).
[[nodiscard]] std::size_t backgroundTaskCount();

} // namespace imlui

module :private;

namespace imlui {

namespace {

using Task = std::function<void(std::stop_token)>;

/// One pool, from its first task to its shutdown. Shared by the runner and every worker thread, so a worker detached
/// by a shutdown keeps it alive until it ends.
struct Pool {
    std::mutex mutex;
    std::condition_variable wake;   ///< a task was queued, or the pool is closing
    std::condition_variable exited; ///< a worker ended
    std::deque<Task> queue;
    std::vector<std::thread> threads;
    std::size_t available = 0; ///< workers started and not running a task
    std::size_t running = 0;   ///< tasks running
    std::size_t live = 0;      ///< workers whose loop hasn't ended
    bool closing = false;
    std::stop_source stop; ///< the token every task gets
};

/// The current pool. Allocated once and never destroyed (see the module comment).
struct Runner {
    std::mutex mutex;
    std::shared_ptr<Pool> pool;
};

Runner& runner() {
    static Runner* const instance = new Runner; // never deleted
    return *instance;
}

/// Runs a task, reporting what it throws on the main thread (the error list is main-thread only).
void runTask(const Task& task, std::stop_token stop) {
    try {
        task(std::move(stop));
    } catch (const std::exception& e) {
        post([what = std::string{e.what()}] { luaui::reportException("background task", {}, what.c_str()); });
    } catch (...) {
        post([] { luaui::reportException("background task", {}, nullptr); });
    }
}

void workerLoop(const std::shared_ptr<Pool>& pool) {
    std::unique_lock lock{pool->mutex};
    for (;;) {
        pool->wake.wait(lock, [&] { return pool->closing || !pool->queue.empty(); });
        if (pool->closing) break;
        Task task = std::move(pool->queue.front());
        pool->queue.pop_front();
        --pool->available;
        ++pool->running;
        const std::stop_token stop = pool->stop.get_token();
        lock.unlock();
        runTask(task, stop);
        task = nullptr; // its captures go before the lock is taken again: their destructors may queue more work
        lock.lock();
        --pool->running;
        ++pool->available;
    }
    --pool->live;
    pool->exited.notify_all();
}

} // namespace

std::size_t backgroundThreadLimit() {
    const unsigned cores = std::thread::hardware_concurrency(); // 0 when unknown
    return std::clamp<std::size_t>(cores > 1 ? cores - 1 : 0, 2, 8);
}

void runInBackground(std::function<void(std::stop_token)> work) {
    if (!work) return;
    std::shared_ptr<Pool> pool;
    {
        Runner& r = runner();
        std::scoped_lock lock{r.mutex};
        if (!r.pool) r.pool = std::make_shared<Pool>();
        pool = r.pool;
    }
    std::scoped_lock lock{pool->mutex};
    // A shutdown that detached this pool between the two locks: the work is dropped like the queued tasks it drops.
    if (pool->closing) return;
    pool->queue.push_back(std::move(work));
    if (pool->queue.size() > pool->available && pool->threads.size() < backgroundThreadLimit()) {
        ++pool->available;
        ++pool->live;
        pool->threads.emplace_back([pool] { workerLoop(pool); });
    }
    pool->wake.notify_one();
}

void shutdownBackground(std::chrono::milliseconds wait) {
    std::shared_ptr<Pool> pool;
    {
        Runner& r = runner();
        std::scoped_lock lock{r.mutex};
        pool = std::move(r.pool);
    }
    if (!pool) return;
    std::deque<Task> dropped;
    std::vector<std::thread> threads;
    {
        std::scoped_lock lock{pool->mutex};
        pool->closing = true;
        dropped.swap(pool->queue);
        threads.swap(pool->threads);
    }
    pool->wake.notify_all();
    pool->stop.request_stop(); // outside the pool's lock: the tasks' stop callbacks run here
    dropped.clear();           // likewise: a capture's destructor may queue work (into a new pool)
    bool ended = false;
    {
        std::unique_lock lock{pool->mutex};
        ended = pool->exited.wait_for(lock, wait, [&] { return pool->live == 0; });
    }
    for (auto& thread : threads) {
        if (ended)
            thread.join();
        else
            thread.detach();
    }
}

std::size_t backgroundTaskCount() {
    std::shared_ptr<Pool> pool;
    {
        Runner& r = runner();
        std::scoped_lock lock{r.mutex};
        pool = r.pool;
    }
    if (!pool) return 0;
    std::scoped_lock lock{pool->mutex};
    return pool->queue.size() + pool->running;
}

} // namespace imlui
