// Engine: owns the native window, the Lua state and the frame loop.
//
//  * runs lua/imlui.lua (the declarative DSL) and the user's script,
//    which returns the root element (usually an ApplicationWindow)
//  * renders that tree every frame through the Renderer
//  * hot-reloads when any .lua file next to the script changes
//  * exposes `imlui.native` to Lua (theme, colors, time, quit, ...)
module;

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module imlui.engine;

import lua;
import imgui;
import imlui.platform;
import imlui.color;
import imlui.theme;
import imlui.widgets;
import imlui.luaui;
import imlui.renderer;

namespace fs = std::filesystem;

export namespace imlui {

struct EngineConfig {
    fs::path script;                 ///< main UI script
    std::vector<fs::path> luaPaths;  ///< dirs searched for imlui.lua & modules
    fs::path fontDir;                ///< Roboto + Material Icons
    platform::WindowConfig window;
    bool hotReload = true;
};

/// Standard config for an app host: script from argv[1] (or
/// examples/gallery.lua), lua/ and fonts/ next to the executable. If
/// `sourceDir` is given (dev builds), lua/ and examples/ are taken from the
/// source tree so edits hot-reload without rebuilding.
EngineConfig defaultConfig(int argc, char** argv, const char* sourceDir = nullptr) {
    const fs::path exeDir = fs::absolute(fs::path{argv[0]}).parent_path();
    const fs::path root = (sourceDir && *sourceDir && fs::is_directory(sourceDir)) ? fs::path{sourceDir} : exeDir;
    EngineConfig cfg;
    cfg.script = argc > 1 ? fs::absolute(fs::path{argv[1]}) : root / "examples" / "gallery.lua";
    cfg.luaPaths = {root / "lua"};
    cfg.fontDir = exeDir / "fonts";
    return cfg;
}

class Engine {
public:
    explicit Engine(EngineConfig cfg) : cfg_{std::move(cfg)} {}
    ~Engine() { closeState(L_); }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Register custom C++ elements here before run().
    Renderer& renderer() { return renderer_; }

    void requestQuit() { quit_ = true; }
    void requestReload() { reload_ = true; }
    platform::Window& window() { return window_; }

    int run() {
        // The window starts hidden: its title and size come from the script.
        platform::WindowConfig wc = cfg_.window;
        wc.visible = false;
        if (!window_.create(wc)) {
            std::fprintf(stderr, "[imlui] failed to create window\n");
            return 1;
        }
        loadFonts(cfg_.fontDir, window_.contentScale());
        setTheme(Theme{});
        applyPendingTheme();

        reloadNow();
        window_.center();
        window_.show();
        snapshot_ = scanFiles();
        auto lastCheck = std::chrono::steady_clock::now();

        while (!window_.shouldClose() && !quit_) {
            if (cfg_.hotReload) {
                auto now = std::chrono::steady_clock::now();
                if (now - lastCheck > std::chrono::milliseconds(400)) {
                    lastCheck = now;
                    auto snap = scanFiles();
                    if (snap != snapshot_) { snapshot_ = std::move(snap); reload_ = true; }
                }
            }
            if (reload_) { reload_ = false; reloadNow(); }

            applyPendingTheme();
            window_.beginFrame();
            frame();
            drawErrors();
            ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            window_.endFrame(bg.x, bg.y, bg.z);
            if (!renderer_.windowTitle().empty()) window_.setTitle(renderer_.windowTitle());
        }
        closeState(L_);
        L_ = nullptr;
        window_.destroy();
        return 0;
    }

private:
    EngineConfig cfg_;
    platform::Window window_;
    Renderer renderer_;
    lua::State* L_ = nullptr;
    int rootRef_ = lua::NOREF;
    int tickRef_ = lua::NOREF;
    int overlayRef_ = lua::NOREF;
    bool quit_ = false;
    bool reload_ = false;
    int appliedW_ = 0, appliedH_ = 0;
    std::map<fs::path, fs::file_time_type> snapshot_;

    // ------------------------------------------------------------ frame
    void frame() {
        if (!L_ || rootRef_ == lua::NOREF) return;
        lua::StackGuard guard{L_};
        if (tickRef_ != lua::NOREF) {
            lua::rawgeti(L_, lua::REGISTRYINDEX, tickRef_);
            lua::pushnumber(L_, ImGui::GetTime());
            luaui::safeCall(L_, 1, 0);
        }
        lua::rawgeti(L_, lua::REGISTRYINDEX, rootRef_);
        renderer_.render(L_, -1);
        lua::settop(L_, 0);
        if (overlayRef_ != lua::NOREF) {
            // ui.__overlays: snackbars and other app-level overlays
            lua::rawgeti(L_, lua::REGISTRYINDEX, overlayRef_);
            luaui::Node o{L_, -1};
            renderer_.renderChildren(o);
        }
    }

