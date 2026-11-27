// The Fluent containers, messages, tables, charts and chrome (group C2 of the Fluent controls): a Card keeps
// Material's sizes and card stack with Fluent's colors, also when an error cuts its content short; a Dialog opens,
// closes on Escape and leaves its stacks balanced; the snackbar's action and close button work; the tooltip, divider,
// table, chart and the chrome hooks give Fluent's values; nothing reports a Lua error.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.skin;
import imlui.skin.fluent.card;
import imlui.skin.fluent.common;
import imlui.skin.fluent.dialog;
import imlui.skin.fluent.snackbar;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.theme;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.widgets.card;
import imlui.widgets.chart;
import imlui.widgets.common;
import imlui.widgets.snackbar;
import imlui.widgets.surface;
import imlui.widgets.text;
import imlui.test.headless_frame;
import imlui.widgets.top_app_bar;

using namespace imlui;

namespace {

/// Records where it renders: a 1px item.
class Spot final : public Element {
public:
    static void declare(TypeBuilder<Spot>&) {}
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override { return ImVec2{1.0f, 1.0f}; }
    void render(Frame&) override {
        m_pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy({1.0f, 1.0f});
    }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }

private:
    ImVec2 m_pos{};
};

/// A 40px high block; records the surface it renders on and how many cards are open around it; with `boom` set it
/// throws instead, cutting the render pass short.
class Block final : public Element {
public:
    static void declare(TypeBuilder<Block>& type) { type.property("boom", &Block::boom, &Block::setBoom); }
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override { return ImVec2{10.0f, 40.0f}; }
    void render(Frame&) override {
        if (m_boom) throw std::runtime_error("block exploded");
        m_surface = md::surfaceColor();
        m_cards = md::cardDepth();
        ++m_renders;
        ImGui::Dummy({10.0f, 40.0f});
    }
    [[nodiscard]] bool boom() const { return m_boom; }
    void setBoom(bool on) { m_boom = on; }
    [[nodiscard]] Color surface() const { return m_surface; }
    [[nodiscard]] std::size_t cards() const { return m_cards; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    bool m_boom = false;
    Color m_surface{};
    std::size_t m_cards = 99;
    int m_renders = 0;
};

/// Runs `body` while it renders: what needs an ImGui frame and window (measuring text, resolving widths).
class Call final : public Element {
public:
    static void declare(TypeBuilder<Call>&) {}
    void render(Frame&) override {
        if (m_body) m_body();
        ImGui::Dummy({1.0f, 1.0f});
    }
    static inline std::function<void()> m_body;
};

bool hasError(std::string_view part) {
    return std::ranges::any_of(luaui::errors(),
                               [&](const std::string& e) { return e.find(part) != std::string::npos; });
}

class FluentContainersTest : public ::testing::Test {
protected:
    FluentContainersTest() {
        m_h.registerType<Spot>("Spot");
        m_h.registerType<Block>("Block");
        m_h.registerType<Call>("Call");
    }
    ~FluentContainersTest() override {
        Call::m_body = nullptr;
        setTheme(Theme{});
        applyPendingTheme();
    }

