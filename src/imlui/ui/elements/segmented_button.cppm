// SegmentedButton: the Material 3 single-select segmented button, as a typed element.
module;

#include <cstddef>
#include <string>
#include <vector>

export module imlui.ui.elements.segmented_button;

import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A Material segmented button from a model.
///
/// Props:   model = {}, icons = {}, currentIndex = 1 (1-based)
/// Raw:     width
/// Signals: onActivated(self, index, text)
/// Two-way: currentIndex
class SegmentedButton final : public Element {
public:
    static void declare(TypeBuilder<SegmentedButton>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::vector<std::string>& model() const;
    void setModel(std::vector<std::string> model);
    [[nodiscard]] const std::vector<std::string>& icons() const; ///< per-segment icons for unselected segments
    void setIcons(std::vector<std::string> icons);
    /// 1-based. Whole numbers reach Lua as integers (see LuaConvert).
    [[nodiscard]] double currentIndex() const;
    void setCurrentIndex(double index);

private:
    std::vector<std::string> m_model;
    std::vector<std::string> m_icons;
    double m_currentIndex = 1.0;
};

} // namespace imlui

module :private;

namespace imlui {

void SegmentedButton::declare(TypeBuilder<SegmentedButton>& type) {
    type.property("model", &SegmentedButton::model, &SegmentedButton::setModel)
        .property("icons", &SegmentedButton::icons, &SegmentedButton::setIcons)
        .property("currentIndex", &SegmentedButton::currentIndex, &SegmentedButton::setCurrentIndex)
        .signal("onActivated");
}

void SegmentedButton::render(Frame& frame) {
    int cur = static_cast<int>(m_currentIndex) - 1;
    if (md::segmented("##seg", m_model, &cur, frame.size("width"), m_icons)) {
        m_currentIndex = static_cast<double>(cur + 1);
        notify("currentIndex");
        // Copied first: the handler's own changes can't reach m_model before the next sync, but a method
        // call on the element syncs it.
        const std::string text = m_model[static_cast<std::size_t>(cur)];
        emit("onActivated", static_cast<double>(cur + 1), text);
    }
}

const std::vector<std::string>& SegmentedButton::model() const { return m_model; }
void SegmentedButton::setModel(std::vector<std::string> model) { m_model = std::move(model); }
const std::vector<std::string>& SegmentedButton::icons() const { return m_icons; }
void SegmentedButton::setIcons(std::vector<std::string> icons) { m_icons = std::move(icons); }
double SegmentedButton::currentIndex() const { return m_currentIndex; }
void SegmentedButton::setCurrentIndex(double index) { m_currentIndex = index; }

} // namespace imlui
