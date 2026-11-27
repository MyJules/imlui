// Module wrapper around SDL3 + OpenGL 3 + the ImGui platform/renderer backends.
//
// Exposes a single `imlui::platform::Window` class: create a native window
// with an ImGui context, and drive the frame loop. No SDL / GL types leak
// out of this module.
module;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
// The app keeps its own plain main(): SDL_main.h must not rename it (macros don't leave a module anyway); SDL_SetMainReady
// below says SDL's startup was done.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#if defined(_WIN32)
// The native window handle, for the title bar colors (DwmSetWindowAttribute).
#include <windows.h>
#include <dwmapi.h>
#endif
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

export module imlui.platform.window;

import std;

import imlui.log;

export namespace imlui::platform {

class Window {
public:
    struct Config {
        std::string title = "imlui";
        /// Initial size in unscaled units (dp at 100 %); scaled to the monitor's content scale.
        int width = 1280;
        int height = 800;
        bool vsync = true;
        bool visible = true; ///< create hidden and call show() later
    };

    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// Creates the OS window, a GL 3.3 context and the ImGui context +
    /// backends. Returns false (and logs why) on failure.
    bool create(const Config& cfg);
    void destroy();

    [[nodiscard]] bool shouldClose() const;
    void requestClose();

    /// Called to render one complete frame (beginFrame ... endFrame) while
    /// events are being processed, e.g. during a live resize.
    void setRefreshCallback(std::function<void()> fn);

    /// Called when the monitor content scale of the window changes (moved to
    /// another monitor, or the OS scale setting changed). Gets the new scale.
    void setContentScaleCallback(std::function<void(float)> fn);

    /// Processes OS events. May render frames through the refresh callback.
    void pollEvents();

    /// Waits up to `seconds` for OS events and processes them (0 = don't wait,
    /// infinity = wait until an event arrives). May render frames through the
    /// refresh callback. Returns true if input or window events arrived.
    bool waitEvents(double seconds);

    /// Wakes a waitEvents() call from anywhere (thread-safe).
    static void postEmptyEvent();

    [[nodiscard]] bool minimized() const;

    /// Starts a new ImGui frame.
    void beginFrame();

    /// Renders the ImGui frame over a cleared background and presents it (draw(), then present()).
    void endFrame(float r, float g, float b);
    /// Ends the ImGui frame and draws it over a cleared background into the back buffer, without presenting it: a
    /// frame drawn again before it is shown (beginFrame() ... draw() once more) replaces it.
    void draw(float r, float g, float b);
    /// Shows the last frame draw() drew.
    void present();
    /// The framebuffer size in pixels.
    [[nodiscard]] std::pair<int, int> framebufferSize() const;

    void setTitle(const std::string& title);
    /// Colors the OS title bar (sRGB, 0..1) to match the app: on Windows 10 (20H1+) a dark `background` switches
    /// the title bar to dark mode; Windows 11 also takes both colors exactly. Calls with unchanged colors do
    /// nothing; other platforms ignore it.
    void setTitleBarColors(std::array<float, 3> background, std::array<float, 3> text);
    /// Sets the size in screen coordinates.
    void setSize(int w, int h);
    /// Sets the size in unscaled units (dp at 100 %): multiplied by the content
    /// scale and converted to screen coordinates (pixels on Windows, points on macOS).
    void setScaledSize(int w, int h);
    void show();
    /// Centers the window on the primary monitor.
    void center();
    /// Keeps the window (with its decorations) inside the work area (the monitor minus task bars and docks)
    /// of the monitor it overlaps most: shrinks it if it's larger, then moves it left/up if it extends past
    /// the right or bottom edge (and right/down if it starts before the left or top edge).
    void keepInWorkArea();

    /// Current monitor content scale (DPI factor) of the window.
    [[nodiscard]] float contentScale() const;
    /// The refresh rate in Hz of the monitor the window overlaps most (the primary one when it overlaps none); 0 when
    /// it isn't known. Looked up again only after the window moved or was resized, or the monitors changed.
    [[nodiscard]] int refreshRate();

private:
    /// `resized`: called for a new framebuffer size (else for a repaint the OS asked for).
    void refreshDuringEvents(bool resized);
    /// SDL's event watch: sees each event as it's sent, also from inside the OS's modal loops (a live resize).
    static bool watchEvent(void* self, SDL_Event* event);
    /// An event taken from the queue: to ImGui, and the window's own state.
    void handleEvent(const SDL_Event& event);
    /// After an event poll: resumes vsync (refreshDuringEvents) and forgets the frames it presented.
    void endPoll();
    void onContentScale(float scale);

