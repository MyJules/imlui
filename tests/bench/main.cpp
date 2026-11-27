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
//   build/clang-release/bin/imlui_bench model luarows --rows 100000
//
// The list-model scenarios (model, modelkey, luarows, luarowskey) show one ListView of `--rows` rows: a VectorModel
// without and with keys, or a Lua list of row tables without and with a `key` function. They print the first frame,
// the steady frames, the frame after one visible row changed and the frame after a row was inserted at the top, with
// the number of delegates each of those built. modelend is `model` scrolled to the end, modelsparse the same with a
// delegate only for every 10 000th row (nil for the others).
//
// drop (not in the default list) shows the process's private bytes after a big page (controls at --count) was left
// and collected, before and after lua::trimMemory: one count per process, since an earlier one's bytes stay.
//
//   build/clang-release/bin/imlui_bench drop --count 10000
//
// Module-only translation unit (no #include).
import std;

import imgui;
import lua;
import imlui.lua.errors;
import imlui.platform.memory;
import imlui.types.lua_convert;
import imlui.types.script_object;
import imlui.types.type_builder;
import imlui.types.vector_model;
import imlui.ui.layout_cache;
import imlui.test.headless_frame;

namespace bench {

/// A row of the list-model scenarios.
struct Row {
    std::int64_t id = 0;
    std::string text;
    std::string detail;
    bool operator==(const Row&) const = default;
};

} // namespace bench

