// Engine: owns the native window, the Lua state and the frame loop.
//
//  * runs lua/imlui.lua (the declarative DSL) and the user's script,
//    which returns the root element (usually an ApplicationWindow)
//  * renders that tree on demand through the Renderer: after input (for a short
//    settle period), and while animations, timers or scripts request frames
//  * keeps the UI scaled to the monitor DPI times the user zoom
//  * hot-reloads when any .lua file next to the script changes
//  * exposes `imlui.native` to Lua (theme, colors, time, quit, ...)
module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module imlui.app.engine;

import lua;
import imgui;
import lua.stack_guard;
import imlui.log;
import imlui.platform.window;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.theme.theme;
import imlui.widgets;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.native;
import imlui.lua.painter;
import imlui.ui.frame_request;
import imlui.ui.renderer;
import imlui.ui.elements;
import imlui.app.config;

export namespace imlui {

class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Register custom C++ elements here before run().
    Renderer& renderer();

    void requestQuit();
    void requestReload();
    platform::Window& window();

    /// Creates the window, loads the script and runs the frame loop until quit.
    int run();

private:
    using FileSnapshot = std::map<std::filesystem::path, std::filesystem::file_time_type>;

    /// One complete frame. Also called from inside event processing while
    /// the window is being resized.
    void renderFrame();
    /// Applies a pending zoom and re-applies the theme when the effective scale changed.
    void syncScale();
    /// Ctrl + =/+, Ctrl + -, Ctrl + 0 and Ctrl + wheel. Call after beginFrame().
    void handleZoomShortcuts();
    /// How long the loop may sleep before it has to check for changed files.
    [[nodiscard]] double secondsUntilFileCheck() const;
    void renderTree();
    void drawErrors();

    /// Sets m_reload when a watched .lua file changed (checked at most every 400 ms).
    void checkForChanges();
    [[nodiscard]] FileSnapshot scanFiles() const;
    void reloadNow();
    /// Applies the root element's width/height/title to the native window.
    void applyRootWindowProps(lua::State* state, int rootRef);

    EngineConfig m_config;
    platform::Window m_window;
    Renderer m_renderer;
    luaui::NativeHooks m_hooks;
    lua::State* m_state = nullptr;
    int m_rootRef = lua::noRef;
    int m_tickRef = lua::noRef;
    int m_overlayRef = lua::noRef;
    bool m_quit = false;
    bool m_reload = false;
    int m_appliedWidth = 0;
    int m_appliedHeight = 0;
    float m_themeScale = 0.0f;          ///< dpiScale() the ImGui style was last built for
    std::optional<float> m_pendingZoom; ///< applied at the start of the next frame
    bool m_zoomShortcuts = true;
    float m_wheelZoom = 0.0f; ///< Ctrl + wheel notches not yet turned into zoom steps
    FileSnapshot m_snapshot;
    std::chrono::steady_clock::time_point m_lastCheck;
};

} // namespace imlui

module :private;

