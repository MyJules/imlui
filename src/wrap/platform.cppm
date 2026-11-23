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

#include <cstdio>
#include <string>

export module imlui.platform;

export namespace imlui::platform {

struct WindowConfig {
    std::string title = "imlui";
    int width = 1280;
    int height = 800;
    bool vsync = true;
    bool visible = true; ///< create hidden and call show() later
};

class Window {
public:
    Window() = default;
    ~Window() { destroy(); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// Creates the OS window, a GL 3.3 context and the ImGui context +
    /// backends. Returns false (and prints why) on failure.
    bool create(const WindowConfig& cfg) {
        glfwSetErrorCallback([](int code, const char* msg) {
            std::fprintf(stderr, "[glfw] error %d: %s\n", code, msg);
        });
        if (!glfwInit()) return false;
        glfwInitialized_ = true;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, cfg.visible ? GLFW_TRUE : GLFW_FALSE);

        window_ = glfwCreateWindow(cfg.width, cfg.height, cfg.title.c_str(), nullptr, nullptr);
        if (!window_) return false;
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(cfg.vsync ? 1 : 0);

        float sx = 1.0f, sy = 1.0f;
        glfwGetWindowContentScale(window_, &sx, &sy);
        scale_ = sx > 0.0f ? sx : 1.0f;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr; // UI layout is owned by the Lua description

        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        ImGui_ImplOpenGL3_Init("#version 330");
        backendsReady_ = true;
        return true;
    }

    void destroy() {
        if (backendsReady_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            backendsReady_ = false;
        }
        if (window_) { glfwDestroyWindow(window_); window_ = nullptr; }
        if (glfwInitialized_) { glfwTerminate(); glfwInitialized_ = false; }
    }

    [[nodiscard]] bool shouldClose() const { return glfwWindowShouldClose(window_) != 0; }
    void requestClose() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }

    /// Polls events and starts a new ImGui frame.
    void beginFrame() {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED)) {
            // Don't spin while minimized.
            glfwWaitEventsTimeout(0.1);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    /// Renders the ImGui frame over a cleared background and presents it.
    void endFrame(float r, float g, float b) {
        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    void setTitle(const std::string& title) {
        if (title != title_) { title_ = title; glfwSetWindowTitle(window_, title.c_str()); }
    }
    void setSize(int w, int h) { glfwSetWindowSize(window_, w, h); }
    void show() { glfwShowWindow(window_); }
    /// Centers the window on the primary monitor.
    void center() {
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        if (!mon) return;
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        int mx = 0, my = 0, w = 0, h = 0;
        glfwGetMonitorPos(mon, &mx, &my);
        glfwGetWindowSize(window_, &w, &h);
        if (mode) glfwSetWindowPos(window_, mx + (mode->width - w) / 2, my + (mode->height - h) / 2);
    }

    /// Monitor content scale (DPI factor) at creation time.
    [[nodiscard]] float contentScale() const { return scale_; }

    /// Must be called after the font atlas changed between frames.
    void reloadFonts() {
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }

private:
    GLFWwindow* window_ = nullptr;
    bool glfwInitialized_ = false;
    bool backendsReady_ = false;
    float scale_ = 1.0f;
    std::string title_;
};

} // namespace imlui::platform
