// Tests for the Material widgets (imlui.widgets.*) called directly in ImGui frames: the headless harness provides the
// ImGui context with the Roboto and icon fonts, and each test runs its own frames around the widget calls.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.common;
import imlui.skin.fluent.segmented;
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

// ------------------------------------------------------------------ icon

// icon()'s item is the square drawIcon() draws: never infinite or NaN (ImGui converts the cursor to int, and an
// `Icon { size = 1/0 }` was undefined there), at most maxIconPixels, empty for a size that isn't positive.
TEST_F(WidgetsTest, IconSizeIsFiniteAndMatchesWhatIsDrawn) {
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(md::iconSize(inf).x, md::maxIconPixels);
    EXPECT_EQ(md::iconSize(1e9f).y, md::maxIconPixels);
    EXPECT_EQ(md::iconSize(std::numeric_limits<float>::quiet_NaN()).x, 0.0f);
    EXPECT_EQ(md::iconSize(-8.0f).x, 0.0f);
    EXPECT_EQ(md::iconSize(-inf).y, 0.0f);
    EXPECT_FLOAT_EQ(md::iconSize(24.0f).x, dp(24.0f));
    frame([inf] {
        const float y = ImGui::GetCursorPosY();
        md::icon("star", inf);
        EXPECT_NEAR(ImGui::GetCursorPosY() - y, md::maxIconPixels + ImGui::GetStyle().ItemSpacing.y, 1.0f);
    });
}

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

// A line chart's filled area indexes its vertices with the draw list's own index type (they were cast to unsigned
// short, which wrapped onto earlier vertices with 32-bit indices). Next to 65535 vertices, 16-bit indices go on from
// a new VtxOffset: each quad's indices still land on its own four vertices.
TEST_F(WidgetsTest, ChartAreaIndicesReachTheirVertices) {
    for (const int padding : {100, 65533, 65534, 65535}) {
        frame([&] {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            for (int i = 0; i < padding; ++i) {
                dl->PrimReserve(0, 1);
                dl->PrimWriteVtx({-1.0f, -1.0f}, uv, 0);
            }
            const ImVec2 a{10.0f, 20.0f}, b{30.0f, 25.0f};
            md::fillAreaBelow(dl, a, b, 90.0f, 0xFF0000FFu, 0x000000FFu);
            const auto& cmd = dl->CmdBuffer.back();
            ASSERT_GE(cmd.ElemCount, 6u);
            const ImVec2 expected[] = {a, b, {b.x, 90.0f}, a, {b.x, 90.0f}, {a.x, 90.0f}};
            const unsigned first = cmd.IdxOffset + cmd.ElemCount - 6;
            for (unsigned i = 0; i < 6; ++i) {
                const unsigned vertex = cmd.VtxOffset + dl->IdxBuffer[static_cast<int>(first + i)];
                ASSERT_LT(vertex, static_cast<unsigned>(dl->VtxBuffer.Size)) << padding;
                EXPECT_EQ(dl->VtxBuffer[static_cast<int>(vertex)].pos.x, expected[i].x) << padding << ", corner " << i;
                EXPECT_EQ(dl->VtxBuffer[static_cast<int>(vertex)].pos.y, expected[i].y) << padding << ", corner " << i;
            }
            // And a whole filled chart past the boundary: every index reaches a vertex of its command.
            md::ChartOptions o;
            o.fill = true;
            md::chart("##chart", {1.0f, 4.0f, 2.0f, 5.0f, 3.0f}, o);
            for (const auto& c : dl->CmdBuffer)
                for (unsigned i = 0; i < c.ElemCount; ++i)
                    ASSERT_LT(c.VtxOffset + dl->IdxBuffer[static_cast<int>(c.IdxOffset + i)],
                              static_cast<unsigned>(dl->VtxBuffer.Size))
                        << padding;
        });
    }
}

// ------------------------------------------------------------------ animate

