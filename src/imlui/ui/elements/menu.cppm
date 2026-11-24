// Menu: a menu-bar menu with Material menu styling, as a typed element. The same element is the popup of a
// `menu` prop (Frame::attachMenu renders its children in an anchored menu).
module;

#include <string>

export module imlui.ui.elements.menu;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Menu { title, <MenuItems, Dividers, Menus> }: in a MenuBar, a title that opens the items as a Material menu.
class Menu final : public Element {
public:
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

void Menu::declare(TypeBuilder<Menu>& type) { type.property("title", &Menu::title, &Menu::setTitle); }

void Menu::render(Frame& frame) {
    const std::string label = Frame::labelId(m_title, "menu");
    md::pushMenuStyle();
    ImGui::PopStyleVar(); // keep the menubar's own item spacing
    const bool open = ImGui::BeginMenu(label.c_str());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    if (open) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        frame.renderChildren();
        ImGui::PopStyleVar();
        ImGui::EndMenu();
    }
}

const std::string& Menu::title() const { return m_title; }
void Menu::setTitle(std::string title) { m_title = std::move(title); }

} // namespace imlui
