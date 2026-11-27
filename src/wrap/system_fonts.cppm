// Module wrapper around where the OS keeps its installed fonts: the system font folder, asked of the OS (not a fixed
// path: Windows may live on another drive or folder). Windows APIs only in this file.
module;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h> // FOLDERID_Fonts
#include <shlobj.h>       // SHGetKnownFolderPath
#endif

export module imlui.platform.system_fonts;

import std;

export namespace imlui::platform {

/// The folder of the fonts installed for all users (`C:\Windows\Fonts`), from the OS; nullopt where there is no such
/// single folder (outside Windows) or the OS can't tell.
[[nodiscard]] std::optional<std::filesystem::path> systemFontFolder();

} // namespace imlui::platform

module :private;

namespace imlui::platform {

std::optional<std::filesystem::path> systemFontFolder() {
#if defined(_WIN32)
    PWSTR path = nullptr;
    // The known folder, not %WINDIR%\Fonts: it is what the shell and the font APIs use.
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_Fonts, KF_FLAG_DEFAULT, nullptr, &path);
    std::optional<std::filesystem::path> folder;
    if (SUCCEEDED(result) && path) folder = std::filesystem::path{path};
    CoTaskMemFree(path); // also on failure, which may still allocate
    return folder;
#else
    return std::nullopt;
#endif
}

} // namespace imlui::platform