// A value in pixels settles within pixelEpsilon: a tab indicator sliding 300px stopped requesting frames about 20
// frames after it had come within a quarter pixel, at the fraction's 0.0015.
TEST_F(WidgetsTest, PixelValuesSettleWithinAQuarterPixel) {
    const auto framesToSettle = [&](ImGuiID id, float epsilon) {
        int frames = 0;
        frame([&] { md::animate(id, 0.0f, 16.0f, epsilon); });
        for (bool settled = false; !settled && frames < 1000; ++frames)
            frame([&] { settled = md::animate(id, 300.0f, 16.0f, epsilon) == 300.0f; });
        return frames;
    };
    const int fine = framesToSettle(0x51DE, md::fractionEpsilon);
    const int coarse = framesToSettle(0x51DF, md::pixelEpsilon);
    EXPECT_LT(coarse, fine - 15) << coarse << " vs " << fine;
    // Settled means at the target, and within a quarter pixel the frame before.
    float before = 0.0f, after = 0.0f;
    frame([&] { md::animate(0x51E0, 0.0f, 16.0f, md::pixelEpsilon); });
    for (int i = 0; i < coarse - 1; ++i)
        frame([&] { before = md::animate(0x51E0, 300.0f, 16.0f, md::pixelEpsilon); });
    frame([&] { after = md::animate(0x51E0, 300.0f, 16.0f, md::pixelEpsilon); });
    EXPECT_EQ(after, 300.0f);
    EXPECT_GE(300.0f - before, md::pixelEpsilon);
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

// A field narrower than its insets left a negative input width, which ImGui takes as "the available width minus
// that": the input stretched past the field, over whatever is beside it. The field's group (its input included) stays
// within its width.
TEST_F(WidgetsTest, NarrowFieldsKeepTheirInputInside) {
    const float w = dp(20);
    float x0 = 0.0f, field = 0.0f, multiline = 0.0f, spin = 0.0f;
    frame([&] {
        x0 = ImGui::GetCursorScreenPos().x;
        std::string text = "hello";
        md::TextFieldOptions options;
        options.width = w;
        md::textField("##field", text, options);
        field = ImGui::GetItemRectMax().x;
        options.multiline = true;
        md::textField("##multiline", text, options);
        multiline = ImGui::GetItemRectMax().x;
        double v = 1.0;
        md::spinBox("##spin", &v, 0.0, 10.0, 1.0, 0, {}, w);
        spin = ImGui::GetItemRectMax().x;
    });
    EXPECT_LE(field, x0 + w);
    EXPECT_LE(multiline, x0 + w);
    EXPECT_LE(spin, x0 + w);
}

// ------------------------------------------------------------------ SpinBox

// A step that isn't finite or positive: its magnitude, else 1. A NaN step made the value NaN, a zero one reported a
// change that wasn't, a negative one turned the buttons around.
TEST_F(WidgetsTest, SpinBoxStepIsFiniteAndPositive) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(md::spinStepSize(0.5), 0.5);
    EXPECT_EQ(md::spinStepSize(-2.0), 2.0);
    EXPECT_EQ(md::spinStepSize(0.0), 1.0);
    EXPECT_EQ(md::spinStepSize(nan), 1.0);
    EXPECT_EQ(md::spinStepSize(inf), 1.0);
    EXPECT_EQ(md::spinStepSize(-inf), 1.0);
}

