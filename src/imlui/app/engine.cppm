// Engine: owns the native window, the Lua state and the frame loop.
//
//  * loads Lua and fonts through Resources: imlui:/imlui.lua (the declarative DSL) and the entry script
//    (app:/main.lua), which returns the root element (usually an ApplicationWindow)
//  * renders that tree on demand through the Renderer: after input (for a short
//    settle period), and while animations, timers or scripts request frames
//  * keeps the UI scaled to the monitor DPI times the user zoom
//  * hot-reloads when a .lua resource in a mutable mount (a directory) changes; a changed image is decoded again
//    without a reload. The files are scanned on the background pool every 400 ms, and not at all when no mount can
//    change (only embedded files and .pak archives)
//  * exposes `imlui.native` to Lua (theme, colors, time, quit, ...)
//  * owns the TypeRegistry: C++ element types and singletons registered with registerType<T>() and
//    registerSingleton(), installed into every new Lua state
//  * writes the Lua language-server definitions of those types instead of running, when EngineConfig::luaDefinitions
//    is set (`--lua-defs <dir>`)
export module imlui.app.engine;

import std;

import lua;
import imgui;
import lua.stack_guard;
import imlui.log;
import imlui.platform.window;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.font_library;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.theme.theme;
import imlui.widgets;
import imlui.skin;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.native;
import imlui.lua.painter;
import imlui.lua.protected_render;
import imlui.lua.resources;
import imlui.resources;
import imlui.resources.mount;
import imlui.resources.name;
import imlui.image.image_cache;
import imlui.ui.background;
import imlui.ui.clock;
import imlui.ui.frame_request;
import imlui.ui.layout_box;
import imlui.ui.main_thread;
import imlui.ui.main_timers;
import imlui.ui.renderer;
import imlui.ui.elements.builtin;
import imlui.types.script_object;
import imlui.types.element;
import imlui.types.type_info;
import imlui.types.registry;
import imlui.types.lua_definitions;
import imlui.app.config;
import imlui.app.host_settings;
import imlui.app.file_snapshot;

export namespace imlui {

class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Registers element type T (derived from imlui::Element, with `static void declare(TypeBuilder<T>&)`)
    /// as `name`, usable from Lua like a built-in: `name { prop = ..., onSignal = ... }`. Call before run().
    template <class T>
        requires std::derived_from<T, Element> && std::default_initializable<T>
    TypeInfo& registerType(std::string name) {
        return m_types.registerType<T>(std::move(name));
    }

    /// Exposes `object` (derived from imlui::ScriptObject) to Lua as the global `name`. Not owned: it must
    /// outlive run() (declare it before the Engine, or use the shared_ptr overload). Call before run().
    template <class T>
        requires std::derived_from<T, ScriptObject>
    TypeInfo& registerSingleton(std::string name, T& object) {
        return m_types.registerSingleton<T>(std::move(name), object);
    }

    /// Exposes `object` to Lua as the global `name` and keeps it alive as long as the Engine, so it
    /// outlives run() and every Lua state. Call before run().
    template <class T>
        requires std::derived_from<T, ScriptObject>
    TypeInfo& registerSingleton(std::string name, std::shared_ptr<T> object) {
        return m_types.registerSingleton<T>(std::move(name), std::move(object));
    }

    /// The registered types and singletons.
    TypeRegistry& types();

    /// The resources the engine loads scripts and fonts from (EngineConfig::resources). Mount more before run().
    Resources& resources();

    /// Runs `fn` on the main thread at the next loop iteration and wakes the loop. The one Engine call
    /// that is safe from any thread: hand results of worker threads to the UI through it. The same as the free
    /// imlui::post (module imlui.ui.main_thread), which code without an Engine& uses; to post to an element or
    /// singleton that may be destroyed meanwhile, use its ScriptObject::handle().
    void post(std::function<void()> fn);

    void requestQuit();
    void requestReload();
    platform::Window& window();

    /// Writes the Lua language-server definitions (LuaLS `---@meta` files) of the registered types and singletons into
    /// `dir`, created when missing: `imlui.lua` (the DSL, from luaPreludeDefinitions in the resources) and
    /// `types.lua` (luaTypeDefinitions). Logs and returns false when it fails. Call after registering everything.
    bool writeLuaDefinitions(const std::filesystem::path& dir);

