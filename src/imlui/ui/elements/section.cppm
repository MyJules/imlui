// Section (alias Expander): a Material list header that shows or hides its children, as a typed element
//.
export module imlui.ui.elements.section;

import std;

import imgui;
import imlui.widgets;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.stack_props;

export namespace imlui {

/// A collapsible header whose children are indented while expanded (also registered as Expander).
///
/// Props:   title = unset (the text), text = "" (positional), supportingText = "", expanded = true
/// Raw:     width; spacing, align, cull (its children are stacked with them)
/// Signals: onToggled(self, expanded)
/// Two-way: expanded
class Section final : public Element {
public:
    static void declare(TypeBuilder<Section>& type);

    void render(Frame& frame) override;
    /// Its header, and while expanded its stack (Frame::stackSize) indented 16dp below it.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

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
    type.doc("A collapsible header (48dp, 64dp with supporting text) whose children are indented 16dp while expanded "
             "(`Expander` is an alias of `Section`).\n"
             "`Section { \"Advanced\" }` sets `text`. Natural size: fills the available width (raw `width`).");
    type.fillsWidth(); // an unset width fills
    type.property("title", &Section::title, &Section::setTitle)
        .doc("The header text (`titleMedium`). Unset: `text`.")
        .property("text", &Section::text, &Section::setText)
        .doc("The title when `title` isn't set. The positional value: `Section { \"Advanced\" }`.")
        .property("supportingText", &Section::supportingText, &Section::setSupportingText)
        .doc("A second header line; `\"\"` for none.")
        .property("expanded", &Section::expanded, &Section::setExpanded)
        .doc("Two-way. Whether it is open (`true`).")
        .signal<bool>("onToggled", {"expanded"})
        .doc("Called when the user opens or closes it, with the new state.")
        .defaultProperty("text");
    rawStackProps(type);
}

void Section::render(Frame& frame) {
    bool open = m_expanded;
    if (skin().expander("##exp", m_title ? *m_title : m_text, m_supportingText, &open, frame.size("width"))) {
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

std::optional<ImVec2> Section::intrinsicSize(const Frame& frame, float maxWidth) {
    const ImVec2 header = skin().expanderSize(m_supportingText, frame.widthIn(maxWidth));
    if (!m_expanded) return header;
    const float indent = frame.dp(16);
    const std::optional<ImVec2> stack = frame.stackSize(std::max(0.0f, maxWidth - indent));
    if (!stack) return std::nullopt;
    if (stack->x == 0.0f && stack->y == 0.0f) return header; // no visual children
    // The stack starts on the line after the header (ImGui: a whole pixel, the item spacing below).
    const float top = std::trunc(header.y + ImGui::GetStyle().ItemSpacing.y);
    return ImVec2{std::max(header.x, indent + stack->x), top + stack->y};
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
