// Theme: Material 3 color scheme generated from a seed color, shape scale,
// typography, and the mapping of all of it onto ImGuiStyle.
//
// A Theme is plain data. Lua builds one (see luaui / imlui.lua `ui.theme{}`),
// can override any color role, any raw ImGui color and any ImGuiStyle field,
// or pick a non-material preset entirely.
module;

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module imlui.theme;

import imgui;
import imlui.color;

export namespace imlui {

// ------------------------------------------------------------- color roles

struct Scheme {
    Color primary, onPrimary, primaryContainer, onPrimaryContainer;
    Color secondary, onSecondary, secondaryContainer, onSecondaryContainer;
    Color tertiary, onTertiary, tertiaryContainer, onTertiaryContainer;
    Color error, onError, errorContainer, onErrorContainer;
    Color surface, onSurface, surfaceVariant, onSurfaceVariant;
    Color surfaceDim, surfaceBright;
    Color surfaceContainerLowest, surfaceContainerLow, surfaceContainer;
    Color surfaceContainerHigh, surfaceContainerHighest;
    Color outline, outlineVariant;
    Color inverseSurface, inverseOnSurface, inversePrimary;
    Color scrim, shadow;
};

struct RoleEntry {
    std::string_view name;
    Color Scheme::* member;
};

inline constexpr std::array kRoles = {
    RoleEntry{"primary", &Scheme::primary},
    RoleEntry{"onPrimary", &Scheme::onPrimary},
    RoleEntry{"primaryContainer", &Scheme::primaryContainer},
    RoleEntry{"onPrimaryContainer", &Scheme::onPrimaryContainer},
    RoleEntry{"secondary", &Scheme::secondary},
    RoleEntry{"onSecondary", &Scheme::onSecondary},
    RoleEntry{"secondaryContainer", &Scheme::secondaryContainer},
    RoleEntry{"onSecondaryContainer", &Scheme::onSecondaryContainer},
    RoleEntry{"tertiary", &Scheme::tertiary},
    RoleEntry{"onTertiary", &Scheme::onTertiary},
    RoleEntry{"tertiaryContainer", &Scheme::tertiaryContainer},
    RoleEntry{"onTertiaryContainer", &Scheme::onTertiaryContainer},
    RoleEntry{"error", &Scheme::error},
    RoleEntry{"onError", &Scheme::onError},
    RoleEntry{"errorContainer", &Scheme::errorContainer},
    RoleEntry{"onErrorContainer", &Scheme::onErrorContainer},
    RoleEntry{"surface", &Scheme::surface},
    RoleEntry{"onSurface", &Scheme::onSurface},
    RoleEntry{"surfaceVariant", &Scheme::surfaceVariant},
    RoleEntry{"onSurfaceVariant", &Scheme::onSurfaceVariant},
    RoleEntry{"surfaceDim", &Scheme::surfaceDim},
    RoleEntry{"surfaceBright", &Scheme::surfaceBright},
    RoleEntry{"surfaceContainerLowest", &Scheme::surfaceContainerLowest},
    RoleEntry{"surfaceContainerLow", &Scheme::surfaceContainerLow},
    RoleEntry{"surfaceContainer", &Scheme::surfaceContainer},
    RoleEntry{"surfaceContainerHigh", &Scheme::surfaceContainerHigh},
    RoleEntry{"surfaceContainerHighest", &Scheme::surfaceContainerHighest},
    RoleEntry{"outline", &Scheme::outline},
    RoleEntry{"outlineVariant", &Scheme::outlineVariant},
    RoleEntry{"inverseSurface", &Scheme::inverseSurface},
    RoleEntry{"inverseOnSurface", &Scheme::inverseOnSurface},
    RoleEntry{"inversePrimary", &Scheme::inversePrimary},
    RoleEntry{"scrim", &Scheme::scrim},
    RoleEntry{"shadow", &Scheme::shadow},
};

Color* findRole(Scheme& s, std::string_view name) {
    for (auto& r : kRoles)
        if (r.name == name) return &(s.*r.member);
    return nullptr;
}
const Color* findRole(const Scheme& s, std::string_view name) {
    return findRole(const_cast<Scheme&>(s), name);
}

/// Generates a full Material 3 scheme from a seed color.
Scheme generateScheme(Color seed, bool dark) {
    Oklch o = toOklch(seed);
    float c = o.C < 0.12f ? 0.12f : (o.C > 0.22f ? 0.22f : o.C);
    TonalPalette P{o.h, c};
    TonalPalette S{o.h, 0.045f};
    TonalPalette T{o.h + 60.0f >= 360.0f ? o.h - 300.0f : o.h + 60.0f, 0.07f};
    TonalPalette N{o.h, 0.008f};
    TonalPalette NV{o.h, 0.018f};
    TonalPalette E{25.0f, 0.19f};

    Scheme s{};
    auto roles = [&](TonalPalette p, Color& base, Color& on, Color& cont, Color& onCont) {
        base = p.tone(dark ? 80 : 40);
        on = p.tone(dark ? 20 : 100);
        cont = p.tone(dark ? 30 : 90);
        onCont = p.tone(dark ? 90 : 10);
    };
    roles(P, s.primary, s.onPrimary, s.primaryContainer, s.onPrimaryContainer);
    roles(S, s.secondary, s.onSecondary, s.secondaryContainer, s.onSecondaryContainer);
    roles(T, s.tertiary, s.onTertiary, s.tertiaryContainer, s.onTertiaryContainer);
    roles(E, s.error, s.onError, s.errorContainer, s.onErrorContainer);

    if (dark) {
        s.surface = N.tone(6);            s.onSurface = N.tone(90);
        s.surfaceVariant = NV.tone(30);   s.onSurfaceVariant = NV.tone(80);
        s.surfaceDim = N.tone(6);         s.surfaceBright = N.tone(24);
        s.surfaceContainerLowest = N.tone(4);
        s.surfaceContainerLow = N.tone(10);
        s.surfaceContainer = N.tone(12);
        s.surfaceContainerHigh = N.tone(17);
        s.surfaceContainerHighest = N.tone(22);
        s.outline = NV.tone(60);          s.outlineVariant = NV.tone(30);
        s.inverseSurface = N.tone(90);    s.inverseOnSurface = N.tone(20);
        s.inversePrimary = P.tone(40);
    } else {
        s.surface = N.tone(98);           s.onSurface = N.tone(10);
        s.surfaceVariant = NV.tone(90);   s.onSurfaceVariant = NV.tone(30);
        s.surfaceDim = N.tone(87);        s.surfaceBright = N.tone(98);
        s.surfaceContainerLowest = N.tone(100);
        s.surfaceContainerLow = N.tone(96);
        s.surfaceContainer = N.tone(94);
        s.surfaceContainerHigh = N.tone(92);
        s.surfaceContainerHighest = N.tone(90);
        s.outline = NV.tone(50);          s.outlineVariant = NV.tone(80);
        s.inverseSurface = N.tone(20);    s.inverseOnSurface = N.tone(95);
        s.inversePrimary = P.tone(80);
    }
    s.scrim = {0, 0, 0, 1};
    s.shadow = {0, 0, 0, 1};
    return s;
}

// ------------------------------------------------------------- typography

enum class TextStyle : int {
    DisplayLarge, DisplayMedium, DisplaySmall,
    HeadlineLarge, HeadlineMedium, HeadlineSmall,
    TitleLarge, TitleMedium, TitleSmall,
    BodyLarge, BodyMedium, BodySmall,
    LabelLarge, LabelMedium, LabelSmall,
    Count
};

struct TextStyleInfo {
    std::string_view name;
    float size;
};

inline constexpr std::array<TextStyleInfo, static_cast<int>(TextStyle::Count)> kTextStyles = {{
    {"displayLarge", 57}, {"displayMedium", 45}, {"displaySmall", 36},
    {"headlineLarge", 32}, {"headlineMedium", 28}, {"headlineSmall", 24},
    {"titleLarge", 22}, {"titleMedium", 16}, {"titleSmall", 14},
    {"bodyLarge", 16}, {"bodyMedium", 14}, {"bodySmall", 12},
    {"labelLarge", 14}, {"labelMedium", 12}, {"labelSmall", 11},
}};

std::optional<TextStyle> textStyleFromName(std::string_view name) {
    for (int i = 0; i < static_cast<int>(TextStyle::Count); ++i)
        if (kTextStyles[i].name == name) return static_cast<TextStyle>(i);
    return std::nullopt;
}

namespace detail {
inline std::array<ImFont*, static_cast<int>(TextStyle::Count)> g_fonts{};
inline float g_dpi = 1.0f;
} // namespace detail

/// Loads the Material type scale from a TTF file (falls back to the ImGui
/// default font). Must run before the first frame.
void loadFonts(const std::string& ttfPath, float dpiScale) {
    detail::g_dpi = dpiScale;
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    const bool haveTtf = !ttfPath.empty() && std::filesystem::exists(ttfPath);
    if (!haveTtf) std::fprintf(stderr, "[imlui] font '%s' not found, using ImGui default font\n", ttfPath.c_str());

    // Body large is the default font, so add it first.
    // Latin + Latin-1, Cyrillic, general punctuation (— … “ ”), arrows (→).
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0400, 0x052F, 0x2000, 0x206F, 0x2190, 0x21FF, 0};
    auto add = [&](float px) -> ImFont* {
        ImFontConfig cfg;
        cfg.GlyphRanges = ranges;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        if (haveTtf) return atlas->AddFontFromFileTTF(ttfPath.c_str(), px * dpiScale, &cfg);
        cfg.SizePixels = px * dpiScale;
        return atlas->AddFontDefault(&cfg);
    };
    ImFont* body = add(kTextStyles[static_cast<int>(TextStyle::BodyLarge)].size);
    std::vector<std::pair<float, ImFont*>> bySize{{kTextStyles[static_cast<int>(TextStyle::BodyLarge)].size, body}};
    for (int i = 0; i < static_cast<int>(TextStyle::Count); ++i) {
        float sz = kTextStyles[i].size;
        ImFont* f = nullptr;
        for (auto& [s, font] : bySize)
            if (s == sz) f = font;
        if (!f) { f = add(sz); bySize.emplace_back(sz, f); }
        detail::g_fonts[i] = f;
    }
    ImGui::GetIO().FontDefault = body;
}

