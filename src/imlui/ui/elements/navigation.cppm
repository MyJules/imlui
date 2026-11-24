// App bars, navigation and menus: TopAppBar, NavigationItem, NavigationBar, NavigationRail,
// ModalNavigationDrawer, MenuBar, Menu, MenuItem.
//
// NavigationItem is the one destination element for every navigation container: it draws itself in the style of
// the current NavigationScope (imlui.ui.navigation_scope), which the containers open around their children.
module;

#include <algorithm>
#include <cmath>
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
import imlui.ui.navigation_scope;
import imlui.ui.props;
import imlui.ui.renderer;
import imlui.types.registry;

export namespace imlui {

void registerNavigationElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerNavigationTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

/// Number of visible visual children (inline containers spliced in).
int visibleItemCount(Renderer& r, const Node& n) {
    int count = 0;
    r.forEachChild(n, [&](int idx) {
        if (r.isVisual(n.state(), idx) && Node{n.state(), idx}.boolean("visible", true)) ++count;
    });
    return count;
}

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

// NavigationItem { icon, text, selected, badge, width, onClicked }: drawn in the style of its navigation container.
void navigationItem(Renderer&, const Node& n) {
    const std::string icon = n.string("icon", ""), label = n.text(), badge = n.string("badge", "");
    const bool selected = n.boolean("selected", false);
    bool clicked = false;
    const NavigationScope::Context& context = NavigationScope::current();
    switch (context.style) {
    case NavigationScope::Style::Bar:
        clicked = md::navigationBarItem("##nav", icon, label, selected, badge, context.itemWidth);
        break;
    case NavigationScope::Style::Rail:
        clicked = md::navigationRailItem("##nav", icon, label, selected, badge, context.itemWidth);
        break;
    case NavigationScope::Style::Drawer:
        clicked = md::navigationItem("##nav", icon, label, selected, badge,
                                     sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width"));
        break;
    }
    if (clicked) n.emit("onClicked");
}

// NavigationBar { color, width, <NavigationItems> }: the M3 bottom bar, 80dp tall. Destinations share the
// width equally.
void navigationBar(Renderer& r, const Node& n) {
    lua::State* state = n.state();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float w = sizeProp(n, "width");
    if (w <= 0) w = ImGui::GetContentRegionAvail().x + w;
    const float h = dp(80);
    const Color bg = n.color("color").value_or(theme().scheme.surfaceContainer);
    md::fillRoundRect(ImGui::GetWindowDrawList(), p, {p.x + w, p.y + h}, 0.0f, bg);
    md::pushSurface(bg);
    const int count = visibleItemCount(r, n);
    const float cell = count > 0 ? w / static_cast<float>(count) : w;
    {
        NavigationScope scope{{NavigationScope::Style::Bar, cell}};
        int i = 0;
        r.forEachChild(n, [&](int idx) {
            if (!r.isVisual(state, idx)) {
                r.render(state, idx);
                return;
            }
            if (!Node{state, idx}.boolean("visible", true)) return;
            ImGui::SetCursorScreenPos({p.x + std::floor(cell * static_cast<float>(i)), p.y});
            r.render(state, idx);
            ++i;
        });
    }
    md::popSurface();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
}

// NavigationRail { header, color, height, spacing = 12, align = "top"|"center"|"bottom", <NavigationItems> }:
// the M3 rail, 80dp wide, full height. Destinations stack below the optional header element.
void navigationRail(Renderer& r, const Node& n) {
    lua::State* state = n.state();
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = dp(80);
    float h = sizeProp(n, "height");
    if (h <= 0) h = ImGui::GetContentRegionAvail().y + h;
    h = std::max(h, dp(80));
    const Color bg = n.color("color").value_or(theme().scheme.surface);
    md::fillRoundRect(ImGui::GetWindowDrawList(), p, {p.x + w, p.y + h}, 0.0f, bg);
    md::pushSurface(bg);

    // Children are centered horizontally, using their width from the last frame.
    const auto renderCentered = [&](int idx, float y) {
        const ImGuiID key = ImGui::GetID(Node{state, idx}.ptr());
        const float cw = st->GetFloat(key, w);
        ImGui::SetCursorScreenPos({p.x + std::floor((w - cw) * 0.5f), y});
        r.render(state, idx);
        const ImVec2 size = ImGui::GetItemRectSize();
        st->SetFloat(key, size.x);
        return size.y;
    };

    float y = p.y + dp(12);
    if (n.push("header") == lua::Type::Table) {
        NavigationScope scope{{}};
        y += renderCentered(lua::gettop(state), y) + dp(28);
    }
    lua::pop(state);

    const float spacing = dp(n.numf("spacing", 12.0f));
    const float align = alignFactor(n.string("align", "top"));
    const ImGuiID totalKey = ImGui::GetID("##railItemsH");
    const float lastTotal = st->GetFloat(totalKey, 0.0f);
    y += std::max(0.0f, std::floor((p.y + h - dp(12) - y - lastTotal) * align));
    const float top = y;
    {
        NavigationScope scope{{NavigationScope::Style::Rail, w}};
        bool first = true;
        r.forEachChild(n, [&](int idx) {
            if (!r.isVisual(state, idx)) {
                r.render(state, idx);
                return;
            }
            if (!Node{state, idx}.boolean("visible", true)) return;
            if (!first) y += spacing;
            y += renderCentered(idx, y);
            first = false;
        });
    }
    st->SetFloat(totalKey, y - top);
    md::popSurface();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
}

// ModalNavigationDrawer { open (two-way), width = 360, onClosed, <children> }: a sheet over a scrim, above
// everything. Its children are stacked in a scrolling region; clicking the scrim or Escape closes it.
void modalNavigationDrawer(Renderer& r, const Node& n) {
    bool open = n.boolean("open", false);
    const bool was = open;
    if (md::beginModalDrawer("##drawer", &open, n.numf("width", 360.0f))) {
        NavigationScope scope{{}};
        r.renderStack(n);
        md::endModalDrawer();
    }
    if (open != was) {
        n.set("open", open);
        if (!open) n.emit("onClosed");
    }
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
    renderer.registerElement("NavigationBar", navigationBar);
    renderer.registerElement("NavigationRail", navigationRail);
    renderer.registerElement("ModalNavigationDrawer", modalNavigationDrawer, false);
    renderer.registerElement("MenuBar", menuBar);
    renderer.registerElement("Menu", menu);
    renderer.registerElement("MenuItem", menuItem);
}

void registerNavigationTypes(TypeRegistry&) {}

} // namespace imlui
