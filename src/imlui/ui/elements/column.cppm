// Column (alias Item): children stacked top to bottom, as a typed element.
export module imlui.ui.elements.column;

import std;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.lua_convert;
import imlui.types.type_builder;
import imlui.ui.elements.stack_props;
import imlui.ui.item_size;
import imlui.ui.layout_box;
import imlui.ui.renderer;
import imlui.ui.enum_names;
import imlui.ui.props;

export namespace imlui {

/// A vertical stack (also registered as Item).
///
/// Props:   padding = 0 (dp on every side), paddingLeft, paddingTop, paddingRight, paddingBottom (unset: `padding`),
///          spacing = unset (ImGui item spacing), align = "start" ("center", "end"),
///          justify = "start" ("center", "end", "spaceBetween"; with a `height`, while no child fills the height)
/// Raw:     width (the column's width, padding included, like a Card's; the children's layout box (`align`, fills,
///          wrapping) is that less the padding on each side; unset: the parent's box less the padding on each side,
///          and a column holding a fill fills itself: contentFillsWidth)
///          height (the column's height, padding included: fixed, or -1 for the height left below; the children with a
///          fill height share what the others leave, else `justify` distributes it; unset: its content's)
///          cull (default true: children out of view are skipped, Renderer::skipOutOfView)
class Column final : public Element {
public:
    /// The vertical distribution of the children in the column's height, while none fills it.
    enum class Justify { Start, Center, End, SpaceBetween };

    static void declare(TypeBuilder<Column>& type);

    void render(Frame& frame) override;
    /// Its stack (Frame::stackSize) and padding, when every visual child has an intrinsic size; with a fixed `height`,
    /// that height. None for a fill height.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;
    /// It holds a child that fills (and has no `width`): it is as wide as the available width.
    [[nodiscard]] bool contentFillsWidth(const Frame& frame) override;

    [[nodiscard]] float padding() const; ///< dp around the content
    void setPadding(float padding);
    /// One side's padding (dp); unset: `padding`.
    [[nodiscard]] std::optional<double> paddingLeft() const;
    void setPaddingLeft(std::optional<double> v);
    [[nodiscard]] std::optional<double> paddingTop() const;
    void setPaddingTop(std::optional<double> v);
    [[nodiscard]] std::optional<double> paddingRight() const;
    void setPaddingRight(std::optional<double> v);
    [[nodiscard]] std::optional<double> paddingBottom() const;
    void setPaddingBottom(std::optional<double> v);
    /// Gap between children (dp); unset uses ImGui's item spacing. Frame::renderStack() applies it.
    [[nodiscard]] std::optional<double> spacing() const;
    void setSpacing(std::optional<double> spacing);
    /// Horizontal alignment of the children: "start" | "center" | "end". Frame::renderStack() applies it.
    [[nodiscard]] Align align() const;
    void setAlign(Align align);
    [[nodiscard]] Justify justify() const;
    void setJustify(Justify justify);

private:
    /// The padding on each side, in pixels.
    struct Insets {
        float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
        [[nodiscard]] bool any() const { return left > 0 || top > 0 || right > 0 || bottom > 0; }
    };
    [[nodiscard]] Insets insets(const Frame& frame) const;
    /// How the stack distributes a content height `contentH` among the children (expected heights, read before they
    /// render: the layout cache gets another frame when one was stale).
    [[nodiscard]] Renderer::StackFlow flow(const Frame& frame, float contentH) const;

    float m_padding = 0.0f;
    std::optional<double> m_paddingLeft, m_paddingTop, m_paddingRight, m_paddingBottom;
    std::optional<double> m_spacing;
    Align m_align = Align::Start;
    Justify m_justify = Justify::Start;
};

} // namespace imlui

template <>
struct imlui::EnumNames<imlui::Column::Justify> {
    static constexpr std::string_view name = "ColumnJustify";
    static constexpr std::string_view doc = "A Column's vertical distribution of its children in its height.";
    static constexpr std::pair<imlui::Column::Justify, std::string_view> values[] = {
        {imlui::Column::Justify::Start, "start"},
        {imlui::Column::Justify::Center, "center"},
        {imlui::Column::Justify::End, "end"},
        {imlui::Column::Justify::SpaceBetween, "spaceBetween"},
    };
};

module :private;