    /// Creates the window, loads the script and runs the frame loop until quit. With EngineConfig::luaDefinitions set,
    /// writes the Lua definitions there instead (writeLuaDefinitions) and returns 0, or 1 when that failed, without
    /// creating a window.
    int run();

private:
    /// run() with the window created: loads the script and runs the frame loop until quit.
    void runWindow(const platform::Window::Config& wc);
    /// Undoes what runWindow() set up (wake handlers, timers, posts, the Lua state, the window, the background pool).
    /// run() calls it however the loop leaves; a second call does nothing harmful.
    void shutdown();
    /// One complete frame. Also called from inside event processing while
    /// the window is being resized.
    void renderFrame();
    /// Applies a pending zoom and re-applies the theme when the effective scale changed.
    void syncScale();
    /// Adds the fonts the current theme declares to the resources watched for hot reload.
    void watchThemeFonts();
    /// Ctrl + =/+, Ctrl + -, Ctrl + 0 and Ctrl + wheel. Call after beginFrame().
    void handleZoomShortcuts();
    /// How long the loop may sleep before it has to check for changed files.
    [[nodiscard]] double secondsUntilFileCheck() const;
    void renderTree();
    void drawErrors();

    /// Starts a scan of the watched files on the background pool every 400 ms, and takes the result of the last one:
    /// drops the changed images and sets m_reload when a watched resource changed (see resourcesChanged).
    void checkForChanges();
    /// Takes m_snapshot (and m_snapshotTime) now, on the main thread (the first one, before the loop).
    void takeSnapshot();
    void reloadNow();
    /// Applies the root element's width/height/title to the native window.
    void applyRootWindowProps(lua::State* state, int rootRef);
    /// Applies a size requested with ui.setWindowSize().
    void applyPendingWindowSize();
    void syncTitleBar();

    EngineConfig m_config;
    platform::Window m_window;
    TypeRegistry m_types;
    Renderer m_renderer{m_types};
    ImageCache m_images{m_config.resources}; ///< what Image elements draw; outlives the ImGui context (see ~ImageCache)
    HostSettings m_settings{m_renderer};     ///< title, zoom, zoom shortcuts, window size: what m_hooks set
    luaui::NativeHooks m_hooks;
    lua::State* m_state = nullptr;
    int m_rootRef = lua::noRef;
    int m_tickRef = lua::noRef;
    int m_overlayRef = lua::noRef;
    bool m_quit = false;
    int m_framesRendered = 0; ///< counted only for EngineConfig::exitAfterFrames
    bool m_reload = false;
    int m_appliedWidth = 0;
    int m_appliedHeight = 0;
    float m_themeScale = 0.0f; ///< dpiScale() the ImGui style was last built for
    float m_wheelZoom = 0.0f;  ///< Ctrl + wheel notches not yet turned into zoom steps
    FileSnapshot m_snapshot;
    ResourceTime m_snapshotTime; ///< when the scan of m_snapshot started
    struct ScanInbox;
    /// Where the background scan leaves its result; shared with it (it may finish after the Engine is gone).
    std::shared_ptr<ScanInbox> m_scanInbox;
    bool m_scanning = false; ///< a background scan runs: the loop waits for its post, not for the next check
    bool m_watching = false; ///< hot reload is on and some mount's files may change (Resources::mayChange)
    /// The resources the current Lua state read or loaded, and the current theme's fonts: watched for hot reload.
    /// Each load starts a set of its own (its state holds a pointer to it, see registerResources), which replaces this
    /// one when the load succeeds: a file the script no longer reads stops being watched. Never null.
    std::unique_ptr<std::set<std::string>> m_loaded = std::make_unique<std::set<std::string>>();
    std::chrono::steady_clock::time_point m_lastCheck;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// After any input, frames keep coming this long so transitions and hover layers finish.
constexpr double settleSeconds = 0.3;
/// Changed .lua files are looked for this often.
constexpr double fileCheckSeconds = 0.4;
/// While a text field is active: frames for the caret blink.
constexpr double caretBlinkSeconds = 0.1;
/// While minimized, the loop wakes at most this often.
constexpr double minimizedSeconds = 0.1;
/// With vsync off, the most frames per second the loop renders (vsync paces it otherwise): an animation asks for a
/// frame every frame, and nothing else would stop it from rendering as fast as the CPU and GPU go.
constexpr double maxUnsyncedFrameRate = 240.0;

/// The largest root window width or height, in dp: ui.setWindowSize's limit, bigger than any screen and far from
/// overflowing an int once scaled to pixels.
constexpr double maxWindowExtent = 100000.0;

/// A root window width/height from the script as a whole number of dp: 0 (no size) unless finite and positive,
/// at most maxWindowExtent.
int windowExtent(double v) {
    if (!std::isfinite(v) || v <= 0.0) return 0;
    return static_cast<int>(std::min(v, maxWindowExtent));
}

void closeState(lua::State* state) {
    if (state) lua::close(state);
}

/// The times of every .lua file in the mounted prefixes and of every name in `watched` (what Lua has loaded or read,
/// the theme's fonts). Any thread: Resources is safe to read from any.
FileSnapshot scanFiles(const Resources& res, const std::set<std::string>& watched) {
    FileSnapshot out;
    for (const auto& prefix : res.prefixes())
        for (auto& name : res.listRecursive(prefix + ":/"))
            if (name.ends_with(".lua")) out.emplace(std::move(name), std::nullopt);
    for (const auto& name : watched)
        out.emplace(name, std::nullopt);
    for (auto& [name, time] : out)
        time = res.modified(name);
    return out;
}

} // namespace

