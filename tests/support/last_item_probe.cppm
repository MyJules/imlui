// LastItemProbe: a test container that renders its children and remembers the screen rect of the last visual
// one, so a test knows where to click. Test-only (built into imlui_tests); register it under any name:
//
//   h.registerType<test::LastItemProbe>("Probe");
//   h.run("root = Probe { Slider { value = 0.5 } }");
//   h.renderFrame("root");
//   const ImVec2 at = h.instance<test::LastItemProbe>("root")->center();

export module imlui.test.last_item_probe;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui::test {

class LastItemProbe final : public Element {
public:
    static void declare(TypeBuilder<LastItemProbe>& type);

    void render(Frame& frame) override;

    /// The rect of the last visual child on the last rendered frame (screen pixels).
    [[nodiscard]] ImVec2 min() const;
    [[nodiscard]] ImVec2 max() const;
    [[nodiscard]] ImVec2 size() const;
    [[nodiscard]] ImVec2 center() const;

private:
    ImVec2 m_min{};
    ImVec2 m_max{};
};

} // namespace imlui::test

module :private;

namespace imlui::test {

void LastItemProbe::declare(TypeBuilder<LastItemProbe>&) {}

void LastItemProbe::render(Frame& frame) {
    frame.forEachChild([&](Frame::Child& child) {
        child.render();
        if (!child.isVisual()) return;
        m_min = ImGui::GetItemRectMin();
        m_max = ImGui::GetItemRectMax();
    });
}

ImVec2 LastItemProbe::min() const { return m_min; }
ImVec2 LastItemProbe::max() const { return m_max; }
ImVec2 LastItemProbe::size() const { return {m_max.x - m_min.x, m_max.y - m_min.y}; }
ImVec2 LastItemProbe::center() const { return {(m_min.x + m_max.x) * 0.5f, (m_min.y + m_max.y) * 0.5f}; }

} // namespace imlui::test
