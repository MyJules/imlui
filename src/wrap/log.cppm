// Module wrapper around spdlog.
//
//   import imlui.log;
//   imlui::log::info("loaded {} in {} ms", path, ms);
//
// Formatting uses std::format on the caller side; spdlog only receives the
// finished string, so no spdlog/fmt templates cross the module boundary.
// The templates check enabled() first: a message at a level that is off is
// never formatted (its arguments' formatters don't run).
module;

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

export module imlui.log;

import std;

export namespace imlui::log {

enum class Level { Trace, Debug, Info, Warn, Error, Critical };

/// Also log into file `path`, appending (any file name: on Windows it's opened by its UTF-16 name, so characters
/// outside the ANSI code page work). Safe while other threads log. False, with the reason logged, when the file
/// can't be opened.
bool addFileSink(const std::filesystem::path& path);

void setLevel(Level level);

/// Whether a message at `level` is logged (spdlog's should_log on the current logger): one atomic load, no lock.
/// Check it before building a message that costs something; the templates below do.
[[nodiscard]] bool enabled(Level level);

/// Logs an already formatted message; `source` tags it, e.g. "lua". Does nothing when `level` isn't enabled().
void write(Level level, std::string_view msg, std::string_view source = {});

template <class... A> void trace(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::Trace)) write(Level::Trace, std::format(f, std::forward<A>(a)...));
}
template <class... A> void debug(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::Debug)) write(Level::Debug, std::format(f, std::forward<A>(a)...));
}
template <class... A> void info(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::Info)) write(Level::Info, std::format(f, std::forward<A>(a)...));
}
template <class... A> void warn(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::Warn)) write(Level::Warn, std::format(f, std::forward<A>(a)...));
}
template <class... A> void error(std::format_string<A...> f, A&&... a) {
    if (enabled(Level::Error)) write(Level::Error, std::format(f, std::forward<A>(a)...));
}

} // namespace imlui::log

module :private;

namespace imlui::log {

namespace {

std::mutex loggerMutex; ///< serializes the changes of the logger (addFileSink, setLevel); writers don't take it

/// Every logger that was ever current, the current one last. Adding a sink replaces the logger with a copy that has
/// the sink (a logger's sink list mustn't change while other threads log through it). The replaced one is kept, not
/// freed: a writer that loaded the pointer just before the swap may still be logging through it. That holds nothing
/// open the current one doesn't (the copy shares its sinks), and only addFileSink adds one, a handful per process.
/// So writers need no lock and no reference count: one atomic load (current()), on every message, enabled or not.
std::vector<std::shared_ptr<spdlog::logger>>& loggers() {
    static std::vector<std::shared_ptr<spdlog::logger>> all = [] {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        auto lg = std::make_shared<spdlog::logger>("imlui", sink);
        lg->set_pattern("%^[%H:%M:%S.%e] [%n] [%l]%$ %v");
        lg->set_level(spdlog::level::debug);
        std::vector<std::shared_ptr<spdlog::logger>> list;
        list.push_back(std::move(lg));
        return list;
    }();
    return all;
}

/// The current logger: loggers().back(), published for the writers.
std::atomic<spdlog::logger*>& current() {
    static std::atomic<spdlog::logger*> slot{loggers().back().get()};
    return slot;
}

spdlog::logger& logger() { return *current().load(std::memory_order_acquire); }

/// Appends to a file opened through std::filesystem::path, so by its UTF-16 name on Windows (spdlog's own file sink
/// takes a narrow name, which Windows reads in the ANSI code page).
class FileSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit FileSink(std::ofstream out) : m_out{std::move(out)} {}

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        m_out.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
    }
    void flush_() override { m_out.flush(); }

private:
    std::ofstream m_out;
};

spdlog::level::level_enum toSpd(Level level) {
    switch (level) {
    case Level::Trace: return spdlog::level::trace;
    case Level::Debug: return spdlog::level::debug;
    case Level::Info: return spdlog::level::info;
    case Level::Warn: return spdlog::level::warn;
    case Level::Error: return spdlog::level::err;
    case Level::Critical: return spdlog::level::critical;
    }
    return spdlog::level::info;
}

} // namespace

bool addFileSink(const std::filesystem::path& path) {
    try {
        std::ofstream out{path, std::ios::binary | std::ios::app};
        if (!out) {
            const std::u8string name = path.generic_u8string();
            write(Level::Error, "can't open log file '" + std::string{name.begin(), name.end()} + "'");
            return false;
        }
        auto sink = std::make_shared<FileSink>(std::move(out));
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        std::scoped_lock lock{loggerMutex};
        auto& all = loggers();
        all.reserve(all.size() + 1); // the push_back below can't throw once the copy is made
        std::shared_ptr<spdlog::logger> withFile = all.back()->clone(all.back()->name());
        withFile->sinks().push_back(std::move(sink)); // not shared yet: no other thread logs through it
        all.push_back(std::move(withFile));
        current().store(all.back().get(), std::memory_order_release); // published with its sink list complete
    } catch (const std::exception& e) { // spdlog::spdlog_ex, out of memory
        write(Level::Error, std::string{"can't add a log file: "} + e.what());
        return false;
    }
    return true;
}

// Under the lock: a level set while addFileSink copies the logger would otherwise be lost with the old one.
void setLevel(Level level) {
    std::scoped_lock lock{loggerMutex};
    logger().set_level(toSpd(level));
}

bool enabled(Level level) { return logger().should_log(toSpd(level)); }

void write(Level level, std::string_view msg, std::string_view source) {
    spdlog::logger& lg = logger();
    const auto spdLevel = toSpd(level);
    if (!lg.should_log(spdLevel)) return; // before the "[source] msg" formatting below
    if (source.empty()) lg.log(spdLevel, "{}", msg);
    else lg.log(spdLevel, "[{}] {}", source, msg);
    if (level >= Level::Warn) lg.flush();
}

} // namespace imlui::log
