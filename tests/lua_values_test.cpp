// Tests for the composite LuaConvert specializations (enums with EnumNames, structs with LuaFields, maps, pairs and
// tuples, variants) and for imlui::LuaFunction (calls, errors, lifetime across closed states), on their own and
// through a registered type's properties, methods and signals.
#include <gtest/gtest.h>

import std;

import lua;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.ui.frame_request;
import imlui.types.lua_convert;
import imlui.types.lua_function;
import imlui.types.script_object;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.types.registry;

using namespace imlui;

namespace {

enum class Quality : std::uint8_t { Low, High, Unnamed };

struct Vec2 {
    double x = 0;
    double y = 0;
    bool operator==(const Vec2&) const = default;
};

struct Segment {
    std::string label = "segment";
    Vec2 from;
    std::vector<Vec2> points;
    std::optional<int> weight = 5; // a missing optional field reads as nullopt, whatever its default
    Quality quality = Quality::High;
    std::map<std::string, int> tags;
    bool operator==(const Segment&) const = default;
};

/// A type with its own LuaConvert that has no mismatch(): errors fall back to "<name> expected, got <type>".
struct Celsius {
    double degrees = 0;
    bool operator==(const Celsius&) const = default;
};

} // namespace

template <>
struct imlui::LuaConvert<Celsius> {
    static constexpr std::string_view name = "celsius";
    static void push(lua::State* s, const Celsius& c) { lua::pushnumber(s, c.degrees); }
    static bool check(lua::State* s, int idx) { return lua::type(s, idx) == lua::Type::Number; }
    static Celsius get(lua::State* s, int idx) { return {lua::tonumber(s, idx)}; }
};

template <>
struct imlui::EnumNames<Quality> {
    static constexpr std::pair<Quality, std::string_view> values[] = {{Quality::Low, "low"}, {Quality::High, "high"}};
};

template <>
struct imlui::LuaFields<Vec2> {
    static constexpr std::tuple fields{field("x", &Vec2::x), field("y", &Vec2::y)};
};

template <>
struct imlui::LuaFields<Segment> {
    static constexpr std::string_view name = "Segment";
    static constexpr auto fields = std::tuple{field("label", &Segment::label),     field("from", &Segment::from),
                                              field("points", &Segment::points),   field("weight", &Segment::weight),
                                              field("quality", &Segment::quality), field("tags", &Segment::tags)};
};

// Names for error messages, built at compile time.
static_assert(LuaConvert<Quality>::name == "'low'|'high'");
static_assert(LuaConvert<Vec2>::name == "{x, y}");
static_assert(LuaConvert<Segment>::name == "Segment");
static_assert(LuaConvert<std::vector<Vec2>>::name == "{{x, y}...}");
static_assert(LuaConvert<std::map<std::string, double>>::name == "{[string] = number}");
static_assert(LuaConvert<std::unordered_map<std::string, bool>>::name == "{[string] = boolean}");
static_assert(LuaConvert<std::pair<int, std::string>>::name == "{integer, string}");
static_assert(LuaConvert<std::tuple<>>::name == "{}");
static_assert(LuaConvert<std::variant<std::monostate, int, std::string>>::name == "nil|integer|string");
static_assert(LuaConvert<LuaFunction>::name == "function");
static_assert(LuaReadable<Segment> && LuaReadable<std::variant<int, Vec2>> && LuaReadable<LuaFunction>);
static_assert(takesLuaFunction<LuaFunction>() && takesLuaFunction<std::optional<LuaFunction>>());
static_assert(takesLuaFunction<std::variant<int, LuaFunction>>() && !takesLuaFunction<int>());

namespace {

/// Pushes v, reads it back through LuaConvert<T>, and pops it.
template <class T>
T roundTrip(lua::State* state, const T& v) {
    LuaConvert<T>::push(state, v);
    EXPECT_TRUE(LuaConvert<T>::check(state, -1));
    T out = LuaConvert<T>::get(state, -1);
    lua::pop(state);
    return out;
}

/// A plain Lua state for the conversion tests, closed by the destructor.
class LuaState {
public:
    LuaState() : m_state{lua::newstate()} { lua::openlibs(m_state); }
    ~LuaState() {
        if (m_state) lua::close(m_state);
    }
    LuaState(const LuaState&) = delete;
    LuaState& operator=(const LuaState&) = delete;
    LuaState(LuaState&&) = delete;
    LuaState& operator=(LuaState&&) = delete;

