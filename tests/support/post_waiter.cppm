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

    /// Waits until at least `count` posts (wakes) were made since this waiter was created, without running them.
    bool waitForPosts(std::size_t count, std::chrono::seconds timeout = std::chrono::seconds{10});

private:
    std::mutex m_mutex;
    std::condition_variable m_posted;
    std::size_t m_posts = 0; ///< guarded by m_mutex
};

} // namespace imlui::test

module :private;

namespace imlui::test {

// The handler runs with the queue's lock held: it only takes m_mutex, and nothing here holds m_mutex while calling
// into the queue, so the two locks are never taken in the other order.
PostWaiter::PostWaiter() {
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
    std::unique_lock lock{m_mutex};
    return m_posted.wait_for(lock, timeout, [&] { return m_posts >= count; });
}

} // namespace imlui::test
