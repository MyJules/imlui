// Link: a text hyperlink that opens a URL, as a typed element.
export module imlui.ui.elements.link;

import std;

import imgui;
import imlui.color;
import imlui.skin;
import imlui.theme.typography;
import imlui.theme.font_scope;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// bodyLarge link text; clicking it opens `url` in the browser.
///
/// Props:   text = "" (positional), url = unset (the text)
class Link final : public Element {
public:
    static void declare(TypeBuilder<Link>& type);

    void render(Frame& frame) override;
    /// Its text, as ImGui measures a link.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& text() const;
    void setText(std::string text);
    /// Unset: the text is the URL.
    [[nodiscard]] const std::optional<std::string>& url() const;
    void setUrl(std::optional<std::string> url);

private:
    std::string m_text;
    std::optional<std::string> m_url;
};

} // namespace imlui

module :private;

namespace imlui {

void Link::declare(TypeBuilder<Link>& type) {
    type.doc("A clickable text link (`bodyLarge`) that opens `url` in the browser.\n"
             "`Link { \"https://example.com\" }` sets `text`. Natural size: the text's.")
        .property("text", &Link::text, &Link::setText)
        .doc("The label. The positional value: `Link { \"Docs\", url = \"https://...\" }`.")
        .property("url", &Link::url, &Link::setUrl)
        .doc("The URL to open; unset: `text`.")
        .defaultProperty("text");
}

void Link::render(Frame&) {
    const FontScope font{TextStyle::BodyLarge};
    const std::optional<Color> color = skin().linkColor();
    if (color) ImGui::PushStyleColor(ImGuiCol_TextLink, color->vec4());
    ImGui::TextLinkOpenURL(m_text.c_str(), m_url.value_or(m_text).c_str());
    if (color) ImGui::PopStyleColor();
}

const std::string& Link::text() const { return m_text; }
void Link::setText(std::string text) { m_text = std::move(text); }
const std::optional<std::string>& Link::url() const { return m_url; }
void Link::setUrl(std::optional<std::string> url) { m_url = std::move(url); }

std::optional<ImVec2> Link::intrinsicSize(const Frame&, float) {
    // ImGui::TextLink's item: its label up to a "##" (TextLinkOpenURL draws one).
    const FontScope font{TextStyle::BodyLarge};
    return ImGui::CalcTextSize(m_text.c_str(), nullptr, true);
}

} // namespace imlui
