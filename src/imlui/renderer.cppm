// Renderer: walks the Lua element tree every frame and draws it with ImGui
// (through the Material widgets in imlui.widgets).
//
// Each element type ("Button", "Column", ...) maps to a handler. Apps can add
// their own C++ elements with Renderer::registerElement().
module;

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module imlui.renderer;

import lua;
import imgui;
import imlui.color;
import imlui.theme;
import imlui.widgets;
import imlui.luaui;

namespace imlui {
using luaui::Node;

inline float dpf(float v) { return v * dpiScale(); }

/// Reads a size property in dp: > 0 px (scaled), < 0 "fill minus", 0 auto.
inline float sizeProp(const Node& n, const char* key) {
    if (key[0] == 'w' && n.boolean("fillWidth", false)) return -1.0f;
    float v = n.numf(key, 0.0f);
    return v > 0 ? dpf(v) : v;
}

/// "Label###id": the visible text may change every frame without changing the ImGui ID.
inline std::string label(std::string_view text, const char* id) {
    std::string s{text};
    s += "###";
    s += id;
    return s;
}

inline float alignFactor(std::string_view a) {
    if (a == "center") return 0.5f;
    if (a == "end" || a == "right" || a == "bottom") return 1.0f;
    return 0.0f;
}

// ---------------------------------------------------------- canvas painter
namespace painter {
inline ImDrawList* g_dl = nullptr;
inline ImVec2 g_origin{};

inline Color colorArg(lua::State* L, int idx, Color def) {
    if (lua::type(L, idx) == lua::TSTRING)
        if (auto c = resolveColor(lua::tostring(L, idx))) return *c;
    if (lua::istable(L, idx))
        if (auto c = Node::colorFromTable(L, idx)) return *c;
    return def;
}
inline ImU32 u32(Color c) { return ImGui::ColorConvertFloat4ToU32(c.vec4()); }
inline float num(lua::State* L, int idx, float def = 0.0f) {
    return lua::isnumber(L, idx) ? static_cast<float>(lua::tonumber(L, idx)) : def;
}
inline ImVec2 pt(lua::State* L, int ix) { return {g_origin.x + dpf(num(L, ix)), g_origin.y + dpf(num(L, ix + 1))}; }
inline void ready(lua::State* L) {
    if (!g_dl) lua::error(L, "painter used outside of Canvas.onPaint");
}

// All methods are called as p:method(...), so argument 1 is the painter.
int line(lua::State* L) {
    ready(L);
    g_dl->AddLine(pt(L, 2), pt(L, 4), u32(colorArg(L, 6, theme().scheme.onSurface)), dpf(num(L, 7, 1.0f)));
    return 0;
}
int rect(lua::State* L) {
    ready(L);
    ImVec2 a = pt(L, 2);
    ImVec2 b{a.x + dpf(num(L, 4)), a.y + dpf(num(L, 5))};
    ImU32 c = u32(colorArg(L, 6, theme().scheme.primary));
    float r = dpf(num(L, 7));
    if (lua::toboolean(L, 8)) g_dl->AddRect(a, b, c, r, dpf(num(L, 9, 1.0f)));
    else g_dl->AddRectFilled(a, b, c, r);
    return 0;
}
int circle(lua::State* L) {
    ready(L);
    ImVec2 c = pt(L, 2);
    float r = dpf(num(L, 4));
    ImU32 col = u32(colorArg(L, 5, theme().scheme.primary));
    if (lua::toboolean(L, 6)) g_dl->AddCircle(c, r, col, 0, dpf(num(L, 7, 1.0f)));
    else g_dl->AddCircleFilled(c, r, col);
    return 0;
}
int text(lua::State* L) {
    ready(L);
    ImVec2 p = pt(L, 2);
    std::string_view s = lua::tostring(L, 4);
    Color c = colorArg(L, 5, theme().scheme.onSurface);
    TextStyle st = TextStyle::BodyMedium;
    if (lua::type(L, 6) == lua::TSTRING)
        if (auto t = textStyleFromName(lua::tostring(L, 6))) st = *t;
    g_dl->AddText(font(st), fontPixels(st), p, u32(c), s.data(), s.data() + s.size());
    return 0;
}
/// p:icon(cx, cy, name, size = 24, color) — centered at (cx, cy)
int icon(lua::State* L) {
    ready(L);
    ImVec2 c = pt(L, 2);
    std::string_view name = lua::tostring(L, 4);
    float size = num(L, 5, 24.0f);
    Color col = colorArg(L, 6, theme().scheme.onSurfaceVariant);
    ImFont* f = iconFont();
    std::string t = iconText(name);
    if (!f || t.empty()) return 0;
    float px = dpf(size);
    ImVec2 sz = f->CalcTextSizeA(px, 3.4e38f, 0.0f, t.c_str());
    g_dl->AddText(f, px, {c.x - sz.x * 0.5f, c.y - sz.y * 0.5f}, u32(col), t.c_str());
    return 0;
}
int polyline(lua::State* L) {
    ready(L);
    std::vector<ImVec2> pts;
    if (lua::istable(L, 2)) {
        lua::Integer n = lua::rawlen(L, 2);
        for (lua::Integer i = 1; i + 1 <= n; i += 2) {
            lua::rawgeti(L, 2, i);
            lua::rawgeti(L, 2, i + 1);
            pts.push_back({g_origin.x + dpf(num(L, -2)), g_origin.y + dpf(num(L, -1))});
            lua::pop(L, 2);
        }
    }
    if (pts.size() >= 2)
        g_dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), u32(colorArg(L, 3, theme().scheme.primary)),
                          dpf(num(L, 4, 1.0f)), lua::toboolean(L, 5) ? ImDrawFlags_Closed : ImDrawFlags_None);
    return 0;
}
} // namespace painter

} // namespace imlui

