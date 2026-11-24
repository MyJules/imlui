// Spacer: empty layout space, as a typed element.
module;

#include <algorithm>

export module imlui.ui.elements.spacer;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Spacer { width, height (dp, at least 1px), fill = false }. In a Row, `fill = true` spacers share the
/// remaining width (the Row reads it).
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
    // width/height go through the common size path (sizeProp), like every element's sizes.
    ImGui::Dummy({std::max(1.0f, frame.size("width")), std::max(1.0f, frame.size("height"))});
}

bool Spacer::fill() const { return m_fill; }
void Spacer::setFill(bool fill) { m_fill = fill; }

} // namespace imlui