    std::function<void()> m_refresh;
    std::function<void(float)> m_onContentScale;
    bool m_inPoll = false, m_inRefresh = false;
    bool m_vsync = true;
    /// Vsync is off for the rest of this event poll: while a live resize renders frames from inside it.
    bool m_vsyncPaused = false;
    /// The framebuffer size of the last frame presented from inside this event poll (a refresh after a resize
    /// already shows that size: not rendered again), or none.
    std::optional<std::pair<int, int>> m_presentedInPoll;
    bool m_hadEvents = false;
    bool m_shouldClose = false;
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    bool m_sdlInitialized = false;
    bool m_watching = false;
    bool m_backendsReady = false;
    float m_scale = 1.0f;
    std::optional<int> m_refreshRate; ///< refreshRate(), until the window moves or the monitors change
    std::string m_title;
    std::optional<std::pair<std::array<float, 3>, std::array<float, 3>>> m_titleBarColors;
    std::optional<bool> m_titleBarDark; ///< the dark mode last set on the title bar
};

} // namespace imlui::platform

module :private;

namespace imlui::platform {

namespace {

/// The user event postEmptyEvent() pushes (SDL_RegisterEvents), 0 while SDL isn't initialized: a post then does nothing.
std::atomic<std::uint32_t> wakeEventType{0};
/// A wake event is in the queue: further posts don't add another one (a burst of posts from workers wakes the loop
/// once, and can't fill SDL's queue). Cleared when the loop takes the event.
std::atomic<bool> wakePending{false};

/// SDL's log lines (warnings and errors only, by SDL's default priorities) go to imlui's log, tagged `[sdl]`.
void logSdl(void*, int, SDL_LogPriority priority, const char* message) {
    log::Level level = log::Level::Info;
    switch (priority) {
    case SDL_LOG_PRIORITY_TRACE:
    case SDL_LOG_PRIORITY_VERBOSE:
    case SDL_LOG_PRIORITY_DEBUG: level = log::Level::Debug; break;
    case SDL_LOG_PRIORITY_INFO: level = log::Level::Info; break;
    case SDL_LOG_PRIORITY_WARN: level = log::Level::Warn; break;
    case SDL_LOG_PRIORITY_ERROR: level = log::Level::Error; break;
    case SDL_LOG_PRIORITY_CRITICAL: level = log::Level::Critical; break;
    default: break;
    }
    log::write(level, message ? message : "", "sdl");
}

/// Logs why an SDL call failed (SDL_GetError).
void logSdlError(std::string_view what) { log::write(log::Level::Error, std::format("{}: {}", what, SDL_GetError()), "sdl"); }

/// The display whose work area overlaps `window`'s frame (decorations included) most; the primary one when none does.
SDL_DisplayID displayOf(SDL_Window* window) {
    int x = 0, y = 0, w = 0, h = 0;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    int left = 0, top = 0, right = 0, bottom = 0;
    SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID best = SDL_GetPrimaryDisplay();
    long long bestArea = 0;
    for (int i = 0; i < count; ++i) {
        SDL_Rect area{};
        if (!SDL_GetDisplayUsableBounds(displays[i], &area)) continue;
        const long long ow = std::max(0, std::min(x + w + right, area.x + area.w) - std::max(x - left, area.x));
        const long long oh = std::max(0, std::min(y + h + bottom, area.y + area.h) - std::max(y - top, area.y));
        if (ow * oh > bestArea) {
            bestArea = ow * oh;
            best = displays[i];
        }
    }
    SDL_free(displays);
    return best;
}

} // namespace

Window::~Window() { destroy(); }

bool Window::create(const Config& cfg) {
    SDL_SetLogOutputFunction(logSdl, nullptr);
    SDL_SetMainReady();
    // A UI app, not a game: the screen saver and the console's Ctrl+C stay the OS's and the app's (SDL would block the
    // first and turn the second into a quit event).
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        logSdlError("SDL_Init");
        return false;
    }
    m_sdlInitialized = true;
    m_shouldClose = false;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // The size is in dp: scaled to the content scale of the display the window opens on (the primary one), as screen
    // coordinates (pixels on Windows, points on macOS, where that scale is 1 and the pixel density does the rest).
    float displayScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (displayScale <= 0.0f) displayScale = 1.0f;
    const auto scaled = [&](int v) { return std::max(1, static_cast<int>(static_cast<float>(v) * displayScale + 0.5f)); };
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!cfg.visible) flags |= SDL_WINDOW_HIDDEN;
    m_window = SDL_CreateWindow(cfg.title.c_str(), scaled(cfg.width), scaled(cfg.height), flags);
    if (!m_window) {
        logSdlError("SDL_CreateWindow");
        return false;
    }
    m_title = cfg.title;
    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context) {
        logSdlError("SDL_GL_CreateContext");
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_context);
    m_vsync = cfg.vsync;
    SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0);

    const float scale = SDL_GetWindowDisplayScale(m_window);
    m_scale = scale > 0.0f ? scale : 1.0f;

    // The wake event of postEmptyEvent(): registered now, as SDL forgets the registrations when it quits.
    const std::uint32_t wake = SDL_RegisterEvents(1);
    wakePending = false;
    wakeEventType = wake; // 0 when SDL ran out of event numbers: then nothing wakes the loop early

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // UI layout is owned by the Lua description

    // Clipboard and the IME are the backend's (ImGui's platform IO): it starts SDL's text input on the window while
    // ImGui wants text (a focused text field) and stops it after, and places the IME's candidate window at the caret.
    ImGui_ImplSDL3_InitForOpenGL(m_window, m_context);
    ImGui_ImplOpenGL3_Init("#version 330");
    m_backendsReady = true;

    // While the user drags a window edge, Windows runs a modal loop inside the event poll and the app loop is stalled.
    // SDL still sends the window's events (a new size, and an expose for each step of the drag and on a timer) through
    // its event watches as they happen, so frames are rendered from there to keep the UI live (watchEvent).
    m_watching = SDL_AddEventWatch(watchEvent, this);
    log::info("window created ({}x{}, content scale {:.2f}, {})", cfg.width, cfg.height, m_scale,
              SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
    return true;
}

