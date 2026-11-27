// The Fluent inputs (group B): TextField / TextArea (TextBox), SpinBox (NumberBox), ComboBox (ComboBox) and
// ColorPicker (a color drop-down) under `theme { design = "fluent" }`. Their sizes follow the tokens (density
// included), fixed and fill widths resolve, and typing, stepping, picking and the disabled state work through injected
// input exactly as under Material.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.lua.errors;
import imlui.skin;
import imlui.skin.fluent.common;
import imlui.skin.fluent.inputs;
import imlui.theme.dpi;
import imlui.theme.theme;
import imlui.theme.typography;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.widgets.common;
import imlui.test.headless_frame;
import imlui.test.last_item_probe;

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

using Probe = test::LastItemProbe;

class FluentInputsTest : public ::testing::Test {
protected:
    FluentInputsTest() {
        m_h.registerType<Spot>("Spot");
        m_h.registerType<Probe>("Probe");
    }
    ~FluentInputsTest() override {
        setTheme(Theme{});
        applyPendingTheme();
    }

    void SetUp() override { applyTheme(R"(theme { design = "fluent" })"); }
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    void applyTheme(std::string_view code) {
        run(code);
        ASSERT_TRUE(applyPendingTheme());
    }
    void frames(int n = 1) { m_h.renderFrames("root", n); }
    void click(float x, float y) { m_h.click("root", x, y); }
    [[nodiscard]] Probe& probe() { return *m_h.instance<Probe>("root"); }
    /// The x distance from Spot `a`'s item to Spot `b`'s, less `a`'s 1px.
    float across(std::string_view a, std::string_view b) {
        return m_h.instance<Spot>(b)->pos().x - m_h.instance<Spot>(a)->pos().x - 1.0f;
    }
    float down(std::string_view a, std::string_view b) {
        return m_h.instance<Spot>(b)->pos().y - m_h.instance<Spot>(a)->pos().y - 1.0f;
    }
    /// An element's width and height as laid out: between Spots in a Row and in a Column.
    ImVec2 laidOut(std::string_view element) {
        run(std::format(R"(
            a, b, c, d = Spot {{}}, Spot {{}}, Spot {{}}, Spot {{}}
            root = Column {{ spacing = 0, Row {{ spacing = 0, a, {0}, b }}, c, {0}, d }})",
                        element));
        m_h.renderFrames("root", 2);
        return {across("a", "b"), down("c", "d")};
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

/// A header's height: a body line and the gap.
float header(float gapDp = 8.0f) { return fontPixels(TextStyle::BodyMedium) + dp(gapDp); }
/// A description's height: 4dp and a caption line.
float description() { return dp(4) + fontPixels(TextStyle::BodySmall); }

} // namespace

TEST_F(FluentInputsTest, TextFieldSizesFollowTheTokens) {
    ASSERT_EQ(skin().name(), "fluent");
    const ImVec2 plain = laidOut(R"(TextField { width = 300 })");
    EXPECT_FLOAT_EQ(plain.x, 300.0f);
    EXPECT_FLOAT_EQ(plain.y, dp(32));
    // The label is a header above the box, the supporting text a description below it.
    EXPECT_FLOAT_EQ(laidOut(R"(TextField { width = 300, label = "Name" })").y, header() + dp(32));
    EXPECT_FLOAT_EQ(laidOut(R"(TextField { width = 300, supportingText = "Help" })").y, dp(32) + description());
    // Icons and the password toggle stay inside the box.
    EXPECT_FLOAT_EQ(laidOut(R"(TextField { width = 300, leadingIcon = "search", trailingIcon = "clear" })").y, dp(32));
    EXPECT_FLOAT_EQ(laidOut(R"(TextField { width = 300, echoMode = "password" })").y, dp(32));

    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 40, density = 1.5 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(TextField { width = 300 })").y, dp(60));
    EXPECT_FLOAT_EQ(laidOut(R"(TextField { width = 300, label = "Name" })").y, header(12) + dp(60));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, TextFieldWidthsResolve) {
    // An unset width fills (the TextField element passes -1), a fixed one wins.
    run(R"(
        a, b = Spot {}, Spot {}
        root = Row { spacing = 0, width = 500, a, TextField {}, b })");
    frames(2);
    EXPECT_FLOAT_EQ(across("a", "b"), 498.0f);
    EXPECT_FLOAT_EQ(laidOut(R"(TextField { width = 150 })").x, 150.0f);
    // Narrower than its insets: the input keeps a pixel at its inset (paddingX, the icon, the gap), like Material's,
    // and doesn't stretch over its siblings to the available width.
    EXPECT_LE(laidOut(R"(TextField { width = 10, leadingIcon = "search", trailingIcon = "clear" })").x,
              dp(12 + 16 + 8) + 1.0f);
    EXPECT_TRUE(luaui::errors().empty());
}

