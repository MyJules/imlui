// The typed input elements (Slider, SpinBox, TextField, TextArea, ComboBox, ColorPicker), through the headless
// frame harness: defaults, props reaching the instance, nil resets, two-way write-back through bind() and the
// signals with their arguments, driven by injected mouse, key and text input.
#include <gtest/gtest.h>

import imgui;
import imlui.lua.errors;
import imlui.color;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.elements.slider;
import imlui.ui.elements.spin_box;
import imlui.ui.elements.text_field;
import imlui.ui.elements.text_area;
import imlui.ui.elements.combo_box;
import imlui.ui.elements.color_picker;
import imlui.ui.frame_request;
import imlui.test.headless_frame;
import imlui.test.last_item_probe;

using namespace imlui;

namespace {

using Probe = test::LastItemProbe;

class InputsTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_h.registerType<Probe>("Probe");
    }

    void run(const std::string& code) { ASSERT_EQ(m_h.run(code), ""); }
    void frames(int n = 1) { m_h.renderFrames("root", n); }
    /// Moves the mouse to (x, y) and clicks there, a frame for each step.
    void click(float x, float y) { m_h.click("root", x, y); }
    [[nodiscard]] Probe& probe() { return *m_h.instance<Probe>("root"); }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

// ------------------------------------------------------------------ Slider