template <>
struct imlui::LuaFields<bench::Row> {
    static constexpr std::string_view name = "BenchRow";
    static constexpr std::tuple fields{imlui::field("id", &bench::Row::id), imlui::field("text", &bench::Row::text),
                                       imlui::field("detail", &bench::Row::detail)};
};

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Options {
    std::vector<std::string> scenarios;
    std::vector<int> counts{1000, 5000};
    int rows = 100'000; ///< the list-model scenarios' row count
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
    /// Presented frames while the window's width changes on every frame (a live resize drag), each rendered again
    /// while its layout was stale, up to Engine::resizePasses (3) passes, as the Engine does after a size change.
    Series resize;
    int resizeStale = 0;       ///< of those, the frames whose first pass was stale (a frame late without the passes)
    double resizePasses = 0.0; ///< passes per presented frame
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

    // Resize: nothing changes but the window's width, a few pixels on every frame (a live resize drag).
    {
        std::vector<double> times;
        times.reserve(static_cast<std::size_t>(options.frames));
        for (int i = 0; i < options.frames; ++i) {
            const float step = static_cast<float>(i % 40 < 20 ? i % 20 : 20 - i % 20);
            h.setSize(options.width - 6.0f * step - 3.0f, options.height);
            const auto t = Clock::now();
            for (int pass = 1;; ++pass) {
                const std::uint64_t stale = imlui::layout::staleCount();
                h.renderFrame("root");
                r.resizePasses += 1.0;
                const bool again = imlui::layout::staleCount() != stale;
                if (again && pass == 1) ++r.resizeStale;
                if (!again || pass >= 3) break;
            }
            times.push_back(msSince(t));
        }
        r.resizePasses /= static_cast<double>(options.frames);
        h.setSize(options.width, options.height);
        h.renderFrames("root", options.warmup);
        r.resize = summarize(std::move(times));
    }

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

// ------------------------------------------------------------ peak and drop

/// The `drop` scenario: the process's memory while a big page is shown and after it was left. Builds `controls` at
/// `count` (eight elements a row), renders it, then replaces it by an empty Column (leaving the page), collects the
/// garbage and renders again, then gives the Lua pool's empty chunks back as the Engine does at the start of a frame
/// (lua::trimMemory).
struct DropResult {
    int count = 0;
    double freshMb = 0.0;    ///< private MiB before the page (the harness and an empty root)
    double peakMb = 0.0;     ///< private MiB while the page is shown
    double dropMb = 0.0;     ///< private MiB after it was left and the garbage collected
    double trimMb = 0.0;     ///< private MiB after lua::trimMemory
    double reservedMb = 0.0; ///< the Lua pool's reserved MiB after the trim
    double luaPeakMb = 0.0;
    double luaDropMb = 0.0;
    std::vector<std::string> errors;
};

double privateMb() { return static_cast<double>(imlui::platform::privateBytes().value_or(0)) / (1024.0 * 1024.0); }

double luaMb(imlui::test::HeadlessFrame& h) {
    check(h, "_G.benchKb = collectgarbage('count')");
    lua::getglobal(h.state(), "benchKb"); // the globals (_G)
    const double kb = lua::tonumber(h.state(), -1);
    lua::pop(h.state());
    return kb / 1024.0;
}

DropResult runDropScenario(const std::string& script, int count, const Options& options) {
    imlui::test::HeadlessFrame h{{.width = options.width, .height = options.height, .builtins = true}};
    check(h, script);
    DropResult r;
    r.count = count;
    check(h, "root = Column {}");
    h.renderFrames("root", options.warmup);
    check(h, "collectgarbage()");
    r.freshMb = privateMb();

    check(h, std::format("root = stress.build('controls', {})", count));
    h.renderFrames("root", options.warmup);
    check(h, "collectgarbage()");
    r.peakMb = privateMb();
    r.luaPeakMb = luaMb(h);

    check(h, "root = Column {}; collectgarbage(); collectgarbage()");
    h.renderFrames("root", options.warmup);
    check(h, "collectgarbage()");
    h.renderFrame("root");
    r.dropMb = privateMb();
    r.luaDropMb = luaMb(h);
    lua::trimMemory(h.state());
    r.trimMb = privateMb();
    r.reservedMb = static_cast<double>(lua::memoryStats(h.state()).reserved) / (1024.0 * 1024.0);
    r.errors = imlui::luaui::errors();
    return r;
}

// ------------------------------------------------------------ list-model scenarios

bool isModelScenario(std::string_view name) {
    return name == "model" || name == "modelkey" || name == "luarows" || name == "luarowskey" ||
           name == "modelend" || name == "modelsparse";
}

/// The models of the list-model scenarios, a singleton: `models.plain` (no keys) and `models.keyed` (by id).
/// `models:built()` counts the delegates built.
class BenchModels final : public imlui::ScriptObject {
public:
    static void declare(imlui::TypeBuilder<BenchModels>& type) {
        type.doc("The bench's list models.")
            .property("plain", &BenchModels::plain)
            .doc("Rows without keys.")
            .property("keyed", &BenchModels::keyed)
            .doc("Rows keyed by id.")
            .method("built", &BenchModels::built)
            .doc("Counts a delegate built.");
    }
    [[nodiscard]] const imlui::VectorModel<bench::Row>& plain() const { return plainRows; }
    [[nodiscard]] const imlui::VectorModel<bench::Row>& keyed() const { return keyedRows; }
    void built() { ++builtCount; }

    imlui::VectorModel<bench::Row> plainRows;
    imlui::VectorModel<bench::Row> keyedRows{[](const bench::Row& r) { return r.id; }};
    int builtCount = 0;
};

bench::Row benchRow(std::int64_t id, std::string_view what = "line") {
    return {id, std::format("{} {}", what, id), std::format("Second line of row {}", id)};
}

struct ModelResult {
    std::string scenario;
    int rows = 0;
    double buildMs = 0.0;
    double firstMs = 0.0;
    Series steady;
    Series change; ///< the frame after one visible row changed
    double changeBuilt = 0.0;
    Series insert; ///< the frame after a row was inserted at the top
    double insertBuilt = 0.0;
    std::vector<std::string> errors;
};

ModelResult runModelScenario(const std::string& scenario, const Options& options) {
    BenchModels models; // outlives the harness
    imlui::test::HeadlessFrame h{{.width = options.width, .height = options.height, .builtins = true}};
    h.types().registerSingleton("models", models);
    ModelResult r;
    r.scenario = scenario;
    r.rows = options.rows;
    // modelend: `model` scrolled to the end, where a list that adds the items' heights up from the top pays for
    // every row above the view. modelsparse: the same, with a delegate only for every 10 000th row (nil for the
    // others, which take no space), so most of the list is long runs of rows without an item.
    const bool atEnd = scenario == "modelend" || scenario == "modelsparse";
    const bool sparse = scenario == "modelsparse";
    const bool isModel = scenario == "model" || scenario == "modelkey" || atEnd;
    imlui::VectorModel<bench::Row>& model = scenario == "modelkey" ? models.keyedRows : models.plainRows;

    auto start = Clock::now();
    if (isModel) {
        std::vector<bench::Row> rows;
        rows.reserve(static_cast<std::size_t>(options.rows));
        for (int i = 1; i <= options.rows; ++i)
            rows.push_back(benchRow(i));
        model.appendRows(std::move(rows));
    } else {
        check(h, std::format("rows = {{}}; for i = 1, {} do rows[i] = {{ id = i, text = 'line ' .. i, "
                             "detail = 'Second line of row ' .. i }} end",
                             options.rows));
    }
    const std::string_view modelExpr = scenario == "modelkey" ? "models.keyed"
                                       : isModel                ? "models.plain"
                                                                : "function() return rows end";
    const std::string_view key = scenario == "luarowskey" ? "key = function(r) return r.id end," : "";
    const std::string_view skip = sparse ? "if r.id % 10000 ~= 0 then return nil end" : "";
    check(h, std::format(R"(root = ListView {{ height = 700, spacing = 0, model = {}, {}
            delegate = function(r)
                {}
                models:built()
                return ListItem {{ r.text, supportingText = r.detail, icon = "description" }}
            end }})",
                         modelExpr, key, skip));
    r.buildMs = msSince(start);
    start = Clock::now();
    h.renderFrame("root");
    r.firstMs = msSince(start);
    if (atEnd) check(h, "root:positionViewAtEnd()");
    h.renderFrames("root", options.warmup);
    check(h, "collectgarbage()");
    r.steady = summarize(timeFrames(h, options.frames));

    // One visible row changes (row 3), then rows are inserted at the top, one before each timed frame.
    const int repeats = std::max(5, options.frames / 5);
    const auto timed = [&](const std::function<void(int)>& edit, double& builtPerEdit) {
        std::vector<double> times;
        const int builtBefore = models.builtCount;
        for (int k = 0; k < repeats; ++k) {
            edit(k);
            const auto t = Clock::now();
            h.renderFrame("root");
            times.push_back(msSince(t));
        }
        builtPerEdit = static_cast<double>(models.builtCount - builtBefore) / repeats;
        return summarize(std::move(times));
    };
    r.change = timed(
        [&](int k) {
            if (isModel)
                model.set(2, benchRow(model.at(2).id, std::format("changed {}", k)));
            else
                check(h,
                      std::format("rows[3] = {{ id = rows[3].id, text = 'changed {}', detail = rows[3].detail }}", k));
        },
        r.changeBuilt);
    r.insert = timed(
        [&](int k) {
            const std::int64_t id = -1 - k;
            if (isModel)
                model.insert(0, benchRow(id, "inserted"));
            else
                check(h, std::format("table.insert(rows, 1, {{ id = {0}, text = 'inserted {0}', "
                                     "detail = 'Second line of row {0}' }})",
                                     id));
        },
        r.insertBuilt);
    r.errors = imlui::luaui::errors();
    return r;
}