// The value rules both skins' spin boxes share: a non-finite value (typed "nan", "inf") goes back, a change is
// reported only when the value differs, and a changed value is clamped (unless the range is empty).
TEST_F(WidgetsTest, SpinBoxValueRules) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    double v = nan; // typed "nan"
    EXPECT_FALSE(md::spinBoxApply(&v, 4.0, 0, 1.0, 0.0, 10.0));
    EXPECT_EQ(v, 4.0);
    v = inf;
    EXPECT_FALSE(md::spinBoxApply(&v, 4.0, 0, 1.0, 0.0, 10.0));
    EXPECT_EQ(v, 4.0);
    v = 4.0; // a NaN step steps by 1
    EXPECT_TRUE(md::spinBoxApply(&v, 4.0, 1, nan, 0.0, 10.0));
    EXPECT_EQ(v, 5.0);
    v = 4.0; // a negative step: the increment still increments
    EXPECT_TRUE(md::spinBoxApply(&v, 4.0, 1, -2.0, 0.0, 10.0));
    EXPECT_EQ(v, 6.0);
    v = 4.0; // a zero step steps by 1, so a press changes the value
    EXPECT_TRUE(md::spinBoxApply(&v, 4.0, -1, 0.0, 0.0, 10.0));
    EXPECT_EQ(v, 3.0);
    v = 9.5; // clamped to the range
    EXPECT_TRUE(md::spinBoxApply(&v, 9.5, 1, 1.0, 0.0, 10.0));
    EXPECT_EQ(v, 10.0);
    v = 10.0; // clamped back to where it was: no change
    EXPECT_FALSE(md::spinBoxApply(&v, 10.0, 1, 1.0, 0.0, 10.0));
    v = 20.0; // an empty range leaves the value alone
    EXPECT_TRUE(md::spinBoxApply(&v, 4.0, 0, 1.0, 10.0, 0.0));
    EXPECT_EQ(v, 20.0);
    v = nan; // a NaN value bound from the script: no change reported every frame
    EXPECT_FALSE(md::spinBoxApply(&v, nan, 0, 1.0, 0.0, 10.0));
    EXPECT_FALSE(md::spinBoxApply(&v, nan, 1, 1.0, 0.0, 10.0));
    EXPECT_TRUE(std::isnan(v));
}

// Typing "nan" into a spin box leaves its value alone and reports no change.
TEST_F(WidgetsTest, SpinBoxTypedNanIsIgnored) {
    double v = 3.0;
    bool changed = false;
    ImVec2 at{};
    const auto render = [&] {
        frame([&] {
            at = ImGui::GetCursorScreenPos();
            changed = md::spinBox("##spin", &v, 0.0, 10.0, 1.0, 0, {}) || changed;
        });
    };
    ImGuiIO& io = ImGui::GetIO();
    // Clicks into the number (which selects it all), types `text` and presses Enter (InputScalar applies it then).
    const auto type = [&](std::string_view text) {
        render();
        io.AddMousePosEvent(at.x + dp(30), at.y + dp(28));
        render();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render();
        for (const char ch : text)
            io.AddInputCharacter(static_cast<unsigned>(ch));
        render();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        render();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        render();
        render();
    };
    type("nan");
    EXPECT_EQ(v, 3.0);
    EXPECT_FALSE(changed);
    type("inf");
    EXPECT_EQ(v, 3.0);
    EXPECT_FALSE(changed);
    // A number typed the same way is taken.
    type("7");
    EXPECT_EQ(v, 7.0);
    EXPECT_TRUE(changed);
}

// ------------------------------------------------------------------ Slider

