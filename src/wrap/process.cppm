// Module wrapper around what the OS knows about the running process: the executable's real path and the command
// line in its original encoding (UTF-16 on Windows, where `argv` is in the ANSI code page and loses any character
// outside it). Windows APIs only in this file.
module;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW
#endif

export module imlui.platform.process;

import std;

export namespace imlui::platform {

/// A program's arguments as file system paths, as processArguments() finds them.
struct ProcessArguments {
    std::vector<std::filesystem::path> args; ///< args[0] is the program as it was started
    /// True when the arguments given are the process's own command line (not a list a host made up), so the OS's
    /// copy was used (lossless on Windows) and executablePath() is the program they name.
    bool ofThisProcess = false;
};

/// The absolute path of the running executable, from the OS (not argv[0], which may be a bare name found through
/// PATH, relative to another folder, or anything the parent process chose); nullopt where the OS can't tell.
[[nodiscard]] std::optional<std::filesystem::path> executablePath();

/// `argv` as paths. When it is the process's own command line, the OS's copy of it: on Windows its UTF-16 form, so a
/// file name with characters outside the ANSI code page survives. Otherwise (a host passing its own list) `argv`
/// itself, in the native narrow encoding.
[[nodiscard]] ProcessArguments processArguments(int argc, const char* const* argv);

} // namespace imlui::platform

module :private;

namespace imlui::platform {

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)
/// `wide` in the ANSI code page, as the C runtime hands it to main() in argv.
std::string ansi(const wchar_t* wide) {
    const int length = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide, -1, out.data(), length, nullptr, nullptr);
    out.pop_back(); // the terminating NUL
    return out;
}

/// The process's command line split as main() sees it, in UTF-16; empty when it can't be read.
std::vector<std::wstring> wideCommandLine() {
    int count = 0;
    wchar_t** list = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!list) return {};
    std::vector<std::wstring> out{list, list + count};
    LocalFree(list);
    return out;
}
#elif defined(__linux__)
/// The process's command line (/proc/self/cmdline: arguments each followed by a NUL); empty when it can't be read.
std::vector<std::string> procCommandLine() {
    std::ifstream in{"/proc/self/cmdline", std::ios::binary};
    std::vector<std::string> out;
    std::string arg;
    while (std::getline(in, arg, '\0'))
        out.push_back(arg);
    return out;
}
#endif

std::vector<fs::path> narrow(int argc, const char* const* argv) {
    std::vector<fs::path> out;
    for (int i = 0; i < argc; ++i)
        out.emplace_back(argv[i]);
    return out;
}

} // namespace

std::optional<fs::path> executablePath() {
    try {
#if defined(_WIN32)
        std::wstring buffer(MAX_PATH, L'\0');
        while (true) {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) return std::nullopt;
            if (length < buffer.size()) {
                buffer.resize(length);
                return fs::path{buffer};
            }
            if (buffer.size() >= 32768) return std::nullopt; // longer than any Windows path
            buffer.resize(buffer.size() * 2);                // truncated
        }
#elif defined(__linux__)
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (ec || !exe.is_absolute()) return std::nullopt;
        return exe;
#else
        return std::nullopt;
#endif
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

ProcessArguments processArguments(int argc, const char* const* argv) {
    try {
#if defined(_WIN32)
        // The UTF-16 command line, when argv is what the C runtime made of it (converted back, it matches).
        const std::vector<std::wstring> wide = wideCommandLine();
        bool same = argc > 0 && wide.size() == static_cast<std::size_t>(argc);
        for (std::size_t i = 0; same && i < wide.size(); ++i)
            same = ansi(wide[i].c_str()) == argv[i];
        if (same) return {std::vector<fs::path>{wide.begin(), wide.end()}, true};
#elif defined(__linux__)
        const std::vector<std::string> own = procCommandLine();
        bool same = argc > 0 && own.size() == static_cast<std::size_t>(argc);
        for (std::size_t i = 0; same && i < own.size(); ++i)
            same = own[i] == argv[i];
        if (same) return {narrow(argc, argv), true};
#endif
        return {narrow(argc, argv), false};
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace imlui::platform
