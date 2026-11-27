// Placement of popups anchored to a widget (menus, dropdowns, pickers): keeps
// them inside the main viewport and draws their elevation shadow.
//
// Popups can't draw outside themselves, so the shadow is drawn in the anchor's
// window, behind the popup, using the popup size recorded on the previous frame.
export module imlui.widgets.anchored_popup;

import std;

import imgui;
import imlui.theme.dpi;
import imlui.widgets.common;

export namespace imlui::md {

/// Returned by placeAnchoredPopup(); pass it to trackAnchoredPopup().
struct AnchoredPopup {
    ImGuiStorage* storage = nullptr;  ///< state storage of the anchor's window
    ImGuiID key = 0;                  ///< base key of the stored popup size
};

/// Positions the next window (call right before ImGui::BeginPopup(popupId), in the anchor's window) next to the
/// anchor rect [a, b]: below it, left edges aligned. When that would overflow the main viewport it aligns the right
/// edges instead and/or opens above the anchor, and clamps to the viewport as a last resort. Also draws the
/// popup's elevation shadow at that position, with Material's extra-small corner radius.
AnchoredPopup placeAnchoredPopup(const char* popupId, ImVec2 a, ImVec2 b, float minWidth, float gap);
/// The same with the shadow's corner radius `shadowRadius` (pixels): a skin whose popups have other corners (the
/// Fluent flyouts).
AnchoredPopup placeAnchoredPopup(const char* popupId, ImVec2 a, ImVec2 b, float minWidth, float gap,
                                 float shadowRadius);

/// Records the popup's size for the next frame's placement and shadow. Call right after BeginPopup() with its
/// result (while inside the popup when it is open).
void trackAnchoredPopup(const AnchoredPopup& popup, bool open);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

// Offsets from the popup key in the anchor window's storage.
constexpr ImGuiID widthSlot = 3;
constexpr ImGuiID heightSlot = 4;
constexpr ImGuiID shownSlot = 5;

ImVec2 placeInViewport(ImVec2 a, ImVec2 b, ImVec2 size, float gap) {
    // The anchor's window decides: the app window, or the monitor of a Window dragged out of it.
    const auto [lo, hi] = windowWorkArea();

    float x = a.x;
    if (x + size.x > hi.x) x = b.x - size.x;  // align right edges
    x = std::max(lo.x, std::min(x, hi.x - size.x));

    float y = b.y + gap;
    if (y + size.y > hi.y) {
        // No room below: open upward when it fits there, or when there is more room above than below.
        const float above = a.y - gap - size.y;
        if (above >= lo.y || a.y - lo.y > hi.y - b.y) y = above;
    }
    y = std::max(lo.y, std::min(y, hi.y - size.y));
    return {x, y};
}

} // namespace

AnchoredPopup placeAnchoredPopup(const char* popupId, ImVec2 a, ImVec2 b, float minWidth, float gap) {
    return placeAnchoredPopup(popupId, a, b, minWidth, gap, dp(shape().extraSmall));
}

AnchoredPopup placeAnchoredPopup(const char* popupId, ImVec2 a, ImVec2 b, float minWidth, float gap,
                                 float shadowRadius) {
    AnchoredPopup popup{ImGui::GetStateStorage(), ImGui::GetID(popupId)};
    ImGuiStorage* st = popup.storage;
    // Before the first open the size is unknown: assume the minimum width.
    const ImVec2 size{std::max(minWidth, st->GetFloat(popup.key + widthSlot, 0.0f)),
                      st->GetFloat(popup.key + heightSlot, 0.0f)};
    const ImVec2 pos = placeInViewport(a, b, size, gap);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);

    if (ImGui::IsPopupOpen(popupId) && st->GetBool(popup.key + shownSlot, false) && size.y > 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRectFullScreen();
        drawShadow(dl, pos, {pos.x + size.x, pos.y + size.y}, shadowRadius, 2.0f);
        dl->PopClipRect();
    }
    return popup;
}

void trackAnchoredPopup(const AnchoredPopup& popup, bool open) {
    if (!popup.storage) return;
    if (!open) {
        popup.storage->SetBool(popup.key + shownSlot, false);
        return;
    }
    // The appearing frame is laid out hidden, with a provisional size: no shadow for it yet.
    popup.storage->SetBool(popup.key + shownSlot, !ImGui::IsWindowAppearing());
    const ImVec2 sz = ImGui::GetWindowSize();
    popup.storage->SetFloat(popup.key + widthSlot, sz.x);
    popup.storage->SetFloat(popup.key + heightSlot, sz.y);
}

} // namespace imlui::md
