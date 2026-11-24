// Chip: the Material 3 chip (filter, assist, suggestion, input), as a typed element (TypeBuilder).
module;

#include <string>

export module imlui.ui.elements.chip;

import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Chip { "label" | text = ..., checked = false, checkable = true, variant = "filter", icon = "",
/// onClicked(!checked) }. A checkable filter chip toggles `checked` (two-way) when clicked.
class Chip final : public Element {
public:
    static void declare(TypeBuilder<Chip>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] bool checked() const;
    void setChecked(bool checked);
    [[nodiscard]] bool checkable() const; ///< filter chips only
    void setCheckable(bool checkable);
    [[nodiscard]] const std::string& variant() const; ///< "filter", "assist", "suggestion", "input"
    void setVariant(std::string variant);
    [[nodiscard]] const std::string& icon() const; ///< optional leading Material icon
    void setIcon(std::string icon);

private:
    std::string m_text;
    bool m_checked = false;
    bool m_checkable = true;
    std::string m_variant = "filter";
    std::string m_icon;
};

} // namespace imlui

module :private;

namespace imlui {

void Chip::declare(TypeBuilder<Chip>& type) {
    type.property("text", &Chip::text, &Chip::setText)
        .property("checked", &Chip::checked, &Chip::setChecked)
        .property("checkable", &Chip::checkable, &Chip::setCheckable)
        .property("variant", &Chip::variant, &Chip::setVariant)
        .property("icon", &Chip::icon, &Chip::setIcon)
        .defaultProperty("text")
        .signal("onClicked");
}

void Chip::render(Frame&) {
    const std::string label = Frame::labelId(m_text, "chip");
    const bool v = m_checked;
    const md::ChipVariant variant = md::chipVariant(m_variant);
    if (md::chip(label.c_str(), v, variant, m_icon)) {
        if (variant == md::ChipVariant::Filter && m_checkable) {
            m_checked = !v;
            notify("checked");
        }
        emit("onClicked", !v);
    }
}

const std::string& Chip::text() const { return m_text; }
void Chip::setText(std::string text) { m_text = std::move(text); }
bool Chip::checked() const { return m_checked; }
void Chip::setChecked(bool checked) { m_checked = checked; }
bool Chip::checkable() const { return m_checkable; }
void Chip::setCheckable(bool checkable) { m_checkable = checkable; }
const std::string& Chip::variant() const { return m_variant; }
void Chip::setVariant(std::string variant) { m_variant = std::move(variant); }
const std::string& Chip::icon() const { return m_icon; }
void Chip::setIcon(std::string icon) { m_icon = std::move(icon); }

} // namespace imlui
