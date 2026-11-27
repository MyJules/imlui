// Module wrapper around what the OS knows about the calling thread's stack: how much of it is left. Recursive code
// (the renderer walking an element tree) checks it so that a tree deeper than the stack holds is cut and reported
// instead of overflowing it, whatever the build (sanitizers enlarge every frame) and the thread's stack size. OS APIs
// only in this file.
module;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h> // GetCurrentThreadStackLimits, SetThreadStackGuarantee, GetSystemInfo
#elif defined(__linux__)
#include <pthread.h> // pthread_getattr_np, pthread_attr_getstack
#endif

export module imlui.platform.stack;

import std;

export namespace imlui::platform {

/// Bytes of the calling thread's stack left below the caller's frame, or nullopt where the OS can't tell. Cheap: the
/// thread's stack bounds are looked up once per thread.
[[nodiscard]] std::optional<std::size_t> stackRemaining();

} // namespace imlui::platform

module :private;

namespace imlui::platform {

namespace {

/// A thread's stack: [low, high), growing down from `high`. The bytes from `low` up to `usable` can't
/// be used: what a stack overflow needs to be raised and handled (Windows), so stackRemaining() counts from `usable`.
/// Bounds that can't be read stay all 0.
struct StackBounds {
    std::uintptr_t low = 0;
    std::uintptr_t usable = 0;
    std::uintptr_t high = 0;
};

#if defined(_WIN32)
/// The bytes at the bottom of a Windows thread's stack reservation that it can't grow into. GetCurrentThreadStackLimits
/// gives the whole reservation, but the stack overflows before its end: the guard page sits above the last pages, and
/// the overflow handler's guarantee (SetThreadStackGuarantee, which a host or a runtime may raise; 0 reads it without
/// changing it) is taken out of what the thread can use. Counted generously (64 KiB at least): a recursive caller that
/// stops with this much less left still stops before the real end, while one counting to the reservation's end would
/// overflow first.
std::size_t unusableStackBytes() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::size_t page = info.dwPageSize ? info.dwPageSize : 4096;
    ULONG guarantee = 0;
    if (!SetThreadStackGuarantee(&guarantee)) guarantee = 0;
    // The guarantee, the guard page, the page at the very bottom that is never committed, and a page of slack for the
    // guarantee's rounding to pages.
    return std::max<std::size_t>(64 * 1024, std::size_t{guarantee} + 3 * page);
}
#endif

StackBounds currentThreadStack() {
    StackBounds bounds;
#if defined(_WIN32)
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high); // the whole reservation, guard pages included
    const std::uintptr_t usable = static_cast<std::uintptr_t>(low) + unusableStackBytes();
    if (low != 0 && usable < high)
        bounds = {static_cast<std::uintptr_t>(low), usable, static_cast<std::uintptr_t>(high)};
#elif defined(__linux__)
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void* address = nullptr;
        std::size_t size = 0;
        if (pthread_attr_getstack(&attr, &address, &size) == 0) {
            const auto low = reinterpret_cast<std::uintptr_t>(address);
            bounds = {low, low, low + size}; // as the thread library reports it
        }
        pthread_attr_destroy(&attr);
    }
#endif
    return bounds;
}

} // namespace

std::optional<std::size_t> stackRemaining() {
    thread_local const StackBounds bounds = currentThreadStack();
    // The frame address is on the thread's real stack, also under AddressSanitizer (its fake stack, for
    // use-after-return detection, holds only locals, and imlui builds it without that: cmake/ImluiSanitizers.cmake).
    const auto here = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    if (bounds.low == 0 || here <= bounds.low || here > bounds.high) return std::nullopt;
    // On this thread's stack, but in its unusable bottom (a caller deep in it): nothing left, not unknown.
    return here > bounds.usable ? static_cast<std::size_t>(here - bounds.usable) : std::size_t{0};
}

} // namespace imlui::platform