    [[nodiscard]] lua::State* get() const { return m_state; }
    void close() {
        lua::close(m_state);
        m_state = nullptr;
    }
    /// Evaluates `expr` and leaves its value on top of the stack.
    void eval(std::string_view expr) const {
        const std::string code = "return " + std::string{expr};
        ASSERT_EQ(lua::loadbuffer(m_state, code, "=eval"), lua::ok) << lua::tostring(m_state, -1);
        ASSERT_EQ(lua::pcall(m_state, 0, 1, 0), lua::ok) << lua::tostring(m_state, -1);
    }
    /// LuaConvert<T>::check of `expr`.
    template <class T>
    [[nodiscard]] bool checks(std::string_view expr) const {
        eval(expr);
        const bool ok = LuaConvert<T>::check(m_state, -1);
        lua::pop(m_state);
        return ok;
    }
    /// luaMismatch<T> of `expr` (after a failing check).
    template <class T>
    [[nodiscard]] std::string why(std::string_view expr) const {
        eval(expr);
        EXPECT_FALSE(LuaConvert<T>::check(m_state, -1)) << expr;
        std::string out = luaMismatch<T>(m_state, -1);
        lua::pop(m_state);
        EXPECT_EQ(lua::gettop(m_state), 0);
        return out;
    }
    /// LuaConvert<T>::get of `expr` (after a passing check).
    template <class T>
    [[nodiscard]] T read(std::string_view expr) const {
        eval(expr);
        EXPECT_TRUE(LuaConvert<T>::check(m_state, -1)) << expr;
        T v = LuaConvert<T>::get(m_state, -1);
        lua::pop(m_state);
        return v;
    }

private:
    lua::State* m_state;
};

using IntMap = std::map<std::string, int>;
using IntOrVec2 = std::variant<int, Vec2>;
using Ints = std::vector<int>;
using ListOrMap = std::variant<std::monostate, std::vector<double>, std::map<std::string, double>>;
using MapOrList = std::variant<std::monostate, std::map<std::string, double>, std::vector<double>>;

bool hasError(std::string_view part) {
    return std::ranges::any_of(luaui::errors(),
                               [&](const std::string& e) { return e.find(part) != std::string::npos; });
}

} // namespace

// ------------------------------------------------------------------ enums

TEST(LuaValuesTest, EnumsConvertByName) {
    const LuaState s;
    EXPECT_EQ(roundTrip(s.get(), Quality::Low), Quality::Low);
    EXPECT_EQ(roundTrip(s.get(), Quality::High), Quality::High);
    LuaConvert<Quality>::push(s.get(), Quality::High);
    EXPECT_EQ(lua::tostring(s.get(), -1), "high");
    lua::pop(s.get());
    LuaConvert<Quality>::push(s.get(), Quality::Unnamed); // no name: its integer, which doesn't read back
    EXPECT_EQ(lua::tointeger(s.get(), -1), 2);
    EXPECT_FALSE(LuaConvert<Quality>::check(s.get(), -1));
    lua::pop(s.get());
    EXPECT_FALSE(s.checks<Quality>("'medium'"));
    EXPECT_FALSE(s.checks<Quality>("'Low'")); // case-sensitive
    EXPECT_FALSE(s.checks<Quality>("0"));     // names only
    EXPECT_FALSE(s.checks<Quality>("nil"));
    EXPECT_EQ(s.read<std::optional<Quality>>("nil"), std::nullopt);
    EXPECT_EQ(lua::gettop(s.get()), 0);
}

// ------------------------------------------------------------------ structs

TEST(LuaValuesTest, StructsRoundTripAndCompose) {
    const LuaState s;
    const Segment seg{.label = "a",
                      .from = {1, 2},
                      .points = {{3, 4}, {5, 6}},
                      .weight = 7,
                      .quality = Quality::Low,
                      .tags = {{"k", 1}, {"m", 2}}};
    EXPECT_EQ(roundTrip(s.get(), seg), seg);
    EXPECT_EQ(roundTrip(s.get(), std::vector<Segment>{seg, Segment{}}), (std::vector<Segment>{seg, Segment{}}));
    EXPECT_EQ(roundTrip(s.get(), std::optional<Vec2>{Vec2{1, 2}}), (Vec2{1, 2}));

    LuaConvert<Segment>::push(s.get(), seg);
    lua::setglobal(s.get(), "seg");
    s.eval(R"(seg.label == "a" and seg.from.x == 1 and seg.points[2].y == 6 and seg.weight == 7 and
              seg.quality == "low" and seg.tags.m == 2)");
    EXPECT_TRUE(lua::toboolean(s.get(), -1));
    lua::pop(s.get());
    EXPECT_EQ(lua::gettop(s.get()), 0);
}

// A missing field keeps its default member value, a missing std::optional reads as nullopt, unknown keys are ignored,
// and a present field of the wrong type (nested too) fails the check.
TEST(LuaValuesTest, StructFieldsMissingAndWrong) {
    const LuaState s;
    const auto seg = s.read<Segment>("{ from = { y = 3 }, extra = true }");
    EXPECT_EQ(seg.label, "segment");
    EXPECT_EQ(seg.from, (Vec2{0, 3}));
    EXPECT_EQ(seg.weight, std::nullopt);
    EXPECT_EQ(seg.quality, Quality::High);
    EXPECT_TRUE(seg.points.empty());
    EXPECT_TRUE(s.checks<Segment>("{}"));
    EXPECT_FALSE(s.checks<Segment>("'segment'"));
    EXPECT_FALSE(s.checks<Segment>("{ label = {} }"));
    EXPECT_FALSE(s.checks<Segment>("{ from = { x = 'left' } }"));
    EXPECT_FALSE(s.checks<Segment>("{ points = { { x = 1 }, 5 } }"));
    EXPECT_FALSE(s.checks<Segment>("{ quality = 'medium' }"));
    EXPECT_FALSE(s.checks<Segment>("{ tags = { 1, 2 } }"));
    // Raw reads: the check never runs __index (and passes: no field is present).
    EXPECT_TRUE(s.checks<Vec2>("setmetatable({}, { __index = function() error('no metamethods') end })"));
}

// ------------------------------------------------------------------ maps, pairs, tuples, variants

