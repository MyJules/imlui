// Separator: a Divider, or with `text` a titleSmall heading followed by a rule, as a typed element.
export module imlui.ui.elements.separator;

import std;

import imgui;
import imlui.theme.typography;
import imlui.theme.theme;
import imlui.theme.font_scope;
import imlui.ui.elements.divider;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.layout_box;

export namespace imlui {

/// A Divider with an optional label: with text, the text in titleSmall onSurfaceVariant followed by a rule.
///
/// Props:   Divider's, and text = "" (positional; the insets apply to the plain rule only)
class Separator final : public Divider {
public:
    static void declare(TypeBuilder<Separator>& type);

    void render(Frame& frame) override;
    /// A plain rule's (Divider); none with a text: ImGui draws that to the window's edge, not the layout box's.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);

private:
    std::string m_text;
};

} // namespace imlui

module :private;

namespace imlui {

void Separator::declare(TypeBuilder<Separator>& type) {
    type.doc("A `Divider` with an optional label: with `text`, the text in `titleSmall` `onSurfaceVariant` "
             "followed by a rule.\n"
             "`Separator { \"Advanced\" }` sets `text`. Without text, a plain `Divider`.")
        .extends<Divider>()
        .docMember("inset", "The start inset (dp) of the plain rule; not applied with `text`.")
        .docMember("insetEnd", "The end inset (dp) of the plain rule; not applied with `text`.");
    type.property("text", &Separator::text, &Separator::setText)
        .doc("The label; `\"\"`: a plain rule. The positional value: `Separator { \"Advanced\" }`.")
        .defaultProperty("text");
}

void Separator::render(Frame& frame) {
    if (m_text.empty()) {
        Divider::render(frame);
        return;
    }
    const FontScope font{TextStyle::TitleSmall};
    ImGui::PushStyleColor(ImGuiCol_Text, theme().scheme.onSurfaceVariant.vec4());
    // ImGui draws the rule to the window's edge: clip it to the layout box's.
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect({p.x, p.y}, {layout::boxRight(), p.y + ImGui::GetFrameHeight() * 2.0f}, true);
    ImGui::SeparatorText(m_text.c_str());
    ImGui::PopClipRect();
    ImGui::PopStyleColor();
}

const std::string& Separator::text() const { return m_text; }
void Separator::setText(std::string text) { m_text = std::move(text); }

std::optional<ImVec2> Separator::intrinsicSize(const Frame& frame, float maxWidth) {
    if (m_text.empty()) return Divider::intrinsicSize(frame, maxWidth);
    return std::nullopt;
}

} // namespace imlui
