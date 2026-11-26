// ListItem: the Material 3 one- or two-line list item, as a typed element.
export module imlui.ui.elements.list_item;

import std;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A one-line (56dp) or two-line (72dp) Material list item.
///
/// Props:   text = "" (positional; the headline), supportingText = "", icon = "", trailingText = "",
///          trailingIcon = "", selected = false
/// Raw:     width (fills by default)
/// Signals: onClicked(self)
class ListItem final : public Element {
public:
    static void declare(TypeBuilder<ListItem>& type);

    void render(Frame& frame) override;
    /// Its width (natural 320dp, fixed, or a fill of `maxWidth`): 56dp, 72dp with supporting text.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const; ///< the headline
    void setText(std::string text);
    [[nodiscard]] const std::string& supportingText() const; ///< a second line
    void setSupportingText(std::string text);
    [[nodiscard]] const std::string& icon() const; ///< leading icon
    void setIcon(std::string icon);
    [[nodiscard]] const std::string& trailingText() const;
    void setTrailingText(std::string text);
    [[nodiscard]] const std::string& trailingIcon() const;
    void setTrailingIcon(std::string icon);
    [[nodiscard]] bool selected() const;
    void setSelected(bool selected);

private:
    std::string m_text;
    std::string m_supportingText;
    std::string m_icon;
    std::string m_trailingText;
    std::string m_trailingIcon;
    bool m_selected = false;
};

} // namespace imlui

module :private;

namespace imlui {

void ListItem::declare(TypeBuilder<ListItem>& type) {
    type.doc("A clickable Material 3 list item: a headline with optional supporting text, leading icon and "
             "trailing text or icon.\n"
             "`ListItem { \"Inbox\" }` sets `text`. Natural size: fills the width (`width`, raw); 56dp high, 72dp "
             "with `supportingText`.");
    type.fillsWidth(); // an unset width fills
    type.property("text", &ListItem::text, &ListItem::setText)
        .doc("The headline (`bodyLarge`). The positional value: `ListItem { \"Inbox\" }`.")
        .property("supportingText", &ListItem::supportingText, &ListItem::setSupportingText)
        .doc("A second line; makes the item two-line (72dp).")
        .property("icon", &ListItem::icon, &ListItem::setIcon)
        .doc("A leading Material icon name; `\"\"` for none.")
        .property("trailingText", &ListItem::trailingText, &ListItem::setTrailingText)
        .doc("A trailing label (`labelSmall`).")
        .property("trailingIcon", &ListItem::trailingIcon, &ListItem::setTrailingIcon)
        .doc("A trailing Material icon name; `\"\"` for none.")
        .property("selected", &ListItem::selected, &ListItem::setSelected)
        .doc("The selected look.")
        .defaultProperty("text")
        .signal<>("onClicked")
        .doc("Called when the item is clicked.");
}

void ListItem::render(Frame& frame) {
    md::ListItemOptions o;
    o.headline = m_text;
    o.supporting = m_supportingText;
    o.leadingIcon = m_icon;
    o.trailingText = m_trailingText;
    o.trailingIcon = m_trailingIcon;
    o.selected = m_selected;
    o.width = frame.size("width");
    if (md::listItem("##li", o)) emit("onClicked");
}

const std::string& ListItem::text() const { return m_text; }
void ListItem::setText(std::string text) { m_text = std::move(text); }
const std::string& ListItem::supportingText() const { return m_supportingText; }
void ListItem::setSupportingText(std::string text) { m_supportingText = std::move(text); }
const std::string& ListItem::icon() const { return m_icon; }
void ListItem::setIcon(std::string icon) { m_icon = std::move(icon); }
const std::string& ListItem::trailingText() const { return m_trailingText; }
void ListItem::setTrailingText(std::string text) { m_trailingText = std::move(text); }
const std::string& ListItem::trailingIcon() const { return m_trailingIcon; }
void ListItem::setTrailingIcon(std::string icon) { m_trailingIcon = std::move(icon); }
bool ListItem::selected() const { return m_selected; }
void ListItem::setSelected(bool selected) { m_selected = selected; }

std::optional<ImVec2> ListItem::intrinsicSize(const Frame& frame, float maxWidth) {
    md::ListItemOptions o;
    o.supporting = m_supportingText;
    o.width = frame.widthIn(maxWidth);
    return md::listItemSize(o);
}

} // namespace imlui
