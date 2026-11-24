// Validation of printf formats that scripts pass for displaying a number (the Slider's `format` prop).
export module imlui.widgets.value_format;

import std;

export namespace imlui::md {

/// Fallback when a script's value format is rejected.
inline constexpr const char* defaultValueFormat = "%.2f";

/// True when `format` is safe to pass to snprintf with one `double`: exactly one floating conversion (`f`, `F`, `e`,
/// `E`, `g` or `G`, with optional `-+ #0` flags, a width and a `.precision` of up to two digits each), any number of
/// `%%`, and nothing else (no `*`, no length modifiers, no other conversions).
[[nodiscard]] bool isValidFloatFormat(std::string_view format);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

/// Skips up to `maxDigits` digits at `i`. False when there are more.
bool skipDigits(std::string_view s, std::size_t& i, std::size_t maxDigits) {
    std::size_t n = 0;
    while (i < s.size() && isDigit(s[i])) {
        ++i;
        ++n;
    }
    return n <= maxDigits;
}

} // namespace

bool isValidFloatFormat(std::string_view format) {
    constexpr std::size_t maxDigits = 2;
    constexpr std::string_view flags = "-+ #0";
    constexpr std::string_view conversions = "fFeEgG";
    int count = 0;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') continue;
        if (++i >= format.size()) return false; // a lone '%' at the end
        if (format[i] == '%') continue;
        while (i < format.size() && flags.find(format[i]) != std::string_view::npos)
            ++i;
        if (!skipDigits(format, i, maxDigits)) return false;
        if (i < format.size() && format[i] == '.') {
            ++i;
            if (!skipDigits(format, i, maxDigits)) return false;
        }
        if (i >= format.size() || conversions.find(format[i]) == std::string_view::npos) return false;
        ++count;
    }
    return count == 1;
}

} // namespace imlui::md