// The value logic every skin's slider shares: drag to a snapped, clamped value; ticks at the whole steps.
TEST_F(WidgetsTest, SliderValueLogic) {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    const md::SliderRange r = md::sliderRange(0.0, 1.0, 0.4);
    EXPECT_TRUE(r.ranged);
    double v = 0.0;
    EXPECT_TRUE(md::sliderDragTo(&v, r, 70.0f, 0.0f, 100.0f)); // 0.7 snaps to 0.8, the last whole step
    EXPECT_EQ(v, 0.8);
    EXPECT_FALSE(md::sliderDragTo(&v, r, 100.0f, 0.0f, 100.0f)); // 1.0 is no step: still 0.8, no change
    EXPECT_FLOAT_EQ(md::sliderFraction(v, r), 0.8f);
    EXPECT_EQ(md::sliderStops(r), 2); // 0, 0.4, 0.8
    EXPECT_FLOAT_EQ(md::sliderStopAt(r, 2), 0.8f);
    const md::SliderRange tenths = md::sliderRange(0.0, 1.0, 0.1);
    EXPECT_EQ(md::sliderStops(tenths), 10); // 1 / 0.1 = 9.999...: the last tick stays
    EXPECT_TRUE(md::sliderDragTo(&v, tenths, 30.0f, 0.0f, 100.0f));
    EXPECT_EQ(v, 0.3); // without binary noise
    // Continuous (a step that isn't finite, or <= 0): no ticks.
    for (const double step : {0.0, -1.0, nan, inf})
        EXPECT_EQ(md::sliderStops(md::sliderRange(0.0, 1.0, step)), -1) << step;
    EXPECT_EQ(md::sliderStops(md::sliderRange(0.0, 100.0, 1.0)), -1); // more than 40 steps
    // No range: the value is left alone.
    for (const auto& [lo, hi] : {std::pair{1.0, 1.0}, std::pair{2.0, 1.0}, std::pair{0.0, inf}, std::pair{nan, 1.0}}) {
        const md::SliderRange empty = md::sliderRange(lo, hi, 0.0);
        EXPECT_FALSE(empty.ranged);
        v = 0.5;
        EXPECT_FALSE(md::sliderDragTo(&v, empty, 50.0f, 0.0f, 100.0f));
        EXPECT_EQ(v, 0.5);
        EXPECT_EQ(md::sliderFraction(v, empty), 0.0f);
    }
}

// ------------------------------------------------------------------ ColorField

// While typing, only a whole #RRGGBB or #RRGGBBAA is applied ("#123" on the way to "#123456" isn't #112233); the
// short forms when the edit ends.
TEST_F(WidgetsTest, HexInputAppliesCompleteColorsWhileTyping) {
    Color c{0.0f, 0.0f, 0.0f, 1.0f};
    for (const char* partial : {"#", "#1", "#12", "#123", "#1234", "#12345", "#1234567", "123", "#zzzzzz"})
        EXPECT_FALSE(md::applyHexInput(&c, partial, false, true)) << partial;
    EXPECT_EQ(c, (Color{0.0f, 0.0f, 0.0f, 1.0f}));
    EXPECT_TRUE(md::applyHexInput(&c, "#336699", false, true));
    EXPECT_EQ(toHex(c), "#336699");
    EXPECT_TRUE(md::applyHexInput(&c, "33669980", false, true));
    EXPECT_EQ(toHex(c), "#33669980");
    EXPECT_TRUE(md::applyHexInput(&c, "#33669980", false, false)); // no alpha: opaque
    EXPECT_EQ(toHex(c), "#336699");
    // When the edit ends, the short forms too.
    EXPECT_TRUE(md::applyHexInput(&c, "#123", true, true));
    EXPECT_EQ(toHex(c), "#112233");
    EXPECT_TRUE(md::applyHexInput(&c, "#1234", true, true));
    EXPECT_EQ(toHex(c), "#11223344");
    EXPECT_FALSE(md::applyHexInput(&c, "#12", true, true));
    EXPECT_EQ(toHex(c), "#11223344");
}

// The picker body keeps a short form typed in the hex box until the edit ends, then applies it; the box's own buffer
// (made from the color each frame) doesn't hold it by then.
TEST_F(WidgetsTest, ColorPickerBodyCommitsShortHexWhenTheEditEnds) {
    Color c{1.0f, 1.0f, 1.0f, 1.0f};
    std::string typed; // what the "user" typed in the hex box this frame (empty: nothing)
    bool ends = false; // the box loses focus this frame
    bool changed = false;
    const auto render = [&] {
        frame([&] {
            std::string hex = toHex(c);
            const md::HexField box = [&](std::string& value, const md::TextFieldOptions&) {
                md::TextFieldResult r;
                if (!typed.empty()) {
                    value = typed;
                    r.changed = true;
                }
                r.committed = ends;
                return r;
            };
            changed = md::colorPickerBody(&c, hex, false, dp(240), box);
        });
    };
    for (const char* step : {"#1", "#12", "#123"}) {
        typed = step;
        render();
        EXPECT_FALSE(changed) << step;
        EXPECT_EQ(toHex(c), "#FFFFFF") << step;
    }
    typed.clear();
    ends = true;
    render();
    EXPECT_TRUE(changed);
    EXPECT_EQ(toHex(c), "#112233");
    // A whole color applies while typing.
    ends = false;
    typed = "#336699";
    render();
    EXPECT_TRUE(changed);
    EXPECT_EQ(toHex(c), "#336699");
}