export namespace imlui {

class Renderer;
using ElementFn = std::function<void(Renderer&, const luaui::Node&)>;

class Renderer {
public:
    Renderer() { registerBuiltins(); }

    /// Adds (or replaces) an element type. `visual == false` elements (e.g.
    /// Timer, Dialog) take no layout space.
    void registerElement(std::string name, ElementFn fn, bool visual = true) {
        elements_[std::move(name)] = Entry{std::move(fn), visual};
    }

    /// Title requested by the root ApplicationWindow (empty if none).
    [[nodiscard]] const std::string& windowTitle() const { return title_; }

    /// Renders the element at stack index `idx`. Returns false if it was
    /// hidden (visible = false) or unknown.
    bool render(lua::State* L, int idx) {
        lua::checkstack(L, 32);
        Node n{L, idx};
        std::string type = n.type();
        auto it = elements_.find(type);
        if (it == elements_.end()) {
            luaui::reportError("unknown element type '" + type + "'");
            return false;
        }
        if (!n.boolean("visible", true)) return false;
        ImGui::PushID(n.ptr());
        const bool disabled = n.has("enabled") && !n.boolean("enabled", true);
        if (disabled) ImGui::BeginDisabled();
        // Every visual element is exactly one layout item (a group), so
        // composite elements (label + field, ...) behave inside Rows.
        const bool visual = it->second.visual;
        if (visual) ImGui::BeginGroup();
        it->second.fn(*this, n);
        if (visual) ImGui::EndGroup();
        if (n.has("tooltip")) md::tooltip(n.string("tooltip"));
        if (disabled) ImGui::EndDisabled();
        ImGui::PopID();
        return true;
    }

    /// Calls f(stackIndex) for every child element. Children come from the
    /// element's array part, or from its `__children(self)` function
    /// (Repeater / ListView / If / Loader on the Lua side). Inline
    /// containers are spliced into the parent.
    template <class F>
    void forEachChild(const Node& n, F&& f) {
        lua::State* L = n.state();
        n.pushRaw("__children");
        if (lua::isfunction(L, -1)) {
            lua::pushvalue(L, n.index());
            if (!luaui::safeCall(L, 1, 1)) return;
            if (!lua::istable(L, -1)) { lua::pop(L); return; }
        } else {
            lua::pop(L);
            lua::pushvalue(L, n.index());
        }
        const int src = lua::gettop(L);
        const lua::Integer len = lua::rawlen(L, src);
        for (lua::Integer i = 1; i <= len; ++i) {
            lua::rawgeti(L, src, i);
            if (lua::istable(L, -1)) {
                lua::getfield(L, -1, "__type");
                bool element = lua::isstring(L, -1);
                lua::pop(L);
                lua::getfield(L, -1, "__inline");
                bool inl = lua::toboolean(L, -1);
                lua::pop(L);
                if (element && inl) {
                    Node c{L, lua::gettop(L)};
                    if (c.boolean("visible", true)) forEachChild(c, f);
                } else if (element) {
                    f(lua::gettop(L));
                }
            }
            lua::settop(L, src);
        }
        lua::settop(L, src - 1);
    }

    void renderChildren(const Node& n) {
        forEachChild(n, [&](int idx) { render(n.state(), idx); });
    }

    [[nodiscard]] bool isVisual(lua::State* L, int idx) {
        Node c{L, idx};
        auto it = elements_.find(c.type());
        return it == elements_.end() || it->second.visual;
    }

    /// Renders children top-to-bottom. `spacing` (dp) overrides the gap
    /// between visible items; `align` positions them horizontally.
    void renderStack(const Node& n) {
        const auto spacing = n.number("spacing");
        const float align = alignFactor(n.string("align", "start"));
        lua::State* L = n.state();
        ImGuiStorage* st = ImGui::GetStateStorage();
        const float avail = ImGui::GetContentRegionAvail().x;
        const float x0 = ImGui::GetCursorScreenPos().x;
        bool first = true;
        forEachChild(n, [&](int idx) {
            if (!isVisual(L, idx)) { render(L, idx); return; }
            Node c{L, idx};
            if (!c.boolean("visible", true)) return;
            if (!first && spacing)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + static_cast<float>(*spacing) * dpiScale() -
                                     ImGui::GetStyle().ItemSpacing.y);
            const ImGuiID key = ImGui::GetID(c.ptr());
            if (align > 0.0f) {
                float w = st->GetFloat(key, 0.0f);
                ImGui::SetCursorScreenPos({x0 + std::max(0.0f, (avail - w) * align), ImGui::GetCursorScreenPos().y});
            }
            render(L, idx);
            if (align > 0.0f) st->SetFloat(key, ImGui::GetItemRectSize().x);
            first = false;
        });
    }

    /// Pops up `node.menu` (a Menu element) anchored to the last item when
    /// `clicked`; renders it while open.
    void attachMenu(const Node& n, bool clicked) {
        lua::State* L = n.state();
        if (n.pushRaw("menu") != lua::TTABLE) { lua::pop(L); return; }
        const int menuIdx = lua::gettop(L);
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (clicked) ImGui::OpenPopup("##menu");
        Node menu{L, menuIdx};
        if (md::beginAnchoredMenu("##menu", a, b, dpf(112))) {
            renderChildren(menu);
            md::endAnchoredMenu();
        }
        lua::settop(L, menuIdx - 1);
    }

private:
    struct Entry {
        ElementFn fn;
        bool visual = true;
    };
    std::map<std::string, Entry, std::less<>> elements_;
    std::string title_;

    void registerBuiltins();
};

// ===================================================================== builtins

