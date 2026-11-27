// Tests for the Lua language-server definitions (imlui.types.lua_types, imlui.types.lua_definitions): the LuaLS type
// expressions of LuaConvert types, the generated types.lua of a small registry (golden, with docs), the docs a
// TypeBuilder records (TypeBuilder::doc, docMember, inherited through extends), that the files are valid Lua,
// that the hand-written prelude definitions (src/resources/definitions/imlui.lua) cover every `ui` member, the files
// writeLuaDefinitions() writes, and the `--lua-defs` / IMLUI_LUA_DEFS options of defaultConfig().
#include <gtest/gtest.h>

import std;

import lua;
import imlui.color;
import imlui.app.config;
import imlui.theme.skin_tokens;
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
import imlui.types.type_info;
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
    static constexpr std::string_view doc = "How carefully it is drawn.";
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

// Structs for the flows (FlowsDecideOptionalFields): Report goes only to Lua (a result, a signal argument, a read-only
// property), with a Hit inside it (in a vector, a map and a std::optional) that is output only too; Options comes only
// from Lua (a method parameter); Mixed goes both ways, and the Inner inside it with it; Pinned is `outputOnly` though
// it is a parameter.
namespace {

struct Hit {
    int row = 0;
    std::string text;
};
struct Report {
    int matched = 0;
    std::optional<double> seconds;
    std::vector<Hit> hits;
    std::map<std::string, Hit> byName;
    std::optional<Hit> best;
    Quality quality = Quality::High;
};
struct Options {
    std::string text;
    int limit = 10;
    std::optional<Quality> quality;
};
struct Inner {
    double v = 0;
};
struct Mixed {
    Inner inner;
    std::optional<int> n;
};
struct Pinned {
    int id = 0;
};

} // namespace