/// One background scan's result: the watched files' times, and the cached images' (ImageCache::refresh).
struct Engine::ScanInbox {
    struct Result {
        FileSnapshot files;
        std::map<std::string, std::optional<ResourceTime>> images;
        ResourceTime takenAt; ///< when the scan started
        bool ok = false;      ///< false: the scan failed (out of memory) or was dropped; nothing to compare
    };
    std::mutex mutex;
    std::optional<Result> result; ///< guarded by `mutex`: set when the scan ended
};

Engine::Engine(EngineConfig config) : m_config{std::move(config)}, m_scanInbox{std::make_shared<ScanInbox>()} {
    registerBuiltinTypes(m_types);
    m_renderer.setImages(&m_images);
    m_images.setAsync(true); // decodes on the background pool: a large image no longer stalls a frame
    m_hooks.quit = [this] { requestQuit(); };
    m_hooks.reload = [this] { requestReload(); };
    m_settings.install(m_hooks); // title, zoom, zoom shortcuts, window size
}

Engine::~Engine() { closeState(m_state); }

TypeRegistry& Engine::types() { return m_types; }
Resources& Engine::resources() { return m_config.resources; }

void Engine::post(std::function<void()> fn) { imlui::post(std::move(fn)); }

void Engine::requestQuit() { m_quit = true; }
void Engine::requestReload() { m_reload = true; }
platform::Window& Engine::window() { return m_window; }

bool Engine::writeLuaDefinitions(const std::filesystem::path& dir) {
    const std::optional<std::string> prelude = m_config.resources.readText(luaPreludeDefinitions);
    if (!prelude) {
        log::error("Lua definitions: {} isn't in the resources", luaPreludeDefinitions);
        return false;
    }
    if (const std::string error = imlui::writeLuaDefinitions(m_types, dir, *prelude); !error.empty()) {
        log::error("Lua definitions: {}", error);
        return false;
    }
    std::error_code ec;
    const std::u8string where = std::filesystem::absolute(dir, ec).generic_u8string();
    log::info("Lua definitions written to {}", std::string{where.begin(), where.end()});
    return true;
}

int Engine::run() {
    // A build step or CI job asked for the editor definitions: nothing to show.
    if (!m_config.luaDefinitions.empty()) return writeLuaDefinitions(m_config.luaDefinitions) ? 0 : 1;
    // The window starts hidden: its title and size come from the script.
    platform::Window::Config wc = m_config.window;
    wc.visible = false;
    if (!m_window.create(wc)) {
        log::error("failed to create the window");
        return 1;
    }
    {
        // However the loop leaves (an exception too), shutdown() tears down what it set up: the window (and with it the
        // ImGui context and the backend's textures) before ~Engine destroys the image cache, whose textures the
        // backend's shutdown still walks; the wake handlers before the window goes.
        struct Shutdown {
            Engine& engine;
            ~Shutdown() { engine.shutdown(); }
        } shutdown{*this};
        runWindow(wc);
    }
    // A smoke run (exitAfterFrames) fails when the script reported an error.
    if (m_config.exitAfterFrames > 0 && !luaui::errors().empty()) {
        log::error("exiting after {} frames with script errors", m_framesRendered);
        return 1;
    }
    return 0;
}

