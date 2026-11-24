// The layout box: the space a parent gives its children, as a stack of boxes in screen coordinates. Constraints go
// down: a container that gives its children less than the ImGui window's content region (a fixed-width Column, a
// share of a Row, a Grid cell) pushes a box, and everything that sizes, aligns or wraps by "the available width"
// (resolveSize through Frame::resolvedWidth() and md::itemWidth(), md::text's align and wrap, md::divider, a
// Column's `align`) reads availableWidth() instead of ImGui::GetContentRegionAvail().
//
// A box belongs to the ImGui window it was pushed in (and to the frame): in another window (a popup, a child window
// a container opened without a box of its own) the queries fall back to that window's content region. When ImGui
// itself narrows the region under a box (a table cell begun after the box was pushed), the narrower edge wins.
//
// Auto-sized windows (a Window with autoResize, a Dialog, menus, popups) take their content's width, so their
// content must not size itself by the window's width: a fill there would keep the window at whatever width it once
// had (it could grow, never shrink back). Their root box is shrink-to-fit (shrinkToFitBox()): a fill resolves to the
// window's minimum content width, widened to the widest claimWidth() of the last frame (menu rows), never to the
// window's current width, so the window's width depends on its content only. Content may still grow past it (a
// long label), up to the maximum width, where text wraps.
export module imlui.ui.layout_box;

import std;

import imgui;
import imlui.ui.frame_request;

export namespace imlui::layout {

/// The space given to a container's children, in absolute screen pixels.
struct Box {
    /// Right edge, where a fill ends: the available width at the cursor is `right - cursor x`, so Indent / SameLine /
    /// a Row's x positions shrink it.
    float right = 0.0f;
    /// Bottom edge; unset uses the ImGui window's content region.
    std::optional<float> bottom;
    /// How far content may reach, where text wraps; unset: `right`. Past `right` in an auto-sized window.
    std::optional<float> wrapRight;
    /// An auto-sized window's box (shrinkToFitBox(), or insetBox() inside one): pushing it widens `right` to the
    /// widest claimWidth() of the last frame (up to wrapRight).
    bool shrinkToFit = false;
};

/// The ImGui content region at the cursor as a box (what the queries use where no box was pushed).
[[nodiscard]] Box regionBox();
/// A box ending at `right`.
[[nodiscard]] Box boxTo(float right);
/// The root box of an auto-sized window, at the cursor: a fill is `minWidth` wide (or as wide as the widest
/// claimWidth() of the last frame), and content may grow, and text wraps, up to `maxWidth`.
[[nodiscard]] Box shrinkToFitBox(float minWidth, float maxWidth);
/// The current box, `inset` pixels narrower on the right (still shrink-to-fit in an auto-sized window).
[[nodiscard]] Box insetBox(float inset);

/// Right edge of the current box (screen x): where a fill ends.
[[nodiscard]] float boxRight();
/// Width / height available at the cursor in the current box (>= 0): what a fill takes.
[[nodiscard]] float availableWidth();
[[nodiscard]] float availableHeight();
/// The current box's wrap edge as an ImGui::PushTextWrapPos() position (window-local), for wrapping text at it.
[[nodiscard]] float textWrapPos();
/// Whether the current box is in an auto-sized window, whose width follows its content.
[[nodiscard]] bool shrinkToFit();
/// The width of a fill that needs at least `natural` pixels (a menu row): max(natural, availableWidth()). In a
/// shrink-to-fit box it also widens the box's fill width to cover it from the next frame on (asking for a frame
/// when that changes), so every fill in an auto-sized window (the other rows) matches the widest.
float claimWidth(float natural);

/// How a box is mirrored to ImGui's own state. ItemWidthAndWrap pushes ImGui's item width (the fill width) and text
/// wrap position (the wrap edge), so raw ImGui widgets in custom elements follow the box. None, for the root box of
/// a window that isn't auto-sized, leaves ImGui's per-window defaults alone.
enum class Mirror { ItemWidthAndWrap, None };

/// Pushes a box until popBox(), for begin/end APIs (md::beginDialog). Prefer BoxScope.
void pushBox(Box box, Mirror mirror = Mirror::ItemWidthAndWrap);
void popBox();

/// Pushes a box for its lifetime. Boxes a child left pushed when it threw (a Lua error) are popped with it.
class BoxScope {
public:
    explicit BoxScope(Box box, Mirror mirror = Mirror::ItemWidthAndWrap);
    ~BoxScope();
    BoxScope(const BoxScope&) = delete;
    BoxScope& operator=(const BoxScope&) = delete;
    BoxScope(BoxScope&&) = delete;
    BoxScope& operator=(BoxScope&&) = delete;

private:
    std::size_t m_depth; ///< stack size below this box
    int m_exceptions;    ///< std::uncaught_exceptions() when pushed
};

} // namespace imlui::layout

module :private;

