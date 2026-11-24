// imlui_custom_types: registering C++ types with imlui, QML style. `imlui_custom_types [script.lua]`
//
// Gauge (example.gauge) is an element type, used from Lua like a built-in: `Gauge { value = ... }`.
// SystemInfo (example.system_info) is a singleton, the Lua global `backend`. The UI is main.lua in this
// folder, embedded into the binary (imlui.embedded.custom_types, from resources.lua), so it runs on its own.
// Module-only translation unit (no #include). See docs/extending.md.
import imlui;
import imlui.embedded.custom_types;
import example.gauge;
import example.system_info;

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, imlui::embedded::custom_types::packages())};
    engine.registerType<example::Gauge>("Gauge");

    example::SystemInfo backend; // declared after the engine, destroyed (thread joined) before it
    engine.registerSingleton("backend", backend);
    backend.start(engine);

    return engine.run();
}
