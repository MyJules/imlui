// ApplicationWindow: the root window that fills the main viewport, as a typed element.
export module imlui.ui.elements.application_window;

import std;

import imgui;
import imlui.color;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.layout_box;

export namespace imlui {

/// The root element: fills the native window without decoration and stacks its children. Non-visual.
///
/// Props:   title = unset (the native window title, applied every frame while the prop exists),
///          color = unset (surface), padding = 24, scrollable = true
/// Raw:     menuBar (a MenuBar), spacing, align; width, height (the initial window size, read by the engine)
///
/// Without a `title` prop, the last ui.setTitle() (or "imlui") sticks.
class ApplicationWindow final : public Element {
public:
    static void declare(TypeBuilder<ApplicationWindow>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& title() const;
    void setTitle(std::string title);
    /// The window background; unset: the theme's.
    [[nodiscard]] std::optional<Color> color() const;
    void setColor(std::optional<Color> color);
    [[nodiscard]] float padding() const; ///< dp
    void setPadding(float padding);
    [[nodiscard]] bool scrollable() const;
    void setScrollable(bool scrollable);

private:
    std::string m_title{"imlui"};
    std::optional<Color> m_color;
    float m_padding = 24.0f;
    bool m_scrollable = true;
};

} // namespace imlui

module :private;

namespace imlui {

void ApplicationWindow::declare(TypeBuilder<ApplicationWindow>& type) {
    type.property("title", &ApplicationWindow::title, &ApplicationWindow::setTitle)
        .property("color", &ApplicationWindow::color, &ApplicationWindow::setColor)
        .property("padding", &ApplicationWindow::padding, &ApplicationWindow::setPadding)
        .property("scrollable", &ApplicationWindow::scrollable, &ApplicationWindow::setScrollable)
        .nonVisual();
}

void ApplicationWindow::render(Frame& frame) {
    // A `title` prop wins every frame; without one, ui.setTitle() (or the default) sticks.
    if (frame.node().has("title")) frame.setWindowTitle(m_title);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    const bool menu = frame.node().has("menuBar");
    if (menu) flags |= ImGuiWindowFlags_MenuBar;
    if (!m_scrollable) flags |= ImGuiWindowFlags_NoScrollbar;
    const float pad = frame.dp(m_padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {pad, pad});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (m_color) ImGui::PushStyleColor(ImGuiCol_WindowBg, m_color->vec4());
    ImGui::Begin("##ApplicationWindow", nullptr, flags);
    if (m_color) ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (menu) frame.renderElementProp("menuBar");
    {
        const layout::BoxScope box{layout::regionBox(), layout::BoxScope::Mirror::None};
        frame.renderStack();
    }
    ImGui::End();
}

const std::string& ApplicationWindow::title() const { return m_title; }
void ApplicationWindow::setTitle(std::string title) { m_title = std::move(title); }
std::optional<Color> ApplicationWindow::color() const { return m_color; }
void ApplicationWindow::setColor(std::optional<Color> color) { m_color = color; }
float ApplicationWindow::padding() const { return m_padding; }
void ApplicationWindow::setPadding(float padding) { m_padding = padding; }
bool ApplicationWindow::scrollable() const { return m_scrollable; }
void ApplicationWindow::setScrollable(bool scrollable) { m_scrollable = scrollable; }

} // namespace imlui
