// System fonts: the fonts installed with the OS, by resource name (`system:/fonts/SegUIVar.ttf`), for the faces a
// design wants that can't be bundled (Fluent's Segoe UI Variable and Segoe Fluent Icons). The folder comes from the
// OS (imlui.platform.system_fonts); it is read through a DirectoryMount of its own, not mounted in the app's
// Resources, so scripts don't see the system's fonts as app files and hot reload doesn't watch them.
export module imlui.theme.system_fonts;

import std;

import imlui.platform.system_fonts;
import imlui.resources;
import imlui.resources.data;
import imlui.resources.directory_mount;

export namespace imlui {

/// The prefix of a system font's resource name: `system:/fonts/<file name in the OS font folder>`.
inline constexpr std::string_view systemFontPrefix = "system:/fonts/";

/// Whether `name` is a system font's resource name (starts with systemFontPrefix).
[[nodiscard]] bool isSystemFont(std::string_view name);
/// `system:/fonts/<fileName>` when the OS font folder has that file (exactly so spelled); nullopt otherwise, and
/// always where there is no system font folder (outside Windows) or system fonts are off.
[[nodiscard]] std::optional<std::string> systemFont(std::string_view fileName);
/// The bytes of system font `name` (a `system:/fonts/...` name); nullopt when it is missing or system fonts are off.
/// They stay valid while the handle lives.
[[nodiscard]] std::optional<ResourceData> readSystemFont(std::string_view name);

/// Turns the system fonts off (every lookup then finds none, as on a system without them) or back on (the default).
/// For tests, which must measure the same text on every machine; call before a theme is applied.
void setSystemFontsEnabled(bool enabled);
[[nodiscard]] bool systemFontsEnabled();

} // namespace imlui

module :private;

namespace imlui {

namespace {

std::atomic<bool> enabled{true};

/// The OS font folder at `system:/fonts/`; no mount where there is none. Made on first use, once.
const Resources& systemFonts() {
    static const Resources resources = [] {
        Resources r;
        if (const auto folder = platform::systemFontFolder())
            r.mount(systemFontPrefix, std::make_shared<DirectoryMount>(*folder));
        return r;
    }();
    return resources;
}

} // namespace

bool isSystemFont(std::string_view name) { return name.starts_with(systemFontPrefix); }

std::optional<std::string> systemFont(std::string_view fileName) {
    if (!enabled || fileName.empty() || fileName.find('/') != std::string_view::npos) return std::nullopt;
    std::string name = std::string{systemFontPrefix} + std::string{fileName};
    if (!systemFonts().exists(name)) return std::nullopt;
    return name;
}

std::optional<ResourceData> readSystemFont(std::string_view name) {
    if (!enabled || !isSystemFont(name)) return std::nullopt;
    return systemFonts().read(name);
}

void setSystemFontsEnabled(bool on) { enabled = on; }
bool systemFontsEnabled() { return enabled; }

} // namespace imlui
