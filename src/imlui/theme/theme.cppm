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
import imlui.theme.font_library;
import imlui.theme.scheme;

export namespace imlui {

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

    /// "material" | "imgui-dark" | "imgui-light" | "imgui-classic"
    std::string preset = "material";
    Color seed = *parseHex("#6750A4");
    bool dark = true;
    Scheme scheme = generateScheme(seed, dark);
    Shape shape;
    float spacing = 8.0f; ///< base spacing unit (dp)
    float fontScale = 1.0f;
    std::vector<std::pair<int, Color>> imguiColors; ///< ImGuiCol index -> color
    std::vector<StyleOverride> style;               ///< ImGuiStyle field overrides
    FontSet fonts;                                  ///< type-scale faces, named fonts and fallbacks
};

/// The theme currently in effect.
[[nodiscard]] const Theme& theme();

/// Schedules `t` to become the theme at the start of the next frame
/// (safe to call from inside a frame, e.g. from a Lua click handler).
void setTheme(Theme t);

/// Applies a pending theme, its fonts included (useFontSet()); call between frames. Returns true if it changed.
bool applyPendingTheme();

/// Re-applies the current theme to ImGuiStyle at the current dpiScale() (after a
/// monitor DPI or zoom change). Call between frames.
void reapplyTheme();

/// Whether a style field name is known (for validating Lua input).
[[nodiscard]] bool isStyleField(std::string_view name);

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

void applyNow(const Theme& t) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle{};
    if (t.preset == "imgui-light") ImGui::StyleColorsLight(&st);
    else if (t.preset == "imgui-classic") ImGui::StyleColorsClassic(&st);
    else if (t.preset == "imgui-dark") ImGui::StyleColorsDark(&st);
    else applyMaterial(t, st);

    for (const auto& o : t.style) {
        for (const auto& f : styleFields) {
            if (f.name != o.name) continue;
            if (const auto* fp = std::get_if<FloatField>(&f.member)) st.*(*fp) = o.value.x;
            else st.*std::get<VecField>(f.member) = o.value;
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
    st.FontSizeBase = 16.0f;
    st.FontScaleDpi = scale;
    st.FontScaleMain = t.fontScale;
}

} // namespace

const Theme& theme() { return currentTheme; }

void setTheme(Theme t) { pendingTheme = std::move(t); }

bool applyPendingTheme() {
    if (!pendingTheme) return false;
    currentTheme = std::move(*pendingTheme);
    pendingTheme.reset();
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