template <>
struct imlui::LuaFields<Hit> {
    static constexpr std::string_view name = "Hit";
    static constexpr std::tuple fields{imlui::field("row", &Hit::row).doc("The row, 1-based."),
                                       imlui::field("text", &Hit::text)};
};
template <>
struct imlui::LuaFields<Report> {
    static constexpr std::string_view name = "Report";
    static constexpr std::string_view doc = "What a query found.\n\n@see `Options`";
    static constexpr std::tuple fields{
        imlui::field("matched", &Report::matched).doc("Rows that passed the filter."),
        imlui::field("seconds", &Report::seconds).doc("How long it took; nil when it was cancelled."),
        imlui::field("hits", &Report::hits),
        imlui::field("byName", &Report::byName),
        imlui::field("best", &Report::best),
        imlui::field("quality", &Report::quality)};
};
template <>
struct imlui::LuaFields<Options> {
    static constexpr std::string_view name = "Options";
    static constexpr std::string_view doc = "How to query.";
    static constexpr std::tuple fields{imlui::field("text", &Options::text).doc("What to look for."),
                                       imlui::field("limit", &Options::limit).doc("At most this many hits."),
                                       imlui::field("quality", &Options::quality)};
};
template <>
struct imlui::LuaFields<Inner> {
    static constexpr std::string_view name = "Inner";
    static constexpr std::tuple fields{imlui::field("v", &Inner::v)};
};
template <>
struct imlui::LuaFields<Mixed> {
    static constexpr std::string_view name = "Mixed";
    static constexpr std::tuple fields{imlui::field("inner", &Mixed::inner), imlui::field("n", &Mixed::n)};
};
template <>
struct imlui::LuaFields<Pinned> {
    static constexpr std::string_view name = "Pinned";
    static constexpr bool outputOnly = true;
    static constexpr std::tuple fields{imlui::field("id", &Pinned::id)};
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
        // Docs: several lines and paragraphs, Markdown, lines LuaLS would read as annotations (`@`, `|`), a CR,
        // trailing blanks and empty lines at the ends; "" is no doc.
        type.doc("A gauge.\n\nIt has a *needle*; see `value`.")
            .property("value", &Gauge::value, &Gauge::setValue)
            .doc("The needle's position.\r\n@see `max`\n  | 100% |  \n")
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
            .doc("Moved by the user: `to` is the new value, `why` \"drag\" or \"key\".")
            .method("reset", &Gauge::reset)
            .doc("")
            .method("setBounds", &Gauge::setBounds, {"min", "max"})
            .doc("\n\nSets both ends\tat once. -- not a comment end: ]]\n\n")
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

/// A base class and a subclass that extends it: the subclass inherits the members' docs, replaces one, has a type doc
/// of its own and documents its own members. Raw properties: one typed by its C++ type, one by a LuaLS expression and
/// read as the value itself (no binding), one undocumented (whose binding returns a type that starts with a group).
class Dial : public Element {
public:
    static void declare(TypeBuilder<Dial>& type) {
        type.doc("A dial.")
            .property("angle", &Dial::angle, &Dial::setAngle)
            .doc("The angle, in degrees.")
            .signal<>("onTurned")
            .doc("Turned by the user.")
            .method("spin", &Dial::spin)
            .doc("Spins it once.")
            .rawProperty<std::optional<double>>("size")
            .doc("The diameter (dp).")
            .rawProperty("knob", "Element|false", RawRead::ValueOnly)
            .doc("An element drawn on the knob.")
            .rawProperty<std::vector<std::variant<int, std::string>>>("marks");
    }
    void render(Frame&) override {}
    [[nodiscard]] double angle() const { return m_angle; }
    void setAngle(double v) { m_angle = v; }
    void spin() { m_angle += 360; }

private:
    double m_angle = 0;
};

class FineDial final : public Dial {
public:
    static void declare(TypeBuilder<FineDial>& type) {
        type.doc("A dial with a fine scale.")
            .extends<Dial>()
            .docMember("angle", "The angle, in tenths of a degree.")
            .docMember("size", "The fine dial's diameter (dp).")
            .property("step", &FineDial::step, &FineDial::setStep)
            .doc("The scale's step.");
    }
    [[nodiscard]] int step() const { return m_step; }
    void setStep(int v) { m_step = v; }

private:
    int m_step = 1;
};

class Lab final : public ScriptObject {
public:
    static void declare(TypeBuilder<Lab>& type) {
        type.doc("The lab backend.")
            .property("load", &Lab::load, &Lab::setLoad)
            .doc("The load, 0..1.")
            .signal<int>("onTick", {"n"})
            .doc("Every tick.")
            .method("fetch", &Lab::fetch, {"url"})
            .doc("Fetches `url`.")
            .method("idle", &Lab::idle)
            .method("opaque", &Lab::opaque)
            .method("tagged", &Lab::tagged)
            .doc("Returns `t`:\n- unchanged\n- as it is"); // the last field before `lab = {}`
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

---How carefully it is drawn.
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

-- Dial

---@alias DialSignal "onTurned"|"onAngleChanged"

---A dial.
---@class DialProps: ElementProps
---The angle, in degrees.
---@field angle? number|(fun(self: Dial): number?)|Binding
---The diameter (dp).
---@field size? number|(fun(self: Dial): number?)|Binding
---An element drawn on the knob.
---@field knob? Element|false
---@field marks? (integer|string)[]|(fun(self: Dial): ((integer|string)[]?))|Binding
---Turned by the user.
---@field onTurned? (fun(self: Dial))
---Called when `angle` changes, with the new value.
---@field onAngleChanged? (fun(self: Dial, value: number))

---A dial.
---@class Dial: Element
---The angle, in degrees.
---@field angle number
---The diameter (dp).
---@field size? number
---An element drawn on the knob.
---@field knob? Element|false
---@field marks? (integer|string)[]
---Turned by the user.
---@field onTurned? (fun(self: Dial))
---Called when `angle` changes, with the new value.
---@field onAngleChanged? (fun(self: Dial, value: number))
---Spins it once.
---@field spin (fun(self: Dial))

---A dial.
---@param props? DialProps
---@return Dial
function Dial(props) end
ui.Dial = Dial

-- FineDial

---@alias FineDialSignal "onTurned"|"onAngleChanged"|"onStepChanged"

---A dial with a fine scale.
---@class FineDialProps: ElementProps
---The angle, in tenths of a degree.
---@field angle? number|(fun(self: FineDial): number?)|Binding
---The scale's step.
---@field step? integer|(fun(self: FineDial): integer?)|Binding
---The fine dial's diameter (dp).
---@field size? number|(fun(self: FineDial): number?)|Binding
---An element drawn on the knob.
---@field knob? Element|false
---@field marks? (integer|string)[]|(fun(self: FineDial): ((integer|string)[]?))|Binding
---Turned by the user.
---@field onTurned? (fun(self: FineDial))
---Called when `angle` changes, with the new value.
---@field onAngleChanged? (fun(self: FineDial, value: number))
---Called when `step` changes, with the new value.
---@field onStepChanged? (fun(self: FineDial, value: integer))

---A dial with a fine scale.
---@class FineDial: Element
---The angle, in tenths of a degree.
---@field angle number
---The scale's step.
---@field step integer
---The fine dial's diameter (dp).
---@field size? number
---An element drawn on the knob.
---@field knob? Element|false
---@field marks? (integer|string)[]
---Turned by the user.
---@field onTurned? (fun(self: FineDial))
---Called when `angle` changes, with the new value.
---@field onAngleChanged? (fun(self: FineDial, value: number))
---Called when `step` changes, with the new value.
---@field onStepChanged? (fun(self: FineDial, value: integer))
---Spins it once.
---@field spin (fun(self: FineDial))

---A dial with a fine scale.
---@param props? FineDialProps
---@return FineDial
function FineDial(props) end
ui.FineDial = FineDial

-- Gauge

---@alias GaugeSignal "onPeak"|"onMoved"|"onValueChanged"|"onLimitChanged"|"onTagsChanged"|"onRangeChanged"|"onModeChanged"|"onSeriesChanged"|"onEitherChanged"|"onFormatterChanged"|"onTintChanged"|"onPeakChanged"

---A gauge.
---
---It has a *needle*; see `value`.
---@class GaugeProps: ElementProps
---The needle's position.
---\@see `max`
---  \| 100% |
---@field value? number|(fun(self: Gauge): number?)|Binding
---@field limit? integer|(fun(self: Gauge): integer?)|Binding
---@field tags? table<string, number>|(fun(self: Gauge): table<string, number>?)|Binding
---@field range? [number, number]|(fun(self: Gauge): [number, number]?)|Binding
---@field mode? "fast"|"slow"|(fun(self: Gauge): ("fast"|"slow")?)|Binding
---@field series? Series|(fun(self: Gauge): Series?)|Binding
---@field either? integer|string|(fun(self: Gauge): (integer|string)?)|Binding
---@field formatter? function|Binding
---@field tint? Color|(fun(self: Gauge): Color?)|Binding
---@field onPeak? (fun(self: Gauge, ...: any))
---Moved by the user: `to` is the new value, `why` "drag" or "key".
---@field onMoved? (fun(self: Gauge, to: number, why: string))
---Called when `value` changes, with the new value.
---@field onValueChanged? (fun(self: Gauge, value: number))
---Called when `limit` changes, with the new value.
---@field onLimitChanged? (fun(self: Gauge, value?: integer))
---Called when `tags` changes, with the new value.
---@field onTagsChanged? (fun(self: Gauge, value: table<string, number>))
---Called when `range` changes, with the new value.
---@field onRangeChanged? (fun(self: Gauge, value: [number, number]))
---Called when `mode` changes, with the new value.
---@field onModeChanged? (fun(self: Gauge, value: "fast"|"slow"))
---Called when `series` changes, with the new value.
---@field onSeriesChanged? (fun(self: Gauge, value: Series))
---Called when `either` changes, with the new value.
---@field onEitherChanged? (fun(self: Gauge, value: integer|string))
---Called when `formatter` changes, with the new value.
---@field onFormatterChanged? (fun(self: Gauge, value?: function))
---Called when `tint` changes, with the new value.
---@field onTintChanged? (fun(self: Gauge, value: Color))
---Called when `peak` changes, with the new value.
---@field onPeakChanged? (fun(self: Gauge, value: number))

---A gauge.
---
---It has a *needle*; see `value`.
---@class Gauge: Element
---The needle's position.
---\@see `max`
---  \| 100% |
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
---@field onPeak? (fun(self: Gauge, ...: any))
---Moved by the user: `to` is the new value, `why` "drag" or "key".
---@field onMoved? (fun(self: Gauge, to: number, why: string))
---Called when `value` changes, with the new value.
---@field onValueChanged? (fun(self: Gauge, value: number))
---Called when `limit` changes, with the new value.
---@field onLimitChanged? (fun(self: Gauge, value?: integer))
---Called when `tags` changes, with the new value.
---@field onTagsChanged? (fun(self: Gauge, value: table<string, number>))
---Called when `range` changes, with the new value.
---@field onRangeChanged? (fun(self: Gauge, value: [number, number]))
---Called when `mode` changes, with the new value.
---@field onModeChanged? (fun(self: Gauge, value: "fast"|"slow"))
---Called when `series` changes, with the new value.
---@field onSeriesChanged? (fun(self: Gauge, value: Series))
---Called when `either` changes, with the new value.
---@field onEitherChanged? (fun(self: Gauge, value: integer|string))
---Called when `formatter` changes, with the new value.
---@field onFormatterChanged? (fun(self: Gauge, value?: function))
---Called when `tint` changes, with the new value.
---@field onTintChanged? (fun(self: Gauge, value: Color))
---Called when `peak` changes, with the new value.
---@field onPeakChanged? (fun(self: Gauge, value: number))
---@field reset (fun(self: Gauge))
---Sets both ends at once. -- not a comment end: ]]
---@field setBounds (fun(self: Gauge, min: number, max?: number))
---@field points (fun(self: Gauge): { x: number, y: number }[])
---@field forEach (fun(self: Gauge, arg1: function): integer)

---A gauge.
---
---It has a *needle*; see `value`.
---@param props? GaugeProps|number
---@return Gauge
function Gauge(props) end
ui.Gauge = Gauge

-- lab (singleton)

---@alias labSignal "onTick"|"onLoadChanged"

---The lab backend.
---@class lab
---The load, 0..1.
---@field load number
---Every tick.
---@field onTick? (fun(n: integer))
---Called when `load` changes, with the new value.
---@field onLoadChanged? (fun(value: number))
---Fetches `url`.
---@field fetch (fun(self: lab, url: string): Future<Series>)
---@field idle (fun(self: lab): Future<nil>)
---@field opaque (fun(self: lab, arg1: any): any)
---Returns `t`:
---- unchanged
---- as it is
---@field tagged (fun(self: lab, arg1: Tagged): Tagged)
lab = {}

-- connect

---Every signal name of the registered types and singletons (`connect` raises for any other).
---@alias ImluiSignal DialSignal|FineDialSignal|GaugeSignal|labSignal|ListModelSignal

---Adds a listener to a signal of a C++ element or singleton: `fn` runs whenever `target` emits `signal`, after its handler.
---@param target Element|table
---@param signal ImluiSignal
---@param fn function
---@return fun() disconnect
---@overload fun(target: ListModel<any>, signal: ListModelSignal, fn: fun(...: integer)): fun()
---@overload fun(target: Dial, signal: "onTurned", fn: (fun(self: Dial))): fun()
---@overload fun(target: Dial, signal: "onAngleChanged", fn: (fun(self: Dial, value: number))): fun()
---@overload fun(target: FineDial, signal: "onTurned", fn: (fun(self: FineDial))): fun()
---@overload fun(target: FineDial, signal: "onAngleChanged", fn: (fun(self: FineDial, value: number))): fun()
---@overload fun(target: FineDial, signal: "onStepChanged", fn: (fun(self: FineDial, value: integer))): fun()
---@overload fun(target: Gauge, signal: "onPeak", fn: (fun(self: Gauge, ...: any))): fun()
---@overload fun(target: Gauge, signal: "onMoved", fn: (fun(self: Gauge, to: number, why: string))): fun()
---@overload fun(target: Gauge, signal: "onValueChanged", fn: (fun(self: Gauge, value: number))): fun()
---@overload fun(target: Gauge, signal: "onLimitChanged", fn: (fun(self: Gauge, value?: integer))): fun()
---@overload fun(target: Gauge, signal: "onTagsChanged", fn: (fun(self: Gauge, value: table<string, number>))): fun()
---@overload fun(target: Gauge, signal: "onRangeChanged", fn: (fun(self: Gauge, value: [number, number]))): fun()
---@overload fun(target: Gauge, signal: "onModeChanged", fn: (fun(self: Gauge, value: "fast"|"slow"))): fun()
---@overload fun(target: Gauge, signal: "onSeriesChanged", fn: (fun(self: Gauge, value: Series))): fun()
---@overload fun(target: Gauge, signal: "onEitherChanged", fn: (fun(self: Gauge, value: integer|string))): fun()
---@overload fun(target: Gauge, signal: "onFormatterChanged", fn: (fun(self: Gauge, value?: function))): fun()
---@overload fun(target: Gauge, signal: "onTintChanged", fn: (fun(self: Gauge, value: Color))): fun()
---@overload fun(target: Gauge, signal: "onPeakChanged", fn: (fun(self: Gauge, value: number))): fun()
---@overload fun(target: lab, signal: "onTick", fn: (fun(n: integer))): fun()
---@overload fun(target: lab, signal: "onLoadChanged", fn: (fun(value: number))): fun()
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
    EXPECT_EQ(types.definitions().at("Quality"), "---How carefully it is drawn.\n---@alias Quality \"low\"|\"high\"\n");
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

TEST(LuaTypes, RawPropertyNamesAreUniqueAndNeedAType) {
    class Clash final : public ScriptObject {
    public:
        static void declare(TypeBuilder<Clash>& type) { type.signal("onX").rawProperty<bool>("onX"); }
    };
    class Untyped final : public ScriptObject {
    public:
        static void declare(TypeBuilder<Untyped>& type) { type.rawProperty("x", ""); }
    };
    TypeRegistry registry;
    Clash clash;
    Untyped untyped;
    EXPECT_THROW(registry.registerSingleton("clash", clash), std::invalid_argument);
    EXPECT_THROW(registry.registerSingleton("untyped", untyped), std::invalid_argument);
}

TEST(LuaDefinitions, GoldenTypesFile) {
    TypeRegistry registry;
    registry.registerType<Gauge>("Gauge");
    registry.registerType<Blank>("Blank");
    registry.registerType<Blank>("not-a-name");
    registry.registerType<Dial>("Dial");
    registry.registerType<FineDial>("FineDial");
    registry.registerSingleton("lab", std::make_shared<Lab>());
    const std::string text = luaTypeDefinitions(registry);
    EXPECT_EQ(text, goldenTypes);
    EXPECT_EQ(compileError(text, "types.lua"), "");
    EXPECT_EQ(luaTypeDefinitions(registry), text); // deterministic
}

namespace {

class Searcher final : public ScriptObject {
public:
    static void declare(TypeBuilder<Searcher>& type) {
        type.property("last", &Searcher::last)
            .property("mixed", &Searcher::mixed, &Searcher::setMixed)
            .method("query", &Searcher::query, {"options"})
            .method("pin", &Searcher::pin, {"p"})
            .signal<Report>("onDone", {"report"});
    }
    [[nodiscard]] const Report& last() const { return m_last; }
    [[nodiscard]] const Mixed& mixed() const { return m_mixed; }
    void setMixed(Mixed m) { m_mixed = std::move(m); }
    Future<Report> query(Options) { return {}; }
    void pin(Pinned) {}

private:
    Report m_last;
    Mixed m_mixed;
};

} // namespace

TEST(LuaDefinitions, FlowsDecideOptionalFields) {
    TypeRegistry registry;
    registry.registerSingleton("searcher", std::make_shared<Searcher>());
    const std::string text = luaTypeDefinitions(registry);
    const std::string_view expected = R"lua(-- Value types

---@class Hit
---The row, 1-based.
---@field row integer
---@field text string

---@class Inner
---@field v? number

---@class Mixed
---@field inner? Inner
---@field n? integer

---How to query.
---@class Options
---What to look for.
---@field text? string
---At most this many hits.
---@field limit? integer
---@field quality? Quality

---@class Pinned
---@field id integer

---How carefully it is drawn.
---@alias Quality "low"|"high"

---What a query found.
---
---\@see `Options`
---@class Report
---Rows that passed the filter.
---@field matched integer
---How long it took; nil when it was cancelled.
---@field seconds? number
---@field hits Hit[]
---@field byName table<string, Hit>
---@field best? Hit
---@field quality Quality

-- searcher (singleton)
)lua";
    EXPECT_NE(text.find(expected), std::string::npos) << text;
    EXPECT_EQ(compileError(text, "types.lua"), "");

