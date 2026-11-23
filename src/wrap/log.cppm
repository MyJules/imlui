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

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module imlui.log;

export namespace imlui::log {

enum class Level { Trace, Debug, Info, Warn, Error, Critical };

namespace detail {
inline std::shared_ptr<spdlog::logger>& logger() {
    static std::shared_ptr<spdlog::logger> l = [] {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        auto lg = std::make_shared<spdlog::logger>("imlui", sink);
        lg->set_pattern("%^[%H:%M:%S.%e] [%n] [%l]%$ %v");
        lg->set_level(spdlog::level::debug);
        return lg;
    }();
    return l;
}
inline spdlog::level::level_enum toSpd(Level l) {
    switch (l) {
    case Level::Trace: return spdlog::level::trace;
    case Level::Debug: return spdlog::level::debug;
    case Level::Info: return spdlog::level::info;
    case Level::Warn: return spdlog::level::warn;
    case Level::Error: return spdlog::level::err;
    case Level::Critical: return spdlog::level::critical;
    }
    return spdlog::level::info;
}
} // namespace detail

/// Also log into `path` (appending). Call once at startup.
void addFileSink(const std::string& path) {
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, false);
    sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
    detail::logger()->sinks().push_back(std::move(sink));
}

void setLevel(Level l) { detail::logger()->set_level(detail::toSpd(l)); }

/// Logs an already formatted message; `source` tags it, e.g. "lua".
void write(Level l, std::string_view msg, std::string_view source = {}) {
    auto& lg = detail::logger();
    if (source.empty()) lg->log(detail::toSpd(l), "{}", msg);
    else lg->log(detail::toSpd(l), "[{}] {}", source, msg);
    if (l >= Level::Warn) lg->flush();
}

template <class... A> void trace(std::format_string<A...> f, A&&... a) { write(Level::Trace, std::format(f, std::forward<A>(a)...)); }
template <class... A> void debug(std::format_string<A...> f, A&&... a) { write(Level::Debug, std::format(f, std::forward<A>(a)...)); }
template <class... A> void info(std::format_string<A...> f, A&&... a) { write(Level::Info, std::format(f, std::forward<A>(a)...)); }
template <class... A> void warn(std::format_string<A...> f, A&&... a) { write(Level::Warn, std::format(f, std::forward<A>(a)...)); }
template <class... A> void error(std::format_string<A...> f, A&&... a) { write(Level::Error, std::format(f, std::forward<A>(a)...)); }

} // namespace imlui::log
