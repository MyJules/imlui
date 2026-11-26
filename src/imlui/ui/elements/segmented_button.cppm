// SegmentedButton: the Material 3 single-select segmented button, as a typed element.
export module imlui.ui.elements.segmented_button;

import std;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.props;

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
    /// Its segments (each as wide as the widest label), or its `width`; none without items.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

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
    type.doc("A Material 3 single-select segmented button from a `model` of labels; the selected segment is "
             "`currentIndex` (1-based, two-way) and shows a check.\n"
             "Natural size: every segment as wide as the widest label, 40dp high; `width` (raw) sizes the whole.")
        .property("model", &SegmentedButton::model, &SegmentedButton::setModel)
        .doc("The segment labels.")
        .property("icons", &SegmentedButton::icons, &SegmentedButton::setIcons)
        .doc("Per-segment Material icon names, shown on the unselected segments (the selected one shows a check).")
        .property("currentIndex", &SegmentedButton::currentIndex, &SegmentedButton::setCurrentIndex)
        .doc("**Two-way.** The selected segment, 1-based (default 1).")
        .signal<int, std::string>("onActivated", {"index", "text"})
        .doc("Called when a segment is picked, with its 1-based index and label (after `currentIndex` was written "
             "back).");
}

void SegmentedButton::render(Frame& frame) {
    int cur = indexValue(m_currentIndex) - 1; // NaN, infinities and huge values are safe
    if (md::segmented("##seg", m_model, &cur, frame.size("width"), m_icons)) {
        m_currentIndex = static_cast<double>(cur + 1);
        // Copied before notify(): its handler (onCurrentIndexChanged, a bound setter) may replace the model, and a
        // method call on the element syncs the new one into m_model.
        const std::string text = m_model[static_cast<std::size_t>(cur)];
        notify("currentIndex");
        emit("onActivated", static_cast<double>(cur + 1), text);
    }
}

const std::vector<std::string>& SegmentedButton::model() const { return m_model; }
void SegmentedButton::setModel(std::vector<std::string> model) { m_model = std::move(model); }
const std::vector<std::string>& SegmentedButton::icons() const { return m_icons; }
void SegmentedButton::setIcons(std::vector<std::string> icons) { m_icons = std::move(icons); }
double SegmentedButton::currentIndex() const { return m_currentIndex; }
void SegmentedButton::setCurrentIndex(double index) { m_currentIndex = index; }

std::optional<ImVec2> SegmentedButton::intrinsicSize(const Frame& frame, float maxWidth) {
    if (m_model.empty()) return std::nullopt; // draws nothing: no item to measure
    return md::segmentedSize(m_model, frame.widthIn(maxWidth));
}

} // namespace imlui
