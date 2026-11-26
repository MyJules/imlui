// Tab: one page of a TabBar, as a typed element. The TabBar reads its title and icon and
// renders the content of the current tab; a Tab outside a TabBar renders its content inline.
export module imlui.ui.elements.tab;

import std;

import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.stack_props;

export namespace imlui {

/// One tab of a TabBar; a Tab outside a TabBar renders its content inline.
///
/// Props:   title = "Tab", icon = ""
/// Raw:     spacing, align, cull (the TabBar stacks the content with them)
class Tab final : public Element {
public:
    static void declare(TypeBuilder<Tab>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& title() const;
    void setTitle(std::string title);
    /// Material Symbols name shown before the title; empty for none.
    [[nodiscard]] const std::string& icon() const;
    void setIcon(std::string icon);

private:
    std::string m_title = "Tab";
    std::string m_icon;
};

} // namespace imlui

module :private;

namespace imlui {

void Tab::declare(TypeBuilder<Tab>& type) {
    type.doc("One tab of a `TabBar`: its header and its content, stacked with `spacing` / `align`. "
             "`visible = false` hides the tab and its header; outside a `TabBar` it renders its content inline.")
        .property("title", &Tab::title, &Tab::setTitle)
        .doc("The header's label, `\"Tab\"` by default.")
        .property("icon", &Tab::icon, &Tab::setIcon)
        .doc("The header's Material icon name; `\"\"` for none.");
    rawStackProps(type);
}

void Tab::render(Frame& frame) { frame.renderStack(); }

const std::string& Tab::title() const { return m_title; }
void Tab::setTitle(std::string title) { m_title = std::move(title); }
const std::string& Tab::icon() const { return m_icon; }
void Tab::setIcon(std::string icon) { m_icon = std::move(icon); }

} // namespace imlui