void Engine::runWindow(const platform::Window::Config& wc) {
    // Keep drawing while the OS blocks the loop (live window resize, a modal loop on Windows): the refresh callback
    // runs only inside waitEvents(), outside any frame, so the posted functions and due C++ timers run first, as the
    // loop would (they would stall for as long as the drag lasts otherwise).
    m_window.setRefreshCallback([this] {
        runPosted();
        runDueMainTimers();
        renderFrame();
    });
    // Moved to a monitor with another DPI: the next frame re-applies the theme.
    m_window.setContentScaleCallback([](float scale) {
        setContentScale(scale);
        requestFramesFor(settleSeconds);
    });
    setContentScale(m_window.contentScale());
    for (const auto& line : m_config.resources.describe())
        log::info("mount {}", line);
    loadFonts(m_config.resources);
    loadIcons(m_config.resources);
    setTheme(Theme{});
    applyPendingTheme();
    m_themeScale = dpiScale();

    reloadNow();
    applyPendingWindowSize(); // a script that calls setWindowSize() while loading wins over its root size
    m_window.center();
    m_window.keepInWorkArea();
    // The script's theme now, so the title bar has its colors before the window first shows.
    if (applyPendingTheme()) watchThemeFonts();
    syncTitleBar();
    m_window.show();
    // Embedded files and .pak archives never change: with nothing else mounted, there is nothing to poll for.
    m_watching = m_config.hotReload && m_config.resources.mayChange();
    if (m_watching) takeSnapshot();
    m_lastCheck = std::chrono::steady_clock::now();

    // On-demand rendering: sleep in waitEvents() until input arrives or the next
    // requested frame, Lua timer, C++ main timer or file check is due, and render only then.
    setFrameWakeHandler([] { platform::Window::postEmptyEvent(); });
    setMinFrameInterval(wc.vsync ? 0.0 : 1.0 / maxUnsyncedFrameRate);
    requestFramesFor(settleSeconds);
    // From here on a post() from any thread wakes the loop. Functions posted before run() are run below, first.
    setPostWakeHandler([] { platform::Window::postEmptyEvent(); });
    while (!m_window.shouldClose() && !m_quit) {
        runPosted();
        runDueMainTimers();
        checkForChanges();
        if (m_reload) {
            m_reload = false;
            reloadNow();
            requestFramesFor(settleSeconds);
        }
        double wait = std::min(secondsUntilFrame(), secondsUntilFileCheck());
        if (m_window.minimized()) wait = std::max(wait, minimizedSeconds);
        wait = std::min(wait, secondsUntilMainTimer()); // C++ timers fire on time, minimized too: they don't render
        setLoopAwake(false);
        const bool input = m_window.waitEvents(wait);
        setLoopAwake(true);
        if (input) requestFramesFor(settleSeconds);
        runPosted();        // may notify() and so request a frame
        runDueMainTimers(); // likewise (a delay() settling)
        if (secondsUntilFrame() <= 0.0) {
            renderFrame();
            if (m_config.exitAfterFrames > 0 && ++m_framesRendered >= m_config.exitAfterFrames) m_quit = true;
        }
    }
}