// ------------------------------------------------------------------ Chart and shapes

// The hovered point is the finite one nearest the mouse (the earlier on a tie): found from the mouse's x, then moved
// past the gaps. The hover line marks it.
TEST_F(WidgetsTest, ChartHoverFindsTheNearestFinitePoint) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> values{1.0f, nan, nan, nan, 3.0f, 2.0f, nan, 5.0f};
    const Color hoverLine = md::scheme().outline.withAlpha(0.6f);
    const int n = static_cast<int>(values.size());
    for (float mouse = 0.0f; mouse < 420.0f; mouse += 7.0f) {
        ImGui::GetIO().AddMousePosEvent(mouse, 60.0f);
        float x0 = 0.0f, x1 = 0.0f;
        std::vector<Vertex> drawn;
        frame([&] {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            md::ChartOptions o;
            o.width = 400.0f;
            x0 = p.x + dp(40);
            x1 = p.x + 400.0f - dp(8);
            drawn = verticesOf([&] { md::chart("##chart", values, o); });
        });
        const auto px = [&](int i) { return x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(n - 1); };
        int expected = -1;
        float best = md::hugeExtent;
        for (int i = 0; i < n; ++i)
            if (std::isfinite(values[static_cast<std::size_t>(i)]) && std::fabs(px(i) - mouse) < best) {
                best = std::fabs(px(i) - mouse);
                expected = i;
            }
        const ImU32 packed = md::col(hoverLine);
        float lineX = -1.0f;
        for (const Vertex& v : drawn)
            if (v.col == packed) lineX = v.pos.x;
        if (mouse < 8.0f || mouse >= 400.0f) continue; // outside the chart's item: nothing hovered
        ASSERT_GE(lineX, 0.0f) << "mouse at " << mouse;
        EXPECT_NEAR(lineX, std::floor(px(expected) + 0.5f), 1.01f) << "mouse at " << mouse;
    }
}

// A line with far more points than pixel columns draws each column's lowest and highest point: about as many
// vertices as for a few hundred points, all finite.
TEST_F(WidgetsTest, ChartDecimatesLinesWiderThanItsPixels) {
    std::vector<float> many(200000);
    for (std::size_t i = 0; i < many.size(); ++i)
        many[i] = std::sin(static_cast<float>(i) * 0.01f);
    many[1234] = std::numeric_limits<float>::quiet_NaN(); // a gap
    std::vector<Vertex> drawn, few;
    frame([&] {
        md::ChartOptions o;
        o.width = 400.0f;
        drawn = verticesOf([&] { md::chart("##many", many, o); });
        few = verticesOf([&] { md::chart("##few", std::vector<float>(many.begin(), many.begin() + 700), o); });
    });
    ASSERT_FALSE(drawn.empty());
    EXPECT_LT(drawn.size(), few.size() * 2);
    for (const Vertex& v : drawn)
        ASSERT_TRUE(std::isfinite(v.pos.x) && std::isfinite(v.pos.y));
}

// Circles and rounded corners are built by rotating one point (one sincos per arc): every vertex still lies on the
// circle, within the anti-aliased fringe, all the way round.
TEST_F(WidgetsTest, ArcsStayOnTheirCircle) {
    for (const float r : {3.0f, 20.0f, 150.0f, 900.0f}) {
        std::vector<Vertex> drawn;
        const ImVec2 c{500.0f, 400.0f};
        frame([&] {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect({-5000.0f, -5000.0f}, {5000.0f, 5000.0f});
            drawn = verticesOf([&] { md::fillCircle(dl, c, r, {1.0f, 0.0f, 0.0f, 1.0f}); });
            dl->PopClipRect();
        });
        ASSERT_FALSE(drawn.empty());
        float minX = md::hugeExtent, maxX = -md::hugeExtent;
        for (const Vertex& v : drawn) {
            const float d = std::hypot(v.pos.x - c.x, v.pos.y - c.y);
            ASSERT_NEAR(d, r, 1.0f) << "radius " << r;
            minX = std::min(minX, v.pos.x);
            maxX = std::max(maxX, v.pos.x);
        }
        EXPECT_NEAR(maxX - minX, 2.0f * r, 1.5f) << "radius " << r;
    }
}

