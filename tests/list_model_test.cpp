// ListModel / VectorModel<T> (imlui.types.list_model, imlui.types.vector_model): rows that live in C++, read lazily by
// Lua. Covered: Lua access (#model, model[i], ipairs, the methods, one value per state), lazy conversion (counting and
// rows outside a ListView's view never convert), the row bookkeeping of every notification, delegates rebuilt only for
// the rows that changed (ListView and Repeater, with and without keys: inserts at the top, removals, moves, sorts,
// replace), the model as a singleton property, a method result (shared and borrowed) and a singleton global, a model
// whose owner (an element) is destroyed and a model that outlives its Lua state (a hot reload), a model edited inside
// a delegate, frame requests from notifications, the thread-safe appender and the Lua definitions. The row cap
// (setMaxRows: erasing the oldest rows in the same step as the append), appender(onApplied), and filling a model
// with runAsyncThen, whose future carries only a report to Lua.
#include <gtest/gtest.h>

import std;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.future;
import imlui.types.list_model;
import imlui.types.lua_convert;
import imlui.types.lua_definitions;
import imlui.types.script_object;
import imlui.types.type_builder;
import imlui.types.vector_model;
import imlui.ui.background;
import imlui.ui.frame_request;
import imlui.ui.main_thread;
import imlui.test.headless_frame;
import imlui.test.post_waiter;

namespace lmtest {

struct LogLine {
    std::int64_t id = 0;
    std::string text;
    bool operator==(const LogLine&) const = default;
};

/// What Backend::fill reports to Lua (the rows stay in C++).
struct FillReport {
    int rows = 0;
    bool onMainThread = false;
};

} // namespace lmtest

template <>
struct imlui::LuaFields<lmtest::FillReport> {
    static constexpr std::string_view name = "FillReport";
    static constexpr std::tuple fields{imlui::field("rows", &lmtest::FillReport::rows),
                                       imlui::field("onMainThread", &lmtest::FillReport::onMainThread)};
};

template <>
struct imlui::LuaFields<lmtest::LogLine> {
    static constexpr std::string_view name = "LogLine";
    static constexpr std::tuple fields{imlui::field("id", &lmtest::LogLine::id),
                                       imlui::field("text", &lmtest::LogLine::text)};
};

using namespace imlui;
using lmtest::FillReport;
using lmtest::LogLine;

namespace {

/// A VectorModel that counts the rows it converts for Lua.
class Logs final : public VectorModel<LogLine> {
public:
    using VectorModel::VectorModel;
    void pushItem(lua::State* state, std::size_t row) const override {
        ++m_converted;
        VectorModel::pushItem(state, row);
    }
    [[nodiscard]] std::optional<Key> key(std::size_t row) const override {
        ++m_keyed;
        return VectorModel::key(row);
    }
    [[nodiscard]] int converted() const { return m_converted; }
    [[nodiscard]] int keyed() const { return m_keyed; }

private:
    mutable int m_converted = 0;
    mutable int m_keyed = 0;
};

std::int64_t idOf(const LogLine& l) { return l.id; }

std::vector<LogLine> lines(std::int64_t from, std::int64_t to) {
    std::vector<LogLine> out;
    for (std::int64_t i = from; i <= to; ++i)
        out.push_back({i, "line " + std::to_string(i)});
    return out;
}

/// A singleton owning models: `logs` (keyed), `plain` (no keys), and methods returning models.
class Backend final : public ScriptObject {
public:
    static void declare(TypeBuilder<Backend>& type) {
        type.doc("The test backend.")
            .property("logs", &Backend::logs)
            .doc("Keyed log lines.")
            .property("plain", &Backend::plain)
            .doc("Log lines without keys.")
            .method("query", &Backend::query, {"n"})
            .doc("A new model of `n` rows.")
            .method("plainRef", &Backend::plainRef)
            .doc("`plain`, through a method.")
            .method("add", &Backend::add, {"text"})
            .doc("Appends a line to `logs`.")
            .method("dropFirst", &Backend::dropFirst)
            .doc("Removes the first line of `logs`.")
            .method("fill", &Backend::fill, {"n"})
            .doc("Fills `plain` with `n` lines on the pool; the future gets a report.");
    }
    /// The recipe of the docs: the rows are built on the pool and applied on the main thread, the future gets a report.
    Future<FillReport> fill(int n) {
        return runAsyncThen([n] { return lines(1, n); },
                            [this, mainThread = std::this_thread::get_id()](std::vector<LogLine> rows) {
                                plainModel.replace(std::move(rows));
                                return FillReport{static_cast<int>(plainModel.size()),
                                                  std::this_thread::get_id() == mainThread};
                            });
    }

    [[nodiscard]] const Logs& logs() const { return logsModel; }
    [[nodiscard]] const Logs& plain() const { return plainModel; }
    [[nodiscard]] std::shared_ptr<VectorModel<LogLine>> query(int n) {
        auto model = std::make_shared<VectorModel<LogLine>>(&idOf);
        model->appendRows(lines(1, n));
        lastQuery = model;
        return model;
    }
    VectorModel<LogLine>& plainRef() { return plainModel; }
    void add(std::string text) { logsModel.append({nextId++, std::move(text)}); }
    void dropFirst() {
        if (!logsModel.empty()) logsModel.erase(0);
    }

