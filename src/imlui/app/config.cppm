// Engine configuration and the standard config for an app host.
module;

#include <filesystem>
#include <vector>

export module imlui.app.config;

import imlui.platform.window;

export namespace imlui {

struct EngineConfig {
    std::filesystem::path script;                ///< main UI script
    std::vector<std::filesystem::path> luaPaths; ///< dirs searched for imlui.lua & modules
    std::filesystem::path fontDir;               ///< Roboto + Material Icons
    platform::Window::Config window;
    bool hotReload = true;
};

/// Standard config for an app host: script from argv[1] (or
/// examples/gallery.lua), lua/ and fonts/ next to the executable. If
/// `sourceDir` is given (dev builds), lua/ and examples/ are taken from the
/// source tree so edits hot-reload without rebuilding.
[[nodiscard]] EngineConfig defaultConfig(int argc, char** argv, const char* sourceDir = nullptr);

} // namespace imlui

module :private;

namespace imlui {

namespace fs = std::filesystem;

EngineConfig defaultConfig(int argc, char** argv, const char* sourceDir) {
    const fs::path exeDir = fs::absolute(fs::path{argv[0]}).parent_path();
    const fs::path root = (sourceDir && *sourceDir && fs::is_directory(sourceDir)) ? fs::path{sourceDir} : exeDir;
    EngineConfig cfg;
    cfg.script = argc > 1 ? fs::absolute(fs::path{argv[1]}) : root / "examples" / "gallery.lua";
    cfg.luaPaths = {root / "lua"};
    cfg.fontDir = exeDir / "fonts";
    return cfg;
}

} // namespace imlui
