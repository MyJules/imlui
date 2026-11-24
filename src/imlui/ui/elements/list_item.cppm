// ListItem: the Material 3 one- or two-line list item, as a typed element.
export module imlui.ui.elements.list_item;

import std;

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
    type.property("text", &ListItem::text, &ListItem::setText)
        .property("supportingText", &ListItem::supportingText, &ListItem::setSupportingText)
        .property("icon", &ListItem::icon, &ListItem::setIcon)
        .property("trailingText", &ListItem::trailingText, &ListItem::setTrailingText)
        .property("trailingIcon", &ListItem::trailingIcon, &ListItem::setTrailingIcon)
        .property("selected", &ListItem::selected, &ListItem::setSelected)
        .defaultProperty("text")
        .signal("onClicked");
}

void ListItem::render(Frame& frame) {
    md::ListItemOptions o;
    o.headline = m_text;
    o.supporting = m_supportingText;
    o.leadingIcon = m_icon;
    o.trailingText = m_trailingText;
    o.trailingIcon = m_trailingIcon;
    o.selected = m_selected;
    o.width = frame.size("width", -1.0f);
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

} // namespace imlui
