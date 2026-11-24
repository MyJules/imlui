// defaultConfig() (declared in imlui.app.config): defaultConfigWith() with the library's own files. An implementation
// unit of imlui.app.config compiled into the target that holds the generated module imlui.embedded.imlui (`imlui` in
// the build tree, imlui_files_install for an install: src/CMakeLists.txt), not into imlui_core. So imlui_core never
// refers to the library's files: one compiled imlui_core serves both variants, and an app's link line (imlui, then
// imlui_core) has no reference back. Module-only translation unit (headers only in the global module fragment).
module imlui.app.config;

import std;

import imlui.embedded.imlui;
import imlui.resources.package;

namespace imlui {

EngineConfig defaultConfig(int argc, char** argv, std::vector<ResourcePackage> app) {
    return defaultConfigWith(argc, argv, embedded::imlui::packages(), std::move(app));
}

EngineConfig defaultConfig(int argc, char** argv, std::initializer_list<std::vector<ResourcePackage>> apps) {
    std::vector<ResourcePackage> all;
    for (const auto& packages : apps)
        all.insert(all.end(), packages.begin(), packages.end());
    return defaultConfigWith(argc, argv, embedded::imlui::packages(), std::move(all));
}

} // namespace imlui