// ------------------------------------------------------------------ Fluent segmented

// A Fluent segmented button narrower than its insets gives each segment at least a pixel: ImGui takes a 0-wide hit box
// as the available width, so every segment's box lay across the row and a click beside the control picked one.
TEST_F(WidgetsTest, NarrowFluentSegmentedKeepsItsHitBoxesInside) {
    const fluent::Look look{fluent::themeTokens(), fluent::themeColors()};
    const std::vector<std::string> items{"A", "B", "C"};
    int current = 1;
    ImVec2 at{};
    const auto render = [&] {
        frame([&] {
            at = ImGui::GetCursorScreenPos();
            fluent::segmented(look, "##seg", items, &current, 1.0f, {});
        });
    };
    render();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x + 150.0f, at.y + dp(8));
    render();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    render();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    render();
    EXPECT_EQ(current, 1);
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

// ------------------------------------------------------------------ Culling

namespace {

/// A widget for the culling tests, drawn at the cursor.
struct CulledWidget {
    const char* name;
    std::function<void()> draw;
};

std::vector<CulledWidget> culledWidgets() {
    static bool on = true;
    static double value = 40.0;
    static int segment = 1;
    static const std::vector<std::string> segments{"One", "Two"};
    static const std::vector<float> series{1.0f, 3.0f, 2.0f, 4.0f};
    return {
        {"checkbox", [] { md::checkbox("Check##checkbox", &on); }},
        {"radio", [] { md::radio("Radio##radio", true); }},
        {"switch", [] { md::switchToggle("Switch##switch", &on); }},
        {"chip", [] { md::chip("Chip##chip", true); }},
        {"segmented", [] { md::segmented("##segmented", segments, &segment); }},
        {"slider", [] { md::slider("##slider", &value, 0.0, 100.0, 10.0); }},
        {"iconButton", [] { md::iconButton("##iconButton", "star", md::IconButtonVariant::Outlined); }},
        {"button", [] { md::button("Button##button", md::ButtonVariant::Elevated, {0, 0}, "add"); }},
        {"circularProgress", [] { md::circularProgress(0.4f, dp(24)); }},
        {"progress", [] { md::progress(0.4f, dp(200)); }},
        {"icon", [] { md::icon("star"); }},
        {"divider", [] { md::divider(0.0f, 0.0f, dp(200)); }},
        {"lineChart", [] { md::chart("##lines", series, {}); }},
        {"barChart",
         [] {
             md::ChartOptions o;
             o.bars = true;
             md::chart("##bars", series, o);
         }},
    };
}

/// What drawing a widget added to its window's draw list.
struct Drawn {
    int vertices = 0;
    int commands = 0;
    ImVec2 size{}; ///< of the last item
};

} // namespace

