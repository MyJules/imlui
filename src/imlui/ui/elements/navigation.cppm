// App bars, navigation and menus: TopAppBar, NavigationItem, MenuBar, Menu, MenuItem.
module;

#include <string>

export module imlui.ui.elements.navigation;

import lua;
import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.theme;
import imlui.widgets;
import imlui.lua.node;
import imlui.ui.props;
import imlui.ui.renderer;

export namespace imlui {

void registerNavigationElements(Renderer& renderer);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

// TopAppBar { title, navigationIcon, onNavigation, variant = "small"|"center"|"medium"|"large", <actions...> }
void topAppBar(Renderer& r, const Node& n) {
    lua::State* state = n.state();
    const Scheme& s = theme().scheme;
    const std::string variant = n.string("variant", "small");
    const float barH = dp(64);
    const float h = variant == "medium" ? dp(112) : variant == "large" ? dp(152) : barH;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float w = sizeProp(n, "width");
    if (w <= 0) w = ImGui::GetContentRegionAvail().x + w;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Color bg = n.color("color").value_or(md::surfaceColor());
    dl->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::ColorConvertFloat4ToU32(bg.vec4()));
    ImGuiStorage* st = ImGui::GetStateStorage();

    float titleX = p.x + dp(16);
    const std::string navIcon = n.string("navigationIcon", "");
    if (!navIcon.empty()) {
        ImGui::SetCursorScreenPos({p.x + dp(4), p.y + (barH - dp(48)) * 0.5f});
        if (md::iconButton("##nav", navIcon)) n.emit("onNavigation");
        titleX = p.x + dp(56);
    }
    // actions, right aligned, each vertically centered in the 64dp bar
    const ImGuiID widthKey = ImGui::GetID("##actionsW");
    const float lastW = st->GetFloat(widthKey, 0.0f);
    float x = p.x + w - dp(4) - lastW, total = 0;
    r.forEachChild(n, [&](int idx) {
        if (!r.isVisual(state, idx)) {
            r.render(state, idx);
            return;
        }
        Node c{state, idx};
        if (!c.boolean("visible", true)) return;
        const ImGuiID ck = ImGui::GetID(c.ptr());
        const float ch = st->GetFloat(ck, dp(48));
        ImGui::SetCursorScreenPos({x, p.y + (barH - ch) * 0.5f});
        r.render(state, idx);
        ImVec2 sz = ImGui::GetItemRectSize();
        st->SetFloat(ck, sz.y);
        x += sz.x;
        total += sz.x;
    });
    st->SetFloat(widthKey, total);

    const std::string title = n.string("title", "");
    if (variant == "medium" || variant == "large") {
        TextStyle ts = variant == "large" ? TextStyle::HeadlineMedium : TextStyle::HeadlineSmall;
        float th = font(ts)->CalcTextSizeA(fontPixels(ts), 3.4e38f, 0.0f, title.c_str()).y;
        dl->AddText(font(ts), fontPixels(ts), {p.x + dp(16), p.y + h - dp(variant == "large" ? 28 : 24) - th},
                    ImGui::ColorConvertFloat4ToU32(s.onSurface.vec4()), title.c_str());
    } else {
        TextStyle ts = TextStyle::TitleLarge;
        ImVec2 tsz = font(ts)->CalcTextSizeA(fontPixels(ts), 3.4e38f, 0.0f, title.c_str());
        float tx = variant == "center" ? p.x + (w - tsz.x) * 0.5f : titleX;
        dl->AddText(font(ts), fontPixels(ts), {tx, p.y + (barH - tsz.y) * 0.5f},
                    ImGui::ColorConvertFloat4ToU32(s.onSurface.vec4()), title.c_str());
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
}

void navigationItem(Renderer&, const Node& n) {
    if (md::navigationItem("##nav", n.string("icon", ""), n.text(), n.boolean("selected", false), n.string("badge", ""),
                           sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width")))
        n.emit("onClicked");
}

void menuBar(Renderer& r, const Node& n) {
    if (ImGui::BeginMenuBar()) {
        r.renderChildren(n);
        ImGui::EndMenuBar();
    }
}

void menu(Renderer& r, const Node& n) {
    std::string t = labelId(n.string("title", ""), "menu");
    md::pushMenuStyle();
    ImGui::PopStyleVar(); // keep the menubar's own item spacing
    const bool open = ImGui::BeginMenu(t.c_str());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    if (open) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        r.renderChildren(n);
        ImGui::PopStyleVar();
        ImGui::EndMenu();
    }
}

void menuItem(Renderer&, const Node& n) {
    const bool checkable = n.has("checked");
    bool checked = n.boolean("checked", false);
    std::string icon = n.string("icon", "");
    if (checkable) icon = checked ? "check" : "";
    if (md::menuItem("##mi", n.text(), icon, n.string("shortcut", ""), false, !checkable, checkable || n.has("icon"))) {
        if (checkable) n.set("checked", !checked);
        n.emit("onTriggered");
    }
}

} // namespace

void registerNavigationElements(Renderer& renderer) {
    renderer.registerElement("TopAppBar", topAppBar);
    renderer.registerElement("NavigationItem", navigationItem);
    renderer.registerElement("MenuBar", menuBar);
    renderer.registerElement("Menu", menu);
    renderer.registerElement("MenuItem", menuItem);
}

} // namespace imlui
