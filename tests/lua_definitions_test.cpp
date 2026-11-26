// Tests for the Lua language-server definitions (imlui.types.lua_types, imlui.types.lua_definitions): the LuaLS type
// expressions of LuaConvert types, the generated types.lua of a small registry (golden), that the files are valid Lua,
// that the hand-written prelude definitions (src/resources/definitions/imlui.lua) cover every `ui` member, the files
// writeLuaDefinitions() writes, and the `--lua-defs` / IMLUI_LUA_DEFS options of defaultConfig().
#include <gtest/gtest.h>

import std;

import lua;
import imlui.color;
import imlui.app.config;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.future;
import imlui.types.lua_convert;
import imlui.types.lua_definitions;
import imlui.types.lua_function;
import imlui.types.lua_types;
import imlui.types.registry;
import imlui.types.script_object;
import imlui.types.type_builder;
import imlui.ui.elements.builtin;
import imlui.test.headless_frame;
import imlui.test.scoped_env;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

enum class Quality { Low, High };
enum class Mode { Fast, Slow };

struct Vec2 {
    double x = 0, y = 0;
    bool operator==(const Vec2&) const = default;
};

struct Series {
    std::string label = "series";
    std::vector<Vec2> points;
    std::optional<double> max;
    Quality quality = Quality::High;
    bool operator==(const Series&) const = default;
};

} // namespace

template <>
struct imlui::EnumNames<Quality> {
    static constexpr std::string_view name = "Quality";
    static constexpr std::pair<Quality, std::string_view> values[] = {{Quality::Low, "low"}, {Quality::High, "high"}};
};
template <>
struct imlui::EnumNames<Mode> { // no name: written inline
    static constexpr std::pair<Mode, std::string_view> values[] = {{Mode::Fast, "fast"}, {Mode::Slow, "slow"}};
};
template <>
struct imlui::LuaFields<Vec2> { // no name: written inline
    static constexpr std::tuple fields{imlui::field("x", &Vec2::x), imlui::field("y", &Vec2::y)};
};
template <>
struct imlui::LuaFields<Series> {
    static constexpr std::string_view name = "Series";
    static constexpr std::tuple fields{imlui::field("label", &Series::label), imlui::field("points", &Series::points),
                                       imlui::field("max", &Series::max), imlui::field("quality", &Series::quality)};
};

namespace {

/// A LuaConvert of the app's own, without a luaType hook: its name decides.
struct Opaque {
    bool operator==(const Opaque&) const = default;
};
struct Tagged {
    bool operator==(const Tagged&) const = default;
};

} // namespace

template <>
struct imlui::LuaConvert<Opaque> {
    static constexpr std::string_view name = "{opaque}";
    static void push(lua::State* s, const Opaque&) { lua::pushnil(s); }
    static bool check(lua::State*, int) { return true; }
    static Opaque get(lua::State*, int) { return {}; }
};
template <>
struct imlui::LuaConvert<Tagged> {
    static constexpr std::string_view name = "tagged";
    static std::string luaType() { return "Tagged"; }
    static void push(lua::State* s, const Tagged&) { lua::pushnil(s); }
    static bool check(lua::State*, int) { return true; }
    static Tagged get(lua::State*, int) { return {}; }
};

namespace {

template <class T>
std::string luaType() {
    LuaTypes types;
    return luaTypeOf<T>(types);
}

class Gauge final : public Element {
public:
    static void declare(TypeBuilder<Gauge>& type) {
        type.property("value", &Gauge::value, &Gauge::setValue)
            .property("limit", &Gauge::limit, &Gauge::setLimit)
            .property("tags", &Gauge::tags, &Gauge::setTags)
            .property("range", &Gauge::range, &Gauge::setRange)
            .property("mode", &Gauge::mode, &Gauge::setMode)
            .property("series", &Gauge::series, &Gauge::setSeries)
            .property("either", &Gauge::either, &Gauge::setEither)
            .property("formatter", &Gauge::formatter, &Gauge::setFormatter)
            .property("tint", &Gauge::tint, &Gauge::setTint)
            .property("peak", &Gauge::peak)
            .signal("onPeak")
            .signal<double, const std::string&>("onMoved", {"to", "why"})
            .method("reset", &Gauge::reset)
            .method("setBounds", &Gauge::setBounds, {"min", "max"})
            .method("points", &Gauge::points)
            .method("forEach", &Gauge::forEach)
            .defaultProperty("value");
    }
    void render(Frame&) override {}

