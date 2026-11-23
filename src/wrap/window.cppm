// Module wrapper around GLFW + OpenGL 3 + the ImGui platform/renderer backends.
//
// Exposes a single `imlui::platform::Window` class: create a native window
// with an ImGui context, and drive the frame loop. No GLFW / GL types leak
// out of this module.
module;

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <functional>
#include <string>
#include <utility>

export module imlui.platform.window;

import imlui.log;

export namespace imlui::platform {

class Window {
public:
    struct Config {
        std::string title = "imlui";
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

    /// Processes OS events. May render frames through the refresh callback.
    void pollEvents();

    /// Starts a new ImGui frame.
    void beginFrame();

    /// Renders the ImGui frame over a cleared background and presents it.
    void endFrame(float r, float g, float b);

    void setTitle(const std::string& title);
    void setSize(int w, int h);
    void show();
    /// Centers the window on the primary monitor.
    void center();

    /// Monitor content scale (DPI factor) at creation time.
    [[nodiscard]] float contentScale() const;

private:
    void refreshDuringEvents();

    static Window* m_self;
    std::function<void()> m_refresh;
    bool m_inPoll = false, m_inRefresh = false;
    GLFWwindow* m_window = nullptr;
    bool m_glfwInitialized = false;
    bool m_backendsReady = false;
    float m_scale = 1.0f;
    std::string m_title;
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
    glfwSwapInterval(cfg.vsync ? 1 : 0);

    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(m_window, &sx, &sy);
    m_scale = sx > 0.0f ? sx : 1.0f;

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
    m_self = this;
    glfwSetWindowRefreshCallback(m_window, [](GLFWwindow*) {
        if (m_self) m_self->refreshDuringEvents();
    });
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow*, int, int) {
        if (m_self) m_self->refreshDuringEvents();
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

void Window::pollEvents() {
    m_inPoll = true;
    glfwPollEvents();
    if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) {
        // Don't spin while minimized.
        glfwWaitEventsTimeout(0.1);
    }
    m_inPoll = false;
}

void Window::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Window::endFrame(float r, float g, float b) {
    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
}

void Window::setTitle(const std::string& title) {
    if (title != m_title) {
        m_title = title;
        glfwSetWindowTitle(m_window, title.c_str());
    }
}

void Window::setSize(int w, int h) { glfwSetWindowSize(m_window, w, h); }
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

float Window::contentScale() const { return m_scale; }

void Window::refreshDuringEvents() {
    // Only from inside pollEvents(): no frame is in progress then.
    if (!m_inPoll || m_inRefresh || !m_refresh) return;
    m_inRefresh = true;
    m_refresh();
    m_inRefresh = false;
}

} // namespace imlui::platform
