// Engine configuration and the standard config for an app host.
module;

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

export module imlui.app.config;

import imlui.platform.window;
import imlui.resources;
import imlui.resources.directory_mount;

export namespace imlui {

struct EngineConfig {
    /// Where every file comes from: `imlui:/` (lua/imlui.lua), `imlui:/fonts/` (Roboto, Material Icons and
    /// their codepoints) and `app:/` (the app's scripts and data). Must outlive the font atlas, which points
    /// into the font files; the Engine keeps it for its whole lifetime.
    Resources resources;
    std::string entry = "app:/main.lua"; ///< the main UI script, a full resource name
    platform::Window::Config window;
    bool hotReload = true; ///< reload when a Lua file in a mutable mount (a directory) changes
};

/// Standard config for an app host. The root is `sourceDir` if given (dev builds, so lua/ and the examples
/// hot-reload from the source tree), otherwise the executable's folder. Mounts `imlui:/` on <root>/lua,
/// `imlui:/fonts/` on fonts/ next to the executable and `app:/` on the script's folder; the entry is
/// `app:/<script file name>`. The script is argv[1], or `defaultScript` relative to the root. A relative argv[1]
/// that doesn't exist relative to the current directory is looked up under the root, so
/// `imlui_app examples/counter/main.lua` works from anywhere.
[[nodiscard]] EngineConfig defaultConfig(int argc, char** argv, const char* sourceDir = nullptr,
                                         const char* defaultScript = "examples/gallery/main.lua");

} // namespace imlui

module :private;

namespace imlui {

namespace fs = std::filesystem;

namespace {

fs::path resolveScript(const fs::path& arg, const fs::path& root) {
    std::error_code ec;
    if (arg.is_relative() && !fs::exists(arg, ec) && fs::exists(root / arg, ec)) return root / arg;
    return fs::absolute(arg);
}

} // namespace

EngineConfig defaultConfig(int argc, char** argv, const char* sourceDir, const char* defaultScript) {
    const fs::path exeDir = fs::absolute(fs::path{argv[0]}).parent_path();
    const fs::path root = (sourceDir && *sourceDir && fs::is_directory(sourceDir)) ? fs::path{sourceDir} : exeDir;
    const fs::path script = argc > 1 ? resolveScript(fs::path{argv[1]}, root) : root / defaultScript;
    EngineConfig cfg;
    cfg.resources.mount("imlui:/", std::make_shared<DirectoryMount>(root / "lua"));
    cfg.resources.mount("imlui:/fonts/", std::make_shared<DirectoryMount>(exeDir / "fonts"));
    cfg.resources.mount("app:/", std::make_shared<DirectoryMount>(script.parent_path()));
    const std::u8string file = script.filename().u8string();
    cfg.entry = "app:/" + std::string{file.begin(), file.end()};
    return cfg;
}

} // namespace imlui
