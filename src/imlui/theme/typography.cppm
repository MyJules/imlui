// Typography: the Material 3 type scale and the Roboto text fonts.
export module imlui.theme.typography;

import std;

import imgui;
import imlui.resources;
import imlui.theme.font_library;

export namespace imlui {

enum class TextStyle : int {
    DisplayLarge, DisplayMedium, DisplaySmall,
    HeadlineLarge, HeadlineMedium, HeadlineSmall,
    TitleLarge, TitleMedium, TitleSmall,
    BodyLarge, BodyMedium, BodySmall,
    LabelLarge, LabelMedium, LabelSmall,
    Count
};

enum class Weight { Regular, Medium };

struct TextStyleInfo {
    std::string_view name;
    float size; ///< dp
    Weight weight;
};

/// Name, size and weight of a text style (Material 3 type scale).
[[nodiscard]] const TextStyleInfo& textStyleInfo(TextStyle s);

/// The style named `name` ("bodyLarge", "titleSmall", ...).
std::optional<TextStyle> textStyleFromName(std::string_view name);

/// Clears the font atlas, points the font library at `resources` (see resetFontLibrary()) and loads the built-in
/// Roboto Regular/Medium (`imlui:/fonts/Roboto-Regular.ttf`, `-Medium.ttf`) as the faces in use. Fonts are
/// rasterized on demand at any size (ImGui 1.92), so one source per weight is enough. `resources` (its mounts) must
/// outlive the font atlas. Call before loadIcons() and before the first frame. A theme's FontSet replaces the faces
/// when the theme is applied.
void loadFonts(const Resources& resources);

/// The font of style `s`: the face override of the scope (FontFaceScope), else the theme's face for its weight;
/// ImGui's current font before loadFonts().
[[nodiscard]] ImFont* font(TextStyle s);
/// The largest text size in pixels: ImGui's own limit for a pushed font (IMGUI_FONT_SIZE_MAX), which fontPixels()
/// keeps to as well, so text measured or drawn with an explicit size never outgrows what PushFont() gives. Together
/// with font_check's maxGlyphExtent() it bounds a glyph's bitmap to half the side of ImGui's 8192-pixel font atlas:
/// the size is the theme's fontScale times the zoom times the monitor scale times the style's size, which reaches
/// thousands of pixels at the extremes (57dp displayLarge x 4 x 3 x a 2x monitor), and a glyph larger than the atlas
/// trips ImGui's "Out of texture memory" assert.
inline constexpr float maxTextPixels = 512.0f;

/// Size to pass to ImGui::PushFont (dp, before global scale factors).
[[nodiscard]] float fontSize(TextStyle s);
/// Final pixel size, for ImDrawList::AddText with an explicit font: at most maxTextPixels.
[[nodiscard]] float fontPixels(float sizeDp);
[[nodiscard]] float fontPixels(TextStyle s);
/// ImGui::GetFontSize() inside pushFont(s), without pushing it: fontPixels(s) rounded to a whole pixel and kept in
/// 1..maxTextPixels, as ImGui does (UpdateCurrentFontSize). The height of a line of `s` text in an ImGui item.
[[nodiscard]] float linePixels(TextStyle s);
void pushFont(TextStyle s);

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Material 3 type scale (Roboto: Regular for display/headline/body, Medium for titles/labels).
constexpr std::array<TextStyleInfo, static_cast<int>(TextStyle::Count)> textStyles = {{
    {"displayLarge", 57, Weight::Regular}, {"displayMedium", 45, Weight::Regular}, {"displaySmall", 36, Weight::Regular},
    {"headlineLarge", 32, Weight::Regular}, {"headlineMedium", 28, Weight::Regular}, {"headlineSmall", 24, Weight::Regular},
    {"titleLarge", 22, Weight::Regular}, {"titleMedium", 16, Weight::Medium}, {"titleSmall", 14, Weight::Medium},
    {"bodyLarge", 16, Weight::Regular}, {"bodyMedium", 14, Weight::Regular}, {"bodySmall", 12, Weight::Regular},
    {"labelLarge", 14, Weight::Medium}, {"labelMedium", 12, Weight::Medium}, {"labelSmall", 11, Weight::Medium},
}};

} // namespace

const TextStyleInfo& textStyleInfo(TextStyle s) { return textStyles[static_cast<std::size_t>(s)]; }

std::optional<TextStyle> textStyleFromName(std::string_view name) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(TextStyle::Count); ++i)
        if (textStyles[i].name == name) return static_cast<TextStyle>(i);
    return std::nullopt;
}

void loadFonts(const Resources& resources) {
    ImGui::GetIO().Fonts->Clear();
    resetFontLibrary(resources);
    useFontSet({});
}

ImFont* font(TextStyle s) {
    if (ImFont* f = faceOverride()) return f;
    ImFont* f = textStyleInfo(s).weight == Weight::Medium ? mediumFace() : regularFace();
    return f ? f : ImGui::GetFont();
}
float fontSize(TextStyle s) { return textStyleInfo(s).size; }
float fontPixels(float sizeDp) {
    const ImGuiStyle& st = ImGui::GetStyle();
    // The same products in the same order as ImGui's (FontSizeBase * FontScaleMain * FontScaleDpi), so linePixels()
    // matches GetFontSize() to the bit.
    return std::min(sizeDp * st.FontScaleMain * st.FontScaleDpi, maxTextPixels);
}
float fontPixels(TextStyle s) { return fontPixels(fontSize(s)); }
float linePixels(TextStyle s) {
    // IM_ROUND, then ImGui's clamp: (float)(int)(v + 0.5f) is floor(v + 0.5f) for the positive sizes here.
    return std::clamp(std::floor(fontPixels(s) + 0.5f), 1.0f, maxTextPixels);
}
void pushFont(TextStyle s) { ImGui::PushFont(font(s), fontSize(s)); }

} // namespace imlui
