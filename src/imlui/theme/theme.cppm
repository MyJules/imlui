// Theme: color scheme, shape scale and spacing, and the mapping of all of it
// onto ImGuiStyle.
//
// A Theme is plain data. Lua builds one (see imlui.lua.theme_table and
// src/resources/imlui.lua `ui.theme{}`), can override any color role, any raw ImGui
// color and any ImGuiStyle field, or pick a non-material preset entirely.
export module imlui.theme.theme;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.theme.font_library;
import imlui.theme.scheme;
import imlui.theme.skin_tokens;

export namespace imlui {

/// How ImGui's own style is set: from the scheme (Material, or the Fluent colors under design "fluent"), or one of
/// ImGui's stock styles. Its Lua names ("material", "imgui-dark", ...) are in imlui.ui.enum_names.
enum class ThemePreset { Material, ImguiDark, ImguiLight, ImguiClassic };

struct Theme {
    /// Material 3 shape scale: corner radii in dp.
    struct Shape {
        float extraSmall = 4, small = 8, medium = 12, large = 16, extraLarge = 28;
    };

    /// One raw ImGuiStyle field override, by field name.
    struct StyleOverride {
        std::string name;
        ImVec2 value;
    };

    /// The skin that draws the controls, by its registered name ("material", or one registered with registerSkin()).
    /// Only the name is kept here: the widgets import the theme and the skins import the widgets, so imlui.skin
    /// resolves it (skin(), after the theme changed). An unknown name draws Material.
    std::string design = "material";
    /// Overrides of the design's SkinTokens, by name, applied in order over its defaults (a token-driven skin's
    /// tokens(); the Material skin ignores them). Each must pass tokenProblem(); setTheme() drops one that doesn't.
    std::vector<TokenOverride> tokens;
    /// Under Material, design "fluent" maps the Fluent colors and its tokens onto ImGuiStyle instead of the Material
    /// scheme mapping.
    ThemePreset preset = ThemePreset::Material;
    Color seed = *parseHex("#6750A4");
    bool dark = true;
    Scheme scheme = generateScheme(seed, dark);
    Shape shape;                                    ///< each in 0..maxStyleValue
    float spacing = 8.0f;                           ///< base spacing unit (dp), in 0..maxStyleValue
    float fontScale = 1.0f;                         ///< in minFontScale..maxFontScale
    std::vector<std::pair<int, Color>> imguiColors; ///< ImGuiCol index -> color
    std::vector<StyleOverride> style;               ///< ImGuiStyle field overrides
    FontSet fonts;                                  ///< type-scale faces, named fonts and fallbacks
};

/// The range of Theme::fontScale. A larger scale would make glyphs that outgrow ImGui's font atlas (font_check's
/// maxGlyphExtent() assumes sizes this side of it).
inline constexpr float minFontScale = 0.25f;
inline constexpr float maxFontScale = 4.0f;
/// The largest Theme::spacing, Theme::shape radius and magnitude of a style value (dp, before the DPI scale).
inline constexpr float maxStyleValue = 10000.0f;

/// Why `value` can't set ImGuiStyle field `name` ("" when it can, or when `name` isn't a field): a component that
/// isn't finite or whose magnitude exceeds maxStyleValue, an `Alpha` or `DisabledAlpha` outside 0..1, or a
/// `WindowMinSize` below 1 (ImGui asserts on those). A scalar field uses `value.x` only.
[[nodiscard]] std::string styleValueProblem(std::string_view name, ImVec2 value);

/// The theme currently in effect.
[[nodiscard]] const Theme& theme();

/// A count that moves every time the theme in effect changes (applyPendingTheme()). What depends on the theme beyond
/// the frame it was computed on (a color role resolved by a property sync, the active skin) compares it.
[[nodiscard]] std::uint64_t themeGeneration();

/// The tokens design "fluent" draws with under `t`: fluentTokens() with `t.tokens` applied.
[[nodiscard]] SkinTokens fluentTokensFor(const Theme& t);

/// Schedules `t` to become the theme at the start of the next frame
/// (safe to call from inside a frame, e.g. from a Lua click handler). Values out of range are brought into it:
/// `fontScale` is clamped (not finite: 1), `spacing` and `shape` radii too (not finite: the default), colors (`seed`,
/// the scheme's roles, `imguiColors`) to 0..1 (clampColor: NaN is 0), and style overrides with a styleValueProblem()
/// are dropped, and so are token overrides with a tokenProblem(). imlui.lua.theme_table reports them to the script first.
void setTheme(Theme t);

/// Applies a pending theme, its fonts included (useFontSet()); call between frames. Returns true if it changed.
bool applyPendingTheme();

/// Re-applies the current theme to ImGuiStyle at the current dpiScale() (after a
/// monitor DPI or zoom change). Call between frames.
void reapplyTheme();

/// Whether a style field name is known (for validating Lua input).
[[nodiscard]] bool isStyleField(std::string_view name);
/// The ImGuiStyle field names a theme's `style` can override, in declaration order.
[[nodiscard]] std::vector<std::string_view> styleFieldNames();

/// Looks up an ImGuiCol index by its name ("WindowBg", "Text", ...); -1 if unknown.
[[nodiscard]] int imguiColorIndex(std::string_view name);

/// Resolves a color spec: a role name ("primary"), a hex string ("#ff0000")
/// or a role with alpha ("onSurface/0.6", the alpha a number in 0..1). Roles come from the current theme.
/// nullopt when the spec is none of these (an unknown role, bad hex, an alpha that isn't a number in 0..1).
[[nodiscard]] std::optional<Color> resolveColor(std::string_view spec);

/// Like resolveColor(spec), but roles are looked up in `scheme`.
[[nodiscard]] std::optional<Color> resolveColor(std::string_view spec, const Scheme& scheme);

} // namespace imlui

