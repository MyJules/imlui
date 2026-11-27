// Card: a Material 3 card container (Skin::beginCard), as a typed element.
export module imlui.ui.elements.card;

import std;

import imgui;
import imlui.color;
import imlui.widgets;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.stack_props;
import imlui.ui.layout_box;
import imlui.ui.enum_names;
import imlui.ui.props;

export namespace imlui {

/// A Material card that stacks its children.
///
/// Props:   variant = "elevated" ("filled", "outlined"), padding = 16, color = unset (the variant's),
///          rounding = unset (shape.medium), spacing = unset, align = "start"
/// Raw:     width, height, cull (default true: children out of view are skipped, Renderer::skipOutOfView)
class Card final : public Element {
public:
    static void declare(TypeBuilder<Card>& type);

    void render(Frame& frame) override;
    /// Its width, and around its stack (Frame::stackSize) when no `height` is set.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    /// "elevated" | "filled" | "outlined".
    [[nodiscard]] const std::string& variant() const;
    void setVariant(std::string variant);
    [[nodiscard]] float padding() const; ///< dp
    void setPadding(float padding);
    [[nodiscard]] std::optional<Color> color() const; ///< container color; unset uses the variant's
    void setColor(std::optional<Color> color);
    [[nodiscard]] std::optional<float> rounding() const; ///< dp; unset uses the card shape
    void setRounding(std::optional<float> rounding);
    /// Gap between children (dp). Frame::renderStack() applies it.
    [[nodiscard]] std::optional<double> spacing() const;
    void setSpacing(std::optional<double> spacing);
    /// Horizontal alignment of the children. Frame::renderStack() applies it.
    [[nodiscard]] Align align() const;
    void setAlign(Align align);

private:
    std::string m_variant = "elevated";
    float m_padding = 16.0f;
    std::optional<Color> m_color;
    std::optional<float> m_rounding;
    std::optional<double> m_spacing;
    Align m_align = Align::Start;
};

} // namespace imlui

module :private;

namespace imlui {

void Card::declare(TypeBuilder<Card>& type) {
    type.doc("A Material card that stacks its children.\n"
             "Natural size: fills the available width (raw `width`) and fits its content's height (raw `height`: a "
             "fixed height).");
    type.fillsWidth(); // an unset width fills
    type.property("variant", &Card::variant, &Card::setVariant)
        .doc("`\"elevated\"` (default: `surfaceContainerLow` with a shadow), `\"filled\"` (`surfaceContainerHighest`) "
             "or `\"outlined\"` (`surface` with an `outlineVariant` border).")
        .property("padding", &Card::padding, &Card::setPadding)
        .doc("The inner padding (dp), 16 by default.")
        .property("color", &Card::color, &Card::setColor)
        .doc("The container color. Unset: the variant's.")
        .property("rounding", &Card::rounding, &Card::setRounding)
        .doc("The corner radius (dp). Unset: the card shape (`shape.medium`).")
        .property("spacing", &Card::spacing, &Card::setSpacing)
        .doc("The gap between visible children (dp). Unset: ImGui's item spacing.")
        .property("align", &Card::align, &Card::setAlign)
        .doc("Each child's horizontal position by its width: `\"start\"` (default), `\"center\"` or `\"end\"`.");
    rawCullProp(type);
}

void Card::render(Frame& frame) {
    // Clipped away (scrolled out of view): the content isn't rendered, so its sizes in the layout cache stay what
    // they were, instead of the near-zero sizes items get under SkipItems (settle frames now, a misplaced frame
    // when it scrolls back).
    if (skin().beginCard("##card", md::cardVariant(m_variant), frame.size("width"), m_padding,
                         frame.resolvedHeight(0.0f), m_color, m_rounding.value_or(-1.0f))) {
        const layout::BoxScope box{layout::regionBox(), layout::Mirror::None};
        frame.renderStack();
    }
    skin().endCard();
}

std::optional<ImVec2> Card::intrinsicSize(const Frame& frame, float maxWidth) {
    const float height = frame.size("height");
    if (height < 0) return std::nullopt; // fills a height known only where it renders
    const float width = frame.widthIn(maxWidth, frame.dp(320));
    if (height > 0) return skin().cardSize(width, m_padding, height, 0.0f);
    // The stack renders in the card's content region: its width less the padding on both sides.
    const std::optional<ImVec2> stack = frame.stackSize(std::max(0.0f, width - 2.0f * frame.dp(m_padding)));
    if (!stack) return std::nullopt;
    return skin().cardSize(width, m_padding, 0.0f, stack->y);
}

const std::string& Card::variant() const { return m_variant; }
void Card::setVariant(std::string variant) { m_variant = std::move(variant); }
float Card::padding() const { return m_padding; }
void Card::setPadding(float padding) { m_padding = padding; }
std::optional<Color> Card::color() const { return m_color; }
void Card::setColor(std::optional<Color> color) { m_color = color; }
std::optional<float> Card::rounding() const { return m_rounding; }
void Card::setRounding(std::optional<float> rounding) { m_rounding = rounding; }
std::optional<double> Card::spacing() const { return m_spacing; }
void Card::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
Align Card::align() const { return m_align; }
void Card::setAlign(Align align) { m_align = align; }

} // namespace imlui
