// Window: a floating ImGui window (movable, resizable, closable), as a typed element (TypeBuilder).
module;

#include <optional>
#include <string>
#include <utility>

export module imlui.ui.elements.window;

import imgui;
import imlui.theme.scheme;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Window { title = "Window", open = true, x = 60, y = 60, width, height, autoResize, titleBar = true,
///          resizable = true, movable = true, menuBar = MenuBar { ... }, onClosed = fn, children... }
/// It has a close button when the `open` prop exists (two-way: closing writes open = false, then emits
/// onClosed). x, y, width and height (dp) apply when the window first appears. `autoResize` defaults to
/// true when neither width nor height is set.
class Window final : public Element {
public:
    static void declare(TypeBuilder<Window>& type);

    void render(Frame& frame) override;

    [[nodiscard]] bool open() const;
    void setOpen(bool open);
    [[nodiscard]] const std::string& title() const;
    void setTitle(std::string title);
    [[nodiscard]] float x() const;
    void setX(float x);
    [[nodiscard]] float y() const;
    void setY(float y);
    [[nodiscard]] float width() const;
    void setWidth(float width);
    [[nodiscard]] float height() const;
    void setHeight(float height);
    /// Unset: true when neither width nor height is set.
    [[nodiscard]] std::optional<bool> autoResize() const;
    void setAutoResize(std::optional<bool> autoResize);
    [[nodiscard]] bool titleBar() const;
    void setTitleBar(bool titleBar);
    [[nodiscard]] bool resizable() const;
    void setResizable(bool resizable);
    [[nodiscard]] bool movable() const;
    void setMovable(bool movable);

private:
    bool m_open = true;
    std::string m_title{"Window"};
    float m_x = 60.0f;
    float m_y = 60.0f;
    float m_width = 0.0f;
    float m_height = 0.0f;
    std::optional<bool> m_autoResize;
    bool m_titleBar = true;
    bool m_resizable = true;
    bool m_movable = true;
};

} // namespace imlui

module :private;

namespace imlui {

void Window::declare(TypeBuilder<Window>& type) {
    type.property("open", &Window::open, &Window::setOpen)
        .property("title", &Window::title, &Window::setTitle)
        .property("x", &Window::x, &Window::setX)
        .property("y", &Window::y, &Window::setY)
        .property("width", &Window::width, &Window::setWidth)
        .property("height", &Window::height, &Window::setHeight)
        .property("autoResize", &Window::autoResize, &Window::setAutoResize)
        .property("titleBar", &Window::titleBar, &Window::setTitleBar)
        .property("resizable", &Window::resizable, &Window::setResizable)
        .property("movable", &Window::movable, &Window::setMovable)
        .signal("onClosed")
        .nonVisual();
}

void Window::render(Frame& frame) {
    if (!m_open) return;
    const std::string title = Frame::labelId(m_title, "win");
    ImGui::SetNextWindowPos({frame.dp(m_x), frame.dp(m_y)}, ImGuiCond_FirstUseEver);
    if (m_width > 0 || m_height > 0)
        ImGui::SetNextWindowSize({frame.dp(m_width), frame.dp(m_height)}, ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    if (m_autoResize.value_or(m_width <= 0 && m_height <= 0)) flags |= ImGuiWindowFlags_AlwaysAutoResize;
    if (!m_titleBar) flags |= ImGuiWindowFlags_NoTitleBar;
    if (!m_resizable) flags |= ImGuiWindowFlags_NoResize;
    if (!m_movable) flags |= ImGuiWindowFlags_NoMove;
    const bool menu = frame.node().has("menuBar");
    if (menu) flags |= ImGuiWindowFlags_MenuBar;
    const bool closable = frame.node().has("open");
    bool keep = true;
    const Scheme& s = frame.scheme();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, s.surfaceContainerLow.vec4());
    ImGui::PushStyleColor(ImGuiCol_TitleBg, s.surfaceContainer.vec4());
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, s.surfaceContainerHigh.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, frame.dp(1));
    ImGui::PushStyleColor(ImGuiCol_Border, s.outlineVariant.vec4());
    const bool shown = ImGui::Begin(title.c_str(), closable ? &keep : nullptr, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (shown) {
        md::pushSurface(s.surfaceContainerLow);
        if (menu) frame.renderElementProp("menuBar");
        frame.renderStack();
        md::popSurface();
    }
    ImGui::End();
    if (!keep) {
        m_open = false;
        notify("open");
        emit("onClosed");
    }
}

bool Window::open() const { return m_open; }
void Window::setOpen(bool open) { m_open = open; }
const std::string& Window::title() const { return m_title; }
void Window::setTitle(std::string title) { m_title = std::move(title); }
float Window::x() const { return m_x; }
void Window::setX(float x) { m_x = x; }
float Window::y() const { return m_y; }
void Window::setY(float y) { m_y = y; }
float Window::width() const { return m_width; }
void Window::setWidth(float width) { m_width = width; }
float Window::height() const { return m_height; }
void Window::setHeight(float height) { m_height = height; }
std::optional<bool> Window::autoResize() const { return m_autoResize; }
void Window::setAutoResize(std::optional<bool> autoResize) { m_autoResize = autoResize; }
bool Window::titleBar() const { return m_titleBar; }
void Window::setTitleBar(bool titleBar) { m_titleBar = titleBar; }
bool Window::resizable() const { return m_resizable; }
void Window::setResizable(bool resizable) { m_resizable = resizable; }
bool Window::movable() const { return m_movable; }
void Window::setMovable(bool movable) { m_movable = movable; }

} // namespace imlui
