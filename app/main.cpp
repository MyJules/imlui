// imlui app host: `imlui_app [script.lua]`
//
// Module-only translation unit (no #include). All UI — including the window
// title and size — is described by the Lua script (ApplicationWindow).
// Defaults to the gallery (examples/gallery/main.lua), which is embedded
// (imlui_add_resources in CMakeLists.txt), so the binary runs on its own.
import imlui;
import imlui.embedded.gallery;

#ifndef IMLUI_SOURCE_DIR
#define IMLUI_SOURCE_DIR nullptr
#endif

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, IMLUI_SOURCE_DIR, "examples/gallery/main.lua",
                                              imlui::embedded::gallery::mount)};
    return engine.run();
}