    // Outside the generator (no flow set: Input), fields are optional; `outputOnly` holds anyway.
    LuaTypes types;
    EXPECT_EQ(luaTypeOf<Report>(types), "Report");
    EXPECT_NE(types.definitions().at("Report").find("---@field matched? integer\n"), std::string::npos);
    EXPECT_EQ(luaTypeOf<Pinned>(types), "Pinned");
    EXPECT_EQ(types.definitions().at("Pinned"), "---@class Pinned\n---@field id integer\n");
    // A use in the Output flow is recorded per type; restart() keeps the uses for the next pass.
    LuaTypes flows;
    flows.setFlow(LuaTypes::Flow::Output);
    EXPECT_TRUE(flows.use("A"));
    EXPECT_FALSE(flows.use("A"));
    EXPECT_TRUE(flows.outputOnly("A"));
    flows.setFlow(LuaTypes::Flow::Input);
    EXPECT_TRUE(flows.use("A"));
    EXPECT_FALSE(flows.outputOnly("A"));
    EXPECT_FALSE(flows.outputOnly("B"));
    flows.restart();
    EXPECT_FALSE(flows.use("A"));
}

TEST(LuaDefinitions, TypeBuilderRecordsTheDocs) {
    TypeRegistry registry;
    const TypeInfo& gauge = registry.registerType<Gauge>("Gauge");
    EXPECT_EQ(gauge.doc(), "A gauge.\n\nIt has a *needle*; see `value`.");
    EXPECT_EQ(gauge.findProperty("value")->doc, "The needle's position.\r\n@see `max`\n  | 100% |  \n");
    EXPECT_EQ(gauge.findProperty("limit")->doc, "");
    EXPECT_EQ(gauge.findMethod("setBounds")->doc, "\n\nSets both ends\tat once. -- not a comment end: ]]\n\n");
    EXPECT_EQ(gauge.signalDeclarations()[1].doc, R"(Moved by the user: `to` is the new value, `why` "drag" or "key".)");

    // The subclass: its own type doc (not the base's), the base's member docs, one of them replaced, its own.
    const TypeInfo& dial = registry.registerType<Dial>("Dial");
    const TypeInfo& fine = registry.registerType<FineDial>("FineDial");
    EXPECT_EQ(dial.doc(), "A dial.");
    EXPECT_EQ(fine.doc(), "A dial with a fine scale.");
    EXPECT_EQ(dial.findProperty("angle")->doc, "The angle, in degrees.");
    EXPECT_EQ(fine.findProperty("angle")->doc, "The angle, in tenths of a degree.");
    EXPECT_EQ(fine.findMethod("spin")->doc, "Spins it once.");
    EXPECT_EQ(fine.signalDeclarations().at(0).doc, "Turned by the user.");
    EXPECT_EQ(fine.findProperty("step")->doc, "The scale's step.");
    // Raw properties: declared for the definitions only, not as properties; inherited, their doc replaceable.
    ASSERT_EQ(dial.rawProperties().size(), 3u);
    EXPECT_EQ(dial.findProperty("size"), nullptr);
    EXPECT_EQ(dial.rawProperties()[0].doc, "The diameter (dp).");
    EXPECT_EQ(dial.rawProperties()[1].read, RawRead::ValueOnly);
    EXPECT_EQ(dial.rawProperties()[2].doc, "");
    EXPECT_EQ(fine.rawProperties()[0].doc, "The fine dial's diameter (dp).");
    EXPECT_EQ(fine.rawProperties()[1].doc, "An element drawn on the knob.");
}

