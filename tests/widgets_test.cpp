// Tests for the Material widgets (imlui.widgets.*) called directly in ImGui frames: the headless harness provides the
// ImGui context with the Roboto and icon fonts, and each test runs its own frames around the widget calls.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.widgets;
import imlui.test.headless_frame;

using namespace imlui;

namespace {

using Vertex = std::remove_pointer_t<decltype(std::declval<ImDrawList&>().VtxBuffer.Data)>;

class WidgetsTest : public ::testing::Test {
protected:
    /// One ImGui frame: `body` runs inside a full-display window.
    template <class F>
    void frame(F&& body) {
        ImGui::NewFrame();
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##widgets", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        std::forward<F>(body)();
        ImGui::End();
        ImGui::Render();
    }

    /// The vertices `draw` adds to the current window's draw list.
    template <class F>
    static std::vector<Vertex> verticesOf(F&& draw) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int before = dl->VtxBuffer.Size;
        std::forward<F>(draw)();
        return {dl->VtxBuffer.Data + before, dl->VtxBuffer.Data + dl->VtxBuffer.Size};
    }

    static bool hasColor(const std::vector<Vertex>& vertices, Color c) {
        const ImU32 packed = md::col(c);
        return std::ranges::any_of(vertices, [&](const Vertex& v) { return v.col == packed; });
    }

    test::HeadlessFrame m_h; ///< the ImGui context (with its own style) and the fonts, per test
};

} // namespace

// ------------------------------------------------------------------ Snackbar

// A width of 0 is a widget's natural width, never a fill: whether an element fills by default is its type's policy
// (TypeBuilder::fillsWidth), and the element passes -1 then.
TEST_F(WidgetsTest, AZeroWidthIsTheNaturalWidth) {
    float card = 0.0f, field = 0.0f, slider = 0.0f, filled = 0.0f;
    frame([&] {
        md::beginCard("##card", md::CardVariant::Filled);
        md::endCard();
        card = ImGui::GetItemRectSize().x;
        std::string text;
        md::textField("##field", text, {});
        field = ImGui::GetItemRectSize().x;
        double v = 0.5;
        md::slider("##slider", &v, 0.0, 1.0);
        slider = ImGui::GetItemRectSize().x;
        md::slider("##filled", &v, 0.0, 1.0, 0.0, -1.0f);
        filled = ImGui::GetItemRectSize().x;
    });
    EXPECT_FLOAT_EQ(card, dp(320));
    EXPECT_FLOAT_EQ(field, dp(280));
    EXPECT_FLOAT_EQ(slider, dp(200));
    EXPECT_GT(filled, dp(500));
}

TEST_F(WidgetsTest, ClosableSnackbarKeepsItsWidthWhateverTheThemeSpacing) {
    // A theme spacing other than the snackbar's own 8dp gap made the auto-resized window wider every frame. The
    // width is now computed from the content: message + gaps + actions + padding, at least 344dp.
    for (const std::string_view message :
         {std::string_view{"Saved"}, std::string_view{"A message long enough to widen the bar"}}) {
        for (const float spacing : {4.0f, 8.0f, 20.0f}) {
            ImGui::GetStyle().ItemSpacing.x = spacing;
            for (const bool withAction : {false, true}) {
                const std::string_view action = withAction ? "Undo" : "";
                float content = md::measure(TextStyle::BodyMedium, message).x + dp(48) + 2.0f * dp(16);
                if (withAction) content += dp(8) + md::measure(TextStyle::LabelLarge, action).x + dp(24);
                const float expected = std::max(dp(344), content);
                std::vector<float> widths;
                for (int i = 0; i < 12; ++i) {
                    frame([&] {
                        bool closed = false;
                        md::snackbar(message, action, 1.0f, true, &closed);
                        // Appends to the snackbar's window to read its size.
                        ImGui::Begin(md::scopedWindowName("", "snackbar").c_str());
                        widths.push_back(ImGui::GetWindowWidth());
                        ImGui::End();
                    });
                }
                for (std::size_t i = 0; i < widths.size(); ++i)
                    EXPECT_FLOAT_EQ(widths[i], expected)
                        << message << ", spacing " << spacing << ", action " << withAction << ", frame " << i;
            }
        }
    }
}

