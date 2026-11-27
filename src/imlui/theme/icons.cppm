// Icons: the Material Icons font and its name -> codepoint table, and the Fluent design's Segoe Fluent Icons (a system
// font on Windows 11) for the Material names it has a glyph for.
export module imlui.theme.icons;

import std;

import imgui;
import imlui.log;
import imlui.resources;
import imlui.theme.font_check;
import imlui.theme.system_fonts;

export namespace imlui {

/// Loads Material Icons Round and its name -> codepoint table (`imlui:/fonts/MaterialIconsRound-Regular.otf`,
/// `.codepoints`) from `resources`, which must outlive the font atlas (see loadFonts()). Call after loadFonts()
/// (which clears the font atlas).
void loadIcons(const Resources& resources);

/// The icon font, or nullptr if it could not be loaded.
[[nodiscard]] ImFont* iconFont();

/// UTF-8 for a Material icon name ("home", "add", ...); empty if unknown or beyond Unicode.
[[nodiscard]] std::string iconText(std::string_view name);

[[nodiscard]] bool hasIcon(std::string_view name);

/// The system file of Segoe Fluent Icons, Windows 11's icon font (Windows 10 has Segoe MDL2 Assets, segmdl2.ttf, with
/// the same code points for most of them).
inline constexpr std::array fluentIconFiles{std::string_view{"SegoeIcons.ttf"}, std::string_view{"segmdl2.ttf"}};

/// Draws the Material icon names with Segoe Fluent Icons where it has a glyph for them (`on`: design "fluent", set
/// by the theme between frames); loads the font from the system fonts on first use. Names it has no glyph for, and
/// every name where the font is missing (outside Windows), stay Material's.
void useFluentIcons(bool on);
/// The Segoe icon font in use: nullptr while useFluentIcons is off or the font couldn't be loaded.
[[nodiscard]] ImFont* fluentIconFont();

/// The glyph an icon name draws with: its font, its UTF-8 text, and whether it is Segoe Fluent Icons' (centered on
/// its ink box, as its glyphs don't share Material's baseline). Empty text for an unknown name.
struct IconGlyph {
    ImFont* font = nullptr;
    std::string text;
    unsigned code = 0;
    bool fluent = false;
};
[[nodiscard]] IconGlyph iconGlyph(std::string_view name);
/// The Segoe Fluent Icons code point for Material icon `name`, 0 when it has none mapped.
[[nodiscard]] unsigned fluentIconCode(std::string_view name);

} // namespace imlui

module :private;

