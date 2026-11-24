// Engine configuration and the standard config for an app host.
module;

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

export module imlui.app.config;

import imlui.log;
import imlui.platform.window;
import imlui.resources;
import imlui.resources.directory_mount;
import imlui.resources.pak_mount;
import imlui.embedded.imlui;
import imlui.embedded.imlui_fonts;

export namespace imlui {

struct EngineConfig {
    /// Where every file comes from: `imlui:/` (lua/imlui.lua), `imlui:/fonts/` (Roboto, Material Icons and
    /// their codepoints) and `app:/` (the app's scripts and data). Must outlive the font atlas, which points
    /// into the font files; the Engine keeps it for its whole lifetime.
    Resources resources;
    std::string entry = "app:/main.lua"; ///< the main UI script, a full resource name
    platform::Window::Config window;
    bool hotReload = true; ///< reload when a Lua file in a mutable mount (a directory) changes
    /// When > 0, quit after this many rendered frames and exit with 1 if a script error was reported (smoke
    /// tests). defaultConfig() reads it from the environment variable IMLUI_EXIT_AFTER_FRAMES.
    int exitAfterFrames = 0;
};

/// Mounts every `*.pak` archive in `folder`, in file name order, each at the mount point stored in it. Archives
/// that can't be read are logged and skipped. Returns how many were mounted.
int mountPaks(Resources& resources, const std::filesystem::path& folder);

/// Standard config for an app host. Mounts, from the bottom of the stack (overridden) to the top (wins):
///
///  1. the embedded library files, `imlui:/` (lua/) and `imlui:/fonts/`, and the embedded app files when no script
///     is given on the command line (`mountApp`: a function imlui_add_resources generates, such as
///     `imlui::embedded::my_app::mount`);
///  2. every `*.pak` next to the executable (mountPaks());
///  3. folders on disk, where they exist: `imlui:/` on <root>/lua, `imlui:/fonts/` on fonts/ next to the
///     executable, `app:/` on the script's folder.
///
/// The root is `sourceDir` when given and present (dev builds: lua/ and the examples hot-reload from the source
/// tree; the environment variable IMLUI_DEV_PATHS=0 ignores it), otherwise the executable's folder. The script is
/// argv[1], or `defaultScript` relative to the root, and the entry is `app:/<script file name>`. A relative
/// argv[1] that doesn't exist relative to the current directory is looked up under the root, so
/// `imlui_app examples/counter/main.lua` works from anywhere. With everything embedded the executable runs on its
/// own, and files next to it still override.
[[nodiscard]] EngineConfig defaultConfig(int argc, char** argv, const char* sourceDir = nullptr,
                                         const char* defaultScript = "examples/gallery/main.lua",
                                         void (*mountApp)(Resources&) = nullptr);

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

/// The value of environment variable `name`, "" when unset.
std::string environment(const char* name) {
    // MSVC's CRT deprecates getenv in favor of _dupenv_s. It's read once at startup and nothing else changes the
    // environment, so the plain call is fine and stays portable.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const char* value = std::getenv(name);
#pragma clang diagnostic pop
    return value ? std::string{value} : std::string{};
}

bool isDirectory(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

std::string utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string{s.begin(), s.end()};
}

} // namespace

int mountPaks(Resources& resources, const fs::path& folder) {
    std::vector<fs::path> paks;
    std::error_code ec;
    for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->path().extension() == ".pak" && it->is_regular_file(ec)) paks.push_back(it->path());
    std::ranges::sort(paks);
    int mounted = 0;
    for (const auto& file : paks) {
        std::string error;
        auto pak = PakMount::open(file, &error);
        if (!pak) {
            log::warn("skipping {}", error);
            continue;
        }
        const std::string point = pak->mountPoint();
        if (resources.mount(point, std::move(pak))) ++mounted;
    }
    return mounted;
}

EngineConfig defaultConfig(int argc, char** argv, const char* sourceDir, const char* defaultScript,
                           void (*mountApp)(Resources&)) {
    const fs::path exeDir = fs::absolute(fs::path{argv[0]}).parent_path();
    const bool devPaths = sourceDir && *sourceDir && environment("IMLUI_DEV_PATHS") != "0" && isDirectory(sourceDir);
    const fs::path root = devPaths ? fs::path{sourceDir} : exeDir;
    const bool scriptArg = argc > 1;
    const fs::path script = scriptArg ? resolveScript(fs::path{argv[1]}, root) : root / defaultScript;

    EngineConfig cfg;
    Resources& res = cfg.resources;
    // Bottom of the stack: what the executable carries.
    embedded::imlui::mount(res);
    embedded::imlui_fonts::mount(res);
    if (mountApp && !scriptArg) mountApp(res);
    // Archives next to the executable override embedded files.
    mountPaks(res, exeDir);
    // Folders on disk override both: the source tree in dev builds (hot reload), loose files next to a release.
    if (isDirectory(root / "lua")) res.mount("imlui:/", std::make_shared<DirectoryMount>(root / "lua"));
    if (isDirectory(exeDir / "fonts")) res.mount("imlui:/fonts/", std::make_shared<DirectoryMount>(exeDir / "fonts"));
    // A script named on the command line is always mounted, so a missing one is reported where it was looked for.
    if (scriptArg || isDirectory(script.parent_path()))
        res.mount("app:/", std::make_shared<DirectoryMount>(script.parent_path()));

    cfg.entry = "app:/" + utf8(script.filename());
    if (const std::string frames = environment("IMLUI_EXIT_AFTER_FRAMES"); !frames.empty())
        cfg.exitAfterFrames = std::max(0, std::atoi(frames.c_str()));
    return cfg;
}

} // namespace imlui
