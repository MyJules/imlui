// Menu: a menu-bar menu with Material menu styling, as a typed element. The same element is the popup of a
// `menu` prop (Frame::attachMenu renders its children in an anchored menu).
export module imlui.ui.elements.menu;

import std;

import imgui;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// In a MenuBar, a title that opens the children (MenuItems, Dividers, Menus) as a Material menu; inside a menu,
/// a submenu. As a Button's or IconButton's `menu`, the button opens its children (Frame::attachMenu).
///
/// Props:   title = ""
class Menu final : public Element {
public:
    /// The menu's minimum width (dp), Material's.
    static constexpr float minWidth = 112.0f;

    static void declare(TypeBuilder<Menu>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& title() const;
    void setTitle(std::string title);

private:
    std::string m_title;
};

} // namespace imlui

module :private;

namespace imlui {

void Menu::declare(TypeBuilder<Menu>& type) {
    type.doc("A Material menu of `MenuItem`s, `Divider`s, nested `Menu`s (submenus), `Repeater`s, ...: in a "
             "`MenuBar` a title that opens it, inside a menu a submenu, and as a `Button`'s or `IconButton`'s "
             "`menu` a dropdown under the button.\n"
             "As wide as its widest row (at least 112dp; a row at least 160dp).")
        .property("title", &Menu::title, &Menu::setTitle)
        .doc("The title in a menu bar, or the submenu's label.");
}

void Menu::render(Frame& frame) {
    const std::string label = Frame::labelId(m_title, "menu");
    if (skin().beginMenu(label.c_str(), frame.dp(minWidth))) {
        frame.renderChildren();
        skin().endMenu();
    }
}

const std::string& Menu::title() const { return m_title; }
void Menu::setTitle(std::string title) { m_title = std::move(title); }

} // namespace imlui