namespace {

/// A type whose declare() misuses doc() the way `Kind` says.
enum class Misuse { Twice, AfterDefaultProperty, AfterExtends, AfterNonVisual, UnknownMember };

template <Misuse Kind>
class Misdocumented final : public Dial {
public:
    static void declare(TypeBuilder<Misdocumented>& type) {
        if constexpr (Kind == Misuse::Twice) type.property("x", &Misdocumented::x).doc("X.").doc("X again.");
        if constexpr (Kind == Misuse::AfterDefaultProperty)
            type.property("x", &Misdocumented::x).defaultProperty("x").doc("X?");
        if constexpr (Kind == Misuse::AfterExtends) type.template extends<Dial>().doc("The last member of Dial?");
        if constexpr (Kind == Misuse::AfterNonVisual) type.doc("Doc.").nonVisual().doc("Doc again?");
        if constexpr (Kind == Misuse::UnknownMember) type.docMember("nope", "No such member.");
    }
    [[nodiscard]] int x() const { return 0; }
};

} // namespace

TEST(LuaDefinitions, DocThatDocumentsNothingThrows) {
    TypeRegistry registry;
    EXPECT_THROW(registry.registerType<Misdocumented<Misuse::Twice>>("A"), std::invalid_argument);
    EXPECT_THROW(registry.registerType<Misdocumented<Misuse::AfterDefaultProperty>>("B"), std::invalid_argument);
    EXPECT_THROW(registry.registerType<Misdocumented<Misuse::AfterExtends>>("C"), std::invalid_argument);
    EXPECT_THROW(registry.registerType<Misdocumented<Misuse::AfterNonVisual>>("D"), std::invalid_argument);
    EXPECT_THROW(registry.registerType<Misdocumented<Misuse::UnknownMember>>("E"), std::invalid_argument);
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
    // A sample: Button's text, its declared signal and the change signal of a property, with their docs.
    EXPECT_NE(types.find("---@field text? string|(fun(self: Button): string?)|Binding\n"), std::string::npos);
    EXPECT_NE(types.find("---Called when the button is clicked.\n---@field onClicked? (fun(self: Button))\n"),
              std::string::npos);
    EXPECT_NE(types.find("---Called when `text` changes, with the new value.\n"
                         "---@field onTextChanged? (fun(self: Button, value: string))\n"),
              std::string::npos);
    EXPECT_NE(types.find("---@field restart (fun(self: Timer))\n"), std::string::npos);
    EXPECT_NE(types.find("---Called every `interval` ms while running.\n---@field onTriggered? (fun(self: Timer))\n"),
              std::string::npos);
    // A signal argument of a class of its own: Canvas.onPaint's painter.
    EXPECT_NE(types.find("---@field onPaint? (fun(self: Canvas, p: CanvasPainter))\n"), std::string::npos);
    EXPECT_NE(types.find("---@class CanvasPainter\n"), std::string::npos);
    EXPECT_NE(types.find(", style?: TextStyle, font?: string)) Text with"), std::string::npos);
    // Every built-in signal is typed and every method parameter named: no `...: any` handler, no `argN`.
    for (const std::string& name : registry.typeNames()) {
        const TypeInfo* info = registry.find(name);
        ASSERT_NE(info, nullptr) << name;
        for (const TypeInfo::Signal& s : info->signalDeclarations())
            EXPECT_TRUE(s.args.has_value()) << name << "." << s.name << " is untyped";
        for (const TypeInfo::Method& m : info->methods())
            for (std::size_t i = 0; i < m.params.size(); ++i)
                EXPECT_NE(m.params[i].name, "arg" + std::to_string(i + 1)) << name << ":" << m.name << " parameter";
    }
    EXPECT_EQ(types.find("...: any"), std::string::npos);
    // connect has an overload per signal, with the handler's parameters.
    EXPECT_NE(types.find("---@overload fun(target: Slider, signal: \"onMoved\", fn: (fun(self: Slider, value: "
                         "number))): fun()\n"),
              std::string::npos);
    EXPECT_NE(types.find("---@overload fun(target: Button, signal: \"onClicked\", fn: (fun(self: Button))): fun()\n"),
              std::string::npos);
    // The files are read together: no alias is declared twice (TextStyle is generated, from Text.style). A class may
    // be (LuaLS merges a class's declarations: imlui.lua adds the Lua-side props to RepeaterProps, ...).
    std::map<std::string, int> declared;
    for (const std::string& text : {prelude, types}) {
        static const std::regex declaration{R"(---@alias ([A-Za-z_][A-Za-z0-9_]*))"};
        for (auto it = std::sregex_iterator{text.begin(), text.end(), declaration}; it != std::sregex_iterator{}; ++it)
            ++declared[(*it)[1].str()];
    }
    EXPECT_EQ(declared["TextStyle"], 1);
    for (const auto& [name, count] : declared)
        EXPECT_EQ(count, 1) << name;
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

// The theme table's `tokens` class names every token (imlui.theme.skin_tokens), with the right type, and nothing else.
TEST(LuaDefinitions, ThemeTokensCoverEveryToken) {
    std::string prelude = readSource(std::filesystem::path{IMLUI_RESOURCES_DIR} / "definitions" / "imlui.lua");
    std::erase(prelude, '\r'); // a checkout may have CRLF line ends
    const std::size_t begin = prelude.find("---@class ThemeTokens\n");
    ASSERT_NE(begin, std::string::npos);
    const std::string_view cls{prelude.data() + begin, prelude.find("\n\n", begin) - begin};
    std::size_t fields = 0;
    for (std::size_t at = cls.find("---@field "); at != std::string_view::npos; at = cls.find("---@field ", at + 1))
        ++fields;
    EXPECT_EQ(fields, tokenInfos().size());
    for (const TokenInfo& info : tokenInfos()) {
        const std::string_view type = info.kind == TokenKind::Number ? "number"
                                      : info.kind == TokenKind::Bool ? "boolean"
                                                                     : R"("filled"|"outline")";
        EXPECT_NE(cls.find(std::format("---@field {}? {} ", info.name, type)), std::string_view::npos) << info.name;
    }
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
