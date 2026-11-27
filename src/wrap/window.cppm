// Module wrapper around GLFW + OpenGL 3 + the ImGui platform/renderer backends.
//
// Exposes a single `imlui::platform::Window` class: create a native window
// with an ImGui context, and drive the frame loop. No GLFW / GL types leak
// out of this module.
module;

#include <GLFW/glfw3.h>
#if defined(_WIN32)
// The native window handle, for the title bar colors (DwmSetWindowAttribute).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#endif
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

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

private:
    /// `resized`: called for a new framebuffer size (else for a repaint the OS asked for).
    void refreshDuringEvents(bool resized);
    void installEventCallbacks();
    /// After an event poll: resumes vsync (refreshDuringEvents) and forgets the frames it presented.
    void endPoll();
    static void markEvent();
    void onContentScale(float scale);

    static Window* m_self;
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
    GLFWwindow* m_window = nullptr;
    bool m_glfwInitialized = false;
    bool m_backendsReady = false;
    float m_scale = 1.0f;
    std::string m_title;
    std::optional<std::pair<std::array<float, 3>, std::array<float, 3>>> m_titleBarColors;
    std::optional<bool> m_titleBarDark; ///< the dark mode last set on the title bar
};

} // namespace imlui::platform

module :private;

namespace imlui::platform {

Window* Window::m_self = nullptr;

Window::~Window() { destroy(); }

bool Window::create(const Config& cfg) {
    glfwSetErrorCallback([](int code, const char* msg) {
        log::write(log::Level::Error, std::to_string(code) + ": " + msg, "glfw");
    });
    if (!glfwInit()) return false;
    m_glfwInitialized = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, cfg.visible ? GLFW_TRUE : GLFW_FALSE);

    m_window = glfwCreateWindow(cfg.width, cfg.height, cfg.title.c_str(), nullptr, nullptr);
    if (!m_window) return false;
    glfwMakeContextCurrent(m_window);
    m_vsync = cfg.vsync;
    glfwSwapInterval(cfg.vsync ? 1 : 0);

    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(m_window, &sx, &sy);
    m_scale = sx > 0.0f ? sx : 1.0f;

    // Before the ImGui backend installs its callbacks: it chains to these, so
    // every input event also marks the frame loop as woken by input.
    m_self = this;
    installEventCallbacks();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // UI layout is owned by the Lua description

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    m_backendsReady = true;

    // While the user drags a window edge, Windows runs a modal loop inside
    // glfwPollEvents() and the app loop is stalled. GLFW still calls these
    // callbacks, so frames are rendered from them to keep the UI live.
    // A resize step sends both: the new size, then a repaint at that size, rendered only once.
    glfwSetWindowRefreshCallback(m_window, [](GLFWwindow*) {
        if (m_self) m_self->refreshDuringEvents(false);
    });
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow*, int, int) {
        if (!m_self) return;
        m_self->m_hadEvents = true;
        m_self->refreshDuringEvents(true);
    });
    log::info("window created ({}x{}, content scale {:.2f})", cfg.width, cfg.height, m_scale);
    return true;
}

void Window::destroy() {
    if (m_backendsReady) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_backendsReady = false;
    }
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_self == this) m_self = nullptr;
    if (m_glfwInitialized) {
        glfwTerminate();
        m_glfwInitialized = false;
    }
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window) != 0; }
void Window::requestClose() { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }

void Window::setRefreshCallback(std::function<void()> fn) { m_refresh = std::move(fn); }

void Window::setContentScaleCallback(std::function<void(float)> fn) { m_onContentScale = std::move(fn); }

void Window::installEventCallbacks() {
    glfwSetCursorPosCallback(m_window, [](GLFWwindow*, double, double) { markEvent(); });
    glfwSetMouseButtonCallback(m_window, [](GLFWwindow*, int, int, int) { markEvent(); });
    glfwSetScrollCallback(m_window, [](GLFWwindow*, double, double) { markEvent(); });
    glfwSetKeyCallback(m_window, [](GLFWwindow*, int, int, int, int) { markEvent(); });
    glfwSetCharCallback(m_window, [](GLFWwindow*, unsigned int) { markEvent(); });
    glfwSetCursorEnterCallback(m_window, [](GLFWwindow*, int) { markEvent(); });
    glfwSetWindowFocusCallback(m_window, [](GLFWwindow*, int) { markEvent(); });
    glfwSetWindowSizeCallback(m_window, [](GLFWwindow*, int, int) { markEvent(); });
    glfwSetWindowIconifyCallback(m_window, [](GLFWwindow*, int) { markEvent(); });
    glfwSetWindowMaximizeCallback(m_window, [](GLFWwindow*, int) { markEvent(); });
    glfwSetDropCallback(m_window, [](GLFWwindow*, int, const char**) { markEvent(); });
    glfwSetWindowContentScaleCallback(m_window, [](GLFWwindow*, float sx, float) {
        if (m_self) m_self->onContentScale(sx);
    });
}

