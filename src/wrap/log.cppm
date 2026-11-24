// Module wrapper around spdlog.
//
//   import imlui.log;
//   imlui::log::info("loaded {} in {} ms", path, ms);
//
// Formatting uses std::format on the caller side; spdlog only receives the
// finished string, so no spdlog/fmt templates cross the module boundary.
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

/// Logs an already formatted message; `source` tags it, e.g. "lua".
void write(Level level, std::string_view msg, std::string_view source = {});

template <class... A> void trace(std::format_string<A...> f, A&&... a) { write(Level::Trace, std::format(f, std::forward<A>(a)...)); }
template <class... A> void debug(std::format_string<A...> f, A&&... a) { write(Level::Debug, std::format(f, std::forward<A>(a)...)); }
template <class... A> void info(std::format_string<A...> f, A&&... a) { write(Level::Info, std::format(f, std::forward<A>(a)...)); }
template <class... A> void warn(std::format_string<A...> f, A&&... a) { write(Level::Warn, std::format(f, std::forward<A>(a)...)); }
template <class... A> void error(std::format_string<A...> f, A&&... a) { write(Level::Error, std::format(f, std::forward<A>(a)...)); }

} // namespace imlui::log

module :private;

namespace imlui::log {

namespace {

std::mutex loggerMutex; ///< guards the logger pointer (loggerSlot()), not the logger

/// The current logger. Adding a sink replaces it with a copy that has the sink (a logger's sink list mustn't change
/// while other threads log through it), so writers log through their own reference (logger()).
std::shared_ptr<spdlog::logger>& loggerSlot() {
    static std::shared_ptr<spdlog::logger> instance = [] {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        auto lg = std::make_shared<spdlog::logger>("imlui", sink);
        lg->set_pattern("%^[%H:%M:%S.%e] [%n] [%l]%$ %v");
        lg->set_level(spdlog::level::debug);
        return lg;
    }();
    return instance;
}

std::shared_ptr<spdlog::logger> logger() {
    std::scoped_lock lock{loggerMutex};
    return loggerSlot();
}

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
    std::shared_ptr<spdlog::logger> previous; // released after the lock: writers may still hold it
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
        auto& slot = loggerSlot();
        std::shared_ptr<spdlog::logger> withFile = slot->clone(slot->name());
        withFile->sinks().push_back(std::move(sink)); // not shared yet: no other thread logs through it
        previous = std::exchange(slot, std::move(withFile));
    } catch (const std::exception& e) { // spdlog::spdlog_ex, out of memory
        write(Level::Error, std::string{"can't add a log file: "} + e.what());
        return false;
    }
    return true;
}

void setLevel(Level level) { logger()->set_level(toSpd(level)); }

void write(Level level, std::string_view msg, std::string_view source) {
    const auto lg = logger();
    if (source.empty()) lg->log(toSpd(level), "{}", msg);
    else lg->log(toSpd(level), "[{}] {}", source, msg);
    if (level >= Level::Warn) lg->flush();
}

} // namespace imlui::log
