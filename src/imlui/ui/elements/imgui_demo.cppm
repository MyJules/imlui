// ImGuiDemo: Dear ImGui's demo window (debugging), as a typed element.
module;

export module imlui.ui.elements.imgui_demo;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Dear ImGui's demo window, for debugging. Non-visual.
///
/// Props:   open = true
/// Two-way: open (closing the window writes false)
class ImGuiDemo final : public Element {
public:
    static void declare(TypeBuilder<ImGuiDemo>& type);

    void render(Frame& frame) override;

    [[nodiscard]] bool open() const;
    void setOpen(bool open);

private:
    bool m_open = true;
};

} // namespace imlui

module :private;

namespace imlui {

void ImGuiDemo::declare(TypeBuilder<ImGuiDemo>& type) {
    type.doc("Dear ImGui's demo window, for debugging. Non-visual.")
        .property("open", &ImGuiDemo::open, &ImGuiDemo::setOpen)
        .doc("Two-way. Shows the window (`true`); closing it writes `false`.")
        .nonVisual();
}

void ImGuiDemo::render(Frame&) {
    if (!m_open) return;
    ImGui::ShowDemoWindow(&m_open);
    if (!m_open) notify("open");
}

bool ImGuiDemo::open() const { return m_open; }
void ImGuiDemo::setOpen(bool open) { m_open = open; }

} // namespace imlui
