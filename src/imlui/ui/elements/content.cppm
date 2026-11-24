// Text and decoration: Text, Label, Icon, Link, Separator, Divider, Spacer.
module;

#include <algorithm>
#include <string>

export module imlui.ui.elements.content;

import imgui;
import imlui.theme.typography;
import imlui.theme.theme;
import imlui.theme.font_scope;
import imlui.widgets;
import imlui.lua.node;
import imlui.ui.props;
import imlui.ui.renderer;
import imlui.types.registry;

export namespace imlui {

void registerContentElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerContentTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

void text(Renderer&, const Node& n) {
    auto style = textStyleFromName(n.string("style", "bodyLarge")).value_or(TextStyle::BodyLarge);
    md::text(n.text(), style, n.color("color"), n.boolean("wrap", false), alignFactor(n.string("align", "start")));
}

void icon(Renderer&, const Node& n) {
    md::icon(n.string("name", n.string("icon", n.text())), n.numf("size", 24.0f), n.color("color"));
}

void link(Renderer&, const Node& n) {
    std::string t = n.text();
    std::string url = n.string("url", t);
    FontScope fs{TextStyle::BodyLarge};
    ImGui::TextLinkOpenURL(t.c_str(), url.c_str());
}

void separator(Renderer&, const Node& n) {
    std::string t = n.text();
    if (t.empty()) {
        md::divider(n.numf("inset", 0.0f), n.numf("insetEnd", 0.0f));
    } else {
        FontScope fs{TextStyle::TitleSmall};
        ImGui::PushStyleColor(ImGuiCol_Text, theme().scheme.onSurfaceVariant.vec4());
        ImGui::SeparatorText(t.c_str());
        ImGui::PopStyleColor();
    }
}

void divider(Renderer&, const Node& n) { md::divider(n.numf("inset", 0.0f), n.numf("insetEnd", 0.0f)); }

void spacer(Renderer&, const Node& n) {
    ImGui::Dummy({std::max(1.0f, sizeProp(n, "width")), std::max(1.0f, sizeProp(n, "height"))});
}

} // namespace

void registerContentElements(Renderer& renderer) {
    renderer.registerElement("Text", text);
    renderer.registerElement("Label", text);
    renderer.registerElement("Icon", icon);
    renderer.registerElement("Link", link);
    renderer.registerElement("Separator", separator);
    renderer.registerElement("Divider", divider);
    renderer.registerElement("Spacer", spacer);
}

void registerContentTypes(TypeRegistry&) {}

} // namespace imlui
