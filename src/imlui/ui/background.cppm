// The background runner: a process-wide pool of worker threads for work that must not block the main loop.
//
// runInBackground(work) queues `work` from any thread; a worker runs it as work(stopToken). The pool starts lazily:
// threads are created as work comes in, up to backgroundThreadLimit() (hardware_concurrency() - 1, clamped to
// [2, 8]), and then wait for more. Tasks run in the order they were queued, several at once.
//
// Image decodes (ImageCache) run in a lane of their own, BackgroundLane::Decodes: at most decodeThreadLimit workers,
// the newest request first (the images just scrolled into view before the ones scrolled past). So fifty photos don't
// hold up a runAsync queued after them, and long-running tasks that fill the main lane don't starve the images.
//
// Work never touches Lua, the element tree, notify() or emit(): it hands results back with imlui::post (module
// imlui.ui.main_thread) or, for an object Lua may destroy, ScriptObject::runAsync / Handle::post. A task that throws is
// reported on the main thread ("background task: <what>", through imlui::post).
//
// Shutdown (shutdownBackground(), which Engine::run() calls when it returns), for both lanes:
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

/// The pool's lanes, each with workers and a queue of its own.
enum class BackgroundLane {
    Tasks,   ///< runInBackground's default: first queued, first run; up to backgroundThreadLimit() workers
    Decodes, ///< image decodes: last queued, first run; up to decodeThreadLimit workers
};

/// The most worker threads of BackgroundLane::Decodes.
constexpr std::size_t decodeThreadLimit = 2;

/// Thread-safe: queues `work` in `lane` of the background pool (starting it, or another worker, if needed). It runs on
/// a worker thread as work(stopToken); the token is stopped by shutdownBackground(). An empty `work` is ignored.
void runInBackground(std::function<void(std::stop_token)> work, BackgroundLane lane = BackgroundLane::Tasks);

/// Thread-safe (Engine::run() calls it on the main thread when it returns): stops every task's token (both lanes),
/// drops the queued tasks (destroyed here, without running) and waits up to `wait` for the running ones to return. The
/// workers are joined when they all did, otherwise detached (each ends after its task). Work queued afterwards starts a
/// new pool. Must not be called from a background task.
void shutdownBackground(std::chrono::milliseconds wait = backgroundShutdownWait);

/// The most worker threads the pool runs: std::thread::hardware_concurrency() - 1, clamped to [2, 8].
[[nodiscard]] std::size_t backgroundThreadLimit();

/// Thread-safe: tasks queued or running in the current pool, both lanes (stale as soon as it returns; for tests and
/// stats).
[[nodiscard]] std::size_t backgroundTaskCount();

} // namespace imlui

module :private;

namespace imlui {

namespace {

using Task = std::function<void(std::stop_token)>;

/// One lane's pool, from its first task to its shutdown. Shared by the runner and every worker thread, so a worker
/// detached by a shutdown keeps it alive until it ends.
struct Pool {
    Pool(bool newestFirst, std::size_t threadLimit) : lifo{newestFirst}, limit{threadLimit} {}
    const bool lifo;         ///< workers take the newest task (BackgroundLane::Decodes) instead of the oldest
    const std::size_t limit; ///< the most worker threads
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

constexpr std::size_t laneCount = 2;

/// The current pool of each lane (by BackgroundLane). Allocated once and never destroyed (see the module comment).
struct Runner {
    std::mutex mutex;
    std::array<std::shared_ptr<Pool>, laneCount> pools;
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
        Task task;
        if (pool->lifo) {
            task = std::move(pool->queue.back());
            pool->queue.pop_back();
        } else {
            task = std::move(pool->queue.front());
            pool->queue.pop_front();
        }
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

void runInBackground(std::function<void(std::stop_token)> work, BackgroundLane lane) {
    if (!work) return;
    std::shared_ptr<Pool> pool;
    {
        Runner& r = runner();
        std::scoped_lock lock{r.mutex};
        std::shared_ptr<Pool>& current = r.pools.at(static_cast<std::size_t>(lane));
        if (!current) {
            const bool decodes = lane == BackgroundLane::Decodes;
            current = std::make_shared<Pool>(decodes, decodes ? decodeThreadLimit : backgroundThreadLimit());
        }
        pool = current;
    }
    std::scoped_lock lock{pool->mutex};
    // A shutdown that detached this pool between the two locks: the work is dropped like the queued tasks it drops.
    if (pool->closing) return;
    pool->queue.push_back(std::move(work));
    if (pool->queue.size() > pool->available && pool->threads.size() < pool->limit) {
        ++pool->available;
        ++pool->live;
        pool->threads.emplace_back([pool] { workerLoop(pool); });
    }
    pool->wake.notify_one();
}

void shutdownBackground(std::chrono::milliseconds wait) {
    std::array<std::shared_ptr<Pool>, laneCount> pools;
    {
        Runner& r = runner();
        std::scoped_lock lock{r.mutex};
        pools.swap(r.pools);
    }
    std::deque<Task> dropped;
    std::vector<std::thread> threads;
    for (const auto& pool : pools) {
        if (!pool) continue;
        {
            std::scoped_lock lock{pool->mutex};
            pool->closing = true;
            for (auto& task : pool->queue)
                dropped.push_back(std::move(task));
            pool->queue.clear();
            for (auto& thread : pool->threads)
                threads.push_back(std::move(thread));
            pool->threads.clear();
        }
        pool->wake.notify_all();
        pool->stop.request_stop(); // outside the pool's lock: the tasks' stop callbacks run here
    }
    dropped.clear(); // likewise: a capture's destructor may queue work (into a new pool)
    // One deadline for both lanes.
    const auto deadline = std::chrono::steady_clock::now() + wait;
    bool ended = true;
    for (const auto& pool : pools) {
        if (!pool) continue;
        std::unique_lock lock{pool->mutex};
        ended = pool->exited.wait_until(lock, deadline, [&] { return pool->live == 0; }) && ended;
    }
    for (auto& thread : threads) {
        if (ended)
            thread.join();
        else
            thread.detach();
    }
}

std::size_t backgroundTaskCount() {
    std::array<std::shared_ptr<Pool>, laneCount> pools;
    {
        Runner& r = runner();
        std::scoped_lock lock{r.mutex};
        pools = r.pools;
    }
    std::size_t count = 0;
    for (const auto& pool : pools) {
        if (!pool) continue;
        std::scoped_lock lock{pool->mutex};
        count += pool->queue.size() + pool->running;
    }
    return count;
}

} // namespace imlui