void Window::destroy() {
    if (m_watching) {
        SDL_RemoveEventWatch(watchEvent, this);
        m_watching = false;
    }
    if (m_backendsReady) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_backendsReady = false;
    }
    if (m_context) {
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_sdlInitialized) {
        wakeEventType = 0;
        wakePending = false;
        SDL_Quit();
        m_sdlInitialized = false;
    }
    m_refreshRate.reset();
    m_titleBarColors.reset();
    m_titleBarDark.reset();
    m_title.clear();
}

bool Window::shouldClose() const { return m_shouldClose; }
void Window::requestClose() { m_shouldClose = true; }

void Window::setRefreshCallback(std::function<void()> fn) { m_refresh = std::move(fn); }

void Window::setContentScaleCallback(std::function<void(float)> fn) { m_onContentScale = std::move(fn); }

bool Window::watchEvent(void* userdata, SDL_Event* event) {
    // Any thread may push an event (postEmptyEvent): only the window's and the displays' own, which SDL sends on the
    // main thread, are looked at here.
    auto* self = static_cast<Window*>(userdata);
    switch (event->type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        // A resize step sends both: the new size, then an expose at that size, rendered only once.
        self->m_hadEvents = true;
        self->refreshDuringEvents(true);
        break;
    case SDL_EVENT_WINDOW_EXPOSED: self->refreshDuringEvents(false); break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        // Here rather than from the queue: moving a window to another monitor is a modal loop on Windows too.
        if (self->m_window) self->onContentScale(SDL_GetWindowDisplayScale(self->m_window));
        break;
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        self->m_refreshRate.reset(); // may now overlap another monitor most, or the monitor changed its mode
        break;
    default: break;
    }
    return true;
}

void Window::handleEvent(const SDL_Event& event) {
    if (wakeEventType != 0 && event.type == wakeEventType) {
        wakePending = false;
        return; // only wakes the loop: not input
    }
    ImGui_ImplSDL3_ProcessEvent(&event);
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: m_shouldClose = true; break;
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
        // SDL switched the title bar to the OS's light or dark mode: the app's colors again.
        if (const auto colors = m_titleBarColors) {
            m_titleBarColors.reset();
            m_titleBarDark.reset();
            setTitleBarColors(colors->first, colors->second);
        }
        break;
    // Input, and what changes the window's look: the frame loop wakes up for these (waitEvents() returns true).
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_COMPLETE: m_hadEvents = true; break;
    default: break;
    }
}

