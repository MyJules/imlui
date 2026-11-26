// Chip: the Material 3 chip (filter, assist, suggestion, input), as a typed element.
export module imlui.ui.elements.chip;

import std;

import imgui;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A 32dp Material chip. A checkable filter chip toggles `checked` when clicked.
///
/// Props:   text = "" (positional), checked = false, checkable = true,
///          variant = "filter" ("assist", "suggestion", "input"), icon = ""
/// Signals: onClicked(self, checked) (`checked` is the negated state before the click)
/// Two-way: checked (checkable filter chips)
class Chip final : public Element {
public:
    static void declare(TypeBuilder<Chip>& type);

    void render(Frame& frame) override;
    /// Its label, icon and padding.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

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
    type.doc("A 32dp Material 3 chip. A `\"filter\"` chip toggles `checked` on a click (two-way, unless "
             "`checkable = false`) and shows a check while selected; the other variants only report clicks.\n"
             "`Chip { \"lua\" }` sets `text`. Natural size: its label, icon and padding, 32dp high.")
        .property("text", &Chip::text, &Chip::setText)
        .doc("The label. The positional value: `Chip { \"lua\" }`.")
        .property("checked", &Chip::checked, &Chip::setChecked)
        .doc("The selected state. **Two-way** on a checkable `\"filter\"` chip.")
        .property("checkable", &Chip::checkable, &Chip::setCheckable)
        .doc("`false` stops a filter chip from toggling itself (it still reports clicks). Default `true`.")
        .property("variant", &Chip::variant, &Chip::setVariant)
        .doc("`\"filter\"` (default), `\"assist\"`, `\"suggestion\"` or `\"input\"`.")
        .property("icon", &Chip::icon, &Chip::setIcon)
        .doc("A leading Material icon name; `\"\"` for none.")
        .defaultProperty("text")
        .signal<bool>("onClicked", {"checked"})
        .doc("Called when the chip is clicked, with `not` the state before the click (for every variant, also when it "
             "doesn't toggle).");
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

std::optional<ImVec2> Chip::intrinsicSize(const Frame&, float) {
    return md::chipSize(Frame::labelId(m_text, "chip").c_str(), m_checked, md::chipVariant(m_variant), m_icon);
}

} // namespace imlui
