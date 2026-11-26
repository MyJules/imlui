// The `imlui.native` Lua library: theme, colors, time, text metrics, window size classes, logging
// and app control (quit / reload / title / window size, through NativeHooks).
export module imlui.lua.native;

import std;

import lua;
import imgui;
import imlui.log;
import imlui.color;
import imlui.color.oklch;
import imlui.color.tonal_palette;
import imlui.theme.dpi;
import imlui.theme.font_face_scope;
import imlui.theme.font_library;
import imlui.theme.typography;
import imlui.theme.theme;
import imlui.lua.element_scope;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.lua.theme_table;
import imlui.lua.value;
import imlui.ui.clock;
import imlui.ui.culling;
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

/// Case-insensitive ASCII comparison.
bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

/// A keyboard or gamepad key by its ImGui name ("Escape", "Enter", "Tab", "A", "0", "Equal", "Minus", "F5",
/// "LeftArrow", "Keypad1", ...) or a common alias ("Esc", "Return", "Left", "Ctrl", ...), ignoring case.
/// ImGuiKey_None when unknown. Mouse "keys" don't count: mouse input goes through injectMouse.
ImGuiKey keyFromName(std::string_view name) {
    constexpr std::pair<std::string_view, std::string_view> aliases[] = {
        {"Esc", "Escape"},      {"Return", "Enter"},     {"Left", "LeftArrow"},  {"Right", "RightArrow"},
        {"Up", "UpArrow"},      {"Down", "DownArrow"},   {"Del", "Delete"},      {"Ins", "Insert"},
        {"Ctrl", "LeftCtrl"},   {"Control", "LeftCtrl"}, {"Shift", "LeftShift"}, {"Alt", "LeftAlt"},
        {"Super", "LeftSuper"}, {"=", "Equal"},          {"-", "Minus"},         {",", "Comma"},
        {".", "Period"},        {"/", "Slash"},          {" ", "Space"},
    };
    for (const auto& [alias, key] : aliases)
        if (equalsIgnoringCase(name, alias)) {
            name = key;
            break;
        }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_MouseLeft; ++k) {
        const auto key = static_cast<ImGuiKey>(k);
        if (equalsIgnoringCase(name, ImGui::GetKeyName(key))) return key;
    }
    return ImGuiKey_None;
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
         // Argument 2: the script that declared the theme, for relative font names.
         const std::string_view script = lua::type(state, 2) == lua::Type::String ? lua::tostring(state, 2) : "";
         Theme t = themeFromTable(state, 1, script);
         // Loaded now, so a missing or broken font is reported with the script still running; the theme
         // switches to them when it is applied (between frames).
         for (const auto& e : loadFontSet(t.fonts))
             reportError("theme.fonts." + e);
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
         lua::pushstring(state, toHex(p.tone(toFloat(lua::tonumber(state, 2)))));
         return 1;
     }},
    {"mix",
     [](lua::State* state) -> int {
         lua::pushstring(
             state, toHex(mix(argColor(state, 1), argColor(state, 2), clampUnit(toFloat(lua::tonumber(state, 3))))));
         return 1;
     }},
    {"alpha",
     [](lua::State* state) -> int {
         lua::pushstring(state, toHex(argColor(state, 1).withAlpha(toFloat(lua::tonumber(state, 2)))));
         return 1;
     }},
    {"time",
     [](lua::State* state) -> int {
         lua::pushnumber(state, clockTime());
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
         const float z = toFloat(lua::tonumber(state, 1));
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
    // setCulling(on): whether containers skip the children out of view (imlui.ui.culling). On by default and again
    // in every new Lua state (a reload).
    {"setCulling",
     [](lua::State* state) -> int {
         cull::setEnabled(lua::toboolean(state, 1));
         requestFrame();
         return 0;
     }},
    {"culling",
     [](lua::State* state) -> int {
         lua::pushboolean(state, cull::enabled());
         return 1;
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
         lua::pushstring(state, sizeClassName(sizeClassForWidth(toFloat(lua::tonumber(state, 1)))));
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
    // requestFrame([seconds]): render a frame now, or after `seconds` (a finite number).
    {"requestFrame",
     [](lua::State* state) -> int {
         const double seconds = lua::isnumber(state, 1) ? lua::tonumber(state, 1) : 0.0;
         if (!std::isfinite(seconds)) lua::error(state, "requestFrame: the delay must be a finite number");
         requestFrameAfter(seconds);
         return 0;
     }},
    {"textSize",
     [](lua::State* state) -> int {
         // Checked: a non-string has no text, and CalcTextSizeA would take a null range as a C string.
         if (!lua::isstring(state, 1)) lua::error(state, "textSize expects a string");
         std::string_view s = lua::tostring(state, 1);
         TextStyle st = TextStyle::BodyLarge;
         if (lua::type(state, 2) == lua::Type::String)
             if (auto t = textStyleFromName(lua::tostring(state, 2))) st = *t;
         const FontFaceScope face{lua::type(state, 3) == lua::Type::String ? lua::tostring(state, 3) : ""};
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
    // setScopeHooks(scopes, callIn) — the DSL's element scopes, for C++ calls of element functions (element_scope).
    {"setScopeHooks",
     [](lua::State* state) -> int {
         if (!lua::istable(state, 1) || !lua::isfunction(state, 2))
             lua::error(state, "setScopeHooks expects a table and a function");
         luaui::setScopeHooks(state, 1, 2);
         return 0;
     }},
    // reportError(message) — shows a script error in the error overlay (and logs it once).
    {"reportError",
     [](lua::State* state) -> int {
         luaui::reportError(lua::toStringAny(state, 1));
         return 0;
     }},
    // injectMouse(x, y [, down]) — feeds ImGui a mouse event in window pixels (for UI tests).
    {"injectMouse",
     [](lua::State* state) -> int {
         ImGuiIO& io = ImGui::GetIO();
         io.AddMousePosEvent(toFloat(lua::tonumber(state, 1)), toFloat(lua::tonumber(state, 2)));
         if (!lua::isnoneornil(state, 3)) io.AddMouseButtonEvent(0, lua::toboolean(state, 3));
         if (lua::isnumber(state, 4)) io.AddMouseWheelEvent(0.0f, toFloat(lua::tonumber(state, 4)));
         requestFramesFor(0.3); // like real input: let the UI react and settle
         return 0;
     }},
    // injectKey(key, down [, ctrl, shift, alt]) — feeds ImGui a key event (for UI tests). `key` is a key
    // name ("Escape", "Enter", "Tab", "A", "0", "Equal", ...). The modifier states are sent first, so the key
    // sees them. A press and a release queued in the same frame are seen in consecutive frames.
    {"injectKey",
     [](lua::State* state) -> int {
         if (lua::type(state, 1) != lua::Type::String) lua::error(state, "injectKey expects a key name");
         const ImGuiKey key = keyFromName(lua::tostring(state, 1));
         if (key == ImGuiKey_None)
             lua::error(state, "injectKey: unknown key '" + std::string{lua::tostring(state, 1)} + "'");
         ImGuiIO& io = ImGui::GetIO();
         io.AddKeyEvent(ImGuiMod_Ctrl, lua::toboolean(state, 3));
         io.AddKeyEvent(ImGuiMod_Shift, lua::toboolean(state, 4));
         io.AddKeyEvent(ImGuiMod_Alt, lua::toboolean(state, 5));
         io.AddKeyEvent(key, lua::toboolean(state, 2));
         requestFramesFor(0.3); // like real input: let the UI react and settle
         return 0;
     }},
};

/// Upvalue 1: the index of the function in nativeFns. Runs it with C++ exceptions (from the host's NativeHooks, the
/// theme or font loading, ...) turned into Lua errors that name the function ("imlui.native.setTheme: what").
int callNative(lua::State* state) {
    const NativeFn& f = nativeFns[static_cast<std::size_t>(lua::tointeger(state, lua::upvalueindex(1)))];
    return catchCxx(state, [&] { return f.fn(state); }, "imlui.native.", f.name);
}

int openNative(lua::State* state) {
    lua::createtable(state, 0, static_cast<int>(std::size(nativeFns)));
    for (std::size_t i = 0; i < std::size(nativeFns); ++i) {
        lua::pushinteger(state, static_cast<lua::Integer>(i));
        lua::pushcclosure(state, &callNative, 1);
        lua::setfield(state, -2, nativeFns[i].name);
    }
    return 1;
}

} // namespace

void registerNative(lua::State* state, NativeHooks* hooks) {
    cull::setEnabled(true); // a new state (a reload) starts with the default
    lua::pushlightuserdata(state, hooks);
    lua::setfield(state, lua::registryIndex, hooksKey);
    lua::getglobal(state, "package");
    lua::getfield(state, -1, "preload");
    lua::pushcfunction(state, &openNative);
    lua::setfield(state, -2, "imlui.native");
    lua::pop(state, 2);
}

} // namespace imlui::luaui