TEST(LuaValuesTest, MapsHaveStringKeys) {
    const LuaState s;
    const std::map<std::string, double> m{{"a", 1.5}, {"b", -2}};
    EXPECT_EQ(roundTrip(s.get(), m), m);
    const std::unordered_map<std::string, std::vector<int>> u{{"x", {1, 2}}, {"y", {}}};
    EXPECT_EQ(roundTrip(s.get(), u), u);
    EXPECT_EQ(roundTrip(s.get(), std::map<std::string, Vec2>{{"p", {1, 2}}}),
              (std::map<std::string, Vec2>{{"p", {1, 2}}}));
    EXPECT_TRUE(s.read<IntMap>("{}").empty());
    EXPECT_FALSE(s.checks<IntMap>("{ 1, 2 }")); // number keys
    EXPECT_FALSE(s.checks<IntMap>("{ a = 1, b = 'x' }"));
    EXPECT_FALSE(s.checks<IntMap>("'a'"));
    EXPECT_EQ(lua::gettop(s.get()), 0);
}

TEST(LuaValuesTest, PairsAndTuplesAreSequences) {
    const LuaState s;
    EXPECT_EQ(roundTrip(s.get(), std::pair<int, std::string>{3, "x"}), (std::pair<int, std::string>{3, "x"}));
    using T = std::tuple<std::string, Vec2, std::optional<int>>;
    EXPECT_EQ(roundTrip(s.get(), T{"a", {1, 2}, std::nullopt}), (T{"a", {1, 2}, std::nullopt}));
    EXPECT_EQ(s.read<T>("{ 'a', { x = 1 } }"), (T{"a", {1, 0}, std::nullopt})); // a missing optional element is nil
    EXPECT_FALSE(s.checks<T>("{ 'a' }"));                                       // a missing struct is no struct
    EXPECT_FALSE(s.checks<T>("{ 'a', {}, 1, 'extra' }"));                       // too long
    EXPECT_FALSE(s.checks<T>("{ {}, {} }"));
    EXPECT_TRUE(s.checks<std::tuple<>>("{}"));
    EXPECT_EQ(lua::gettop(s.get()), 0);
}

// A sequence is a table whose keys are all integers 1..n: other keys fail the check instead of being dropped. `{}` is
// an empty sequence (and an empty map).
TEST(LuaValuesTest, SequencesHaveOnlyIndexKeys) {
    const LuaState s;
    EXPECT_TRUE(s.read<Ints>("{}").empty());
    EXPECT_EQ(s.read<Ints>("{ 1, 2, 3 }"), (Ints{1, 2, 3}));
    EXPECT_EQ(s.read<Ints>("{ [2] = 2, [1] = 1 }"), (Ints{1, 2}));
    EXPECT_FALSE(s.checks<Ints>("{ x = 1 }"));
    EXPECT_FALSE(s.checks<Ints>("{ a = 1, 2 }"));
    EXPECT_FALSE(s.checks<Ints>("{ [0] = 0, 1 }"));
    EXPECT_FALSE(s.checks<Ints>("{ [1.5] = 1 }"));
    EXPECT_FALSE(s.checks<Ints>("{ [true] = 1 }"));
    EXPECT_FALSE(s.checks<std::vector<Vec2>>("{ { x = 1 }, n = 1 }"));
    // Pairs and tuples too.
    EXPECT_FALSE((s.checks<std::pair<int, std::string>>("{ 1, 'x', extra = true }")));
    EXPECT_FALSE(s.checks<std::tuple<>>("{ x = 1 }"));
    EXPECT_TRUE((s.checks<std::tuple<int, std::optional<int>>>("{ 1 }")));
    // Maps reject non-string keys, symmetrically.
    EXPECT_FALSE(s.checks<IntMap>("{ a = 1, [2] = 3 }"));
    EXPECT_FALSE(s.checks<IntMap>("{ [true] = 1 }"));
    // Raw: no metamethod runs (__index, __pairs, __len).
    const char* guarded = "setmetatable({ 1, 2 }, { __index = error, __pairs = error, __len = error })";
    EXPECT_EQ(s.read<Ints>(guarded), (Ints{1, 2}));
    EXPECT_EQ(lua::gettop(s.get()), 0);
}

// A variant of a list and a map takes the map for a table with string keys and the list for a sequence, in either
// order; `{}` goes to the first of the two.
TEST(LuaValuesTest, VariantsTellListsFromMaps) {
    const LuaState s;
    using Map = std::map<std::string, double>;
    using List = std::vector<double>;
    EXPECT_EQ(s.read<ListOrMap>("{ x = 1 }"), (ListOrMap{Map{{"x", 1}}}));
    EXPECT_EQ(s.read<MapOrList>("{ x = 1 }"), (MapOrList{Map{{"x", 1}}}));
    EXPECT_EQ(s.read<ListOrMap>("{ 1, 2 }"), (ListOrMap{List{1, 2}}));
    EXPECT_EQ(s.read<MapOrList>("{ 1, 2 }"), (MapOrList{List{1, 2}}));
    EXPECT_EQ(s.read<ListOrMap>("{}"), ListOrMap{List{}});
    EXPECT_EQ(s.read<MapOrList>("{}"), MapOrList{Map{}});
    EXPECT_FALSE(s.checks<ListOrMap>("{ a = 1, 2 }")); // neither
    EXPECT_EQ(lua::gettop(s.get()), 0);
}