void Engine::shutdown() {
    setPostWakeHandler({}); // before the window goes: waits for a worker inside the handler
    // Still waiting: dropped, like the posts (a capture may post). Before the state closes: a dropped delay cancels
    // itself, which may resume a coroutine in it.
    clearMainTimers();
    clearPosted(); // still queued: dropped (they may refer to the Lua state closed below)
    setFrameWakeHandler({});
    setMinFrameInterval(0.0);
    log::info("shutting down");
    closeState(m_state);
    m_state = nullptr;
    // What the dropped timers resumed (and the state's finalizers) may have added timers, posts and frame requests:
    // dropped too, or the next run() (tests run several) would start with them.
    clearMainTimers();
    clearPosted();
    clearFrameRequests();
    m_window.setRefreshCallback({});
    m_window.setContentScaleCallback({});
    m_window.destroy();
    clearFontLibrary();
    // The background pool: tokens stopped, queued tasks dropped, workers joined (or detached after a second). After
    // the window went, so a slow task doesn't keep it on screen; after the state closed, so the elements' own tasks
    // were already stopped by their deaths. What they posted meanwhile is dropped (their objects are dead or unused).
    shutdownBackground();
    clearPosted();
    clearMainTimers();
    // A scan still in flight (or dropped by the shutdown) is forgotten: a new inbox for the next run().
    m_scanning = false;
    m_scanInbox = std::make_shared<ScanInbox>();
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
    syncTitleBar();
    m_window.endFrame(bg.x, bg.y, bg.z);
    m_images.collect(); // textures the backend destroyed while drawing this frame
    if (!m_renderer.windowTitle().empty()) m_window.setTitle(m_renderer.windowTitle());
    applyPendingWindowSize();
}

void Engine::syncTitleBar() {
    // The OS title bar follows the theme: its window background and text colors (any preset, light or dark).
    const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    const ImVec4 fg = ImGui::GetStyle().Colors[ImGuiCol_Text];
    m_window.setTitleBarColors({bg.x, bg.y, bg.z}, {fg.x, fg.y, fg.z});
}

void Engine::applyPendingWindowSize() {
    const std::optional<std::pair<int, int>> size = m_settings.takePendingWindowSize();
    if (!size) return;
    const auto [w, h] = *size;
    // dp, scaled by the monitor content scale like the root window's width/height.
    m_window.setScaledSize(w, h);
    m_window.keepInWorkArea(); // a "phone preview" toggle must not push the window off-screen
    requestFramesFor(settleSeconds);
}

void Engine::syncScale() {
    if (const std::optional<float> z = m_settings.takePendingZoom()) setZoom(*z);
    // applyPendingTheme() builds the style at the current scale, so only a
    // scale change without a new theme needs the current one re-applied.
    const bool applied = applyPendingTheme();
    if (applied) watchThemeFonts();
    if (!applied && dpiScale() != m_themeScale) {
        log::info("scale {:.2f} (content scale {:.2f} x zoom {:.2f})", dpiScale(), contentScale(), zoom());
        reapplyTheme();
    }
    m_themeScale = dpiScale();
}

void Engine::watchThemeFonts() {
    const FontSet& fonts = theme().fonts;
    auto watch = [&](const std::string& name) {
        if (const auto canonical = normalizeName(name)) m_loaded->insert(*canonical);
    };
    watch(fonts.regular);
    watch(fonts.medium);
    for (const auto& name : fonts.fallback)
        watch(name);
    for (const auto& [key, name] : fonts.named)
        watch(name);
}

void Engine::handleZoomShortcuts() {
    if (!m_settings.zoomShortcuts()) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.KeyAlt) { // Ctrl + Alt is AltGr on many layouts
        m_wheelZoom = 0.0f;
        return;
    }
    const float current = m_settings.zoom();
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
        m_settings.setZoom(1.0f);
    else if (steps != 0)
        m_settings.setZoom(zoomStep(current, steps));
}

double Engine::secondsUntilFileCheck() const {
    // A scan in flight posts when it is done, which wakes the loop.
    if (!m_watching || m_scanning) return std::numeric_limits<double>::infinity();
    const std::chrono::duration<double> since = std::chrono::steady_clock::now() - m_lastCheck;
    return std::max(0.0, fileCheckSeconds - since.count());
}