module :private;

namespace imlui {

namespace {

Theme currentTheme{};
std::optional<Theme> pendingTheme{};
std::uint64_t generation = 0; ///< themeGeneration()

using FloatField = float ImGuiStyle::*;
using VecField = ImVec2 ImGuiStyle::*;

struct StyleField {
    std::string_view name;
    std::variant<FloatField, VecField> member;
};

const StyleField styleFields[] = {
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

void applyMaterial(const Theme& t, ImGuiStyle& st) {
    const Scheme& s = t.scheme;
    const float sp = t.spacing;
    st.WindowPadding = {sp * 2, sp * 2};
    st.FramePadding = {sp * 1.5f, sp};
    st.ItemSpacing = {sp, sp};
    st.ItemInnerSpacing = {sp, sp * 0.5f};
    st.CellPadding = {sp, sp * 0.5f};
    st.IndentSpacing = sp * 3;
    st.ScrollbarSize = sp * 1.5f;
    st.ScrollbarPadding = sp * 0.25f;
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
    set(ImGuiCol_InputTextCursor, s.primary);
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
    set(ImGuiCol_HeaderHovered, s.onSurface.withAlpha(0.08f));
    set(ImGuiCol_HeaderActive, s.onSurface.withAlpha(0.12f));
    set(ImGuiCol_Separator, s.outlineVariant);
    set(ImGuiCol_SeparatorHovered, s.outline);
    set(ImGuiCol_SeparatorActive, s.primary);
    set(ImGuiCol_ResizeGrip, s.primary.withAlpha(0.2f));
    set(ImGuiCol_ResizeGripHovered, s.primary.withAlpha(0.6f));
    set(ImGuiCol_ResizeGripActive, s.primary);
    set(ImGuiCol_Tab, s.surfaceContainer);
    set(ImGuiCol_TabHovered, s.onSurface.withAlpha(0.08f));
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

/// Design "fluent" under the "material" preset: the Material mapping, then the Fluent colors and the tokens' geometry
/// over it, so raw ImGui (scrollbars, popups, InputText, trees, menu bars) matches the Fluent controls.
void applyFluent(const Theme& t, ImGuiStyle& st) {
    applyMaterial(t, st);
    const SkinTokens k = fluentTokensFor(t);
    const fluent::Colors c = fluent::colorsFor(t.scheme, t.dark);
    const float pad = k.paddingX * k.density;
    // ImGui's frame height is the font size (16dp base, times fontScale) plus twice FramePadding.y.
    const float frameY = std::max(0.0f, (k.controlHeight * k.density - 16.0f * t.fontScale) * 0.5f);
    st.FramePadding = {pad, frameY};
    st.ItemInnerSpacing = {k.gap * k.density, st.ItemInnerSpacing.y};
    st.WindowRounding = k.overlayRadius;
    st.ChildRounding = k.overlayRadius;
    st.PopupRounding = k.overlayRadius;
    st.FrameRounding = k.controlRadius;
    st.TabRounding = k.controlRadius;
    st.GrabRounding = k.controlRadius;
    st.WindowBorderSize = k.strokeWidth;
    st.PopupBorderSize = k.strokeWidth;
    st.FrameBorderSize = k.strokeWidth;
    st.ChildBorderSize = k.strokeWidth;
    st.TabBarOverlineSize = 0;

    ImVec4* col = st.Colors;
    auto set = [&](ImGuiCol idx, Color v) { col[idx] = v.vec4(); };
    const Color clear{0, 0, 0, 0};
    set(ImGuiCol_Text, c.text);
    set(ImGuiCol_TextDisabled, c.textDisabled);
    set(ImGuiCol_WindowBg, c.background);
    set(ImGuiCol_ChildBg, clear);
    set(ImGuiCol_PopupBg, c.flyout);
    set(ImGuiCol_InputTextCursor, c.text);
    set(ImGuiCol_Border, c.cardStroke);
    set(ImGuiCol_FrameBg, c.control);
    set(ImGuiCol_FrameBgHovered, c.controlHover);
    set(ImGuiCol_FrameBgActive, c.controlInputActive);
    set(ImGuiCol_TitleBg, c.background);
    set(ImGuiCol_TitleBgActive, c.background);
    set(ImGuiCol_TitleBgCollapsed, c.background);
    set(ImGuiCol_MenuBarBg, c.background);
    set(ImGuiCol_ScrollbarBg, clear);
    set(ImGuiCol_ScrollbarGrab, c.controlStrongStroke);
    set(ImGuiCol_ScrollbarGrabHovered, c.textSecondary);
    set(ImGuiCol_ScrollbarGrabActive, c.text);
    set(ImGuiCol_CheckMark, c.accent);
    set(ImGuiCol_SliderGrab, c.accent);
    set(ImGuiCol_SliderGrabActive, c.accentPressed);
    set(ImGuiCol_Button, c.control);
    set(ImGuiCol_ButtonHovered, c.controlHover);
    set(ImGuiCol_ButtonActive, c.controlPressed);
    set(ImGuiCol_Header, c.subtleHover);
    set(ImGuiCol_HeaderHovered, c.subtleHover);
    set(ImGuiCol_HeaderActive, c.subtlePressed);
    set(ImGuiCol_Separator, c.divider);
    set(ImGuiCol_SeparatorHovered, c.controlStrongStroke);
    set(ImGuiCol_SeparatorActive, c.accent);
    set(ImGuiCol_ResizeGrip, clear);
    set(ImGuiCol_ResizeGripHovered, c.controlStrongStroke);
    set(ImGuiCol_ResizeGripActive, c.accent);
    set(ImGuiCol_Tab, clear);
    set(ImGuiCol_TabHovered, c.subtleHover);
    set(ImGuiCol_TabSelected, c.layer);
    set(ImGuiCol_TabSelectedOverline, c.accent);
    set(ImGuiCol_TabDimmed, clear);
    set(ImGuiCol_TabDimmedSelected, c.layer);
    set(ImGuiCol_TabDimmedSelectedOverline, clear);
    set(ImGuiCol_PlotLines, c.accent);
    set(ImGuiCol_PlotHistogram, c.accent);
    set(ImGuiCol_TableHeaderBg, c.subtleHover);
    set(ImGuiCol_TableBorderStrong, c.divider);
    set(ImGuiCol_TableBorderLight, c.divider);
    set(ImGuiCol_TableRowBg, clear);
    set(ImGuiCol_TableRowBgAlt, c.subtlePressed);
    set(ImGuiCol_TextLink, c.accentText);
    set(ImGuiCol_TextSelectedBg, c.accent.withAlpha(0.4f));
    set(ImGuiCol_NavCursor, c.focusOuter);
    set(ImGuiCol_NavWindowingDimBg, c.smoke);
    set(ImGuiCol_ModalWindowDimBg, c.smoke);
}

void applyNow(const Theme& t) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle{};
    switch (t.preset) {
    case ThemePreset::ImguiLight: ImGui::StyleColorsLight(&st); break;
    case ThemePreset::ImguiClassic: ImGui::StyleColorsClassic(&st); break;
    case ThemePreset::ImguiDark: ImGui::StyleColorsDark(&st); break;
    case ThemePreset::Material:
        if (t.design == fluent::designName)
            applyFluent(t, st);
        else
            applyMaterial(t, st);
        break;
    }

    for (const auto& o : t.style) {
        for (const auto& f : styleFields) {
            if (f.name != o.name) continue;
            if (const auto* fp = std::get_if<FloatField>(&f.member))
                st.*(*fp) = o.value.x;
            else
                st.*std::get<VecField>(f.member) = o.value;
        }
    }
    for (const auto& [idx, col] : t.imguiColors)
        if (idx >= 0 && idx < ImGuiCol_COUNT) st.Colors[idx] = col.vec4();

    // Geometry quality, for every preset: smoother ImGui-drawn circles and roundings (child windows, cards,
    // scrollbars, popups) than the default 0.30px error, and anti-aliasing on (ImGuiStyle's default, made explicit).
    st.CircleTessellationMaxError = 0.10f;
    st.AntiAliasedLines = true;
    st.AntiAliasedLinesUseTex = true;
    st.AntiAliasedFill = true;

    const float scale = dpiScale();
    st.ScaleAllSizes(scale);
    // ScaleAllSizes truncates: below a scale of 1 an accepted WindowMinSize of 1 becomes 0, and NewFrame asserts on
    // either of these below 1.
    st.WindowMinSize = {std::max(st.WindowMinSize.x, 1.0f), std::max(st.WindowMinSize.y, 1.0f)};
    st.WindowBorderHoverPadding = std::max(st.WindowBorderHoverPadding, 1.0f);
    // Menus and popups store ItemSpacing.x as an unsigned 16-bit width (ImGuiMenuColumns::Update): a negative one, or
    // one past 65535 (an accepted 10000 at a large zoom and monitor scale), would be undefined there.
    st.ItemSpacing.x = std::clamp(st.ItemSpacing.x, 0.0f, 65535.0f);
    st.FontSizeBase = 16.0f;
    st.FontScaleDpi = scale;
    st.FontScaleMain = t.fontScale;
}

} // namespace

const Theme& theme() { return currentTheme; }
SkinTokens fluentTokensFor(const Theme& t) { return applyTokenOverrides(fluentTokens(), t.tokens); }
std::uint64_t themeGeneration() { return generation; }

void setTheme(Theme t) {
    const Theme defaults;
    const auto bounded = [](float& v, float lo, float hi, float fallback) {
        v = std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    bounded(t.fontScale, minFontScale, maxFontScale, defaults.fontScale);
    bounded(t.spacing, 0.0f, maxStyleValue, defaults.spacing);
    for (auto [radius, fallback] :
         {std::pair{&t.shape.extraSmall, defaults.shape.extraSmall}, std::pair{&t.shape.small, defaults.shape.small},
          std::pair{&t.shape.medium, defaults.shape.medium}, std::pair{&t.shape.large, defaults.shape.large},
          std::pair{&t.shape.extraLarge, defaults.shape.extraLarge}})
        bounded(*radius, 0.0f, maxStyleValue, fallback);
    std::erase_if(t.style, [](const Theme::StyleOverride& o) { return !styleValueProblem(o.name, o.value).empty(); });
    std::erase_if(t.tokens, [](const TokenOverride& o) { return !tokenProblem(o.name, o.value).empty(); });
    // Widgets and ImGui turn colors into bytes with IM_F32_TO_INT8_SAT, which casts NaN to int (undefined).
    t.seed = clampColor(t.seed);
    for (const auto& role : roles())
        t.scheme.*role.member = clampColor(t.scheme.*role.member);
    for (auto& entry : t.imguiColors)
        entry.second = clampColor(entry.second);
    pendingTheme = std::move(t);
}

std::string styleValueProblem(std::string_view name, ImVec2 value) {
    const auto field = std::ranges::find(styleFields, name, &StyleField::name);
    if (field == std::ranges::end(styleFields)) return {};
    const bool scalar = std::holds_alternative<FloatField>(field->member);
    for (const float v : {value.x, value.y}) {
        if (!std::isfinite(v)) return "not a finite number";
        if (std::abs(v) > maxStyleValue) return "larger than " + std::to_string(static_cast<int>(maxStyleValue));
        if (scalar) break;
    }
    if ((name == "Alpha" || name == "DisabledAlpha") && !(value.x >= 0.0f && value.x <= 1.0f)) return "must be in 0..1";
    if (name == "WindowMinSize" && !(value.x >= 1.0f && value.y >= 1.0f)) return "must be at least 1";
    return {};
}

bool applyPendingTheme() {
    if (!pendingTheme) return false;
    currentTheme = std::move(*pendingTheme);
    pendingTheme.reset();
    ++generation;
    applyNow(currentTheme);
    useFontSet(currentTheme.fonts);
    return true;
}

void reapplyTheme() { applyNow(currentTheme); }

bool isStyleField(std::string_view name) {
    for (const auto& f : styleFields)
        if (f.name == name) return true;
    return false;
}

std::vector<std::string_view> styleFieldNames() {
    std::vector<std::string_view> names;
    for (const auto& f : styleFields) names.push_back(f.name);
    return names;
}

int imguiColorIndex(std::string_view name) {
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (name == ImGui::GetStyleColorName(i)) return i;
    return -1;
}

std::optional<Color> resolveColor(std::string_view spec) { return resolveColor(spec, theme().scheme); }

std::optional<Color> resolveColor(std::string_view spec, const Scheme& scheme) {
    if (spec.empty()) return std::nullopt;
    if (spec.front() == '#') return parseHex(spec);
    if (auto slash = spec.find('/'); slash != std::string_view::npos) {
        // "role/alpha": the whole rest must be a number in 0..1 ("onSurface/O.6" or "primary/2" is no color).
        const std::string_view text = spec.substr(slash + 1);
        float alpha = 0.0f;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), alpha);
        if (ec != std::errc{} || end != text.data() + text.size() || !(alpha >= 0.0f && alpha <= 1.0f))
            return std::nullopt;
        const Color* c = findRole(scheme, spec.substr(0, slash));
        return c ? std::optional{c->withAlpha(alpha)} : std::nullopt;
    }
    if (const Color* c = findRole(scheme, spec)) return *c;
    return parseHex(spec);
}

} // namespace imlui