void Window::markEvent() {
    if (m_self) m_self->m_hadEvents = true;
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
    glfwPollEvents();
    if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) {
        // Don't spin while minimized.
        glfwWaitEventsTimeout(0.1);
    }
    endPoll();
}

bool Window::waitEvents(double seconds) {
    m_inPoll = true;
    if (seconds <= 0.0)
        glfwPollEvents();
    else if (seconds >= 1e9)
        glfwWaitEvents();
    else
        glfwWaitEventsTimeout(seconds);
    endPoll();
    const bool had = m_hadEvents;
    m_hadEvents = false;
    return had;
}

void Window::postEmptyEvent() { glfwPostEmptyEvent(); }

bool Window::minimized() const { return glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0; }

void Window::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Window::endFrame(float r, float g, float b) {
    draw(r, g, b);
    present();
}

void Window::draw(float r, float g, float b) {
    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Also when the frame is drawn again before it's shown: the backend creates and updates the textures the frame
    // asked for (new glyphs, images) here.
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Window::present() {
    glfwSwapBuffers(m_window);
    if (m_inPoll) m_presentedInPoll = framebufferSize();
}

std::pair<int, int> Window::framebufferSize() const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    return {w, h};
}

void Window::setTitle(const std::string& title) {
    if (title != m_title) {
        m_title = title;
        glfwSetWindowTitle(m_window, title.c_str());
    }
}

void Window::setTitleBarColors(std::array<float, 3> background, std::array<float, 3> text) {
    if (!m_window || m_titleBarColors == std::pair{background, text}) return;
    m_titleBarColors = std::pair{background, text};
#if defined(_WIN32)
    HWND hwnd = glfwGetWin32Window(m_window);
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

void Window::setSize(int w, int h) { glfwSetWindowSize(m_window, w, h); }

void Window::setScaledSize(int w, int h) {
    // Pixels per screen coordinate: 1 on Windows and X11, 2 on a Retina display.
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetWindowSize(m_window, &ww, &wh);
    glfwGetFramebufferSize(m_window, &fw, &fh);
    const float pixelsPerUnit = ww > 0 && fw > 0 ? static_cast<float>(fw) / static_cast<float>(ww) : 1.0f;
    const float k = m_scale / pixelsPerUnit;
    glfwSetWindowSize(m_window, static_cast<int>(static_cast<float>(w) * k + 0.5f),
                      static_cast<int>(static_cast<float>(h) * k + 0.5f));
}
void Window::show() { glfwShowWindow(m_window); }

void Window::center() {
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (!mon) return;
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    int mx = 0, my = 0, w = 0, h = 0;
    glfwGetMonitorPos(mon, &mx, &my);
    glfwGetWindowSize(m_window, &w, &h);
    if (mode) glfwSetWindowPos(m_window, mx + (mode->width - w) / 2, my + (mode->height - h) / 2);
}

void Window::keepInWorkArea() {
    if (!m_window) return;
    int x = 0, y = 0, w = 0, h = 0;
    glfwGetWindowPos(m_window, &x, &y);
    glfwGetWindowSize(m_window, &w, &h);
    // Decorations (title bar, borders) count: the whole frame must fit.
    int left = 0, top = 0, right = 0, bottom = 0;
    glfwGetWindowFrameSize(m_window, &left, &top, &right, &bottom);

    // The monitor with the largest overlap with the frame (the primary one when none overlaps).
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    long long bestArea = 0;
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        const long long ow = std::max(0, std::min(x + w + right, mx + mw) - std::max(x - left, mx));
        const long long oh = std::max(0, std::min(y + h + bottom, my + mh) - std::max(y - top, my));
        if (ow * oh > bestArea) {
            bestArea = ow * oh;
            best = monitors[i];
        }
    }
    if (!best) return;
    int ax = 0, ay = 0, aw = 0, ah = 0;
    glfwGetMonitorWorkarea(best, &ax, &ay, &aw, &ah);
    if (aw <= 0 || ah <= 0) return;

    const int maxW = std::max(1, aw - left - right), maxH = std::max(1, ah - top - bottom);
    const int nw = std::min(w, maxW), nh = std::min(h, maxH);
    // Past the right/bottom edge: move back in; then make sure the top-left (title bar) is on screen.
    int nx = std::min(x, ax + aw - right - nw), ny = std::min(y, ay + ah - bottom - nh);
    nx = std::max(nx, ax + left);
    ny = std::max(ny, ay + top);
    if (nw != w || nh != h) {
        log::info("window {}x{} doesn't fit the work area: {}x{}", w, h, nw, nh);
        glfwSetWindowSize(m_window, nw, nh);
    }
    if (nx != x || ny != y) glfwSetWindowPos(m_window, nx, ny);
}

float Window::contentScale() const { return m_scale; }

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
        glfwSwapInterval(0);
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
        glfwSwapInterval(1);
        m_vsyncPaused = false;
    }
}

} // namespace imlui::platform