void Renderer::registerBuiltins() {
    using R = Renderer;

    // ------------------------------------------------------------ windows
    registerElement("ApplicationWindow", [](R& r, const Node& n) {
        r.title_ = n.string("title", "imlui");
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
        const bool menu = n.has("menuBar");
        if (menu) flags |= ImGuiWindowFlags_MenuBar;
        if (!n.boolean("scrollable", true)) flags |= ImGuiWindowFlags_NoScrollbar;
        float pad = n.numf("padding", 24.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dpf(pad), dpf(pad)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        auto bg = n.color("color");
        if (bg) ImGui::PushStyleColor(ImGuiCol_WindowBg, bg->vec4());
        ImGui::Begin("##ApplicationWindow", nullptr, flags);
        if (bg) ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (menu) {
            n.push("menuBar");
            if (lua::istable(n.state(), -1)) r.render(n.state(), -1);
            lua::pop(n.state());
        }
        r.renderStack(n);
        ImGui::End();
    }, false);

    registerElement("Window", [](R& r, const Node& n) {
        bool open = n.boolean("open", true);
        if (!open) return;
        std::string title = label(n.string("title", "Window"), "win");
        float x = n.numf("x", 60), y = n.numf("y", 60);
        float w = n.numf("width", 0), h = n.numf("height", 0);
        ImGui::SetNextWindowPos({dpf(x), dpf(y)}, ImGuiCond_FirstUseEver);
        if (w > 0 || h > 0) ImGui::SetNextWindowSize({dpf(w), dpf(h)}, ImGuiCond_FirstUseEver);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
        if (n.boolean("autoResize", w <= 0 && h <= 0)) flags |= ImGuiWindowFlags_AlwaysAutoResize;
        if (!n.boolean("titleBar", true)) flags |= ImGuiWindowFlags_NoTitleBar;
        if (!n.boolean("resizable", true)) flags |= ImGuiWindowFlags_NoResize;
        if (!n.boolean("movable", true)) flags |= ImGuiWindowFlags_NoMove;
        if (n.has("menuBar")) flags |= ImGuiWindowFlags_MenuBar;
        const bool closable = n.has("open");
        bool keep = true;
        const Scheme& s = theme().scheme;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, s.surfaceContainerLow.vec4());
        ImGui::PushStyleColor(ImGuiCol_TitleBg, s.surfaceContainer.vec4());
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, s.surfaceContainerHigh.vec4());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, dpf(1));
        ImGui::PushStyleColor(ImGuiCol_Border, s.outlineVariant.vec4());
        const bool shown = ImGui::Begin(title.c_str(), closable ? &keep : nullptr, flags);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        if (shown) {
            md::pushSurface(s.surfaceContainerLow);
            if (n.has("menuBar")) {
                n.push("menuBar");
                if (lua::istable(n.state(), -1)) r.render(n.state(), -1);
                lua::pop(n.state());
            }
            r.renderStack(n);
            md::popSurface();
        }
        ImGui::End();
        if (!keep) {
            n.set("open", false);
            n.emit("onClosed");
        }
    }, false);

    registerElement("Dialog", [](R& r, const Node& n) {
        bool open = n.boolean("open", false);
        const bool was = open;
        std::string title = label(n.string("title", ""), "dlg");
        if (md::beginDialog(title.c_str(), &open, sizeProp(n, "width"), n.string("icon", ""))) {
            r.renderStack(n);
            md::endDialog();
        }
        if (open != was) {
            n.set("open", open);
            if (!open) n.emit("onClosed");
        }
    }, false);

    // ------------------------------------------------------------ layouts
    auto column = [](R& r, const Node& n) {
        const float pad = n.numf("padding", 0.0f);
        if (pad > 0) { ImGui::Dummy({0, dpf(pad) - ImGui::GetStyle().ItemSpacing.y}); ImGui::Indent(dpf(pad)); }
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
        if (pad > 0) { ImGui::Unindent(dpf(pad)); ImGui::Dummy({0, dpf(pad) - ImGui::GetStyle().ItemSpacing.y}); }
    };
    registerElement("Column", column);
    registerElement("Item", column);

    // Row: horizontal layout.
    //   spacing  gap between children (dp)
    //   align    vertical: "center" (default) | "top" | "bottom"
    //   justify  horizontal: "start" | "center" | "end" | "spaceBetween"
    //   Spacer { fill = true } children share the remaining width.
    // Sizes are measured on the previous frame (immediate mode), so a
    // layout settles within one frame.
    registerElement("Row", [](R& r, const Node& n) {
        lua::State* L = n.state();
        const auto spacing = n.number("spacing");
        const float sp = spacing ? dpf(static_cast<float>(*spacing)) : ImGui::GetStyle().ItemSpacing.x;
        const float valign = alignFactor(n.string("align", "center")) + (n.string("align", "center") == "center" ? 0.0f : 0.0f);
        const std::string valignName = n.string("align", "center");
        const std::string justify = n.string("justify", "start");
        ImGuiStorage* st = ImGui::GetStateStorage();
        const ImGuiID kH = ImGui::GetID("##rowH"), kFixed = ImGui::GetID("##rowFixed"), kFills = ImGui::GetID("##rowFills"),
                      kCount = ImGui::GetID("##rowCount");
        const float rowH = st->GetFloat(kH, 0.0f), lastFixed = st->GetFloat(kFixed, 0.0f);
        const int lastFills = st->GetInt(kFills, 0), lastCount = st->GetInt(kCount, 0);
        float availW = sizeProp(n, "width");
        if (availW <= 0) availW = ImGui::GetContentRegionAvail().x + availW;
        const float free = std::max(0.0f, availW - lastFixed);
        const float fillW = lastFills > 0 ? free / static_cast<float>(lastFills) : 0.0f;
        float offset = 0.0f, extraGap = 0.0f;
        if (lastFills == 0) {
            if (justify == "center") offset = free * 0.5f;
            else if (justify == "end") offset = free;
            else if (justify == "spaceBetween" && lastCount > 1) extraGap = free / static_cast<float>(lastCount - 1);
        }
        (void)valign;

        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float top = start.y;
        float maxH = 0.0f, fixed = 0.0f;
        int fills = 0, count = 0;
        float x = start.x + offset;
        r.forEachChild(n, [&](int idx) {
            if (!r.isVisual(L, idx)) { r.render(L, idx); return; }
            Node c{L, idx};
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
                if (valignName == "center") y = top + std::max(0.0f, (rowH - ch) * 0.5f);
                else if (valignName == "bottom") y = top + std::max(0.0f, rowH - ch);
            }
            ImGui::SetCursorScreenPos({x, y});
            r.render(L, idx);
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
        st->SetFloat(kH, maxH);
        st->SetFloat(kFixed, fixed);
        st->SetInt(kFills, fills);
        st->SetInt(kCount, count);
    });

    registerElement("Grid", [](R& r, const Node& n) {
        int cols = std::max(1, static_cast<int>(n.number("columns", 2)));
        ImGuiTableFlags f = ImGuiTableFlags_SizingStretchSame;
        if (n.boolean("border", false)) f |= ImGuiTableFlags_Borders;
        float sp = n.numf("spacing", 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {dpf(sp) * 0.5f, dpf(sp) * 0.5f});
        if (ImGui::BeginTable("##grid", cols, f, {sizeProp(n, "width"), 0})) {
            r.forEachChild(n, [&](int idx) {
                if (!r.isVisual(n.state(), idx)) { r.render(n.state(), idx); return; }
                ImGui::TableNextColumn();
                r.render(n.state(), idx);
            });
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    });

    auto scroll = [](R& r, const Node& n) {
        ImGuiChildFlags cf = n.boolean("border", false) ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
        const float pad = n.numf("padding", 0.0f);
        const auto bg = n.color("color");
        const float rounding = n.numf("rounding", theme().shape.medium);
        if (pad > 0) {
            cf |= ImGuiChildFlags_AlwaysUseWindowPadding;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dpf(pad), dpf(pad)});
        }
        if (bg) ImGui::PushStyleColor(ImGuiCol_ChildBg, bg->vec4());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, dpf(rounding));
        ImGui::BeginChild("##scroll", {sizeProp(n, "width"), sizeProp(n, "height")}, cf);
        ImGui::PopStyleVar();
        if (bg) ImGui::PopStyleColor();
        if (pad > 0) ImGui::PopStyleVar();
        if (bg) md::pushSurface(*bg);
        r.renderStack(n);
        if (bg) md::popSurface();
        ImGui::EndChild();
    };
    registerElement("ScrollView", scroll);
    registerElement("ListView", scroll); // children come from model/delegate (Lua side)

    registerElement("Card", [](R& r, const Node& n) {
        auto rounding = n.number("rounding");
        md::beginCard("##card", md::cardVariant(n.string("variant", "elevated")), sizeProp(n, "width"),
                      n.numf("padding", 16.0f), sizeProp(n, "height"), n.color("color"),
                      rounding ? static_cast<float>(*rounding) : -1.0f);
        r.renderStack(n);
        md::endCard();
    });

    registerElement("TabBar", [](R& r, const Node& n) {
        lua::State* L = n.state();
        // Collect tab headers.
        std::vector<md::TabInfo> tabs;
        r.forEachChild(n, [&](int idx) {
            Node t{L, idx};
            if (t.boolean("visible", true)) tabs.push_back({t.string("title", "Tab"), t.string("icon", "")});
        });
        int cur = static_cast<int>(n.number("currentIndex", 1)) - 1;
        cur = std::clamp(cur, 0, std::max(0, static_cast<int>(tabs.size()) - 1));
        if (md::tabs("##tabs", tabs, &cur, n.string("variant", "primary") == "secondary", sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width"))) {
            n.set("currentIndex", static_cast<double>(cur + 1));
            n.emit("onCurrentChanged", {static_cast<double>(cur + 1)});
        }
        ImGui::Dummy({0, dpf(8)});
        int i = 0;
        r.forEachChild(n, [&](int idx) {
            Node t{L, idx};
            if (!t.boolean("visible", true)) return;
            if (i++ == cur) {
                ImGui::PushID(t.ptr());
                r.renderStack(t);
                ImGui::PopID();
            }
        });
    });
    // Tabs are rendered by TabBar; a stray Tab renders its content inline.
    registerElement("Tab", [](R& r, const Node& n) { r.renderStack(n); });

    auto expander = [](R& r, const Node& n) {
        // `expanded` may be plain (stored on the element) or bound.
        bool open = n.boolean("expanded", true);
        if (md::expander("##exp", n.string("title", n.text()), n.string("supportingText", ""), &open, sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width"))) {
            n.set("expanded", open);
            n.emit("onToggled", {open});
        }
        if (open) {
            ImGui::Indent(dpf(16));
            r.renderStack(n);
            ImGui::Unindent(dpf(16));
        }
    };
    registerElement("Section", expander);
    registerElement("Expander", expander);

    registerElement("TreeNode", [](R& r, const Node& n) {
        std::string t = label(n.text(), "tree");
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (n.boolean("expanded", false)) f |= ImGuiTreeNodeFlags_DefaultOpen;
        md::FontScope fs{TextStyle::BodyLarge};
        if (ImGui::TreeNodeEx(t.c_str(), f)) {
            r.renderChildren(n);
            ImGui::TreePop();
        }
    });

    // ------------------------------------------------------------ app bars & navigation
    // TopAppBar { title, navigationIcon, onNavigation, variant = "small"|"center"|"medium"|"large", <actions...> }
    registerElement("TopAppBar", [](R& r, const Node& n) {
        lua::State* L = n.state();
        const Scheme& s = theme().scheme;
        const std::string variant = n.string("variant", "small");
        const float barH = dpf(64);
        const float h = variant == "medium" ? dpf(112) : variant == "large" ? dpf(152) : barH;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        float w = sizeProp(n, "width");
        if (w <= 0) w = ImGui::GetContentRegionAvail().x + w;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Color bg = n.color("color").value_or(md::surfaceColor());
        dl->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::ColorConvertFloat4ToU32(bg.vec4()));
        ImGuiStorage* st = ImGui::GetStateStorage();

        float titleX = p.x + dpf(16);
        const std::string navIcon = n.string("navigationIcon", "");
        if (!navIcon.empty()) {
            ImGui::SetCursorScreenPos({p.x + dpf(4), p.y + (barH - dpf(48)) * 0.5f});
            if (md::iconButton("##nav", navIcon)) n.emit("onNavigation");
            titleX = p.x + dpf(56);
        }
        // actions, right aligned, each vertically centered in the 64dp bar
        const ImGuiID kW = ImGui::GetID("##actionsW");
        const float lastW = st->GetFloat(kW, 0.0f);
        float x = p.x + w - dpf(4) - lastW, total = 0;
        int count = 0;
        r.forEachChild(n, [&](int idx) {
            if (!r.isVisual(L, idx)) { r.render(L, idx); return; }
            Node c{L, idx};
            if (!c.boolean("visible", true)) return;
            const ImGuiID ck = ImGui::GetID(c.ptr());
            const float ch = st->GetFloat(ck, dpf(48));
            ImGui::SetCursorScreenPos({x, p.y + (barH - ch) * 0.5f});
            r.render(L, idx);
            ImVec2 sz = ImGui::GetItemRectSize();
            st->SetFloat(ck, sz.y);
            x += sz.x;
            total += sz.x;
            ++count;
        });
        st->SetFloat(kW, total);

        const std::string title = n.string("title", "");
        if (variant == "medium" || variant == "large") {
            TextStyle ts = variant == "large" ? TextStyle::HeadlineMedium : TextStyle::HeadlineSmall;
            float th = font(ts)->CalcTextSizeA(fontPixels(ts), 3.4e38f, 0.0f, title.c_str()).y;
            dl->AddText(font(ts), fontPixels(ts), {p.x + dpf(16), p.y + h - dpf(variant == "large" ? 28 : 24) - th},
                        ImGui::ColorConvertFloat4ToU32(s.onSurface.vec4()), title.c_str());
        } else {
            TextStyle ts = TextStyle::TitleLarge;
            ImVec2 tsz = font(ts)->CalcTextSizeA(fontPixels(ts), 3.4e38f, 0.0f, title.c_str());
            float tx = variant == "center" ? p.x + (w - tsz.x) * 0.5f : titleX;
            dl->AddText(font(ts), fontPixels(ts), {tx, p.y + (barH - tsz.y) * 0.5f}, ImGui::ColorConvertFloat4ToU32(s.onSurface.vec4()),
                        title.c_str());
            const std::string sub = n.string("subtitle", "");
            (void)sub;
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy({w, h});
    });

    registerElement("NavigationItem", [](R&, const Node& n) {
        if (md::navigationItem("##nav", n.string("icon", ""), n.text(), n.boolean("selected", false), n.string("badge", ""),
                               sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width")))
            n.emit("onClicked");
    });

    // ------------------------------------------------------------ menus
    registerElement("MenuBar", [](R& r, const Node& n) {
        if (ImGui::BeginMenuBar()) {
            r.renderChildren(n);
            ImGui::EndMenuBar();
        }
    });
    registerElement("Menu", [](R& r, const Node& n) {
        std::string t = label(n.string("title", ""), "menu");
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
    });
    registerElement("MenuItem", [](R&, const Node& n) {
        const bool checkable = n.has("checked");
        bool checked = n.boolean("checked", false);
        std::string icon = n.string("icon", "");
        if (checkable) icon = checked ? "check" : "";
        if (md::menuItem("##mi", n.text(), icon, n.string("shortcut", ""), false, !checkable, checkable || n.has("icon"))) {
            if (checkable) n.set("checked", !checked);
            n.emit("onTriggered");
        }
    });

    // ------------------------------------------------------------ text & decoration
    auto textFn = [](R&, const Node& n) {
        auto style = textStyleFromName(n.string("style", "bodyLarge")).value_or(TextStyle::BodyLarge);
        md::text(n.text(), style, n.color("color"), n.boolean("wrap", false), alignFactor(n.string("align", "start")));
    };
    registerElement("Text", textFn);
    registerElement("Label", textFn);

    registerElement("Icon", [](R&, const Node& n) {
        md::icon(n.string("name", n.string("icon", n.text())), n.numf("size", 24.0f), n.color("color"));
    });

    registerElement("Link", [](R&, const Node& n) {
        std::string t = n.text();
        std::string url = n.string("url", t);
        md::FontScope fs{TextStyle::BodyLarge};
        ImGui::TextLinkOpenURL(t.c_str(), url.c_str());
    });

    registerElement("Separator", [](R&, const Node& n) {
        std::string t = n.text();
        if (t.empty()) {
            md::divider(n.numf("inset", 0.0f), n.numf("insetEnd", 0.0f));
        } else {
            md::FontScope fs{TextStyle::TitleSmall};
            ImGui::PushStyleColor(ImGuiCol_Text, theme().scheme.onSurfaceVariant.vec4());
            ImGui::SeparatorText(t.c_str());
            ImGui::PopStyleColor();
        }
    });
    registerElement("Divider", [](R&, const Node& n) { md::divider(n.numf("inset", 0.0f), n.numf("insetEnd", 0.0f)); });
    registerElement("Spacer", [](R&, const Node& n) {
        ImGui::Dummy({std::max(1.0f, sizeProp(n, "width")), std::max(1.0f, sizeProp(n, "height"))});
    });

    // ------------------------------------------------------------ buttons & selection
    registerElement("Button", [](R& r, const Node& n) {
        std::string t = label(n.text(), "btn");
        bool clicked = md::button(t.c_str(), md::buttonVariant(n.string("variant", "filled")),
                                  {sizeProp(n, "width"), sizeProp(n, "height")}, n.string("icon", ""));
        if (clicked) n.emit("onClicked");
        r.attachMenu(n, clicked);
    });

    registerElement("IconButton", [](R& r, const Node& n) {
        const bool checkable = n.has("checked");
        const bool checked = n.boolean("checked", false);
        std::string icon = n.string("icon", n.text());
        if (checkable && checked && n.has("checkedIcon")) icon = n.string("checkedIcon", icon);
        bool clicked = md::iconButton("##ib", icon, md::iconButtonVariant(n.string("variant", "standard")), checkable, checked,
                                      n.color("color"));
        if (clicked) {
            if (checkable) n.set("checked", !checked);
            n.emit("onClicked");
        }
        r.attachMenu(n, clicked);
    });

    registerElement("Checkbox", [](R&, const Node& n) {
        std::string t = label(n.text(), "cb");
        bool v = n.boolean("checked", false);
        if (md::checkbox(t.c_str(), &v)) {
            n.set("checked", v);
            n.emit("onToggled", {v});
        }
    });

    registerElement("Switch", [](R&, const Node& n) {
        std::string t = label(n.text(), "sw");
        bool v = n.boolean("checked", false);
        if (md::switchToggle(t.c_str(), &v, n.boolean("icons", true))) {
            n.set("checked", v);
            n.emit("onToggled", {v});
        }
    });

    registerElement("RadioButton", [](R&, const Node& n) {
        std::string t = label(n.text(), "rb");
        if (md::radio(t.c_str(), n.boolean("checked", false))) {
            n.set("checked", true);
            n.emit("onClicked");
        }
    });

    registerElement("RadioGroup", [](R&, const Node& n) {
        auto items = n.strings("model");
        int cur = static_cast<int>(n.number("currentIndex", 1));
        const bool horizontal = n.boolean("horizontal", false);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 0});
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (horizontal && i > 0) ImGui::SameLine(0, dpf(16));
            ImGui::PushID(i);
            std::string t = label(items[i], "r");
            if (md::radio(t.c_str(), cur == i + 1) && cur != i + 1) {
                n.set("currentIndex", static_cast<double>(i + 1));
                n.emit("onActivated", {static_cast<double>(i + 1), items[i]});
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
    });

    registerElement("Chip", [](R&, const Node& n) {
        std::string t = label(n.text(), "chip");
        bool v = n.boolean("checked", false);
        auto variant = md::chipVariant(n.string("variant", "filter"));
        if (md::chip(t.c_str(), v, variant, n.string("icon", ""))) {
            if (variant == md::ChipVariant::Filter && n.boolean("checkable", true)) n.set("checked", !v);
            n.emit("onClicked", {!v});
        }
    });

    registerElement("SegmentedButton", [](R&, const Node& n) {
        auto items = n.strings("model");
        auto icons = n.strings("icons");
        int cur = static_cast<int>(n.number("currentIndex", 1)) - 1;
        if (md::segmented("##seg", items, &cur, sizeProp(n, "width"), icons)) {
            n.set("currentIndex", static_cast<double>(cur + 1));
            n.emit("onActivated", {static_cast<double>(cur + 1), items[static_cast<std::size_t>(cur)]});
        }
    });

    registerElement("ListItem", [](R&, const Node& n) {
        std::string headline = n.text(), supporting = n.string("supportingText", ""), lead = n.string("icon", ""),
                    tt = n.string("trailingText", ""), ti = n.string("trailingIcon", "");
        md::ListItemOptions o;
        o.headline = headline;
        o.supporting = supporting;
        o.leadingIcon = lead;
        o.trailingText = tt;
        o.trailingIcon = ti;
        o.selected = n.boolean("selected", false);
        o.width = sizeProp(n, "width") == 0 ? -1.0f : sizeProp(n, "width");
        if (md::listItem("##li", o)) n.emit("onClicked");
    });

    // ------------------------------------------------------------ value inputs
    registerElement("Slider", [](R&, const Node& n) {
        float v = n.numf("value", 0.0f);
        float from = n.numf("from", 0.0f), to = n.numf("to", 1.0f), step = n.numf("stepSize", 0.0f);
        std::string fmt = n.string("format", step >= 1.0f ? "%.0f" : "%.2f");
        if (md::slider("##slider", &v, from, to, step, sizeProp(n, "width"), fmt.c_str())) {
            n.set("value", static_cast<double>(v));
            n.emit("onMoved", {static_cast<double>(v)});
        }
        if (ImGui::IsItemDeactivated()) n.emit("onReleased", {static_cast<double>(v)});
    });

    registerElement("SpinBox", [](R&, const Node& n) {
        double v = n.number("value", 0);
        double from = n.number("from", -1e9), to = n.number("to", 1e9), step = n.number("stepSize", 1);
        if (md::spinBox("##spin", &v, from, to, step, static_cast<int>(n.number("decimals", 0)), n.string("label", ""),
                        sizeProp(n, "width"), n.string("variant", "filled") == "outlined")) {
            n.set("value", v);
            n.emit("onValueModified", {v});
        }
    });

    auto field = [](bool multiline) {
        return [multiline](R&, const Node& n) {
            std::string text = n.string("text", "");
            std::string lbl = n.string("label", ""), ph = n.string("placeholderText", ""), li = n.string("leadingIcon", ""),
                        ti = n.string("trailingIcon", ""), sup = n.string("supportingText", "");
            md::TextFieldOptions o;
            o.label = lbl;
            o.placeholder = ph;
            o.leadingIcon = li;
            o.trailingIcon = ti;
            o.supporting = sup;
            o.outlined = n.string("variant", "filled") == "outlined";
            o.password = n.boolean("password", false) || n.string("echoMode", "") == "password";
            o.multiline = multiline;
            o.readOnly = n.boolean("readOnly", false);
            o.error = n.boolean("error", false);
            o.width = sizeProp(n, "width");
            o.height = sizeProp(n, "height");
            auto res = md::textField("##tf", text, o);
            if (res.changed) {
                n.set("text", text);
                n.emit("onTextChanged", {text});
            }
            if (res.accepted) n.emit("onAccepted", {text});
            if (res.trailingClicked) n.emit("onTrailingClicked");
        };
    };
    registerElement("TextField", field(false));
    registerElement("TextArea", field(true));

    registerElement("ComboBox", [](R&, const Node& n) {
        auto items = n.strings("model");
        auto icons = n.strings("icons");
        int cur = static_cast<int>(n.number("currentIndex", 1)) - 1;
        std::string lbl = n.string("label", ""), li = n.string("leadingIcon", ""), sup = n.string("supportingText", "");
        md::DropdownOptions o;
        o.label = lbl;
        o.leadingIcon = li;
        o.supporting = sup;
        o.outlined = n.string("variant", "filled") == "outlined";
        o.error = n.boolean("error", false);
        o.width = sizeProp(n, "width");
        if (md::dropdown("##dd", items, &cur, o, icons)) {
            n.set("currentIndex", static_cast<double>(cur + 1));
            n.emit("onActivated", {static_cast<double>(cur + 1), items[static_cast<std::size_t>(cur)]});
        }
    });

    registerElement("ColorPicker", [](R&, const Node& n) {
        auto c = n.color("color").value_or(Color{1, 1, 1, 1});
        if (md::colorField("##cf", &c, n.string("label", ""), n.boolean("alpha", false), sizeProp(n, "width"))) {
            std::string hex = toHex(c);
            n.set("color", hex);
            n.emit("onColorChanged", {hex});
        }
    });

    // ------------------------------------------------------------ progress & data
    registerElement("ProgressBar", [](R&, const Node& n) {
        md::progress(n.boolean("indeterminate", false) ? -1.0f : n.numf("value", 0.0f), sizeProp(n, "width"));
    });
    registerElement("CircularProgress", [](R&, const Node& n) {
        md::circularProgress(n.boolean("indeterminate", false) ? -1.0f : n.numf("value", 0.0f), sizeProp(n, "size"),
                             n.numf("thickness", 4.0f));
    });
    registerElement("BusyIndicator", [](R&, const Node& n) {
        if (n.boolean("running", true)) md::circularProgress(-1.0f, sizeProp(n, "size"), n.numf("thickness", 4.0f));
        else ImGui::Dummy({sizeProp(n, "size") > 0 ? sizeProp(n, "size") : dpf(48), sizeProp(n, "size") > 0 ? sizeProp(n, "size") : dpf(48)});
    });

    registerElement("Plot", [](R&, const Node& n) {
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
    });

    registerElement("Table", [](R&, const Node& n) {
        lua::State* L = n.state();
        auto headers = n.strings("columns");
        if (headers.empty()) return;
        const Scheme& s = theme().scheme;
        const float rowH = dpf(n.numf("rowHeight", 52.0f)), headH = dpf(56);
        const bool outlined = n.boolean("outlined", true);
        if (outlined) md::beginCard("##tablecard", md::CardVariant::Outlined, sizeProp(n, "width"), 0.0f);
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, s.outlineVariant.vec4());
        ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, s.outlineVariant.vec4());
        ImGui::PushStyleColor(ImGuiCol_Header, s.secondaryContainer.vec4());
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {dpf(16), 0});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {dpf(8), 0});
        ImGuiTableFlags f = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##table", static_cast<int>(headers.size()), f)) {
            for (auto& h : headers) ImGui::TableSetupColumn(h.c_str());
            ImGui::TableNextRow(0, headH);
            for (auto& h : headers) {
                ImGui::TableNextColumn();
                md::tableHeaderCell(h, headH);
            }
            if (n.push("rows") == lua::TTABLE) {
                const int rows = lua::gettop(L);
                const lua::Integer nr = lua::rawlen(L, rows);
                const int selected = static_cast<int>(n.number("currentRow", 0));
                for (lua::Integer i = 1; i <= nr; ++i) {
                    lua::rawgeti(L, rows, i);
                    ImGui::TableNextRow(0, rowH);
                    for (int c = 0; c < static_cast<int>(headers.size()); ++c) {
                        ImGui::TableNextColumn();
                        lua::rawgeti(L, -1, c + 1);
                        std::string cell = lua::isnil(L, -1) ? std::string{} : lua::tostring_any(L, -1);
                        lua::pop(L);
                        if (c == 0) {
                            ImGui::PushID(static_cast<int>(i));
                            ImVec2 p = ImGui::GetCursorScreenPos();
                            if (ImGui::Selectable("##row", selected == i,
                                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, {0, rowH})) {
                                n.set("currentRow", static_cast<double>(i));
                                n.emit("onRowClicked", {static_cast<double>(i)});
                            }
                            ImGui::SetCursorScreenPos(p);
                            ImGui::PopID();
                        }
                        md::tableCell(cell, rowH, selected == i ? std::optional<Color>{s.onSecondaryContainer} : std::nullopt);
                    }
                    lua::pop(L);
                }
            }
            lua::pop(L);
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        if (outlined) md::endCard();
    });

    // ------------------------------------------------------------ custom drawing
    registerElement("Canvas", [](R&, const Node& n) {
        lua::State* L = n.state();
        float w = sizeProp(n, "width"), h = sizeProp(n, "height");
        if (w <= 0) w = std::max(1.0f, ImGui::GetContentRegionAvail().x + (w < 0 ? w + 1.0f : 0.0f));
        if (h <= 0) h = dpf(200);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##canvas", {w, h});
        bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        ImVec2 m = ImGui::GetMousePos();
        const float s = dpiScale();
        if (clicked) n.emit("onClicked", {(m.x - p.x) / s, (m.y - p.y) / s});

        n.pushRaw("onPaint");
        if (!lua::isfunction(L, -1)) { lua::pop(L); return; }
        lua::pushvalue(L, n.index());
        lua::createtable(L, 0, 8);
        lua::pushnumber(L, w / s);           lua::setfield(L, -2, "width");
        lua::pushnumber(L, h / s);           lua::setfield(L, -2, "height");
        lua::pushnumber(L, (m.x - p.x) / s); lua::setfield(L, -2, "mouseX");
        lua::pushnumber(L, (m.y - p.y) / s); lua::setfield(L, -2, "mouseY");
        lua::pushboolean(L, hovered);        lua::setfield(L, -2, "hovered");
        lua::pushboolean(L, active);         lua::setfield(L, -2, "pressed");
        lua::pushnumber(L, ImGui::GetTime()); lua::setfield(L, -2, "time");
        lua::getfield(L, lua::REGISTRYINDEX, "imlui.painter");
        lua::setmetatable(L, -2);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p, {p.x + w, p.y + h}, true);
        painter::g_dl = dl;
        painter::g_origin = p;
        luaui::safeCall(L, 2, 0);
        painter::g_dl = nullptr;
        dl->PopClipRect();
    });

    // Inline containers are normally spliced into their parent by
    // forEachChild; these handlers only run when one is used as the root.
    for (const char* name : {"Repeater", "If", "Loader"})
        registerElement(name, [](R& r, const Node& n) { r.renderChildren(n); });

    // ------------------------------------------------------------ overlays & non-visual
    // Snackbar { text, actionText, onAction, shownAt, duration (ms), closable }
    // Normally driven by ui.snackbar(); rendered by the engine as an overlay.
    registerElement("Snackbar", [](R&, const Node& n) {
        lua::State* L = n.state();
        const double shownAt = n.number("shownAt", -1.0);
        if (shownAt < 0) return;
        const double now = ImGui::GetTime();
        const double dur = n.number("duration", 4000) / 1000.0;
        const double age = now - shownAt;
        float t = static_cast<float>(std::min(1.0, age / 0.18));
        if (age > dur) t = static_cast<float>(std::max(0.0, 1.0 - (age - dur) / 0.15));
        if (age > dur + 0.15) {
            lua::pushnumber(L, -1);
            lua::setfield(L, n.index(), "shownAt");
            return;
        }
        bool closed = false;
        if (md::snackbar(n.string("text", ""), n.string("actionText", ""), t, n.boolean("closable", false), &closed)) {
            n.emit("onAction");
            closed = true;
        }
        if (closed) {
            // fade out now
            lua::pushnumber(L, now - dur);
            lua::setfield(L, n.index(), "shownAt");
        }
    }, false);

    registerElement("Timer", [](R&, const Node& n) {
        lua::State* L = n.state();
        if (!n.boolean("running", true)) {
            lua::pushnil(L);
            lua::setfield(L, n.index(), "__last");
            return;
        }
        const double now = ImGui::GetTime();
        lua::getfield(L, n.index(), "__last");
        const bool started = lua::isnumber(L, -1);
        double last = started ? lua::tonumber(L, -1) : now;
        lua::pop(L);
        if (!started) {
            lua::pushnumber(L, now);
            lua::setfield(L, n.index(), "__last");
            if (n.boolean("triggeredOnStart", false)) n.emit("onTriggered");
            return;
        }
        if (now - last >= n.number("interval", 1000) / 1000.0) {
            lua::pushnumber(L, now);
            lua::setfield(L, n.index(), "__last");
            if (!n.boolean("repeat", true)) n.set("running", false);
            n.emit("onTriggered");
        }
    }, false);

    registerElement("ImGuiDemo", [](R&, const Node& n) {
        bool open = n.boolean("open", true);
        if (!open) return;
        ImGui::ShowDemoWindow(&open);
        if (!open) n.set("open", false);
    }, false);
}

/// Registers the painter metatable used by Canvas.onPaint (call once per state).
void registerPainter(lua::State* L) {
    lua::createtable(L, 0, 2);          // metatable
    lua::createtable(L, 0, 6);          // methods
    lua::pushcfunction(L, &painter::line);     lua::setfield(L, -2, "line");
    lua::pushcfunction(L, &painter::rect);     lua::setfield(L, -2, "rect");
    lua::pushcfunction(L, &painter::circle);   lua::setfield(L, -2, "circle");
    lua::pushcfunction(L, &painter::text);     lua::setfield(L, -2, "text");
    lua::pushcfunction(L, &painter::icon);     lua::setfield(L, -2, "icon");
    lua::pushcfunction(L, &painter::polyline); lua::setfield(L, -2, "polyline");
    lua::setfield(L, -2, "__index");
    lua::setfield(L, lua::REGISTRYINDEX, "imlui.painter");
}

} // namespace imlui
