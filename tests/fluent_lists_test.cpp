// The Fluent list rows, expanders and menus: their sizes follow the tokens (density included), fixed and fill widths
// resolve, clicks, selection, toggling and the disabled state work as under Material, the selection colors are the
// documented ones, and the menus (a Button's anchored menu, a menu bar's menu) open, choose, keep a checkable row
// open, close, and leave ImGui's stacks as they found them.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.lua.errors;
import imlui.skin;
import imlui.skin.fluent.common;
import imlui.skin.fluent.menu;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.theme;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.widgets.list;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

/// Records where it renders, and how often: a 1px item.
class Spot final : public Element {
public:
    static void declare(TypeBuilder<Spot>&) {}
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override { return ImVec2{1.0f, 1.0f}; }
    void render(Frame&) override {
        m_pos = ImGui::GetCursorScreenPos();
        ++m_renders;
        ImGui::Dummy({1.0f, 1.0f});
    }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }
    [[nodiscard]] int renders() const { return m_renders; }

private:
    ImVec2 m_pos{};
    int m_renders = 0;
};

/// ImGui's stacks where it renders: what a balanced begin/end pair leaves unchanged.
struct Stacks {
    int styleVars = 0, colors = 0, windows = 0, popups = 0;
    friend bool operator==(const Stacks&, const Stacks&) = default;
};

/// Records ImGui's stacks (and its position) when it renders.
class StackProbe final : public Element {
public:
    static void declare(TypeBuilder<StackProbe>&) {}
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override { return ImVec2{1.0f, 1.0f}; }
    void render(Frame&) override {
        const ImGuiContext& g = *ImGui::GetCurrentContext();
        m_stacks = {g.StyleVarStack.Size, g.ColorStack.Size, g.CurrentWindowStack.Size, g.BeginPopupStack.Size};
        m_pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy({1.0f, 1.0f});
    }
    [[nodiscard]] const Stacks& stacks() const { return m_stacks; }
    [[nodiscard]] ImVec2 pos() const { return m_pos; }

private:
    Stacks m_stacks{-1, -1, -1, -1};
    ImVec2 m_pos{};
};

/// A child window with a menu bar, for a MenuBar child; records the menu bar's rect.
class MenuBarHost final : public Element {
public:
    static void declare(TypeBuilder<MenuBarHost>&) {}
    std::optional<ImVec2> intrinsicSize(const Frame&, float) override { return ImVec2{400.0f, 200.0f}; }
    void render(Frame& frame) override {
        ImGui::BeginChild("##host", {400.0f, 200.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_MenuBar);
        // The child has no title bar: its menu bar is at its top, a frame high.
        const ImVec2 at = ImGui::GetWindowPos();
        m_bar = {at, {at.x + 400.0f, at.y + ImGui::GetFrameHeight()}};
        frame.renderChildren();
        ImGui::EndChild();
    }
    [[nodiscard]] std::pair<ImVec2, ImVec2> bar() const { return m_bar; }

private:
    std::pair<ImVec2, ImVec2> m_bar{};
};

/// What InFrame runs when it renders: calls that need a current ImGui window (a size resolving a width).
std::function<void()> inFrameCall;

/// Runs inFrameCall while it renders.
class InFrame final : public Element {
public:
    static void declare(TypeBuilder<InFrame>&) {}
    void render(Frame&) override {
        if (inFrameCall) inFrameCall();
    }
};

class FluentListsTest : public ::testing::Test {
protected:
    FluentListsTest() {
        m_h.registerType<Spot>("Spot");
        m_h.registerType<InFrame>("InFrame");
        m_h.registerType<StackProbe>("StackProbe");
        m_h.registerType<MenuBarHost>("MenuBarHost");
    }
    ~FluentListsTest() override {
        setTheme(Theme{});
        applyPendingTheme();
    }

