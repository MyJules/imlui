// Column (alias Item): children stacked top to bottom, as a typed element.
export module imlui.ui.elements.column;

import std;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A vertical stack (also registered as Item).
///
/// Props:   padding = 0, spacing = unset (ImGui item spacing), align = "start" ("center", "end")
/// Raw:     width (a fixed width is the children's available width: `align`, fills and wrapping use it)
class Column final : public Element {
public:
    static void declare(TypeBuilder<Column>& type);

    void render(Frame& frame) override;

    [[nodiscard]] float padding() const; ///< dp around the content
    void setPadding(float padding);
    /// Gap between children (dp); unset uses ImGui's item spacing. Frame::renderStack() applies it.
    [[nodiscard]] std::optional<double> spacing() const;
    void setSpacing(std::optional<double> spacing);
    /// Horizontal alignment of the children: "start" | "center" | "end". Frame::renderStack() applies it.
    [[nodiscard]] const std::string& align() const;
    void setAlign(std::string align);

private:
    float m_padding = 0.0f;
    std::optional<double> m_spacing;
    std::string m_align = "start";
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Moves the right edge of the current window's content region (its WorkRect, and its ContentRegionRect, which
/// GetContentRegionAvail() reads outside tables) to `right` for its lifetime. ImGui has no public API for it;
/// tables narrow the same rects for their cells. The window recomputes both at its next Begin(), so a Lua error
/// that unwinds past the guard can't leak the narrowing into the next frame.
class ContentRegionGuard {
public:
    explicit ContentRegionGuard(float right)
        : m_window{ImGui::GetCurrentWindow()}, m_work{m_window->WorkRect.Max.x},
          m_content{m_window->ContentRegionRect.Max.x} {
        m_window->WorkRect.Max.x = right;
        m_window->ContentRegionRect.Max.x = right;
    }
    ~ContentRegionGuard() {
        m_window->WorkRect.Max.x = m_work;
        m_window->ContentRegionRect.Max.x = m_content;
    }
    ContentRegionGuard(const ContentRegionGuard&) = delete;
    ContentRegionGuard& operator=(const ContentRegionGuard&) = delete;
    ContentRegionGuard(ContentRegionGuard&&) = delete;
    ContentRegionGuard& operator=(ContentRegionGuard&&) = delete;

private:
    ImGuiWindow* m_window;
    float m_work;
    float m_content;
};

} // namespace

void Column::declare(TypeBuilder<Column>& type) {
    type.property("padding", &Column::padding, &Column::setPadding)
        .property("spacing", &Column::spacing, &Column::setSpacing)
        .property("align", &Column::align, &Column::setAlign);
}

void Column::render(Frame& frame) {
    const float pad = m_padding;
    if (pad > 0) {
        ImGui::Dummy({0, frame.dp(pad) - ImGui::GetStyle().ItemSpacing.y});
        ImGui::Indent(frame.dp(pad));
    }
    const float w = frame.size("width");
    if (w != 0) {
        // A fixed-width column: reserve the width, and narrow the window's content region to it while the
        // children render, so everything that measures the available width (GetContentRegionAvail: `align`,
        // fills, md::itemWidth, text wrapping) measures the column instead of the window.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float ww = frame.resolvedWidth(0.0f);
        ImGui::Dummy({ww, 0});
        ImGui::SetCursorScreenPos(p);
        const ContentRegionGuard region{p.x + ww};
        ImGui::PushItemWidth(ww);
        frame.renderStack();
        ImGui::PopItemWidth();
    } else {
        frame.renderStack();
    }
    if (pad > 0) {
        ImGui::Unindent(frame.dp(pad));
        ImGui::Dummy({0, frame.dp(pad) - ImGui::GetStyle().ItemSpacing.y});
    }
}

float Column::padding() const { return m_padding; }
void Column::setPadding(float padding) { m_padding = padding; }
std::optional<double> Column::spacing() const { return m_spacing; }
void Column::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
const std::string& Column::align() const { return m_align; }
void Column::setAlign(std::string align) { m_align = std::move(align); }

} // namespace imlui
