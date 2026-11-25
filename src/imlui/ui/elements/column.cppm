// Column (alias Item): children stacked top to bottom, as a typed element.
export module imlui.ui.elements.column;

import std;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.item_size;
import imlui.ui.layout_box;
import imlui.ui.renderer;

export namespace imlui {

/// A vertical stack (also registered as Item).
///
/// Props:   padding = 0 (dp on every side), spacing = unset (ImGui item spacing), align = "start" ("center", "end")
/// Raw:     width (the column's width, padding included, like a Card's; the children's layout box (`align`, fills,
///          wrapping) is that less the padding on each side; unset: the parent's box less the padding on each side,
///          and a column holding a fill fills itself: contentFillsWidth)
///          cull (default true: children out of view are skipped, Renderer::skipOutOfView)
class Column final : public Element {
public:
    static void declare(TypeBuilder<Column>& type);

    void render(Frame& frame) override;
    /// Its stack (Frame::stackSize) and padding, when every visual child has an intrinsic size.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;
    /// It holds a child that fills (and has no `width`): it is as wide as the available width.
    [[nodiscard]] bool contentFillsWidth(const Frame& frame) override;

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
    const float pad = std::max(0.0f, frame.dp(m_padding));
    const bool sized = frame.size("width") != 0;
    if (!sized && pad <= 0) {
        frame.renderStack(); // the parent's box
        return;
    }
    // The width is the column's own, padding included: resolved from its left edge, before the padding indents.
    const float left = ImGui::GetCursorScreenPos().x;
    const float width = sized ? frame.resolvedWidth(0.0f) : 0.0f;
    const float lineGap = ImGui::GetStyle().ItemSpacing.y;
    if (pad > 0) {
        // The top padding: the children start `pad` below. Not a Dummy `pad - lineGap` high, which can't be negative
        // (a padding below the item spacing would pad by the spacing): an empty one starts a line (and ends a
        // SameLine), then the cursor moves to a whole pixel `pad` below the column's top.
        const float y = ImGui::GetCursorScreenPos().y;
        ImGui::Dummy({0, 0});
        ImGui::Indent(pad);
        ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, std::trunc(y + pad)});
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float right = p.x; // the content's right edge
    {
        layout::Box inner = layout::insetBox(pad); // the padding on the right (still shrink-to-fit in a Dialog)
        if (sized) {
            // A fixed or fill width: reserve it (the column is at least that wide), and its content is the
            // children's box.
            const float content = std::max(0.0f, width - 2.0f * pad);
            ImGui::Dummy({content, 0});
            ImGui::SetCursorScreenPos(p);
            inner = layout::boxTo(p.x + content);
            right = p.x + content;
        }
        const layout::BoxScope box{inner};
        right = std::max(right, frame.renderer().renderStack(frame.node()));
    }
    if (pad > 0) {
        ImGui::Unindent(pad);
        // The bottom padding, `pad` below the last child (the cursor is the item spacing below it) or below the top
        // padding without one, and as wide as the content and the padding on its right: the column's group (its
        // size) covers the padding on every side.
        const ImVec2 c = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({c.x, std::max(p.y, c.y - lineGap)});
        ImGui::Dummy({right + pad - left, pad});
    }
}

std::optional<ImVec2> Column::intrinsicSize(const Frame& frame, float maxWidth) {
    // render() in numbers: the children's box, the stack in it, and the padding (ImGui starts each line on a whole
    // pixel).
    const float pad = std::max(0.0f, frame.dp(m_padding));
    const float width = frame.size("width");
    float reserved = 0.0f, inner = std::max(0.0f, maxWidth - 2.0f * pad);
    if (width != 0) inner = reserved = std::max(0.0f, resolveSize(width, maxWidth, 0.0f) - 2.0f * pad);
    const std::optional<ImVec2> stack = frame.stackSize(inner);
    if (!stack) return std::nullopt;
    // Without padding, width or children, its group holds no item: ImGui measures it by whatever came before.
    if (pad <= 0 && width == 0 && stack->x == 0.0f && stack->y == 0.0f) return std::nullopt;
    const float w = std::max(stack->x, reserved) + 2.0f * pad;
    if (pad <= 0) return ImVec2{w, stack->y};
    const float lineGap = ImGui::GetStyle().ItemSpacing.y;
    const float top = std::trunc(pad);
    const float stackBottom = top + stack->y;
    // Where the bottom padding starts: the line after the stack, less the item spacing, or the top padding's end.
    // The group ends at its dummy's bottom.
    const float contentBottom = std::max(top, std::trunc(stackBottom + lineGap) - lineGap);
    return ImVec2{w, std::max(stackBottom, contentBottom + pad)};
}

bool Column::contentFillsWidth(const Frame& frame) {
    bool fills = false;
    frame.forEachChild([&](Frame::Child& c) { fills = fills || (c.isVisual() && c.fillsWidth()); });
    return fills;
}

float Column::padding() const { return m_padding; }
void Column::setPadding(float padding) { m_padding = padding; }
std::optional<double> Column::spacing() const { return m_spacing; }
void Column::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
const std::string& Column::align() const { return m_align; }
void Column::setAlign(std::string align) { m_align = std::move(align); }

} // namespace imlui
