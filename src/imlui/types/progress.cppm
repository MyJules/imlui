// Progress: a thread-safe reporter for how far a job got, before its result.
//
// runAsync(work) passes one to work that takes it (work(std::stop_token, imlui::Progress)); Promise::reporter() makes
// one for other producers. Reports reach the future the reporter belongs to (Future::progress(), f.progress in Lua),
// coalesced: the latest value wins and the main thread applies at most one per loop iteration, so a job may report at
// any rate. A report after the future settled is dropped.
//
//   imlui::Future<Report> Backend::fetch(std::string url) {
//       return runAsync([url](std::stop_token stop, imlui::Progress progress) {
//           return download(url, stop, [&](double done) { progress.report(done); }); // 0..1, any thread
//       });
//   }
export module imlui.types.progress;

import std;

export namespace imlui {

/// Reports a job's progress (a number, by convention 0..1) to its future. Copy it to any thread: report() is
/// thread-safe and cheap, and never keeps the future's promise alive. An empty reporter (default-constructed) drops
/// every report.
class Progress {
public:
    Progress() = default;
    /// A reporter that hands each value to `sink` (thread-safe itself): what Promise::reporter() builds.
    explicit Progress(std::function<void(double)> sink);

    /// Thread-safe: `value` is the job's progress now (a NaN is dropped).
    void report(double value) const;

private:
    std::shared_ptr<const std::function<void(double)>> m_sink;
};

} // namespace imlui

module :private;

namespace imlui {

Progress::Progress(std::function<void(double)> sink) {
    if (sink) m_sink = std::make_shared<const std::function<void(double)>>(std::move(sink));
}

void Progress::report(double value) const {
    if (m_sink) (*m_sink)(value);
}

} // namespace imlui
