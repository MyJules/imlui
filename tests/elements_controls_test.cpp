// The controls group as typed elements (Button, IconButton, Checkbox, Switch, RadioButton, RadioGroup, Chip,
// SegmentedButton, ListItem), through the headless frame harness: defaults, props reaching the instance,
// clicks injected with native.injectMouse, two-way write-back through bind() and the signals' arguments.
#include <gtest/gtest.h>

import imlui.lua.errors;
import imlui.ui.elements.button;
import imlui.ui.elements.icon_button;
import imlui.ui.elements.checkbox;
import imlui.ui.elements.switch_toggle;
import imlui.ui.elements.radio_button;
import imlui.ui.elements.radio_group;
import imlui.ui.elements.chip;
import imlui.ui.elements.segmented_button;
import imlui.ui.elements.list_item;
import imlui.test.headless_frame;
import imlui.widgets.button;

using namespace imlui;

namespace {

class ControlsTest : public ::testing::Test {
protected:
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }

    /// Moves the mouse to (x, y), presses and releases the left button, one frame each. The root renders at
    /// the window padding (8, 8), so a single control's first pixels are around there.
    void click(float x, float y) {
        m_h.renderFrame("root"); // the window exists, so it can be hovered
        m_h.click("root", x, y);
    }

    void expectNoErrors() {
        for (const auto& e : luaui::errors())
            ADD_FAILURE() << e;
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

TEST_F(ControlsTest, ButtonDefaultsPositionalTextAndClick) {
    run(R"(
        s = state { clicks = 0 }
        root = Button { "OK", onClicked = function(self) s.clicks = s.clicks + 1; s.self = self end })");
    Button* b = m_h.instance<Button>("root");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->text(), "OK");
    EXPECT_EQ(b->variant(), md::ButtonVariant::Filled);
    EXPECT_EQ(b->icon(), "");

    click(20, 20);
    run("assert(s.clicks == 1, s.clicks); assert(s.self == root)");

    run(R"(root.text = "Go"; root.variant = "tonal"; root.icon = "add")");
    b = m_h.instance<Button>("root");
    EXPECT_EQ(b->text(), "Go");
    EXPECT_EQ(b->variant(), md::ButtonVariant::Tonal);
    EXPECT_EQ(b->icon(), "add");
    run("root.variant = nil");
    EXPECT_EQ(m_h.instance<Button>("root")->variant(), md::ButtonVariant::Filled);
    expectNoErrors();
}

// A variant is one of the ButtonVariant names: a typo is reported with the names, and the button keeps its variant.
TEST_F(ControlsTest, ButtonReportsAnUnknownVariant) {
    run(R"(root = Button { "OK", variant = "tonal" })");
    m_h.renderFrame("root");
    run(R"(root.variant = "tonla")");
    m_h.renderFrame("root");
    EXPECT_EQ(m_h.instance<Button>("root")->variant(), md::ButtonVariant::Tonal);
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find(
                  R"(Button.variant: ButtonVariant ('filled'|'tonal'|'outlined'|'text'|'elevated'|'fab') expected, got string "tonla")"),
              std::string::npos)
        << luaui::errors()[0];
}

TEST_F(ControlsTest, ButtonOpensItsMenuOnClick) {
    run(R"(
        s = state { picked = 0 }
        root = Button { "More", menu = Menu { MenuItem { "One", onTriggered = function() s.picked = 1 end } } })");
    click(20, 20);
    run("assert(s.picked == 0)");
    // The menu opened below the button (8..48): its first item is just under it.
    m_h.renderFrames("root", 10); // the popup settles
    click(40, 76);
    run("assert(s.picked == 1, 'the menu item was not triggered')");
    expectNoErrors();
}

