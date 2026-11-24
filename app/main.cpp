// imlui app host: `imlui_app [script.lua]`
//
// Module-only translation unit (no #include). All UI — including the window
// title and size — is described by the Lua script (ApplicationWindow).
// Defaults to examples/gallery/main.lua.
import imlui;

#ifndef IMLUI_SOURCE_DIR
#define IMLUI_SOURCE_DIR nullptr
#endif

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, IMLUI_SOURCE_DIR)};
    return engine.run();
}
