// The `imlui.native` Lua library: theme, colors, time, text metrics, window size classes, logging
// and app control (quit / reload / title / window size, through NativeHooks).
module;

#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

export module imlui.lua.native;

import lua;
import imgui;
import imlui.log;
import imlui.color;
import imlui.color.oklch;
import imlui.color.tonal_palette;
import imlui.theme.dpi;
import imlui.theme.typography;
import imlui.theme.theme;
import imlui.lua.node;
import imlui.lua.theme_table;
import imlui.ui.frame_request;
import imlui.ui.size_class;

export namespace imlui::luaui {

/// App-level actions the host provides to scripts. Empty functions are no-ops,
/// except the zoom hooks, which fall back to imlui.theme.dpi directly.
struct NativeHooks {
    std::function<void()> quit, reload;
    std::function<void(const std::string&)> setTitle;
    std::function<float()> zoom;        ///< current or pending zoom
    std::function<void(float)> setZoom; ///< schedules a zoom change for the next frame
    std::function<void(bool)> setZoomShortcuts;
    /// Resizes the native window to w x h dp (times the monitor content scale, not the zoom).
    std::function<void(int, int)> setWindowSize;
};

/// Makes `require("imlui.native")` available in `state`. `hooks` is stored in
/// the registry and must outlive the state.
void registerNative(lua::State* state, NativeHooks* hooks);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

constexpr const char* hooksKey = "imlui.native.hooks";

NativeHooks& hostHooks(lua::State* state) {
    lua::getfield(state, lua::registryIndex, hooksKey);
    auto* h = static_cast<NativeHooks*>(lua::touserdata(state, -1));
    lua::pop(state);
    if (!h) lua::error(state, "imlui.native: no host hooks registered");
    return *h;
}

/// Main viewport size in dp (pixels / dpiScale()): last frame's size, or the display size before the first frame.
ImVec2 windowSizeDp() {
    ImVec2 px = ImGui::GetIO().DisplaySize;
    if (const ImGuiViewport* vp = ImGui::GetMainViewport(); vp && vp->Size.x > 0.0f) px = vp->Size;
    const float scale = dpiScale();
    return {px.x / scale, px.y / scale};
}

Color argColor(lua::State* state, int idx) {
    if (lua::type(state, idx) == lua::Type::String)
        if (auto c = resolveColor(lua::tostring(state, idx))) return *c;
    if (lua::istable(state, idx))
        if (auto c = Node::colorFromTable(state, idx)) return *c;
    lua::error(state, "expected a color (hex string, role name or {r,g,b,a})");
}

struct NativeFn {
    const char* name;
    lua::CFunction fn;
};

constexpr NativeFn nativeFns[] = {
    {"setTheme",
     [](lua::State* state) -> int {
         if (!lua::istable(state, 1)) lua::error(state, "setTheme expects a table");
         Theme t = themeFromTable(state, 1);
         pushScheme(state, t.scheme);
         setTheme(std::move(t));
         return 1;
     }},
    {"schemeFor",
     [](lua::State* state) -> int {
         if (!lua::istable(state, 1)) lua::error(state, "schemeFor expects a table");
         pushScheme(state, themeFromTable(state, 1).scheme);
         return 1;
     }},
    {"scheme",
     [](lua::State* state) -> int {
         pushScheme(state, theme().scheme);
         return 1;
     }},
    {"isDark",
     [](lua::State* state) -> int {
         lua::pushboolean(state, theme().dark);
         return 1;
     }},
    {"color",
     [](lua::State* state) -> int {
         lua::pushstring(state, toHex(argColor(state, 1)));
         return 1;
     }},
    {"tone",
     [](lua::State* state) -> int {
         Oklch o = toOklch(argColor(state, 1));
         TonalPalette p{o.h, o.c};
         lua::pushstring(state, toHex(p.tone(static_cast<float>(lua::tonumber(state, 2)))));
         return 1;
     }},
    {"mix",
     [](lua::State* state) -> int {
         lua::pushstring(
             state, toHex(mix(argColor(state, 1), argColor(state, 2), static_cast<float>(lua::tonumber(state, 3)))));
         return 1;
     }},
    {"alpha",
     [](lua::State* state) -> int {
         lua::pushstring(state, toHex(argColor(state, 1).withAlpha(static_cast<float>(lua::tonumber(state, 2)))));
         return 1;
     }},
    {"time",
     [](lua::State* state) -> int {
         lua::pushnumber(state, ImGui::GetTime());
         return 1;
     }},
    {"deltaTime",
     [](lua::State* state) -> int {
         lua::pushnumber(state, static_cast<double>(ImGui::GetIO().DeltaTime));
         return 1;
     }},
    {"fps",
     [](lua::State* state) -> int {
         lua::pushnumber(state, static_cast<double>(ImGui::GetIO().Framerate));
         return 1;
     }},
    {"dpiScale",
     [](lua::State* state) -> int {
         lua::pushnumber(state, static_cast<double>(dpiScale()));
         return 1;
     }},
    {"contentScale",
     [](lua::State* state) -> int {
         lua::pushnumber(state, static_cast<double>(contentScale()));
         return 1;
     }},
    {"zoom",
     [](lua::State* state) -> int {
         auto& fn = hostHooks(state).zoom;
         lua::pushnumber(state, static_cast<double>(fn ? fn() : zoom()));
         return 1;
     }},
    {"setZoom",
     [](lua::State* state) -> int {
         if (!lua::isnumber(state, 1)) lua::error(state, "setZoom expects a number");
         const float z = static_cast<float>(lua::tonumber(state, 1));
         if (auto& fn = hostHooks(state).setZoom)
             fn(z);
         else
             setZoom(z);
         return 0;
     }},
    {"setZoomShortcuts",
     [](lua::State* state) -> int {
         if (auto& fn = hostHooks(state).setZoomShortcuts) fn(lua::toboolean(state, 1));
         return 0;
     }},
    // windowSize() -> w, h: the main viewport in dp.
    {"windowSize",
     [](lua::State* state) -> int {
         const ImVec2 sz = windowSizeDp();
         lua::pushnumber(state, static_cast<double>(sz.x));
         lua::pushnumber(state, static_cast<double>(sz.y));
         return 2;
     }},
    // sizeClass() -> "compact" | "medium" | "expanded" | "large" | "extraLarge", from windowSize()'s width.
    {"sizeClass",
     [](lua::State* state) -> int {
         lua::pushstring(state, sizeClassName(sizeClassForWidth(windowSizeDp().x)));
         return 1;
     }},
    // sizeClassFor(widthDp) -> name: the size class of any width.
    {"sizeClassFor",
     [](lua::State* state) -> int {
         if (!lua::isnumber(state, 1)) lua::error(state, "sizeClassFor expects a width in dp");
         lua::pushstring(state, sizeClassName(sizeClassForWidth(static_cast<float>(lua::tonumber(state, 1)))));
         return 1;
     }},
    // setWindowSize(w, h): native window size in dp, applied after the current frame.
    {"setWindowSize",
     [](lua::State* state) -> int {
         if (!lua::isnumber(state, 1) || !lua::isnumber(state, 2))
             lua::error(state, "setWindowSize expects a width and a height in dp");
         const double w = lua::tonumber(state, 1), h = lua::tonumber(state, 2);
         if (!(w >= 1.0 && h >= 1.0 && w <= 100000.0 && h <= 100000.0))
             lua::error(state, "setWindowSize: width and height must be between 1 and 100000 dp");
         if (auto& fn = hostHooks(state).setWindowSize) fn(static_cast<int>(w + 0.5), static_cast<int>(h + 0.5));
         return 0;
     }},
    // requestFrame([seconds]): render a frame now, or after `seconds`.
    {"requestFrame",
     [](lua::State* state) -> int {
         requestFrameAfter(lua::isnumber(state, 1) ? lua::tonumber(state, 1) : 0.0);
         return 0;
     }},
    {"textSize",
     [](lua::State* state) -> int {
         std::string_view s = lua::tostring(state, 1);
         TextStyle st = TextStyle::BodyLarge;
         if (lua::type(state, 2) == lua::Type::String)
             if (auto t = textStyleFromName(lua::tostring(state, 2))) st = *t;
         ImVec2 sz = font(st)->CalcTextSizeA(fontPixels(st), 3.4e38f, 0.0f, s.data(), s.data() + s.size());
         lua::pushnumber(state, static_cast<double>(sz.x / dpiScale()));
         lua::pushnumber(state, static_cast<double>(sz.y / dpiScale()));
         return 2;
     }},
    {"setTitle",
     [](lua::State* state) -> int {
         std::string title{lua::tostring(state, 1)};
         if (auto& fn = hostHooks(state).setTitle) fn(title);
         return 0;
     }},
    {"quit",
     [](lua::State* state) -> int {
         if (auto& fn = hostHooks(state).quit) fn();
         return 0;
     }},
    {"reload",
     [](lua::State* state) -> int {
         if (auto& fn = hostHooks(state).reload) fn();
         return 0;
     }},
    {"imguiVersion",
     [](lua::State* state) -> int {
         lua::pushstring(state, ImGui::GetVersion());
         return 1;
     }},
    // log(level, message) — level: "trace" | "debug" | "info" | "warn" | "error"
    {"log",
     [](lua::State* state) -> int {
         std::string_view lv = lua::tostring(state, 1);
         log::Level l = lv == "trace"   ? log::Level::Trace
                        : lv == "debug" ? log::Level::Debug
                        : lv == "warn"  ? log::Level::Warn
                        : lv == "error" ? log::Level::Error
                                        : log::Level::Info;
         log::write(l, lua::toStringAny(state, 2), "lua");
         return 0;
     }},
    // injectMouse(x, y [, down]) — feeds ImGui a mouse event in window pixels (for UI tests).
    {"injectMouse",
     [](lua::State* state) -> int {
         ImGuiIO& io = ImGui::GetIO();
         io.AddMousePosEvent(static_cast<float>(lua::tonumber(state, 1)), static_cast<float>(lua::tonumber(state, 2)));
         if (!lua::isnoneornil(state, 3)) io.AddMouseButtonEvent(0, lua::toboolean(state, 3));
         if (lua::isnumber(state, 4)) io.AddMouseWheelEvent(0.0f, static_cast<float>(lua::tonumber(state, 4)));
         requestFramesFor(0.3); // like real input: let the UI react and settle
         return 0;
     }},
};

int openNative(lua::State* state) {
    lua::createtable(state, 0, static_cast<int>(std::size(nativeFns)));
    for (const auto& f : nativeFns) {
        lua::pushcfunction(state, f.fn);
        lua::setfield(state, -2, f.name);
    }
    return 1;
}

} // namespace

void registerNative(lua::State* state, NativeHooks* hooks) {
    lua::pushlightuserdata(state, hooks);
    lua::setfield(state, lua::registryIndex, hooksKey);
    lua::getglobal(state, "package");
    lua::getfield(state, -1, "preload");
    lua::pushcfunction(state, &openNative);
    lua::setfield(state, -2, "imlui.native");
    lua::pop(state, 2);
}

} // namespace imlui::luaui
