// PostWaiter: runs the main-thread queue (imlui.ui.main_thread) until a condition holds, woken by the posts
// themselves (it installs the post wake handler, as the Engine does), so tests of worker threads wait without sleeps
// or busy loops. Test-only (built into imlui_tests). Main thread, one at a time (the wake handler is global).
//
//   test::PostWaiter posts;
//   startWorker();
//   ASSERT_TRUE(posts.runUntil([&] { return done; }));
export module imlui.test.post_waiter;

import std;

import imlui.ui.main_thread;

export namespace imlui::test {

class PostWaiter {
public:
    PostWaiter();
    ~PostWaiter();
    PostWaiter(const PostWaiter&) = delete;
    PostWaiter& operator=(const PostWaiter&) = delete;
    PostWaiter(PostWaiter&&) = delete;
    PostWaiter& operator=(PostWaiter&&) = delete;

    /// Runs the posted functions (runPosted()) until `done()` holds, waiting for the next post in between. False when
    /// it still doesn't after `timeout`.
    bool runUntil(const std::function<bool()>& done, std::chrono::seconds timeout = std::chrono::seconds{10});

    /// Waits until at least `count` more functions are queued than when this waiter was created, without running them
    /// (so call it before runUntil(), which runs them). Only a post into an empty queue wakes the waiter, so it also
    /// looks at the queue's length every millisecond.
    bool waitForPosts(std::size_t count, std::chrono::seconds timeout = std::chrono::seconds{10});

private:
    std::mutex m_mutex;
    std::condition_variable m_posted;
    std::size_t m_posts = 0;  ///< guarded by m_mutex
    std::size_t m_queued = 0; ///< postedCount() when the waiter was created
};

} // namespace imlui::test

module :private;

namespace imlui::test {

// The handler runs on the posting thread, outside the queue's lock, for a post that found the queue empty: counting
// those is enough, as runUntil() runs the queue (emptying it) before it waits for the next one.
PostWaiter::PostWaiter() : m_queued{postedCount()} {
    setPostWakeHandler([this] {
        {
            const std::scoped_lock lock{m_mutex};
            ++m_posts;
        }
        m_posted.notify_all();
    });
}

PostWaiter::~PostWaiter() { setPostWakeHandler({}); }

bool PostWaiter::runUntil(const std::function<bool()>& done, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        std::size_t seen = 0;
        {
            const std::scoped_lock lock{m_mutex};
            seen = m_posts;
        }
        runPosted();
        if (done()) return true;
        std::unique_lock lock{m_mutex};
        if (!m_posted.wait_until(lock, deadline, [&] { return m_posts != seen; })) return done();
    }
}

bool PostWaiter::waitForPosts(std::size_t count, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto done = [&] { return postedCount() >= m_queued + count; };
    std::unique_lock lock{m_mutex};
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        // Woken by the first post; the ones after it don't wake (the queue isn't empty), hence the short wait.
        m_posted.wait_for(lock, std::chrono::milliseconds{1});
    }
    return true;
}

} // namespace imlui::test