namespace imlui::layout {

namespace {

struct Entry {
    Box box;
    const ImDrawList* window = nullptr; ///< the ImGui window it was pushed in (every window has its own draw list)
    int frame = 0;                      ///< ImGui frame it was pushed in
    float regionRight = 0.0f;           ///< the window's content region edge at push time (see current())
    bool mirrored = false;
    // Shrink-to-fit only: the widest claim (from `left`) of this frame and of the last one, which is kept in the
    // window's state storage under `key`.
    ImGuiStorage* storage = nullptr;
    ImGuiID key = 0;
    float left = 0.0f;
    float lastClaim = 0.0f;
    float claim = 0.0f;
};

std::vector<Entry> boxes; // main thread only, like all ImGui code

/// Right edge of ImGui's content region at the cursor (narrowed by table cells).
float regionRight() { return ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; }

/// The top entry when it belongs to the current window and frame.
Entry* top() {
    if (boxes.empty()) return nullptr;
    Entry& e = boxes.back();
    if (e.window != ImGui::GetWindowDrawList() || e.frame != ImGui::GetFrameCount()) return nullptr;
    return &e;
}

/// The box in effect: the top entry when it belongs to the current window and frame, else the content region.
Box current() {
    const Entry* e = top();
    if (!e) return regionBox();
    Box b = e->box;
    // The region edge is constant within a window, except where ImGui narrowed it since the push (a table cell):
    // then the cell's edge bounds the box too.
    const float now = regionRight();
    if (std::fabs(now - e->regionRight) > 0.5f) {
        b.right = std::min(b.right, now);
        if (b.wrapRight) b.wrapRight = std::min(*b.wrapRight, now);
    }
    return b;
}

/// Pops the boxes above `depth`. While an exception unwinds the render pass (a Lua error), a child window or popup
/// may still be current, whose stacks the ImGui pops would hit; ImGui clears both stacks at the window's next
/// Begin(), so they are skipped then.
void popTo(std::size_t depth, bool unwinding) {
    while (boxes.size() > depth) {
        const Entry& e = boxes.back();
        if (e.mirrored && !unwinding) {
            ImGui::PopTextWrapPos();
            ImGui::PopItemWidth();
        }
        if (e.storage && std::fabs(e.claim - e.lastClaim) > 0.5f) {
            // The widest claim changed: the fills take it on the next frame.
            e.storage->SetFloat(e.key, e.claim);
            requestFrame();
        }
        boxes.pop_back();
    }
}

} // namespace

Box regionBox() {
    Box b;
    b.right = regionRight();
    return b;
}

Box boxTo(float right) {
    Box b;
    b.right = right;
    return b;
}

Box shrinkToFitBox(float minWidth, float maxWidth) {
    const float x = ImGui::GetCursorScreenPos().x;
    Box b;
    b.right = x + std::max(0.0f, minWidth);
    b.wrapRight = std::max(b.right, x + maxWidth);
    b.shrinkToFit = true;
    return b;
}

Box insetBox(float inset) {
    Box b = current();
    b.right -= inset;
    if (b.wrapRight) *b.wrapRight -= inset;
    return b;
}

float boxRight() { return current().right; }

float availableWidth() { return std::max(0.0f, current().right - ImGui::GetCursorScreenPos().x); }

float availableHeight() {
    const Box b = current();
    if (!b.bottom) return std::max(0.0f, ImGui::GetContentRegionAvail().y);
    return std::max(0.0f, *b.bottom - ImGui::GetCursorScreenPos().y);
}

float textWrapPos() {
    // PushTextWrapPos takes a window-local x (ImGui adds the window position and subtracts the scroll); 0 and less
    // mean something else (the window edge, no wrap), so it is at least a pixel.
    const Box b = current();
    return std::max(1.0f, b.wrapRight.value_or(b.right) - ImGui::GetWindowPos().x + ImGui::GetScrollX());
}

bool shrinkToFit() {
    const Entry* e = top();
    return e && e->box.shrinkToFit;
}

float claimWidth(float natural) {
    if (Entry* e = top(); e && e->storage)
        e->claim = std::max(e->claim, ImGui::GetCursorScreenPos().x + natural - e->left);
    return std::max(natural, availableWidth());
}

void pushBox(Box box, Mirror mirror) {
    // Every frame ends with an empty stack, unless a render pass threw past a popBox(): drop what it left.
    if (!boxes.empty() && boxes.front().frame != ImGui::GetFrameCount()) boxes.clear();
    Entry e;
    e.box = box;
    e.window = ImGui::GetWindowDrawList();
    e.frame = ImGui::GetFrameCount();
    e.regionRight = regionRight();
    e.mirrored = mirror == Mirror::ItemWidthAndWrap;
    if (box.shrinkToFit) {
        e.storage = ImGui::GetStateStorage();
        e.key = ImGui::GetID("##imlui.layout.claim");
        e.left = ImGui::GetCursorScreenPos().x;
        e.lastClaim = e.storage->GetFloat(e.key, 0.0f);
        e.box.right = std::max(box.right, std::min(box.wrapRight.value_or(box.right), e.left + e.lastClaim));
    }
    boxes.push_back(e);
    if (e.mirrored) {
        ImGui::PushItemWidth(std::max(1.0f, e.box.right - ImGui::GetCursorScreenPos().x));
        ImGui::PushTextWrapPos(textWrapPos());
    }
}

void popBox() {
    if (!boxes.empty()) popTo(boxes.size() - 1, false);
}

BoxScope::BoxScope(Box box, Mirror mirror) : m_depth{0}, m_exceptions{std::uncaught_exceptions()} {
    pushBox(box, mirror);
    m_depth = boxes.size() - 1; // after pushBox, which may drop stale entries
}

BoxScope::~BoxScope() { popTo(m_depth, std::uncaught_exceptions() != m_exceptions); }

} // namespace imlui::layout
