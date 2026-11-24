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
    type.property("fill", &Spacer::fill, &Spacer::setFill).fillsWidthWhen(&Spacer::fill);
}

void Spacer::render(Frame& frame) {
    // width/height follow the common size rule (resolveSize): fixed, fill, fill minus; unset is 1px, or with `fill`
    // (the type's fill policy, fillsWidthWhen) the available width (in a Row, the spacer's share).
    ImGui::Dummy({frame.resolvedWidth(1.0f), frame.resolvedHeight(1.0f)});
}

std::optional<ImVec2> Spacer::intrinsicSize(const Frame& frame, float) {
    // Fixed or unset (1px); a fill is its parent's to give.
    const float w = frame.size("width"), h = frame.size("height");
    if (w < 0.0f || h < 0.0f) return std::nullopt;
    return ImVec2{w > 0.0f ? w : 1.0f, h > 0.0f ? h : 1.0f};
}

bool Spacer::fill() const { return m_fill; }
void Spacer::setFill(bool fill) { m_fill = fill; }

} // namespace imlui
