// Spacer: empty layout space, as a typed element.
export module imlui.ui.elements.spacer;

import std;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Empty space.
///
/// Props:   fill = false (fills the available width: in a Row, fill spacers share the remaining width)
/// Raw:     width, height (sizes: fixed, -1 fill, < -1 fill minus; unset 1px)
class Spacer final : public Element {
public:
    static void declare(TypeBuilder<Spacer>& type);

    void render(Frame& frame) override;
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] bool fill() const;
    void setFill(bool fill);

private:
    bool m_fill = false;
};

} // namespace imlui

module :private;

namespace imlui {

void Spacer::declare(TypeBuilder<Spacer>& type) {
    type.doc("Empty space: `width` and `height` (fixed, fill or fill minus, like every size; unset: 1px), or "
             "`fill`.\n"
             "`Row { Text { \"Left\" }, Spacer { fill = true }, Text { \"Right\" } }` pushes the texts apart.")
        .property("fill", &Spacer::fill, &Spacer::setFill)
        .doc("Fills the available width when `width` is unset: in a `Row`, a share of the remaining width.")
        .fillsWidthWhen(&Spacer::fill);
}

void Spacer::render(Frame& frame) {
    // width/height follow the common size rule (resolveSize): fixed, fill, fill minus; unset is 1px, or with `fill`
    // (the type's fill policy, fillsWidthWhen) the available width (in a Row, the spacer's share).
    ImGui::Dummy({frame.resolvedWidth(1.0f), frame.resolvedHeight(1.0f)});
}

std::optional<ImVec2> Spacer::intrinsicSize(const Frame& frame, float maxWidth) {
    // Fixed, unset (1px) or a fill of `maxWidth`; a fill height depends on where it renders.
    const float h = frame.size("height");
    if (h < 0.0f) return std::nullopt;
    return ImVec2{frame.widthIn(maxWidth, 1.0f), h > 0.0f ? h : 1.0f};
}

bool Spacer::fill() const { return m_fill; }
void Spacer::setFill(bool fill) { m_fill = fill; }

} // namespace imlui
