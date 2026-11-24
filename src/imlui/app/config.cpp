// The definitions of imlui.app.config, except defaultConfig() (default_config.cpp). An implementation unit rather
// than the interface's private module fragment: a module with a private fragment can have no other unit
// ([module.private.frag]), and default_config.cpp is one, compiled into another target (see defaultConfigWith()).
// Module-only translation unit (headers only in the global module fragment).
module imlui.app.config;

import std;

import imlui.log;
import imlui.platform.process;
import imlui.resources;
import imlui.resources.directory_mount;
import imlui.resources.package;
import imlui.resources.pak_mount;

namespace imlui {

namespace fs = std::filesystem;

namespace {

/// `arg` relative to the current folder, or to `exeDir` when it only exists there.
fs::path resolveScript(const fs::path& arg, const fs::path& exeDir) {
    std::error_code ec;
    if (arg.is_relative() && !fs::exists(arg, ec) && fs::exists(exeDir / arg, ec)) return exeDir / arg;
    return fs::absolute(arg, ec);
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

fs::path pathFromUtf8(std::string_view s) { return fs::path{std::u8string{s.begin(), s.end()}}; }

/// The prefix of mount point `point` (`app` for `app:/x/`).
std::string_view prefixOf(std::string_view point) { return point.substr(0, point.find(':')); }

} // namespace

int mountPaks(Resources& resources, const fs::path& folder, std::string_view skipPrefix) {
    std::vector<fs::path> paks;
    std::error_code ec;
    for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->path().extension() == ".pak" && it->is_regular_file(ec)) paks.push_back(it->path());
    std::ranges::sort(paks);
    int mounted = 0;
    for (const auto& file : paks) {
        std::string error;
        auto pak = PakMount::open(file, &error); // never throws: a junk or huge file is an error like any other
        if (!pak) {
            log::warn("skipping {}", error);
            continue;
        }
        const std::string point = pak->mountPoint();
        if (!skipPrefix.empty() && prefixOf(point) == skipPrefix) continue;
        if (resources.mount(point, std::move(pak))) ++mounted;
    }
    return mounted;
}

EngineConfig defaultConfigWith(int argc, char** argv, std::vector<ResourcePackage> library,
                               std::vector<ResourcePackage> app) {
    // The executable's folder from the OS when argv is the process's own (a bare name found through PATH or a path
    // relative to another folder would point elsewhere), else from argv[0] (a host or a test passing its own list).
    const platform::ProcessArguments args = platform::processArguments(argc, argv);
    fs::path exe = args.args.empty() ? fs::path{} : args.args[0];
    if (args.ofThisProcess)
        if (auto real = platform::executablePath()) exe = std::move(*real);
    std::error_code ec;
    const fs::path exeDir = fs::absolute(exe, ec).parent_path();
    const bool scriptArg = args.args.size() > 1;
    // A script on the command line is the app: nothing else is mounted at app:/.
    const std::string_view replaced = scriptArg ? "app" : "";

    std::vector<ResourcePackage> packages = std::move(library);
    for (ResourcePackage& p : app)
        if (prefixOf(p.mountPoint) != replaced) packages.push_back(std::move(p));
    std::vector<std::string> prefixes{"app"};
    for (const ResourcePackage& p : packages)
        if (std::ranges::find(prefixes, prefixOf(p.mountPoint)) == prefixes.end())
            prefixes.emplace_back(prefixOf(p.mountPoint));

    EngineConfig cfg;
    Resources& res = cfg.resources;
    // Bottom of the stack: what the executable carries (nothing for a package that isn't embedded).
    for (const ResourcePackage& p : packages)
        if (p.files) res.mount(p.mountPoint, p.files);
    // Archives next to the executable override embedded files.
    mountPaks(res, exeDir, replaced);
    // Loose files next to the executable override both: <exe folder>/<prefix>/ at <prefix>:/.
    for (const std::string& prefix : prefixes)
        if (prefix != replaced && isDirectory(exeDir / prefix))
            res.mount(prefix + ":/", std::make_shared<DirectoryMount>(exeDir / prefix));
    // Dev builds: the folders the files were embedded from, on top, so editing them hot-reloads. A package that
    // isn't embedded (IMLUI_EMBED_RESOURCES=OFF) has only its folder, which is then always mounted.
    const bool devPaths = environment("IMLUI_DEV_PATHS") != "0";
    for (const ResourcePackage& p : packages) {
        if (p.files && (!devPaths || p.sourceDir.empty())) continue;
        const fs::path dir = pathFromUtf8(p.sourceDir);
        // Only what the manifest selects, as embedded: a file the build wouldn't ship fails in development too.
        if (isDirectory(dir))
            res.mount(p.mountPoint, std::make_shared<DirectoryMount>(dir, p.selection));
        else if (!p.files)
            log::error("the files of {} aren't embedded in this build (IMLUI_EMBED_RESOURCES=OFF) and their source "
                       "folder '{}' doesn't exist",
                       p.mountPoint, p.sourceDir);
    }
    // A script named on the command line is always mounted, so a missing one is reported where it was looked for.
    if (scriptArg) {
        const fs::path script = resolveScript(args.args[1], exeDir); // UTF-16 on Windows: any file name works
        res.mount("app:/", std::make_shared<DirectoryMount>(script.parent_path()));
        cfg.entry = "app:/" + utf8(script.filename());
    }

    if (const std::string frames = environment("IMLUI_EXIT_AFTER_FRAMES"); !frames.empty())
        cfg.exitAfterFrames = std::max(0, std::atoi(frames.c_str()));
    return cfg;
}

} // namespace imlui
