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
// Retention: a page a TabBar hides, a branch an If switches off or a section that collapses keeps its sizes, so it is
// placed right (and seen) on the frame it comes back. An entry is dropped only when it is old in frames and in time
// (not used for keepFrames frames and keepSeconds seconds: on-demand rendering renders few frames while idle, an
// animation many per second), and above maxEntries the least recently used ones go first (elements that come and go
// every frame, a Repeater over a changing model). Entries used on this frame or the last are never dropped. Keys are
// ImGui IDs of element tables, which stay the same while a table lives (Repeater/ListView keep their delegates while
// the item at an index doesn't change, TabBar pages are kept); a new table that reuses the address of a collected one
// starts from its size and converges like any stale size. A new ImGui context starts empty.
export module imlui.ui.layout_cache;

import std;

import imgui;
import imlui.log;
import imlui.ui.frame_request;

export namespace imlui::layout {

/// The size last recorded for `id` (on this frame, the last, or whenever it was last rendered), if it is still kept.
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

/// Retention: an entry not used for keepFrames frames and keepSeconds seconds (ImGui time) is dropped.
inline constexpr int keepFrames = 600;
inline constexpr double keepSeconds = 60.0;
/// Above this many entries, the least recently used ones are dropped (never those used on this frame or the last).
inline constexpr std::size_t maxEntries = 16384;

/// Number of entries (tests: the cache doesn't grow while elements come and go).
[[nodiscard]] std::size_t cacheEntries();

} // namespace imlui::layout

module :private;

namespace imlui::layout {

namespace {

constexpr float tolerance = 0.5f;
/// Entries used on this frame or the last are live: never dropped, whatever the cap.
constexpr int liveFrames = 2;

struct Entry {
    std::optional<ImVec2> size;
    int touched = 0;                         ///< last frame it was read, assumed or recorded
    double touchedTime = 0.0;                ///< ImGui time of that frame
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

/// Drops the entries that are old in frames and in time, then, above maxEntries, the least recently used ones that
/// aren't live.
void sweep(std::unordered_map<ImGuiID, Entry>& map, int frame, double now) {
    std::erase_if(map, [&](const auto& e) {
        return frame - e.second.touched >= keepFrames && now - e.second.touchedTime >= keepSeconds;
    });
    if (map.size() <= maxEntries) return;
    std::vector<std::pair<int, ImGuiID>> old; // (last frame used, id) of the entries that may go
    old.reserve(map.size());
    for (const auto& [id, e] : map)
        if (frame - e.touched >= liveFrames) old.emplace_back(e.touched, id);
    const std::size_t drop = std::min(old.size(), map.size() - maxEntries);
    std::ranges::nth_element(old, old.begin() + static_cast<std::ptrdiff_t>(drop));
    for (std::size_t i = 0; i < drop; ++i)
        map.erase(old[i].second);
}

/// The entries of the current ImGui context, swept once per frame.
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
        sweep(cache.entries, frame, ImGui::GetTime());
    }
    return cache.entries;
}

void markUsed(Entry& e) {
    e.touched = ImGui::GetFrameCount();
    e.touchedTime = ImGui::GetTime();
}

Entry& touch(ImGuiID id) {
    Entry& e = entries()[id];
    markUsed(e);
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
    markUsed(it->second);
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
