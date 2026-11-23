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

// ---------------------------------------------------------- canvas painter
namespace painter {
inline ImDrawList* g_dl = nullptr;
inline ImVec2 g_origin{};

inline ImU32 colorArg(lua::State* L, int idx, Color def) {
    if (lua::type(L, idx) == lua::TSTRING)
        if (auto c = resolveColor(lua::tostring(L, idx))) return ImGui::GetColorU32(c->vec4());
    if (lua::istable(L, idx))
        if (auto c = Node::colorFromTable(L, idx)) return ImGui::GetColorU32(c->vec4());
    return ImGui::GetColorU32(def.vec4());
}
inline float num(lua::State* L, int idx, float def = 0.0f) {
    return lua::isnumber(L, idx) ? static_cast<float>(lua::tonumber(L, idx)) : def;
}
inline ImVec2 pt(lua::State* L, int ix) { return {g_origin.x + dpf(num(L, ix)), g_origin.y + dpf(num(L, ix + 1))}; }
inline bool ready(lua::State* L) {
    if (!g_dl) lua::error(L, "painter used outside of Canvas.onPaint");
    return true;
}

// All methods are called as p:method(...), so argument 1 is the painter.
int line(lua::State* L) {
    ready(L);
    g_dl->AddLine(pt(L, 2), pt(L, 4), colorArg(L, 6, theme().scheme.onSurface), dpf(num(L, 7, 1.0f)));
    return 0;
}
int rect(lua::State* L) {
    ready(L);
    ImVec2 a = pt(L, 2);
    ImVec2 b{a.x + dpf(num(L, 4)), a.y + dpf(num(L, 5))};
    ImU32 c = colorArg(L, 6, theme().scheme.primary);
    float r = dpf(num(L, 7));
    if (lua::toboolean(L, 8)) g_dl->AddRect(a, b, c, r, ImDrawFlags_None, dpf(num(L, 9, 1.0f)));
    else g_dl->AddRectFilled(a, b, c, r);
    return 0;
}
int circle(lua::State* L) {
    ready(L);
    ImVec2 c = pt(L, 2);
    float r = dpf(num(L, 4));
    ImU32 col = colorArg(L, 5, theme().scheme.primary);
    if (lua::toboolean(L, 6)) g_dl->AddCircle(c, r, col, 0, dpf(num(L, 7, 1.0f)));
    else g_dl->AddCircleFilled(c, r, col);
    return 0;
}
int text(lua::State* L) {
    ready(L);
    ImVec2 p = pt(L, 2);
    std::string_view s = lua::tostring(L, 4);
    ImU32 c = colorArg(L, 5, theme().scheme.onSurface);
    ImFont* f = ImGui::GetFont();
    if (lua::type(L, 6) == lua::TSTRING)
        if (auto st = textStyleFromName(lua::tostring(L, 6))) f = font(*st);
    g_dl->AddText(f, f->FontSize * ImGui::GetIO().FontGlobalScale, p, c, s.data(), s.data() + s.size());
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
        g_dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), colorArg(L, 3, theme().scheme.primary),
                          lua::toboolean(L, 5) ? ImDrawFlags_Closed : ImDrawFlags_None, dpf(num(L, 4, 1.0f)));
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
    /// Timer) take no layout space inside rows.
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
    /// (used by Repeater / ListView / If on the Lua side).
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
                    // Repeater / If / Loader: splice their children into ours.
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

    /// Renders children top-to-bottom; `spacing` (dp) overrides the gap
    /// between visible items (it is not inherited by nested layouts).
    void renderStack(const Node& n) {
        const auto spacing = n.number("spacing");
        lua::State* L = n.state();
        bool first = true;
        forEachChild(n, [&](int idx) {
            if (!isVisual(L, idx)) { render(L, idx); return; }
            if (!Node{L, idx}.boolean("visible", true)) return;
            if (!first && spacing)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + static_cast<float>(*spacing) * dpiScale() -
                                     ImGui::GetStyle().ItemSpacing.y);
            render(L, idx);
            first = false;
        });
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
        if (auto bg = n.color("color")) ImGui::PushStyleColor(ImGuiCol_WindowBg, bg->vec4());
        ImGui::Begin("##ApplicationWindow", nullptr, flags);
        if (n.has("color")) ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (menu) {
            n.push("menuBar");
            if (lua::istable(n.state(), -1)) r.render(n.state(), -1);
            lua::pop(n.state());
        }
        r.renderChildren(n);
        ImGui::End();
    });

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
        if (ImGui::Begin(title.c_str(), closable ? &keep : nullptr, flags)) {
            if (n.has("menuBar")) {
                n.push("menuBar");
                if (lua::istable(n.state(), -1)) r.render(n.state(), -1);
                lua::pop(n.state());
            }
            r.renderChildren(n);
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
        if (md::beginDialog(title.c_str(), &open, sizeProp(n, "width"))) {
            r.renderChildren(n);
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
        ImGui::BeginGroup();
        if (pad > 0) { ImGui::Dummy({0, dpf(pad) - ImGui::GetStyle().ItemSpacing.y}); ImGui::Indent(dpf(pad)); }
        r.renderStack(n);
        if (pad > 0) { ImGui::Unindent(dpf(pad)); ImGui::Dummy({0, dpf(pad) - ImGui::GetStyle().ItemSpacing.y}); }
        ImGui::EndGroup();
    };
    registerElement("Column", column);
    registerElement("Item", column);

    registerElement("Row", [](R& r, const Node& n) {
        lua::State* L = n.state();
        const auto spacing = n.number("spacing");
        const float sp = spacing ? dpf(static_cast<float>(*spacing)) : -1.0f;
        const std::string align = n.string("align", "center");
        ImGuiStorage* st = ImGui::GetStateStorage();
        const ImGuiID rowKey = ImGui::GetID("##rowH");
        const float rowH = st->GetFloat(rowKey, 0.0f);
        ImGui::BeginGroup();
        const float top = ImGui::GetCursorScreenPos().y;
        float maxH = 0.0f;
        bool first = true;
        r.forEachChild(n, [&](int idx) {
            if (!r.isVisual(L, idx)) { r.render(L, idx); return; }
            Node c{L, idx};
            if (!c.boolean("visible", true)) return;
            if (!first) ImGui::SameLine(0.0f, sp);
            const ImGuiID ck = ImGui::GetID(c.ptr());
            const float ch = st->GetFloat(ck, 0.0f);
            float y = top;
            if (rowH > 0 && ch > 0) {
                if (align == "center") y = top + std::max(0.0f, (rowH - ch) * 0.5f);
                else if (align == "bottom") y = top + std::max(0.0f, rowH - ch);
            }
            ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, y});
            r.render(L, idx);
            float h = ImGui::GetItemRectSize().y;
            st->SetFloat(ck, h);
            maxH = std::max(maxH, h);
            first = false;
        });
        ImGui::EndGroup();
        st->SetFloat(rowKey, maxH);
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
        float pad = n.numf("padding", 0.0f);
        if (pad > 0) {
            cf |= ImGuiChildFlags_AlwaysUseWindowPadding;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dpf(pad), dpf(pad)});
        }
        ImGui::BeginChild("##scroll", {sizeProp(n, "width"), sizeProp(n, "height")}, cf);
        if (pad > 0) ImGui::PopStyleVar();
        r.renderStack(n);
        ImGui::EndChild();
    };
    registerElement("ScrollView", scroll);
    registerElement("ListView", scroll); // children come from model/delegate (Lua side)

    registerElement("Card", [](R& r, const Node& n) {
        md::beginCard("##card", md::cardVariant(n.string("variant", "elevated")), sizeProp(n, "width"),
                      n.numf("padding", 16.0f), sizeProp(n, "height"));
        r.renderStack(n);
        md::endCard();
    });

    registerElement("TabBar", [](R& r, const Node& n) {
        if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            int i = 0;
            r.forEachChild(n, [&](int idx) {
                ++i;
                Node tab{n.state(), idx};
                if (!tab.boolean("visible", true)) return;
                std::string t = label(tab.string("title", "Tab"), "tab");
                ImGui::PushID(tab.ptr());
                ImGui::PushFont(font(TextStyle::TitleSmall));
                const bool selected = ImGui::BeginTabItem(t.c_str());
                ImGui::PopFont();
                if (selected) {
                    if (static_cast<int>(n.number("currentIndex", 0)) != i) {
                        n.set("currentIndex", static_cast<double>(i));
                        n.emit("onCurrentChanged", {static_cast<double>(i)});
                    }
                    ImGui::Dummy({0, dpf(4)});
                    r.renderChildren(tab);
                    ImGui::EndTabItem();
                }
                ImGui::PopID();
            });
            ImGui::EndTabBar();
        }
    });
    // Tabs are rendered by TabBar; a stray Tab renders its content inline.
    registerElement("Tab", [](R& r, const Node& n) { r.renderChildren(n); });

    registerElement("Section", [](R& r, const Node& n) {
        std::string t = label(n.string("title", ""), "sec");
        ImGuiTreeNodeFlags f = n.boolean("expanded", true) ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        ImGui::PushFont(font(TextStyle::TitleMedium));
        bool open = ImGui::CollapsingHeader(t.c_str(), f);
        ImGui::PopFont();
        if (open) {
            ImGui::Indent(dpf(8));
            r.renderChildren(n);
            ImGui::Unindent(dpf(8));
        }
    });

    registerElement("TreeNode", [](R& r, const Node& n) {
        std::string t = label(n.text(), "tree");
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (n.boolean("expanded", false)) f |= ImGuiTreeNodeFlags_DefaultOpen;
        if (ImGui::TreeNodeEx(t.c_str(), f)) {
            r.renderChildren(n);
            ImGui::TreePop();
        }
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
        if (ImGui::BeginMenu(t.c_str())) {
            r.renderChildren(n);
            ImGui::EndMenu();
        }
    });
    registerElement("MenuItem", [](R&, const Node& n) {
        std::string t = label(n.text(), "mi");
        std::string sc = n.string("shortcut", "");
        const bool checkable = n.has("checked");
        bool checked = n.boolean("checked", false);
        if (ImGui::MenuItem(t.c_str(), sc.empty() ? nullptr : sc.c_str(), checkable ? &checked : nullptr)) {
            if (checkable) n.set("checked", checked);
            n.emit("onTriggered");
        }
    });

    // ------------------------------------------------------------ text & decoration
    auto textFn = [](R&, const Node& n) {
        auto style = textStyleFromName(n.string("style", "bodyLarge")).value_or(TextStyle::BodyLarge);
        md::text(n.text(), style, n.color("color"), n.boolean("wrap", false));
    };
    registerElement("Text", textFn);
    registerElement("Label", textFn);

    registerElement("Link", [](R&, const Node& n) {
        std::string t = n.text();
        std::string url = n.string("url", t);
        ImGui::PushFont(font(TextStyle::BodyLarge));
        ImGui::TextLinkOpenURL(t.c_str(), url.c_str());
        ImGui::PopFont();
    });

    registerElement("Separator", [](R&, const Node& n) {
        std::string t = n.text();
        if (t.empty()) ImGui::Separator();
        else {
            ImGui::PushFont(font(TextStyle::TitleSmall));
            ImGui::SeparatorText(t.c_str());
            ImGui::PopFont();
        }
    });
    registerElement("Spacer", [](R&, const Node& n) {
        ImGui::Dummy({std::max(1.0f, sizeProp(n, "width")), std::max(1.0f, sizeProp(n, "height"))});
    });

    // ------------------------------------------------------------ buttons & selection
    registerElement("Button", [](R&, const Node& n) {
        std::string t = label(n.text(), "btn");
        if (md::button(t.c_str(), md::buttonVariant(n.string("variant", "filled")), {sizeProp(n, "width"), sizeProp(n, "height")}))
            n.emit("onClicked");
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
        if (md::switchToggle(t.c_str(), &v)) {
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
        ImGui::BeginGroup();
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (horizontal && i > 0) ImGui::SameLine();
            ImGui::PushID(i);
            std::string t = label(items[i], "r");
            if (md::radio(t.c_str(), cur == i + 1) && cur != i + 1) {
                n.set("currentIndex", static_cast<double>(i + 1));
                n.emit("onActivated", {static_cast<double>(i + 1), items[i]});
            }
            ImGui::PopID();
        }
        ImGui::EndGroup();
    });

    registerElement("Chip", [](R&, const Node& n) {
        std::string t = label(n.text(), "chip");
        bool v = n.boolean("checked", false);
        if (md::chip(t.c_str(), v)) {
            if (n.boolean("checkable", true)) n.set("checked", !v);
            n.emit("onClicked", {!v});
        }
    });

    registerElement("SegmentedButton", [](R&, const Node& n) {
        auto items = n.strings("model");
        int cur = static_cast<int>(n.number("currentIndex", 1)) - 1;
        if (md::segmented("##seg", items, &cur, sizeProp(n, "width"))) {
            n.set("currentIndex", static_cast<double>(cur + 1));
            n.emit("onActivated", {static_cast<double>(cur + 1), items[cur]});
        }
    });

    registerElement("ListItem", [](R&, const Node& n) {
        if (md::listItem("##li", n.text(), n.string("supportingText", ""), n.boolean("selected", false), sizeProp(n, "width")))
            n.emit("onClicked");
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
        int decimals = static_cast<int>(n.number("decimals", 0));
        float w = sizeProp(n, "width");
        ImGui::SetNextItemWidth(w != 0 ? w : dpf(160));
        bool changed = false;
        if (decimals <= 0) {
            int iv = static_cast<int>(v);
            if (ImGui::InputInt("##spin", &iv, static_cast<int>(step))) { v = iv; changed = true; }
        } else {
            char fmt[16];
            std::snprintf(fmt, sizeof fmt, "%%.%df", decimals);
            if (ImGui::InputDouble("##spin", &v, step, step * 10, fmt)) changed = true;
        }
        if (changed) {
            v = std::clamp(v, from, to);
            n.set("value", v);
            n.emit("onValueModified", {v});
        }
    });

    auto field = [](bool multiline) {
        return [multiline](R&, const Node& n) {
            std::string text = n.string("text", "");
            std::string lbl = n.string("label", "");
            std::string ph = n.string("placeholderText", "");
            bool password = n.boolean("password", false) || n.string("echoMode", "") == "password";
            auto res = md::textField("##tf", text, lbl.c_str(), ph.c_str(), password, multiline, sizeProp(n, "width"),
                                     sizeProp(n, "height"), n.boolean("readOnly", false));
            if (res.changed) {
                n.set("text", text);
                n.emit("onTextChanged", {text});
            }
            if (res.accepted) n.emit("onAccepted", {text});
        };
    };
    registerElement("TextField", field(false));
    registerElement("TextArea", field(true));

    registerElement("ComboBox", [](R&, const Node& n) {
        auto items = n.strings("model");
        int cur = static_cast<int>(n.number("currentIndex", 1)) - 1;
        std::string lbl = n.string("label", "");
        float w = sizeProp(n, "width");
        if (!lbl.empty()) md::text(lbl, TextStyle::BodySmall, theme().scheme.onSurfaceVariant);
        ImGui::SetNextItemWidth(w != 0 ? w : dpf(240));
        const char* preview = cur >= 0 && cur < static_cast<int>(items.size()) ? items[cur].c_str() : "";
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {dpf(16), dpf(10)});
        bool open = ImGui::BeginCombo("##combo", preview);
        ImGui::PopStyleVar();
        if (open) {
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(items[i].c_str(), i == cur, ImGuiSelectableFlags_None, {0, dpf(32)}) && i != cur) {
                    n.set("currentIndex", static_cast<double>(i + 1));
                    n.emit("onActivated", {static_cast<double>(i + 1), items[i]});
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    });

    registerElement("ColorPicker", [](R&, const Node& n) {
        auto c = n.color("color").value_or(Color{1, 1, 1, 1});
        float v[4] = {c.r, c.g, c.b, c.a};
        std::string lbl = label(n.string("label", ""), "cp");
        const bool alpha = n.boolean("alpha", false);
        float w = sizeProp(n, "width");
        ImGui::SetNextItemWidth(w != 0 ? w : dpf(240));
        bool changed = alpha ? ImGui::ColorEdit4(lbl.c_str(), v, ImGuiColorEditFlags_AlphaBar)
                             : ImGui::ColorEdit3(lbl.c_str(), v);
        if (changed) {
            std::string hex = toHex({v[0], v[1], v[2], alpha ? v[3] : 1.0f});
            n.set("color", hex);
            n.emit("onColorChanged", {hex});
        }
    });

    // ------------------------------------------------------------ progress & data
    registerElement("ProgressBar", [](R&, const Node& n) {
        md::progress(n.boolean("indeterminate", false) ? -1.0f : n.numf("value", 0.0f), sizeProp(n, "width"));
    });
    registerElement("BusyIndicator", [](R&, const Node& n) {
        if (n.boolean("running", true)) md::spinner(sizeProp(n, "size"));
    });

    registerElement("Plot", [](R&, const Node& n) {
        auto values = n.floats("values");
        std::string lbl = label(n.string("label", ""), "plot");
        ImVec2 size{sizeProp(n, "width"), sizeProp(n, "height")};
        if (size.x == 0) size.x = -1.0f;
        if (size.y == 0) size.y = dpf(120);
        if (size.x < 0) size.x = ImGui::GetContentRegionAvail().x;
        float mn = n.has("min") ? n.numf("min", 0) : 3.402823466e+38f;
        float mx = n.has("max") ? n.numf("max", 1) : 3.402823466e+38f;
        std::string overlay = n.string("overlay", "");
        const char* ov = overlay.empty() ? nullptr : overlay.c_str();
        if (n.string("kind", "lines") == "histogram")
            ImGui::PlotHistogram(lbl.c_str(), values.data(), static_cast<int>(values.size()), 0, ov, mn, mx, size);
        else
            ImGui::PlotLines(lbl.c_str(), values.data(), static_cast<int>(values.size()), 0, ov, mn, mx, size);
    });

    registerElement("Table", [](R&, const Node& n) {
        lua::State* L = n.state();
        auto headers = n.strings("columns");
        if (headers.empty()) return;
        ImGuiTableFlags f = ImGuiTableFlags_Borders | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchSame;
        if (n.boolean("rowBg", true)) f |= ImGuiTableFlags_RowBg;
        if (!ImGui::BeginTable("##table", static_cast<int>(headers.size()), f, {sizeProp(n, "width"), 0})) return;
        for (auto& h : headers) ImGui::TableSetupColumn(h.c_str());
        ImGui::TableHeadersRow();
        if (n.push("rows") == lua::TTABLE) {
            const int rows = lua::gettop(L);
            const lua::Integer nr = lua::rawlen(L, rows);
            const int selected = static_cast<int>(n.number("currentRow", 0));
            for (lua::Integer i = 1; i <= nr; ++i) {
                lua::rawgeti(L, rows, i);
                ImGui::TableNextRow();
                for (int c = 0; c < static_cast<int>(headers.size()); ++c) {
                    ImGui::TableNextColumn();
                    lua::rawgeti(L, -1, c + 1);
                    std::string cell = lua::isnil(L, -1) ? std::string{} : lua::tostring_any(L, -1);
                    lua::pop(L);
                    if (c == 0) {
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(cell.c_str(), selected == i, ImGuiSelectableFlags_None)) {
                            n.set("currentRow", static_cast<double>(i));
                            n.emit("onRowClicked", {static_cast<double>(i)});
                        }
                        ImGui::PopID();
                    } else {
                        ImGui::TextUnformatted(cell.c_str());
                    }
                }
                lua::pop(L);
            }
        }
        lua::pop(L);
        ImGui::EndTable();
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
        // painter object
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

    // ------------------------------------------------------------ non-visual
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
    lua::createtable(L, 0, 5);          // methods
    lua::pushcfunction(L, &painter::line);     lua::setfield(L, -2, "line");
    lua::pushcfunction(L, &painter::rect);     lua::setfield(L, -2, "rect");
    lua::pushcfunction(L, &painter::circle);   lua::setfield(L, -2, "circle");
    lua::pushcfunction(L, &painter::text);     lua::setfield(L, -2, "text");
    lua::pushcfunction(L, &painter::polyline); lua::setfield(L, -2, "polyline");
    lua::setfield(L, -2, "__index");
    lua::setfield(L, lua::REGISTRYINDEX, "imlui.painter");
}

} // namespace imlui