TEST_F(WidgetsTest, SnackbarWidthIsClampedAndFitsANarrowViewport) {
    EXPECT_FLOAT_EQ(md::snackbarWidth(dp(100), dp(800)), dp(344)); // the minimum
    EXPECT_FLOAT_EQ(md::snackbarWidth(dp(500), dp(800)), dp(500)); // the content
    EXPECT_FLOAT_EQ(md::snackbarWidth(dp(900), dp(800)), dp(672)); // the maximum
    EXPECT_FLOAT_EQ(md::snackbarWidth(dp(900), dp(600)), dp(568)); // the viewport less 16dp each side
    EXPECT_FLOAT_EQ(md::snackbarWidth(dp(100), dp(300)), dp(268)); // ... even below the minimum
}

// A message longer than the widest bar wraps inside it (it used to run out of the window and be clipped): the bar
// keeps its computed width, grows by a line (68dp for two lines), and every glyph stays within the padding.
TEST_F(WidgetsTest, ALongSnackbarMessageWrapsInsideTheBar) {
    const std::string message = "A message far too long for one line of a snackbar, which therefore wraps onto a "
                                "second line instead of running out of the bar and being clipped by its edge";
    ASSERT_GT(md::measure(TextStyle::BodyMedium, message).x, dp(672));
    const float viewport = ImGui::GetIO().DisplaySize.x > 0.0f ? ImGui::GetIO().DisplaySize.x : 800.0f;
    std::vector<ImVec2> sizes;
    float textLeft = 1e9f, textRight = -1e9f, textTop = 1e9f, textBottom = -1e9f;
    ImVec2 pos{};
    const ImU32 textColor = md::col(md::scheme().inverseOnSurface);
    for (int i = 0; i < 6; ++i) {
        frame([&] {
            bool closed = false;
            md::snackbar(message, "Undo", 1.0f, false, &closed);
            ImGui::Begin(md::scopedWindowName("", "snackbar").c_str());
            sizes.push_back(ImGui::GetWindowSize());
            pos = ImGui::GetWindowPos();
            const ImDrawList* dl = ImGui::GetWindowDrawList();
            textLeft = textTop = 1e9f;
            textRight = textBottom = -1e9f;
            for (const Vertex& v : dl->VtxBuffer) {
                if (v.col != textColor) continue;
                textLeft = std::min(textLeft, v.pos.x);
                textRight = std::max(textRight, v.pos.x);
                textTop = std::min(textTop, v.pos.y);
                textBottom = std::max(textBottom, v.pos.y);
            }
            ImGui::End();
        });
    }
    const float width = md::snackbarWidth(dp(10000), viewport);
    for (std::size_t i = 1; i < sizes.size(); ++i) {
        EXPECT_FLOAT_EQ(sizes[i].x, width) << i;
        EXPECT_FLOAT_EQ(sizes[i].y, sizes[1].y) << i;
    }
    EXPECT_GE(sizes.back().y, dp(68) + 2.0f * dp(4)); // two lines at least, and the vertical padding
    ASSERT_LT(textLeft, textRight);                   // the message was drawn
    EXPECT_GE(textLeft, pos.x + dp(16) - 1.0f);
    EXPECT_LE(textRight, pos.x + width - dp(16) + 1.0f);
    EXPECT_GE(textTop, pos.y);
    EXPECT_LE(textBottom, pos.y + sizes.back().y);

    // Lines: word-wrapped, at newlines too, without blanks at their ends.
    const std::vector<std::string_view> lines = md::snackbarLines(message, dp(400));
    EXPECT_GE(lines.size(), 2u);
    for (const std::string_view line : lines) {
        EXPECT_LE(md::measure(TextStyle::BodyMedium, line).x, dp(400) + 0.5f) << line;
        EXPECT_FALSE(line.empty());
        EXPECT_NE(line.front(), ' ');
        EXPECT_NE(line.back(), ' ');
    }
    EXPECT_EQ(md::snackbarLines("one\ntwo", dp(400)), (std::vector<std::string_view>{"one", "two"}));
    EXPECT_EQ(md::snackbarLines("", dp(400)).size(), 1u);
}