    void SetUp() override { applyTheme(R"(theme { design = "fluent" })"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void applyTheme(std::string_view code) {
        run(code);
        ASSERT_TRUE(applyPendingTheme());
    }
    Spot* spot(std::string_view name) { return m_h.instance<Spot>(name); }
    Block* block(std::string_view name) { return m_h.instance<Block>(name); }
    /// `element`'s laid-out size between Spots in a Row and in a Column.
    ImVec2 laidOut(std::string_view element) {
        run(std::format(R"(
            a, b, c, d = Spot {{}}, Spot {{}}, Spot {{}}, Spot {{}}
            root = Column {{ spacing = 0, Row {{ spacing = 0, a, {0}, b }}, c, {0}, d }})",
                        element));
        m_h.renderFrames("root", 3);
        return {spot("b")->pos().x - spot("a")->pos().x - 1.0f, spot("d")->pos().y - spot("c")->pos().y - 1.0f};
    }
    /// Runs `body` inside a rendered frame.
    void inFrame(std::function<void()> body) {
        Call::m_body = std::move(body);
        run("call = Call {}");
        m_h.renderFrame("call");
        Call::m_body = nullptr;
    }
    [[nodiscard]] static const FluentSkin& fluentSkin() {
        const auto* f = dynamic_cast<const FluentSkin*>(&skin());
        EXPECT_NE(f, nullptr);
        return *f;
    }
    /// The active window whose name starts with `prefix`, or nullptr.
    static const ImGuiWindow* activeWindow(std::string_view prefix) {
        for (const ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
            if (std::string_view{w->Name}.starts_with(prefix) && (w->Active || w->WasActive)) return w;
        return nullptr;
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// ------------------------------------------------------------------ Card

// The card's size is md::cardSize's: the content plus the padding on both sides; the tokens change its look (the
// corner radius), not its size.
TEST_F(FluentContainersTest, CardSizeFollowsThePadding) {
    const ImVec2 card = laidOut("Card { width = 200, Block {} }");
    EXPECT_FLOAT_EQ(card.x, 200.0f);
    EXPECT_FLOAT_EQ(card.y, 40.0f + 2.0f * dp(16));
    EXPECT_FLOAT_EQ(laidOut("Card { width = 200, padding = 4, Block {} }").y, 40.0f + 2.0f * dp(4));
    EXPECT_FLOAT_EQ(laidOut("Card { width = 200, height = 90, Block {} }").y, 90.0f);
    for (const char* variant : {"filled", "outlined"})
        EXPECT_FLOAT_EQ(laidOut(std::format(R"(Card {{ width = 200, variant = "{}", Block {{}} }})", variant)).y,
                        card.y)
            << variant;
    ImVec2 sized{};
    inFrame([&] { sized = fluentSkin().cardSize(200.0f, 16.0f, 0.0f, 40.0f); });
    EXPECT_FLOAT_EQ(sized.x, card.x);
    EXPECT_FLOAT_EQ(sized.y, card.y);

    applyTheme(R"(theme { design = "fluent", tokens = { density = 2, controlHeight = 50, overlayRadius = 2 } })");
    EXPECT_FLOAT_EQ(laidOut("Card { width = 200, Block {} }").y, card.y);
    // An unset width fills, as under Material.
    EXPECT_GT(laidOut("Card { Block {} }").x, 500.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// elevated: card fill and stroke; filled: the layer, no stroke; outlined: the surface and a divider stroke. A
// translucent fill is pushed as the surface painted over what it is on, and a `color` replaces it.
TEST_F(FluentContainersTest, CardVariantsMapToFluentFills) {
    const fluent::Look look = fluentSkin().look();
    const fluent::Colors& c = look.colors;
    using md::CardVariant;
    EXPECT_EQ(fluent::cardColors(look, CardVariant::Elevated).stroke, c.cardStroke);
    EXPECT_EQ(fluent::cardColors(look, CardVariant::Filled).stroke.a, 0.0f);
    EXPECT_EQ(fluent::cardColors(look, CardVariant::Outlined).stroke, c.divider);
    for (const CardVariant v : {CardVariant::Elevated, CardVariant::Filled, CardVariant::Outlined})
        EXPECT_FLOAT_EQ(fluent::cardColors(look, v).fill.a, 1.0f);

    run(R"(
        e, f, o, x = Block {}, Block {}, Block {}, Block {}
        root = Column { Card { e }, Card { variant = "filled", f }, Card { variant = "outlined", o },
                        Card { color = "#ff0000", x } })");
    m_h.renderFrames("root", 2);
    const Color bg = Color::from(ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
    EXPECT_EQ(block("e")->surface(), overlay(bg, c.card.withAlpha(1.0f), c.card.a));
    EXPECT_EQ(block("f")->surface(), c.layer);
    EXPECT_EQ(block("o")->surface(), bg);
    EXPECT_EQ(block("x")->surface(), (Color{1, 0, 0, 1}));
    EXPECT_EQ(block("e")->cards(), 1u);
    EXPECT_TRUE(luaui::errors().empty());
}

// An error inside a Fluent card cuts the render past endCard(): the card stack is md's, so the renderer's recovery
// drops the card and its surface like under Material.
TEST_F(FluentContainersTest, AnErrorInsideACardLeavesNoCardOrSurfaceBehind) {
    run(R"(
        boom = Block { boom = true } inside = Block {} outside = Block {}
        root = Column { Card { color = "#ff0000", inside, Card { boom } }, outside })");
    m_h.renderFrame("root");
    EXPECT_TRUE(hasError("block exploded"));
    EXPECT_EQ(md::surfaceDepth(), 0u);
    EXPECT_EQ(md::cardDepth(), 0u);
    luaui::clearErrors();
    run("boom.boom = false");
    m_h.renderFrames("root", 2);
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(block("inside")->surface(), (Color{1, 0, 0, 1}));
    EXPECT_EQ(block("inside")->cards(), 1u);
    EXPECT_EQ(block("boom")->cards(), 2u);
    EXPECT_EQ(block("outside")->cards(), 0u);
    EXPECT_EQ(block("outside")->surface(), Color::from(ImGui::GetStyle().Colors[ImGuiCol_WindowBg]));
}

// ------------------------------------------------------------------ Dialog

TEST_F(FluentContainersTest, DialogOpensClosesAndKeepsItsStacksBalanced) {
    run(R"(
        s = state { open = false }
        closed = 0
        inner = Block {}
        root = Column { Dialog { title = "Delete?", icon = "delete", open = bind(s, "open"),
                                 onClosed = function() closed = closed + 1 end, inner } })");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(block("inner")->renders(), 0);
    run("s.open = true");
    m_h.renderFrames("root", 3);
    EXPECT_GE(block("inner")->renders(), 2);
    const fluent::Colors& c = fluentSkin().colors();
    EXPECT_EQ(block("inner")->surface(), c.layer);
    EXPECT_EQ(md::surfaceDepth(), 0u); // popped by endDialog
    const ImGuiWindow* dlg = activeWindow("Delete?");
    ASSERT_NE(dlg, nullptr);
    EXPECT_FLOAT_EQ(dlg->WindowRounding, fluent::overlayRadius(fluentSkin().look()));
    EXPECT_GE(dlg->Size.x, dp(fluent::dialogMinWidth));
    EXPECT_LE(dlg->Size.x, dp(fluent::dialogMaxWidth));

    // Escape closes it: open = false is written back, onClosed runs.
    run(R"(ui.native.injectKey("Escape", true); ui.native.injectKey("Escape", false))");
    m_h.renderFrames("root", 3);
    run("assert(s.open == false and closed == 1, 'closed ' .. closed)");
    const int shown = block("inner")->renders();
    m_h.renderFrames("root", 2);
    EXPECT_EQ(block("inner")->renders(), shown);

    // An error inside the open dialog: the stacks are unwound, the dialog still works afterwards.
    run("inner.boom = true; s.open = true");
    m_h.renderFrames("root", 2);
    EXPECT_TRUE(hasError("block exploded"));
    EXPECT_EQ(md::surfaceDepth(), 0u);
    EXPECT_EQ(md::cardDepth(), 0u);
    luaui::clearErrors();
    run("inner.boom = false");
    m_h.renderFrames("root", 3);
    EXPECT_EQ(block("inner")->surface(), c.layer);
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ Snackbar

TEST_F(FluentContainersTest, SnackbarActionAndCloseWork) {
    // The action and the close button move shownAt back by the duration, to start the fade: the clock must be past
    // it (a negative shownAt hides the bar at once).
    m_h.advanceTime(1000.0);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    fluent::SnackbarLayout l;
    inFrame([&] { l = fluent::snackbarLayout(fluentSkin().look(), "Saved", "Undo", true, display.x); });
    EXPECT_FLOAT_EQ(l.height, dp(48));
    EXPECT_GE(l.width, dp(344));
    EXPECT_FLOAT_EQ(l.actionSize.y, dp(32)); // a standard Fluent button
    EXPECT_FLOAT_EQ(l.closeSize.x, dp(32));
    EXPECT_GT(l.closePos.x, l.actionPos.x + l.actionSize.x);
    const ImVec2 bar{(display.x - l.width) * 0.5f, display.y - dp(16) - l.height};

    // Each bar is a new instance, so a new window, on top (the harness's window takes the front back when a focused
    // bar hides).
    for (const bool close : {true, false}) {
        run(R"(
            acted = 0
            snack = Snackbar { text = "Saved", actionText = "Undo", closable = true, duration = 60000,
                               onAction = function() acted = acted + 1 end }
            snack.shownAt = ui.time()
            before = snack.shownAt)");
        m_h.renderFrames("snack", 20); // past the slide-in
        const ImVec2 at = close ? ImVec2{bar.x + l.closePos.x + l.closeSize.x * 0.5f, bar.y + l.closePos.y + 16.0f}
                                : ImVec2{bar.x + l.actionPos.x + l.actionSize.x * 0.5f, bar.y + l.actionPos.y + 16.0f};
        m_h.click("snack", at.x, at.y, 2);
        // Either starts the fade (shownAt moves back); only the action fires onAction.
        run(std::format("assert(acted == {} and snack.shownAt < before, 'acted ' .. acted .. ', shownAt ' .. "
                        "snack.shownAt)",
                        close ? 0 : 1));
    }
    EXPECT_EQ(md::surfaceDepth(), 0u);
    EXPECT_TRUE(luaui::errors().empty());
}

// A long message wraps like md::snackbar's: the bar grows by 20dp a line.
TEST_F(FluentContainersTest, ALongSnackbarMessageWraps) {
    const std::string words = [] {
        std::string s;
        for (int i = 0; i < 60; ++i)
            s += "word ";
        return s;
    }();
    fluent::SnackbarLayout l;
    inFrame([&] { l = fluent::snackbarLayout(fluentSkin().look(), words, "Undo", false, 800.0f); });
    ASSERT_GT(l.lines.size(), 1u);
    EXPECT_FLOAT_EQ(l.width, md::snackbarWidth(dp(10000), 800.0f));
    EXPECT_FLOAT_EQ(l.height, static_cast<float>(l.lines.size()) * dp(20) + 2.0f * dp(14));
}

// ------------------------------------------------------------------ Tooltip, divider, table, chart, chrome

TEST_F(FluentContainersTest, TooltipIsAFlyout) {
    run(R"(a = Spot {}; root = Column { spacing = 0, a, Button { "Hover", width = 100, tooltip = "A tip" } })");
    m_h.renderFrames("root", 2);
    const ImVec2 at = spot("a")->pos();
    run(std::format("ui.native.injectMouse({}, {})", at.x + 50.0f, at.y + 1.0f + dp(16)));
    for (int i = 0; i < 90 && !activeWindow("##Tooltip"); ++i)
        m_h.renderFrame("root");
    const ImGuiWindow* tip = activeWindow("##Tooltip");
    ASSERT_NE(tip, nullptr);
    EXPECT_FLOAT_EQ(tip->WindowRounding, fluent::controlRadius(fluentSkin().look()));
    EXPECT_FLOAT_EQ(tip->WindowBorderSize, fluent::strokeWidth(fluentSkin().look()));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentContainersTest, DividerTableAndChartKeepTheirSizes) {
    EXPECT_FLOAT_EQ(laidOut("Divider { width = 120 }").y, dp(1));
    inFrame([&] {
        EXPECT_FLOAT_EQ(fluentSkin().dividerSize(120.0f).y, md::dividerSize(120.0f).y);
        md::ChartOptions o;
        o.width = 0.0f;
        EXPECT_FLOAT_EQ(fluentSkin().chartSize(o).x, dp(320));
        EXPECT_FLOAT_EQ(fluentSkin().chartSize(o).y, dp(160));
    });
    // Everything renders under Fluent, hovered too (the chart's readout).
    run(R"(
        root = Column {
            Divider { inset = 8 }, Separator { text = "Section" }, Link { text = "Docs", url = "" },
            TopAppBar { title = "Title", variant = "large" },
            Plot { values = { 1, 4, 2, 0/0, 3 }, title = "Lines", width = 300 },
            Plot { values = { 1, 4, 2 }, kind = "bars", width = 300 },
            Table { columns = { "A", "B" }, rows = { { 1, 2 }, { 3, 4 } }, currentRow = 1 },
            Table { columns = { "A" }, rows = { { "x" } }, outlined = false } })");
    m_h.renderFrames("root", 3);
    for (float y = 0.0f; y < 600.0f; y += 40.0f) {
        run(std::format("ui.native.injectMouse(150, {})", y));
        m_h.renderFrame("root");
    }
    EXPECT_EQ(md::surfaceDepth(), 0u);
    EXPECT_EQ(md::cardDepth(), 0u);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentContainersTest, ChromeHooksGiveFluentValues) {
    const FluentSkin& f = fluentSkin();
    const fluent::Look look = f.look();
    const fluent::Colors& c = look.colors;
    const Skin::TableChrome table = f.tableChrome();
    EXPECT_EQ(table.border, c.divider);
    EXPECT_EQ(table.header, c.subtleHover);
    EXPECT_EQ(table.selectedText, c.text);
    EXPECT_FLOAT_EQ(table.cellPaddingX, dp(12));
    EXPECT_FLOAT_EQ(table.headerHeight, dp(32));
    using md::TopAppBarVariant;
    for (const auto& [name, variant, height] :
         {std::tuple{"small", TopAppBarVariant::Small, 48.0f}, std::tuple{"center", TopAppBarVariant::Center, 48.0f},
          std::tuple{"medium", TopAppBarVariant::Medium, 104.0f},
          std::tuple{"large", TopAppBarVariant::Large, 128.0f}}) {
        const Skin::TopAppBarChrome bar = f.topAppBarChrome(variant);
        EXPECT_FLOAT_EQ(bar.rowHeight, dp(48)) << name;
        EXPECT_FLOAT_EQ(bar.height, dp(height)) << name;
        EXPECT_EQ(bar.title, c.text);
    }
    EXPECT_EQ(f.separatorTextColor(), c.textSecondary);
    EXPECT_EQ(f.linkColor(), std::optional<Color>{c.accentText});
    EXPECT_FLOAT_EQ(laidOut(R"(TopAppBar { title = "T", width = 300 })").y, dp(48));

    // The tokens reach the table: padding and header height.
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 40, paddingX = 20, density = 1.5 } })");
    EXPECT_FLOAT_EQ(fluentSkin().tableChrome().cellPaddingX, dp(30));
    EXPECT_FLOAT_EQ(fluentSkin().tableChrome().headerHeight, dp(60));
    EXPECT_TRUE(luaui::errors().empty());
}