namespace imlui {

void Column::declare(TypeBuilder<Column>& type) {
    type.doc("A vertical stack (`Item` is an alias of `Column`).\n"
             "Raw `width`: the column's own width, padding included (its children get it less the padding); unset, a "
             "column holding a child that fills fills the available width itself. Raw `height`: the column's own "
             "height, padding included, fixed or `-1` for the height left below; the children with a fill height "
             "(`height = -1`) share what the others leave, else `justify` places them in it. Natural size: its "
             "children's, stacked, plus the padding.")
        .property("padding", &Column::padding, &Column::setPadding)
        .doc("The inset on every side (dp), 0 by default; the column's size includes it on both sides.")
        .property("paddingLeft", &Column::paddingLeft, &Column::setPaddingLeft)
        .doc("The left inset (dp). Unset: `padding`.")
        .property("paddingTop", &Column::paddingTop, &Column::setPaddingTop)
        .doc("The top inset (dp). Unset: `padding`.")
        .property("paddingRight", &Column::paddingRight, &Column::setPaddingRight)
        .doc("The right inset (dp). Unset: `padding`.")
        .property("paddingBottom", &Column::paddingBottom, &Column::setPaddingBottom)
        .doc("The bottom inset (dp). Unset: `padding`.")
        .property("spacing", &Column::spacing, &Column::setSpacing)
        .doc("The gap between visible children (dp). Unset: ImGui's item spacing.")
        .property("align", &Column::align, &Column::setAlign)
        .doc("Each child's horizontal position by its width: `\"start\"` (default), `\"center\"` or `\"end\"`. A "
             "child that fills isn't moved.")
        .property("justify", &Column::justify, &Column::setJustify)
        .doc("With a `height`: the children's vertical distribution in it, `\"start\"` (default), `\"center\"`, "
             "`\"end\"` or `\"spaceBetween\"`. Ignored while a child fills the height.");
    rawCullProp(type);
}

Column::Insets Column::insets(const Frame& frame) const {
    const auto side = [&](const std::optional<double>& v) {
        return std::max(0.0f, frame.dp(v ? static_cast<float>(*v) : m_padding));
    };
    return {side(m_paddingLeft), side(m_paddingTop), side(m_paddingRight), side(m_paddingBottom)};
}

Renderer::StackFlow Column::flow(const Frame& frame, float contentH) const {
    const float gap = m_spacing ? frame.dp(static_cast<float>(*m_spacing)) : ImGui::GetStyle().ItemSpacing.y;
    const float availW = layout::availableWidth();
    int count = 0, fills = 0;
    float fixedH = 0.0f;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) return;
        ++count;
        if (sizeProp(c.node(), "height") < 0.0f)
            ++fills;
        else
            fixedH += c.expectedHeight(availW);
    });
    Renderer::StackFlow flow;
    const float gaps = count > 1 ? gap * static_cast<float>(count - 1) : 0.0f;
    const float free = std::max(0.0f, contentH - fixedH - gaps);
    if (fills > 0) {
        // Each fill's share; at least a pixel, so a fill never falls back to the height left in the window.
        flow.fillHeight = std::max(1.0f, free / static_cast<float>(fills));
        return flow;
    }
    switch (m_justify) {
    case Justify::Center: flow.offset = free * 0.5f; break;
    case Justify::End: flow.offset = free; break;
    case Justify::SpaceBetween:
        if (count > 1) flow.extraGap = free / static_cast<float>(count - 1);
        break;
    case Justify::Start: break;
    }
    return flow;
}

