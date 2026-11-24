// Progress, data and custom drawing: CircularProgress, BusyIndicator, Plot, Table, Canvas.
// (ProgressBar is a typed element: imlui.ui.elements.progress_bar.)
module;

#include <algorithm>
#include <optional>
#include <string>

export module imlui.ui.elements.data;

import lua;
import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.theme;
import imlui.widgets;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.painter;
import imlui.ui.frame_request;
import imlui.ui.props;
import imlui.ui.renderer;

export namespace imlui {

void registerDataElements(Renderer& renderer);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

void circularProgress(Renderer&, const Node& n) {
    md::circularProgress(n.boolean("indeterminate", false) ? -1.0f : n.numf("value", 0.0f), sizeProp(n, "size"),
                         n.numf("thickness", 4.0f));
}

void busyIndicator(Renderer&, const Node& n) {
    if (n.boolean("running", true)) {
        md::circularProgress(-1.0f, sizeProp(n, "size"), n.numf("thickness", 4.0f));
    } else {
        const float size = sizeProp(n, "size") > 0 ? sizeProp(n, "size") : dp(48);
        ImGui::Dummy({size, size});
    }
}

void plot(Renderer&, const Node& n) {
    auto values = n.floats("values");
    std::string title = n.string("title", n.string("label", ""));
    md::ChartOptions o;
    o.bars = n.string("kind", "lines") == "bars" || n.string("kind", "lines") == "histogram";
    o.fill = n.boolean("fill", true);
    o.width = sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width");
    o.height = sizeProp(n, "height");
    o.title = title;
    o.color = n.color("color");
    if (n.has("min") || n.has("max")) {
        o.autoRange = false;
        o.min = n.numf("min", 0.0f);
        o.max = n.numf("max", 1.0f);
    }
    md::chart("##chart", values, o);
}

void table(Renderer&, const Node& n) {
    lua::State* state = n.state();
    auto headers = n.strings("columns");
    if (headers.empty()) return;
    const Scheme& s = theme().scheme;
    const float rowH = dp(n.numf("rowHeight", 52.0f)), headH = dp(56);
    const bool outlined = n.boolean("outlined", true);
    if (outlined) md::beginCard("##tablecard", md::CardVariant::Outlined, sizeProp(n, "width"), 0.0f);
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, s.outlineVariant.vec4());
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, s.outlineVariant.vec4());
    ImGui::PushStyleColor(ImGuiCol_Header, s.secondaryContainer.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {dp(16), 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {dp(8), 0});
    ImGuiTableFlags f = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("##table", static_cast<int>(headers.size()), f)) {
        for (auto& h : headers)
            ImGui::TableSetupColumn(h.c_str());
        ImGui::TableNextRow(0, headH);
        for (auto& h : headers) {
            ImGui::TableNextColumn();
            md::tableHeaderCell(h, headH);
        }
        if (n.push("rows") == lua::Type::Table) {
            const int rows = lua::gettop(state);
            const lua::Integer nr = lua::rawlen(state, rows);
            const int selected = static_cast<int>(n.number("currentRow", 0));
            for (lua::Integer i = 1; i <= nr; ++i) {
                lua::rawgeti(state, rows, i);
                ImGui::TableNextRow(0, rowH);
                for (int c = 0; c < static_cast<int>(headers.size()); ++c) {
                    ImGui::TableNextColumn();
                    lua::rawgeti(state, -1, c + 1);
                    std::string cell = lua::isnil(state, -1) ? std::string{} : lua::toStringAny(state, -1);
                    lua::pop(state);
                    if (c == 0) {
                        ImGui::PushID(static_cast<int>(i));
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        if (ImGui::Selectable("##row", selected == i,
                                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                              {0, rowH})) {
                            n.set("currentRow", static_cast<double>(i));
                            n.emit("onRowClicked", {static_cast<double>(i)});
                        }
                        ImGui::SetCursorScreenPos(p);
                        ImGui::PopID();
                    }
                    md::tableCell(cell, rowH,
                                  selected == i ? std::optional<Color>{s.onSecondaryContainer} : std::nullopt);
                }
                lua::pop(state);
            }
        }
        lua::pop(state);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (outlined) md::endCard();
}

void canvas(Renderer&, const Node& n) {
    lua::State* state = n.state();
    float w = sizeProp(n, "width"), h = sizeProp(n, "height");
    if (w <= 0) w = std::max(1.0f, ImGui::GetContentRegionAvail().x + (w < 0 ? w + 1.0f : 0.0f));
    if (h <= 0) h = dp(200);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##canvas", {w, h});
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImVec2 m = ImGui::GetMousePos();
    const float s = dpiScale();
    if (clicked) n.emit("onClicked", {(m.x - p.x) / s, (m.y - p.y) / s});

    // `animated = true`: onPaint draws something time-dependent, so render continuously.
    if (n.boolean("animated", false)) requestFrame();

    n.pushRaw("onPaint");
    if (!lua::isfunction(state, -1)) {
        lua::pop(state);
        return;
    }
    lua::pushvalue(state, n.index());
    lua::createtable(state, 0, 8);
    lua::pushnumber(state, static_cast<double>(w / s));
    lua::setfield(state, -2, "width");
    lua::pushnumber(state, static_cast<double>(h / s));
    lua::setfield(state, -2, "height");
    lua::pushnumber(state, static_cast<double>((m.x - p.x) / s));
    lua::setfield(state, -2, "mouseX");
    lua::pushnumber(state, static_cast<double>((m.y - p.y) / s));
    lua::setfield(state, -2, "mouseY");
    lua::pushboolean(state, hovered);
    lua::setfield(state, -2, "hovered");
    lua::pushboolean(state, active);
    lua::setfield(state, -2, "pressed");
    lua::pushnumber(state, ImGui::GetTime());
    lua::setfield(state, -2, "time");
    lua::getfield(state, lua::registryIndex, "imlui.painter");
    lua::setmetatable(state, -2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, {p.x + w, p.y + h}, true);
    luaui::beginPaint(dl, p);
    luaui::safeCall(state, 2, 0);
    luaui::endPaint();
    dl->PopClipRect();
}

} // namespace

void registerDataElements(Renderer& renderer) {
    renderer.registerElement("CircularProgress", circularProgress);
    renderer.registerElement("BusyIndicator", busyIndicator);
    renderer.registerElement("Plot", plot);
    renderer.registerElement("Table", table);
    renderer.registerElement("Canvas", canvas);
}

} // namespace imlui
