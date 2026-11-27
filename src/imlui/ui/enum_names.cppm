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
// ---- end (C)