// luaMismatch says where inside a value it failed, and why.
TEST(LuaValuesTest, MismatchesNameThePath) {
    const LuaState s;
    EXPECT_EQ(s.why<Quality>("'medium'"), R"('low'|'high' expected, got string "medium")");
    EXPECT_EQ(s.why<Quality>("3"), "'low'|'high' expected, got number");
    EXPECT_EQ(s.why<Quality>("'" + std::string(50, 'a') + "'"),
              "'low'|'high' expected, got string \"" + std::string(40, 'a') + "...\"");
    EXPECT_EQ(s.why<Segment>("5"), "Segment expected, got number");
    EXPECT_EQ(s.why<Segment>("{ from = { x = 'left' } }"), ".from.x: number expected, got string");
    EXPECT_EQ(s.why<Segment>("{ points = { { x = 1 }, 5 } }"), ".points[2]: {x, y} expected, got number");
    EXPECT_EQ(s.why<Segment>("{ points = { {}, {}, { y = {} } } }"), ".points[3].y: number expected, got table");
    EXPECT_EQ(s.why<Segment>("{ quality = 'medium' }"), R"(.quality: 'low'|'high' expected, got string "medium")");
    EXPECT_EQ(s.why<Segment>("{ tags = { a = 1, b = 'x' } }"), ".tags.b: integer expected, got string");
    EXPECT_EQ(s.why<Segment>("{ tags = { 1 } }"), ".tags: {[string] = integer} expected, got table with key 1");
    EXPECT_EQ(s.why<std::vector<Segment>>("{ {}, { from = { y = false } } }"),
              "[2].from.y: number expected, got boolean");
    EXPECT_EQ(s.why<IntMap>(R"({ ["two words"] = 'x' })"), R"(["two words"]: integer expected, got string)");
    EXPECT_EQ(s.why<IntMap>("{ [true] = 1 }"), "{[string] = integer} expected, got table with a boolean key");
    EXPECT_EQ(s.why<Ints>("{ 1, x = 2 }"), R"({integer...} expected, got table with key "x")");
    EXPECT_EQ(s.why<Ints>("{ [1.5] = 2 }"), "{integer...} expected, got table with key 1.5");
    EXPECT_EQ(s.why<Ints>("'1, 2'"), "{integer...} expected, got string");
    EXPECT_EQ(s.why<std::optional<Vec2>>("{ y = true }"), ".y: number expected, got boolean");
    using T = std::tuple<std::string, Vec2, std::optional<int>>;
    EXPECT_EQ(s.why<T>("{ 'a', { x = 'no' } }"), "[2].x: number expected, got string");
    EXPECT_EQ(s.why<T>("{ 'a' }"), "[2]: {x, y} expected, got nil");
    EXPECT_EQ(s.why<T>("{ 'a', {}, 1, 'extra' }"), "{string, {x, y}, integer} expected, got table with key 4");
    // A variant reports the one alternative the value had the shape of, else its own name.
    EXPECT_EQ(s.why<ListOrMap>("{ x = 'a' }"), ".x: number expected, got string");
    EXPECT_EQ(s.why<ListOrMap>("{ 1, 'a' }"), "[2]: number expected, got string");
    EXPECT_EQ(s.why<ListOrMap>("true"), "nil|{number...}|{[string] = number} expected, got boolean");
    // A LuaConvert without mismatch(): its name, also inside a composite.
    EXPECT_EQ(s.why<Celsius>("'hot'"), "celsius expected, got string");
    EXPECT_EQ(s.why<std::vector<Celsius>>("{ 20, 'hot' }"), "[2]: celsius expected, got string");
    EXPECT_EQ(s.read<std::vector<Celsius>>("{ 20 }"), std::vector<Celsius>{Celsius{20}});

    EXPECT_EQ(luaMismatchAt("Plot.points", "[3].x: number expected, got string"),
              "Plot.points[3].x: number expected, got string");
    EXPECT_EQ(luaMismatchAt("Plot.max", "number expected, got string"), "Plot.max: number expected, got string");
    EXPECT_EQ(luaMismatchOf("Segment", ".from.x: number expected"), "Segment.from.x: number expected");
    EXPECT_EQ(luaMismatchOf("{x, y}", ".x: number expected"), "x: number expected");
    EXPECT_EQ(luaMismatchOf("{integer...}", "[2]: integer expected"), "[2]: integer expected");
    EXPECT_EQ(luaMismatchOf("Segment", "Segment expected, got number"), "Segment expected, got number");
}

TEST(LuaValuesTest, VariantsTakeTheFirstMatchingAlternative) {
    const LuaState s;
    using V = std::variant<std::monostate, int, std::string, Vec2>;
    EXPECT_EQ(roundTrip(s.get(), V{}), V{});
    EXPECT_EQ(roundTrip(s.get(), V{4}), V{4});
    EXPECT_EQ(roundTrip(s.get(), V{std::string{"hi"}}), V{std::string{"hi"}});
    EXPECT_EQ(roundTrip(s.get(), V{Vec2{1, 2}}), (V{Vec2{1, 2}}));
    EXPECT_EQ(s.read<V>("nil"), V{});
    EXPECT_EQ(s.read<V>("'12'"), V{12}); // int comes before std::string, and takes numeric strings
    EXPECT_FALSE(s.checks<V>("true"));
    EXPECT_FALSE(s.checks<IntOrVec2>("nil"));
    EXPECT_EQ(lua::gettop(s.get()), 0);
}