    Logs logsModel{&idOf};
    Logs plainModel;
    std::weak_ptr<VectorModel<LogLine>> lastQuery;
    std::int64_t nextId = 100'000;
};

/// A box `h` dp tall.
class Box final : public Element {
public:
    static void declare(TypeBuilder<Box>& type) { type.property("h", &Box::h, &Box::setH); }
    void render(Frame& frame) override { ImGui::Dummy({10.0f, frame.dp(static_cast<float>(m_h))}); }
    [[nodiscard]] double h() const { return m_h; }
    void setH(double h) { m_h = h; }

private:
    double m_h = 20.0;
};

/// An element that owns a model and hands it out through a method.
class Owner final : public Element {
public:
    static void declare(TypeBuilder<Owner>& type) { type.method("rows", &Owner::rows).nonVisual(); }
    void render(Frame&) override {}
    VectorModel<std::int64_t>& rows() { return m_rows; }
    Owner() { m_rows.appendRows({1, 2, 3}); }

private:
    VectorModel<std::int64_t> m_rows;
};

class ListModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        clearPosted();
        luaui::clearErrors();
        clearFrameRequests();
        m_backend.logsModel.appendRows(lines(1, 20));
        m_backend.plainModel.appendRows(lines(1, 20));
        makeHarness();
    }
    void TearDown() override {
        m_h.reset();
        clearPosted();
        luaui::clearErrors();
        clearFrameRequests();
    }
    void makeHarness() {
        m_h.reset();
        m_h.emplace(test::HeadlessFrame::Options{.height = 600.0f, .builtins = true, .fullDebug = true});
        m_h->registerType<Box>("Box");
        m_h->registerType<Owner>("Owner");
        m_h->types().registerSingleton("backend", m_backend);
    }
    void run(std::string_view code) { ASSERT_EQ(m_h->run(code), ""); }
    void check(std::string_view expr) { ASSERT_EQ(m_h->run("assert(" + std::string{expr} + ")"), "") << expr; }

    Backend m_backend; // outlives the harness, like a host's singleton
    std::optional<test::HeadlessFrame> m_h;
};

} // namespace

// ------------------------------------------------------------ the C++ side

TEST(ListModelRows, NotificationsKeepTheIdsOfTheRowsThatOnlyMoved) {
    VectorModel<int> m;
    m.appendRows({10, 20, 30, 40, 50});
    const auto ids = [&] {
        std::vector<std::uint64_t> out;
        for (std::size_t i = 0; i < m.size(); ++i)
            out.push_back(m.rowId(i));
        return out;
    };
    const std::vector<std::uint64_t> a = ids(); // starts the bookkeeping
    const std::uint64_t rev = m.revision();
    m.insert(0, 5);
    std::vector<std::uint64_t> b = ids();
    EXPECT_EQ(std::vector(b.begin() + 1, b.end()), a);
    EXPECT_GT(m.revision(), rev);
    m.erase(2, 2); // 20, 30
    b = ids();
    EXPECT_EQ(b[1], a[0]);
    EXPECT_EQ(b[2], a[3]);
    EXPECT_EQ(b[3], a[4]);
    m.set(1, 11); // 5 11 40 50: row 1 is new
    EXPECT_NE(ids()[1], a[0]);
    const std::uint64_t r = m.revision();
    m.set(1, 11); // the same value: nothing happens
    EXPECT_EQ(m.revision(), r);
    b = ids();
    m.move(0, 1, 3); // 11 40 50 5
    EXPECT_EQ(m.rows(), (std::vector<int>{11, 40, 50, 5}));
    EXPECT_EQ(ids(), (std::vector<std::uint64_t>{b[1], b[2], b[3], b[0]}));
    b = ids();
    m.move(2, 2, 0); // 50 5 11 40
    EXPECT_EQ(m.rows(), (std::vector<int>{50, 5, 11, 40}));
    EXPECT_EQ(ids(), (std::vector<std::uint64_t>{b[2], b[3], b[0], b[1]}));
    b = ids();
    m.sort(); // 5 11 40 50
    EXPECT_EQ(ids(), (std::vector<std::uint64_t>{b[1], b[2], b[3], b[0]}));
    b = ids();
    m.replace({5, 99, 40, 50, 60}); // by index: 11 -> 99 is new, 60 is new
    const std::vector<std::uint64_t> c = ids();
    EXPECT_EQ(c[0], b[0]);
    EXPECT_NE(c[1], b[1]);
    EXPECT_EQ(c[2], b[2]);
    EXPECT_EQ(c[3], b[3]);
    m.reset();
    for (std::size_t i = 0; i < m.size(); ++i)
        EXPECT_NE(ids()[i], c[i]);
    m.clear();
    EXPECT_EQ(m.count(), 0u);
    EXPECT_THROW(m.set(0, 1), std::out_of_range);
    EXPECT_THROW(m.insert(1, 1), std::out_of_range);
}

