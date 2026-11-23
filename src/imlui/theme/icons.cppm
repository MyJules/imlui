// Icons: the Material Icons font and its name -> codepoint table.
module;

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

export module imlui.theme.icons;

import imgui;
import imlui.log;

export namespace imlui {

/// Loads Material Icons Round and its codepoint table from `dir`. Call after
/// loadFonts() (which clears the font atlas).
void loadIcons(const std::filesystem::path& dir);

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
std::unordered_map<std::string, unsigned> iconCodes;
} // namespace

void loadIcons(const std::filesystem::path& dir) {
    auto path = (dir / "MaterialIconsRound-Regular.otf").string();
    icons = nullptr;
    if (!std::filesystem::exists(path)) {
        log::warn("font '{}' not found", path);
    } else {
        ImFontConfig cfg;
        icons = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &cfg);
    }

    // name -> codepoint table shipped with the icon font
    iconCodes.clear();
    std::ifstream in(dir / "MaterialIconsRound-Regular.codepoints");
    std::string name, hex;
    while (in >> name >> hex) iconCodes[name] = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
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