class WidgetCullingTest : public WidgetsTest {
protected:
    /// One frame with `draw` at (20, 20) in a 400 x 300 scrolling child window scrolled to `scroll`.
    template <class F>
    Drawn viewFrame(ImVec2 scroll, F&& draw) {
        Drawn d;
        frame([&] {
            ImGui::SetNextWindowScroll(scroll);
            ImGui::BeginChild("##view", {400.0f, 300.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::SetCursorPos({20.0f, 20.0f});
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int vertices = dl->VtxBuffer.Size, commands = dl->CmdBuffer.Size;
            std::forward<F>(draw)();
            d = {dl->VtxBuffer.Size - vertices, dl->CmdBuffer.Size - commands, ImGui::GetItemRectSize()};
            ImGui::SetCursorPos({2000.0f, 2000.0f}); // room to scroll both ways
            ImGui::Dummy({1.0f, 1.0f});
            ImGui::EndChild();
        });
        return d;
    }

    /// Runs `frames` frames and returns the last one's.
    template <class F>
    Drawn viewFrames(ImVec2 scroll, int frames, F&& draw) {
        Drawn d;
        for (int i = 0; i < frames; ++i)
            d = viewFrame(scroll, draw);
        return d;
    }
};

// A widget scrolled out of view, down or sideways, builds no geometry; scrolled back, it draws exactly as before.
TEST_F(WidgetCullingTest, OutOfViewWidgetsBuildNoGeometry) {
    for (const CulledWidget& w : culledWidgets()) {
        const Drawn inView = viewFrames({0.0f, 0.0f}, 30, w.draw);
        ASSERT_GT(inView.vertices, 0) << w.name;
        for (const ImVec2 away : {ImVec2{0.0f, 1000.0f}, ImVec2{1000.0f, 0.0f}}) {
            const Drawn out = viewFrames(away, 3, w.draw);
            EXPECT_EQ(out.vertices, 0) << w.name << " scrolled by " << away.x << ", " << away.y;
            EXPECT_EQ(out.commands, 0) << w.name << " scrolled by " << away.x << ", " << away.y;
            const Drawn back = viewFrame({0.0f, 0.0f}, w.draw);
            EXPECT_EQ(back.vertices, inView.vertices) << w.name << " back from " << away.x << ", " << away.y;
        }
    }
}

// A widget partly in view still draws: culling goes by the whole shape, not the item's corner.
TEST_F(WidgetCullingTest, PartlyVisibleWidgetsDraw) {
    for (const CulledWidget& w : culledWidgets()) {
        const ImVec2 size = viewFrames({0.0f, 0.0f}, 30, w.draw).size;
        // Scrolled so the view's edge cuts the widget in half.
        const float cutY = 20.0f + std::floor(size.y * 0.5f), cutX = 20.0f + std::floor(size.x * 0.5f);
        EXPECT_GT(viewFrames({0.0f, cutY}, 3, w.draw).vertices, 0) << w.name << " cut at the top";
        EXPECT_GT(viewFrames({cutX, 0.0f}, 3, w.draw).vertices, 0) << w.name << " cut at the left";
    }
}

// Animations run on out of view: a checkbox toggled while scrolled away comes back fully checked, drawn like one
// that was checked in view all along.
TEST_F(WidgetCullingTest, AnimationsContinueOutOfView) {
    bool reference = true;
    const Drawn checked = viewFrames({0.0f, 0.0f}, 60, [&] { md::checkbox("##reference", &reference); });
    bool v = false;
    const auto draw = [&] { md::checkbox("##check", &v); };
    viewFrames({0.0f, 0.0f}, 30, draw);
    v = true;
    viewFrames({0.0f, 1000.0f}, 60, draw);
    EXPECT_EQ(viewFrame({0.0f, 0.0f}, draw).vertices, checked.vertices);
}

// A widget takes a press on the very frame it scrolls back into view.
TEST_F(WidgetCullingTest, InteractionWorksOnTheFrameAWidgetScrollsIn) {
    bool v = false;
    ImVec2 center{};
    bool active = false;
    const auto draw = [&] {
        md::checkbox("##check", &v);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        center = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        active = ImGui::IsItemActive();
    };
    viewFrames({0.0f, 0.0f}, 3, draw);
    const ImVec2 target = center;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(target.x, target.y);
    viewFrames({0.0f, 1000.0f}, 3, draw);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    EXPECT_GT(viewFrame({0.0f, 0.0f}, draw).vertices, 0);
    EXPECT_TRUE(active) << "pressed on the frame it scrolled in";
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    viewFrame({0.0f, 0.0f}, draw);
    EXPECT_TRUE(v) << "clicked";
}
