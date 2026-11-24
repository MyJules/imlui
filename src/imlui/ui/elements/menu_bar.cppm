// MenuBar: the window menu bar (ApplicationWindow / Window `menuBar`), as a typed element.
module;

export module imlui.ui.elements.menu_bar;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The current window's ImGui menu bar, with its children (Menus) in it. No props of its own.
class MenuBar final : public Element {
public:
    static void declare(TypeBuilder<MenuBar>& type);

    void render(Frame& frame) override;
};

} // namespace imlui

module :private;

namespace imlui {

void MenuBar::declare(TypeBuilder<MenuBar>&) {}

void MenuBar::render(Frame& frame) {
    if (ImGui::BeginMenuBar()) {
        frame.renderChildren();
        ImGui::EndMenuBar();
    }
}

} // namespace imlui
