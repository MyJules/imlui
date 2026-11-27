// The skin seam (imlui.skin): a C++ skin registered by name and picked with `theme { design = ... }` draws and
// measures the controls it overrides, the others stay Material; an unknown design is reported and keeps Material;
// a design switch takes effect between frames, and layouts follow the new sizes.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.lua.errors;
import imlui.skin;
import imlui.theme.theme;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.widgets.button;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

/// The size the test skin gives every button, in pixels.
constexpr ImVec2 testButton{123.0f, 45.0f};

/// What the test skin was asked to do.
int buttonDraws = 0;
int buttonMeasures = 0;
int brokenImages = 0;
/// The test skin's NavigationBar height, in pixels.
constexpr float testBarHeight = 77.0f;

/// Overrides the button only: every other control falls back to Material.
class TestSkin final : public Skin {
public:
    [[nodiscard]] std::string_view name() const override { return "test"; }
    bool button(const char*, md::ButtonVariant, ImVec2, std::string_view) const override {
        ++buttonDraws;
        ImGui::Dummy(testButton);
        return false;
    }
    [[nodiscard]] ImVec2 buttonSize(const char*, md::ButtonVariant, ImVec2, std::string_view) const override {
        ++buttonMeasures;
        return testButton;
    }
    [[nodiscard]] ImVec2 navigationBarSize(float width) const override { return {width, testBarHeight}; }
    void brokenImage(ImVec2, ImVec2, float) const override { ++brokenImages; }
};

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

class SkinTest : public ::testing::Test {
protected:
    SkinTest() {
        registerSkin("test", [] { return std::make_unique<TestSkin>(); });
        m_h.registerType<Spot>("Spot");
        buttonDraws = 0;
        buttonMeasures = 0;
        brokenImages = 0;
    }
    ~SkinTest() override {
        setTheme(Theme{});
        applyPendingTheme();
    }

    void SetUp() override { applyTheme("theme {}"); } // the default theme's style (its window padding) from the start
    void run(std::string_view code) { ASSERT_EQ(m_h.run(code), ""); }
    /// The x distance from Spot `a`'s item to Spot `b`'s, less `a`'s 1px: what lies between them.
    float between(std::string_view a, std::string_view b) {
        return m_h.instance<Spot>(b)->pos().x - m_h.instance<Spot>(a)->pos().x - 1.0f;
    }
    /// Applies the theme the script set, between frames, as the Engine does.
    void applyTheme(std::string_view code) {
        run(code);
        ASSERT_TRUE(applyPendingTheme());
    }

    test::HeadlessFrame m_h{test::HeadlessFrame::Options{.builtins = true}};
};

} // namespace

TEST_F(SkinTest, MaterialIsTheDefault) {
    EXPECT_EQ(Theme{}.design, "material");
    EXPECT_TRUE(hasSkin("material"));
    EXPECT_TRUE(hasSkin("test"));
    EXPECT_TRUE(hasSkin("fluent")); // built in
    EXPECT_FALSE(hasSkin("nope"));
    EXPECT_EQ(skin().name(), "material");
}

// "material" is made from its own class, MaterialSkin, like "fluent" from FluentSkin.
TEST_F(SkinTest, MaterialIsAMaterialSkin) {
    EXPECT_NE(dynamic_cast<const MaterialSkin*>(&skin()), nullptr);
    EXPECT_EQ(MaterialSkin{}.name(), material::designName);
}

// The test skin draws and measures the Button; a Checkbox, which it doesn't override, keeps its Material size.
TEST_F(SkinTest, ARegisteredSkinDrawsTheControlsItOverrides) {
    run(R"(
        a, b, c, d = Spot {}, Spot {}, Spot {}, Spot {}
        root = Column { Row { spacing = 0, a, Button { "OK" }, b },
                        Row { spacing = 0, c, Checkbox { "Check" }, d } })");
    m_h.renderFrames("root", 2);
    const float materialButton = between("a", "b");
    const float materialCheckbox = between("c", "d");
    EXPECT_NE(materialButton, testButton.x);
    EXPECT_EQ(buttonDraws, 0);

    applyTheme(R"(theme { design = "test" })");
    EXPECT_EQ(theme().design, "test");
    EXPECT_EQ(skin().name(), "test");
    m_h.renderFrames("root", 2);
    EXPECT_FLOAT_EQ(between("a", "b"), testButton.x);
    EXPECT_FLOAT_EQ(between("c", "d"), materialCheckbox);
    EXPECT_GT(buttonDraws, 0);
    EXPECT_TRUE(luaui::errors().empty());

    // Back to Material: the same sizes as before.
    applyTheme(R"(theme { design = "material" })");
    buttonDraws = 0;
    m_h.renderFrames("root", 2);
    EXPECT_FLOAT_EQ(between("a", "b"), materialButton);
    EXPECT_EQ(buttonDraws, 0);
}

