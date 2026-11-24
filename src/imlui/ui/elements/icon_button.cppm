// IconButton: the Material 3 icon button (optionally a toggle), as a typed element.
export module imlui.ui.elements.icon_button;

import std;

import imlui.color;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A 40dp icon button. It is a toggle only when `checked` is present in the element table; a click then flips it.
///
/// Props:   text = "" (positional; the icon when `icon` is unset), icon = unset, checkedIcon = unset,
///          variant = "standard" ("filled", "tonal", "outlined"), checked = false, color = unset
/// Raw:     menu (a Menu opened on click)
/// Signals: onClicked(self)
/// Two-way: checked (toggles)
class IconButton final : public Element {
public:
    static void declare(TypeBuilder<IconButton>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] const std::optional<std::string>& icon() const; ///< unset: `text` is the icon name
    void setIcon(std::optional<std::string> icon);
    [[nodiscard]] const std::optional<std::string>& checkedIcon() const; ///< the icon while checked
    void setCheckedIcon(std::optional<std::string> icon);
    [[nodiscard]] const std::string& variant() const; ///< "standard", "filled", "tonal", "outlined"
    void setVariant(std::string variant);
    [[nodiscard]] bool checked() const;
    void setChecked(bool checked);
    [[nodiscard]] const std::optional<Color>& color() const; ///< icon tint
    void setColor(std::optional<Color> color);

private:
    std::string m_text;
    std::optional<std::string> m_icon;
    std::optional<std::string> m_checkedIcon;
    std::string m_variant = "standard";
    bool m_checked = false;
    std::optional<Color> m_color;
};

} // namespace imlui

module :private;

namespace imlui {

void IconButton::declare(TypeBuilder<IconButton>& type) {
    type.property("text", &IconButton::text, &IconButton::setText)
        .property("icon", &IconButton::icon, &IconButton::setIcon)
        .property("checkedIcon", &IconButton::checkedIcon, &IconButton::setCheckedIcon)
        .property("variant", &IconButton::variant, &IconButton::setVariant)
        .property("checked", &IconButton::checked, &IconButton::setChecked)
        .property("color", &IconButton::color, &IconButton::setColor)
        .defaultProperty("text")
        .signal("onClicked");
}

void IconButton::render(Frame& frame) {
    // Presence, not value: `checked = function() return nil end` still makes a (unchecked) toggle.
    const bool checkable = frame.node().has("checked");
    const bool checked = m_checked;
    std::string icon = m_icon.value_or(m_text);
    if (checkable && checked && m_checkedIcon) icon = *m_checkedIcon;
    const bool clicked = md::iconButton("##ib", icon, md::iconButtonVariant(m_variant), checkable, checked, m_color);
    if (clicked) {
        if (checkable) {
            m_checked = !checked;
            notify("checked");
        }
        emit("onClicked");
    }
    frame.attachMenu(clicked);
}

const std::string& IconButton::text() const { return m_text; }
void IconButton::setText(std::string text) { m_text = std::move(text); }
const std::optional<std::string>& IconButton::icon() const { return m_icon; }
void IconButton::setIcon(std::optional<std::string> icon) { m_icon = std::move(icon); }
const std::optional<std::string>& IconButton::checkedIcon() const { return m_checkedIcon; }
void IconButton::setCheckedIcon(std::optional<std::string> icon) { m_checkedIcon = std::move(icon); }
const std::string& IconButton::variant() const { return m_variant; }
void IconButton::setVariant(std::string variant) { m_variant = std::move(variant); }
bool IconButton::checked() const { return m_checked; }
void IconButton::setChecked(bool checked) { m_checked = checked; }
const std::optional<Color>& IconButton::color() const { return m_color; }
void IconButton::setColor(std::optional<Color> color) { m_color = color; }

} // namespace imlui
