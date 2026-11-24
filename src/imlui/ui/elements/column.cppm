// Column (alias Item): children stacked top to bottom, as a typed element.
export module imlui.ui.elements.column;

import std;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.layout_box;

export namespace imlui {

/// A vertical stack (also registered as Item).
///
/// Props:   padding = 0, spacing = unset (ImGui item spacing), align = "start" ("center", "end")
/// Raw:     width (the children's layout box: `align`, fills and wrapping use it; unset: the parent's box, less the
///          padding on the right)
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
    const float pad = frame.dp(m_padding);
    if (pad > 0) {
        ImGui::Dummy({0, pad - ImGui::GetStyle().ItemSpacing.y});
        ImGui::Indent(pad);
    }
    const bool sized = frame.size("width") != 0;
    if (sized || pad > 0) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        layout::Box inner = layout::insetBox(pad); // the padding on the right (still shrink-to-fit in a Dialog)
        if (sized) {
            // A fixed or fill width: reserve it (the column is at least that wide), and make it the children's box.
            const float w = frame.resolvedWidth(0.0f);
            ImGui::Dummy({w, 0});
            ImGui::SetCursorScreenPos(p);
            inner = layout::boxTo(p.x + w);
        }
        const layout::BoxScope box{inner};
        frame.renderStack();
    } else {
        frame.renderStack(); // the parent's box
    }
    if (pad > 0) {
        ImGui::Unindent(pad);
        ImGui::Dummy({0, pad - ImGui::GetStyle().ItemSpacing.y});
    }
}

float Column::padding() const { return m_padding; }
void Column::setPadding(float padding) { m_padding = padding; }
std::optional<double> Column::spacing() const { return m_spacing; }
void Column::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
const std::string& Column::align() const { return m_align; }
void Column::setAlign(std::string align) { m_align = std::move(align); }

} // namespace imlui
