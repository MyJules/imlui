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
// ---- end (A)
// ---- (B) inputs, text and data: imports
import imlui.image.fill_mode;
import imlui.theme.typography;
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
// ---- end (C)
