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

/// Standard config for an app host. The root is `sourceDir` if given (dev builds, so lua/ and the examples
/// hot-reload from the source tree), otherwise the executable's folder. lua/ is taken from the root and
/// fonts/ from next to the executable. The script is argv[1], or `defaultScript` relative to the root. A
/// relative argv[1] that doesn't exist relative to the current directory is looked up under the root, so
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
    EngineConfig cfg;
    cfg.script = argc > 1 ? resolveScript(fs::path{argv[1]}, root) : root / defaultScript;
    cfg.luaPaths = {root / "lua"};
    cfg.fontDir = exeDir / "fonts";
    return cfg;
}

} // namespace imlui