[[noreturn]] void usage(std::string_view error) {
    if (!error.empty()) std::cerr << "imlui_bench: " << error << "\n";
    std::cerr << "usage: imlui_bench [scenario...] [--count n[,n...]] [--rows n] [--frames n] [--warmup n] "
                 "[--size WxH]\n"
                 "scenarios: tiles controls listview table nested plots, and the list-model ones model modelkey "
                 "luarows luarowskey (--rows rows; default: all), and drop (the memory after leaving a big page; not "
                 "in the default list)\n";
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
        } else if (a == "--rows") {
            o.rows = parseInt(value());
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
    if (o.scenarios.empty())
        o.scenarios = {"tiles",   "controls", "listview",   "table",    "nested",     "plots",
                       "model",   "modelkey", "luarows",    "luarowskey", "modelend", "modelsparse"};
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
    std::cout << std::format("{:<10} {:>6} {:>9} {:>9} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>6} {:>6} {:>9}\n",
                             "scenario", "count", "build", "first", "anim med", "anim mean", "idle med", "idle mean",
                             "resize med", "resize max", "stale", "passes", "vertices");
    int failures = 0;
    for (const std::string& scenario : options.scenarios) {
        if (isModelScenario(scenario) || scenario == "drop") continue;
        for (const int count : options.counts) {
            try {
                const Result r = runScenario(script, scenario, count, options);
                std::cout << std::format(
                    "{:<10} {:>6} {:>9.1f} {:>9.1f} {:>10.2f} {:>10.2f} {:>10.2f} {:>10.2f} {:>10.2f} {:>10.2f} {:>6} "
                    "{:>6.2f} {:>9}\n",
                    r.scenario, r.count, r.buildMs, r.firstMs, r.anim.median, r.anim.mean, r.idle.median, r.idle.mean,
                    r.resize.median, r.resize.max, r.resizeStale, r.resizePasses, r.vertices);
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

    // The list-model scenarios: a table of their own.
    bool header = false;
    for (const std::string& scenario : options.scenarios) {
        if (!isModelScenario(scenario)) continue;
        if (!header) {
            std::cout << "\nListView of n rows (frame times: medians; built: delegates built per frame)\n";
            std::cout << std::format("{:<10} {:>7} {:>9} {:>9} {:>9} {:>10} {:>7} {:>10} {:>7}\n", "scenario", "rows",
                                     "build", "first", "steady", "change 1", "built", "insert 1", "built");
            header = true;
        }
        try {
            const ModelResult r = runModelScenario(scenario, options);
            std::cout << std::format("{:<10} {:>7} {:>9.1f} {:>9.1f} {:>9.3f} {:>10.3f} {:>7.1f} {:>10.3f} {:>7.1f}\n",
                                     r.scenario, r.rows, r.buildMs, r.firstMs, r.steady.median, r.change.median,
                                     r.changeBuilt, r.insert.median, r.insertBuilt);
            for (const std::string& e : r.errors)
                std::cout << "  script error: " << e << "\n";
            failures += r.errors.empty() ? 0 : 1;
        } catch (const std::exception& e) {
            std::cout << std::format("{:<10}  failed: {}\n", scenario, e.what());
            ++failures;
        }
        std::cout.flush();
    }

    // Peak and drop: a table of its own.
    if (std::ranges::find(options.scenarios, "drop") != options.scenarios.end()) {
        std::cout << "\nPeak and drop: controls at count n, left for an empty Column (private MiB; Lua heap MiB)\n";
        std::cout << std::format("{:>7} {:>9} {:>9} {:>9} {:>9} {:>9} {:>9} {:>9}\n", "count", "fresh", "peak",
                                 "dropped", "trimmed", "lua peak", "lua drop", "reserved");
        for (const int count : options.counts) {
            try {
                const DropResult r = runDropScenario(script, count, options);
                std::cout << std::format("{:>7} {:>9.1f} {:>9.1f} {:>9.1f} {:>9.1f} {:>9.1f} {:>9.1f} {:>9.1f}\n",
                                         r.count, r.freshMb, r.peakMb, r.dropMb, r.trimMb, r.luaPeakMb, r.luaDropMb,
                                         r.reservedMb);
                for (const std::string& e : r.errors)
                    std::cout << "  script error: " << e << "\n";
                failures += r.errors.empty() ? 0 : 1;
            } catch (const std::exception& e) {
                std::cout << std::format("drop {:>7}  failed: {}\n", count, e.what());
                ++failures;
            }
            std::cout.flush();
        }
    }
    return failures == 0 ? 0 : 1;
}