TEST(ListModelRows, KeyedReplaceKeepsRowsByKeyAndValue) {
    VectorModel<LogLine> m{&idOf};
    m.appendRows(lines(1, 4));
    std::vector<std::uint64_t> a;
    for (std::size_t i = 0; i < m.size(); ++i)
        a.push_back(m.rowId(i));
    std::vector<LogLine> next{{4, "line 4"}, {2, "line 2 changed"}, {1, "line 1"}, {7, "line 7"}};
    m.replace(next);
    EXPECT_EQ(m.rowId(0), a[3]); // moved, same value: kept
    EXPECT_NE(m.rowId(1), a[1]); // same key, new value: new
    EXPECT_EQ(m.rowId(2), a[0]);
    EXPECT_NE(m.rowId(3), a[2]);
    ASSERT_TRUE(m.key(0));
    EXPECT_EQ(std::get<std::int64_t>(*m.key(0)), 4);
    VectorModel<LogLine> named{[](const LogLine& l) { return l.text; }};
    named.append({1, "x"});
    EXPECT_EQ(std::get<std::string>(*named.key(0)), "x");
}

// A subclass that changed its rows without the notification gets new ids for all of them (and a new revision).
TEST(ListModelRows, AMissedNotificationRenumbersTheRows) {
    struct Raw final : ListModel {
        std::vector<int> rows{1, 2, 3};
        [[nodiscard]] std::size_t count() const override { return rows.size(); }
        void pushItem(lua::State* s, std::size_t row) const override { lua::pushinteger(s, rows[row]); }
    } m;
    const std::uint64_t first = m.rowId(0);
    const std::uint64_t rev = m.revision();
    m.rows.push_back(4); // no rowsInserted
    EXPECT_NE(m.rowId(0), first);
    EXPECT_GT(m.revision(), rev);
    m.rows.erase(m.rows.begin());
    m.rowsInserted(0, 1); // the wrong notification: renumbered too
    EXPECT_EQ(m.count(), 3u);
    EXPECT_NE(m.rowId(2), 0u);
}

TEST(ListModelRows, NotificationsRequestAFrame) {
    clearFrameRequests();
    VectorModel<int> m;
    EXPECT_TRUE(std::isinf(secondsUntilFrame()));
    m.append(1);
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    clearFrameRequests();
    m.set(0, 2);
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    clearFrameRequests();
}

TEST(ListModelRows, ACapErasesTheOldestRowsInTheSameEdit) {
    VectorModel<int> m;
    m.appendRows({1, 2, 3, 4, 5});
    const std::uint64_t secondId = m.rowId(1);
    m.setMaxRows(5);
    EXPECT_EQ(m.maxRows(), 5u);
    std::uint64_t rev = m.revision();
    EXPECT_EQ(m.size(), 5u); // at the cap: nothing erased

    m.append(6); // the first row goes first (rowsRemoved), then the new one comes (rowsInserted)
    EXPECT_EQ(m.rows(), (std::vector<int>{2, 3, 4, 5, 6}));
    EXPECT_EQ(m.revision(), rev + 2);
    EXPECT_EQ(m.rowId(0), secondId); // the rows that stay keep their identity
    rev = m.revision();
    m.appendRows({7, 8});
    EXPECT_EQ(m.rows(), (std::vector<int>{4, 5, 6, 7, 8}));
    EXPECT_EQ(m.revision(), rev + 2);           // one rowsRemoved for both
    m.appendRows({10, 11, 12, 13, 14, 15, 16}); // alone over the cap: its last 5
    EXPECT_EQ(m.rows(), (std::vector<int>{12, 13, 14, 15, 16}));

    m.insert(5, 100); // inserted, then the oldest erased
    EXPECT_EQ(m.rows(), (std::vector<int>{13, 14, 15, 16, 100}));
    m.insertRows(1, {101, 102});
    EXPECT_EQ(m.rows(), (std::vector<int>{102, 14, 15, 16, 100}));
    m.replace({1, 2, 3, 4, 5, 6, 7, 8}); // the last 5
    EXPECT_EQ(m.rows(), (std::vector<int>{4, 5, 6, 7, 8}));

    rev = m.revision();
    m.setMaxRows(3); // lowering it erases now
    EXPECT_EQ(m.rows(), (std::vector<int>{6, 7, 8}));
    EXPECT_EQ(m.revision(), rev + 1);
    m.setMaxRows(0); // no cap
    m.appendRows({9, 10, 11});
    EXPECT_EQ(m.size(), 6u);
}

// ------------------------------------------------------------ Lua access