ImFont* font(TextStyle s) { return detail::g_fonts[static_cast<int>(s)]; }
float dpiScale() { return detail::g_dpi; }

// ------------------------------------------------------------- theme

struct Shape {
    float extraSmall = 4, small = 8, medium = 12, large = 16, extraLarge = 28;
};

/// One raw ImGuiStyle field override, by field name.
struct StyleOverride {
    std::string name;
    ImVec2 value;
};

struct Theme {
    /// "material" | "imgui-dark" | "imgui-light" | "imgui-classic"
    std::string preset = "material";
    Color seed = *parseHex("#6750A4");
    bool dark = true;
    Scheme scheme = generateScheme(seed, dark);
    Shape shape;
    float spacing = 8.0f;  ///< base spacing unit (dp)
    float fontScale = 1.0f;
    std::vector<std::pair<int, Color>> imguiColors; ///< ImGuiCol index -> color
    std::vector<StyleOverride> style;               ///< ImGuiStyle field overrides
};

namespace detail {
inline Theme g_theme{};
inline std::optional<Theme> g_pending{};

using F = float ImGuiStyle::*;
using V = ImVec2 ImGuiStyle::*;
struct StyleField { std::string_view name; std::variant<F, V> member; };
inline const StyleField kStyleFields[] = {
    {"Alpha", &ImGuiStyle::Alpha},
    {"DisabledAlpha", &ImGuiStyle::DisabledAlpha},
    {"WindowPadding", &ImGuiStyle::WindowPadding},
    {"WindowRounding", &ImGuiStyle::WindowRounding},
    {"WindowBorderSize", &ImGuiStyle::WindowBorderSize},
    {"WindowMinSize", &ImGuiStyle::WindowMinSize},
    {"WindowTitleAlign", &ImGuiStyle::WindowTitleAlign},
    {"ChildRounding", &ImGuiStyle::ChildRounding},
    {"ChildBorderSize", &ImGuiStyle::ChildBorderSize},
    {"PopupRounding", &ImGuiStyle::PopupRounding},
    {"PopupBorderSize", &ImGuiStyle::PopupBorderSize},
    {"FramePadding", &ImGuiStyle::FramePadding},
    {"FrameRounding", &ImGuiStyle::FrameRounding},
    {"FrameBorderSize", &ImGuiStyle::FrameBorderSize},
    {"ItemSpacing", &ImGuiStyle::ItemSpacing},
    {"ItemInnerSpacing", &ImGuiStyle::ItemInnerSpacing},
    {"CellPadding", &ImGuiStyle::CellPadding},
    {"IndentSpacing", &ImGuiStyle::IndentSpacing},
    {"ScrollbarSize", &ImGuiStyle::ScrollbarSize},
    {"ScrollbarRounding", &ImGuiStyle::ScrollbarRounding},
    {"GrabMinSize", &ImGuiStyle::GrabMinSize},
    {"GrabRounding", &ImGuiStyle::GrabRounding},
    {"TabRounding", &ImGuiStyle::TabRounding},
    {"TabBorderSize", &ImGuiStyle::TabBorderSize},
    {"TabBarBorderSize", &ImGuiStyle::TabBarBorderSize},
    {"TabBarOverlineSize", &ImGuiStyle::TabBarOverlineSize},
    {"ButtonTextAlign", &ImGuiStyle::ButtonTextAlign},
    {"SelectableTextAlign", &ImGuiStyle::SelectableTextAlign},
    {"SeparatorTextBorderSize", &ImGuiStyle::SeparatorTextBorderSize},
    {"SeparatorTextAlign", &ImGuiStyle::SeparatorTextAlign},
    {"SeparatorTextPadding", &ImGuiStyle::SeparatorTextPadding},
};

inline void applyMaterial(const Theme& t, ImGuiStyle& st) {
    const Scheme& s = t.scheme;
    const float sp = t.spacing;
    st.WindowPadding = {sp * 2, sp * 2};
    st.FramePadding = {sp * 1.5f, sp};
    st.ItemSpacing = {sp, sp};
    st.ItemInnerSpacing = {sp, sp * 0.5f};
    st.CellPadding = {sp, sp * 0.5f};
    st.IndentSpacing = sp * 3;
    st.ScrollbarSize = sp * 1.25f;
    st.GrabMinSize = sp * 1.5f;
    st.WindowRounding = t.shape.large;
    st.ChildRounding = t.shape.medium;
    st.PopupRounding = t.shape.extraSmall;
    st.FrameRounding = t.shape.extraSmall;
    st.ScrollbarRounding = 99.0f;
    st.GrabRounding = 99.0f;
    st.TabRounding = t.shape.small;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 1;
    st.PopupBorderSize = 0;
    st.FrameBorderSize = 0;
    st.TabBorderSize = 0;
    st.TabBarBorderSize = 1;
    st.TabBarOverlineSize = 0;
    st.SeparatorTextBorderSize = 1;
    st.WindowTitleAlign = {0.0f, 0.5f};
    st.DisabledAlpha = 0.38f;

    ImVec4* c = st.Colors;
    auto set = [&](ImGuiCol idx, Color col) { c[idx] = col.vec4(); };
    auto layer = [](Color base, Color on, float o) { return overlay(base, on, o); };

    set(ImGuiCol_Text, s.onSurface);
    set(ImGuiCol_TextDisabled, s.onSurfaceVariant.withAlpha(0.6f));
    set(ImGuiCol_WindowBg, s.surface);
    set(ImGuiCol_ChildBg, Color{0, 0, 0, 0});
    set(ImGuiCol_PopupBg, s.surfaceContainer);
    set(ImGuiCol_Border, s.outlineVariant);
    set(ImGuiCol_BorderShadow, Color{0, 0, 0, 0});
    set(ImGuiCol_FrameBg, s.surfaceContainerHighest);
    set(ImGuiCol_FrameBgHovered, layer(s.surfaceContainerHighest, s.onSurface, 0.08f));
    set(ImGuiCol_FrameBgActive, layer(s.surfaceContainerHighest, s.onSurface, 0.12f));
    set(ImGuiCol_TitleBg, s.surfaceContainer);
    set(ImGuiCol_TitleBgActive, s.surfaceContainerHigh);
    set(ImGuiCol_TitleBgCollapsed, s.surfaceContainer);
    set(ImGuiCol_MenuBarBg, s.surfaceContainer);
    set(ImGuiCol_ScrollbarBg, Color{0, 0, 0, 0});
    set(ImGuiCol_ScrollbarGrab, s.outlineVariant);
    set(ImGuiCol_ScrollbarGrabHovered, s.outline);
    set(ImGuiCol_ScrollbarGrabActive, s.onSurfaceVariant);
    set(ImGuiCol_CheckMark, s.primary);
    set(ImGuiCol_SliderGrab, s.primary);
    set(ImGuiCol_SliderGrabActive, layer(s.primary, s.onPrimary, 0.12f));
    set(ImGuiCol_Button, s.secondaryContainer);
    set(ImGuiCol_ButtonHovered, layer(s.secondaryContainer, s.onSecondaryContainer, 0.08f));
    set(ImGuiCol_ButtonActive, layer(s.secondaryContainer, s.onSecondaryContainer, 0.12f));
    set(ImGuiCol_Header, s.secondaryContainer);
    set(ImGuiCol_HeaderHovered, layer(s.surface, s.onSurface, 0.08f));
    set(ImGuiCol_HeaderActive, layer(s.secondaryContainer, s.onSecondaryContainer, 0.12f));
    set(ImGuiCol_Separator, s.outlineVariant);
    set(ImGuiCol_SeparatorHovered, s.outline);
    set(ImGuiCol_SeparatorActive, s.primary);
    set(ImGuiCol_ResizeGrip, s.primary.withAlpha(0.2f));
    set(ImGuiCol_ResizeGripHovered, s.primary.withAlpha(0.6f));
    set(ImGuiCol_ResizeGripActive, s.primary);
    set(ImGuiCol_Tab, s.surfaceContainer);
    set(ImGuiCol_TabHovered, layer(s.surfaceContainer, s.onSurface, 0.08f));
    set(ImGuiCol_TabSelected, s.secondaryContainer);
    set(ImGuiCol_TabSelectedOverline, s.primary);
    set(ImGuiCol_TabDimmed, s.surfaceContainer);
    set(ImGuiCol_TabDimmedSelected, s.surfaceContainerHigh);
    set(ImGuiCol_TabDimmedSelectedOverline, s.outline);
    set(ImGuiCol_PlotLines, s.primary);
    set(ImGuiCol_PlotLinesHovered, s.tertiary);
    set(ImGuiCol_PlotHistogram, s.primary);
    set(ImGuiCol_PlotHistogramHovered, s.tertiary);
    set(ImGuiCol_TableHeaderBg, s.surfaceContainerHigh);
    set(ImGuiCol_TableBorderStrong, s.outlineVariant);
    set(ImGuiCol_TableBorderLight, s.outlineVariant.withAlpha(0.5f));
    set(ImGuiCol_TableRowBg, Color{0, 0, 0, 0});
    set(ImGuiCol_TableRowBgAlt, s.onSurface.withAlpha(0.04f));
    set(ImGuiCol_TextLink, s.primary);
    set(ImGuiCol_TextSelectedBg, s.primary.withAlpha(0.35f));
    set(ImGuiCol_DragDropTarget, s.tertiary);
    set(ImGuiCol_NavCursor, s.secondary);
    set(ImGuiCol_NavWindowingHighlight, s.inverseSurface);
    set(ImGuiCol_NavWindowingDimBg, s.scrim.withAlpha(0.3f));
    set(ImGuiCol_ModalWindowDimBg, s.scrim.withAlpha(0.32f));
}

inline void applyNow(const Theme& t) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle{};
    if (t.preset == "imgui-light") ImGui::StyleColorsLight(&st);
    else if (t.preset == "imgui-classic") ImGui::StyleColorsClassic(&st);
    else if (t.preset == "imgui-dark") ImGui::StyleColorsDark(&st);
    else applyMaterial(t, st);