void Window::onContentScale(float scale) {
    m_hadEvents = true;
    if (scale <= 0.0f || scale == m_scale) return;
    log::info("content scale changed: {:.2f} -> {:.2f}", m_scale, scale);
    m_scale = scale;
    if (m_onContentScale) m_onContentScale(scale);
}

void Window::pollEvents() {
    m_inPoll = true;
    SDL_Event event;
    while (SDL_PollEvent(&event))
        handleEvent(event);
    if (minimized()) {
        // Don't spin while minimized.
        if (SDL_WaitEventTimeout(&event, 100)) handleEvent(event);
    }
    endPoll();
}

bool Window::waitEvents(double seconds) {
    m_inPoll = true;
    SDL_Event event;
    // Whole milliseconds, rounded up: waking early would only poll again until the time is due.
    if (seconds >= 1e9 ? SDL_WaitEvent(&event)
        : seconds > 0.0 ? SDL_WaitEventTimeout(&event, static_cast<Sint32>(std::min(std::ceil(seconds * 1000.0), 2.0e9)))
                        : false)
        handleEvent(event);
    while (SDL_PollEvent(&event))
        handleEvent(event);
    endPoll();
    const bool had = m_hadEvents;
    m_hadEvents = false;
    return had;
}

void Window::postEmptyEvent() {
    const std::uint32_t type = wakeEventType;
    if (type == 0 || wakePending.exchange(true)) return;
    SDL_Event event{};
    event.type = type;
    if (!SDL_PushEvent(&event)) wakePending = false; // not queued: the next post tries again
}

bool Window::minimized() const { return m_window && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0; }

void Window::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Window::endFrame(float r, float g, float b) {
    draw(r, g, b);
    present();
}