TEST_F(ListModelTest, LuaReadsRowsLazilyThroughIndexLenAndIpairs) {
    run("logs = backend.logs");
    check("rawequal(logs, backend.logs)"); // one value per model and state
    check("#logs == 20 and logs:count() == 20");
    EXPECT_EQ(m_backend.logsModel.converted(), 0); // counting converts nothing
    check("logs[1].id == 1 and logs[1].text == 'line 1' and logs[20].id == 20");
    check("logs[0] == nil and logs[21] == nil and logs[1.5] == nil and logs.nope == nil");
    check("logs[2.0].id == 2");
    run("held = logs[3]");
    const int converted = m_backend.logsModel.converted();
    check("logs[3] == held"); // cached while something holds it
    EXPECT_EQ(m_backend.logsModel.converted(), converted);
    run("sum = 0; for i, l in ipairs(logs) do sum = sum + l.id end");
    check("sum == 210");
    check("logs:hasKeys() and logs:key(4) == 4 and logs:key(99) == nil");
    check("not backend.plain:hasKeys() and backend.plain:key(1) == nil");
    check("type(logs:revision()) == 'number'");
    check("tostring(logs) == 'ListModel (20 rows)'");
    check("rawequal(backend:plainRef(), backend.plain)");
    check("not pcall(function() logs[1] = 5 end)"); // read-only: no __newindex on a userdata
    // A changed row reads as a new table; an unchanged one stays the same table.
    run("r4 = logs[4]");
    m_backend.logsModel.set(2, {3, "changed"});
    check("logs[3] ~= held and logs[3].text == 'changed' and logs[4] == r4");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ListModelTest, AModelSingletonIsItsValueAsAGlobal) {
    Logs model{&idOf};
    model.appendRows(lines(1, 3));
    m_h.reset();
    m_h.emplace(test::HeadlessFrame::Options{.builtins = true});
    m_h->registerType<Box>("Box");
    m_h->types().registerSingleton("logs", model);
    run("assert(#logs == 3 and logs[2].text == 'line 2')");
    run(R"(built = 0
        root = Column { Repeater { model = logs, delegate = function(l) built = built + 1; return Box {} end } })");
    m_h->renderFrames("root", 2);
    check("built == 3");
    model.append({4, "line 4"});
    m_h->renderFrame("root");
    check("built == 4");
    const std::string defs = luaTypeDefinitions(m_h->types());
    EXPECT_NE(defs.find("---@type ListModel<LogLine>\nlogs = nil"), std::string::npos) << defs;
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ListModelTest, AMethodMayReturnANewSharedModel) {
    run("q = backend:query(5)");
    check("#q == 5 and q[5].id == 5 and q:hasKeys()");
    EXPECT_FALSE(m_backend.lastQuery.expired()); // Lua keeps it
    run(R"(built = 0
        root = ListView { height = 300, model = function() return q end,
                          delegate = function(l) built = built + 1; return Box {} end })");
    m_h->renderFrames("root", 2);
    check("built == 5");
    run("q = nil; root = nil; collectgarbage(); collectgarbage()");
    EXPECT_TRUE(m_backend.lastQuery.expired()); // and lets it go
    EXPECT_TRUE(luaui::errors().empty());
}