    for (const auto& o : t.style) {
        for (const auto& f : kStyleFields) {
            if (f.name != o.name) continue;
            if (auto* fp = std::get_if<F>(&f.member)) st.*(*fp) = o.value.x;
            else st.*std::get<V>(f.member) = o.value;
        }
    }
    for (const auto& [idx, col] : t.imguiColors)
        if (idx >= 0 && idx < ImGuiCol_COUNT) st.Colors[idx] = col.vec4();

    st.ScaleAllSizes(g_dpi);
    ImGui::GetIO().FontGlobalScale = t.fontScale;
}
} // namespace detail

/// The theme currently in effect.
const Theme& theme() { return detail::g_theme; }

/// Schedules `t` to become the theme at the start of the next frame
/// (safe to call from inside a frame, e.g. from a Lua click handler).
void setTheme(Theme t) { detail::g_pending = std::move(t); }

/// Applies a pending theme; call between frames. Returns true if it changed.
bool applyPendingTheme() {
    if (!detail::g_pending) return false;
    detail::g_theme = std::move(*detail::g_pending);
    detail::g_pending.reset();
    detail::applyNow(detail::g_theme);
    return true;
}

/// Whether a style field name is known (for validating Lua input).
bool isStyleField(std::string_view name) {
    for (const auto& f : detail::kStyleFields)
        if (f.name == name) return true;
    return false;
}

/// Looks up an ImGuiCol index by its name ("WindowBg", "Text", ...).
int imguiColorIndex(std::string_view name) {
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (name == ImGui::GetStyleColorName(i)) return i;
    return -1;
}

/// Resolves a color spec: a role name ("primary"), a hex string ("#ff0000")
/// or a role with alpha ("onSurface/0.6").
std::optional<Color> resolveColor(std::string_view spec) {
    if (spec.empty()) return std::nullopt;
    if (spec.front() == '#') return parseHex(spec);
    float alpha = -1.0f;
    if (auto slash = spec.find('/'); slash != std::string_view::npos) {
        std::string a{spec.substr(slash + 1)};
        alpha = static_cast<float>(std::atof(a.c_str()));
        spec = spec.substr(0, slash);
    }
    if (const Color* c = findRole(theme().scheme, spec)) return alpha >= 0 ? c->withAlpha(alpha) : *c;
    return parseHex(spec);
}

} // namespace imlui
