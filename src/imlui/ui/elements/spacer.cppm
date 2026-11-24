// Spacer: empty layout space, as a typed element.
module;

export module imlui.ui.elements.spacer;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Empty space.
///
/// Props:   fill = false (in a Row, fill spacers share the remaining width; the Row reads it)
/// Raw:     width, height (sizes: fixed, -1 fill, < -1 fill minus; unset 1px)
class Spacer final : public Element {
public:
    static void declare(TypeBuilder<Spacer>& type);

    void render(Frame& frame) override;

    [[nodiscard]] bool fill() const;
    void setFill(bool fill);

private:
    bool m_fill = false;
};

} // namespace imlui

module :private;

namespace imlui {

void Spacer::declare(TypeBuilder<Spacer>& type) { type.property("fill", &Spacer::fill, &Spacer::setFill); }

void Spacer::render(Frame& frame) {
    // width/height follow the common size rule (resolveSize): fixed, fill, fill minus; unset is 1px.
    ImGui::Dummy({frame.resolvedWidth(1.0f), frame.resolvedHeight(1.0f)});
}

bool Spacer::fill() const { return m_fill; }
void Spacer::setFill(bool fill) { m_fill = fill; }

} // namespace imlui