// ------------------------------------------------------------------ LuaFunction

TEST(LuaFunctionTest, CallsWithConvertedArgumentsAndResults) {
    luaui::clearErrors();
    const LuaState s;
    s.eval("function(a, p) return a + p.x * 10 end");
    const LuaFunction f{s.get(), -1};
    lua::pop(s.get());
    EXPECT_FALSE(f.expired());
    EXPECT_TRUE(f);
    EXPECT_EQ(f.call<double>(1, Vec2{2, 0}), 21.0);
    EXPECT_TRUE(f.call(1, Vec2{}));
    EXPECT_EQ(f.call<std::string>(1, Vec2{}), "1"); // numbers convert to strings, like everywhere
    EXPECT_EQ(lua::gettop(s.get()), 0);

    const LuaFunction copy = f; // copies share the reference
    EXPECT_EQ(copy, f);
    s.eval("function() end");
    const LuaFunction other{s.get(), -1};
    EXPECT_NE(other, f);
    const LuaFunction same{s.get(), -1};
    EXPECT_EQ(same, other); // the same Lua function, through two references
    lua::pop(s.get());
    EXPECT_EQ(LuaFunction{}, LuaFunction{});
    EXPECT_TRUE(luaui::errors().empty());
}

TEST(LuaFunctionTest, ErrorsAreReportedNotThrown) {
    luaui::clearErrors();
    const LuaState s;
    s.eval("function(n) if n < 0 then error('negative: ' .. n) end return 'ok' end");
    const LuaFunction f{s.get(), -1};
    lua::pop(s.get());
    EXPECT_FALSE(f.call(-1));
    EXPECT_TRUE(hasError("negative: -1"));
    luaui::clearErrors();
    EXPECT_EQ(f.call<int>(1), std::nullopt); // 'ok' isn't an integer
    EXPECT_TRUE(hasError("LuaFunction: bad result (integer expected, got string)"));
    s.eval("function() return { x = 1, y = 'up' } end");
    const LuaFunction g{s.get(), -1};
    lua::pop(s.get());
    EXPECT_EQ(g.call<Vec2>(), std::nullopt);
    EXPECT_TRUE(hasError("LuaFunction: bad result (y: number expected, got string)"));
    EXPECT_EQ(f.call<std::optional<int>>(-2), std::nullopt); // an error: no result at all
    EXPECT_EQ(lua::gettop(s.get()), 0);
    luaui::clearErrors();

    lua::pushinteger(s.get(), 3);
    const LuaFunction notAFunction{s.get(), -1};
    lua::pop(s.get());
    EXPECT_TRUE(notAFunction.expired());
    EXPECT_FALSE(notAFunction.call());
    EXPECT_TRUE(luaui::errors().empty());
}

// After its state closed, a handle is expired: calls do nothing, pushing pushes nil, destroying it doesn't touch the
// (freed) state.
TEST(LuaFunctionTest, IsASafeNoOpAfterItsStateCloses) {
    luaui::clearErrors();
    std::optional<LuaFunction> f;
    {
        LuaState s;
        s.eval("function(x) calls = (calls or 0) + 1 return x end");
        f.emplace(s.get(), -1);
        lua::pop(s.get());
        EXPECT_EQ(f->call<int>(4), 4);
        s.close();
    }
    EXPECT_TRUE(f->expired());
    EXPECT_FALSE(*f);
    EXPECT_FALSE(f->call(1));
    EXPECT_EQ(f->call<int>(1), std::nullopt);
    const LuaFunction copy = *f;
    EXPECT_TRUE(copy.expired());

    const LuaState other; // possibly at the closed state's address
    EXPECT_FALSE(f->push(other.get()));
    EXPECT_TRUE(lua::isnil(other.get(), -1));
    lua::pop(other.get());
    f.reset(); // releases nothing: the state is gone
    EXPECT_TRUE(luaui::errors().empty());
}

// A handle only pushes onto its own state: another state (a reloaded one) gets nil.
TEST(LuaFunctionTest, PushesNilIntoAnotherState) {
    const LuaState a;
    const LuaState b;
    a.eval("print");
    const LuaFunction f{a.get(), -1};
    lua::pop(a.get());
    EXPECT_FALSE(f.push(b.get()));
    EXPECT_TRUE(lua::isnil(b.get(), -1));
    lua::pop(b.get());
    EXPECT_TRUE(f.push(a.get()));
    lua::pop(a.get());
}

// Releasing handles frees the registry references while the state lives (they are reused, not leaked).
TEST(LuaFunctionTest, ReleasesItsReference) {
    const LuaState s;
    s.eval("function() end");
    for (int i = 0; i < 1000; ++i) {
        const LuaFunction f{s.get(), -1};
        EXPECT_FALSE(f.expired());
    }
    lua::pop(s.get());
    lua::pushnil(s.get());
    const int ref = lua::ref(s.get()); // nil gets refNil; take a real one below
    EXPECT_EQ(ref, lua::refNil);
    lua::pushboolean(s.get(), true);
    const int next = lua::ref(s.get());
    EXPECT_LT(next, 10); // the freed references were reused
    lua::unref(s.get(), next);
}

// ------------------------------------------------------------------ through a registered type

