// Icons: the Material Icons font and its name -> codepoint table.
export module imlui.theme.icons;

import std;

import imgui;
import imlui.log;
import imlui.resources;
import imlui.theme.font_check;

export namespace imlui {

/// Loads Material Icons Round and its name -> codepoint table (`imlui:/fonts/MaterialIconsRound-Regular.otf`,
/// `.codepoints`) from `resources`, which must outlive the font atlas (see loadFonts()). Call after loadFonts()
/// (which clears the font atlas).
void loadIcons(const Resources& resources);

/// The icon font, or nullptr if it could not be loaded.
[[nodiscard]] ImFont* iconFont();

/// UTF-8 for a Material icon name ("home", "add", ...); empty if unknown.
[[nodiscard]] std::string iconText(std::string_view name);

[[nodiscard]] bool hasIcon(std::string_view name);

} // namespace imlui

module :private;

namespace imlui {

namespace {
ImFont* icons = nullptr;
ResourceData iconBytes; ///< the font's data, which the atlas points into without a copy
std::unordered_map<std::string, unsigned> iconCodes;
} // namespace

void loadIcons(const Resources& resources) {
    constexpr std::string_view fontName = "imlui:/fonts/MaterialIconsRound-Regular.otf";
    constexpr std::string_view codepointsName = "imlui:/fonts/MaterialIconsRound-Regular.codepoints";
    icons = nullptr;
    iconBytes = {};
    if (auto bytes = resources.read(fontName); bytes && !bytes->empty()) {
        if (const auto problem = checkFont(bytes->bytes())) {
            log::warn("font '{}': {}", fontName, *problem);
        } else {
            iconBytes = std::move(*bytes);
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false; // iconBytes keeps the data alive, like the text fonts' handles
            icons = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(iconBytes.data()),
                                                               static_cast<int>(iconBytes.size()), 16.0f, &cfg);
        }
    } else {
        log::warn("font '{}' not found", fontName);
    }

    // name -> codepoint table shipped with the icon font: one "name hex" pair per line
    iconCodes.clear();
    const auto codepoints = resources.read(codepointsName);
    if (!codepoints) {
        log::warn("icon codepoints '{}' not found", codepointsName);
        return;
    }
    const std::string_view text = codepoints->text();
    std::vector<std::string_view> words;
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t start = text.find_first_not_of(" \t\r\n", pos);
        if (start == std::string_view::npos) break;
        std::size_t end = text.find_first_of(" \t\r\n", start);
        if (end == std::string_view::npos) end = text.size();
        words.push_back(text.substr(start, end - start));
        pos = end;
    }
    for (std::size_t i = 0; i + 1 < words.size(); i += 2) {
        unsigned code = 0;
        const std::string_view hex = words[i + 1];
        const auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), code, 16);
        if (ec == std::errc{} && ptr == hex.data() + hex.size()) iconCodes[std::string{words[i]}] = code;
    }
}

ImFont* iconFont() { return icons; }

std::string iconText(std::string_view name) {
    auto it = iconCodes.find(std::string{name});
    if (it == iconCodes.end()) return {};
    unsigned c = it->second;
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

bool hasIcon(std::string_view name) { return !name.empty() && iconCodes.contains(std::string{name}); }

} // namespace imlui
