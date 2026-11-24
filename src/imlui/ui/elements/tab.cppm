// Tab: one page of a TabBar, as a typed element (TypeBuilder). The TabBar reads its title and icon and
// renders the content of the current tab; a Tab outside a TabBar renders its content inline.
module;

#include <string>
#include <utility>

export module imlui.ui.elements.tab;

import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Tab { title = "Tab", icon = "", spacing = nil, align = "start", children... }
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
    type.property("title", &Tab::title, &Tab::setTitle).property("icon", &Tab::icon, &Tab::setIcon);
}

void Tab::render(Frame& frame) { frame.renderStack(); }

const std::string& Tab::title() const { return m_title; }
void Tab::setTitle(std::string title) { m_title = std::move(title); }
const std::string& Tab::icon() const { return m_icon; }
void Tab::setIcon(std::string icon) { m_icon = std::move(icon); }

} // namespace imlui
