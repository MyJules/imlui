// RadioGroup: a column (or row) of Material 3 radio buttons for a list of strings, as a typed element
//.
export module imlui.ui.elements.radio_group;

import std;

import imgui;
import imlui.widgets;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.props;

export namespace imlui {

/// A list of radio buttons from a model.
///
/// Props:   model = {}, currentIndex = 1 (1-based), horizontal = false
/// Signals: onActivated(self, index, text)
/// Two-way: currentIndex
class RadioGroup final : public Element {
public:
    static void declare(TypeBuilder<RadioGroup>& type);

    void render(Frame& frame) override;
    /// Its radio buttons, stacked (or side by side when `horizontal`).
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

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
    type.doc("A list of Material 3 radio buttons from a `model` of labels; the selected one is `currentIndex` "
             "(1-based, two-way).\n"
             "Natural size: the buttons stacked (40dp each), or side by side 16dp apart when `horizontal`.")
        .property("model", &RadioGroup::model, &RadioGroup::setModel)
        .doc("The option labels.")
        .property("currentIndex", &RadioGroup::currentIndex, &RadioGroup::setCurrentIndex)
        .doc("**Two-way.** The selected option, 1-based (default 1).")
        .property("horizontal", &RadioGroup::horizontal, &RadioGroup::setHorizontal)
        .doc("Lays the options out in a row, 16dp apart, instead of a column.")
        .signal<int, std::string>("onActivated", {"index", "text"})
        .doc("Called when another option is picked, with its 1-based index and label (after `currentIndex` was "
             "written back).");
}

void RadioGroup::render(Frame& frame) {
    const int cur = indexValue(m_currentIndex); // NaN, infinities and huge values are safe
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0});
    // The handlers may replace the model (a sync while they run): the bound is read again for every item, and a
    // clicked item's text is copied before they run.
    for (int i = 0; i < static_cast<int>(m_model.size()); ++i) {
        if (m_horizontal && i > 0) ImGui::SameLine(0, frame.dp(16));
        ImGui::PushID(i);
        const auto idx = static_cast<std::size_t>(i);
        const std::string label = Frame::labelId(m_model[idx], "r");
        if (skin().radio(label.c_str(), cur == i + 1) && cur != i + 1) {
            const std::string item = m_model[idx];
            m_currentIndex = static_cast<double>(i + 1);
            notify("currentIndex");
            emit("onActivated", static_cast<double>(i + 1), item);
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

std::optional<ImVec2> RadioGroup::intrinsicSize(const Frame& frame, float) {
    // render() in numbers: the buttons with no vertical item spacing (ImGui starts each line on a whole pixel), or
    // side by side 16dp apart.
    if (m_model.empty()) return std::nullopt; // draws nothing: no item to measure
    ImVec2 size{0.0f, 0.0f};
    float y = 0.0f;
    for (std::size_t i = 0; i < m_model.size(); ++i) {
        const ImVec2 radio = skin().radioSize(Frame::labelId(m_model[i], "r").c_str());
        if (m_horizontal) {
            size.x += (i > 0 ? frame.dp(16) : 0.0f) + radio.x;
            size.y = std::max(size.y, radio.y);
        } else {
            size.x = std::max(size.x, radio.x);
            size.y = y + radio.y;
            y = std::trunc(y + radio.y);
        }
    }
    return size;
}

} // namespace imlui
