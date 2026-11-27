// Module wrapper around what the OS tells about the process's memory. OS headers only in this file.
module;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h> // GetProcessMemoryInfo (K32GetProcessMemoryInfo in kernel32)
#endif

export module imlui.platform.memory;

import std;

export namespace imlui::platform {

/// The process's private bytes (committed memory no other process shares: what Task Manager calls its commit size);
/// nullopt where the OS can't tell.
[[nodiscard]] std::optional<std::size_t> privateBytes();

} // namespace imlui::platform

module :private;

namespace imlui::platform {

std::optional<std::size_t> privateBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof counters;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof counters))
        return std::nullopt;
    return counters.PrivateUsage;
#else
    return std::nullopt;
#endif
}

} // namespace imlui::platform
