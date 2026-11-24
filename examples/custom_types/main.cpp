// imlui_custom_types: registering C++ types with imlui, QML style. `imlui_custom_types [script.lua]`
//
// Gauge (example.gauge) is an element type, used from Lua like a built-in: `Gauge { value = ... }`.
// SystemInfo (example.system_info) is a singleton, the Lua global `backend`. The UI is main.lua in this
// folder. Module-only translation unit (no #include). See docs/extending.md.
import imlui;
import example.gauge;
import example.system_info;

#ifndef IMLUI_SOURCE_DIR
#define IMLUI_SOURCE_DIR nullptr
#endif
// Set by CMakeLists.txt: this folder's main.lua, relative to the source tree (and to the binary's folder).
#ifndef IMLUI_EXAMPLE_SCRIPT
#define IMLUI_EXAMPLE_SCRIPT "examples/custom_types/main.lua"
#endif

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, IMLUI_SOURCE_DIR, IMLUI_EXAMPLE_SCRIPT)};
    engine.registerType<example::Gauge>("Gauge");

    example::SystemInfo backend; // declared after the engine, destroyed (thread joined) before it
    engine.registerSingleton("backend", backend);
    backend.start(engine);

    return engine.run();
}