namespace imlui {

namespace fs = std::filesystem;

namespace {

/// After any input, frames keep coming this long so transitions and hover layers finish.
constexpr double settleSeconds = 0.3;
/// Changed .lua files are looked for this often.
constexpr double fileCheckSeconds = 0.4;
/// While a text field is active: frames for the caret blink.
constexpr double caretBlinkSeconds = 0.1;
/// While minimized, the loop wakes at most this often.
constexpr double minimizedSeconds = 0.1;

void closeState(lua::State* state) {
    if (state) lua::close(state);
}

} // namespace

Engine::Engine(EngineConfig config) : m_config{std::move(config)} {
    registerBuiltinElements(m_renderer);
    m_hooks.quit = [this] { requestQuit(); };
    m_hooks.reload = [this] { requestReload(); };
    m_hooks.setTitle = [this](const std::string& title) { m_window.setTitle(title); };
    m_hooks.zoom = [this] { return m_pendingZoom.value_or(zoom()); };
    m_hooks.setZoom = [this](float z) {
        m_pendingZoom = std::clamp(std::isnan(z) ? 1.0f : z, minZoom, maxZoom);
        requestFrame();
    };
    m_hooks.setZoomShortcuts = [this](bool on) { m_zoomShortcuts = on; };
}

Engine::~Engine() { closeState(m_state); }

Renderer& Engine::renderer() { return m_renderer; }
void Engine::requestQuit() { m_quit = true; }
void Engine::requestReload() { m_reload = true; }
platform::Window& Engine::window() { return m_window; }

int Engine::run() {
    // The window starts hidden: its title and size come from the script.
    platform::Window::Config wc = m_config.window;
    wc.visible = false;
    if (!m_window.create(wc)) {
        log::error("failed to create the window");
        return 1;
    }
    // Keep drawing while the OS blocks the loop (live window resize).
    m_window.setRefreshCallback([this] { renderFrame(); });
    // Moved to a monitor with another DPI: the next frame re-applies the theme.
    m_window.setContentScaleCallback([](float scale) {
        setContentScale(scale);
        requestFramesFor(settleSeconds);
    });
    setContentScale(m_window.contentScale());
    loadFonts(m_config.fontDir);
    loadIcons(m_config.fontDir);
    setTheme(Theme{});
    applyPendingTheme();
    m_themeScale = dpiScale();

    reloadNow();
    m_window.center();
    m_window.show();
    m_snapshot = scanFiles();
    m_lastCheck = std::chrono::steady_clock::now();

    // On-demand rendering: sleep in waitEvents() until input arrives or the next
    // requested frame, Lua timer or file check is due, and render only then.
    setFrameWakeHandler([] { platform::Window::postEmptyEvent(); });
    requestFramesFor(settleSeconds);
    while (!m_window.shouldClose() && !m_quit) {
        checkForChanges();
        if (m_reload) {
            m_reload = false;
            reloadNow();
            requestFramesFor(settleSeconds);
        }
        double wait = std::min(secondsUntilFrame(), secondsUntilFileCheck());
        if (m_window.minimized()) wait = std::max(wait, minimizedSeconds);
        setLoopAwake(false);
        const bool input = m_window.waitEvents(wait);
        setLoopAwake(true);
        if (input) requestFramesFor(settleSeconds);
        if (secondsUntilFrame() <= 0.0) renderFrame();
    }
    setFrameWakeHandler({});
    clearFrameRequests();
    log::info("shutting down");
    closeState(m_state);
    m_state = nullptr;
    m_window.destroy();
    return 0;
}

// ------------------------------------------------------------ frame

void Engine::renderFrame() {
    beginFrameRequests(); // requests made from here on are for the next frame
    syncScale();
    m_window.beginFrame();
    handleZoomShortcuts();
    renderTree();
    drawErrors();
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) requestFrameAfter(caretBlinkSeconds);
    // Held buttons: repeat buttons, drags that pause, press animations.
    if (ImGui::IsAnyMouseDown()) requestFrameAfter(1.0 / 30.0);
    ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    m_window.endFrame(bg.x, bg.y, bg.z);
    if (!m_renderer.windowTitle().empty()) m_window.setTitle(m_renderer.windowTitle());
}

void Engine::syncScale() {
    if (m_pendingZoom) {
        setZoom(*m_pendingZoom);
        m_pendingZoom.reset();
    }
    // applyPendingTheme() builds the style at the current scale, so only a
    // scale change without a new theme needs the current one re-applied.
    const bool applied = applyPendingTheme();
    if (!applied && dpiScale() != m_themeScale) {
        log::info("scale {:.2f} (content scale {:.2f} x zoom {:.2f})", dpiScale(), contentScale(), zoom());
        reapplyTheme();
    }
    m_themeScale = dpiScale();
}

void Engine::handleZoomShortcuts() {
    if (!m_zoomShortcuts) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.KeyAlt) { // Ctrl + Alt is AltGr on many layouts
        m_wheelZoom = 0.0f;
        return;
    }
    const float current = m_pendingZoom.value_or(zoom());
    int steps = 0;
    bool reset = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) ++steps;
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) --steps;
    if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) reset = true;
    // ImGui doesn't scroll on Ctrl + wheel, so the wheel is free for zooming.
    // Touchpads send fractions of a notch: accumulate them.
    m_wheelZoom += io.MouseWheel;
    while (m_wheelZoom >= 1.0f) {
        ++steps;
        m_wheelZoom -= 1.0f;
    }
    while (m_wheelZoom <= -1.0f) {
        --steps;
        m_wheelZoom += 1.0f;
    }
    if (reset)
        m_hooks.setZoom(1.0f);
    else if (steps != 0)
        m_hooks.setZoom(zoomStep(current, steps));
}

double Engine::secondsUntilFileCheck() const {
    if (!m_config.hotReload) return std::numeric_limits<double>::infinity();
    const std::chrono::duration<double> since = std::chrono::steady_clock::now() - m_lastCheck;
    return std::max(0.0, fileCheckSeconds - since.count());
}

void Engine::renderTree() {
    if (!m_state || m_rootRef == lua::noRef) return;
    lua::StackGuard guard{m_state};
    if (m_tickRef != lua::noRef) {
        // ui.__tick runs due timers and returns the seconds until the next one.
        lua::rawgeti(m_state, lua::registryIndex, m_tickRef);
        lua::pushnumber(m_state, ImGui::GetTime());
        if (luaui::safeCall(m_state, 1, 1) && lua::isnumber(m_state, -1)) requestFrameAfter(lua::tonumber(m_state, -1));
        lua::settop(m_state, 0);
    }
    lua::rawgeti(m_state, lua::registryIndex, m_rootRef);
    m_renderer.render(m_state, -1);
    lua::settop(m_state, 0);
    if (m_overlayRef != lua::noRef) {
        // ui.__overlays: snackbars and other app-level overlays
        lua::rawgeti(m_state, lua::registryIndex, m_overlayRef);
        luaui::Node o{m_state, -1};
        m_renderer.renderChildren(o);
    }
}

