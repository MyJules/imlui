// RenderCounter: a test element, a 10 x 10 dp box that counts its renders. Test-only (built into imlui_tests);
// register it under any name: `h.registerType<test::RenderCounter>("Probe")`.

export module imlui.test.render_counter;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui::test {

class RenderCounter final : public Element {
public:
    static void declare(TypeBuilder<RenderCounter>& type);

    void render(Frame& frame) override;

    [[nodiscard]] int renders() const;

private:
    int m_renders = 0;
};

} // namespace imlui::test

module :private;

namespace imlui::test {

void RenderCounter::declare(TypeBuilder<RenderCounter>&) {}

void RenderCounter::render(Frame& frame) {
    ++m_renders;
    ImGui::Dummy({frame.dp(10), frame.dp(10)});
}

int RenderCounter::renders() const { return m_renders; }

} // namespace imlui::test