    void drawErrors() {
        const auto& errs = luaui::errors();
        if (errs.empty()) return;
        const Scheme& s = theme().scheme;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float d = dpiScale();
        ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 16 * d},
                                ImGuiCond_Always, {0.5f, 1.0f});
        ImGui::SetNextWindowSizeConstraints({vp->WorkSize.x * 0.6f, 0}, {vp->WorkSize.x * 0.9f, vp->WorkSize.y * 0.6f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, s.errorContainer.vec4());
        ImGui::PushStyleColor(ImGuiCol_Text, s.onErrorContainer.vec4());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12 * d);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16 * d, 16 * d});
        ImGui::Begin("##imlui_errors", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        md::text("Lua error", TextStyle::TitleMedium);
        ImGui::BeginChild("##errlist", {0, 0}, ImGuiChildFlags_AutoResizeY);
        for (const auto& e : errs) {
            md::text(e, TextStyle::BodySmall, std::nullopt, true);
            ImGui::Spacing();
        }
        ImGui::EndChild();
        if (md::button("Dismiss", md::ButtonVariant::Text)) luaui::clearErrors();
        ImGui::SameLine();
        if (md::button("Reload", md::ButtonVariant::Text)) reload_ = true;
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    // ------------------------------------------------------------ loading
    std::map<fs::path, fs::file_time_type> scanFiles() const {
        std::map<fs::path, fs::file_time_type> out;
        std::vector<fs::path> dirs = cfg_.luaPaths;
        dirs.push_back(cfg_.script.parent_path());
        std::error_code ec;
        for (const auto& dir : dirs) {
            if (!fs::is_directory(dir, ec)) continue;
            for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file(ec) && it->path().extension() == ".lua")
                    out[it->path()] = it->last_write_time(ec);
            }
        }
        return out;
    }

    static void closeState(lua::State* L) {
        if (L) lua::close(L);
    }

    void reloadNow() {
        luaui::clearErrors();
        Theme previous = theme();
        setTheme(Theme{}); // scripts start from the default theme

        lua::State* L = lua::newstate();
        lua::openlibs(L);
        lua::pushlightuserdata(L, this);
        lua::setfield(L, lua::REGISTRYINDEX, "imlui.engine");
        registerNative(L);
        registerPainter(L);

        // package.path: <luaPaths>/?.lua ; <scriptdir>/?.lua ; default
        std::string path;
        for (const auto& p : cfg_.luaPaths) path += (p / "?.lua").generic_string() + ";" + (p / "?/init.lua").generic_string() + ";";
        path += (cfg_.script.parent_path() / "?.lua").generic_string() + ";";
        lua::getglobal(L, "package");
        lua::getfield(L, -1, "path");
        path += std::string{lua::tostring(L, -1)};
        lua::pop(L);
        lua::pushstring(L, path);
        lua::setfield(L, -2, "path");
        lua::pop(L);

        constexpr std::string_view boot = R"(local ui = require("imlui"); return ui.__load(...), ui.__tick, ui.__overlays)";
        bool ok = lua::loadbuffer(L, boot, "=imlui.boot") == lua::OK;
        if (!ok) { luaui::reportError(std::string{lua::tostring(L, -1)}); }
        if (ok) {
            lua::pushstring(L, cfg_.script.generic_string());
            ok = luaui::safeCall(L, 1, 3);
        }
        if (ok && !lua::istable(L, -3)) {
            luaui::reportError(cfg_.script.generic_string() + ": script must return a root element, e.g. `return ApplicationWindow { ... }`");
            ok = false;
        }
        if (!ok) {
            lua::close(L);
            setTheme(std::move(previous));
            return;
        }
        int overlays = lua::istable(L, -1) ? lua::ref(L) : (lua::pop(L), lua::NOREF);
        int tick = lua::ref(L);  // pops __tick
        int root = lua::ref(L);  // pops root

        // Native window properties come from the root element (ApplicationWindow).
        // Size is re-applied on hot reload only when the script changed it.
        {
            lua::rawgeti(L, lua::REGISTRYINDEX, root);
            luaui::Node n{L, -1};
            int w = static_cast<int>(n.number("width", 0)), h = static_cast<int>(n.number("height", 0));
            if (w > 0 && h > 0 && (w != appliedW_ || h != appliedH_)) {
                appliedW_ = w;
                appliedH_ = h;
                // GLFW_SCALE_TO_MONITOR already scales window sizes on Windows.
                window_.setSize(w, h);
            }
            std::string title = n.string("title", "");
            if (!title.empty()) window_.setTitle(title);
            lua::pop(L);
        }

        closeState(L_);
        L_ = L;
        rootRef_ = root;
        tickRef_ = tick;
        overlayRef_ = overlays;
        std::fprintf(stderr, "[imlui] loaded %s\n", cfg_.script.generic_string().c_str());
    }

    // ------------------------------------------------------------ imlui.native
    static Engine& self(lua::State* L) {
        lua::getfield(L, lua::REGISTRYINDEX, "imlui.engine");
        auto* e = static_cast<Engine*>(lua::touserdata(L, -1));
        lua::pop(L);
        return *e;
    }

    static Color argColor(lua::State* L, int idx) {
        if (lua::type(L, idx) == lua::TSTRING)
            if (auto c = resolveColor(lua::tostring(L, idx))) return *c;
        if (lua::istable(L, idx))
            if (auto c = luaui::Node::colorFromTable(L, idx)) return *c;
        lua::error(L, "expected a color (hex string, role name or {r,g,b,a})");
    }

    static void registerNative(lua::State* L) {
        static constexpr struct { const char* name; lua::CFunction fn; } fns[] = {
            {"setTheme", [](lua::State* L) -> int {
                if (!lua::istable(L, 1)) lua::error(L, "setTheme expects a table");
                Theme t = luaui::themeFromTable(L, 1);
                luaui::pushScheme(L, t.scheme);
                setTheme(std::move(t));
                return 1;
            }},
            {"schemeFor", [](lua::State* L) -> int {
                if (!lua::istable(L, 1)) lua::error(L, "schemeFor expects a table");
                luaui::pushScheme(L, luaui::themeFromTable(L, 1).scheme);
                return 1;
            }},
            {"scheme", [](lua::State* L) -> int { luaui::pushScheme(L, theme().scheme); return 1; }},
            {"isDark", [](lua::State* L) -> int { lua::pushboolean(L, theme().dark); return 1; }},
            {"color", [](lua::State* L) -> int { lua::pushstring(L, toHex(argColor(L, 1))); return 1; }},
            {"tone", [](lua::State* L) -> int {
                Oklch o = toOklch(argColor(L, 1));
                TonalPalette p{o.h, o.C};
                lua::pushstring(L, toHex(p.tone(static_cast<float>(lua::tonumber(L, 2)))));
                return 1;
            }},
            {"mix", [](lua::State* L) -> int {
                lua::pushstring(L, toHex(mix(argColor(L, 1), argColor(L, 2), static_cast<float>(lua::tonumber(L, 3)))));
                return 1;
            }},
            {"alpha", [](lua::State* L) -> int {
                lua::pushstring(L, toHex(argColor(L, 1).withAlpha(static_cast<float>(lua::tonumber(L, 2)))));
                return 1;
            }},
            {"time", [](lua::State* L) -> int { lua::pushnumber(L, ImGui::GetTime()); return 1; }},
            {"deltaTime", [](lua::State* L) -> int { lua::pushnumber(L, ImGui::GetIO().DeltaTime); return 1; }},
            {"fps", [](lua::State* L) -> int { lua::pushnumber(L, ImGui::GetIO().Framerate); return 1; }},
            {"dpiScale", [](lua::State* L) -> int { lua::pushnumber(L, dpiScale()); return 1; }},
            {"textSize", [](lua::State* L) -> int {
                std::string_view s = lua::tostring(L, 1);
                TextStyle st = TextStyle::BodyLarge;
                if (lua::type(L, 2) == lua::TSTRING)
                    if (auto t = textStyleFromName(lua::tostring(L, 2))) st = *t;
                ImVec2 sz = font(st)->CalcTextSizeA(fontPixels(st), 3.4e38f, 0.0f, s.data(), s.data() + s.size());
                lua::pushnumber(L, sz.x / dpiScale());
                lua::pushnumber(L, sz.y / dpiScale());
                return 2;
            }},
            {"setTitle", [](lua::State* L) -> int { self(L).window().setTitle(std::string{lua::tostring(L, 1)}); return 0; }},
            {"quit", [](lua::State* L) -> int { self(L).requestQuit(); return 0; }},
            {"reload", [](lua::State* L) -> int { self(L).requestReload(); return 0; }},
            {"imguiVersion", [](lua::State* L) -> int { lua::pushstring(L, ImGui::GetVersion()); return 1; }},
            // injectMouse(x, y [, down]) — feeds ImGui a mouse event in window pixels (for UI tests).
            {"injectMouse", [](lua::State* L) -> int {
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(static_cast<float>(lua::tonumber(L, 1)), static_cast<float>(lua::tonumber(L, 2)));
                if (!lua::isnoneornil(L, 3)) io.AddMouseButtonEvent(0, lua::toboolean(L, 3));
                if (lua::isnumber(L, 4)) io.AddMouseWheelEvent(0.0f, static_cast<float>(lua::tonumber(L, 4)));
                return 0;
            }},
        };
        lua::getglobal(L, "package");
        lua::getfield(L, -1, "preload");
        lua::pushcfunction(L, [](lua::State* L) -> int {
            lua::createtable(L, 0, static_cast<int>(std::size(fns)));
            for (const auto& f : fns) {
                lua::pushcfunction(L, f.fn);
                lua::setfield(L, -2, f.name);
            }
            return 1;
        });
        lua::setfield(L, -2, "imlui.native");
        lua::pop(L, 2);
    }
};

} // namespace imlui
