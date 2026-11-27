// The Lua names of the built-in enums that live below the element layer (the md:: widgets' vocabulary, the theme's,
// the image's): an EnumNames<E> per enum, so an element property or a function argument of that type reads and
// pushes its names as strings, a script's typo is reported with the names it takes, and the Lua definitions get an
// `---@alias` of the enum's name (the editor completes the names). An enum declared in an element's own module
// (ListView::Position) has its EnumNames there. An enum several elements share that has no lower home is declared
// here too (FieldVariant).
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
import imlui.image.fill_mode;
import imlui.theme.typography;
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
export namespace imlui {

/// The container style of the text-field-like inputs (TextField, TextArea, ComboBox, SpinBox).
enum class FieldVariant { Filled, Outlined };

} // namespace imlui

// -- TextField, TextArea, ComboBox, SpinBox
template <>
struct imlui::EnumNames<imlui::FieldVariant> {
    static constexpr std::string_view name = "FieldVariant";
    static constexpr std::string_view doc = "A text field's container: filled or outlined.";
    static constexpr std::pair<imlui::FieldVariant, std::string_view> values[] = {
        {imlui::FieldVariant::Filled, "filled"},
        {imlui::FieldVariant::Outlined, "outlined"},
    };
};

// -- Image
template <>
struct imlui::EnumNames<imlui::FillMode> {
    static constexpr std::string_view name = "FillMode";
    static constexpr std::string_view doc = "How an Image fills its size when the aspect ratios differ.";
    static constexpr std::pair<imlui::FillMode, std::string_view> values[] = {
        {imlui::FillMode::Stretch, "stretch"},
        {imlui::FillMode::Fit, "fit"},
        {imlui::FillMode::Crop, "crop"},
        {imlui::FillMode::Tile, "tile"},
        {imlui::FillMode::Fit, "preserveAspectFit"}, // QML's names
        {imlui::FillMode::Crop, "preserveAspectCrop"},
    };
};

// -- Text, ui.textSize, the Canvas painter's text
template <>
struct imlui::EnumNames<imlui::TextStyle> {
    static constexpr std::string_view name = "TextStyle";
    static constexpr std::string_view doc = "A text style of the Material type scale.";
    static constexpr std::pair<imlui::TextStyle, std::string_view> values[] = {
        {imlui::TextStyle::DisplayLarge, "displayLarge"},     {imlui::TextStyle::DisplayMedium, "displayMedium"},
        {imlui::TextStyle::DisplaySmall, "displaySmall"},     {imlui::TextStyle::HeadlineLarge, "headlineLarge"},
        {imlui::TextStyle::HeadlineMedium, "headlineMedium"}, {imlui::TextStyle::HeadlineSmall, "headlineSmall"},
        {imlui::TextStyle::TitleLarge, "titleLarge"},         {imlui::TextStyle::TitleMedium, "titleMedium"},
        {imlui::TextStyle::TitleSmall, "titleSmall"},         {imlui::TextStyle::BodyLarge, "bodyLarge"},
        {imlui::TextStyle::BodyMedium, "bodyMedium"},         {imlui::TextStyle::BodySmall, "bodySmall"},
        {imlui::TextStyle::LabelLarge, "labelLarge"},         {imlui::TextStyle::LabelMedium, "labelMedium"},
        {imlui::TextStyle::LabelSmall, "labelSmall"},
    };
    static_assert(std::size(values) == static_cast<std::size_t>(imlui::TextStyle::Count), "a name per style");
};
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