// ------------------------------------------------------------------ Menus

TEST_F(WidgetsTest, AnAnchoredMenuFitsItsWidestRowAndShrinksBack) {
    // Rows and fills used to take the menu's current width, so a menu kept the widest width it ever had.
    std::string label = "Open";
    float menuWidth = 0.0f, rowWidth = 0.0f, fill = 0.0f;
    bool opened = false;
    const auto settle = [&](std::string_view what) {
        std::vector<float> widths;
        for (int i = 0; i < 10; ++i) {
            frame([&] {
                if (!opened) ImGui::OpenPopup("##menu");
                opened = true;
                if (md::beginAnchoredMenu("##menu", {10, 10}, {60, 30}, dp(112))) {
                    md::menuItem("##short", "Cut");
                    rowWidth = ImGui::GetItemRectSize().x;
                    md::menuItem("##label", label);
                    md::menuDivider();
                    fill = md::itemWidth(-1.0f, 0.0f);
                    ImGui::Dummy({fill, 1.0f});
                    menuWidth = ImGui::GetWindowWidth();
                    md::endAnchoredMenu();
                }
            });
            widths.push_back(menuWidth);
        }
        for (std::size_t i = 3; i < widths.size(); ++i)
            EXPECT_EQ(widths[i], widths[2]) << what << ", frame " << i;
        // Every row and every fill is the menu's width (which ImGui truncates to whole pixels).
        EXPECT_NEAR(rowWidth, menuWidth, 1.0f) << what;
        EXPECT_NEAR(fill, menuWidth, 1.0f) << what;
        return menuWidth;
    };
    const float narrow = settle("short rows");
    EXPECT_FLOAT_EQ(narrow, dp(160)); // a row's minimum
    label = "A menu entry long enough to widen the menu";
    EXPECT_GT(settle("a long row"), narrow + dp(20));
    label = "Paste";
    EXPECT_FLOAT_EQ(settle("short again"), narrow);
}

// ------------------------------------------------------------------ Chart

TEST_F(WidgetsTest, ChartSkipsNonFiniteValues) {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const std::vector<float> values{1.0f, nan, 3.0f, inf, 2.0f, -inf, 4.0f, nan};
    for (const bool bars : {false, true}) {
        // The mouse sweeps across the chart, so every point (gaps included) gets hovered once.
        for (float x = 10.0f; x < 800.0f; x += 40.0f) {
            ImGui::GetIO().AddMousePosEvent(x, 60.0f);
            std::vector<Vertex> drawn;
            frame([&] {
                md::ChartOptions options;
                options.bars = bars;
                drawn = verticesOf([&] { md::chart("##chart", values, options); });
            });
            ASSERT_FALSE(drawn.empty());
            for (const Vertex& v : drawn)
                ASSERT_TRUE(std::isfinite(v.pos.x) && std::isfinite(v.pos.y)) << "bars " << bars << ", mouse at " << x;
        }
    }
}

// ------------------------------------------------------------------ Tabs

TEST_F(WidgetsTest, TabIndicatorMovesWithTheBar) {
    const std::vector<md::TabInfo> items{{"One", ""}, {"Two", ""}, {"Three", ""}};
    const Color primary = md::scheme().primary;
    int current = 1;
    const auto tabsAt = [&](float x) {
        std::vector<Vertex> drawn;
        frame([&] {
            ImGui::SetCursorScreenPos({x, 20.0f});
            drawn = verticesOf([&] { md::tabs("##tabs", items, &current, false, 300.0f); });
        });
        return drawn;
    };
    for (int i = 0; i < 4; ++i)
        tabsAt(10.0f);
    // The bar moves (scrolling, resizing): the indicator moves with it in the same frame instead of sliding over.
    const auto drawn = tabsAt(310.0f);
    float minX = std::numeric_limits<float>::max(), maxX = 0.0f;
    for (const Vertex& v : drawn)
        if (v.col == md::col(primary)) {
            minX = std::min(minX, v.pos.x);
            maxX = std::max(maxX, v.pos.x);
        }
    // The selected tab (label and indicator in primary) spans 410..510.
    EXPECT_GE(minX, 410.0f);
    EXPECT_LE(maxX, 510.0f);

    // No selection: no indicator (it used to slide to x = 0).
    current = -1;
    for (int i = 0; i < 3; ++i)
        EXPECT_FALSE(hasColor(tabsAt(310.0f), primary)) << "frame " << i;
}

