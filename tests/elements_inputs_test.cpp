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
