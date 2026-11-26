// TextField: the Material 3 text field (filled or outlined), as a typed element. TextArea
// (imlui.ui.elements.text_area) is the multi-line subclass.
export module imlui.ui.elements.text_field;

import std;

import imgui;
import imlui.widgets.text_field;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A Material text field with a floating label (TextArea extends it, multi-line).
///
/// Props:   text = "", label = "", placeholderText = "", leadingIcon = "", trailingIcon = "", supportingText = "",
///          variant = "filled" ("outlined"), password = false, echoMode = "" ("password"), readOnly = false,
///          error = false
/// Raw:     width, height (TextArea only)
/// Signals: onTextChanged(self, text), onAccepted(self, text), onTrailingClicked(self)
/// Two-way: text
class TextField : public Element {
public:
    static void declare(TypeBuilder<TextField>& type);

    void render(Frame& frame) override;
    /// Its width (natural 280dp, fixed, or a fill of `maxWidth`), the field (a TextArea: `height`, else 120dp) and its
    /// supporting text. None for a fill height.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    [[nodiscard]] const std::string& label() const;
    void setLabel(std::string label);
    [[nodiscard]] const std::string& placeholderText() const;
    void setPlaceholderText(std::string text);
    [[nodiscard]] const std::string& leadingIcon() const;
    void setLeadingIcon(std::string icon);
    [[nodiscard]] const std::string& trailingIcon() const;
    void setTrailingIcon(std::string icon);
    [[nodiscard]] const std::string& supportingText() const;
    void setSupportingText(std::string text);
    [[nodiscard]] const std::string& variant() const;
    void setVariant(std::string variant);
    [[nodiscard]] bool password() const;
    void setPassword(bool password);
    /// "password" hides the text like `password = true`.
    [[nodiscard]] const std::string& echoMode() const;
    void setEchoMode(std::string mode);
    [[nodiscard]] bool readOnly() const;
    void setReadOnly(bool readOnly);
    [[nodiscard]] bool error() const;
    void setError(bool error);
    [[nodiscard]] bool multiline() const;

protected:
    bool m_multiline = false;

private:
    std::string m_text;
    std::string m_label;
    std::string m_placeholderText;
    std::string m_leadingIcon;
    std::string m_trailingIcon;
    std::string m_supportingText;
    std::string m_variant = "filled";
    bool m_password = false;
    std::string m_echoMode;
    bool m_readOnly = false;
    bool m_error = false;
};

} // namespace imlui

module :private;