// getmetatable(model) shows only `__imlui_model` (what the prelude reads), not the metamethods. A __gc reached anyway
// (debug.getmetatable) and called by a script, twice or on a userdata that isn't a model (even one given the model
// metatable), finalizes a model value once and leaves it reading as a dead model; a shared model it held is released
// once, and the next push makes a fresh value.
TEST_F(ListModelTest, CallingTheModelFinalizerFromLuaIsHarmless) {
    run(R"lua(
        q = backend:query(3)
        local mt = getmetatable(q)
        assert(type(mt) == "table" and mt.__gc == nil and mt.__index == nil and type(mt.__imlui_model) == "table")
        local gc = debug.getmetatable(q).__gc
        gc(io.stdout)
        local stdoutMeta = debug.getmetatable(io.stdout)
        debug.setmetatable(io.stdout, debug.getmetatable(q))
        gc(io.stdout)
        debug.setmetatable(io.stdout, stdoutMeta)
        io.stdout:write("")
        assert(#q == 3)
        gc(q)
        gc(q)
        assert(#q == 0 and q[1] == nil and tostring(q) == "ListModel (gone)")
        logs = backend.logs
        gc(logs)
        assert(#logs == 0 and #backend.logs == 20) -- a new value for the same model
    )lua");
    EXPECT_TRUE(m_backend.lastQuery.expired()); // the shared model was released by the first call
    run("collectgarbage(); collectgarbage()");
    EXPECT_TRUE(luaui::errors().empty());
}

// A model of an element dies with it: its Lua value then reads as an empty model, and a ListView over it shows none.
TEST_F(ListModelTest, AModelWhoseOwnerIsDestroyedReadsAsEmpty) {
    run("o = Owner {}; m = o:rows()");
    check("#m == 3 and m[2] == 2");
    run(R"(built = 0
        lv = ListView { height = 200, model = function() return m end,
                        delegate = function(i) built = built + 1; return Box {} end })");
    m_h->renderFrames("lv", 2);
    check("built == 3 and lv.__view.built == 3");
    run("o = nil; collectgarbage(); collectgarbage()");
    check("#m == 0 and m[1] == nil and m:count() == 0 and m:revision() == -1 and not m:hasKeys()");
    check("tostring(m) == 'ListModel (gone)'");
    m_h->renderFrames("lv", 2);
    check("lv.__view.built == 0");
    EXPECT_TRUE(luaui::errors().empty());
}

// A singleton's model outlives the Lua state (a hot reload): the next state gets a fresh value with its own cache, and
// the old one left nothing behind in the model.
TEST_F(ListModelTest, AModelOutlivesItsLuaState) {
    run("old = backend.logs; first = old[1]");
    m_backend.logsModel.set(0, {1, "edited"});
    makeHarness(); // the old state is closed: its values are finalized
    m_backend.logsModel.append({21, "after"});
    run("logs = backend.logs");
    check("#logs == 21 and logs[1].text == 'edited' and logs[21].text == 'after'");
    run(R"(built = 0
        root = ListView { height = 300, model = function() return backend.logs end,
                          delegate = function(l) built = built + 1; return Box {} end })");
    m_h->renderFrames("root", 2);
    check("built > 0");
    EXPECT_TRUE(luaui::errors().empty());

    // A model destroyed while a state holds its value: empty, no access to the dead object.
    auto doomed = std::make_unique<VectorModel<int>>();
    doomed->appendRows({1, 2});
    lua::State* state = m_h->state();
    doomed->pushLua(state);
    EXPECT_EQ(ListModel::fromLua(state, -1), doomed.get());
    lua::setglobal(state, "doomedModel");
    run("d = doomedModel; assert(#d == 2 and d[1] == 1)");
    doomed.reset();
    lua::getglobal(state, "doomedModel");
    EXPECT_EQ(ListModel::fromLua(state, -1), nullptr);
    lua::pop(state);
    check("#d == 0 and d[1] == nil");
    lua::pushinteger(state, 1);
    EXPECT_EQ(ListModel::fromLua(state, -1), nullptr); // not a model
    lua::pop(state);
}

// ------------------------------------------------------------ ListView

// Only the rows a ListView builds are converted; counting and scrolling past them converts nothing else.
TEST_F(ListModelTest, AListViewConvertsOnlyTheRowsItBuilds) {
    m_backend.plainModel.clear();
    m_backend.plainModel.appendRows(lines(1, 10'000));
    run(R"(built = 0
        root = ListView { height = 200, spacing = 0, model = function() return backend.plain end,
                          delegate = function(l) built = built + 1; return Box { h = 20 } end })");
    m_h->renderFrames("root", 3);
    check("built >= 10 and built <= 30");
    EXPECT_LE(m_backend.plainModel.converted(), 30);
    // Steady frames: nothing changes, nothing is converted or compared again.
    const int converted = m_backend.plainModel.converted();
    m_h->renderFrames("root", 5);
    EXPECT_EQ(m_backend.plainModel.converted(), converted);
    EXPECT_TRUE(luaui::errors().empty());
}

// The same with keys, also for a model small enough that every key is counted after a change: keys are read without
// converting rows, and steady frames read neither.
TEST_F(ListModelTest, AKeyedListViewConvertsOnlyTheRowsItBuilds) {
    m_backend.logsModel.clear();
    m_backend.logsModel.appendRows(lines(1, 200));
    run(R"(built = 0
        root = ListView { height = 200, spacing = 0, model = backend.logs,
                          delegate = function(l) built = built + 1; return Box { h = 20 } end })");
    m_h->renderFrames("root", 3);
    check("built >= 10 and built <= 30");
    const int converted = m_backend.logsModel.converted();
    EXPECT_LE(converted, 30);
    const int keyed = m_backend.logsModel.keyed();
    m_h->renderFrames("root", 5);
    EXPECT_EQ(m_backend.logsModel.converted(), converted);
    EXPECT_EQ(m_backend.logsModel.keyed(), keyed);
    m_backend.logsModel.set(150, {151, "out of view"}); // every key is counted, no row converted
    m_h->renderFrame("root");
    EXPECT_GT(m_backend.logsModel.keyed(), keyed + 150);
    EXPECT_EQ(m_backend.logsModel.converted(), converted);
    check("built <= 30");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ListModelTest, AListViewWithoutKeysRebuildsTheChangedRowsAndShiftsByIndex) {
    run(R"(built = 0
        made = {}
        root = ListView { height = 600, spacing = 0, model = function() return backend.plain end,
                          delegate = function(l, i) built = built + 1; local b = Box { h = 20 }
                                                    made[l.id] = { box = b, item = l }; return b end })");
    m_h->renderFrames("root", 3);
    check("built == 20");
    m_backend.plainModel.set(4, {5, "five"});
    m_h->renderFrame("root");
    check("built == 21 and made[5].item.text == 'five'"); // only row 5
    m_backend.plainModel.append({21, "end"});
    m_h->renderFrame("root");
    check("built == 22");
    run("before = built");
    // Without keys, delegates are kept per index: an insertion at the top rebuilds the indices whose item moved.
    m_backend.plainModel.insert(0, {0, "top"});
    m_h->renderFrame("root");
    check("built == before + 22 and root.__view.cache[1].item.id == 0 and root.__view.cache[2].item.id == 1");
    m_backend.plainModel.erase(0);
    m_h->renderFrame("root");
    check("root.__view.cache[1].item.id == 1 and root.__view.built == 21");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ListModelTest, AListViewWithModelKeysKeepsDelegatesAcrossInsertsMovesAndSorts) {
    run(R"(built = 0
        made = {}
        root = ListView { height = 600, spacing = 0, model = function() return backend.logs end,
                          delegate = function(l) built = built + 1; local b = Box { h = 20 }; made[l.id] = b
                                               return b end })");
    m_h->renderFrames("root", 3);
    check("built == 20");
    run("first = made[1]");
    m_backend.logsModel.insert(0, {0, "top"}); // one new delegate, the others kept
    m_h->renderFrame("root");
    check("built == 21 and made[1] == first");
    m_backend.logsModel.erase(0);
    m_h->renderFrame("root");
    check("built == 21");
    m_backend.logsModel.move(0, 1, 10);
    m_h->renderFrame("root");
    check("built == 21");
    m_backend.logsModel.sort([](const LogLine& a, const LogLine& b) { return a.id > b.id; });
    m_h->renderFrame("root");
    check("built == 21 and made[1] == first");
    m_backend.logsModel.set(3, {m_backend.logsModel.at(3).id, "changed"}); // a changed row: rebuilt
    m_h->renderFrame("root");
    check("built == 22");
    std::vector<LogLine> same = m_backend.logsModel.rows();
    same.push_back({50, "new"});
    m_backend.logsModel.replace(same); // unchanged rows keep their delegates
    m_h->renderFrame("root");
    check("built == 23 and made[1] == first");
    m_backend.logsModel.reset(); // everything is new
    m_h->renderFrame("root");
    check("built == 44");
    EXPECT_TRUE(luaui::errors().empty());
}

// A Lua `key` overrides the model's keys; a changed row is still rebuilt.
TEST_F(ListModelTest, ALuaKeyOverridesTheModelsKeys) {
    run(R"(built, keyed = 0, 0
        root = ListView { height = 600, spacing = 0, model = function() return backend.logs end,
                          key = function(l) keyed = keyed + 1; return l.text end,
                          delegate = function(l) built = built + 1; return Box { h = 20 } end })");
    m_h->renderFrames("root", 2);
    check("built == 20 and keyed > 0");
    m_backend.logsModel.set(0, {1, "renamed"});
    m_h->renderFrame("root");
    check("built == 21");
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------ Repeater

TEST_F(ListModelTest, ARepeaterRebuildsOnlyTheChangedRows) {
    run(R"(built, plainBuilt = 0, 0
        made = {}
        rep = Repeater { model = backend.logs, delegate = function(l) built = built + 1; made[l.id] = Box {}
                                                                     return made[l.id] end }
        plain = Repeater { model = function() return backend.plain end,
                           delegate = function(l) plainBuilt = plainBuilt + 1; return Box {} end }
        root = Column { rep, plain })");
    m_h->renderFrames("root", 2);
    check("built == 20 and plainBuilt == 20");
    // Unchanged frames: nothing is read again (the revision didn't move).
    const int converted = m_backend.logsModel.converted();
    m_h->renderFrames("root", 3);
    EXPECT_EQ(m_backend.logsModel.converted(), converted);
    run("first = made[1]");
    m_backend.logsModel.insert(0, {0, "top"});
    m_backend.plainModel.insert(0, {0, "top"});
    m_h->renderFrame("root");
    check("built == 21 and rep.__list[2] == first and #rep.__list == 21");
    check("plainBuilt == 41"); // by index: every index's item moved
    m_backend.logsModel.set(5, {m_backend.logsModel.at(5).id, "x"});
    m_backend.plainModel.set(5, {m_backend.plainModel.at(5).id, "x"});
    m_h->renderFrame("root");
    check("built == 22 and plainBuilt == 42");
    m_backend.logsModel.sort([](const LogLine& a, const LogLine& b) { return a.id > b.id; });
    m_h->renderFrame("root");
    check("built == 22 and rep.__list[#rep.__list - 1] == first");
    m_backend.logsModel.clear();
    m_h->renderFrame("root");
    check("#rep.__list == 0");
    EXPECT_TRUE(luaui::errors().empty());
}

// A capped tail: the oldest rows go in the same step as the new ones arrive, so a frame never shows more than the cap.
// With keys, the rows that stay keep their delegates and only the new ones are built; without keys, every index whose
// row changed is rebuilt.
TEST_F(ListModelTest, ACappedAppenderKeepsTheDelegatesOfTheRowsThatStay) {
    run(R"(built, plainBuilt = 0, 0
        made = {}
        keyed = ListView { height = 600, spacing = 0, model = function() return backend.logs end,
                           delegate = function(l) built = built + 1; made[l.id] = Box { h = 20 }; return made[l.id] end }
        plain = ListView { height = 600, spacing = 0, model = function() return backend.plain end,
                           delegate = function(l) plainBuilt = plainBuilt + 1; return Box { h = 20 } end })");
    m_h->renderFrames("keyed", 3);
    m_h->renderFrames("plain", 3);
    check("built == 20 and plainBuilt == 20");
    run("kept = made[4]");
    m_backend.logsModel.setMaxRows(20);
    m_backend.plainModel.setMaxRows(20);
    std::vector<std::pair<std::size_t, std::size_t>> applied;
    const auto keyed = m_backend.logsModel.appender([&](std::size_t appended, std::size_t erased) {
        applied.emplace_back(appended, erased);
        EXPECT_EQ(m_backend.logsModel.size(), 20u); // already capped when the stats run
    });
    const auto plain = m_backend.plainModel.appender();
    std::thread{[keyed, plain] {
        for (const LogLine& line : lines(21, 23)) {
            keyed.append(line);
            plain.append(line);
        }
    }}.join();
    EXPECT_EQ(runPosted(), 2u); // one apply per batch
    EXPECT_EQ(applied, (std::vector<std::pair<std::size_t, std::size_t>>{{3, 3}}));
    m_h->renderFrame("keyed");
    m_h->renderFrame("plain");
    check("#backend.logs == 20 and backend.logs[1].id == 4 and backend.logs[20].id == 23");
    check("built == 23 and made[4] == kept");           // three new delegates, the 17 that stay kept
    check("#backend.plain == 20 and plainBuilt == 40"); // by index: every index's row changed
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------ safety

// A delegate (or a handler) may edit the model it shows: no error, no stale row, and the next frame catches up.
TEST_F(ListModelTest, ADelegateMayEditTheModelItShows) {
    run(R"(built = 0
        root = Column {
            ListView { id = "lv", height = 300, spacing = 0, model = function() return backend.logs end,
                       delegate = function(l)
                           built = built + 1
                           if l.id == 2 then backend:dropFirst(); backend:dropFirst() end
                           if l.id == 4 then backend:add("from a delegate") end
                           return Box { h = 20 }
                       end },
            Repeater { model = backend.plain, delegate = function(l)
                if l.id == 3 then backend:dropFirst() end
                return Box { h = 1 }
            end },
        })");
    m_h->renderFrames("root", 4);
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(m_backend.logsModel.size(), 20u - 3u + 1u);
}

// A delegate that raises leaves a Repeater's list unfinished: the next frame finishes it, though the model didn't
// change.
TEST_F(ListModelTest, ARepeaterFinishesAListADelegateErrorInterrupted) {
    run(R"(failed = false
        rep = Repeater { model = backend.plain, delegate = function(l)
            if l.id == 3 and not failed then failed = true; error("once") end
            return Box {}
        end }
        root = Column { rep })");
    m_h->renderFrames("root", 3);
    EXPECT_EQ(luaui::errors().size(), 1u);
    check("#rep.__list == 20");
}

// ------------------------------------------------------------ threads

TEST_F(ListModelTest, AnAppenderBatchesRowsFromWorkerThreads) {
    test::PostWaiter posts;
    VectorModel<int> m;
    const std::uint64_t rev = m.revision();
    const VectorModel<int>::Appender appender = m.appender();
    std::thread{[appender] {
        for (int i = 0; i < 1000; ++i)
            appender.append(i);
        appender.appendRows({1000, 1001});
    }}.join();
    EXPECT_EQ(m.size(), 0u);    // applied on the main thread
    EXPECT_EQ(runPosted(), 1u); // one apply for all of them
    ASSERT_EQ(m.size(), 1002u);
    EXPECT_EQ(m.revision(), rev + 1); // one rowsInserted
    for (int i = 0; i < 1002; ++i)
        ASSERT_EQ(m.at(static_cast<std::size_t>(i)), i);

    // Concurrently with the main loop: everything arrives, in order per thread.
    std::thread worker{[appender] {
        for (int i = 0; i < 5000; ++i)
            appender.append(2000 + i);
    }};
    ASSERT_TRUE(posts.runUntil([&] { return m.size() == 6002u; }));
    worker.join();
    for (std::size_t i = 1002; i < 6002; ++i)
        ASSERT_EQ(m.at(i), static_cast<int>(i - 1002 + 2000));
}

TEST(ListModelThreads, AnAppenderWithOnAppliedHonorsTheCapAndReportsEachApply) {
    clearPosted();
    VectorModel<int> m;
    m.setMaxRows(10);
    m.appendRows({0, 1, 2, 3, 4, 5, 6, 7});
    std::vector<std::pair<std::size_t, std::size_t>> applied;
    std::vector<std::size_t> sizes;
    const VectorModel<int>::Appender out = m.appender([&](std::size_t appended, std::size_t erased) {
        applied.emplace_back(appended, erased);
        sizes.push_back(m.size());
    });
    const std::uint64_t rev = m.revision();
    std::thread{[out] {
        for (int i = 100; i < 105; ++i)
            out.append(i);
    }}.join();
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(applied, (std::vector<std::pair<std::size_t, std::size_t>>{{5, 3}}));
    EXPECT_EQ(sizes, std::vector<std::size_t>{10});
    EXPECT_EQ(m.rows(), (std::vector<int>{3, 4, 5, 6, 7, 100, 101, 102, 103, 104}));
    EXPECT_EQ(m.revision(), rev + 2); // one rowsRemoved, one rowsInserted

    // A batch alone over the cap: its last 10 rows land, all the old rows and its first 15 count as erased.
    std::vector<int> many(25);
    std::iota(many.begin(), many.end(), 200);
    std::thread{[out, many] { out.appendRows(many); }}.join();
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(applied.back(), (std::pair<std::size_t, std::size_t>{25, 25}));
    EXPECT_EQ(m.at(0), 215);

    // The shared appender() has a batch of its own, without the callback.
    const auto shared = m.appender();
    shared.append(1);
    out.append(2);
    EXPECT_EQ(runPosted(), 2u);
    EXPECT_EQ(applied.size(), 3u);
    EXPECT_EQ(m.size(), 10u);
}

// Rows waiting for the apply are trimmed to the cap as they come (the apply would erase them anyway), with the cap
// set after the appender was made; the apply still reports every row that arrived and went.
TEST(ListModelThreads, AnAppenderTrimsWaitingRowsToTheCap) {
    clearPosted();
    VectorModel<int> m;
    std::vector<std::pair<std::size_t, std::size_t>> applied;
    const auto out =
        m.appender([&](std::size_t appended, std::size_t erased) { applied.emplace_back(appended, erased); });
    m.setMaxRows(10);
    m.appendRows({-2, -1});
    std::thread{[out] {
        for (int i = 0; i < 1000; ++i)
            out.append(i);
        out.appendRows(std::vector<int>(50, 7));
    }}.join();
    EXPECT_EQ(runPosted(), 1u);
    ASSERT_EQ(applied.size(), 1u);
    EXPECT_EQ(applied[0], (std::pair<std::size_t, std::size_t>{1050, 1042})); // 1050 rows came, 2 old + 1040 new went
    EXPECT_EQ(m.rows(), std::vector<int>(10, 7));

    // Without a cap nothing is trimmed.
    m.setMaxRows(0);
    std::thread{[out] {
        for (int i = 0; i < 100; ++i)
            out.append(i);
    }}.join();
    EXPECT_EQ(runPosted(), 1u);
    EXPECT_EQ(applied.back(), (std::pair<std::size_t, std::size_t>{100, 0}));
    EXPECT_EQ(m.size(), 110u);
}

// A worker appends as fast as it can while the main loop applies: after every apply (and in its callback) the model
// is within the cap, nothing is lost, and the counts add up.
TEST(ListModelThreads, ACappedTailNeverExceedsItsCap) {
    test::PostWaiter posts;
    VectorModel<int> m;
    m.setMaxRows(100);
    std::size_t total = 0;
    std::size_t erased = 0;
    std::size_t largest = 0;
    const auto out = m.appender([&](std::size_t appended, std::size_t gone) {
        total += appended;
        erased += gone;
        largest = std::max(largest, m.size());
        EXPECT_EQ(m.size(), total - erased);
    });
    constexpr int rows = 20'000;
    std::thread worker{[out] {
        for (int i = 0; i < rows; ++i)
            out.append(i);
    }};
    ASSERT_TRUE(posts.runUntil([&] {
        largest = std::max(largest, m.size());
        return total == static_cast<std::size_t>(rows);
    }));
    worker.join();
    EXPECT_LE(largest, 100u);
    ASSERT_EQ(m.size(), 100u);
    EXPECT_EQ(total - erased, 100u);
    for (std::size_t i = 0; i < 100; ++i)
        ASSERT_EQ(m.at(i), static_cast<int>(rows - 100 + static_cast<int>(i)));
}

TEST(ListModelThreads, AnAppenderWithOnAppliedOutlivesItsModel) {
    clearPosted();
    auto token = std::make_shared<int>(0);
    const std::weak_ptr<int> captured = token;
    int calls = 0;
    std::optional<VectorModel<int>::Appender> appender;
    {
        auto m = std::make_unique<VectorModel<int>>();
        appender = m->appender([&calls, token = std::move(token)](std::size_t, std::size_t) { ++calls; });
        appender->append(1); // queued
    }
    appender->append(2); // the model is gone: dropped
    runPosted();         // the queued apply doesn't run
    EXPECT_EQ(calls, 0);
    EXPECT_FALSE(captured.expired()); // held by the appender's batch
    appender.reset();
    clearPosted();
    EXPECT_TRUE(captured.expired());
}

TEST(ListModelThreads, AnAppenderOutlivesItsModel) {
    clearPosted();
    VectorModel<int>::Appender appender;
    appender.append(1); // empty: dropped
    {
        auto m = std::make_unique<VectorModel<int>>();
        appender = m->appender();
        appender.append(2); // queued
    }
    appender.append(3); // the model is gone: dropped
    runPosted();        // the queued apply doesn't run
    clearPosted();
}

// The pattern of the docs: a singleton fills its model from runAsync's done.
TEST_F(ListModelTest, ASingletonFillsItsModelFromRunAsync) {
    test::PostWaiter posts;
    m_backend.plainModel.clear();
    bool done = false;
    m_backend.runAsync([] { return lines(1, 500); },
                       [this, &done](std::vector<LogLine> rows) {
                           m_backend.plainModel.replace(std::move(rows));
                           done = true;
                       });
    ASSERT_TRUE(posts.runUntil([&] { return done; }));
    run("assert(#backend.plain == 500 and backend.plain[500].text == 'line 500')");
    shutdownBackground(std::chrono::seconds{10});
}

// The fill recipe: runAsyncThen builds the rows on the pool, applies them on the main thread and resolves the future
// with a report, so Lua converts the report, never the rows.
TEST_F(ListModelTest, AFillResolvesWithAReportAndLeavesTheRowsInCxx) {
    test::PostWaiter posts;
    m_backend.plainModel.clear();
    const int converted = m_backend.plainModel.converted();
    run("task = async(function() return await(backend:fill(500)) end)()");
    ASSERT_TRUE(posts.runUntil([&] { return m_h->run("assert(task.state ~= 'pending')").empty(); }));
    check("task.state == 'done' and task.value.rows == 500 and task.value.onMainThread");
    check("#backend.plain == 500 and backend.plain[500].text == 'line 500'");
    EXPECT_EQ(m_backend.plainModel.converted(), converted + 1); // only the row the check read
    EXPECT_TRUE(luaui::errors().empty());
    shutdownBackground(std::chrono::seconds{10});
}

// ------------------------------------------------------------ Lua definitions

TEST_F(ListModelTest, TheDefinitionsTypeModelsAsListModelOfTheirRows) {
    const std::string defs = luaTypeDefinitions(m_h->types());
    EXPECT_NE(defs.find("---@field logs ListModel<LogLine>"), std::string::npos) << defs;
    EXPECT_NE(defs.find("---@field query (fun(self: backend, n: integer): ListModel<LogLine>?)"), std::string::npos)
        << defs;
    EXPECT_NE(defs.find("---@class LogLine"), std::string::npos);
}
