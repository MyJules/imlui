// RadioGroup: a column (or row) of Material 3 radio buttons for a list of strings, as a typed element
// (TypeBuilder).
module;

#include <cstddef>
#include <string>
#include <vector>

export module imlui.ui.elements.radio_group;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// RadioGroup { model = {"A", "B"}, currentIndex = 1 (1-based, two-way), horizontal = false,
/// onActivated(index, text) }
class RadioGroup final : public Element {
public:
    static void declare(TypeBuilder<RadioGroup>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::vector<std::string>& model() const;
    void setModel(std::vector<std::string> model);
    /// 1-based. Whole numbers reach Lua as integers (see LuaConvert).
    [[nodiscard]] double currentIndex() const;
    void setCurrentIndex(double index);
    [[nodiscard]] bool horizontal() const;
    void setHorizontal(bool horizontal);

private:
    std::vector<std::string> m_model;
    double m_currentIndex = 1.0;
    bool m_horizontal = false;
};

} // namespace imlui

module :private;

namespace imlui {

void RadioGroup::declare(TypeBuilder<RadioGroup>& type) {
    type.property("model", &RadioGroup::model, &RadioGroup::setModel)
        .property("currentIndex", &RadioGroup::currentIndex, &RadioGroup::setCurrentIndex)
        .property("horizontal", &RadioGroup::horizontal, &RadioGroup::setHorizontal)
        .signal("onActivated");
}

void RadioGroup::render(Frame& frame) {
    const int cur = static_cast<int>(m_currentIndex);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0});
    // Iterate a copy: the handler may replace the model.
    const std::vector<std::string> items = m_model;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (m_horizontal && i > 0) ImGui::SameLine(0, frame.dp(16));
        ImGui::PushID(i);
        const auto idx = static_cast<std::size_t>(i);
        const std::string label = Frame::labelId(items[idx], "r");
        if (md::radio(label.c_str(), cur == i + 1) && cur != i + 1) {
            m_currentIndex = static_cast<double>(i + 1);
            notify("currentIndex");
            emit("onActivated", static_cast<double>(i + 1), items[idx]);
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
}

const std::vector<std::string>& RadioGroup::model() const { return m_model; }
void RadioGroup::setModel(std::vector<std::string> model) { m_model = std::move(model); }
double RadioGroup::currentIndex() const { return m_currentIndex; }
void RadioGroup::setCurrentIndex(double index) { m_currentIndex = index; }
bool RadioGroup::horizontal() const { return m_horizontal; }
void RadioGroup::setHorizontal(bool horizontal) { m_horizontal = horizontal; }

} // namespace imlui
