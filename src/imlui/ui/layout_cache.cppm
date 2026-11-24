// The layout cache: sizes come up. Immediate-mode layouts place a child before it renders, so a layout that needs a
// child's size (a Row's widths and heights, a stack's `align`, the TopAppBar's right-aligned actions, the
// NavigationRail's destinations, a Card's shadow, the widest claim of an auto-sized window) uses the size measured on
// the last frame, recorded here by ImGui ID. The Renderer records every visual element it renders (its group's size,
// under the ID Frame::Child::id() gives it in its parent's scope); a layout records its own aggregates.
//
// Convergence: a layout that placed something by a size says so (assumeWidth / assumeHeight, or through
// Frame::Child). When the size recorded later in the frame differs from the assumed one by more than half a pixel, the
// layout was stale: another frame is requested, so it settles even when nothing else asks for frames (rendering is on
// demand, a Timer or a posted change renders one frame). A size that keeps changing every frame (a layout that can't
// settle) stops asking after a few frames in a row and is logged once at debug level.
//
// Entries not used for two frames are dropped (elements come and go); a new ImGui context starts empty.
export module imlui.ui.layout_cache;

import std;

import imgui;
import imlui.log;
import imlui.ui.frame_request;

export namespace imlui::layout {

/// The size recorded for `id` on this frame or the last, if any.
[[nodiscard]] std::optional<ImVec2> lastSize(ImGuiID id);
/// Notes that a layout placed `id` this frame by a width / height of `size` (its last size, an intrinsic size or a
/// fallback): recordSize() later this frame compares it with the size `id` really had.
void assumeWidth(ImGuiID id, float size);
void assumeHeight(ImGuiID id, float size);
/// Records the size `id` rendered at this frame. Requests another frame when a layout assumed a size that differs by
/// more than half a pixel (unless `id` has mismatched on too many frames in a row).
void recordSize(ImGuiID id, ImVec2 size);

/// Consecutive frames a size may differ from the assumed one before its mismatches stop requesting frames.
inline constexpr int maxSettleFrames = 8;

/// Number of entries (tests: the cache doesn't grow while elements come and go).
[[nodiscard]] std::size_t cacheEntries();

} // namespace imlui::layout

module :private;

namespace imlui::layout {

namespace {

constexpr float tolerance = 0.5f;
/// An entry not touched on this frame or the one before is dropped.
constexpr int keepFrames = 2;

struct Entry {
    std::optional<ImVec2> size;
    int touched = 0;                         ///< last frame it was read, assumed or recorded
    std::array<int, 2> assumedFrame{-1, -1}; ///< frame of the last assumeWidth / assumeHeight
    std::array<float, 2> assumed{};
    int mismatchFrame = -1; ///< last frame a recorded size differed from an assumed one
    int streak = 0;         ///< mismatches on consecutive frames
    bool logged = false;
};

struct Cache {
    ImGuiContext* context = nullptr;
    int sweptFrame = -1;
    std::unordered_map<ImGuiID, Entry> entries;
};

Cache cache; // main thread only, like all ImGui code

/// The entries of the current ImGui context, with the stale ones dropped once per frame.
std::unordered_map<ImGuiID, Entry>& entries() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (cache.context != context) {
        cache.entries.clear();
        cache.context = context;
        cache.sweptFrame = -1;
    }
    const int frame = ImGui::GetFrameCount();
    if (cache.sweptFrame != frame) {
        cache.sweptFrame = frame;
        std::erase_if(cache.entries, [frame](const auto& e) { return e.second.touched < frame - keepFrames + 1; });
    }
    return cache.entries;
}

Entry& touch(ImGuiID id) {
    Entry& e = entries()[id];
    e.touched = ImGui::GetFrameCount();
    return e;
}

void assume(ImGuiID id, std::size_t axis, float size) {
    Entry& e = touch(id);
    e.assumedFrame[axis] = e.touched;
    e.assumed[axis] = size;
}

} // namespace

std::optional<ImVec2> lastSize(ImGuiID id) {
    auto& map = entries();
    const auto it = map.find(id);
    if (it == map.end()) return std::nullopt;
    it->second.touched = ImGui::GetFrameCount();
    return it->second.size;
}

void assumeWidth(ImGuiID id, float size) { assume(id, 0, size); }
void assumeHeight(ImGuiID id, float size) { assume(id, 1, size); }

void recordSize(ImGuiID id, ImVec2 size) {
    Entry& e = touch(id);
    const int frame = e.touched;
    const std::array<float, 2> got{size.x, size.y};
    bool mismatch = false;
    for (std::size_t axis = 0; axis < 2; ++axis) {
        if (e.assumedFrame[axis] != frame) continue;
        e.assumedFrame[axis] = -1; // compared once: a second record this frame (an aggregate) needs a new assumption
        if (std::fabs(got[axis] - e.assumed[axis]) > tolerance) mismatch = true;
    }
    e.size = size;
    if (!mismatch) return;
    if (e.mismatchFrame == frame) return; // already counted (and requested) this frame
    e.streak = e.mismatchFrame == frame - 1 ? e.streak + 1 : 1;
    e.mismatchFrame = frame;
    if (e.streak <= maxSettleFrames) {
        requestFrame();
    } else if (!e.logged) {
        e.logged = true;
        log::debug("layout: the size of item {:#010x} changed on {} frames in a row; not requesting more frames for it",
                   id, e.streak);
    }
}

std::size_t cacheEntries() { return entries().size(); }

} // namespace imlui::layout