namespace {

struct Calls {
    int setCallback = 0;
    int destroyed = 0;
    std::vector<std::string> seen;
};
Calls calls;

class Plotter final : public Element {
public:
    static void declare(TypeBuilder<Plotter>& type) {
        type.property("segment", &Plotter::segment, &Plotter::setSegment)
            .property("quality", &Plotter::quality, &Plotter::setQuality)
            .property("pick", &Plotter::pick, &Plotter::setPick)
            .property("callback", &Plotter::callback, &Plotter::setCallback)
            .property("labels", &Plotter::labels, &Plotter::setLabels)
            .signal("onSegment")
            .method("forEach", &Plotter::forEach)
            .method("total", &Plotter::total)
            .method("swap", &Plotter::swap)
            .method("emitSegment", &Plotter::emitSegment)
            .method("runCallback", &Plotter::runCallback)
            .method("measure", &Plotter::measure)
            .method("hottest", &Plotter::hottest)
            .property("temperature", &Plotter::temperature, &Plotter::setTemperature);
    }
    ~Plotter() override {
        ++calls.destroyed;
        m_callback.call(std::string{"destroyed"}); // during lua_close: runs or is a no-op, never crashes
    }
    Plotter() = default;
    Plotter(const Plotter&) = delete;
    Plotter& operator=(const Plotter&) = delete;
    Plotter(Plotter&&) = delete;
    Plotter& operator=(Plotter&&) = delete;
    void render(Frame&) override {}

    [[nodiscard]] const Segment& segment() const { return m_segment; }
    void setSegment(Segment s) { m_segment = std::move(s); }
    [[nodiscard]] Quality quality() const { return m_quality; }
    void setQuality(Quality q) { m_quality = q; }
    [[nodiscard]] const std::variant<int, std::string>& pick() const { return m_pick; }
    void setPick(std::variant<int, std::string> p) { m_pick = std::move(p); }
    [[nodiscard]] const LuaFunction& callback() const { return m_callback; }
    void setCallback(LuaFunction f) {
        ++calls.setCallback;
        m_callback = std::move(f);
    }
    [[nodiscard]] const std::unordered_map<std::string, std::string>& labels() const { return m_labels; }
    void setLabels(std::unordered_map<std::string, std::string> l) { m_labels = std::move(l); }

    /// Calls `fn` for each point of the segment; stops when it returns false (nil, or an error, keeps going).
    int forEach(const LuaFunction& fn) const {
        int n = 0;
        for (const Vec2& p : m_segment.points) {
            ++n;
            if (fn.call<std::optional<bool>>(p, n) == false) break;
        }
        return n;
    }
    [[nodiscard]] std::size_t measure(const Segment& s) const { return s.points.size(); }
    [[nodiscard]] double hottest(const std::vector<Celsius>& all) const {
        double out = -273.15;
        for (const Celsius& c : all)
            out = std::max(out, c.degrees);
        return out;
    }
    [[nodiscard]] Celsius temperature() const { return m_temperature; }
    void setTemperature(Celsius c) { m_temperature = c; }
    [[nodiscard]] double total(const std::map<std::string, double>& values) const {
        double sum = 0;
        for (const auto& [k, v] : values)
            sum += v;
        return sum;
    }
    [[nodiscard]] std::tuple<std::string, int> swap(const std::pair<int, std::string>& p) const {
        return {p.second, p.first};
    }
    void emitSegment() { emit("onSegment", m_segment, m_quality); }
    [[nodiscard]] std::optional<std::string> runCallback(const std::string& arg) const {
        return m_callback.call<std::string>(arg);
    }

private:
    Segment m_segment;
    Quality m_quality = Quality::Low;
    std::variant<int, std::string> m_pick;
    LuaFunction m_callback;
    std::unordered_map<std::string, std::string> m_labels;
    Celsius m_temperature;
};

/// A singleton keeping a Lua callback across reloads.
class Hub final : public ScriptObject {
public:
    static void declare(TypeBuilder<Hub>& type) {
        type.property("filter", &Hub::filter, &Hub::setFilter)
            .property("points", &Hub::points, &Hub::setPoints)
            .method("keep", &Hub::keep);
    }
    [[nodiscard]] const std::vector<Vec2>& points() const { return m_points; }
    void setPoints(std::vector<Vec2> p) { m_points = std::move(p); }
    [[nodiscard]] const std::optional<LuaFunction>& filter() const { return m_filter; }
    void setFilter(std::optional<LuaFunction> f) { m_filter = std::move(f); }
    [[nodiscard]] std::vector<int> keep(const std::vector<int>& in) const {
        std::vector<int> out;
        for (int v : in)
            if (!m_filter || m_filter->call<bool>(v).value_or(false)) out.push_back(v);
        return out;
    }

private:
    std::optional<LuaFunction> m_filter;
    std::vector<Vec2> m_points;
};

lua::State* newUiState(TypeRegistry& types) {
    lua::State* state = lua::newstate();
    lua::openlibs(state);
    types.install(state);
    const std::string boot = std::string{"package.path = '"} + IMLUI_RESOURCES_DIR + "/?.lua;' .. package.path\n" +
                             R"(package.preload["imlui.native"] = function()
                                    return { time = function() return 0 end, log = function() end }
                                end
                                ui = require("imlui")
                                Plotter = ui.env.Plotter)";
    EXPECT_EQ(lua::loadbuffer(state, boot, "=boot"), lua::ok) << lua::tostring(state, -1);
    EXPECT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
    return state;
}

