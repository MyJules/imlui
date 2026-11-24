// SystemInfo: an example C++ singleton for imlui_custom_types, exposed to Lua as `backend`.
//
// Read-only properties `time` ("HH:MM:SS"), `frame` (rendered frame count) and `load` (a fake value
// that drifts over time); read/write `paused`; signal `onTick(count)`; method `tick()`. A worker
// thread wakes up twice a second and hands tick() to the UI thread with Engine::post(), the
// thread-safe way in: Lua, notify() and emit() are main-thread only.
module;

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

export module example.system_info;

import imgui;
import imlui;

export namespace example {

class SystemInfo final : public imlui::Object {
public:
    static void declare(imlui::TypeBuilder<SystemInfo>& type);

    SystemInfo();
    ~SystemInfo() override;
    SystemInfo(const SystemInfo&) = delete;
    SystemInfo& operator=(const SystemInfo&) = delete;
    SystemInfo(SystemInfo&&) = delete;
    SystemInfo& operator=(SystemInfo&&) = delete;

    /// Starts the worker thread that posts tick() to `engine` every 500 ms.
    void start(imlui::Engine& engine);

    [[nodiscard]] const std::string& time() const;
    [[nodiscard]] int frame() const;
    [[nodiscard]] double load() const;
    [[nodiscard]] bool paused() const;
    void setPaused(bool paused);

    /// Updates time and load, notifies both and emits onTick(count). Main thread.
    void tick();

private:
    std::string m_time;
    double m_load = 0.0;
    bool m_paused = false;
    int m_ticks = 0;
    std::chrono::steady_clock::time_point m_start;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::jthread m_worker; ///< last member: stopped and joined first, while the others still exist
};

} // namespace example

module :private;

namespace example {

void SystemInfo::declare(imlui::TypeBuilder<SystemInfo>& type) {
    type.property("time", &SystemInfo::time)
        .property("frame", &SystemInfo::frame)
        .property("load", &SystemInfo::load)
        .property("paused", &SystemInfo::paused, &SystemInfo::setPaused)
        .signal("onTick")
        .method("tick", &SystemInfo::tick);
}

SystemInfo::SystemInfo() : m_start{std::chrono::steady_clock::now()} { tick(); }

SystemInfo::~SystemInfo() {
    m_worker.request_stop(); // wakes the wait below through its stop_token
    if (m_worker.joinable()) m_worker.join();
}

void SystemInfo::start(imlui::Engine& engine) {
    m_worker = std::jthread{[this, &engine](const std::stop_token& stop) {
        std::unique_lock lock{m_mutex};
        while (!stop.stop_requested()) {
            m_wake.wait_for(lock, stop, std::chrono::milliseconds{500}, [] { return false; });
            if (stop.stop_requested()) break;
            engine.post([this] { tick(); });
        }
    }};
}

const std::string& SystemInfo::time() const { return m_time; }
int SystemInfo::frame() const { return ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0; }
double SystemInfo::load() const { return m_load; }
bool SystemInfo::paused() const { return m_paused; }

void SystemInfo::setPaused(bool paused) {
    if (paused == m_paused) return;
    m_paused = paused;
    notify("paused");
}

void SystemInfo::tick() {
    if (m_paused) return;
    using namespace std::chrono;
    const auto now = floor<seconds>(system_clock::now());
    m_time = std::format("{:%H:%M:%S}", zoned_time{current_zone(), now});
    const double t = duration<double>(steady_clock::now() - m_start).count();
    m_load = 0.5 + 0.3 * std::sin(t * 0.7) + 0.15 * std::sin(t * 2.3 + 1.0);
    ++m_ticks;
    notify("time");
    notify("load");
    emit("onTick", m_ticks);
}

} // namespace example