void Engine::drawErrors() {
    const auto& errs = luaui::errors();
    if (errs.empty()) return;
    const Scheme& s = theme().scheme;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float d = dpiScale();
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 16 * d},
                            ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowSizeConstraints({vp->WorkSize.x * 0.6f, 0}, {vp->WorkSize.x * 0.9f, vp->WorkSize.y * 0.6f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, s.errorContainer.vec4());
    ImGui::PushStyleColor(ImGuiCol_Text, s.onErrorContainer.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12 * d);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16 * d, 16 * d});
    ImGui::Begin("##imlui_errors", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    md::text("Lua error", TextStyle::TitleMedium);
    ImGui::BeginChild("##errlist", {0, 0}, ImGuiChildFlags_AutoResizeY);
    for (const auto& e : errs) {
        md::text(e, TextStyle::BodySmall, std::nullopt, true);
        ImGui::Spacing();
    }
    ImGui::EndChild();
    if (md::button("Dismiss", md::ButtonVariant::Text)) luaui::clearErrors();
    ImGui::SameLine();
    if (md::button("Reload", md::ButtonVariant::Text)) m_reload = true;
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ------------------------------------------------------------ loading

void Engine::checkForChanges() {
    if (!m_config.hotReload) return;
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastCheck < std::chrono::duration<double>(fileCheckSeconds)) return;
    m_lastCheck = now;
    auto snap = scanFiles();
    if (snap != m_snapshot) {
        m_snapshot = std::move(snap);
        m_reload = true;
    }
}

Engine::FileSnapshot Engine::scanFiles() const {
    FileSnapshot out;
    std::vector<fs::path> dirs = m_config.luaPaths;
    dirs.push_back(m_config.script.parent_path());
    std::error_code ec;
    for (const auto& dir : dirs) {
        if (!fs::is_directory(dir, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec) && it->path().extension() == ".lua") out[it->path()] = it->last_write_time(ec);
        }
    }
    return out;
}

void Engine::reloadNow() {
    luaui::clearErrors();
    Theme previous = theme();
    setTheme(Theme{}); // scripts start from the default theme

    lua::State* state = lua::newstate();
    lua::openlibs(state);
    luaui::registerNative(state, &m_hooks);
    luaui::registerPainter(state);

    // package.path: <luaPaths>/?.lua ; <scriptdir>/?.lua ; default
    std::string path;
    for (const auto& p : m_config.luaPaths)
        path += (p / "?.lua").generic_string() + ";" + (p / "?/init.lua").generic_string() + ";";
    path += (m_config.script.parent_path() / "?.lua").generic_string() + ";";
    lua::getglobal(state, "package");
    lua::getfield(state, -1, "path");
    path += std::string{lua::tostring(state, -1)};
    lua::pop(state);
    lua::pushstring(state, path);
    lua::setfield(state, -2, "path");
    lua::pop(state);

    constexpr std::string_view boot = R"(local ui = require("imlui"); return ui.__load(...), ui.__tick, ui.__overlays)";
    bool ok = lua::loadbuffer(state, boot, "=imlui.boot") == lua::ok;
    if (!ok) luaui::reportError(std::string{lua::tostring(state, -1)});
    if (ok) {
        lua::pushstring(state, m_config.script.generic_string());
        ok = luaui::safeCall(state, 1, 3);
    }
    if (ok && !lua::istable(state, -3)) {
        luaui::reportError(m_config.script.generic_string() +
                           ": script must return a root element, e.g. `return ApplicationWindow { ... }`");
        ok = false;
    }
    if (!ok) {
        lua::close(state);
        setTheme(std::move(previous));
        return;
    }
    int overlays = lua::istable(state, -1) ? lua::ref(state) : (lua::pop(state), lua::noRef);
    int tick = lua::ref(state); // pops __tick
    int root = lua::ref(state); // pops root

    applyRootWindowProps(state, root);

    m_zoomShortcuts = true; // a script that doesn't want them turns them off again
    closeState(m_state);
    m_state = state;
    m_rootRef = root;
    m_tickRef = tick;
    m_overlayRef = overlays;
    log::info("loaded {}", m_config.script.generic_string());
}

void Engine::applyRootWindowProps(lua::State* state, int rootRef) {
    // Size is re-applied on hot reload only when the script changed it.
    lua::rawgeti(state, lua::registryIndex, rootRef);
    luaui::Node n{state, -1};
    int w = static_cast<int>(n.number("width", 0)), h = static_cast<int>(n.number("height", 0));
    if (w > 0 && h > 0 && (w != m_appliedWidth || h != m_appliedHeight)) {
        m_appliedWidth = w;
        m_appliedHeight = h;
        // width/height are dp: scaled by the monitor content scale (not by the user zoom).
        m_window.setScaledSize(w, h);
    }
    std::string title = n.string("title", "");
    if (!title.empty()) m_window.setTitle(title);
    lua::pop(state);
}

} // namespace imlui