class LuaValuesTypesTest : public ::testing::Test {
protected:
    void SetUp() override {
        calls = {};
        luaui::clearErrors();
        m_types.registerType<Plotter>("Plotter");
        m_types.registerSingleton("hub", m_hub);
        m_state = newUiState(m_types);
    }
    void TearDown() override {
        if (m_state) lua::close(m_state);
        luaui::clearErrors();
        clearFrameRequests();
    }

    static void run(lua::State* state, std::string_view code) {
        ASSERT_EQ(lua::loadbuffer(state, code, "=test"), lua::ok) << lua::tostring(state, -1);
        ASSERT_EQ(lua::pcall(state, 0, 0, 0), lua::ok) << lua::tostring(state, -1);
    }
    void run(std::string_view code) const { run(m_state, code); }
    [[nodiscard]] std::string runError(std::string_view code) const {
        EXPECT_EQ(lua::loadbuffer(m_state, code, "=test"), lua::ok);
        if (lua::pcall(m_state, 0, 0, 0) == lua::ok) return {};
        std::string msg{lua::tostring(m_state, -1)};
        lua::pop(m_state);
        return msg;
    }
    Plotter* sync(const char* name) {
        lua::getglobal(m_state, name);
        Element* e = m_types.sync(luaui::Node{m_state, -1});
        lua::pop(m_state);
        return dynamic_cast<Plotter*>(e);
    }

    TypeRegistry m_types;
    Hub m_hub;
    lua::State* m_state = nullptr;
};

} // namespace

