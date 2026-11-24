// Section (alias Expander): a Material list header that shows or hides its children, as a typed element
//.
export module imlui.ui.elements.section;

import std;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A collapsible header whose children are indented while expanded (also registered as Expander).
///
/// Props:   title = unset (the text), text = "" (positional), supportingText = "", expanded = true
/// Raw:     width
/// Signals: onToggled(self, expanded)
/// Two-way: expanded
class Section final : public Element {
public:
    static void declare(TypeBuilder<Section>& type);

    void render(Frame& frame) override;

    [[nodiscard]] std::optional<std::string> title() const;
    void setTitle(std::optional<std::string> title);
    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] const std::string& supportingText() const;
    void setSupportingText(std::string text);
    [[nodiscard]] bool expanded() const;
    void setExpanded(bool expanded);

private:
    std::optional<std::string> m_title;
    std::string m_text;
    std::string m_supportingText;
    bool m_expanded = true;
};

} // namespace imlui

module :private;

namespace imlui {

void Section::declare(TypeBuilder<Section>& type) {
    type.property("title", &Section::title, &Section::setTitle)
        .property("text", &Section::text, &Section::setText)
        .property("supportingText", &Section::supportingText, &Section::setSupportingText)
        .property("expanded", &Section::expanded, &Section::setExpanded)
        .signal("onToggled")
        .defaultProperty("text");
}

void Section::render(Frame& frame) {
    bool open = m_expanded;
    if (md::expander("##exp", m_title ? *m_title : m_text, m_supportingText, &open, frame.size("width", -1.0f))) {
        m_expanded = open;
        notify("expanded");
        emit("onToggled", open);
    }
    if (open) {
        ImGui::Indent(frame.dp(16));
        frame.renderStack();
        ImGui::Unindent(frame.dp(16));
    }
}

std::optional<std::string> Section::title() const { return m_title; }
void Section::setTitle(std::optional<std::string> title) { m_title = std::move(title); }
const std::string& Section::text() const { return m_text; }
void Section::setText(std::string text) { m_text = std::move(text); }
const std::string& Section::supportingText() const { return m_supportingText; }
void Section::setSupportingText(std::string text) { m_supportingText = std::move(text); }
bool Section::expanded() const { return m_expanded; }
void Section::setExpanded(bool expanded) { m_expanded = expanded; }

} // namespace imlui