// A TextArea's height is the header and the box together (120dp by default), like Material's container.
TEST_F(FluentInputsTest, TextAreaHeight) {
    EXPECT_FLOAT_EQ(laidOut(R"(TextArea { width = 300 })").y, dp(120));
    EXPECT_FLOAT_EQ(laidOut(R"(TextArea { width = 300, label = "Notes" })").y, dp(120));
    EXPECT_FLOAT_EQ(laidOut(R"(TextArea { width = 300, height = 200 })").y, 200.0f);
    // Never shorter than a single-line box.
    EXPECT_FLOAT_EQ(laidOut(R"(TextArea { width = 300, height = 10 })").y, dp(32));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, TextFieldTypingWritesBackAndEmitsChangedAndAccepted) {
    run(R"(
        s = state { t = "" }
        log = {}
        root = Probe { TextField { text = bind(s, "t"), width = 300, label = "Name", placeholderText = "Type",
                                   onTextChanged = function(_, t) log[#log + 1] = "changed " .. t end,
                                   onAccepted = function(_, t) log[#log + 1] = "accepted " .. t end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    EXPECT_FLOAT_EQ(b.y - a.y, header() + dp(32));
    click((a.x + b.x) * 0.5f, b.y - dp(16)); // the box's middle
    ImGui::GetIO().AddInputCharacter('h');
    ImGui::GetIO().AddInputCharacter('i');
    frames(3);
    run(R"(assert(s.t == "hi", s.t))");
    run(R"(assert(log[#log] == "changed hi", tostring(log[#log])))");
    run(R"(assert(#log == 1, table.concat(log, ",")))");
    run(R"(ui.native.injectKey("Enter", true); ui.native.injectKey("Enter", false))");
    frames(3);
    run(R"(assert(log[#log] == "accepted hi", tostring(log[#log])))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, TextFieldClickInTheBoxFocusesTheInput) {
    run(R"(
        s = state { t = "" }
        root = Probe { TextField { text = bind(s, "t"), width = 300 } })");
    frames(2);
    const ImVec2 a = probe().min();
    click(a.x + 150.0f, a.y + 2.0f); // above the input's line, inside the box
    frames();
    ImGui::GetIO().AddInputCharacter('o');
    ImGui::GetIO().AddInputCharacter('k');
    frames(3);
    run(R"(assert(s.t == "ok", s.t))");
}

TEST_F(FluentInputsTest, TextFieldTrailingButtonEmitsTrailingClicked) {
    run(R"(
        clicks = 0
        s = state { t = "" }
        root = Probe { TextField { text = bind(s, "t"), trailingIcon = "clear", width = 300,
                                   onTrailingClicked = function() clicks = clicks + 1 end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    // The inner button: 30dp wide, 4dp inside the box's right edge.
    click(b.x - dp(fluent::innerButtonMargin) - dp(fluent::innerButtonWidth) * 0.5f, (a.y + b.y) * 0.5f);
    run(R"(assert(clicks == 1, clicks))");
    // It doesn't focus the input.
    ImGui::GetIO().AddInputCharacter('x');
    frames(3);
    run(R"(assert(s.t == "", s.t))");
}

TEST_F(FluentInputsTest, DisabledTextFieldDoesntReact) {
    run(R"(
        s = state { t = "" }
        root = Probe { TextField { text = bind(s, "t"), width = 300, enabled = false } })");
    frames(2);
    click(probe().center().x, probe().center().y);
    frames();
    ImGui::GetIO().AddInputCharacter('x');
    frames(3);
    run(R"(assert(s.t == "", s.t))");
}

TEST_F(FluentInputsTest, SpinBoxSizes) {
    const ImVec2 plain = laidOut(R"(SpinBox {})");
    EXPECT_FLOAT_EQ(plain.x, dp(200)); // md's natural width
    EXPECT_FLOAT_EQ(plain.y, dp(32));
    EXPECT_FLOAT_EQ(laidOut(R"(SpinBox { label = "Count" })").y, header() + dp(32));
    EXPECT_FLOAT_EQ(laidOut(R"(SpinBox { width = 120 })").x, 120.0f);
    EXPECT_FLOAT_EQ(laidOut(R"(SpinBox { variant = "outlined" })").y, dp(32)); // one Fluent look
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 36, density = 0.5 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(SpinBox {})").y, dp(18));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, SpinBoxStepsAndClamps) {
    run(R"(
        s = state { v = 4 }
        got = nil
        root = Probe { SpinBox { value = bind(s, "v"), from = 3, to = 5, stepSize = 2,
                                 onValueModified = function(_, v) got = v end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    const float cy = (a.y + b.y) * 0.5f, bw = dp(fluent::innerButtonWidth), m = dp(fluent::innerButtonMargin);
    const float inc = b.x - m - bw * 1.5f, dec = b.x - m - bw * 0.5f; // up, then down
    click(inc, cy); // 4 + 2, clamped to 5
    run(R"(assert(s.v == 5 and math.type(s.v) == "integer", tostring(s.v)))");
    run(R"(assert(got == 5, tostring(got)))");
    click(inc, cy); // disabled at `to`
    run(R"(assert(s.v == 5, tostring(s.v)))");
    click(dec, cy); // 5 - 2, clamped to 3
    run(R"(assert(s.v == 3, tostring(s.v)))");
    click(dec, cy); // disabled at `from`
    run(R"(assert(s.v == 3, tostring(s.v)))");
}

// stepSize is checked: a negative step is its magnitude (the up button still goes up), 0 or NaN steps by 1.
TEST_F(FluentInputsTest, SpinBoxStepSizeIsChecked) {
    run(R"(
        s = state { v = 4 }
        box = SpinBox { value = bind(s, "v"), from = 0, to = 10, stepSize = -2 }
        root = Probe { box })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    const float cy = (a.y + b.y) * 0.5f, bw = dp(fluent::innerButtonWidth), m = dp(fluent::innerButtonMargin);
    const float inc = b.x - m - bw * 1.5f, dec = b.x - m - bw * 0.5f;
    click(inc, cy);
    run(R"(assert(s.v == 6, tostring(s.v)))");
    run("box.stepSize = 0/0");
    click(dec, cy);
    run(R"(assert(s.v == 5, tostring(s.v)))");
    run("box.stepSize = 0");
    click(dec, cy);
    run(R"(assert(s.v == 4, tostring(s.v)))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, SpinBoxTyping) {
    run(R"(
        s = state { v = 3 }
        root = Probe { SpinBox { value = bind(s, "v") } })");
    frames(2);
    const ImVec2 a = probe().min();
    click(a.x + 40.0f, a.y + dp(16)); // into the number, which selects it all
    ImGui::GetIO().AddInputCharacter('7');
    frames(2);
    run(R"(ui.native.injectKey("Enter", true); ui.native.injectKey("Enter", false))");
    frames(3);
    run(R"(assert(s.v == 7, tostring(s.v)))");
}

TEST_F(FluentInputsTest, DisabledSpinBoxDoesntReact) {
    run(R"(
        s = state { v = 1 }
        root = Probe { SpinBox { value = bind(s, "v"), enabled = false } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click(b.x - dp(fluent::innerButtonMargin) - dp(fluent::innerButtonWidth) * 1.5f, (a.y + b.y) * 0.5f);
    run(R"(assert(s.v == 1, tostring(s.v)))");
}

TEST_F(FluentInputsTest, ComboBoxSizes) {
    const ImVec2 plain = laidOut(R"(ComboBox { model = { "a", "b" } })");
    EXPECT_FLOAT_EQ(plain.x, dp(240)); // md's natural width
    EXPECT_FLOAT_EQ(plain.y, dp(32));
    EXPECT_FLOAT_EQ(laidOut(R"(ComboBox { model = { "a" }, label = "Pick" })").y, header() + dp(32));
    EXPECT_FLOAT_EQ(laidOut(R"(ComboBox { model = { "a" }, supportingText = "Help" })").y, dp(32) + description());
    EXPECT_FLOAT_EQ(laidOut(R"(ComboBox { model = { "a" }, width = 100 })").x, 100.0f);
    applyTheme(R"(theme { design = "fluent", tokens = { controlHeight = 40 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(ComboBox { model = { "a" } })").y, dp(40));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, ComboBoxPickingAnItemWritesBackAndEmitsActivated) {
    run(R"(
        s = state { i = 1 }
        got = nil
        root = Probe { ComboBox { model = { "a", "b", "c" }, currentIndex = bind(s, "i"),
                                  onActivated = function(_, i, text) got = i .. " " .. text end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); // opens the flyout
    frames(3);                                     // laid out hidden on its first frame
    // The flyout opens 4dp below the box; its rows (controlHeight) start 2dp inside it: the second row's middle.
    const float rowH = dp(32);
    click(a.x + 40.0f, b.y + dp(fluent::flyoutGap) + dp(fluent::flyoutPaddingY) + rowH * 1.5f);
    run(R"(assert(s.i == 2 and math.type(s.i) == "integer", tostring(s.i)))");
    run(R"(assert(got == "2 b", tostring(got)))");
    // Picking the current item again changes nothing.
    run("got = nil");
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    frames(3);
    click(a.x + 40.0f, b.y + dp(fluent::flyoutGap) + dp(fluent::flyoutPaddingY) + rowH * 1.5f);
    run(R"(assert(s.i == 2 and got == nil, tostring(s.i) .. " " .. tostring(got)))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, DisabledComboBoxDoesntOpen) {
    run(R"(
        s = state { i = 1 }
        root = Probe { ComboBox { model = { "a", "b", "c" }, currentIndex = bind(s, "i"), enabled = false } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    frames(3);
    EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup));
    click(a.x + 40.0f, b.y + dp(fluent::flyoutGap) + dp(fluent::flyoutPaddingY) + dp(32) * 1.5f);
    run(R"(assert(s.i == 1, tostring(s.i)))");
}

TEST_F(FluentInputsTest, ColorPickerSizes) {
    const ImVec2 plain = laidOut(R"(ColorPicker { color = "#336699" })");
    // paddingX, the swatch, a gap, the hex, a gap, the chevron, paddingX.
    const float natural = dp(12) + (dp(32) - 2.0f * dp(fluent::swatchInset)) + dp(8) +
                          md::measure(fluent::bodyStyle, "#336699").x + dp(8) + dp(16) + dp(12);
    EXPECT_FLOAT_EQ(plain.x, natural);
    EXPECT_FLOAT_EQ(plain.y, dp(32));
    EXPECT_FLOAT_EQ(laidOut(R"(ColorPicker { color = "#336699", label = "Seed" })").y, header() + dp(32));
    EXPECT_FLOAT_EQ(laidOut(R"(ColorPicker { color = "#336699", width = 300 })").x, 300.0f);
    applyTheme(R"(theme { design = "fluent", tokens = { density = 2 } })");
    EXPECT_FLOAT_EQ(laidOut(R"(ColorPicker { color = "#336699" })").y, dp(64));
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, ColorPickerPickingWritesBackHex) {
    run(R"(
        s = state { c = "#FFFFFF" }
        got = nil
        root = Probe { ColorPicker { color = bind(s, "c"), onColorChanged = function(_, hex) got = hex end } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); // opens the flyout
    frames(3);
    // The saturation/value square starts 16dp inside the flyout, which opens 4dp below the field.
    const float pad = dp(fluent::pickerPadding);
    click(a.x + pad + 100.0f, b.y + dp(fluent::flyoutGap) + pad + 100.0f);
    run(R"(assert(type(s.c) == "string" and s.c:match("^#%x%x%x%x%x%x$") and s.c ~= "#FFFFFF", tostring(s.c)))");
    run(R"(assert(got == s.c, tostring(got)))");
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(FluentInputsTest, DisabledColorPickerDoesntOpen) {
    run(R"(
        s = state { c = "#FFFFFF" }
        root = Probe { ColorPicker { color = bind(s, "c"), enabled = false } })");
    frames(2);
    const ImVec2 a = probe().min(), b = probe().max();
    click((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    frames(3);
    EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup));
    run(R"(assert(s.c == "#FFFFFF", tostring(s.c)))");
}