    [[nodiscard]] double value() const { return m_value; }
    void setValue(double v) { m_value = v; }
    [[nodiscard]] std::optional<int> limit() const { return m_limit; }
    void setLimit(std::optional<int> v) { m_limit = v; }
    [[nodiscard]] const std::map<std::string, double>& tags() const { return m_tags; }
    void setTags(std::map<std::string, double> v) { m_tags = std::move(v); }
    [[nodiscard]] std::pair<double, double> range() const { return m_range; }
    void setRange(std::pair<double, double> v) { m_range = v; }
    [[nodiscard]] Mode mode() const { return m_mode; }
    void setMode(Mode v) { m_mode = v; }
    [[nodiscard]] const Series& series() const { return m_series; }
    void setSeries(Series v) { m_series = std::move(v); }
    [[nodiscard]] const std::variant<int, std::string>& either() const { return m_either; }
    void setEither(std::variant<int, std::string> v) { m_either = std::move(v); }
    [[nodiscard]] const std::optional<LuaFunction>& formatter() const { return m_formatter; }
    void setFormatter(std::optional<LuaFunction> v) { m_formatter = std::move(v); }
    [[nodiscard]] Color tint() const { return m_tint; }
    void setTint(Color v) { m_tint = v; }
    [[nodiscard]] double peak() const { return m_value; }