// The NavigationBar's height and a broken Image's placeholder come from the skin too.
TEST_F(SkinTest, NavigationBarHeightAndBrokenImageComeFromTheSkin) {
    run(R"(
        a, b = Spot {}, Spot {}
        root = Column { spacing = 0, a, NavigationBar { width = 300 }, b, Image { source = "app:/missing.png" } })");
    m_h.renderFrames("root", 2);
    const auto down = [&] { return m_h.instance<Spot>("b")->pos().y - m_h.instance<Spot>("a")->pos().y - 1.0f; };
    const float material = down();
    EXPECT_NE(material, testBarHeight);
    EXPECT_EQ(brokenImages, 0);

    applyTheme(R"(theme { design = "test" })");
    m_h.renderFrames("root", 2);
    EXPECT_FLOAT_EQ(down(), testBarHeight);
    EXPECT_GT(brokenImages, 0);
}

// Layouts that place by sizes (a right-justified Row) use the new skin's on the first frame after the switch.
TEST_F(SkinTest, ADesignSwitchLaysOutAtTheNewSizesOnTheNextFrame) {
    run(R"(
        a, b = Spot {}, Spot {}
        root = Row { justify = "end", width = 400, spacing = 0, a, Button { "OK" }, b })");
    m_h.renderFrames("root", 3);
    const float right = m_h.instance<Spot>("b")->pos().x;
    EXPECT_NE(between("a", "b"), testButton.x);

    applyTheme(R"(theme { design = "test" })");
    m_h.renderFrame("root");
    EXPECT_GT(buttonMeasures, 0);
    EXPECT_FLOAT_EQ(m_h.instance<Spot>("b")->pos().x, right);
    EXPECT_FLOAT_EQ(between("a", "b"), testButton.x);
}

TEST_F(SkinTest, AnUnknownDesignIsReportedAndKeepsMaterial) {
    luaui::clearErrors();
    applyTheme(R"(theme { design = "nope" })");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_NE(luaui::errors()[0].find("theme.design: unknown design 'nope'"), std::string::npos) << luaui::errors()[0];
    EXPECT_NE(luaui::errors()[0].find("'material'"), std::string::npos) << luaui::errors()[0];
    EXPECT_EQ(theme().design, "material");
    EXPECT_EQ(skin().name(), "material");

    luaui::clearErrors();
    applyTheme(R"(theme { design = 5 })");
    EXPECT_EQ(luaui::errors().size(), 1u);
    EXPECT_EQ(theme().design, "material");
    luaui::clearErrors();
}

// A Theme built in C++ isn't checked against the registry: an unknown name draws Material.
TEST_F(SkinTest, AnUnknownDesignFromCxxDrawsMaterial) {
    Theme t;
    t.design = "nope";
    setTheme(t);
    ASSERT_TRUE(applyPendingTheme());
    EXPECT_EQ(theme().design, "nope");
    EXPECT_EQ(skin().name(), "material");
}

// The active skin changes only with the theme: registering doesn't switch in the middle of a frame.
TEST_F(SkinTest, ARegistrationTakesEffectWithTheNextThemeChange) {
    applyTheme(R"(theme { design = "test" })");
    const Skin* before = &skin();
    registerSkin("test", [] { return std::make_unique<TestSkin>(); });
    EXPECT_EQ(&skin(), before);
    setTheme(theme());
    ASSERT_TRUE(applyPendingTheme());
    EXPECT_NE(&skin(), before);
    EXPECT_EQ(skin().name(), "test");
}
