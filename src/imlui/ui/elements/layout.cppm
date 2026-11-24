// Layout elements: Column, Item, Row, Grid, ScrollView, ListView, Card,
// TabBar, Tab, Section, Expander, TreeNode and the inline containers
// Repeater, If, Loader, Responsive.
module;

#include <algorithm>
#include <string>
#include <vector>

export module imlui.ui.elements.layout;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.theme.typography;
import imlui.theme.theme;
import imlui.theme.font_scope;
import imlui.widgets;
import imlui.lua.node;
import imlui.ui.props;
import imlui.ui.renderer;

export namespace imlui {

void registerLayoutElements(Renderer& renderer);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

void column(Renderer& r, const Node& n) {
    const float pad = n.numf("padding", 0.0f);
    if (pad > 0) {
        ImGui::Dummy({0, dp(pad) - ImGui::GetStyle().ItemSpacing.y});
        ImGui::Indent(dp(pad));
    }
    float w = sizeProp(n, "width");
    if (w != 0) {
        // A fixed-width column: reserve the width so `align` and fills work inside it.
        ImVec2 p = ImGui::GetCursorScreenPos();
        float ww = w > 0 ? w : ImGui::GetContentRegionAvail().x + w + 1.0f;
        ImGui::Dummy({ww, 0});
        ImGui::SetCursorScreenPos(p);
        ImGui::PushItemWidth(ww);
        r.renderStack(n);
        ImGui::PopItemWidth();
    } else {
        r.renderStack(n);
    }
    if (pad > 0) {
        ImGui::Unindent(dp(pad));
        ImGui::Dummy({0, dp(pad) - ImGui::GetStyle().ItemSpacing.y});
    }
}

// Row: horizontal layout.
//   spacing  gap between children (dp)
//   align    vertical: "center" (default) | "top" | "bottom"
//   justify  horizontal: "start" | "center" | "end" | "spaceBetween"
//   Spacer { fill = true } children share the remaining width.
// Sizes are measured on the previous frame (immediate mode), so a
// layout settles within one frame.
void row(Renderer& r, const Node& n) {
    lua::State* state = n.state();
    const auto spacing = n.number("spacing");
    const float sp = spacing ? dp(static_cast<float>(*spacing)) : ImGui::GetStyle().ItemSpacing.x;
    const std::string valign = n.string("align", "center");
    const std::string justify = n.string("justify", "start");
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID heightKey = ImGui::GetID("##rowH"), fixedKey = ImGui::GetID("##rowFixed"),
                  fillsKey = ImGui::GetID("##rowFills"), countKey = ImGui::GetID("##rowCount");
    const float rowH = st->GetFloat(heightKey, 0.0f), lastFixed = st->GetFloat(fixedKey, 0.0f);
    const int lastFills = st->GetInt(fillsKey, 0), lastCount = st->GetInt(countKey, 0);
    float availW = sizeProp(n, "width");
    if (availW <= 0) availW = ImGui::GetContentRegionAvail().x + availW;
    const float free = std::max(0.0f, availW - lastFixed);
    const float fillW = lastFills > 0 ? free / static_cast<float>(lastFills) : 0.0f;
    float offset = 0.0f, extraGap = 0.0f;
    if (lastFills == 0) {
        if (justify == "center")
            offset = free * 0.5f;
        else if (justify == "end")
            offset = free;
        else if (justify == "spaceBetween" && lastCount > 1)
            extraGap = free / static_cast<float>(lastCount - 1);
    }

    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float top = start.y;
    float maxH = 0.0f, fixed = 0.0f;
    int fills = 0, count = 0;
    float x = start.x + offset;
    r.forEachChild(n, [&](int idx) {
        if (!r.isVisual(state, idx)) {
            r.render(state, idx);
            return;
        }
        Node c{state, idx};
        if (!c.boolean("visible", true)) return;
        if (count > 0) x += sp + extraGap;
        const bool fill = c.type() == "Spacer" && c.boolean("fill", false);
        if (fill) {
            ImGui::SetCursorScreenPos({x, top});
            ImGui::Dummy({std::max(0.0f, fillW), 0.0f});
            x += std::max(0.0f, fillW);
            ++fills;
            ++count;
            return;
        }
        const ImGuiID ck = ImGui::GetID(c.ptr());
        const float ch = st->GetFloat(ck, 0.0f);
        float y = top;
        if (rowH > 0 && ch > 0) {
            if (valign == "center")
                y = top + std::max(0.0f, (rowH - ch) * 0.5f);
            else if (valign == "bottom")
                y = top + std::max(0.0f, rowH - ch);
        }
        ImGui::SetCursorScreenPos({x, y});
        r.render(state, idx);
        ImVec2 sz = ImGui::GetItemRectSize();
        st->SetFloat(ck, sz.y);
        maxH = std::max(maxH, sz.y);
        fixed += sz.x;
        x += sz.x;
        ++count;
    });
    if (count > 1) fixed += sp * static_cast<float>(count - 1);
    // Make the row's extent explicit (fills may end the row).
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy({std::max(1.0f, x - start.x), std::max(1.0f, maxH)});
    st->SetFloat(heightKey, maxH);
    st->SetFloat(fixedKey, fixed);
    st->SetInt(fillsKey, fills);
    st->SetInt(countKey, count);
}

void grid(Renderer& r, const Node& n) {
    int cols = std::max(1, static_cast<int>(n.number("columns", 2)));
    ImGuiTableFlags f = ImGuiTableFlags_SizingStretchSame;
    if (n.boolean("border", false)) f |= ImGuiTableFlags_Borders;
    float sp = n.numf("spacing", 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {dp(sp) * 0.5f, dp(sp) * 0.5f});
    if (ImGui::BeginTable("##grid", cols, f, {sizeProp(n, "width"), 0})) {
        r.forEachChild(n, [&](int idx) {
            if (!r.isVisual(n.state(), idx)) {
                r.render(n.state(), idx);
                return;
            }
            ImGui::TableNextColumn();
            r.render(n.state(), idx);
        });
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void scrollView(Renderer& r, const Node& n) {
    ImGuiChildFlags cf = n.boolean("border", false) ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    const float pad = n.numf("padding", 0.0f);
    const auto bg = n.color("color");
    const float rounding = n.numf("rounding", theme().shape.medium);
    if (pad > 0) {
        cf |= ImGuiChildFlags_AlwaysUseWindowPadding;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(pad), dp(pad)});
    }
    if (bg) ImGui::PushStyleColor(ImGuiCol_ChildBg, bg->vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, dp(rounding));
    ImGui::BeginChild("##scroll", {sizeProp(n, "width"), sizeProp(n, "height")}, cf);
    ImGui::PopStyleVar();
    if (bg) ImGui::PopStyleColor();
    if (pad > 0) ImGui::PopStyleVar();
    if (bg) md::pushSurface(*bg);
    r.renderStack(n);
    if (bg) md::popSurface();
    ImGui::EndChild();
}

void card(Renderer& r, const Node& n) {
    auto rounding = n.number("rounding");
    md::beginCard("##card", md::cardVariant(n.string("variant", "elevated")), sizeProp(n, "width"),
                  n.numf("padding", 16.0f), sizeProp(n, "height"), n.color("color"),
                  rounding ? static_cast<float>(*rounding) : -1.0f);
    r.renderStack(n);
    md::endCard();
}

void tabBar(Renderer& r, const Node& n) {
    lua::State* state = n.state();
    // Collect tab headers.
    std::vector<md::TabInfo> tabs;
    r.forEachChild(n, [&](int idx) {
        Node t{state, idx};
        if (t.boolean("visible", true)) tabs.push_back({t.string("title", "Tab"), t.string("icon", "")});
    });
    int cur = static_cast<int>(n.number("currentIndex", 1)) - 1;
    cur = std::clamp(cur, 0, std::max(0, static_cast<int>(tabs.size()) - 1));
    if (md::tabs("##tabs", tabs, &cur, n.string("variant", "primary") == "secondary",
                 sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width"))) {
        n.set("currentIndex", static_cast<double>(cur + 1));
        n.emit("onCurrentChanged", {static_cast<double>(cur + 1)});
    }
    ImGui::Dummy({0, dp(8)});
    int i = 0;
    r.forEachChild(n, [&](int idx) {
        Node t{state, idx};
        if (!t.boolean("visible", true)) return;
        if (i++ == cur) {
            ImGui::PushID(t.ptr());
            r.renderStack(t);
            ImGui::PopID();
        }
    });
}

void expander(Renderer& r, const Node& n) {
    // `expanded` may be plain (stored on the element) or bound.
    bool open = n.boolean("expanded", true);
    if (md::expander("##exp", n.string("title", n.text()), n.string("supportingText", ""), &open,
                     sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width"))) {
        n.set("expanded", open);
        n.emit("onToggled", {open});
    }
    if (open) {
        ImGui::Indent(dp(16));
        r.renderStack(n);
        ImGui::Unindent(dp(16));
    }
}

void treeNode(Renderer& r, const Node& n) {
    std::string t = labelId(n.text(), "tree");
    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (n.boolean("expanded", false)) f |= ImGuiTreeNodeFlags_DefaultOpen;
    FontScope fs{TextStyle::BodyLarge};
    if (ImGui::TreeNodeEx(t.c_str(), f)) {
        r.renderChildren(n);
        ImGui::TreePop();
    }
}

} // namespace

void registerLayoutElements(Renderer& renderer) {
    renderer.registerElement("Column", column);
    renderer.registerElement("Item", column);
    renderer.registerElement("Row", row);
    renderer.registerElement("Grid", grid);
    renderer.registerElement("ScrollView", scrollView);
    renderer.registerElement("ListView", scrollView); // children come from model/delegate (Lua side)
    renderer.registerElement("Card", card);
    renderer.registerElement("TabBar", tabBar);
    // Tabs are rendered by TabBar; a stray Tab renders its content inline.
    renderer.registerElement("Tab", [](Renderer& r, const Node& n) { r.renderStack(n); });
    renderer.registerElement("Section", expander);
    renderer.registerElement("Expander", expander);
    renderer.registerElement("TreeNode", treeNode);
    // Inline containers are normally spliced into their parent by
    // forEachChild; these handlers only run when one is used as the root.
    for (const char* name : {"Repeater", "If", "Loader", "Responsive"})
        renderer.registerElement(name, [](Renderer& r, const Node& n) { r.renderChildren(n); });
}

} // namespace imlui