    void SetUp() override { applyTheme(R"(theme { design = "fluent" })"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void check(std::string_view expr) { EXPECT_EQ(m_h.run("assert(" + std::string{expr} + ")"), "") << expr; }
    void applyTheme(std::string_view code) {
        run(code);
        ASSERT_TRUE(applyPendingTheme());
        ASSERT_EQ(skin().name(), "fluent");
    }
    void frames(int n = 1) { m_h.renderFrames("root", n); }
    void click(ImVec2 at) { m_h.click("root", at.x, at.y); }
    /// Runs `f` inside a frame (in the root window), where the layout functions have a window.
    void inFrame(std::function<void()> f) {
        inFrameCall = std::move(f);
        run("root = InFrame {}");
        frames(1);
        inFrameCall = {};
    }
    [[nodiscard]] const Spot& spot(std::string_view name) {
        const Spot* s = m_h.instance<Spot>(name);
        EXPECT_NE(s, nullptr) << name;
        return *s;
    }
    [[nodiscard]] Stacks stacks(std::string_view probe) { return m_h.instance<StackProbe>(probe)->stacks(); }
    float across(std::string_view a, std::string_view b) { return spot(b).pos().x - spot(a).pos().x - 1.0f; }
    float down(std::string_view a, std::string_view b) { return spot(b).pos().y - spot(a).pos().y - 1.0f; }
    /// An element's width and height as laid out: between Spots in a Row and in a Column.
    ImVec2 laidOut(std::string_view element) {
        run(std::format(R"(
            a, b, c, d = Spot {{}}, Spot {{}}, Spot {{}}, Spot {{}}
            root = Column {{ spacing = 0, Row {{ spacing = 0, a, {0}, b }}, c, {0}, d }})",
                        element));
        frames(2);
        return {across("a", "b"), down("c", "d")};
    }
    [[nodiscard]] static fluent::Look look() { return dynamic_cast<const FluentSkin&>(skin()).look(); }
    void expectNoErrors() { EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front(); }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// One line: controlHeight + 8dp; two lines: + 24dp; the extras scale with the density.
TEST_F(FluentListsTest, ListItemSizesFollowTheTokens) {
    EXPECT_FLOAT_EQ(laidOut(R"(ListItem { "Inbox", width = 200 })").y, dp(40));
    EXPECT_FLOAT_EQ(laidOut(R"(ListItem { "Inbox", width = 200 })").x, 200.0f);
    EXPECT_FLOAT_EQ(laidOut(R"(ListItem { "Inbox", supportingText = "3 new", width = 200 })").y, dp(56));
    inFrame([] {
        md::ListItemOptions natural;
        natural.width = 0;
        EXPECT_FLOAT_EQ(skin().listItemSize(natural).x, dp(320)); // md's
    });

    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 40 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(ListItem { "Inbox", width = 200 })").y, dp(48));
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 30, density = 2 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(ListItem { "Inbox", width = 200 })").y, dp(60 + 16));
    EXPECT_FLOAT_EQ(laidOut(R"(ListItem { "Inbox", supportingText = "x", width = 200 })").y, dp(60 + 48));
    expectNoErrors();
}

TEST_F(FluentListsTest, ListItemClicksFillsTheWidthAndDisables) {
    run(R"(
        s = state { clicks = 0 }
        a, b = Spot {}, Spot {}
        root = Column { spacing = 0,
            a, ListItem { "Inbox", icon = "inbox", trailingText = "3", trailingIcon = "chevron_right", selected = true,
                          onClicked = function() s.clicks = s.clicks + 1 end },
            b, ListItem { "Off", enabled = false, onClicked = function() s.clicks = s.clicks + 10 end } })");
    frames(2);
    const float h = dp(40);
    // It fills the width: a click near the right edge hits it.
    click({700.0f, spot("a").pos().y + 1.0f + h * 0.5f});
    check("s.clicks == 1");
    click({100.0f, spot("b").pos().y + 1.0f + h * 0.5f});
    check("s.clicks == 1");
    expectNoErrors();
}

// Selected: the subtle fills one step on (and faded in by the animated selection); unselected: subtle.
TEST_F(FluentListsTest, SelectionColorsAreTheSubtleFillsOneStepOn) {
    const fluent::Look l = look();
    const fluent::Colors& c = l.colors;
    using fluent::State;
    EXPECT_EQ(fluent::selectedColors(l, State::Rest).fill, c.subtleHover);
    EXPECT_EQ(fluent::selectedColors(l, State::Hover).fill, c.subtlePressed);
    EXPECT_EQ(fluent::selectedColors(l, State::Disabled).text, c.textDisabled);
    EXPECT_EQ(fluent::rowColors(l, State::Rest, 0.0f).fill.a, 0.0f);
    EXPECT_EQ(fluent::rowColors(l, State::Rest, 1.0f).fill, c.subtleHover);
    EXPECT_FLOAT_EQ(fluent::rowColors(l, State::Rest, 0.5f).fill.a, c.subtleHover.a * 0.5f);
    EXPECT_EQ(fluent::rowColors(l, State::Hover, 0.0f).fill, c.subtleHover);
    EXPECT_EQ(fluent::rowColors(l, State::Hover, 1.0f).fill, c.subtlePressed);
}

// The header: controlHeight + 16dp, + 32dp with supporting text; a click toggles it two-way, disabled it doesn't.
TEST_F(FluentListsTest, ExpanderFollowsTheTokensAndToggles) {
    EXPECT_FLOAT_EQ(laidOut(R"(Section { "Advanced", expanded = false, width = 250 })").y, dp(48));
    EXPECT_FLOAT_EQ(laidOut(R"(Section { "Advanced", expanded = false, width = 250 })").x, 250.0f);
    EXPECT_FLOAT_EQ(laidOut(R"(Section { "Advanced", supportingText = "More", expanded = false })").y, dp(64));
    inFrame([] { EXPECT_FLOAT_EQ(skin().expanderSize("", 0).x, dp(320)); });
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 40, density = 0.5 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(Section { "Advanced", expanded = false })").y, dp(20 + 8));
    applyTheme(R"(theme { design = "fluent" })");

    run(R"(
        s = state { open = true }
        toggled = nil
        a, inside = Spot {}, Spot {}
        root = Column { spacing = 0, a,
            Section { "Advanced", expanded = bind(s, "open"), onToggled = function(self, open) toggled = open end,
                      inside },
            Section { "Off", enabled = false, expanded = false, onToggled = function() toggled = "off" end } })");
    frames(2);
    EXPECT_GT(spot("inside").renders(), 0);
    const ImVec2 header{100.0f, spot("a").pos().y + 1.0f + dp(24)};
    click(header);
    check("s.open == false");
    check("toggled == false");
    const int renders = spot("inside").renders();
    frames(2);
    EXPECT_EQ(spot("inside").renders(), renders);
    click(header);
    check("s.open == true");
    frames(2);
    // The disabled one sits right below the open section's header and content.
    click({100.0f, spot("inside").pos().y + 1.0f + dp(24)});
    check("toggled == true");
    expectNoErrors();
}

// A Button's menu: compactHeight + 8dp rows; choosing one closes the menu, a checkable one keeps it open; ImGui's
// stacks are the same after the button as before it, open or closed.
TEST_F(FluentListsTest, AnchoredMenuOpensChoosesAndKeepsTheStacksBalanced) {
    run(R"(
        s = state { picked = 0, wrap = false }
        s1, s2 = Spot {}, Spot {}
        p1, p2 = StackProbe {}, StackProbe {}
        root = Column { spacing = 0, p1,
            Button { "More", menu = Menu {
                MenuItem { "One", icon = "add", onTriggered = function() s.picked = 1 end },
                s1,
                MenuItem { "Two", shortcut = "Ctrl+T", onTriggered = function() s.picked = 2 end },
                s2,
                MenuItem { "Wrap", checked = bind(s, "wrap") } } },
            p2 })");
    frames(2);
    EXPECT_EQ(stacks("p1"), stacks("p2"));
    EXPECT_EQ(spot("s1").renders(), 0);
    const ImVec2 button{m_h.instance<StackProbe>("p1")->pos().x + 20.0f,
                        m_h.instance<StackProbe>("p1")->pos().y + 1.0f + dp(16)};
    click(button);
    frames(10); // the popup settles
    ASSERT_GT(spot("s1").renders(), 0);
    EXPECT_EQ(stacks("p1"), stacks("p2"));
    const float row = fluent::menuRowHeight(look());
    EXPECT_FLOAT_EQ(row, dp(24 + 8));
    EXPECT_FLOAT_EQ(down("s1", "s2"), row);
    // The menu lies below the button.
    EXPECT_GT(spot("s1").pos().y, button.y + dp(16));

    const float x = spot("s1").pos().x + 60.0f;
    click({x, spot("s2").pos().y + 1.0f + row * 0.5f}); // Wrap: toggles, the menu stays open
    check("s.wrap == true");
    frames(2);
    const int open = spot("s1").renders();
    frames(2);
    EXPECT_GT(spot("s1").renders(), open);
    click({x, spot("s1").pos().y + 1.0f + row * 0.5f}); // Two: chosen, the menu closes
    check("s.picked == 2");
    frames(3);
    const int closed = spot("s1").renders();
    frames(2);
    EXPECT_EQ(spot("s1").renders(), closed);
    EXPECT_EQ(stacks("p1"), stacks("p2"));

    // Again: the first row.
    click(button);
    frames(10);
    click({x, spot("s1").pos().y - row * 0.5f});
    check("s.picked == 1");
    expectNoErrors();
}

// The row height follows compactHeight and the density.
TEST_F(FluentListsTest, MenuRowsFollowTheTokens) {
    applyTheme(R"(theme { design = "fluent", tokens = { compactHeight = 30, density = 1.5 } })");
    EXPECT_FLOAT_EQ(fluent::menuRowHeight(look()), dp(45 + 12));
    expectNoErrors();
}

// A menu bar's menu opens on a click on its title, chooses, and leaves the stacks balanced.
TEST_F(FluentListsTest, MenuBarMenuOpensAndChooses) {
    run(R"(
        s = state { opened = 0 }
        m1 = Spot {}
        p1, p2 = StackProbe {}, StackProbe {}
        host = MenuBarHost { MenuBar { Menu { title = "File", m1,
            MenuItem { "Open", onTriggered = function() s.opened = s.opened + 1 end } } } }
        root = Column { spacing = 0, p1, host, p2 })");
    frames(2);
    EXPECT_EQ(stacks("p1"), stacks("p2"));
    const auto [ba, bb] = m_h.instance<MenuBarHost>("host")->bar();
    click({ba.x + 20.0f, (ba.y + bb.y) * 0.5f});
    frames(3);
    ASSERT_GT(spot("m1").renders(), 0);
    EXPECT_EQ(stacks("p1"), stacks("p2"));
    click({spot("m1").pos().x + 40.0f, spot("m1").pos().y + 1.0f + fluent::menuRowHeight(look()) * 0.5f});
    check("s.opened == 1");
    frames(3);
    const int closed = spot("m1").renders();
    frames(2);
    EXPECT_EQ(spot("m1").renders(), closed);
    EXPECT_EQ(stacks("p1"), stacks("p2"));
    expectNoErrors();
}
