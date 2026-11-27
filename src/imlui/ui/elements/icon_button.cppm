// IconButton: the Material 3 icon button (optionally a toggle), as a typed element.
export module imlui.ui.elements.icon_button;

import std;

import imgui;
import imlui.color;
import imlui.widgets;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.enum_names;
import imlui.types.type_info;

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
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] const std::optional<std::string>& icon() const; ///< unset: `text` is the icon name
    void setIcon(std::optional<std::string> icon);
    [[nodiscard]] const std::optional<std::string>& checkedIcon() const; ///< the icon while checked
    void setCheckedIcon(std::optional<std::string> icon);
    [[nodiscard]] md::IconButtonVariant variant() const;
    void setVariant(md::IconButtonVariant variant);
    [[nodiscard]] bool checked() const;
    void setChecked(bool checked);
    [[nodiscard]] const std::optional<Color>& color() const; ///< icon tint
    void setColor(std::optional<Color> color);

private:
    std::string m_text;
    std::optional<std::string> m_icon;
    std::optional<std::string> m_checkedIcon;
    md::IconButtonVariant m_variant = md::IconButtonVariant::Standard;
    bool m_checked = false;
    std::optional<Color> m_color;
};

} // namespace imlui

module :private;

namespace imlui {

void IconButton::declare(TypeBuilder<IconButton>& type) {
    type.doc("A Material 3 icon button: a 40dp icon container in a 48dp touch target.\n"
             "`IconButton { \"menu\" }` sets `text`, the icon name unless `icon` is set. With `checked` in the table "
             "(any value, even a binding that returns `nil`) it is a toggle: a click flips `checked` (two-way).\n"
             "Natural size: 48dp square.")
        .property("text", &IconButton::text, &IconButton::setText)
        .doc("The icon name when `icon` isn't set. The positional value: `IconButton { \"menu\" }`.")
        .property("icon", &IconButton::icon, &IconButton::setIcon)
        .doc("The Material icon name; unset: `text`.")
        .property("checkedIcon", &IconButton::checkedIcon, &IconButton::setCheckedIcon)
        .doc("The icon while a toggle is checked; unset: the normal icon.")
        .property("variant", &IconButton::variant, &IconButton::setVariant)
        .doc("`\"standard\"` (default), `\"filled\"`, `\"tonal\"` or `\"outlined\"`.")
        .property("checked", &IconButton::checked, &IconButton::setChecked)
        .doc("**Two-way.** Present at all, it makes the button a toggle with selected and unselected looks; a click "
             "flips it.")
        .property("color", &IconButton::color, &IconButton::setColor)
        .doc("The icon tint; unset: the variant's.")
        .defaultProperty("text")
        .signal<>("onClicked")
        .doc("Called when the button is clicked (after a toggle flipped `checked`).");
    type.rawProperty("menu", "Element|false", RawRead::ValueOnly)
        .doc("A `Menu { ... }` opened under the button on a click, as an anchored dropdown (at least 112dp wide).");
}

void IconButton::render(Frame& frame) {
    // Presence, not value: `checked = function() return nil end` still makes a (unchecked) toggle.
    const bool checkable = frame.node().has("checked");
    const bool checked = m_checked;
    std::string icon = m_icon.value_or(m_text);
    if (checkable && checked && m_checkedIcon) icon = *m_checkedIcon;
    const bool clicked = skin().iconButton("##ib", icon, m_variant, checkable, checked, m_color);
    if (clicked) {
        if (checkable) {
            m_checked = !checked;
            notify("checked");
        }
        emit("onClicked");
    }
    frame.attachMenu(clicked);
}

std::optional<ImVec2> IconButton::intrinsicSize(const Frame&, float) { return skin().iconButtonSize(); }

const std::string& IconButton::text() const { return m_text; }
void IconButton::setText(std::string text) { m_text = std::move(text); }
const std::optional<std::string>& IconButton::icon() const { return m_icon; }
void IconButton::setIcon(std::optional<std::string> icon) { m_icon = std::move(icon); }
const std::optional<std::string>& IconButton::checkedIcon() const { return m_checkedIcon; }
void IconButton::setCheckedIcon(std::optional<std::string> icon) { m_checkedIcon = std::move(icon); }
md::IconButtonVariant IconButton::variant() const { return m_variant; }
void IconButton::setVariant(md::IconButtonVariant variant) { m_variant = variant; }
bool IconButton::checked() const { return m_checked; }
void IconButton::setChecked(bool checked) { m_checked = checked; }
const std::optional<Color>& IconButton::color() const { return m_color; }
void IconButton::setColor(std::optional<Color> color) { m_color = color; }

} // namespace imlui
