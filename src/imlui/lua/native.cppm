// The `imlui.native` Lua library: theme, colors, time, text metrics, logging
// and app control (quit / reload / title, through NativeHooks).
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

export namespace imlui::luaui {

/// App-level actions the host provides to scripts. Empty functions are no-ops.
struct NativeHooks {
    std::function<void()> quit, reload;
    std::function<void(const std::string&)> setTitle;
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
         lua::pushnumber(state, ImGui::GetIO().DeltaTime);
         return 1;
     }},
    {"fps",
     [](lua::State* state) -> int {
         lua::pushnumber(state, ImGui::GetIO().Framerate);
         return 1;
     }},
    {"dpiScale",
     [](lua::State* state) -> int {
         lua::pushnumber(state, dpiScale());
         return 1;
     }},
    {"textSize",
     [](lua::State* state) -> int {
         std::string_view s = lua::tostring(state, 1);
         TextStyle st = TextStyle::BodyLarge;
         if (lua::type(state, 2) == lua::Type::String)
             if (auto t = textStyleFromName(lua::tostring(state, 2))) st = *t;
         ImVec2 sz = font(st)->CalcTextSizeA(fontPixels(st), 3.4e38f, 0.0f, s.data(), s.data() + s.size());
         lua::pushnumber(state, sz.x / dpiScale());
         lua::pushnumber(state, sz.y / dpiScale());
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
