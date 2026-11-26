// HostSettings: what a script sets on its host through imlui.native (the window title, the zoom, the zoom shortcuts,
// the window size), and the reload transaction over them and over the global settings a script changes (the theme,
// culling): a script that fails while it loads leaves all of them as they were before the attempt.
//
// The zoom and the window size are deferred: the owner (the Engine) takes them between frames. The title goes to the
// Renderer, which the root ApplicationWindow's `title` prop overrides every frame.
// Main thread only.
export module imlui.app.host_settings;

import std;

import imlui.lua.native;
import imlui.theme.dpi;
import imlui.theme.theme;
import imlui.ui.culling;
import imlui.ui.frame_request;
import imlui.ui.renderer;

export namespace imlui {

class HostSettings {
public:
    /// `renderer` holds the window title (ui.setTitle) and must outlive these settings.
    explicit HostSettings(Renderer& renderer);

    /// Points the title, zoom, zoom-shortcut and window-size hooks at these settings, which must outlive every Lua
    /// state the hooks are registered in. The other hooks (quit, reload) are left alone.
    void install(luaui::NativeHooks& hooks);

    /// Runs `attempt` (it creates a Lua state and runs the script in it; true when that succeeded) as one
    /// transaction: the theme starts from Theme{} and the zoom shortcuts on, before the script runs, so a script can
    /// change them at its top level. When `attempt` returns false, the theme, culling, window title, zoom shortcuts
    /// and pending zoom and window size are put back as they were before. Returns what `attempt` returned.
    bool reload(const std::function<bool()>& attempt);

    /// Whether the built-in zoom shortcuts are on (ui.setZoomShortcuts).
    [[nodiscard]] bool zoomShortcuts() const;
    /// The pending zoom, else the current one.
    [[nodiscard]] float zoom() const;
    /// Schedules a zoom change for the next frame (clamped to minZoom..maxZoom, NaN: 1) and requests a frame.
    void setZoom(float z);
    /// The zoom set since the last call (nullopt: none), cleared.
    std::optional<float> takePendingZoom();
    /// The window size in dp requested since the last call (nullopt: none), cleared.
    std::optional<std::pair<int, int>> takePendingWindowSize();

private:
    Renderer& m_renderer;
    std::optional<float> m_pendingZoom;
    std::optional<std::pair<int, int>> m_pendingWindowSize;
    bool m_zoomShortcuts = true;
};

} // namespace imlui

module :private;

namespace imlui {

HostSettings::HostSettings(Renderer& renderer) : m_renderer{renderer} {}

void HostSettings::install(luaui::NativeHooks& hooks) {
    hooks.setTitle = [this](const std::string& title) { m_renderer.setWindowTitle(title); };
    hooks.zoom = [this] { return zoom(); };
    hooks.setZoom = [this](float z) { setZoom(z); };
    hooks.setZoomShortcuts = [this](bool on) { m_zoomShortcuts = on; };
    // Deferred: resizing from inside a frame (a click handler) would change the viewport mid-frame.
    hooks.setWindowSize = [this](int w, int h) {
        m_pendingWindowSize = std::pair{w, h};
        requestFrame();
    };
}

bool HostSettings::reload(const std::function<bool()>& attempt) {
    Theme theme = imlui::theme();
    const bool culling = cull::enabled();
    std::string title = m_renderer.windowTitle();
    const bool zoomShortcuts = m_zoomShortcuts;
    const std::optional<float> pendingZoom = m_pendingZoom;
    const std::optional<std::pair<int, int>> pendingWindowSize = m_pendingWindowSize;

    // Defaults before the script runs: what its top level sets wins. (Culling is reset by registerNative.)
    setTheme(Theme{});
    m_zoomShortcuts = true;
    if (attempt()) return true;

    setTheme(std::move(theme));
    cull::setEnabled(culling);
    m_renderer.setWindowTitle(std::move(title));
    m_zoomShortcuts = zoomShortcuts;
    m_pendingZoom = pendingZoom;
    m_pendingWindowSize = pendingWindowSize;
    return false;
}

bool HostSettings::zoomShortcuts() const { return m_zoomShortcuts; }

float HostSettings::zoom() const { return m_pendingZoom.value_or(imlui::zoom()); }

void HostSettings::setZoom(float z) {
    m_pendingZoom = std::clamp(std::isnan(z) ? 1.0f : z, minZoom, maxZoom);
    requestFrame();
}

std::optional<float> HostSettings::takePendingZoom() { return std::exchange(m_pendingZoom, std::nullopt); }

std::optional<std::pair<int, int>> HostSettings::takePendingWindowSize() {
    return std::exchange(m_pendingWindowSize, std::nullopt);
}

} // namespace imlui
