// ComboBox: the Material 3 exposed dropdown menu, as a typed element.
export module imlui.ui.elements.combo_box;

import std;

import imgui;
import imlui.widgets.dropdown;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.props;

export namespace imlui {

/// A Material exposed dropdown menu.
///
/// Props:   model = {}, icons = {}, currentIndex = 1 (1-based), label = "", leadingIcon = "", supportingText = "",
///          variant = "filled" ("outlined"), error = false
/// Raw:     width
/// Signals: onActivated(self, index, text)
/// Two-way: currentIndex
class ComboBox final : public Element {
public:
    static void declare(TypeBuilder<ComboBox>& type);

    void render(Frame& frame) override;
    /// Its width (natural 240dp, fixed, or a fill of `maxWidth`), the field and its supporting text.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::vector<std::string>& model() const;
    void setModel(std::vector<std::string> model);
    [[nodiscard]] const std::vector<std::string>& icons() const;
    void setIcons(std::vector<std::string> icons);
    /// 1-based, like Lua sequences.
    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int index);
    [[nodiscard]] const std::string& label() const;
    void setLabel(std::string label);
    [[nodiscard]] const std::string& leadingIcon() const;
    void setLeadingIcon(std::string icon);
    [[nodiscard]] const std::string& supportingText() const;
    void setSupportingText(std::string text);
    [[nodiscard]] const std::string& variant() const;
    void setVariant(std::string variant);
    [[nodiscard]] bool error() const;
    void setError(bool error);

private:
    std::vector<std::string> m_model;
    std::vector<std::string> m_icons;
    int m_currentIndex = 1;
    std::string m_label;
    std::string m_leadingIcon;
    std::string m_supportingText;
    std::string m_variant = "filled";
    bool m_error = false;
};

} // namespace imlui

module :private;

namespace imlui {

void ComboBox::declare(TypeBuilder<ComboBox>& type) {
    type.property("model", &ComboBox::model, &ComboBox::setModel)
        .property("icons", &ComboBox::icons, &ComboBox::setIcons)
        .property("currentIndex", &ComboBox::currentIndex, &ComboBox::setCurrentIndex)
        .property("label", &ComboBox::label, &ComboBox::setLabel)
        .property("leadingIcon", &ComboBox::leadingIcon, &ComboBox::setLeadingIcon)
        .property("supportingText", &ComboBox::supportingText, &ComboBox::setSupportingText)
        .property("variant", &ComboBox::variant, &ComboBox::setVariant)
        .property("error", &ComboBox::error, &ComboBox::setError)
        .signal("onActivated");
}

void ComboBox::render(Frame& frame) {
    md::DropdownOptions o;
    o.label = m_label;
    o.leadingIcon = m_leadingIcon;
    o.supporting = m_supportingText;
    o.outlined = m_variant == "outlined";
    o.error = m_error;
    o.width = frame.size("width");
    int cur = indexValue(m_currentIndex) - 1; // no overflow on INT_MIN
    if (md::dropdown("##dd", m_model, &cur, o, m_icons)) {
        m_currentIndex = cur + 1;
        // Copied before notify(): its handler may replace the model (a method call on the element syncs it).
        const std::string text = m_model[static_cast<std::size_t>(cur)];
        notify("currentIndex");
        emit("onActivated", cur + 1, text);
    }
}

const std::vector<std::string>& ComboBox::model() const { return m_model; }
void ComboBox::setModel(std::vector<std::string> model) { m_model = std::move(model); }
const std::vector<std::string>& ComboBox::icons() const { return m_icons; }
void ComboBox::setIcons(std::vector<std::string> icons) { m_icons = std::move(icons); }
int ComboBox::currentIndex() const { return m_currentIndex; }
void ComboBox::setCurrentIndex(int index) { m_currentIndex = index; }
const std::string& ComboBox::label() const { return m_label; }
void ComboBox::setLabel(std::string label) { m_label = std::move(label); }
const std::string& ComboBox::leadingIcon() const { return m_leadingIcon; }
void ComboBox::setLeadingIcon(std::string icon) { m_leadingIcon = std::move(icon); }
const std::string& ComboBox::supportingText() const { return m_supportingText; }
void ComboBox::setSupportingText(std::string text) { m_supportingText = std::move(text); }
const std::string& ComboBox::variant() const { return m_variant; }
void ComboBox::setVariant(std::string variant) { m_variant = std::move(variant); }
bool ComboBox::error() const { return m_error; }
void ComboBox::setError(bool error) { m_error = error; }

std::optional<ImVec2> ComboBox::intrinsicSize(const Frame& frame, float maxWidth) {
    md::DropdownOptions o;
    o.supporting = m_supportingText;
    o.width = frame.widthIn(maxWidth);
    return md::dropdownSize(o);
}

} // namespace imlui