void Engine::renderTree() {
    if (!m_state || m_rootRef == lua::noRef) return;
    lua::StackGuard guard{m_state};
    if (m_tickRef != lua::noRef) {
        // ui.__tick runs due timers and returns the seconds until the next one.
        lua::rawgeti(m_state, lua::registryIndex, m_tickRef);
        lua::pushnumber(m_state, clockTime());
        if (luaui::safeCall(m_state, 1, 1) && lua::isnumber(m_state, -1)) requestFrameAfter(lua::tonumber(m_state, -1));
        lua::settop(m_state, 0);
    }
    // Protected: a Lua error that escapes an unprotected call while rendering is reported, not a panic (abort).
    lua::rawgeti(m_state, lua::registryIndex, m_rootRef);
    luaui::renderProtected(m_state, 1, [this](lua::State* state) { m_renderer.render(state, 1); });
    lua::settop(m_state, 0);
    if (m_overlayRef != lua::noRef) {
        // ui.__overlays: snackbars and other app-level overlays
        lua::rawgeti(m_state, lua::registryIndex, m_overlayRef);
        luaui::renderProtected(m_state, 1,
                               [this](lua::State* state) { m_renderer.renderChildren(luaui::Node{state, 1}); });
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
    // The window fits its content, 60-90% of the viewport wide: the list takes the minimum, not the window's
    // current width (which would keep the window as wide as it ever was).
    layout::pushBox(layout::shrinkToFitBox(vp->WorkSize.x * 0.6f - 32 * d, vp->WorkSize.x * 0.9f - 32 * d));
    md::text("Lua error", TextStyle::TitleMedium);
    ImGui::BeginChild("##errlist", {layout::availableWidth(), 0}, ImGuiChildFlags_AutoResizeY);
    for (const auto& e : errs) {
        md::text(e, TextStyle::BodySmall, std::nullopt, true);
        ImGui::Spacing();
    }
    ImGui::EndChild();
    if (skin().button("Dismiss", md::ButtonVariant::Text, {0, 0}, {})) luaui::clearErrors();
    ImGui::SameLine();
    if (skin().button("Reload", md::ButtonVariant::Text, {0, 0}, {})) m_reload = true;
    layout::popBox();
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ------------------------------------------------------------ loading

void Engine::checkForChanges() {
    if (!m_watching) return;
    if (m_scanning) {
        std::optional<ScanInbox::Result> result;
        {
            std::scoped_lock lock{m_scanInbox->mutex};
            result.swap(m_scanInbox->result);
        }
        if (!result) return; // still running
        m_scanning = false;
        m_lastCheck = std::chrono::steady_clock::now(); // the next scan 400 ms after this one ended
        if (!result->ok) return;
        // A changed image is decoded again at its next use; no reload needed.
        if (m_images.refresh(result->images) > 0) requestFramesFor(settleSeconds);
        if (resourcesChanged(m_snapshot, result->files, *m_loaded, m_snapshotTime)) m_reload = true;
        m_snapshot = std::move(result->files);
        m_snapshotTime = result->takenAt;
        return;
    }
    if (std::chrono::steady_clock::now() - m_lastCheck < std::chrono::duration<double>(fileCheckSeconds)) return;
    // Off the main thread: listing a directory mount walks its folders and checks every file against its selection,
    // and timing every watched file is a stat each. The scan works on copies (the mounts are shared and safe from any
    // thread) and leaves its result in the inbox; its post wakes the loop, whose next checkForChanges() takes it.
    m_scanning = true;
    runInBackground([inbox = m_scanInbox, res = m_config.resources, watched = *m_loaded,
                     images = m_images.names()](std::stop_token stop) {
        ScanInbox::Result result;
        result.takenAt = ResourceTime::clock::now(); // before the scan: an edit during it counts as after
        try {
            if (!stop.stop_requested()) {
                result.files = scanFiles(res, watched);
                for (const auto& name : images)
                    result.images.emplace(name, res.modified(name));
                result.ok = true;
            }
        } catch (const std::exception& e) { // out of memory: no result this time, the next scan tries again
            log::error("hot reload: scanning the files failed: {}", e.what());
        }
        {
            std::scoped_lock lock{inbox->mutex};
            inbox->result = std::move(result);
        }
        imlui::post([] {}); // wakes the loop (the free function: Engine::post would need `this`)
    });
}

void Engine::takeSnapshot() {
    m_snapshotTime = ResourceTime::clock::now(); // before the scan: an edit during it counts as after
    m_snapshot = scanFiles(m_config.resources, *m_loaded);
}

void Engine::reloadNow() {
    luaui::clearErrors();
    lua::State* state = nullptr;
    int root = lua::noRef;
    int tick = lua::noRef;
    int overlays = lua::noRef;
    // What this load reads: a set of its own, so the names only the previous script read drop out (see m_loaded).
    auto loadedNames = std::make_unique<std::set<std::string>>();
    // A transaction over the host settings: the script starts from the default theme and zoom shortcuts, and when
    // it fails, what it set while loading (the theme, the title, the zoom, the window size, ...) is undone.
    const bool ok = m_settings.reload([&] {
        state = lua::newstate();
        if (!state) {
            luaui::reportError("cannot create a Lua state: out of memory");
            return false;
        }
        lua::atpanic(state, &luaui::panicHandler); // an error outside every protected call: log why before the abort
        bool loaded = false;
        // All of the setup runs protected: a Lua error it raises (out of memory, a library's loader) is reported like
        // a script error instead of reaching the panic handler, which aborts.
        luaui::protectedCall(state, "loading the script", [&] {
            lua::openlibs(state);
            luaui::registerNative(state, &m_hooks);
            luaui::registerPainter(state);
            m_types.install(state); // before the DSL: its constructors look the registered types up

            // Lua comes from the resources: `require "imlui"` is imlui:/imlui.lua, components are found next to the
            // entry script.
            luaui::registerResources(state, m_config.resources, loadedNames.get());

            constexpr std::string_view boot =
                R"(local ui = require("imlui"); return ui.__load(...), ui.__tick, ui.__overlays)";
            if (lua::loadbuffer(state, boot, "=imlui.boot") != lua::ok) {
                luaui::reportError(std::string{lua::tostring(state, -1)});
                return;
            }
            lua::pushstring(state, m_config.entry);
            if (!luaui::safeCall(state, 1, 3)) return;
            if (!lua::istable(state, -3)) {
                luaui::reportError(m_config.entry +
                                   ": script must return a root element, e.g. `return ApplicationWindow { ... }`");
                return;
            }
            overlays = lua::istable(state, -1) ? lua::ref(state) : (lua::pop(state), lua::noRef);
            tick = lua::ref(state); // pops __tick
            root = lua::ref(state); // pops root
            loaded = true;
        });
        if (!loaded) {
            // Before the settings are restored: closing runs the state's finalizers, which may set them too.
            lua::close(state);          // destroys its instances and detaches the singletons from it
            m_types.detachState(state); // also when the setup failed before install() made the state detach them
            if (m_state) m_types.activate(m_state);
            // Still watched alongside the running script's: fixing what failed (a data file the new script read,
            // say) must reload.
            m_loaded->merge(*loadedNames);
        }
        return loaded;
    });
    if (!ok) return;

    applyRootWindowProps(state, root);

    closeState(m_state);
    m_loaded = std::move(loadedNames); // after closing the old state, which held the old set
    watchThemeFonts();                 // the theme in use until the script's own applies (which adds its fonts)
    m_state = state;
    m_rootRef = root;
    m_tickRef = tick;
    m_overlayRef = overlays;
    log::info("loaded {} ({})", m_config.entry, m_config.resources.origin(m_config.entry));
}

void Engine::applyRootWindowProps(lua::State* state, int rootRef) {
    double width = 0;
    double height = 0;
    std::string title;
    // Protected: the root table's own metatable (a script's __index) may raise.
    luaui::protectedCall(state, "the root window's properties", [&] {
        lua::rawgeti(state, lua::registryIndex, rootRef);
        const luaui::Node n{state, -1};
        width = n.number("width", 0);
        height = n.number("height", 0);
        title = n.string("title", "");
    });
    // Size is re-applied on hot reload only when the script changed it. A script's size may be anything (math.huge,
    // NaN, 1e12): checked before the cast to int, which is undefined behaviour out of int's range.
    const int w = windowExtent(width), h = windowExtent(height);
    if (w > 0 && h > 0 && (w != m_appliedWidth || h != m_appliedHeight)) {
        m_appliedWidth = w;
        m_appliedHeight = h;
        // width/height are dp: scaled by the monitor content scale (not by the user zoom).
        m_window.setScaledSize(w, h);
        m_window.keepInWorkArea();
    }
    if (!title.empty()) m_window.setTitle(title);
}

} // namespace imlui
