// The layout box: the space a parent gives its children, as a stack of boxes in screen coordinates. Constraints go
// down: a container that gives its children less than the ImGui window's content region (a fixed-width Column, a
// share of a Row, a Grid cell) pushes a box, and everything that sizes, aligns or wraps by "the available width"
// (resolveSize through Frame::resolvedWidth() and md::itemWidth(), md::text's align and wrap, md::divider, a
// Column's `align`) reads availableWidth() instead of ImGui::GetContentRegionAvail().
//
// A box belongs to the ImGui window it was pushed in (and to the frame): in another window (a popup, a child window
// a container opened without a box of its own) the queries fall back to that window's content region. When ImGui
// itself narrows the region under a box (a table cell begun after the box was pushed), the narrower edge wins.
export module imlui.ui.layout_box;

import std;

import imgui;

export namespace imlui::layout {

/// The space given to a container's children, in absolute screen pixels.
struct Box {
    /// Right edge: the available width at the cursor is `right - cursor x`, so Indent / SameLine / a Row's x
    /// positions shrink it.
    float right = 0.0f;
    /// Bottom edge; unset uses the ImGui window's content region.
    std::optional<float> bottom;
    /// Reserved for auto-sized windows (not applied yet): the window takes its content's width, so a fill
    /// resolves to minW instead of the current window width.
    bool shrinkToFit = false;
    float minW = 0.0f;
    float maxW = std::numeric_limits<float>::max();
};

/// The ImGui content region at the cursor as a box (what the queries use where no box was pushed).
[[nodiscard]] Box regionBox();
/// A box ending at `right`.
[[nodiscard]] Box boxTo(float right);

/// Right edge of the current box (screen x).
[[nodiscard]] float boxRight();
/// Width / height available at the cursor in the current box (>= 0).
[[nodiscard]] float availableWidth();
[[nodiscard]] float availableHeight();
/// The current box's right edge as an ImGui::PushTextWrapPos() position (window-local), for wrapping text at it.
[[nodiscard]] float textWrapPos();

/// Pushes a box for its lifetime. By default it also mirrors the box to ImGui's own state, so raw ImGui widgets in
/// custom elements follow it: PushItemWidth(right - cursor x) and PushTextWrapPos(right). The root box of a new
/// window (Mirror::None) leaves ImGui's per-window defaults alone.
class BoxScope {
public:
    enum class Mirror { ItemWidthAndWrap, None };

    explicit BoxScope(Box box, Mirror mirror = Mirror::ItemWidthAndWrap);
    ~BoxScope();
    BoxScope(const BoxScope&) = delete;
    BoxScope& operator=(const BoxScope&) = delete;
    BoxScope(BoxScope&&) = delete;
    BoxScope& operator=(BoxScope&&) = delete;

private:
    bool m_mirrored;
    int m_exceptions; ///< std::uncaught_exceptions() when pushed
};

} // namespace imlui::layout

module :private;

namespace imlui::layout {

namespace {

struct Entry {
    Box box;
    const ImDrawList* window; ///< the ImGui window it was pushed in (every window has its own draw list)
    int frame;                ///< ImGui frame it was pushed in
    float regionRight;        ///< the window's content region edge at push time (see current())
};

std::vector<Entry> boxes; // main thread only, like all ImGui code

/// Right edge of ImGui's content region at the cursor (narrowed by table cells).
float regionRight() { return ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; }

/// The box in effect: the top entry when it belongs to the current window and frame, else the content region.
Box current() {
    if (boxes.empty()) return regionBox();
    const Entry& top = boxes.back();
    if (top.window != ImGui::GetWindowDrawList() || top.frame != ImGui::GetFrameCount()) return regionBox();
    Box b = top.box;
    // The region edge is constant within a window, except where ImGui narrowed it since the push (a table cell):
    // then the cell's edge bounds the box too.
    const float now = regionRight();
    if (std::fabs(now - top.regionRight) > 0.5f) b.right = std::min(b.right, now);
    return b;
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
    return std::max(1.0f, boxRight() - ImGui::GetWindowPos().x + ImGui::GetScrollX());
}

BoxScope::BoxScope(Box box, Mirror mirror)
    : m_mirrored{mirror == Mirror::ItemWidthAndWrap}, m_exceptions{std::uncaught_exceptions()} {
    boxes.push_back({box, ImGui::GetWindowDrawList(), ImGui::GetFrameCount(), regionRight()});
    if (m_mirrored) {
        ImGui::PushItemWidth(std::max(1.0f, box.right - ImGui::GetCursorScreenPos().x));
        ImGui::PushTextWrapPos(textWrapPos());
    }
}

BoxScope::~BoxScope() {
    // A Lua error unwinding the render pass (a C++ exception) may leave a child window or popup current, whose
    // stacks these pops would hit. ImGui clears both stacks at the window's next Begin(), so they are skipped then.
    if (m_mirrored && std::uncaught_exceptions() == m_exceptions) {
        ImGui::PopTextWrapPos();
        ImGui::PopItemWidth();
    }
    boxes.pop_back();
}

} // namespace imlui::layout
