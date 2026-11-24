// Module wrapper around spdlog.
//
//   import imlui.log;
//   imlui::log::info("loaded {} in {} ms", path, ms);
//
// Formatting uses std::format on the caller side; spdlog only receives the
// finished string, so no spdlog/fmt templates cross the module boundary.
module;

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

export module imlui.log;

import std;

export namespace imlui::log {

enum class Level { Trace, Debug, Info, Warn, Error, Critical };

/// Also log into `path` (appending). Call once at startup.
void addFileSink(const std::string& path);

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

std::shared_ptr<spdlog::logger>& logger() {
    static std::shared_ptr<spdlog::logger> instance = [] {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        auto lg = std::make_shared<spdlog::logger>("imlui", sink);
        lg->set_pattern("%^[%H:%M:%S.%e] [%n] [%l]%$ %v");
        lg->set_level(spdlog::level::debug);
        return lg;
    }();
    return instance;
}

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

void addFileSink(const std::string& path) {
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, false);
    sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
    logger()->sinks().push_back(std::move(sink));
}

void setLevel(Level level) { logger()->set_level(toSpd(level)); }

void write(Level level, std::string_view msg, std::string_view source) {
    auto& lg = logger();
    if (source.empty()) lg->log(toSpd(level), "{}", msg);
    else lg->log(toSpd(level), "[{}] {}", source, msg);
    if (level >= Level::Warn) lg->flush();
}

} // namespace imlui::log
