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
import imlui.widgets.card;
import imlui.widgets.selection;
import imlui.widgets.tabs;
import imlui.widgets.top_app_bar;
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
// -- IconButton
template <>
struct imlui::EnumNames<imlui::md::IconButtonVariant> {
    static constexpr std::string_view name = "IconButtonVariant";
    static constexpr std::string_view doc = "An IconButton's emphasis.";
    static constexpr std::pair<imlui::md::IconButtonVariant, std::string_view> values[] = {
        {imlui::md::IconButtonVariant::Standard, "standard"},
        {imlui::md::IconButtonVariant::Filled, "filled"},
        {imlui::md::IconButtonVariant::Tonal, "tonal"},
        {imlui::md::IconButtonVariant::Outlined, "outlined"},
    };
};

// -- Card
template <>
struct imlui::EnumNames<imlui::md::CardVariant> {
    static constexpr std::string_view name = "CardVariant";
    static constexpr std::string_view doc = "A Card's container style.";
    static constexpr std::pair<imlui::md::CardVariant, std::string_view> values[] = {
        {imlui::md::CardVariant::Elevated, "elevated"},
        {imlui::md::CardVariant::Filled, "filled"},
        {imlui::md::CardVariant::Outlined, "outlined"},
    };
};

// -- Chip
template <>
struct imlui::EnumNames<imlui::md::ChipVariant> {
    static constexpr std::string_view name = "ChipVariant";
    static constexpr std::string_view doc = "A Chip's kind: only a filter chip toggles.";
    static constexpr std::pair<imlui::md::ChipVariant, std::string_view> values[] = {
        {imlui::md::ChipVariant::Filter, "filter"},
        {imlui::md::ChipVariant::Assist, "assist"},
        {imlui::md::ChipVariant::Suggestion, "suggestion"},
        {imlui::md::ChipVariant::Input, "input"},
    };
};

// -- TabBar
template <>
struct imlui::EnumNames<imlui::md::TabsVariant> {
    static constexpr std::string_view name = "TabBarVariant";
    static constexpr std::string_view doc = "A TabBar's style.";
    static constexpr std::pair<imlui::md::TabsVariant, std::string_view> values[] = {
        {imlui::md::TabsVariant::Primary, "primary"},
        {imlui::md::TabsVariant::Secondary, "secondary"},
    };
};

// -- TopAppBar
template <>
struct imlui::EnumNames<imlui::md::TopAppBarVariant> {
    static constexpr std::string_view name = "TopAppBarVariant";
    static constexpr std::string_view doc = "A TopAppBar's size and title placement.";
    static constexpr std::pair<imlui::md::TopAppBarVariant, std::string_view> values[] = {
        {imlui::md::TopAppBarVariant::Small, "small"},
        {imlui::md::TopAppBarVariant::Center, "center"},
        {imlui::md::TopAppBarVariant::Medium, "medium"},
        {imlui::md::TopAppBarVariant::Large, "large"},
    };
};
// ---- end (A)

// ---- (B) inputs, text and data
// ---- end (B)

// ---- (C) layout and theme
// ---- end (C)