// ------------------------------------------------------------------ Fields

TEST_F(WidgetsTest, FieldDisabledScopeIsTheItemFlagNotTheAlpha) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 0.95f;        // a translucent theme
    style.DisabledAlpha = 1.0f; // disabled items not dimmed
    frame([&] {
        EXPECT_FALSE(md::field::scopeDisabled());
        ImGui::BeginDisabled();
        EXPECT_TRUE(md::field::scopeDisabled());
        ImGui::EndDisabled();
        EXPECT_FALSE(md::field::scopeDisabled());
    });
}

TEST_F(WidgetsTest, FieldHoverDoesntShowThroughAPopup) {
    const Color hoverLayer = md::scheme().onSurface.withAlpha(0.08f);
    std::string text;
    ImGui::GetIO().AddMousePosEvent(100.0f, 30.0f); // over the field
    const auto fieldFrame = [&](bool popup) {
        std::vector<Vertex> drawn;
        frame([&] {
            drawn = verticesOf([&] { md::textField("##field", text, {}); });
            if (popup) {
                if (!ImGui::IsPopupOpen("##popup")) ImGui::OpenPopup("##popup");
                ImGui::SetNextWindowPos({50.0f, 10.0f});
                if (ImGui::BeginPopup("##popup")) {
                    ImGui::Dummy({200.0f, 100.0f});
                    ImGui::EndPopup();
                }
            }
        });
        return drawn;
    };
    for (int i = 0; i < 2; ++i)
        fieldFrame(false);
    EXPECT_TRUE(hasColor(fieldFrame(false), hoverLayer)) << "hovered";
    for (int i = 0; i < 2; ++i)
        fieldFrame(true);
    EXPECT_FALSE(hasColor(fieldFrame(true), hoverLayer)) << "under the popup";
}

TEST_F(WidgetsTest, ShortMultilineTextFieldKeepsItsHeight) {
    // A height below the input's top padding left a negative input height, which ImGui takes as "the available
    // height minus that": the input grew to the window's height.
    std::string text = "hello";
    float height = 0.0f;
    frame([&] {
        md::TextFieldOptions options;
        options.multiline = true;
        options.height = dp(20);
        md::textField("##field", text, options);
        height = ImGui::GetItemRectSize().y; // the field's group, its input included
    });
    EXPECT_GT(height, 0.0f);
    EXPECT_LT(height, dp(80));
}

// ------------------------------------------------------------------ Navigation

TEST_F(WidgetsTest, DisabledSelectedNavigationItemLooksDisabled) {
    ImGui::GetStyle().DisabledAlpha = 1.0f; // compare the widgets' own colors
    const Scheme& s = md::scheme();
    for (int i = 0; i < 2; ++i) { // the second frame: the indicator's animation has its stored value
        std::vector<Vertex> drawn;
        frame([&] {
            ImGui::BeginDisabled();
            drawn = verticesOf([&] { md::navigationItem("##nav", "home", "Home", true); });
            ImGui::EndDisabled();
        });
        EXPECT_TRUE(hasColor(drawn, md::disabledContent())) << "label and icon";
        EXPECT_TRUE(hasColor(drawn, md::disabledContainer())) << "indicator";
        EXPECT_FALSE(hasColor(drawn, s.onSecondaryContainer));
        EXPECT_FALSE(hasColor(drawn, s.secondaryContainer));
    }
}
