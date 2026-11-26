// TreeNode: a collapsible ImGui tree node, as a typed element.
export module imlui.ui.elements.tree_node;

import std;

import imgui;
import imlui.theme.typography;
import imlui.theme.font_scope;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// An ImGui tree node whose children render inside it.
///
/// Props:   text = "" (positional), expanded = false (open when it first appears; not written back)
class TreeNode final : public Element {
public:
    static void declare(TypeBuilder<TreeNode>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    /// Open when the node first appears; ImGui keeps the open state afterwards.
    [[nodiscard]] bool expanded() const;
    void setExpanded(bool expanded);

private:
    std::string m_text;
    bool m_expanded = false;
};

} // namespace imlui

module :private;

namespace imlui {

void TreeNode::declare(TypeBuilder<TreeNode>& type) {
    type.doc("An ImGui tree node (`bodyLarge`) whose children render inside it while open (not stacked: no "
             "`spacing` / `align`). `TreeNode { \"Files\" }` sets `text`.")
        .property("text", &TreeNode::text, &TreeNode::setText)
        .doc("The label. The positional value: `TreeNode { \"Files\" }`.")
        .property("expanded", &TreeNode::expanded, &TreeNode::setExpanded)
        .doc("Open when the node first appears (`false`). ImGui keeps the open state afterwards; it isn't written "
             "back.")
        .defaultProperty("text");
}

void TreeNode::render(Frame& frame) {
    const std::string label = Frame::labelId(m_text, "tree");
    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_expanded) f |= ImGuiTreeNodeFlags_DefaultOpen;
    const FontScope fs{TextStyle::BodyLarge};
    if (ImGui::TreeNodeEx(label.c_str(), f)) {
        frame.renderChildren();
        ImGui::TreePop();
    }
}

const std::string& TreeNode::text() const { return m_text; }
void TreeNode::setText(std::string text) { m_text = std::move(text); }
bool TreeNode::expanded() const { return m_expanded; }
void TreeNode::setExpanded(bool expanded) { m_expanded = expanded; }

} // namespace imlui