TEST_F(ControlsTest, IconButtonIsAToggleOnlyWhenCheckedIsPresent) {
    run(R"(
        s = state { clicks = 0 }
        root = IconButton { icon = "favorite", onClicked = function() s.clicks = s.clicks + 1 end })");
    IconButton* b = m_h.instance<IconButton>("root");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->variant(), "standard");
    EXPECT_FALSE(b->checked());
    EXPECT_FALSE(b->color().has_value());
    ASSERT_TRUE(b->icon().has_value());
    EXPECT_EQ(*b->icon(), "favorite");

    click(28, 28);
    run("assert(s.clicks == 1, s.clicks); assert(rawget(root, 'checked') == nil, 'not checkable: no write-back')");
    click(28, 28);
    run("assert(s.clicks == 2, s.clicks); assert(rawget(root, 'checked') == nil)");
    expectNoErrors();
}

TEST_F(ControlsTest, IconButtonWithCheckedTogglesThroughBind) {
    run(R"(
        s = state { on = false, clicks = 0, changed = {} }
        root = IconButton { "menu", checkedIcon = "close", color = "primary", checked = bind(s, "on"),
                            onClicked = function() s.clicks = s.clicks + 1; s.onAtClick = s.on end,
                            onCheckedChanged = function(_, v) s.changed[#s.changed + 1] = v end })");
    IconButton* b = m_h.instance<IconButton>("root");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->text(), "menu");
    EXPECT_FALSE(b->icon().has_value()); // the positional text is the icon
    ASSERT_TRUE(b->checkedIcon().has_value());
    EXPECT_EQ(*b->checkedIcon(), "close");
    EXPECT_TRUE(b->color().has_value());

    click(28, 28);
    // Written back before onClicked runs.
    run("assert(s.on == true); assert(s.clicks == 1); assert(s.onAtClick == true); assert(s.changed[1] == true)");
    EXPECT_TRUE(m_h.instance<IconButton>("root")->checked());
    click(28, 28);
    run("assert(s.on == false); assert(s.clicks == 2); assert(s.onAtClick == false)");
    expectNoErrors();
}

TEST_F(ControlsTest, CheckboxTogglesThroughBindAndEmitsOnToggled) {
    run(R"(
        s = state { on = false, toggled = {} }
        root = Checkbox { "Wi-Fi", checked = bind(s, "on"),
                          onToggled = function(_, v) s.toggled[#s.toggled + 1] = v; s.onAtSignal = s.on end })");
    Checkbox* c = m_h.instance<Checkbox>("root");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->text(), "Wi-Fi");
    EXPECT_FALSE(c->checked());

    click(28, 28);
    run("assert(s.on == true); assert(s.toggled[1] == true); assert(s.onAtSignal == true)");
    click(28, 28);
    run("assert(s.on == false); assert(s.toggled[2] == false); assert(#s.toggled == 2)");
    expectNoErrors();
}

TEST_F(ControlsTest, SwitchTogglesThroughBindAndKeepsIcons) {
    run(R"(
        s = state { on = true, toggled = {} }
        root = Switch { checked = bind(s, "on"), onToggled = function(_, v) s.toggled[#s.toggled + 1] = v end })");
    Switch* sw = m_h.instance<Switch>("root");
    ASSERT_NE(sw, nullptr);
    EXPECT_TRUE(sw->icons());
    EXPECT_TRUE(sw->checked());
    run("root.icons = false");
    EXPECT_FALSE(m_h.instance<Switch>("root")->icons());

    click(30, 28);
    run("assert(s.on == false); assert(s.toggled[1] == false)");
    expectNoErrors();
}

TEST_F(ControlsTest, RadioButtonChecksItselfAndUnchecksItsGroupPeersThroughBind) {
    run(R"(
        s = state { a = false, b = true, c = true, clicks = 0 }
        root = Column {
            RadioButton { "A", id = "a", group = "g", checked = bind(s, "a"),
                          onClicked = function() s.clicks = s.clicks + 1; s.bAtClick = s.b end },
            RadioButton { "B", group = "g", checked = bind(s, "b") },
        }
        other = RadioButton { "C", group = "other", checked = bind(s, "c") })");
    RadioButton* a = m_h.instance<RadioButton>("a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->text(), "A");
    EXPECT_FALSE(a->checked());

    click(28, 28);
    // Checked, then the peers unchecked, then onClicked.
    run("assert(s.a == true); assert(s.b == false); assert(s.bAtClick == false); assert(s.clicks == 1)");
    run("assert(s.c == true, 'another group is left alone')");
    EXPECT_TRUE(m_h.instance<RadioButton>("a")->checked());

    // Clicking a checked radio emits again but changes nothing.
    click(28, 28);
    run("assert(s.a == true); assert(s.b == false); assert(s.clicks == 2)");
    expectNoErrors();
}

TEST_F(ControlsTest, RadioGroupWritesTheOneBasedIndexBackAndEmitsOnActivated) {
    run(R"(
        s = state { index = 1, activated = {} }
        root = RadioGroup { model = { "Small", "Medium", "Large" }, currentIndex = bind(s, "index"),
                            onActivated = function(_, i, text)
                                s.activated[#s.activated + 1] = { i = i, text = text, at = s.index }
                            end })");
    RadioGroup* g = m_h.instance<RadioGroup>("root");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->model().size(), 3u);
    EXPECT_FALSE(g->horizontal());
    EXPECT_DOUBLE_EQ(g->currentIndex(), 1.0);

    // Rows are 40 px high with no spacing between them: the third one is centered at y = 8 + 80 + 20.
    click(28, 108);
    run(R"(
        assert(s.index == 3, tostring(s.index))
        local a = s.activated[1]
        assert(a.i == 3 and math.type(a.i) == "integer", tostring(a.i))
        assert(a.text == "Large"); assert(a.at == 3))");
    // The current item doesn't activate again.
    click(28, 108);
    run("assert(#s.activated == 1)");
    run("root.currentIndex = nil");
    EXPECT_DOUBLE_EQ(m_h.instance<RadioGroup>("root")->currentIndex(), 1.0);
    expectNoErrors();
}

// The model isn't copied every frame any more: an onActivated handler that replaces it still gets the clicked text,
// and the group shows the new model from the next frame.
TEST_F(ControlsTest, RadioGroupHandlerMayReplaceTheModel) {
    run(R"(
        got = nil
        root = RadioGroup { model = { "Small", "Medium", "Large" },
                            onActivated = function(self, i, text) got = text; self.model = { "Only" } end })");
    m_h.renderFrame("root");
    click(28, 108);
    run(R"(assert(got == "Large", tostring(got)))");
    m_h.renderFrame("root");
    EXPECT_EQ(m_h.instance<RadioGroup>("root")->model(), std::vector<std::string>{"Only"});
    expectNoErrors();
}

TEST_F(ControlsTest, RadioGroupLaysOutHorizontally) {
    run(R"(
        s = state { index = 1 }
        root = RadioGroup { model = { "A", "B" }, horizontal = true, currentIndex = bind(s, "index") })");
    EXPECT_TRUE(m_h.instance<RadioGroup>("root")->horizontal());
    click(28, 108); // below a horizontal group: nothing
    run("assert(s.index == 1)");
    expectNoErrors();
}

TEST_F(ControlsTest, FilterChipTogglesAndEmitsTheNewState) {
    run(R"(
        s = state { on = false, clicked = {} }
        root = Chip { "Filter", checked = bind(s, "on"),
                      onClicked = function(_, v) s.clicked[#s.clicked + 1] = v; s.onAtClick = s.on end })");
    Chip* c = m_h.instance<Chip>("root");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->variant(), "filter");
    EXPECT_TRUE(c->checkable());
    EXPECT_EQ(c->icon(), "");

    click(24, 24);
    run("assert(s.on == true); assert(s.clicked[1] == true); assert(s.onAtClick == true)");
    click(24, 24);
    run("assert(s.on == false); assert(s.clicked[2] == false)");
    expectNoErrors();
}

TEST_F(ControlsTest, ChipThatIsNotCheckableOnlyEmits) {
    run(R"(
        s = state { on = false, assist = false, clicked = {} }
        root = Column {
            Chip { "Fixed", checkable = false, checked = bind(s, "on"),
                   onClicked = function(_, v) s.clicked[#s.clicked + 1] = v end },
        })");
    click(24, 24);
    run("assert(s.on == false); assert(s.clicked[1] == true)");

    run(R"(root = Chip { "Assist", variant = "assist", checked = bind(s, "assist"),
                         onClicked = function(_, v) s.clicked[#s.clicked + 1] = v end })");
    click(24, 24);
    run("assert(s.assist == false); assert(s.clicked[2] == true)");
    expectNoErrors();
}

TEST_F(ControlsTest, SegmentedButtonWritesTheOneBasedIndexBackAndEmitsOnActivated) {
    run(R"(
        s = state { index = 1, activated = {} }
        root = SegmentedButton { model = { "Day", "Week" }, icons = { "today", "date_range" }, width = 400,
                                 currentIndex = bind(s, "index"),
                                 onActivated = function(_, i, text)
                                     s.activated[#s.activated + 1] = { i = i, text = text, at = s.index }
                                 end })");
    SegmentedButton* sb = m_h.instance<SegmentedButton>("root");
    ASSERT_NE(sb, nullptr);
    EXPECT_EQ(sb->model().size(), 2u);
    EXPECT_EQ(sb->icons().size(), 2u);
    EXPECT_DOUBLE_EQ(sb->currentIndex(), 1.0);

    click(308, 28); // the second 200 px segment
    run(R"(
        assert(s.index == 2, tostring(s.index))
        local a = s.activated[1]
        assert(a.i == 2 and math.type(a.i) == "integer"); assert(a.text == "Week"); assert(a.at == 2))");
    click(308, 28);
    run("assert(#s.activated == 1, 'the current segment does not activate again')");
    expectNoErrors();
}

// currentIndex is a Lua number: NaN, infinities and values past the int range select nothing (or the last) and
// don't overflow.
TEST_F(ControlsTest, IndexControlsTakeAnyNumberAsCurrentIndex) {
    for (const char* index : {"0/0", "math.huge", "-math.huge", "1e300", "-1e300", "math.mininteger", "0"}) {
        run(std::string{"root = Column { SegmentedButton { model = { 'a', 'b' }, currentIndex = "} + index +
            " }, RadioGroup { model = { 'a', 'b' }, currentIndex = " + index + " } }");
        m_h.renderFrames("root", 2);
    }
    expectNoErrors();
}

TEST_F(ControlsTest, ListItemPropsAndClick) {
    run(R"(
        s = state { clicks = 0 }
        root = ListItem { "Inbox", supportingText = "3 new", icon = "inbox", trailingText = "3",
                          trailingIcon = "chevron_right", selected = true,
                          onClicked = function() s.clicks = s.clicks + 1 end })");
    ListItem* li = m_h.instance<ListItem>("root");
    ASSERT_NE(li, nullptr);
    EXPECT_EQ(li->text(), "Inbox");
    EXPECT_EQ(li->supportingText(), "3 new");
    EXPECT_EQ(li->icon(), "inbox");
    EXPECT_EQ(li->trailingText(), "3");
    EXPECT_EQ(li->trailingIcon(), "chevron_right");
    EXPECT_TRUE(li->selected());

    click(400, 40); // fills the width by default
    run("assert(s.clicks == 1, s.clicks)");
    run("root.selected = nil");
    EXPECT_FALSE(m_h.instance<ListItem>("root")->selected());
    expectNoErrors();
}

TEST_F(ControlsTest, DisabledControlsDoNotReact) {
    run(R"(
        s = state { on = false, clicks = 0 }
        root = Checkbox { enabled = false, checked = bind(s, "on"), onToggled = function() s.clicks = 1 end })");
    click(28, 28);
    run("assert(s.on == false); assert(s.clicks == 0)");
    expectNoErrors();
}
