// Link: a text hyperlink that opens a URL, as a typed element.
export module imlui.ui.elements.link;

import std;

import imgui;
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
    type.property("text", &Link::text, &Link::setText)
        .property("url", &Link::url, &Link::setUrl)
        .defaultProperty("text");
}

void Link::render(Frame&) {
    const FontScope font{TextStyle::BodyLarge};
    ImGui::TextLinkOpenURL(m_text.c_str(), m_url.value_or(m_text).c_str());
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
