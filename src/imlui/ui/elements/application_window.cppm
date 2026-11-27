// ApplicationWindow: the root window that fills the main viewport, as a typed element.
export module imlui.ui.elements.application_window;

import std;

import imgui;
import imlui.color;
import imlui.image.image_cache;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.stack_props;
import imlui.ui.layout_box;

export namespace imlui {

/// The root element: fills the native window without decoration and stacks its children. Non-visual.
///
/// Props:   title = unset (the native window title, applied every frame while the prop exists),
///          color = unset (surface), padding = 24, scrollable = true,
///          imageCacheBudget = unset (bytes of decoded image pixels the image cache keeps, ImageCache::setBudget;
///          0: no limit; applied every frame while the prop exists, else the cache's 256 MiB default stands)
/// Raw:     menuBar (a MenuBar), spacing, align, cull; width, height (the initial window size, read by the engine)
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
    /// Bytes; 0: no limit. A negative value reads as 0.
    [[nodiscard]] std::int64_t imageCacheBudget() const;
    void setImageCacheBudget(std::int64_t bytes);

private:
    std::string m_title{"imlui"};
    std::optional<Color> m_color;
    float m_padding = 24.0f;
    bool m_scrollable = true;
    std::int64_t m_imageCacheBudget = static_cast<std::int64_t>(ImageCache::defaultBudget);
};

} // namespace imlui

module :private;

namespace imlui {

void ApplicationWindow::declare(TypeBuilder<ApplicationWindow>& type) {
    type.doc("The root element: fills the native window, with no decoration, and stacks its children. Non-visual.\n"
             "Raw `width` / `height`: the native window's size in dp, applied at load when both are > 0.")
        .property("title", &ApplicationWindow::title, &ApplicationWindow::setTitle)
        .doc("The native window's title, applied every frame while the prop exists (so it can be a binding). "
             "Without it, the last `ui.setTitle()` sticks, else `\"imlui\"`.")
        .property("color", &ApplicationWindow::color, &ApplicationWindow::setColor)
        .doc("The background. Unset: the theme's window background (`surface`).")
        .property("padding", &ApplicationWindow::padding, &ApplicationWindow::setPadding)
        .doc("The content padding (dp), 24 by default.")
        .property("scrollable", &ApplicationWindow::scrollable, &ApplicationWindow::setScrollable)
        .doc("`false` hides the scrollbar.")
        .property("imageCacheBudget", &ApplicationWindow::imageCacheBudget, &ApplicationWindow::setImageCacheBudget)
        .doc("Bytes of decoded image pixels (4 per pixel) the image cache keeps; `0`: no limit (a negative value reads "
             "as 0). Applied every frame while the prop exists; without it the cache keeps its default, 256 MiB. Over "
             "the budget, the images drawn least recently are evicted and decoded again when next drawn.")
        .rawProperty("menuBar", "Element|false")
        .doc("A `MenuBar { ... }` at the top of the window.")
        .nonVisual();
    rawStackProps(type);
}

void ApplicationWindow::render(Frame& frame) {
    // A `title` prop wins every frame; without one, ui.setTitle() (or the default) sticks.
    if (frame.node().has("title")) frame.setWindowTitle(m_title);
    if (ImageCache* images = frame.images(); images && frame.node().has("imageCacheBudget"))
        images->setBudget(static_cast<std::size_t>(m_imageCacheBudget));
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
    const std::optional<Color> background = m_color ? m_color : skin().applicationBackground();
    if (background) ImGui::PushStyleColor(ImGuiCol_WindowBg, background->vec4());
    ImGui::Begin("##ApplicationWindow", nullptr, flags);
    if (background) ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (menu) frame.renderElementProp("menuBar");
    {
        const layout::BoxScope box{layout::regionBox(), layout::Mirror::None};
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
std::int64_t ApplicationWindow::imageCacheBudget() const { return m_imageCacheBudget; }
void ApplicationWindow::setImageCacheBudget(std::int64_t bytes) { m_imageCacheBudget = std::max<std::int64_t>(bytes, 0); }

} // namespace imlui
