// imlui_smoke_host: the test app host. Built only with the tests (tests/CMakeLists.txt), never shipped.
//
// It's the smallest real app: the smoke app (tests/data/smoke_app, embedded as module imlui.embedded.smoke_app from
// its resources.lua) and one registered C++ type, so the smoke CTests run an executable that carries everything it
// needs. Module-only translation unit (no #include).
//
// For smoke.modes (tests/smoke_modes.cmake), which runs it from the source folders, the embedded copy and a .pak and
// compares the runs:
//  * after the run it prints `summary:` lines: every file under app:/ with its size and hash, the folders list()
//    shows, and where the layout probe (`Probe`, the last element of the app's column) was rendered;
//  * IMLUI_SMOKE_NO_EMBEDDED_APP=1 (a test-only switch of this host, not of the library) leaves the embedded app out,
//    so app:/ comes only from the archives next to the executable.
import std;
import imgui;
import imlui;
import imlui.embedded.smoke_app;

namespace {

/// Where the probe was last rendered: the cursor in its window, in dp, and the draw commands of that window so far
/// (texts, the icon, the image). Both follow from the fonts, texts and image the app got from its resources.
struct ProbeReading {
    float x = 0.0f;
    float y = 0.0f;
    int drawCommands = 0;
    int renders = 0;
};
ProbeReading probeReading;

class LayoutProbe final : public imlui::Element {
public:
    static void declare(imlui::TypeBuilder<LayoutProbe>&) {}

    void render(imlui::Frame& frame) override {
        const ImVec2 cursor = ImGui::GetCursorPos();
        const float dp = frame.dp(1.0f);
        probeReading = {cursor.x / dp, cursor.y / dp, ImGui::GetWindowDrawList()->CmdBuffer.Size,
                        probeReading.renders + 1};
        ImGui::Dummy({frame.dp(10), frame.dp(10)});
    }
};

/// The value of environment variable `name`, "" when unset.
std::string environment(const char* name) {
    // MSVC's CRT deprecates getenv in favor of _dupenv_s; read once at startup, so the plain call is fine.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const char* value = std::getenv(name);
#pragma clang diagnostic pop
    return value ? std::string{value} : std::string{};
}

/// 64-bit FNV-1a: enough to tell two copies of a file apart.
std::uint64_t hash(std::span<const std::byte> bytes) {
    std::uint64_t h = 14695981039346656037ull;
    for (const std::byte b : bytes) {
        h ^= static_cast<std::uint64_t>(b);
        h *= 1099511628211ull;
    }
    return h;
}

/// What the run saw of app:/, independent of where it came from (no origins, no times).
void printSummary(const imlui::Resources& res) {
    std::vector<std::string> folders{"app:/"};
    for (std::size_t i = 0; i < folders.size(); ++i) {
        std::string line = "summary: list " + folders[i] + " ->";
        for (const std::string& entry : res.list(folders[i])) {
            line += " " + entry;
            if (entry.ends_with('/')) folders.push_back(folders[i] + entry);
        }
        std::cout << line << '\n';
    }
    for (const std::string& name : res.listRecursive("app:/")) {
        const auto data = res.read(name);
        if (!data) {
            std::cout << std::format("summary: file {} listed but unreadable\n", name);
            continue;
        }
        std::cout << std::format("summary: file {} {} {:016x}\n", name, data->size(), hash(data->bytes()));
    }
    std::cout << std::format("summary: probe at {:.1f},{:.1f} dp after {} draw commands, rendered: {}\n",
                             probeReading.x, probeReading.y, probeReading.drawCommands, probeReading.renders > 0)
              << std::flush;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<imlui::ResourcePackage> app;
    if (environment("IMLUI_SMOKE_NO_EMBEDDED_APP") != "1") app = imlui::embedded::smoke_app::packages();
    imlui::Engine engine{imlui::defaultConfig(argc, argv, std::move(app))};
    engine.registerType<LayoutProbe>("Probe");
    const int result = engine.run();
    printSummary(engine.resources());
    return result;
}