    void reset() { m_value = 0; }
    void setBounds(double min, std::optional<double> max) { m_range = {min, max.value_or(min)}; }
    [[nodiscard]] std::vector<Vec2> points() const { return m_series.points; }
    int forEach(const LuaFunction& fn) { return fn.call(m_value) ? 1 : 0; }

private:
    double m_value = 0;
    std::optional<int> m_limit;
    std::map<std::string, double> m_tags;
    std::pair<double, double> m_range{0, 1};
    Mode m_mode = Mode::Fast;
    Series m_series;
    std::variant<int, std::string> m_either;
    std::optional<LuaFunction> m_formatter;
    Color m_tint;
};

/// An element with nothing declared.
class Blank final : public Element {
public:
    void render(Frame&) override {}
};

class Lab final : public ScriptObject {
public:
    static void declare(TypeBuilder<Lab>& type) {
        type.property("load", &Lab::load, &Lab::setLoad)
            .signal<int>("onTick", {"n"})
            .method("fetch", &Lab::fetch, {"url"})
            .method("idle", &Lab::idle)
            .method("opaque", &Lab::opaque)
            .method("tagged", &Lab::tagged);
    }
    [[nodiscard]] double load() const { return m_load; }
    void setLoad(double v) { m_load = v; }
    Future<Series> fetch(std::string) { return {}; }
    Future<void> idle() { return {}; }
    Opaque opaque(Opaque o) { return o; }
    Tagged tagged(Tagged t) { return t; }

private:
    double m_load = 0;
};

constexpr std::string_view goldenTypes = R"lua(---@meta _
-- LuaLS definitions of the C++ types registered with imlui: the element types (built-in and the app's own)
-- and the singletons. Generated (--lua-defs <dir> or IMLUI_LUA_DEFS=<dir>); don't edit it, generate it again.
-- The DSL (state, bind, Element, Future, ...) is in imlui.lua next to this file.

-- skipped: element type "not-a-name" (not a Lua name)

-- Value types

---@alias Quality "low"|"high"

---@class Series
---@field label? string
---@field points? { x: number, y: number }[]
---@field max? number
---@field quality? Quality

-- Blank

---@class BlankProps: ElementProps

---@class Blank: Element

---@param props? BlankProps
---@return Blank
function Blank(props) end
ui.Blank = Blank

-- Gauge

---@alias GaugeSignal "onPeak"|"onMoved"|"onValueChanged"|"onLimitChanged"|"onTagsChanged"|"onRangeChanged"|"onModeChanged"|"onSeriesChanged"|"onEitherChanged"|"onFormatterChanged"|"onTintChanged"|"onPeakChanged"

---@class GaugeProps: ElementProps
---@field value? number|(fun(self: Gauge): number?)|Binding
---@field limit? integer|(fun(self: Gauge): integer?)|Binding
---@field tags? table<string, number>|(fun(self: Gauge): table<string, number>?)|Binding
---@field range? [number, number]|(fun(self: Gauge): [number, number]?)|Binding
---@field mode? "fast"|"slow"|(fun(self: Gauge): ("fast"|"slow")?)|Binding
---@field series? Series|(fun(self: Gauge): Series?)|Binding
---@field either? integer|string|(fun(self: Gauge): (integer|string)?)|Binding
---@field formatter? function|Binding
---@field tint? Color|(fun(self: Gauge): Color?)|Binding
---@field onPeak? fun(self: Gauge, ...: any)
---@field onMoved? fun(self: Gauge, to: number, why: string)
---@field onValueChanged? fun(self: Gauge, value: number)
---@field onLimitChanged? fun(self: Gauge, value?: integer)
---@field onTagsChanged? fun(self: Gauge, value: table<string, number>)
---@field onRangeChanged? fun(self: Gauge, value: [number, number])
---@field onModeChanged? fun(self: Gauge, value: "fast"|"slow")
---@field onSeriesChanged? fun(self: Gauge, value: Series)
---@field onEitherChanged? fun(self: Gauge, value: integer|string)
---@field onFormatterChanged? fun(self: Gauge, value?: function)
---@field onTintChanged? fun(self: Gauge, value: Color)
---@field onPeakChanged? fun(self: Gauge, value: number)

---@class Gauge: Element
---@field value number
---@field limit integer?
---@field tags table<string, number>
---@field range [number, number]
---@field mode "fast"|"slow"
---@field series Series
---@field either integer|string
---@field formatter function?
---@field tint Color
---@field peak number
---@field onPeak? fun(self: Gauge, ...: any)
---@field onMoved? fun(self: Gauge, to: number, why: string)
---@field onValueChanged? fun(self: Gauge, value: number)
---@field onLimitChanged? fun(self: Gauge, value?: integer)
---@field onTagsChanged? fun(self: Gauge, value: table<string, number>)
---@field onRangeChanged? fun(self: Gauge, value: [number, number])
---@field onModeChanged? fun(self: Gauge, value: "fast"|"slow")
---@field onSeriesChanged? fun(self: Gauge, value: Series)
---@field onEitherChanged? fun(self: Gauge, value: integer|string)
---@field onFormatterChanged? fun(self: Gauge, value?: function)
---@field onTintChanged? fun(self: Gauge, value: Color)
---@field onPeakChanged? fun(self: Gauge, value: number)
---@field reset fun(self: Gauge)
---@field setBounds fun(self: Gauge, min: number, max?: number)
---@field points fun(self: Gauge): { x: number, y: number }[]
---@field forEach fun(self: Gauge, arg1: function): integer

---@param props? GaugeProps|number
---@return Gauge
function Gauge(props) end
ui.Gauge = Gauge

-- lab (singleton)

---@alias labSignal "onTick"|"onLoadChanged"

---@class lab
---@field load number
---@field onTick? fun(n: integer)
---@field onLoadChanged? fun(value: number)
---@field fetch fun(self: lab, url: string): Future<Series>
---@field idle fun(self: lab): Future<nil>
---@field opaque fun(self: lab, arg1: any): any
---@field tagged fun(self: lab, arg1: Tagged): Tagged
lab = {}

-- connect

---Every signal name of the registered types and singletons (`connect` raises for any other).
---@alias ImluiSignal GaugeSignal|labSignal

---Adds a listener to a signal of a C++ element or singleton: `fn` runs whenever `target` emits `signal`, after its handler.
---@param target Element|table
---@param signal ImluiSignal
---@param fn function
---@return fun() disconnect
---@overload fun(target: Gauge, signal: GaugeSignal, fn: fun(self: Gauge, ...: any)): fun()
---@overload fun(target: lab, signal: labSignal, fn: fun(...: any)): fun()
function connect(target, signal, fn) end
ui.connect = connect
)lua";

/// Whether `code` compiles as a Lua chunk; the error otherwise.
std::string compileError(std::string_view code, const char* name) {
    lua::State* state = lua::newstate();
    const int status = lua::loadbuffer(state, code, name);
    std::string error = status == 0 ? "" : std::string{lua::tostring(state, -1)};
    lua::close(state);
    return error;
}

std::string readSource(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST(LuaTypes, ExpressionsOfTheBuiltInConversions) {
    EXPECT_EQ(luaType<bool>(), "boolean");
    EXPECT_EQ(luaType<int>(), "integer");
    EXPECT_EQ(luaType<std::uint8_t>(), "integer");
    EXPECT_EQ(luaType<float>(), "number");
    EXPECT_EQ(luaType<std::string>(), "string");
    EXPECT_EQ(luaType<std::string_view>(), "string");
    EXPECT_EQ(luaType<const char*>(), "string");
    EXPECT_EQ(luaType<Color>(), "Color");
    EXPECT_EQ(luaType<std::optional<double>>(), "number?");
    EXPECT_EQ((luaType<std::optional<std::variant<int, std::string>>>()), "(integer|string)?");
    EXPECT_EQ(luaType<std::vector<int>>(), "integer[]");
    EXPECT_EQ(luaType<std::vector<std::optional<int>>>(), "integer?[]");
    EXPECT_EQ((luaType<std::vector<std::variant<int, bool>>>()), "(integer|boolean)[]");
    EXPECT_EQ((luaType<std::map<std::string, double>>()), "table<string, number>");
    EXPECT_EQ((luaType<std::unordered_map<std::string, std::vector<int>>>()), "table<string, integer[]>");
    EXPECT_EQ((luaType<std::pair<int, std::string>>()), "[integer, string]");
    EXPECT_EQ((luaType<std::tuple<int, double, bool>>()), "[integer, number, boolean]");
    EXPECT_EQ((luaType<std::variant<std::monostate, int>>()), "nil|integer");
    EXPECT_EQ(luaType<LuaFunction>(), "function");
    EXPECT_EQ(luaType<std::optional<LuaFunction>>(), "function?");
    EXPECT_EQ(luaType<Future<int>>(), "Future<integer>");
    EXPECT_EQ(luaType<Future<void>>(), "Future<nil>");
    EXPECT_EQ(luaType<Future<std::vector<std::string>>>(), "Future<string[]>");
    // Unnamed enums and structs inline, named ones by name.
    EXPECT_EQ(luaType<Mode>(), R"("fast"|"slow")");
    EXPECT_EQ(luaType<Vec2>(), "{ x: number, y: number }");
    EXPECT_EQ(luaType<std::vector<Vec2>>(), "{ x: number, y: number }[]");
    EXPECT_EQ(luaType<Quality>(), "Quality");
    EXPECT_EQ(luaType<Series>(), "Series");
    // A specialization of the app's own: its luaType(), else its name if it is a Lua type, else any.
    EXPECT_EQ(luaType<Tagged>(), "Tagged");
    EXPECT_EQ(luaType<Opaque>(), "any");
    EXPECT_EQ(LuaTypes::fromName("number"), "number");
    EXPECT_EQ(LuaTypes::fromName("color"), "Color");
    EXPECT_EQ(LuaTypes::fromName("{x, y}"), "any");
}

TEST(LuaTypes, NamedTypesAreDeclaredOnce) {
    LuaTypes types;
    EXPECT_EQ(luaTypeOf<std::vector<Series>>(types), "Series[]");
    EXPECT_EQ(luaTypeOf<Series>(types), "Series");
    ASSERT_EQ(types.definitions().size(), 2U); // Series and the Quality it uses
    EXPECT_EQ(types.definitions().at("Quality"), "---@alias Quality \"low\"|\"high\"\n");
    EXPECT_EQ(types.definitions().at("Series"), "---@class Series\n"
                                                "---@field label? string\n"
                                                "---@field points? { x: number, y: number }[]\n"
                                                "---@field max? number\n"
                                                "---@field quality? Quality\n");
}

TEST(LuaTypes, Helpers) {
    EXPECT_EQ(LuaTypes::grouped("integer"), "integer");
    EXPECT_EQ(LuaTypes::grouped("integer|string"), "(integer|string)");
    EXPECT_EQ(LuaTypes::grouped("table<string, integer|string>"), "table<string, integer|string>");
    EXPECT_EQ(LuaTypes::grouped("fun(x: number)"), "(fun(x: number))");
    EXPECT_EQ(LuaTypes::optional("integer"), "integer?");
    EXPECT_EQ(LuaTypes::optional("integer?"), "integer?");
    EXPECT_EQ(LuaTypes::optional("a|b"), "(a|b)?");
    EXPECT_EQ(LuaTypes::required("integer?"), "integer");
    EXPECT_TRUE(LuaTypes::isIdentifier("Gauge_2"));
    EXPECT_FALSE(LuaTypes::isIdentifier("2x"));
    EXPECT_FALSE(LuaTypes::isIdentifier("my-thing"));
    EXPECT_FALSE(LuaTypes::isIdentifier("end"));
    EXPECT_EQ(LuaTypes::quoted(R"(a"b\c)"), R"("a\"b\\c")");
}

TEST(LuaTypes, MethodParameterNamesMustMatchTheArity) {
    class Bad final : public ScriptObject {
    public:
        static void declare(TypeBuilder<Bad>& type) { type.method("f", &Bad::f, {"a", "b"}); }
        void f(int) {}
    };
    TypeRegistry registry;
    Bad bad;
    EXPECT_THROW(registry.registerSingleton("bad", bad), std::invalid_argument);
}

TEST(LuaDefinitions, GoldenTypesFile) {
    TypeRegistry registry;
    registry.registerType<Gauge>("Gauge");
    registry.registerType<Blank>("Blank");
    registry.registerType<Blank>("not-a-name");
    registry.registerSingleton("lab", std::make_shared<Lab>());
    const std::string text = luaTypeDefinitions(registry);
    EXPECT_EQ(text, goldenTypes);
    EXPECT_EQ(compileError(text, "types.lua"), "");
    EXPECT_EQ(luaTypeDefinitions(registry), text); // deterministic
}

TEST(LuaDefinitions, BuiltInsAndPreludeAreValidLua) {
    TypeRegistry registry;
    registerBuiltinTypes(registry);
    const std::string types = luaTypeDefinitions(registry);
    EXPECT_EQ(compileError(types, "types.lua"), "");
    const std::string prelude = readSource(std::filesystem::path{IMLUI_RESOURCES_DIR} / "definitions" / "imlui.lua");
    ASSERT_FALSE(prelude.empty());
    EXPECT_EQ(compileError(prelude, "imlui.lua"), "");
    // Every built-in has its classes and constructor.
    for (const std::string& name : registry.typeNames()) {
        EXPECT_NE(types.find("---@class " + name + "Props: ElementProps\n"), std::string::npos) << name;
        EXPECT_NE(types.find("---@class " + name + ": Element\n"), std::string::npos) << name;
        EXPECT_NE(types.find("function " + name + "(props) end\nui." + name + " = " + name + "\n"), std::string::npos)
            << name;
    }
    // A sample: Button's text, its declared signal and the change signal of a property.
    EXPECT_NE(types.find("---@field text? string|(fun(self: Button): string?)|Binding\n"), std::string::npos);
    EXPECT_NE(types.find("---@field onClicked? fun(self: Button, ...: any)\n"), std::string::npos);
    EXPECT_NE(types.find("---@field onTextChanged? fun(self: Button, value: string)\n"), std::string::npos);
    EXPECT_NE(types.find("---@field restart fun(self: Timer)\n"), std::string::npos);
}

// The hand-written prelude definitions keep up with the prelude: every member of `ui` (but the internal `__*` ones)
// is defined in imlui.lua, or, for the element constructors and `connect`, in the generated types.lua.
TEST(LuaDefinitions, PreludeDefinitionsCoverEveryUiMember) {
    test::HeadlessFrame h{{.builtins = true}};
    const std::string prelude = readSource(std::filesystem::path{IMLUI_RESOURCES_DIR} / "definitions" / "imlui.lua");
    const std::string types = luaTypeDefinitions(h.types());
    ASSERT_EQ(h.run("return 0"), ""); // starts the state
    lua::pushstring(h.state(), prelude + "\n" + types);
    lua::setglobal(h.state(), "__definitions");
    EXPECT_EQ(h.run(R"(
        local missing = {}
        for k in pairs(ui) do
            if type(k) == "string" and not k:match("^__") then
                local defined = __definitions:find("\nui%." .. k .. " = ") or __definitions:find("\nfunction ui%." .. k .. "%(")
                if not defined then missing[#missing + 1] = k end
            end
        end
        table.sort(missing)
        if #missing > 0 then error("not in the definitions: " .. table.concat(missing, ", "), 0) end
    )"),
              "");
    // And nothing is defined that the prelude doesn't have (a removed or misspelled member).
    EXPECT_EQ(h.run(R"(
        local extra, isType = {}, {}
        for _, name in ipairs(ui.types()) do isType[name] = true end -- constructors made on first use (Snackbar)
        for k in __definitions:gmatch("\nui%.([%w_]+) = ") do
            if ui[k] == nil and not isType[k] then extra[#extra + 1] = k end
        end
        if #extra > 0 then error("not in the prelude: " .. table.concat(extra, ", "), 0) end
    )"),
              "");
}

TEST(LuaDefinitions, WritesBothFilesAndLeavesUnchangedOnesAlone) {
    test::TempDir dir;
    TypeRegistry registry;
    registry.registerType<Gauge>("Gauge");
    const std::filesystem::path out = dir.path() / "defs" / "nested";
    ASSERT_EQ(writeLuaDefinitions(registry, out, "---@meta imlui\n"), "");
    EXPECT_EQ(readSource(out / "imlui.lua"), "---@meta imlui\n");
    EXPECT_EQ(readSource(out / "types.lua"), luaTypeDefinitions(registry));

    // Same content: not written again (its time stays, so a build step doesn't touch what depends on it).
    const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours{1};
    std::filesystem::last_write_time(out / "types.lua", old);
    ASSERT_EQ(writeLuaDefinitions(registry, out, "---@meta imlui\n"), "");
    EXPECT_EQ(std::filesystem::last_write_time(out / "types.lua"), old);

    // A changed registry rewrites it.
    registry.registerType<Blank>("Blank");
    ASSERT_EQ(writeLuaDefinitions(registry, out, "---@meta imlui\n"), "");
    EXPECT_NE(readSource(out / "types.lua").find("function Blank(props) end"), std::string::npos);

    // A folder that can't be created is an error, not an exception.
    dir.write("file", "x");
    EXPECT_NE(writeLuaDefinitions(registry, dir.path() / "file" / "sub", ""), "");
}

TEST(LuaDefinitions, DefaultConfigTakesTheOption) {
    const test::ScopedEnv noDev{"IMLUI_DEV_PATHS", "0"};
    test::TempDir dir;
    std::string exe = (dir.path() / "app.exe").string();
    std::string option = "--lua-defs";
    std::string target = "out/defs";
    std::string joined = "--lua-defs=joined";
    {
        const test::ScopedEnv env{"IMLUI_LUA_DEFS", ""};
        char* argv[] = {exe.data(), option.data(), target.data()};
        const EngineConfig cfg = defaultConfigWith(3, argv, {}, {});
        EXPECT_EQ(cfg.luaDefinitions, std::filesystem::path{"out/defs"});
        EXPECT_EQ(cfg.entry, "app:/main.lua"); // the folder isn't taken for a script
        char* argv2[] = {exe.data(), joined.data()};
        EXPECT_EQ(defaultConfigWith(2, argv2, {}, {}).luaDefinitions, std::filesystem::path{"joined"});
        char* argv3[] = {exe.data()};
        EXPECT_TRUE(defaultConfigWith(1, argv3, {}, {}).luaDefinitions.empty());
    }
    {
        const test::ScopedEnv env{"IMLUI_LUA_DEFS", "from-env"};
        char* argv[] = {exe.data()};
        EXPECT_EQ(defaultConfigWith(1, argv, {}, {}).luaDefinitions, std::filesystem::path{"from-env"});
        char* argv2[] = {exe.data(), joined.data()}; // the option wins
        EXPECT_EQ(defaultConfigWith(2, argv2, {}, {}).luaDefinitions, std::filesystem::path{"joined"});
    }
}
