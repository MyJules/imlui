// Separator: a Divider, or with `text` a titleSmall heading followed by a rule, as a typed element.
module;

#include <string>

export module imlui.ui.elements.separator;

import imgui;
import imlui.theme.typography;
import imlui.theme.theme;
import imlui.theme.font_scope;
import imlui.ui.elements.divider;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Separator { text | positional string, inset, insetEnd }: without text, a Divider; with text, the text in
/// titleSmall onSurfaceVariant followed by a rule (the insets apply to the plain rule only).
class Separator final : public Divider {
public:
    static void declare(TypeBuilder<Separator>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);

private:
    std::string m_text;
};

} // namespace imlui

module :private;

namespace imlui {

void Separator::declare(TypeBuilder<Separator>& type) {
    type.extends<Divider>();
    type.property("text", &Separator::text, &Separator::setText).defaultProperty("text");
}

void Separator::render(Frame& frame) {
    if (m_text.empty()) {
        Divider::render(frame);
        return;
    }
    const FontScope font{TextStyle::TitleSmall};
    ImGui::PushStyleColor(ImGuiCol_Text, theme().scheme.onSurfaceVariant.vec4());
    ImGui::SeparatorText(m_text.c_str());
    ImGui::PopStyleColor();
}

const std::string& Separator::text() const { return m_text; }
void Separator::setText(std::string text) { m_text = std::move(text); }

} // namespace imlui
