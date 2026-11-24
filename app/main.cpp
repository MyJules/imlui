// imlui app host: `imlui_app [script.lua]`
//
// Module-only translation unit (no #include). All UI — including the window
// title and size — is described by the Lua script (ApplicationWindow).
// The app is the gallery (examples/gallery, embedded as app:/ by imlui_add_resources
// in CMakeLists.txt), so the binary runs on its own; a script argument replaces it.
import imlui;
import imlui.embedded.gallery;

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, {imlui::embedded::gallery::package()})};
    return engine.run();
}
