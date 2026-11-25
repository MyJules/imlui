// Culling: the stacks (Renderer::renderStack: Column, ScrollView, Card, ...) and Grid skip a child whose rect is out
// of view, and only advance the cursor by its size from the layout cache (a Dummy), so positions, the content size
// and the scrollbar stay what they would be. A culled child isn't synced or rendered: its bindings don't run.
//
// What must still render even out of view is "pinned": a child whose render on this frame or the last held the item
// ImGui has active (a TextField being edited, a Slider being dragged), the nav focus, an open popup (a menu, a combo
// box's list), or rendered a non-visual element (a Dialog, a Window, a Snackbar, a Timer: they have no rect in the
// flow). watch() before a child renders and held() after tell; pin() / pinned() remember it per child ID for a
// frame. While a keyboard or gamepad nav move is scoring items, the view is widened by its own height on each side,
// so the item the move lands on (an arrow key, Page Up / Down, Tab) is submitted and ImGui scrolls to it.
//
// Off with ui.setCulling(false) (all of it) or `cull = false` on a container (its own children).
export module imlui.ui.culling;

import std;

import imgui;
import imlui.theme.dpi;

export namespace imlui::cull {

/// Whether containers skip children out of view (ui.setCulling). On by default.
[[nodiscard]] bool enabled();
void setEnabled(bool on);

/// Extra room around the view (dp) before a child counts as out of it.
inline constexpr float margin = 32.0f;

/// Whether the rect `min`..`max` (screen coordinates) is entirely outside the current window's clip rect, widened by
/// `margin` (and by the clip rect's own height while a nav move request scores items).
[[nodiscard]] bool outOfView(ImVec2 min, ImVec2 max);

/// ImGui state taken before a child renders, for held().
struct Watch {
    ImGuiID activeAlive = 0;
    bool navAlive = false;
    int popups = 0;
    std::uint64_t nonVisual = 0;
};
/// The state before a child renders.
[[nodiscard]] Watch watch();
/// Whether the child rendered since `before` held the active item or the nav focus, began an open popup, or rendered
/// a non-visual element: it must not be culled on the next frame.
[[nodiscard]] bool held(const Watch& before);
/// A non-visual element rendered (the Renderer calls it): its ancestors are pinned.
void noteNonVisual();

/// Remembers that the child `id` must not be culled on this frame and the next.
void pin(ImGuiID id);
/// Whether pin(id) was called on this frame or the last.
[[nodiscard]] bool pinned(ImGuiID id);

} // namespace imlui::cull

module :private;

namespace imlui::cull {

namespace {

bool on = true;
std::uint64_t nonVisualCount = 0;

struct Pins {
    ImGuiContext* context = nullptr;
    std::unordered_map<ImGuiID, int> frames; ///< child ID -> last frame it was pinned
};
Pins pins; // main thread only, like all ImGui code

std::unordered_map<ImGuiID, int>& pinFrames() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (pins.context != context) {
        pins.frames.clear();
        pins.context = context;
    }
    return pins.frames;
}

/// Open popups whose window has begun on this frame.
int activePopups(const ImGuiContext& g) {
    int n = 0;
    for (const ImGuiPopupData& p : g.OpenPopupStack)
        if (p.Window && p.Window->LastFrameActive == g.FrameCount) ++n;
    return n;
}

} // namespace

bool enabled() { return on; }
void setEnabled(bool enable) { on = enable; }

bool outOfView(ImVec2 min, ImVec2 max) {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiWindow* window = g.CurrentWindow;
    if (!window) return false;
    const ImVec2 clipMin = window->ClipRect.Min, clipMax = window->ClipRect.Max;
    float m = dp(margin);
    // A nav move (arrows, Page Up / Down, Tab) picks among the items submitted this frame, and ImGui then scrolls to
    // the one it picked: the items up to a page away must be there.
    if (g.NavMoveScoringItems) m += std::max(0.0f, clipMax.y - clipMin.y);
    return max.y < clipMin.y - m || min.y > clipMax.y + m || max.x < clipMin.x - m || min.x > clipMax.x + m;
}

Watch watch() {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    return {g.ActiveIdIsAlive, g.NavIdIsAlive, g.OpenPopupStack.Size > 0 ? activePopups(g) : 0, nonVisualCount};
}

bool held(const Watch& before) {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    if (nonVisualCount != before.nonVisual) return true;
    // ImGui marks the active item alive when it is submitted (ItemAdd / KeepAliveID), the nav item likewise.
    if (g.ActiveId != 0 && g.ActiveIdIsAlive == g.ActiveId && before.activeAlive != g.ActiveId) return true;
    if (g.NavId != 0 && g.NavIdIsAlive && !before.navAlive) return true;
    return g.OpenPopupStack.Size > 0 && activePopups(g) > before.popups;
}

void noteNonVisual() { ++nonVisualCount; }

void pin(ImGuiID id) {
    auto& frames = pinFrames();
    const int frame = ImGui::GetFrameCount();
    if (frames.size() > 256) std::erase_if(frames, [&](const auto& e) { return frame - e.second > 1; });
    frames[id] = frame;
}

bool pinned(ImGuiID id) {
    auto& frames = pinFrames();
    if (frames.empty()) return false;
    const auto it = frames.find(id);
    return it != frames.end() && ImGui::GetFrameCount() - it->second <= 1;
}

} // namespace imlui::cull
