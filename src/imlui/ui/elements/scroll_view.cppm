// ScrollView (alias ListView): a scrolling child window, as a typed element (TypeBuilder). ListView's children
// come from its model/delegate through the Lua-side `__children` function, which every child iteration uses.
//
// The scroll position belongs to the element, not to the ImGui window: ImGui keys a child window by its ID
// path, and moving the element elsewhere in the tree (Responsive picking another layout for a size class)
// gives it a new path, so ImGui would start the new window at the top. The element remembers its scroll and
// puts it back when its window appears at another path.
module;

#include <optional>
#include <string>
#include <utility>

export module imlui.ui.elements.scroll_view;

import imgui;
import imlui.color;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// ScrollView { width = 0, height = 0, padding = 0, color = nil, rounding = theme medium, border = false,
///              spacing = nil, align = "start", children... }
class ScrollView final : public Element {
public:
    static void declare(TypeBuilder<ScrollView>& type);

    void render(Frame& frame) override;

    [[nodiscard]] float padding() const; ///< dp inside the window
    void setPadding(float padding);
    [[nodiscard]] std::optional<Color> color() const; ///< background; unset is transparent
    void setColor(std::optional<Color> color);
    [[nodiscard]] std::optional<float> rounding() const; ///< dp; unset uses the theme's medium shape
    void setRounding(std::optional<float> rounding);
    [[nodiscard]] bool border() const;
    void setBorder(bool border);
    /// Gap between children (dp); unset uses ImGui's item spacing. Frame::renderStack() applies it.
    [[nodiscard]] std::optional<double> spacing() const;
    void setSpacing(std::optional<double> spacing);
    /// Horizontal alignment of the children. Frame::renderStack() applies it.
    [[nodiscard]] const std::string& align() const;
    void setAlign(std::string align);

    /// The scroll offset in pixels when the element was last rendered.
    [[nodiscard]] ImVec2 scroll() const;

private:
    float m_padding = 0.0f;
    std::optional<Color> m_color;
    std::optional<float> m_rounding;
    bool m_border = false;
    std::optional<double> m_spacing;
    std::string m_align = "start";

    ImVec2 m_scroll{0.0f, 0.0f}; ///< what the user scrolled to
    ImGuiID m_windowId = 0;      ///< the child window's ID last frame (0: never rendered)
    int m_lastFrame = -1;        ///< ImGui frame of the last render
    int m_restoreFrames = 0;     ///< frames left to put m_scroll back into a new window
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// A new window knows its content size only after its first frame, and ImGui clamps the scroll to it, so the
/// restore is repeated for a few frames (until it sticks).
constexpr int restoreAttempts = 4;

bool near(float a, float b) { return a - b < 0.5f && b - a < 0.5f; }

} // namespace

void ScrollView::declare(TypeBuilder<ScrollView>& type) {
    type.property("padding", &ScrollView::padding, &ScrollView::setPadding)
        .property("color", &ScrollView::color, &ScrollView::setColor)
        .property("rounding", &ScrollView::rounding, &ScrollView::setRounding)
        .property("border", &ScrollView::border, &ScrollView::setBorder)
        .property("spacing", &ScrollView::spacing, &ScrollView::setSpacing)
        .property("align", &ScrollView::align, &ScrollView::setAlign);
}

void ScrollView::render(Frame& frame) {
    ImGuiChildFlags cf = m_border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    const float pad = m_padding;
    const float rounding = m_rounding.value_or(frame.theme().shape.medium);
    if (pad > 0) {
        cf |= ImGuiChildFlags_AlwaysUseWindowPadding;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {frame.dp(pad), frame.dp(pad)});
    }
    if (m_color) ImGui::PushStyleColor(ImGuiCol_ChildBg, m_color->vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, frame.dp(rounding));

    // Scroll tracking. Only the first render in a frame counts: the same element shown twice in one frame
    // has two windows, and restoring between them would never settle.
    const int frameNo = ImGui::GetFrameCount();
    const bool track = frameNo != m_lastFrame;
    if (track) {
        const ImGuiID id = ImGui::GetID("##scroll"); // what BeginChild names the window
        if (m_windowId != 0 && id != m_windowId) m_restoreFrames = restoreAttempts;
        m_windowId = id;
        m_lastFrame = frameNo;
        if (m_restoreFrames > 0) ImGui::SetNextWindowScroll(m_scroll);
    }

    ImGui::BeginChild("##scroll", {frame.size("width"), frame.size("height")}, cf);
    ImGui::PopStyleVar();
    if (m_color) ImGui::PopStyleColor();
    if (pad > 0) ImGui::PopStyleVar();

    if (track) {
        const ImVec2 now{ImGui::GetScrollX(), ImGui::GetScrollY()};
        if (m_restoreFrames > 0) {
            // Restoring: keep m_scroll until the window took it (or the content got too short for it).
            --m_restoreFrames;
            if (near(now.x, m_scroll.x) && near(now.y, m_scroll.y))
                m_restoreFrames = 0;
            else if (m_restoreFrames > 0)
                frame.requestFrame(); // frames are on demand: the next attempt needs one
            else
                m_scroll = now;
        } else {
            m_scroll = now;
        }
    }

    if (m_color) md::pushSurface(*m_color);
    frame.renderStack();
    if (m_color) md::popSurface();
    ImGui::EndChild();
}

float ScrollView::padding() const { return m_padding; }
void ScrollView::setPadding(float padding) { m_padding = padding; }
std::optional<Color> ScrollView::color() const { return m_color; }
void ScrollView::setColor(std::optional<Color> color) { m_color = color; }
std::optional<float> ScrollView::rounding() const { return m_rounding; }
void ScrollView::setRounding(std::optional<float> rounding) { m_rounding = rounding; }
bool ScrollView::border() const { return m_border; }
void ScrollView::setBorder(bool border) { m_border = border; }
std::optional<double> ScrollView::spacing() const { return m_spacing; }
void ScrollView::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
const std::string& ScrollView::align() const { return m_align; }
void ScrollView::setAlign(std::string align) { m_align = std::move(align); }
ImVec2 ScrollView::scroll() const { return m_scroll; }

} // namespace imlui
