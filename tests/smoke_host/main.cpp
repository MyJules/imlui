// imlui_smoke_host: the test app host. Built only with the tests (tests/CMakeLists.txt), never shipped.
//
// It's the smallest real app: the smoke app (tests/data/smoke_app, embedded as module imlui.embedded.smoke_app from
// its resources.lua) and one registered C++ type, so the smoke CTests run an executable that carries everything it
// needs. Module-only translation unit (no #include).
import imlui;
import imlui.embedded.smoke_app;
import imlui.test.render_counter;

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, imlui::embedded::smoke_app::packages())};
    engine.registerType<imlui::test::RenderCounter>("Probe");
    return engine.run();
}
