// Column (alias Item): children stacked top to bottom, as a typed element.
module;

#include <optional>
#include <string>
#include <utility>

export module imlui.ui.elements.column;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A vertical stack (also registered as Item).
///
/// Props:   padding = 0, spacing = unset (ImGui item spacing), align = "start" ("center", "end")
/// Raw:     width
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
        // A fixed-width column: reserve the width so `align` and fills work inside it.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float ww = frame.resolvedWidth(0.0f);
        ImGui::Dummy({ww, 0});
        ImGui::SetCursorScreenPos(p);
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
