// The package tests' app host (tests/package/consumer): the smallest app, with its files embedded as
// imlui.embedded.consumer. Module-only translation unit (no #include).
import imlui;
import imlui.embedded.consumer;

int main(int argc, char** argv) {
    return imlui::Engine{imlui::defaultConfig(argc, argv, imlui::embedded::consumer::packages())}.run();
}
