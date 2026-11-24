// Divider: a Material 1dp outlineVariant rule, as a typed element. Separator extends it with a text heading.
module;

export module imlui.ui.elements.divider;

import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Divider { inset = 0, insetEnd = 0 (dp) }: a full-width horizontal rule, inset from the start / end.
class Divider : public Element {
public:
    static void declare(TypeBuilder<Divider>& type);

    void render(Frame& frame) override;

    [[nodiscard]] float inset() const; ///< dp
    void setInset(float inset);
    [[nodiscard]] float insetEnd() const; ///< dp
    void setInsetEnd(float insetEnd);

private:
    float m_inset = 0.0f;
    float m_insetEnd = 0.0f;
};

} // namespace imlui

module :private;

namespace imlui {

void Divider::declare(TypeBuilder<Divider>& type) {
    type.property("inset", &Divider::inset, &Divider::setInset)
        .property("insetEnd", &Divider::insetEnd, &Divider::setInsetEnd);
}

void Divider::render(Frame&) { md::divider(m_inset, m_insetEnd); }

float Divider::inset() const { return m_inset; }
void Divider::setInset(float inset) { m_inset = inset; }
float Divider::insetEnd() const { return m_insetEnd; }
void Divider::setInsetEnd(float insetEnd) { m_insetEnd = insetEnd; }

} // namespace imlui
