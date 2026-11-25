// imlui_bench: a headless benchmark of big UIs. Renders the stress scenarios of tests/bench/stress.lua (the gallery
// app's stress page) in a test::HeadlessFrame (real ImGui frames, no window or GPU) and prints, per scenario and
// count: the time to build the element tree, the first frame, and the steady-state time per frame with a value that
// every item reads changing every frame ("anim") and with nothing changing ("idle": a frame forced while the UI
// stands still). Built only when imlui is the top-level project; not a test (ctest doesn't run it). Measure with a
// Release build:
//
//   cmake --preset clang-release && cmake --build --preset clang-release --target imlui_bench
//   build/clang-release/bin/imlui_bench                          # every scenario at 1000 and 5000
//   build/clang-release/bin/imlui_bench controls --count 1000    # one scenario, one count
//
// Module-only translation unit (no #include).
import std;

import imgui;
import imlui.lua.errors;
import imlui.test.headless_frame;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Options {
    std::vector<std::string> scenarios;
    std::vector<int> counts{1000, 5000};
    int frames = 100;
    int warmup = 10;
    float width = 1180.0f;
    float height = 820.0f;
};

struct Series {
    double median = 0.0;
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
};

Series summarize(std::vector<double> times) {
    Series s;
    if (times.empty()) return s;
    std::ranges::sort(times);
    s.median = times[times.size() / 2];
    s.mean = std::accumulate(times.begin(), times.end(), 0.0) / static_cast<double>(times.size());
    s.min = times.front();
    s.max = times.back();
    return s;
}

struct Result {
    std::string scenario;
    int count = 0;
    double buildMs = 0.0;
    double firstMs = 0.0;
    Series anim;
    Series idle;
    int vertices = 0; ///< ImGui vertices in the last animated frame's draw data
    std::vector<std::string> errors;
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) throw std::runtime_error{"can't read " + path.string()};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// Renders `frames` frames of `root` and returns the time of each.
std::vector<double> timeFrames(imlui::test::HeadlessFrame& h, int frames) {
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        const auto start = Clock::now();
        h.renderFrame("root");
        times.push_back(msSince(start));
    }
    return times;
}

void check(imlui::test::HeadlessFrame& h, std::string_view code) {
    const std::string error = h.run(code);
    if (!error.empty()) throw std::runtime_error{std::string{code} + ": " + error};
}

Result runScenario(const std::string& script, const std::string& scenario, int count, const Options& options) {
    imlui::test::HeadlessFrame h{{.width = options.width, .height = options.height, .builtins = true}};
    check(h, script);
    Result r;
    r.scenario = scenario;
    r.count = count;

    auto start = Clock::now();
    check(h, std::format("root = stress.build('{}', {})", scenario, count));
    r.buildMs = msSince(start);
    start = Clock::now();
    h.renderFrame("root");
    r.firstMs = msSince(start);
    h.renderFrames("root", options.warmup); // the layout settles (sizes the first frame didn't know)

    // Idle: nothing changes, a frame is forced anyway (the cost of any redraw: a hover, a caret blink).
    check(h, "collectgarbage()");
    r.idle = summarize(timeFrames(h, options.frames));

    // Animated: the Timer bumps stress.s.tick every frame, and every item reads it through a binding.
    check(h, "stress.s.animate = true");
    h.renderFrames("root", 2);
    check(h, "collectgarbage(); benchTick = stress.s.tick");
    r.anim = summarize(timeFrames(h, options.frames));
    if (const ImDrawData* draw = ImGui::GetDrawData()) r.vertices = draw->TotalVtxCount;
    check(h, std::format("if stress.s.tick < benchTick + {} then error('the Timer did not animate: tick ' .. "
                         "stress.s.tick) end",
                         options.frames));

    r.errors = imlui::luaui::errors();
    return r;
}

[[noreturn]] void usage(std::string_view error) {
    if (!error.empty()) std::cerr << "imlui_bench: " << error << "\n";
    std::cerr << "usage: imlui_bench [scenario...] [--count n[,n...]] [--frames n] [--warmup n] [--size WxH]\n"
                 "scenarios: tiles controls listview table nested plots (default: all)\n";
    std::exit(2);
}

int parseInt(std::string_view text) {
    int v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (ec != std::errc{} || end != text.data() + text.size() || v < 0) usage(std::format("bad number '{}'", text));
    return v;
}

Options parseArgs(int argc, char** argv) {
    Options o;
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        const auto value = [&]() -> std::string_view {
            if (i + 1 >= args.size()) usage(std::format("{} needs a value", a));
            return args[++i];
        };
        if (a == "--count") {
            o.counts.clear();
            for (const auto part : std::views::split(value(), ','))
                o.counts.push_back(parseInt(std::string_view{part.begin(), part.end()}));
        } else if (a == "--frames") {
            o.frames = std::max(1, parseInt(value()));
        } else if (a == "--warmup") {
            o.warmup = parseInt(value());
        } else if (a == "--size") {
            const std::string_view v = value();
            const auto x = v.find('x');
            if (x == std::string_view::npos) usage("--size takes WxH");
            o.width = static_cast<float>(parseInt(v.substr(0, x)));
            o.height = static_cast<float>(parseInt(v.substr(x + 1)));
        } else if (a == "--help" || a == "-h") {
            usage("");
        } else if (a.starts_with("-")) {
            usage(std::format("unknown option {}", a));
        } else {
            o.scenarios.emplace_back(a);
        }
    }
    if (o.scenarios.empty()) o.scenarios = {"tiles", "controls", "listview", "table", "nested", "plots"};
    return o;
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parseArgs(argc, argv);
    std::string script;
    try {
        script = readFile(std::filesystem::path{IMLUI_BENCH_DIR} / "stress.lua");
    } catch (const std::exception& e) {
        std::cerr << "imlui_bench: " << e.what() << "\n";
        return 1;
    }

    std::cout << std::format("imlui_bench: {}x{} px, {} frames measured after {} to settle; times in ms\n\n",
                             options.width, options.height, options.frames, options.warmup);
    std::cout << std::format("{:<10} {:>6} {:>9} {:>9} {:>10} {:>10} {:>10} {:>10} {:>9}\n", "scenario", "count",
                             "build", "first", "anim med", "anim mean", "idle med", "idle mean", "vertices");
    int failures = 0;
    for (const std::string& scenario : options.scenarios) {
        for (const int count : options.counts) {
            try {
                const Result r = runScenario(script, scenario, count, options);
                std::cout << std::format(
                    "{:<10} {:>6} {:>9.1f} {:>9.1f} {:>10.2f} {:>10.2f} {:>10.2f} {:>10.2f} {:>9}\n", r.scenario,
                    r.count, r.buildMs, r.firstMs, r.anim.median, r.anim.mean, r.idle.median, r.idle.mean, r.vertices);
                for (const std::string& e : r.errors)
                    std::cout << "  script error: " << e << "\n";
                failures += r.errors.empty() ? 0 : 1;
            } catch (const std::exception& e) {
                std::cout << std::format("{:<10} {:>6}  failed: {}\n", scenario, count, e.what());
                ++failures;
            }
            std::cout.flush();
        }
    }
    return failures == 0 ? 0 : 1;
}
