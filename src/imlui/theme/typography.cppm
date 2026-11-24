// Typography: the Material 3 type scale and the Roboto text fonts.
module;

#include <array>
#include <filesystem>
#include <optional>
#include <string_view>

export module imlui.theme.typography;

import imgui;
import imlui.log;

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

/// Loads Roboto Regular/Medium from `dir`. Fonts are rasterized on demand at
/// any size (ImGui 1.92), so one source per weight is enough. Clears the font
/// atlas; call before loadIcons() and before the first frame.
void loadFonts(const std::filesystem::path& dir);

[[nodiscard]] ImFont* font(TextStyle s);
/// Size to pass to ImGui::PushFont (dp, before global scale factors).
[[nodiscard]] float fontSize(TextStyle s);
/// Final pixel size, for ImDrawList::AddText with an explicit font.
[[nodiscard]] float fontPixels(float sizeDp);
[[nodiscard]] float fontPixels(TextStyle s);
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

ImFont* regularFont = nullptr;
ImFont* mediumFont = nullptr;

} // namespace

const TextStyleInfo& textStyleInfo(TextStyle s) { return textStyles[static_cast<std::size_t>(s)]; }

std::optional<TextStyle> textStyleFromName(std::string_view name) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(TextStyle::Count); ++i)
        if (textStyles[i].name == name) return static_cast<TextStyle>(i);
    return std::nullopt;
}

void loadFonts(const std::filesystem::path& dir) {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    auto load = [&](const char* file) -> ImFont* {
        auto path = (dir / file).string();
        if (!std::filesystem::exists(path)) {
            log::warn("font '{}' not found", path);
            return nullptr;
        }
        ImFontConfig cfg;
        return atlas->AddFontFromFileTTF(path.c_str(), 16.0f, &cfg);
    };
    regularFont = load("Roboto-Regular.ttf");
    mediumFont = load("Roboto-Medium.ttf");
    if (!regularFont) regularFont = atlas->AddFontDefault();
    if (!mediumFont) mediumFont = regularFont;
    ImGui::GetIO().FontDefault = regularFont;
}

ImFont* font(TextStyle s) { return textStyleInfo(s).weight == Weight::Medium ? mediumFont : regularFont; }
float fontSize(TextStyle s) { return textStyleInfo(s).size; }
float fontPixels(float sizeDp) {
    const ImGuiStyle& st = ImGui::GetStyle();
    return sizeDp * st.FontScaleMain * st.FontScaleDpi;
}
float fontPixels(TextStyle s) { return fontPixels(fontSize(s)); }
void pushFont(TextStyle s) { ImGui::PushFont(font(s), fontSize(s)); }

} // namespace imlui