void Column::render(Frame& frame) {
    const Insets in = insets(frame);
    const bool sized = frame.size("width") != 0;
    const float hp = frame.size("height");
    const bool tall = hp != 0.0f;
    if (!sized && !tall && !in.any()) {
        frame.renderStack(m_spacing); // the parent's box
        return;
    }
    // The width and height are the column's own, padding included: resolved from its top-left, before the padding.
    const float left = ImGui::GetCursorScreenPos().x;
    const float top = ImGui::GetCursorScreenPos().y;
    const float width = sized ? frame.resolvedWidth(0.0f) : 0.0f;
    const float height = tall ? frame.resolvedHeight(0.0f) : 0.0f;
    const float lineGap = ImGui::GetStyle().ItemSpacing.y;
    if (in.any()) {
        // The top padding: the children start `top` below. Not a Dummy `top - lineGap` high, which can't be negative
        // (a padding below the item spacing would pad by the spacing): an empty one starts a line (and ends a
        // SameLine), then the cursor moves to a whole pixel `top` below the column's top.
        ImGui::Dummy({0, 0});
        if (in.left > 0) ImGui::Indent(in.left);
        ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, std::trunc(top + in.top)});
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float right = p.x; // the content's right edge
    {
        layout::Box inner = layout::insetBox(in.right); // the padding on the right (still shrink-to-fit in a Dialog)
        if (sized) {
            // A fixed or fill width: reserve it (the column is at least that wide), and its content is the
            // children's box.
            const float content = std::max(0.0f, width - in.left - in.right);
            ImGui::Dummy({content, 0});
            ImGui::SetCursorScreenPos(p);
            inner = layout::boxTo(p.x + content);
            right = p.x + content;
        }
        const float contentH = std::max(0.0f, height - in.top - in.bottom);
        if (tall) inner.bottom = p.y + contentH;
        const layout::BoxScope box{inner};
        const Renderer::StackFlow stackFlow = tall ? flow(frame, contentH) : Renderer::StackFlow{};
        right = std::max(right, frame.renderer().renderStack(frame.node(), m_spacing, stackFlow));
        if (tall) {
            // The column is as tall as its height (its content may still be taller): an empty item at the content's
            // bottom extends it there.
            ImGui::SetCursorScreenPos({p.x, std::max(ImGui::GetCursorScreenPos().y - lineGap, p.y + contentH)});
            ImGui::Dummy({0, 0});
        }
    }
    if (in.any()) {
        if (in.left > 0) ImGui::Unindent(in.left);
        // The bottom padding, `bottom` below the last child (the cursor is the item spacing below it) or below the
        // top padding without one, and as wide as the content and the padding on its right: the column's group (its
        // size) covers the padding on every side.
        const ImVec2 c = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({c.x, std::max(p.y, c.y - lineGap)});
        ImGui::Dummy({right + in.right - left, in.bottom});
    }
}

std::optional<ImVec2> Column::intrinsicSize(const Frame& frame, float maxWidth) {
    // render() in numbers: the children's box, the stack in it, and the padding (ImGui starts each line on a whole
    // pixel).
    const float hp = frame.size("height");
    if (hp < 0.0f) return std::nullopt; // a fill height depends on where it renders
    const Insets in = insets(frame);
    const float width = frame.size("width");
    float reserved = 0.0f, inner = std::max(0.0f, maxWidth - in.left - in.right);
    if (width != 0) inner = reserved = std::max(0.0f, resolveSize(width, maxWidth, 0.0f) - in.left - in.right);
    const std::optional<ImVec2> stack = frame.stackSize(inner, m_spacing);
    if (!stack) return std::nullopt;
    const float w = std::max(stack->x, reserved) + in.left + in.right;
    // A fixed height, padding included, unless the content is taller.
    if (hp > 0.0f) return ImVec2{w, std::max(hp, std::trunc(in.top) + stack->y + in.bottom)};
    // Without padding, width or children, its group holds no item: ImGui measures it by whatever came before.
    if (!in.any() && width == 0 && stack->x == 0.0f && stack->y == 0.0f) return std::nullopt;
    if (!in.any()) return ImVec2{w, stack->y};
    const float lineGap = ImGui::GetStyle().ItemSpacing.y;
    const float top = std::trunc(in.top);
    const float stackBottom = top + stack->y;
    // Where the bottom padding starts: the line after the stack, less the item spacing, or the top padding's end.
    // The group ends at its dummy's bottom.
    const float contentBottom = std::max(top, std::trunc(stackBottom + lineGap) - lineGap);
    return ImVec2{w, std::max(stackBottom, contentBottom + in.bottom)};
}

bool Column::contentFillsWidth(const Frame& frame) {
    bool fills = false;
    frame.forEachChild([&](Frame::Child& c) { fills = fills || (c.isVisual() && c.fillsWidth()); });
    return fills;
}

float Column::padding() const { return m_padding; }
void Column::setPadding(float padding) { m_padding = padding; }
std::optional<double> Column::paddingLeft() const { return m_paddingLeft; }
void Column::setPaddingLeft(std::optional<double> v) { m_paddingLeft = v; }
std::optional<double> Column::paddingTop() const { return m_paddingTop; }
void Column::setPaddingTop(std::optional<double> v) { m_paddingTop = v; }
std::optional<double> Column::paddingRight() const { return m_paddingRight; }
void Column::setPaddingRight(std::optional<double> v) { m_paddingRight = v; }
std::optional<double> Column::paddingBottom() const { return m_paddingBottom; }
void Column::setPaddingBottom(std::optional<double> v) { m_paddingBottom = v; }
std::optional<double> Column::spacing() const { return m_spacing; }
void Column::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
Align Column::align() const { return m_align; }
void Column::setAlign(Align align) { m_align = align; }
Column::Justify Column::justify() const { return m_justify; }
void Column::setJustify(Justify justify) { m_justify = justify; }

} // namespace imlui