TEST_F(LuaValuesTypesTest, PropertiesSyncCompositeValues) {
    run(R"(
        p = Plotter {
            segment = { label = "s", points = { { x = 1, y = 2 }, { x = 3 } }, tags = { a = 1 } },
            quality = "high",
            pick = "name",
            labels = { ok = "OK" },
        })");
    Plotter* p = sync("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->segment().label, "s");
    EXPECT_EQ(p->segment().points, (std::vector<Vec2>{{1, 2}, {3, 0}}));
    EXPECT_EQ(p->segment().tags.at("a"), 1);
    EXPECT_EQ(p->quality(), Quality::High);
    EXPECT_EQ(p->pick(), (std::variant<int, std::string>{std::string{"name"}}));
    EXPECT_EQ(p->labels().at("ok"), "OK");
    EXPECT_TRUE(luaui::errors().empty());

    run(R"(p.quality = "medium"; p.segment = { from = { x = "left" } }; p.pick = 4)");
    sync("p");
    EXPECT_TRUE(hasError(R"(Plotter.quality: 'low'|'high' expected, got string "medium")"));
    EXPECT_TRUE(hasError("Plotter.segment.from.x: number expected, got string"));
    EXPECT_EQ(p->quality(), Quality::High); // unchanged
    EXPECT_EQ(p->pick(), (std::variant<int, std::string>{4}));
}

TEST_F(LuaValuesTypesTest, MethodsTakeAndReturnCompositeValues) {
    run(R"(
        p = Plotter { segment = { points = { { x = 1 }, { x = 2 }, { x = 3 } } } }
        assert(p:total({ a = 1.5, b = 2 }) == 3.5)
        local t = p:swap({ 7, "seven" })
        assert(t[1] == "seven" and t[2] == 7)
        local xs = {}
        assert(p:forEach(function(pt, i) xs[#xs + 1] = pt.x; return i < 2 end) == 2)
        assert(#xs == 2 and xs[2] == 2)
    )");
    run("assert(p:forEach(function() end) == 3)"); // nil keeps going: only false stops
    EXPECT_NE(runError("p:total({ 1, 2 })")
                  .find("bad argument #1 to 'Plotter:total' ({[string] = number} expected, got table with key 1)"),
              std::string::npos);
    EXPECT_NE(runError("p:total({ a = 1, b = 'x' })").find("bad argument #1 to 'Plotter:total' (b: number expected"),
              std::string::npos);
    EXPECT_NE(runError("p:forEach(5)").find("bad argument #1 to 'Plotter:forEach' (function expected, got number)"),
              std::string::npos);
    EXPECT_NE(runError("p:swap({ 'x', 1 })").find("([1]: integer expected, got string)"), std::string::npos);
    EXPECT_NE(runError("p:swap({ 1, 'x', 3 })").find("({integer, string} expected, got table with key 3)"),
              std::string::npos);
    // An error inside the callback is reported, not raised through the method.
    run("assert(p:forEach(function() error('cb failed') end) == 3)");
    EXPECT_TRUE(hasError("cb failed"));
}

// Conversion errors name the field and the element inside the value that failed, wherever a value converts.
TEST_F(LuaValuesTypesTest, ConversionErrorsNameTheField) {
    run(R"(p = Plotter { segment = { points = { { x = 1 }, { x = 2 } } } })");
    EXPECT_NE(runError("p:measure({ points = { { x = 1 }, { x = 'a' } } })")
                  .find("bad argument #1 to 'Plotter:measure' (Segment.points[2].x: number expected, got string)"),
              std::string::npos);
    EXPECT_NE(runError("p:measure({ quality = 'medium' })")
                  .find(R"((Segment.quality: 'low'|'high' expected, got string "medium"))"),
              std::string::npos);
    EXPECT_NE(runError("p:measure(5)").find("(Segment expected, got number)"), std::string::npos);
    run("assert(p:measure({ points = { {}, {}, {} } }) == 3)");
    // A LuaConvert without mismatch(): the plain message, alone and inside a vector.
    EXPECT_NE(runError("p:hottest({ 20, 'hot' })").find("bad argument #1 to 'Plotter:hottest' ([2]: celsius expected"),
              std::string::npos);
    run("assert(p:hottest({ 20, 31 }) == 31)");

    // Property sync.
    run(R"(p.segment = { points = { { x = 1 }, { y = 'up' } } }; p.temperature = "warm")");
    sync("p");
    EXPECT_TRUE(hasError("Plotter.segment.points[2].y: number expected, got string"));
    EXPECT_TRUE(hasError("Plotter.temperature: celsius expected, got string"));
    luaui::clearErrors();
    run(R"(p.segment = { tags = { ok = 1, ["no way"] = {} } })");
    sync("p");
    EXPECT_TRUE(hasError(R"(Plotter.segment.tags["no way"]: integer expected, got table)"));

    // A singleton property.
    EXPECT_NE(runError("hub.points = { { x = 1 }, { y = 'a' } }").find("hub.points[2].y: number expected, got string"),
              std::string::npos);
    EXPECT_NE(runError("hub.points = { x = 1 }").find(R"(hub.points: {{x, y}...} expected, got table with key "x")"),
              std::string::npos);
    run("hub.points = { { x = 3 } }");
    EXPECT_EQ(m_hub.points(), (std::vector<Vec2>{Vec2{3, 0}}));
}

TEST_F(LuaValuesTypesTest, SignalsPassCompositeValues) {
    run(R"(
        got = nil
        p = Plotter { segment = { label = "sig", from = { x = 4, y = 5 } }, quality = "high",
                      onSegment = function(self, seg, q) got = seg.label .. seg.from.y .. q end }
        p:emitSegment()
        assert(got == "sig5high", got)
    )");
}

// A function given to a property that takes functions is the value, not a binding: the sync doesn't call it, and
// doesn't make a new handle (or call the setter) every sync while it stays the same.
TEST_F(LuaValuesTypesTest, FunctionPropertiesArentBindings) {
    run(R"(
        p = Plotter { callback = function(arg) return "got " .. arg end }
        f = Plotter { pick = function() return 9 end } -- a binding as usual for other types
    )");
    Plotter* p = sync("p");
    ASSERT_NE(p, nullptr);
    EXPECT_FALSE(p->callback().expired());
    EXPECT_EQ(p->callback().call<std::string>(std::string{"x"}), "got x");
    sync("p");
    sync("p");
    EXPECT_EQ(calls.setCallback, 1);
    run(R"(assert(p:runCallback("y") == "got y"))");
    run(R"(p.callback = function(arg) return "new " .. arg end)");
    sync("p");
    EXPECT_EQ(calls.setCallback, 2);
    EXPECT_EQ(p->runCallback("z"), "new z");
    EXPECT_EQ(sync("f")->pick(), (std::variant<int, std::string>{9}));
    run("p.callback = nil");
    sync("p");
    EXPECT_TRUE(p->callback().expired()); // nil resets to the default: an empty handle
    EXPECT_TRUE(luaui::errors().empty());
}

// Elements holding handles are destroyed while their state closes: their handles stay safe, whichever finalizer
// (the instance's or the state's liveness token) runs first.
TEST_F(LuaValuesTypesTest, ElementsHoldingHandlesDieWithTheState) {
    run(R"(
        seen = {}
        keep = {}
        for i = 1, 20 do keep[i] = Plotter { callback = function(why) seen[#seen + 1] = why end } end
    )");
    for (int i = 1; i <= 20; ++i) {
        lua::getglobal(m_state, "keep");
        lua::rawgeti(m_state, -1, i);
        ASSERT_NE(m_types.sync(luaui::Node{m_state, -1}), nullptr);
        lua::pop(m_state, 2);
    }
    lua::close(m_state);
    m_state = nullptr;
    EXPECT_EQ(calls.destroyed, 20);
}

// A singleton's handle outlives the state it came from (a hot reload): it expires, and the new state sets a new one.
TEST_F(LuaValuesTypesTest, SingletonHandlesExpireOnReload) {
    run(R"(
        hub.filter = function(v) return v % 2 == 0 end
        local kept = hub:keep({ 1, 2, 3, 4 })
        assert(#kept == 2 and kept[2] == 4)
        assert(type(hub.filter) == "function")
    )");
    ASSERT_TRUE(m_hub.filter().has_value());
    const LuaFunction old = *m_hub.filter();

    lua::State* fresh = newUiState(m_types);
    lua::close(m_state); // the reload succeeded: the old state closes
    m_state = fresh;
    EXPECT_TRUE(old.expired());
    EXPECT_TRUE(m_hub.filter()->expired());
    EXPECT_EQ(m_hub.keep({1, 2, 3}), std::vector<int>{}); // the expired filter keeps nothing, calls nothing
    run(R"(
        assert(hub.filter == nil) -- an expired handle pushes nil
        hub.filter = function(v) return v > 1 end
        assert(#hub:keep({ 1, 2, 3 }) == 2)
        hub.filter = nil
        assert(#hub:keep({ 1, 2, 3 }) == 3)
    )");
    EXPECT_TRUE(luaui::errors().empty());
}
