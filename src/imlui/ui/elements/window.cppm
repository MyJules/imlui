// Window: a floating ImGui window (movable, resizable, closable), as a typed element.
export module imlui.ui.elements.window;

import std;

import imgui;
import imlui.skin;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.stack_props;
import imlui.ui.layout_box;
import imlui.ui.enum_names;

export namespace imlui {

/// A floating ImGui window with Material surface colors that stacks its children. Non-visual.
///
/// Props:   title = "Window", open = true, x = 60, y = 60, width = 0, height = 0, autoResize = unset,
///          titleBar = true, resizable = true, movable = true, align = "start" ("center", "end"; the children's,
///          StackAlign)
/// Raw:     menuBar (a MenuBar), spacing, cull
/// Signals: onClosed(self)
/// Two-way: open
///
/// It has a close button when the `open` prop exists (closing writes open = false, then emits onClosed). x, y,
/// width and height (dp) apply when the window first appears; `autoResize` defaults to true when neither width
/// nor height is set. An auto-resized window takes its content's size, at least width x height; a fill inside it
/// is `width` wide (240dp when unset), never the window's current width, so the window shrinks back with its
/// content.
class Window final : public Element, public StackAlign {
public:
    /// The width of a fill in an auto-resized window without a `width` (dp).
    static constexpr float defaultFillWidth = 240.0f;

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
    /// The window's name (md::scopedWindowName: the title, and an ID from the element's scope), made again only when
    /// the title or the scope (m_nameScope, the ID it hashes) changed: formatting it every frame allocates.
    std::string m_name;
    std::string m_nameTitle;
    ImGuiID m_nameScope = 0;
};

} // namespace imlui

module :private;

namespace imlui {

void Window::declare(TypeBuilder<Window>& type) {
    type.doc("A floating ImGui window with Material surface colors (`surfaceContainerLow`, an `outlineVariant` "
             "border) that stacks its children. Non-visual.\n"
             "Each `Window` element is its own ImGui window: it keeps its position and size when the title changes.")
        .property("open", &Window::open, &Window::setOpen)
        .doc("Two-way. `false` hides the window. While the prop is present the window has a close button, which writes "
             "`open = false` (then `onClosed`).")
        .property("title", &Window::title, &Window::setTitle)
        .doc("The title bar's text, `\"Window\"` by default.")
        .property("x", &Window::x, &Window::setX)
        .doc("The position's x (dp), applied when the window first appears; 60 by default.")
        .property("y", &Window::y, &Window::setY)
        .doc("The position's y (dp), applied when the window first appears; 60 by default.")
        .property("width", &Window::width, &Window::setWidth)
        .doc("The width in dp (a plain number: no fill values), applied when the window first appears if `width` or "
             "`height` is > 0. With `autoResize`, the minimum width; a fill inside is this wide (240dp when unset).")
        .property("height", &Window::height, &Window::setHeight)
        .doc("The height in dp (a plain number), applied when the window first appears if `width` or `height` is > 0. "
             "With `autoResize`, the minimum height.")
        .property("autoResize", &Window::autoResize, &Window::setAutoResize)
        .doc("Fits the window to its content every frame: it grows and shrinks back with it. Unset: `true` when "
             "neither `width` nor `height` is set.")
        .property("titleBar", &Window::titleBar, &Window::setTitleBar)
        .doc("Shows the title bar (`true`).")
        .property("resizable", &Window::resizable, &Window::setResizable)
        .doc("The user can resize the window (`true`).")
        .property("movable", &Window::movable, &Window::setMovable)
        .doc("The user can move the window (`true`).")
        .signal<>("onClosed")
        .doc("Called after the close button wrote `open = false`.")
        .rawProperty("menuBar", "Element|false")
        .doc("A `MenuBar { ... }` below the title bar.")
        .nonVisual();
    stackProps(type);
}

void Window::render(Frame& frame) {
    if (!m_open) return;
    if (const ImGuiID scope = ImGui::GetID("win"); m_name.empty() || scope != m_nameScope || m_title != m_nameTitle) {
        m_name = md::scopedWindowName(m_title, "win"); // its ID part is this same GetID("win")
        m_nameScope = scope;
        m_nameTitle = m_title;
    }
    const std::string& title = m_name;
    ImGui::SetNextWindowPos({frame.dp(m_x), frame.dp(m_y)}, ImGuiCond_FirstUseEver);
    const bool autoResize = m_autoResize.value_or(m_width <= 0 && m_height <= 0);
    if (autoResize) {
        // `width` / `height` are the minimum size.
        ImGui::SetNextWindowSizeConstraints({frame.dp(std::max(0.0f, m_width)), frame.dp(std::max(0.0f, m_height))},
                                            {md::hugeExtent, md::hugeExtent});
    } else if (m_width > 0 || m_height > 0) {
        ImGui::SetNextWindowSize({frame.dp(m_width), frame.dp(m_height)}, ImGuiCond_FirstUseEver);
    }
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    if (autoResize) flags |= ImGuiWindowFlags_AlwaysAutoResize;
    if (!m_titleBar) flags |= ImGuiWindowFlags_NoTitleBar;
    if (!m_resizable) flags |= ImGuiWindowFlags_NoResize;
    if (!m_movable) flags |= ImGuiWindowFlags_NoMove;
    const bool menu = frame.node().has("menuBar");
    if (menu) flags |= ImGuiWindowFlags_MenuBar;
    const bool closable = frame.node().has("open");
    bool keep = true;
    const Skin::WindowChrome chrome = skin().windowChrome();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, chrome.background.vec4());
    ImGui::PushStyleColor(ImGuiCol_TitleBg, chrome.titleBar.vec4());
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, chrome.titleBarActive.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, chrome.borderSize);
    ImGui::PushStyleColor(ImGuiCol_Border, chrome.border.vec4());
    const bool shown = ImGui::Begin(title.c_str(), closable ? &keep : nullptr, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (shown) {
        md::pushSurface(chrome.background);
        if (menu) frame.renderElementProp("menuBar");
        if (autoResize) {
            // The window takes its content's width: a fill is the minimum width (`width`, else defaultFillWidth),
            // not the window's current width, and content may grow up to the viewport's right edge, where text
            // wraps.
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const float pad = ImGui::GetStyle().WindowPadding.x, x = ImGui::GetCursorScreenPos().x;
            const float fill = m_width > 0 ? frame.dp(m_width) - 2.0f * pad : frame.dp(defaultFillWidth);
            const float max = vp->WorkPos.x + vp->WorkSize.x - pad - x;
            const layout::BoxScope box{layout::shrinkToFitBox(fill, max)};
            frame.renderStack();
        } else {
            const layout::BoxScope box{layout::regionBox(), layout::Mirror::None};
            frame.renderStack();
        }
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