TEST_F(InputsTest, SliderDefaultsAndNilReset) {
    run(R"(
        s = state { v = 0.25, to = 4 }
        root = Probe { Slider { id = "slider", value = bind(s, "v"), to = function() return s.to end,
                                stepSize = 0.5, format = "%.1f" } })");
    frames();
    Slider* slider = m_h.instance<Slider>("slider");
    ASSERT_NE(slider, nullptr);
    EXPECT_DOUBLE_EQ(slider->value(), 0.25);
    EXPECT_DOUBLE_EQ(slider->from(), 0.0);
    EXPECT_DOUBLE_EQ(slider->to(), 4.0);
    EXPECT_DOUBLE_EQ(slider->stepSize(), 0.5);
    EXPECT_EQ(slider->format(), std::optional<std::string>{"%.1f"});

    run("s.v = nil; s.to = nil; slider.stepSize = nil; slider.format = nil");
    frames();
    EXPECT_DOUBLE_EQ(slider->value(), 0.0);
    EXPECT_DOUBLE_EQ(slider->to(), 1.0);
    EXPECT_DOUBLE_EQ(slider->stepSize(), 0.0);
    EXPECT_FALSE(slider->format().has_value());
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(InputsTest, SliderClickWritesBackAndEmitsMovedThenReleased) {
    run(R"(
        s = state { v = 2 }
        log = {}
        root = Probe { Slider { id = "slider", value = bind(s, "v"), from = 0, to = 10, stepSize = 1, width = 300,
                                onMoved = function(_, v) log[#log + 1] = "moved " .. v end,
                                onReleased = function(_, v) log[#log + 1] = "released " .. v end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    EXPECT_NEAR(b.x - a.x, 300.0f, 1.0f);
    click(b.x - 1.0f, (a.y + b.y) * 0.5f);
    // Whole values write back as Lua integers, like the handler did ("10", not "10.0").
    run(R"(assert(s.v == 10 and math.type(s.v) == "integer", tostring(s.v)))");
    run(R"(assert(log[1] == "moved 10", tostring(log[1])))");
    run(R"(assert(log[#log] == "released 10", tostring(log[#log])))");
    EXPECT_DOUBLE_EQ(m_h.instance<Slider>("slider")->value(), 10.0);
}

TEST_F(InputsTest, SliderKeepsExactFractionalValues) {
    run(R"(
        s = state { v = 0.1 }
        root = Probe { Slider { id = "slider", value = bind(s, "v"), from = 0, to = 1, stepSize = 0.1, width = 300 } })");
    frames(2);
    // Rendering doesn't write the value back: the bound 0.1 stays the double 0.1 (not the float 0.10000000149).
    run(R"(assert(s.v == 0.1, string.format("%.17g", s.v)))");
    EXPECT_EQ(m_h.instance<Slider>("slider")->value(), 0.1);

    // A stepped value is vmin + k * step without binary noise: 3 steps of 0.1 are 0.3.
    const ImVec2 a = probe().min(), b = probe().max();
    const float x0 = a.x + 10.0f, x1 = b.x - 10.0f; // the track runs between the handle radii (10 dp)
    click(x0 + 0.3f * (x1 - x0), (a.y + b.y) * 0.5f);
    run(R"(assert(s.v == 0.3, string.format("%.17g", s.v)))");
}

TEST_F(InputsTest, SliderReleasedOnlyAfterAChange) {
    run(R"(
        s = state { v = 2 }
        moved, released = 0, 0
        root = Probe { Slider { value = bind(s, "v"), from = 0, to = 10, stepSize = 1, width = 300,
                                onMoved = function() moved = moved + 1 end,
                                onReleased = function(_, v) released = released + 1; last = v end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    const float x0 = a.x + 10.0f, x1 = b.x - 10.0f, y = (a.y + b.y) * 0.5f;
    click(x0 + 0.2f * (x1 - x0), y); // on the current value: nothing changes
    run(R"(assert(moved == 0 and released == 0, moved .. " " .. released))");
    click(x0 + 0.7f * (x1 - x0), y);
    run(R"(assert(moved == 1 and released == 1 and last == 7 and s.v == 7, moved .. " " .. released))");
}

// A stepped value stays on its steps at the end of the range: range 1, step 0.4 ends at 0.8 (it clamped 1.2 to 1).
TEST_F(InputsTest, SliderStepsStopAtTheLastWholeStep) {
    run(R"(
        s = state { v = 0 }
        root = Probe { Slider { value = bind(s, "v"), from = 0, to = 1, stepSize = 0.4, width = 300 } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click(b.x - 1.0f, (a.y + b.y) * 0.5f);
    run(R"(assert(s.v == 0.8, string.format("%.17g", s.v)))");
}

// A range that isn't finite can't place the thumb: it is empty, and a click leaves the value alone (it wrote NaN or
// an infinity). A step that isn't finite is continuous (it wrote NaN).
TEST_F(InputsTest, SliderWithANonFiniteRangeOrStepWritesNoNaN) {
    for (const char* range : {"from = -math.huge, to = 1", "from = 0, to = math.huge", "from = 0 / 0, to = 1",
                              "from = 0, to = 0 / 0", "from = -1e308, to = 1e308"}) {
        run(std::string{"s = state { v = 0.5 }; root = Probe { Slider { value = bind(s, 'v'), width = 300, "} +
            range + " } }");
        frames(2);
        const ImVec2 a = probe().min(), b = probe().max();
        click(a.x + 0.7f * (b.x - a.x), (a.y + b.y) * 0.5f);
        EXPECT_EQ(m_h.run("assert(s.v == 0.5, tostring(s.v))"), "") << range;
    }
    for (const char* step : {"math.huge", "0 / 0", "-math.huge"}) {
        run(std::string{"s = state { v = 0 }; root = Probe { Slider { value = bind(s, 'v'), width = 300, "} +
            "from = 0, to = 10, stepSize = " + step + " } }");
        frames(2);
        const ImVec2 a = probe().min(), b = probe().max();
        const float x0 = a.x + 10.0f, x1 = b.x - 10.0f; // the track runs between the handle radii (10 dp)
        click(x0 + 0.7f * (x1 - x0), (a.y + b.y) * 0.5f);
        EXPECT_EQ(m_h.run("assert(math.abs(s.v - 7) < 0.1, tostring(s.v))"), "") << step;
    }
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(InputsTest, SliderRecoversFromANaNValue) {
    // A NaN value made animate() store a value that never converged: a frame was requested every frame.
    run(R"(
        s = state { v = 0 / 0 }
        root = Probe { Slider { value = bind(s, "v"), width = 300 } })");
    frames(5);
    EXPECT_EQ(secondsUntilFrame(), __builtin_inf()); // nothing left to animate
    run("s.v = 0.5");
    frames(60); // 1 s at the harness's 60 fps: the handle settles
    EXPECT_EQ(secondsUntilFrame(), __builtin_inf());
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ SpinBox

TEST_F(InputsTest, SpinBoxPropsAndDefaults) {
    run(R"(root = Probe { SpinBox { id = "spin" }, SpinBox { id = "set", value = 3, from = 1, to = 5, stepSize = 2,
                                                        decimals = 2.9, label = "Count", variant = "outlined" } })");
    frames();
    SpinBox* spin = m_h.instance<SpinBox>("spin");
    EXPECT_DOUBLE_EQ(spin->value(), 0.0);
    EXPECT_DOUBLE_EQ(spin->from(), -1e9);
    EXPECT_DOUBLE_EQ(spin->to(), 1e9);
    EXPECT_DOUBLE_EQ(spin->stepSize(), 1.0);
    EXPECT_EQ(spin->decimals(), 0);
    EXPECT_EQ(spin->label(), "");
    EXPECT_EQ(spin->variant(), "filled");
    SpinBox* set = m_h.instance<SpinBox>("set");
    EXPECT_DOUBLE_EQ(set->value(), 3.0);
    EXPECT_DOUBLE_EQ(set->from(), 1.0);
    EXPECT_DOUBLE_EQ(set->to(), 5.0);
    EXPECT_DOUBLE_EQ(set->stepSize(), 2.0);
    EXPECT_EQ(set->decimals(), 2); // truncated, like static_cast<int>
    EXPECT_EQ(set->label(), "Count");
    EXPECT_EQ(set->variant(), "outlined");
}

// decimals is clamped to 0..15: the widget formats with "%.<decimals>f".
TEST_F(InputsTest, SpinBoxDecimalsAreClamped) {
    run("root = Probe { SpinBox { value = 1, decimals = 1e9 } }");
    frames(2);
    run("root = Probe { SpinBox { value = 1, decimals = -5 } }");
    frames(2);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(InputsTest, SpinBoxIncrementWritesBackAndEmitsValueModified) {
    run(R"(
        s = state { v = 4 }
        got = nil
        root = Probe { SpinBox { value = bind(s, "v"), to = 5, onValueModified = function(_, v) got = v end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    EXPECT_NEAR(b.x - a.x, 200.0f, 1.0f); // default width
    // The increment button: 48 dp square, 2 dp from the right edge, vertically centered.
    click(b.x - 2.0f - 24.0f, a.y + 28.0f);
    run(R"(assert(s.v == 5 and math.type(s.v) == "integer", tostring(s.v)))");
    run(R"(assert(got == 5, tostring(got)))");
    // At `to` the increment button is disabled.
    click(b.x - 2.0f - 24.0f, a.y + 28.0f);
    run(R"(assert(s.v == 5, tostring(s.v)))");
}

TEST_F(InputsTest, SpinBoxWithFromAboveToDoesntClamp) {
    // from > to is an empty range: std::clamp(v, 5, 1) was undefined behaviour.
    run(R"(
        s = state { v = 3 }
        root = Probe { SpinBox { value = bind(s, "v"), from = 5, to = 1 } })");
    frames(2);
    const ImVec2 a = probe().min();
    click(a.x + 40.0f, a.y + 28.0f); // into the number, which selects it all
    ImGui::GetIO().AddInputCharacter('7');
    frames(2);
    run(R"(ui.native.injectKey("Enter", true); ui.native.injectKey("Enter", false))"); // InputScalar applies it now
    frames(3);
    run(R"(assert(s.v == 7, tostring(s.v)))");
}

// ------------------------------------------------------------------ TextField / TextArea

TEST_F(InputsTest, TextFieldPropsAndDefaults) {
    run(R"(root = Probe { TextField { id = "plain" },
                         TextField { id = "set", text = "x", label = "L", placeholderText = "P", leadingIcon = "search",
                                     trailingIcon = "clear", supportingText = "S", variant = "outlined",
                                     echoMode = "password", readOnly = true, error = true } })");
    frames();
    TextField* plain = m_h.instance<TextField>("plain");
    EXPECT_EQ(plain->text(), "");
    EXPECT_EQ(plain->variant(), "filled");
    EXPECT_FALSE(plain->password());
    EXPECT_EQ(plain->echoMode(), "");
    EXPECT_FALSE(plain->readOnly());
    EXPECT_FALSE(plain->error());
    EXPECT_FALSE(plain->multiline());
    TextField* set = m_h.instance<TextField>("set");
    EXPECT_EQ(set->text(), "x");
    EXPECT_EQ(set->label(), "L");
    EXPECT_EQ(set->placeholderText(), "P");
    EXPECT_EQ(set->leadingIcon(), "search");
    EXPECT_EQ(set->trailingIcon(), "clear");
    EXPECT_EQ(set->supportingText(), "S");
    EXPECT_EQ(set->variant(), "outlined");
    EXPECT_EQ(set->echoMode(), "password");
    EXPECT_TRUE(set->readOnly());
    EXPECT_TRUE(set->error());
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(InputsTest, TextFieldTypingWritesBackAndEmitsChangedAndAccepted) {
    run(R"(
        s = state { t = "" }
        log = {}
        root = Probe { TextField { text = bind(s, "t"), width = 300,
                                   onTextChanged = function(_, t) log[#log + 1] = "changed " .. t end,
                                   onAccepted = function(_, t) log[#log + 1] = "accepted " .. t end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    ImGui::GetIO().AddInputCharacter('h');
    ImGui::GetIO().AddInputCharacter('i');
    frames(3);
    run(R"(assert(s.t == "hi", s.t))");
    run(R"(assert(log[#log] == "changed hi", tostring(log[#log])))");
    run(R"(assert(#log == 1, table.concat(log, ",")))"); // both characters arrived in one frame: one call
    run(R"(ui.native.injectKey("Enter", true); ui.native.injectKey("Enter", false))");
    frames(3);
    run(R"(assert(log[#log] == "accepted hi", tostring(log[#log])))");
}

TEST_F(InputsTest, TextFieldTrailingIconEmitsTrailingClicked) {
    run(R"(
        clicks = 0
        root = Probe { TextField { trailingIcon = "clear", width = 300,
                                   onTrailingClicked = function() clicks = clicks + 1 end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click(b.x - 2.0f - 24.0f, (a.y + b.y) * 0.5f);
    run(R"(assert(clicks == 1, clicks))");
}

TEST_F(InputsTest, TextFieldClickInTheContainerFocusesTheInput) {
    run(R"(
        s = state { t = "" }
        root = Probe { TextField { text = bind(s, "t"), width = 300 } })");
    frames(2);
    const ImVec2 a = probe().min();
    // Below the input row (about 20-36 dp of the 56 dp container, without a label), inside the container.
    click(a.x + 150.0f, a.y + 50.0f);
    frames();
    ImGui::GetIO().AddInputCharacter('o');
    ImGui::GetIO().AddInputCharacter('k');
    frames(3);
    run(R"(assert(s.t == "ok", s.t))");
}

TEST_F(InputsTest, TextAreaIsAMultilineTextField) {
    run(R"(
        s = state { t = "first" }
        root = Probe { TextArea { id = "area", text = bind(s, "t"), label = "Notes" } })");
    frames(2);
    TextArea* area = m_h.instance<TextArea>("area");
    ASSERT_NE(area, nullptr);
    EXPECT_TRUE(area->multiline());
    EXPECT_EQ(area->text(), "first");
    EXPECT_EQ(area->label(), "Notes");
    EXPECT_EQ(m_h.instance<TextField>("area"), area); // a TextField too
    EXPECT_NEAR(probe().max().y - probe().min().y, 120.0f, 1.0f); // the multi-line default height
    run(R"(s.t = "second")");
    frames();
    EXPECT_EQ(area->text(), "second");
}

// ------------------------------------------------------------------ ComboBox

TEST_F(InputsTest, ComboBoxPropsAndDefaults) {
    run(R"(root = Probe { ComboBox { id = "plain" },
                         ComboBox { id = "set", model = { "a", 2 }, icons = { "home" }, currentIndex = 2, label = "L",
                                    leadingIcon = "search", supportingText = "S", variant = "outlined", error = true } })");
    frames();
    ComboBox* plain = m_h.instance<ComboBox>("plain");
    EXPECT_TRUE(plain->model().empty());
    EXPECT_EQ(plain->currentIndex(), 1);
    EXPECT_EQ(plain->variant(), "filled");
    EXPECT_FALSE(plain->error());
    ComboBox* set = m_h.instance<ComboBox>("set");
    EXPECT_EQ(set->model(), (std::vector<std::string>{"a", "2"}));
    EXPECT_EQ(set->icons(), (std::vector<std::string>{"home"}));
    EXPECT_EQ(set->currentIndex(), 2);
    EXPECT_EQ(set->label(), "L");
    EXPECT_EQ(set->leadingIcon(), "search");
    EXPECT_EQ(set->supportingText(), "S");
    EXPECT_EQ(set->variant(), "outlined");
    EXPECT_TRUE(set->error());
}

TEST_F(InputsTest, ComboBoxTakesTheSmallestIndex) {
    run("root = Probe { ComboBox { model = { 'a', 'b' }, currentIndex = math.mininteger } }"); // no overflow
    frames(2);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(InputsTest, ComboBoxPickingAnItemWritesBackAndEmitsActivated) {
    run(R"(
        s = state { i = 1 }
        got = nil
        root = Probe { ComboBox { model = { "a", "b", "c" }, currentIndex = bind(s, "i"),
                                  onActivated = function(_, i, text) got = i .. " " .. text end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); // opens the menu
    frames(3);                                     // laid out hidden on its first frame
    // Menu items are 48 dp tall below the field, after a small gap and 8 dp of padding: the second one.
    click(a.x + 40.0f, b.y + 84.0f);
    run(R"(assert(s.i == 2 and math.type(s.i) == "integer", tostring(s.i)))");
    run(R"(assert(got == "2 b", tostring(got)))");
}

TEST_F(InputsTest, ComboBoxActivatedGetsThePickedTextWhenTheHandlerReplacesTheModel) {
    run(R"(
        s = state { i = 1 }
        got = nil
        root = Probe { ComboBox { model = { "a", "b", "c" }, currentIndex = bind(s, "i"),
                                  onCurrentIndexChanged = function(self) self.model = {} end,
                                  onActivated = function(_, i, text) got = i .. " " .. text end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); // opens the menu
    frames(3);
    click(a.x + 40.0f, b.y + 84.0f); // the second item
    run(R"(assert(got == "2 b", tostring(got)))");
    frames(2); // the empty model renders
    EXPECT_TRUE(luaui::errors().empty());
}

// ------------------------------------------------------------------ ColorPicker

TEST_F(InputsTest, ColorPickerPropsAndDefaults) {
    run(R"(
        s = state { c = "#336699" }
        root = Probe { ColorPicker { id = "plain" }, ColorPicker { id = "set", color = bind(s, "c"), label = "Seed",
                                                                  alpha = true } })");
    frames();
    ColorPicker* plain = m_h.instance<ColorPicker>("plain");
    EXPECT_FALSE(plain->color().has_value()); // shown as white
    EXPECT_FALSE(plain->alpha());
    ColorPicker* set = m_h.instance<ColorPicker>("set");
    ASSERT_TRUE(set->color().has_value());
    EXPECT_EQ(toHex(*set->color()), "#336699");
    EXPECT_EQ(set->label(), "Seed");
    EXPECT_TRUE(set->alpha());
}

TEST_F(InputsTest, ColorPickerPickingWritesBackHexAndEmitsColorChanged) {
    run(R"(
        s = state { c = "#FFFFFF" }
        got = nil
        root = Probe { ColorPicker { color = bind(s, "c"), onColorChanged = function(_, hex) got = hex; calls = (calls or 0) + 1 end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); // opens the picker
    frames(3);
    // The saturation/value square starts 16 dp inside the popup, which opens 4 dp below the field.
    click(a.x + 16.0f + 100.0f, b.y + 4.0f + 16.0f + 100.0f);
    run(R"(assert(type(s.c) == "string" and s.c:match("^#%x%x%x%x%x%x$") and s.c ~= "#FFFFFF", tostring(s.c)))");
    run(R"(assert(got == s.c, tostring(got)))");
    run(R"(assert(calls == 1, calls))"); // once per pick
}

TEST_F(InputsTest, ColorPickerWithoutAlphaKeepsHexInputOpaque) {
    run(R"(
        s = state { c = "#FFFFFF" }
        root = Probe { ColorPicker { color = bind(s, "c") } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); // opens the picker
    frames(3);
    // The popup opens 4 dp below the field with 16 dp of padding. The picker's square is 240 dp less the hue bar
    // (a frame height wide, plus the inner spacing), then a 4 dp gap and the 56 dp hex field.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float square = 240.0f - (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x);
    const float hexTop = b.y + 4.0f + 16.0f + square + style.ItemSpacing.y + 4.0f + style.ItemSpacing.y;
    click(a.x + 16.0f + 120.0f, hexTop + 28.0f);
    run(R"(ui.native.injectKey("A", true, true); ui.native.injectKey("A", false))");
    frames(3);
    for (const char ch : std::string_view{"#11223380"})
        ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(ch));
    frames(3);
    run(R"(assert(s.c == "#112233", tostring(s.c)))");
}