void Window::draw(float r, float g, float b) {
    ImGui::Render();
    const auto [w, h] = framebufferSize();
    glViewport(0, 0, w, h);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Also when the frame is drawn again before it's shown: the backend creates and updates the textures the frame
    // asked for (new glyphs, images) here.
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Window::present() {
    SDL_GL_SwapWindow(m_window);
    if (m_inPoll) m_presentedInPoll = framebufferSize();
}

std::pair<int, int> Window::framebufferSize() const {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    return {w, h};
}

void Window::setTitle(const std::string& title) {
    if (title != m_title) {
        m_title = title;
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

void Window::setTitleBarColors(std::array<float, 3> background, std::array<float, 3> text) {
    if (!m_window || m_titleBarColors == std::pair{background, text}) return;
    m_titleBarColors = std::pair{background, text};
#if defined(_WIN32)
    auto* hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    // Relative luminance (Rec. 709 weights on the sRGB values: precise enough to tell dark from light).
    const float luma = 0.2126f * background[0] + 0.7152f * background[1] + 0.0722f * background[2];
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 from Windows 10 20H1 on and was 19 before; the SDK may lack either.
    constexpr DWORD useImmersiveDarkMode = 20, useImmersiveDarkModeBefore20H1 = 19;
    const bool isDark = luma < 0.5f;
    const bool darkChanged = m_titleBarDark != isDark;
    m_titleBarDark = isDark;
    const BOOL dark = isDark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, useImmersiveDarkMode, &dark, sizeof dark)))
        DwmSetWindowAttribute(hwnd, useImmersiveDarkModeBefore20H1, &dark, sizeof dark);
    // Windows 10 doesn't repaint the caption of a visible window when its dark mode changes (only on the next
    // resize or activation): a frame change forces the non-client repaint. Only on an actual change, as the
    // Engine calls this every frame (unchanged colors return above anyway).
    if (darkChanged && IsWindowVisible(hwnd))
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    // DWMWA_CAPTION_COLOR / DWMWA_TEXT_COLOR: Windows 11 only; earlier versions fail the call, which is fine.
    constexpr DWORD captionColor = 35, textColor = 36;
    const auto colorRef = [](std::array<float, 3> c) {
        const auto byte = [](float v) { return static_cast<DWORD>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        return static_cast<COLORREF>(byte(c[0]) | (byte(c[1]) << 8) | (byte(c[2]) << 16));
    };
    const COLORREF caption = colorRef(background), captionText = colorRef(text);
    DwmSetWindowAttribute(hwnd, captionColor, &caption, sizeof caption);
    DwmSetWindowAttribute(hwnd, textColor, &captionText, sizeof captionText);
#endif
}

void Window::setSize(int w, int h) { SDL_SetWindowSize(m_window, w, h); }

void Window::setScaledSize(int w, int h) {
    // Pixels per screen coordinate: 1 on Windows and X11, 2 on a Retina display.
    int ww = 0, wh = 0;
    SDL_GetWindowSize(m_window, &ww, &wh);
    const auto [fw, fh] = framebufferSize();
    const float pixelsPerUnit = ww > 0 && fw > 0 ? static_cast<float>(fw) / static_cast<float>(ww) : 1.0f;
    const float k = m_scale / pixelsPerUnit;
    SDL_SetWindowSize(m_window, static_cast<int>(static_cast<float>(w) * k + 0.5f),
                      static_cast<int>(static_cast<float>(h) * k + 0.5f));
}
void Window::show() { SDL_ShowWindow(m_window); }

void Window::center() {
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    SDL_Rect bounds{};
    if (display == 0 || !SDL_GetDisplayBounds(display, &bounds)) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    SDL_SetWindowPosition(m_window, bounds.x + (bounds.w - w) / 2, bounds.y + (bounds.h - h) / 2);
}

void Window::keepInWorkArea() {
    if (!m_window) return;
    int x = 0, y = 0, w = 0, h = 0;
    SDL_GetWindowPosition(m_window, &x, &y);
    SDL_GetWindowSize(m_window, &w, &h);
    // Decorations (title bar, borders) count: the whole frame must fit.
    int left = 0, top = 0, right = 0, bottom = 0;
    SDL_GetWindowBordersSize(m_window, &top, &left, &bottom, &right);

    const SDL_DisplayID best = displayOf(m_window);
    if (best == 0) return;
    SDL_Rect area{};
    if (!SDL_GetDisplayUsableBounds(best, &area) || area.w <= 0 || area.h <= 0) return;

    const int maxW = std::max(1, area.w - left - right), maxH = std::max(1, area.h - top - bottom);
    const int nw = std::min(w, maxW), nh = std::min(h, maxH);
    // Past the right/bottom edge: move back in; then make sure the top-left (title bar) is on screen.
    int nx = std::min(x, area.x + area.w - right - nw), ny = std::min(y, area.y + area.h - bottom - nh);
    nx = std::max(nx, area.x + left);
    ny = std::max(ny, area.y + top);
    if (nw != w || nh != h) {
        log::info("window {}x{} doesn't fit the work area: {}x{}", w, h, nw, nh);
        SDL_SetWindowSize(m_window, nw, nh);
    }
    if (nx != x || ny != y) SDL_SetWindowPosition(m_window, nx, ny);
}

float Window::contentScale() const { return m_scale; }

int Window::refreshRate() {
    if (!m_window) return 0;
    if (!m_refreshRate) {
        const SDL_DisplayID display = displayOf(m_window);
        const SDL_DisplayMode* mode = display != 0 ? SDL_GetCurrentDisplayMode(display) : nullptr;
        m_refreshRate = mode ? static_cast<int>(std::lround(mode->refresh_rate)) : 0;
    }
    return *m_refreshRate;
}

void Window::refreshDuringEvents(bool resized) {
    // Only from inside pollEvents(): no frame is in progress then.
    if (!m_inPoll || m_inRefresh || !m_refresh) return;
    // The repaint that follows a resize step, at the size just presented: that frame shows it already. Rendering it
    // again would wait for another vertical blank on every step of the drag.
    if (!resized && m_presentedInPoll == framebufferSize()) return;
    // A live resize (Windows runs it as a modal loop inside the poll) renders a frame for each step: with vsync
    // each would wait for a vertical blank, and the window's content would trail its edge. The compositor shows a
    // windowed app's frames without tearing anyway; vsync is back when the poll returns.
    if (m_vsync && !m_vsyncPaused) {
        SDL_GL_SetSwapInterval(0);
        m_vsyncPaused = true;
    }
    m_inRefresh = true;
    m_refresh();
    m_inRefresh = false;
}

void Window::endPoll() {
    m_inPoll = false;
    m_presentedInPoll.reset();
    if (m_vsyncPaused) {
        SDL_GL_SetSwapInterval(1);
        m_vsyncPaused = false;
    }
}

} // namespace imlui::platform
