// Material 3 window size classes: compact, medium, expanded, large and extra-large, by window width in dp.
export module imlui.ui.size_class;

import std;

export namespace imlui {

/// Material 3 window size class.
enum class SizeClass { Compact, Medium, Expanded, Large, ExtraLarge };

/// Size class for a window `widthDp` wide: compact < 600, medium < 840, expanded < 1200, large < 1600,
/// extra-large otherwise.
[[nodiscard]] SizeClass sizeClassForWidth(float widthDp);

/// "compact", "medium", "expanded", "large" or "extraLarge".
[[nodiscard]] std::string_view sizeClassName(SizeClass c);

/// The size class named `name` (as sizeClassName() spells it), if any.
[[nodiscard]] std::optional<SizeClass> sizeClassFromName(std::string_view name);

} // namespace imlui

module :private;

namespace imlui {

SizeClass sizeClassForWidth(float widthDp) {
    // A NaN width (no window yet) is compact, like any other width below 600.
    if (!(widthDp >= 600.0f)) return SizeClass::Compact;
    if (widthDp < 840.0f) return SizeClass::Medium;
    if (widthDp < 1200.0f) return SizeClass::Expanded;
    if (widthDp < 1600.0f) return SizeClass::Large;
    return SizeClass::ExtraLarge;
}

std::string_view sizeClassName(SizeClass c) {
    switch (c) {
    case SizeClass::Compact: return "compact";
    case SizeClass::Medium: return "medium";
    case SizeClass::Expanded: return "expanded";
    case SizeClass::Large: return "large";
    case SizeClass::ExtraLarge: return "extraLarge";
    }
    return "compact";
}

std::optional<SizeClass> sizeClassFromName(std::string_view name) {
    for (SizeClass c :
         {SizeClass::Compact, SizeClass::Medium, SizeClass::Expanded, SizeClass::Large, SizeClass::ExtraLarge})
        if (sizeClassName(c) == name) return c;
    return std::nullopt;
}

} // namespace imlui