namespace imlui {

void TextField::declare(TypeBuilder<TextField>& type) {
    type.doc("A single-line Material 3 text field with a floating label, editing `text` (two-way). Clicking "
             "anywhere in the container (but the trailing icon) focuses the input. `TextArea` is the multi-line one.\n"
             "Natural size: fills the width (`width`, raw); 56dp high, plus the supporting text.");
    type.fillsWidth(); // an unset width fills
    type.property("text", &TextField::text, &TextField::setText)
        .doc("**Two-way.** The content.")
        .property("label", &TextField::label, &TextField::setLabel)
        .doc("The floating label.")
        .property("placeholderText", &TextField::placeholderText, &TextField::setPlaceholderText)
        .doc("A hint, shown while the field is focused and empty (always while empty if there's no `label`).")
        .property("leadingIcon", &TextField::leadingIcon, &TextField::setLeadingIcon)
        .doc("A leading Material icon name; `\"\"` for none.")
        .property("trailingIcon", &TextField::trailingIcon, &TextField::setTrailingIcon)
        .doc("A trailing Material icon name, clickable (`onTrailingClicked`); `\"\"` for none.")
        .property("supportingText", &TextField::supportingText, &TextField::setSupportingText)
        .doc("Helper text below the field, error-colored while `error` is set.")
        .property("variant", &TextField::variant, &TextField::setVariant)
        .doc("`\"filled\"` (default) or `\"outlined\"`.")
        .property("password", &TextField::password, &TextField::setPassword)
        .doc("Masks the input and adds a visibility toggle as the trailing icon, unless `trailingIcon` is set.")
        .property("echoMode", &TextField::echoMode, &TextField::setEchoMode)
        .doc("`\"password\"` does the same as `password = true`.")
        .property("readOnly", &TextField::readOnly, &TextField::setReadOnly)
        .doc("The text can't be edited (it can still be selected).")
        .property("error", &TextField::error, &TextField::setError)
        .doc("The error colors.")
        .signal<std::string>("onTextChanged", {"text"})
        .doc("Called on every edit, with the new `text` (after it was written back).")
        .signal<std::string>("onAccepted", {"text"})
        .doc("Called when Enter is pressed, with the `text`.")
        .signal<>("onTrailingClicked")
        .doc("Called when the trailing icon is clicked (the password visibility toggle too).");
}

void TextField::render(Frame& frame) {
    md::TextFieldOptions o;
    o.label = m_label;
    o.placeholder = m_placeholderText;
    o.leadingIcon = m_leadingIcon;
    o.trailingIcon = m_trailingIcon;
    o.supporting = m_supportingText;
    o.outlined = m_variant == "outlined";
    o.password = m_password || m_echoMode == "password";
    o.multiline = m_multiline;
    o.readOnly = m_readOnly;
    o.error = m_error;
    o.width = frame.size("width");
    o.height = frame.resolvedHeight(0.0f);
    std::string text = m_text;
    const md::TextFieldResult res = md::textField("##tf", text, o);
    if (res.changed) {
        m_text = std::move(text);
        notify("text"); // writes the text back and emits onTextChanged(text)
    }
    if (res.accepted) emit("onAccepted", m_text);
    if (res.trailingClicked) emit("onTrailingClicked");
}

const std::string& TextField::text() const { return m_text; }
void TextField::setText(std::string text) { m_text = std::move(text); }
const std::string& TextField::label() const { return m_label; }
void TextField::setLabel(std::string label) { m_label = std::move(label); }
const std::string& TextField::placeholderText() const { return m_placeholderText; }
void TextField::setPlaceholderText(std::string text) { m_placeholderText = std::move(text); }
const std::string& TextField::leadingIcon() const { return m_leadingIcon; }
void TextField::setLeadingIcon(std::string icon) { m_leadingIcon = std::move(icon); }
const std::string& TextField::trailingIcon() const { return m_trailingIcon; }
void TextField::setTrailingIcon(std::string icon) { m_trailingIcon = std::move(icon); }
const std::string& TextField::supportingText() const { return m_supportingText; }
void TextField::setSupportingText(std::string text) { m_supportingText = std::move(text); }
const std::string& TextField::variant() const { return m_variant; }
void TextField::setVariant(std::string variant) { m_variant = std::move(variant); }
bool TextField::password() const { return m_password; }
void TextField::setPassword(bool password) { m_password = password; }
const std::string& TextField::echoMode() const { return m_echoMode; }
void TextField::setEchoMode(std::string mode) { m_echoMode = std::move(mode); }
bool TextField::readOnly() const { return m_readOnly; }
void TextField::setReadOnly(bool readOnly) { m_readOnly = readOnly; }
bool TextField::error() const { return m_error; }
void TextField::setError(bool error) { m_error = error; }
bool TextField::multiline() const { return m_multiline; }

std::optional<ImVec2> TextField::intrinsicSize(const Frame& frame, float maxWidth) {
    const float height = frame.size("height");
    if (height < 0) return std::nullopt; // fills a height known only where it renders
    md::TextFieldOptions o;
    o.label = m_label;
    o.trailingIcon = m_trailingIcon;
    o.supporting = m_supportingText;
    o.outlined = m_variant == "outlined";
    o.password = m_password || m_echoMode == "password";
    o.multiline = m_multiline;
    o.width = frame.widthIn(maxWidth);
    o.height = height;
    return md::textFieldSize(o);
}

} // namespace imlui