namespace imlui {

namespace {
ImFont* icons = nullptr;
ResourceData iconBytes; ///< the font's data, which the atlas points into without a copy
/// Hashes std::string and std::string_view alike, so a lookup by name doesn't build a std::string.
struct NameHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
};
std::unordered_map<std::string, unsigned, NameHash, std::equal_to<>> iconCodes;

bool fluentOn = false;
bool fluentTried = false; ///< loading Segoe was attempted since the atlas was last cleared
ImFont* fluentFont = nullptr;
ResourceData fluentBytes; ///< the font's data, as iconBytes

/// Material icon names -> Segoe Fluent Icons code points (the same in Segoe MDL2 Assets): the common ones with a
/// Fluent counterpart. Sorted by name, for the binary search.
constexpr std::pair<std::string_view, unsigned> fluentCodes[] = {
    {"account_circle", 0xE77B},
    {"add", 0xE710},
    {"arrow_back", 0xE72B},
    {"arrow_downward", 0xE74B},
    {"arrow_drop_down", 0xE70D},
    {"arrow_drop_up", 0xE70E},
    {"arrow_forward", 0xE72A},
    {"arrow_upward", 0xE74A},
    {"attach_file", 0xE723},
    {"calendar_today", 0xE787},
    {"call", 0xE717},
    {"camera_alt", 0xE722},
    {"check", 0xE73E},
    {"chevron_left", 0xE76B},
    {"chevron_right", 0xE76C},
    {"close", 0xE711},
    {"content_copy", 0xE8C8},
    {"content_cut", 0xE8C6},
    {"content_paste", 0xE77F},
    {"delete", 0xE74D},
    {"download", 0xE896},
    {"edit", 0xE70F},
    {"email", 0xE715},
    {"expand_less", 0xE70E},
    {"expand_more", 0xE70D},
    {"favorite", 0xEB52},
    {"favorite_border", 0xEB51},
    {"filter_list", 0xE71C},
    {"folder", 0xE8B7},
    {"fullscreen", 0xE740},
    {"group", 0xE716},
    {"help", 0xE897},
    {"home", 0xE80F},
    {"image", 0xE8B9},
    {"info", 0xE946},
    {"link", 0xE71B},
    {"lock", 0xE72E},
    {"lock_open", 0xE785},
    {"mail", 0xE715},
    {"menu", 0xE700},
    {"more_horiz", 0xE712},
    {"open_in_new", 0xE8A7},
    {"palette", 0xE790},
    {"pause", 0xE769},
    {"people", 0xE716},
    {"person", 0xE77B},
    {"phone", 0xE717},
    {"play_arrow", 0xE768},
    {"print", 0xE749},
    {"push_pin", 0xE718},
    {"redo", 0xE7A6},
    {"refresh", 0xE72C},
    {"remove", 0xE738},
    {"save", 0xE74E},
    {"schedule", 0xE917},
    {"search", 0xE721},
    {"send", 0xE724},
    {"settings", 0xE713},
    {"share", 0xE72D},
    {"star", 0xE735},
    {"star_border", 0xE734},
    {"star_outline", 0xE734},
    {"sync", 0xE895},
    {"undo", 0xE7A7},
    {"upload", 0xE898},
    {"visibility", 0xE890},
    {"visibility_off", 0xED1A},
    {"warning", 0xE7BA},
};

static_assert(std::ranges::is_sorted(fluentCodes, {}, &std::pair<std::string_view, unsigned>::first));

std::string utf8(unsigned c) {
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x110000) {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

/// Loads the first of fluentIconFiles the system has, once since the atlas was last cleared.
void loadFluentIcons() {
    if (fluentTried) return;
    fluentTried = true;
    for (const std::string_view file : fluentIconFiles) {
        const std::optional<std::string> name = systemFont(file);
        if (!name) continue;
        auto bytes = readSystemFont(*name);
        if (!bytes || bytes->empty()) continue;
        if (const auto problem = checkFont(bytes->bytes())) {
            log::warn("font '{}': {}", *name, *problem);
            continue;
        }
        fluentBytes = std::move(*bytes);
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        fluentFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(fluentBytes.data()),
                                                                static_cast<int>(fluentBytes.size()), 16.0f, &cfg);
        if (fluentFont) return;
        fluentBytes = {};
    }
}

} // namespace

void loadIcons(const Resources& resources) {
    constexpr std::string_view fontName = "imlui:/fonts/MaterialIconsRound-Regular.otf";
    constexpr std::string_view codepointsName = "imlui:/fonts/MaterialIconsRound-Regular.codepoints";
    icons = nullptr;
    iconBytes = {};
    // The atlas was cleared (loadFonts()): Segoe's atlas font went with it.
    fluentFont = nullptr;
    fluentBytes = {};
    fluentTried = false;
    if (fluentOn) loadFluentIcons();
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
    auto it = iconCodes.find(name);
    if (it == iconCodes.end()) return {};
    return utf8(it->second);
}

bool hasIcon(std::string_view name) { return !name.empty() && iconCodes.contains(name); }

void useFluentIcons(bool on) {
    fluentOn = on;
    // Only with an atlas to add to (loadIcons() comes after loadFonts(); a bare test context has none yet).
    if (on && icons) loadFluentIcons();
}

ImFont* fluentIconFont() { return fluentOn ? fluentFont : nullptr; }

unsigned fluentIconCode(std::string_view name) {
    const auto it = std::ranges::lower_bound(fluentCodes, name, {}, &std::pair<std::string_view, unsigned>::first);
    return it != std::ranges::end(fluentCodes) && it->first == name ? it->second : 0u;
}

IconGlyph iconGlyph(std::string_view name) {
    if (ImFont* f = fluentIconFont()) {
        const unsigned code = fluentIconCode(name);
        // A code point the font doesn't have (an older Segoe MDL2 Assets) stays Material's.
        if (code != 0 && f->IsGlyphInFont(static_cast<ImWchar>(code))) return {f, utf8(code), code, true};
    }
    const auto it = iconCodes.find(name);
    if (it == iconCodes.end()) return {icons, {}, 0, false};
    return {icons, utf8(it->second), it->second, false};
}

} // namespace imlui
