// Task: a handle to a job started by ScriptObject::runAsync(work, done[, fail]), to cancel it or ask whether it still
// runs.
//
//   imlui::Task m_search;
//   void Finder::setQuery(std::string query) {
//       m_search.cancel(); // the previous search: its work's token stops, its `done` never runs
//       m_search = runAsync([query](std::stop_token stop) { return search(query, stop); },
//                           [this](std::vector<Hit> hits) { m_hits = std::move(hits); notify("hits"); });
//   }
//
// A task is running until it ends one way: `done` or `fail` is called, it is cancelled, or the job is dropped (the
// object died, or the pool shut down before the work started). The end is decided once, atomically: cancel() returning
// true means `done`/`fail` will not be called, from whatever thread it was called.
export module imlui.types.task;

import std;

export namespace imlui {

/// A cheap, copyable handle to a runAsync job. Copies share one state; every member is thread-safe. An empty task
/// (default-constructed) isn't running, and cancel() does nothing.
class Task {
public:
    Task() = default;

    /// For the code that runs the job (ScriptObject::runAsync): a new, running task.
    [[nodiscard]] static Task make();

    /// Ends a running task as cancelled: stops stopToken() (the work's token) and makes sure `done`/`fail` aren't
    /// called. False (doing nothing) once the task ended.
    bool cancel() const;
    /// True until the task ends: `done`/`fail` is about to run or ran, it was cancelled, or the job was dropped.
    [[nodiscard]] bool running() const;
    /// True once cancel() ended it.
    [[nodiscard]] bool cancelled() const;

    /// For the code that runs the job: stopped by cancel(). Never stops for an empty task.
    [[nodiscard]] std::stop_token stopToken() const;
    /// For the code that runs the job: ends a running task as finished, right before calling `done`/`fail` (or when it
    /// drops the job). False (and the caller must not call them) when it ended already, e.g. cancelled.
    bool finish() const;

private:
    struct State;
    explicit Task(std::shared_ptr<State> state);

    std::shared_ptr<State> m_state;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

enum class TaskEnd : std::uint8_t { Running, Finished, Cancelled };

} // namespace

struct Task::State {
    std::atomic<TaskEnd> end{TaskEnd::Running};
    std::stop_source stop;
};

Task::Task(std::shared_ptr<State> state) : m_state{std::move(state)} {}

Task Task::make() { return Task{std::make_shared<State>()}; }

bool Task::cancel() const {
    if (!m_state) return false;
    TaskEnd expected = TaskEnd::Running;
    if (!m_state->end.compare_exchange_strong(expected, TaskEnd::Cancelled)) return false;
    m_state->stop.request_stop();
    return true;
}

bool Task::running() const { return m_state && m_state->end.load() == TaskEnd::Running; }

bool Task::cancelled() const { return m_state && m_state->end.load() == TaskEnd::Cancelled; }

std::stop_token Task::stopToken() const { return m_state ? m_state->stop.get_token() : std::stop_token{}; }

bool Task::finish() const {
    if (!m_state) return false;
    TaskEnd expected = TaskEnd::Running;
    return m_state->end.compare_exchange_strong(expected, TaskEnd::Finished);
}

} // namespace imlui
