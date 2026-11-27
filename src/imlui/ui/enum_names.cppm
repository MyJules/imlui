// The Lua names of the built-in enums that live below the element layer (the md:: widgets' vocabulary, the theme's,
// the image's): an EnumNames<E> per enum, so an element property or a function argument of that type reads and
// pushes its names as strings, a script's typo is reported with the names it takes, and the Lua definitions get an
// `---@alias` of the enum's name (the editor completes the names). An enum declared in an element's own module
// (ListView::Position) has its EnumNames there.
//
// Each `values` lists the canonical names first; an alias (an older or alternative spelling) after them reads as
// the same value, and the value reads back as the canonical name.
//
// The groups are in delimited sections, so they are written in parallel without touching each other's lines:
// (A) controls, (B) inputs, text and data, (C) layout and theme. Add a group's lines just above its "end" marker.
export module imlui.ui.enum_names;

import std;

import imlui.types.lua_convert;
import imlui.widgets.button;
// ---- (A) controls: imports
// ---- end (A)
// ---- (B) inputs, text and data: imports
// ---- end (B)
// ---- (C) layout and theme: imports
import imlui.theme.theme;
import imlui.ui.props;
// ---- end (C)

// -- Button
template <>
struct imlui::EnumNames<imlui::md::ButtonVariant> {
    static constexpr std::string_view name = "ButtonVariant";
    static constexpr std::string_view doc = "A Button's emphasis.";
    static constexpr std::pair<imlui::md::ButtonVariant, std::string_view> values[] = {
        {imlui::md::ButtonVariant::Filled, "filled"},     {imlui::md::ButtonVariant::Tonal, "tonal"},
        {imlui::md::ButtonVariant::Outlined, "outlined"}, {imlui::md::ButtonVariant::Text, "text"},
        {imlui::md::ButtonVariant::Elevated, "elevated"}, {imlui::md::ButtonVariant::Fab, "fab"},
    };
};

// ---- (A) controls
// ---- end (A)

// ---- (B) inputs, text and data
// ---- end (B)

// ---- (C) layout and theme
// -- the stacks' `align` (Column, Card, ScrollView, Window, Dialog, ...) and Text's
template <>
struct imlui::EnumNames<imlui::Align> {
    static constexpr std::string_view name = "Align";
    static constexpr std::string_view doc = "Where each child (a Text: its line) goes across the width.";
    static constexpr std::pair<imlui::Align, std::string_view> values[] = {
        {imlui::Align::Start, "start"}, {imlui::Align::Center, "center"}, {imlui::Align::End, "end"}};
};

// -- the theme's `preset`
template <>
struct imlui::EnumNames<imlui::ThemePreset> {
    static constexpr std::string_view name = "ThemePreset";
    static constexpr std::string_view doc = "How ImGui's own style is set: from the scheme, or a stock ImGui style.";
    static constexpr std::pair<imlui::ThemePreset, std::string_view> values[] = {
        {imlui::ThemePreset::Material, "material"},
        {imlui::ThemePreset::ImguiDark, "imgui-dark"},
        {imlui::ThemePreset::ImguiLight, "imgui-light"},
        {imlui::ThemePreset::ImguiClassic, "imgui-classic"},
    };
};
// ---- end (C)
