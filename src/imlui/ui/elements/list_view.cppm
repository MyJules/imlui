// ListView: a ScrollView whose children come from `model` / `delegate`, virtualized: it builds and renders only the
// delegates in view (and a margin around it), and stands in empty space for the others.
//
// The Lua side (src/resources/imlui.lua) builds the delegates: `__count(self)` resolves the model once a render and
// returns its length, `__range(self, first, last)` returns the delegates for model indices first..last (built on
// demand, cached). This element decides which indices that is, from the scroll position and the items' heights:
// `itemHeight` when it's set, otherwise the heights it measured, with their average standing in for the items it
// hasn't rendered yet. Items above the first one in view are measured too (the margin), and when that changes where
// the first one in view starts, the scroll moves by the same amount on the next frame, so what is on screen doesn't
// jump. A ListView without a delegate is a plain ScrollView of its own children.
//
// Scrolling from a script: `followBottom` keeps the view at the end while it is there (checked at the start of a
// frame against ImGui's scroll range, which is the last frame's content), and positionViewAtIndex() remembers a
// request that every frame turns into a scroll target from the heights known by then, until the scroll is at the
// target and no height changed (the item is then where it was asked to be), for at most maxPositionFrames frames.
export module imlui.ui.elements.list_view;

import std;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.types.frame;
import imlui.types.lua_convert;
import imlui.types.type_builder;
import imlui.ui.culling;
import imlui.ui.elements.scroll_view;
import imlui.ui.frame_request;
import imlui.ui.item_size;
import imlui.ui.layout_box;
import imlui.ui.layout_cache;
import imlui.ui.props;

export namespace imlui {

/// A ScrollView whose children come from `model` / `delegate` (Lua side), every ScrollView prop. Only the delegates
/// in view, and a margin of half the view's height above and below it, are built and rendered.
///
/// Props:   itemHeight = unset (measured), followBottom = false, firstVisibleIndex (read-only)
/// Methods: positionViewAtIndex(index, mode), positionViewAtBeginning(), positionViewAtEnd()
class ListView final : public ScrollView {
public:
    /// Where positionViewAtIndex() puts the item: its top at the view's top, its middle at the view's middle, its
    /// bottom at the view's bottom, or the least scroll that shows all of it (its top, when it is taller than the
    /// view).
    enum class Position { Beginning, Center, End, Contain };

    static void declare(TypeBuilder<ListView>& type);

    /// The height of every item (dp). Unset: each item's height is measured when it renders, and the average of the
    /// measured ones stands in for the others.
    [[nodiscard]] std::optional<float> itemHeight() const;
    void setItemHeight(std::optional<float> itemHeight);

    /// The height of the content (pixels, without the padding) when the element last rendered: every item's height
    /// (measured or estimated) and the gaps between them.
    [[nodiscard]] float contentHeight() const;
    /// The model indices (1-based, inclusive) rendered last time; {0, -1} when none were.
    [[nodiscard]] std::pair<std::int64_t, std::int64_t> renderedRange() const;

    /// Keeps the view at the end while it is scrolled there (a log, a chat): new items, taller items, a smaller view.
    [[nodiscard]] bool followBottom() const;
    void setFollowBottom(bool followBottom);
    /// The model index (1-based) of the first item in view when the element last rendered; 0 when none was.
    [[nodiscard]] std::int64_t firstVisibleIndex() const;

    /// Scrolls so that item `index` (1-based, clamped to the model) is where `mode` says (Beginning when unset),
    /// built or not: from the next render on, until it is there.
    void positionViewAtIndex(std::int64_t index, std::optional<Position> mode);
    /// Scrolls to the top.
    void positionViewAtBeginning();
    /// Scrolls to the end.
    void positionViewAtEnd();

protected:
    void renderContent(Frame& frame) override;

private:
    /// Per index: the measured height in pixels, `unmeasured`, or `noSpace` (nothing visual: a delegate that returned
    /// nil, a hidden or non-visual element), which takes no space and no gap.
    static constexpr float unmeasured = -1.0f;
    static constexpr float noSpace = -2.0f;

    /// Sets the height of index `i`; returns whether it changed by more than half a pixel.
    bool setHeight(std::size_t i, float h);
    /// The space index `i` takes, its gap below included.
    [[nodiscard]] float step(std::size_t i, float gap, float estimate) const;
    /// Where index `i` starts, from the top of the content.
    [[nodiscard]] float top(std::size_t i, float gap, float estimate) const;
    /// The height of an item that wasn't measured: the measured ones' average, else a list item's.
    [[nodiscard]] float estimate(const Frame& frame) const;
    /// The scroll that puts index `i` where `mode` says in a view `viewHeight` tall scrolled to `scrollY`.
    [[nodiscard]] float positionScroll(std::size_t i, Position mode, float scrollY, float viewHeight, float gap,
                                       float estimate, std::optional<float> fixed) const;
    /// Sets firstVisibleIndex, and tells Lua when it changed.
    void publishFirstVisible(std::int64_t index);

    /// A positionViewAtIndex() call that hasn't landed yet.
    struct PendingPosition {
        std::int64_t index = 1;
        Position mode = Position::Beginning;
        int frames = 0; ///< scroll targets set for it so far
    };
    /// Frames a position may take to land before it is dropped (heights that keep changing).
    static constexpr int maxPositionFrames = 20;

    std::optional<float> m_itemHeight;
    bool m_followBottom = false;
    bool m_following = true;    ///< the view was at the end last frame (it starts there)
    float m_viewHeight = -1.0f; ///< the visible content height last frame (pixels)
    float m_followTarget = -1.0f; ///< the scroll followBottom asked for last frame (-1: none), and the scroll then
    float m_followScroll = -1.0f;
    std::int64_t m_firstVisible = 0;
    std::optional<PendingPosition> m_position;
    int m_steerFrame = -1; ///< ImGui frame of the last render that scrolled for the above (the first one of a frame)

    std::vector<float> m_heights; ///< per model index (0-based), when `itemHeight` is unset
    double m_measuredSum = 0.0;   ///< the measured heights' sum
    std::size_t m_measured = 0;   ///< how many are measured
    std::size_t m_spaceless = 0;  ///< how many take no space

    float m_contentHeight = 0.0f;
    std::int64_t m_first = 0;
    std::int64_t m_last = -1;
    int m_unsettled = 0; ///< consecutive frames on which heights changed
    /// Indices (0-based) that held the active item, the nav focus, an open popup or a non-visual element when they
    /// last rendered: they render out of range too (cull::held), at most maxPinned.
    std::vector<std::size_t> m_pinned;
    static constexpr std::size_t maxPinned = 16;
};

} // namespace imlui

template <>
struct imlui::EnumNames<imlui::ListView::Position> {
    static constexpr std::pair<imlui::ListView::Position, std::string_view> values[] = {
        {imlui::ListView::Position::Beginning, "beginning"},
        {imlui::ListView::Position::Center, "center"},
        {imlui::ListView::Position::End, "end"},
        {imlui::ListView::Position::Contain, "contain"},
    };
};

module :private;

namespace imlui {

namespace {

/// The height an unmeasured item is assumed to have before any was measured (dp): a one-line Material list item.
constexpr float defaultItemHeight = 56.0f;

/// Calls `__count(self)`: the model's length (resolving it for the __range calls of this render). nullopt when it
/// failed (reported) or the element has no `__count`.
std::optional<std::size_t> callCount(const luaui::Node& node) {
    lua::State* state = node.state();
    if (node.pushRaw("__count") != lua::Type::Function) {
        lua::pop(state);
        return std::nullopt;
    }
    lua::pushvalue(state, node.index());
    if (!luaui::safeRead(state, 1, 1)) return std::nullopt;
    const lua::Integer n = lua::isinteger(state, -1) ? lua::tointeger(state, -1) : 0;
    lua::pop(state);
    return static_cast<std::size_t>(std::max<lua::Integer>(0, n));
}

/// Pushes `__range(self, first, last)` (1-based, inclusive): the delegates for those model indices, in order (an
/// entry is false for a delegate that returned nil). False, with nothing pushed, when it failed (reported).
bool pushRange(const luaui::Node& node, std::size_t first, std::size_t last) {
    lua::State* state = node.state();
    if (node.pushRaw("__range") != lua::Type::Function) {
        lua::pop(state);
        return false;
    }
    lua::pushvalue(state, node.index());
    lua::pushinteger(state, static_cast<lua::Integer>(first));
    lua::pushinteger(state, static_cast<lua::Integer>(last));
    if (!luaui::safeRead(state, 3, 1)) return false;
    if (!lua::istable(state, -1)) {
        lua::pop(state);
        return false;
    }
    return true;
}

} // namespace

void ListView::declare(TypeBuilder<ListView>& type) {
    type.doc("A virtualized `ScrollView` (it takes every `ScrollView` prop) whose children come from `model` / "
             "`delegate`: only the items in view, and half a view's height above and below, are built and rendered.\n"
             "Built delegates are cached (by index, or by `key`); without `delegate` its own children are used, like a "
             "`ScrollView`'s.");
    type.extends<ScrollView>();
    type.property("itemHeight", &ListView::itemHeight, &ListView::setItemHeight)
        .doc("Every item's height (dp). Unset (or not > 0): each item is measured as it renders, and the measured "
             "ones' average stands in for the others.")
        .property("followBottom", &ListView::followBottom, &ListView::setFollowBottom)
        .doc("Keeps the view at the end while it is scrolled there (`false`): new items, items that turn out taller "
             "and a smaller view keep the last item in view, for a log or a chat. Scrolling up stops it; scrolling "
             "back to the end starts it again. A list starts at the end. Needs a `delegate`.")
        .property("firstVisibleIndex", &ListView::firstVisibleIndex)
        .doc("Read-only: the model index (1-based) of the first item at least partly in view when the list last "
             "rendered; 0 before that, with an empty model or without a `delegate`.")
        .method("positionViewAtIndex", &ListView::positionViewAtIndex, {"index", "mode"})
        .doc("Scrolls so that item `index` (1-based; clamped to 1..the model's length) is placed by `mode`: "
             "`\"beginning\"` (default: its top at the view's top), `\"center\"`, `\"end\"` (its bottom at the view's "
             "bottom) or `\"contain\"` (the least scroll that shows all of it). Works for items never built: it "
             "scrolls by the heights known (`itemHeight`, else the measured ones and their average), and corrects "
             "on the next frames as the items there are measured. Takes effect at the next render; nothing with an "
             "empty model or without a `delegate`.")
        .method("positionViewAtBeginning", &ListView::positionViewAtBeginning)
        .doc("Scrolls to the top: `positionViewAtIndex(1, \"beginning\")`.")
        .method("positionViewAtEnd", &ListView::positionViewAtEnd)
        .doc("Scrolls to the end: the last item's bottom at the view's bottom.")
        .docMember("spacing", "The gap between items (dp). Unset: ImGui's item spacing.");
}

std::optional<float> ListView::itemHeight() const { return m_itemHeight; }
void ListView::setItemHeight(std::optional<float> itemHeight) {
    if (itemHeight && !(*itemHeight > 0.0f)) itemHeight.reset(); // 0, negative or NaN: measured
    m_itemHeight = itemHeight;
}
float ListView::contentHeight() const { return m_contentHeight; }
std::pair<std::int64_t, std::int64_t> ListView::renderedRange() const { return {m_first, m_last}; }
bool ListView::followBottom() const { return m_followBottom; }
void ListView::setFollowBottom(bool followBottom) { m_followBottom = followBottom; }
std::int64_t ListView::firstVisibleIndex() const { return m_firstVisible; }

void ListView::positionViewAtIndex(std::int64_t index, std::optional<Position> mode) {
    m_position = PendingPosition{index, mode.value_or(Position::Beginning), 0};
    requestFrame();
}
void ListView::positionViewAtBeginning() { positionViewAtIndex(1, Position::Beginning); }
void ListView::positionViewAtEnd() { positionViewAtIndex(std::numeric_limits<std::int64_t>::max(), Position::End); }

void ListView::publishFirstVisible(std::int64_t index) {
    if (index == m_firstVisible) return;
    m_firstVisible = index;
    notify("firstVisibleIndex");
}

bool ListView::setHeight(std::size_t i, float h) {
    float& slot = m_heights[i];
    const float old = slot;
    // ImGui puts items on whole pixels, so an item measures a fraction of a pixel differently at another y. Kept
    // as it was, the average doesn't drift with the scroll (times every unmeasured item above, a visible jump).
    if (old >= 0.0f && h >= 0.0f && std::abs(old - h) <= 0.5f) return false;
    if (old >= 0.0f) {
        m_measuredSum -= static_cast<double>(old);
        --m_measured;
    } else if (old == noSpace) {
        --m_spaceless;
    }
    slot = h;
    if (h >= 0.0f) {
        m_measuredSum += static_cast<double>(h);
        ++m_measured;
    } else if (h == noSpace) {
        ++m_spaceless;
    }
    if (old < 0.0f && h < 0.0f) return old != h;
    return true;
}

float ListView::step(std::size_t i, float gap, float estimate) const {
    const float h = m_heights[i];
    if (h >= 0.0f) return h + gap;
    return h == noSpace ? 0.0f : estimate + gap;
}

float ListView::top(std::size_t i, float gap, float estimate) const {
    float y = 0.0f;
    for (std::size_t j = 0; j < i; ++j)
        y += step(j, gap, estimate);
    return y;
}

float ListView::estimate(const Frame& frame) const {
    if (m_measured > 0) return static_cast<float>(m_measuredSum / static_cast<double>(m_measured));
    return frame.dp(defaultItemHeight);
}

float ListView::positionScroll(std::size_t i, Position mode, float scrollY, float viewHeight, float gap,
                               float estimate, std::optional<float> fixed) const {
    float t = 0.0f, h = 0.0f;
    if (fixed) {
        t = static_cast<float>(i) * std::max(1.0f, *fixed + gap); // fixedStep in renderContent()
        h = *fixed;
    } else {
        t = top(i, gap, estimate);
        const float measured = m_heights[i];
        h = measured >= 0.0f ? measured : (measured == noSpace ? 0.0f : estimate);
    }
    switch (mode) {
    case Position::Beginning: return t;
    case Position::Center: return t + (h - viewHeight) * 0.5f;
    case Position::End: return t + h - viewHeight;
    case Position::Contain: break;
    }
    // The least scroll that shows it: its bottom first, then its top, which wins for an item taller than the view.
    return std::min(std::max(scrollY, t + h - viewHeight), t);
}

void ListView::renderContent(Frame& frame) {
    const luaui::Node& node = frame.node();
    lua::State* state = node.state();
    const int stackTop = lua::gettop(state);
    lua::checkstack(state, 24);
    const std::optional<std::size_t> count = callCount(node);
    if (!count) {
        // No delegate: the ListView's own children, like a ScrollView.
        m_position.reset();
        ScrollView::renderContent(frame);
        publishFirstVisible(0);
        return;
    }
    const std::size_t n = *count;

    const float gap = spacing() ? frame.dp(static_cast<float>(*spacing())) : ImGui::GetStyle().ItemSpacing.y;
    const std::optional<float> fixed = m_itemHeight ? std::optional<float>{frame.dp(*m_itemHeight)} : std::nullopt;
    // The distance from one fixed-height item's top to the next one's. At least a pixel: a negative `spacing` as large
    // as the height would put every item at one place, so no number of them would fill the view (and the index in
    // view, the scroll divided by it, would divide by zero).
    const float fixedStep = fixed ? std::max(1.0f, *fixed + gap) : 0.0f;
    if (!fixed && m_heights.size() != n) {
        // The model changed length: keep the heights of the indices that are still there.
        if (n < m_heights.size()) {
            for (std::size_t i = n; i < m_heights.size(); ++i)
                setHeight(i, unmeasured);
        }
        m_heights.resize(n, unmeasured);
    }

    // The view in content coordinates (0: the top of the first item).
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float viewTop = ImGui::GetWindowPos().y - origin.y;
    const float viewHeight = ImGui::GetWindowSize().y;
    const float margin = std::max(viewHeight * 0.5f, frame.dp(defaultItemHeight));
    const float limit = origin.y + viewTop + viewHeight + margin; // screen y where rendering stops
    const float scrollY = ImGui::GetScrollY();
    // The content's visible height (without the padding and a horizontal scrollbar): the scroll range is the content's
    // height minus this.
    const float visibleHeight = std::max(0.0f, ImGui::GetContentRegionAvail().y);
    const bool restoring = restoringScroll();
    // followBottom and positions steer the scroll on the first render of a frame (the same list shown twice in a frame
    // has two windows), and not while ScrollView puts its scroll back.
    const int frameNo = ImGui::GetFrameCount();
    const bool steer = !restoring && frameNo != m_steerFrame;
    if (steer) m_steerFrame = frameNo;
    // Following: at the end now (ImGui's range is last frame's content, which the scroll was clamped to), or following
    // last frame and the view changed height (a smaller view leaves the scroll where it was, above the end).
    bool following = false;
    if (m_followBottom && steer) {
        following = scrollY >= ImGui::GetScrollMaxY() - 1.0f ||
                    (m_following && std::abs(visibleHeight - m_viewHeight) > 0.5f);
    }
    const float prevTotal = m_contentHeight;

    // Horizontal placement: the stack's `align`, as Frame::renderStack does it.
    const float align = alignFactor(this->align());
    const float boxWidth = layout::availableWidth();
    const auto renderAt = [&](Frame::Child& child, float y) {
        float x = origin.x;
        float inset = 0.0f;
        if (align > 0.0f) {
            const float width = sizeProp(child.node(), "width");
            if (width != 0.0f ? width < 0.0f : child.fillsWidth()) {
                inset = std::max(0.0f, (boxWidth - resolveSize(width != 0.0f ? width : -1.0f, boxWidth, 0.0f)) * align);
                x += inset;
            } else {
                x += std::max(0.0f, (boxWidth - child.expectedWidth(boxWidth)) * align);
            }
        }
        ImGui::SetCursorScreenPos({x, y});
        bool rendered = false;
        if (inset > 0.0f) {
            const layout::BoxScope box{layout::insetBox(-inset)};
            rendered = child.render();
        } else {
            rendered = child.render();
        }
        if (!rendered) ImGui::Dummy({0.0f, 0.0f}); // the cursor moved: ImGui wants an item after it
        return rendered;
    };

    // A one-element table to splice an inline delegate (a Repeater or an If) from.
    lua::createtable(state, 1, 0);
    const int wrapper = lua::gettop(state);

    // Renders the delegate at the top of the stack (an element, or false) at screen y `y`. Returns its height, or
    // noSpace when it took none. An inline delegate's children stack with the gap between them, and together they
    // are the item.
    const auto renderItem = [&](float y) -> float {
        const int item = lua::gettop(state);
        if (!lua::istable(state, item)) return noSpace;
        float bottom = y;
        bool spaced = false;
        const auto one = [&](Frame::Child& child) {
            if (!child.isVisual()) {
                child.render();
                return;
            }
            if (renderAt(child, spaced ? bottom + gap : y)) {
                bottom = ImGui::GetItemRectMax().y;
                spaced = true;
            }
        };
        lua::rawgetfield(state, item, "__inline"); // raw, like the renderer: only the table's own marker counts
        const bool isInline = lua::toboolean(state, -1);
        lua::pop(state);
        if (isInline) {
            lua::pushvalue(state, item);
            lua::rawseti(state, wrapper, 1);
            frame.forEachChild(luaui::Node{state, wrapper}, one);
        } else {
            const luaui::Node itemNode{state, item};
            lua::getfield(state, item, "__type");
            const bool element = lua::isstring(state, -1);
            lua::pop(state);
            if (element && itemNode.boolean("visible", true)) {
                Frame::Child child{frame, itemNode};
                one(child);
            }
        }
        return spaced ? std::max(0.0f, bottom - y) : noSpace;
    };
    // renderItem() for index `i`, remembering it when it holds what must keep rendering out of view (the culling
    // rules: the active item, the nav focus, an open popup, a non-visual element).
    std::vector<std::size_t> pinned;
    const auto renderIndex = [&](std::size_t i, float y) -> float {
        const cull::Watch before = cull::watch();
        const float h = renderItem(y);
        if (cull::held(before) && pinned.size() < maxPinned) pinned.push_back(i);
        return h;
    };

    const float est = fixed ? *fixed : estimate(frame);
    const auto stepOf = [&](std::size_t i) { return fixed ? fixedStep : step(i, gap, est); };
    bool changed = false;
    std::size_t first = 0, anchor = 0, end = 0; // rendered: [first, end); anchor: the first item in view
    float anchorTop = 0.0f;
    if (n > 0) {
        // The first item in view, and the margin above it.
        if (fixed) {
            anchor = std::min(n - 1, static_cast<std::size_t>(std::max(0.0f, viewTop) / fixedStep));
            anchorTop = static_cast<float>(anchor) * fixedStep;
        } else {
            while (anchor + 1 < n && anchorTop + stepOf(anchor) <= viewTop)
                anchorTop += stepOf(anchor++);
        }
        // The margin, and more while a nav move scores items (cull::outOfView widens the view then): the item it
        // lands on must be submitted.
        first = anchor;
        for (float above = 0.0f; first > 0;) {
            const float bottom = origin.y + anchorTop - above;
            if (above >= margin && cull::outOfView(bottom - stepOf(first - 1), bottom)) break;
            above += stepOf(--first);
        }

        // Above the anchor: placed by the heights known so far (out of view, so a wrong estimate isn't seen), and
        // measured.
        if (first < anchor && pushRange(node, first + 1, anchor)) {
            const int list = lua::gettop(state);
            float y = anchorTop;
            for (std::size_t i = first; i < anchor; ++i)
                y -= stepOf(i);
            for (std::size_t i = first; i < anchor; ++i) {
                const float before = stepOf(i);
                lua::rawgeti(state, list, static_cast<lua::Integer>(i - first + 1));
                const float h = renderIndex(i, origin.y + y);
                lua::settop(state, list);
                if (!fixed) changed = setHeight(i, h) || changed;
                y += before;
            }
            lua::settop(state, list - 1);
        }

        // From the anchor down: one after the other, at the heights they render at, until past the view and the
        // margin. The delegates are asked for in chunks of the estimated number still needed.
        float y = origin.y + anchorTop;
        end = anchor;
        const auto more = [&] { return y < limit || !cull::outOfView(y, y + 1.0f); };
        while (end < n && more()) {
            const float perItem = fixed ? fixedStep : std::max(1.0f, est + gap);
            // Past `limit` only while a nav move widens the view (see more()): by a view's height, so a page more.
            // The distance is positive either way; a negative one cast to size_t is undefined (in practice a huge
            // count that wraps, and a chunk that never advances).
            const float reach = y < limit ? limit - y : std::max(1.0f, viewHeight);
            const std::size_t want = static_cast<std::size_t>(std::ceil(reach / perItem)) + 1;
            const std::size_t chunkEnd = std::min(n, end + want);
            if (!pushRange(node, end + 1, chunkEnd)) break;
            const int list = lua::gettop(state);
            const std::size_t chunkFirst = end;
            for (; end < chunkEnd && more(); ++end) {
                lua::rawgeti(state, list, static_cast<lua::Integer>(end - chunkFirst + 1));
                const float h = renderIndex(end, y);
                lua::settop(state, list);
                if (fixed) {
                    y += fixedStep;
                } else {
                    changed = setHeight(end, h) || changed;
                    y += step(end, gap, est);
                }
            }
            lua::settop(state, list - 1);
        }

        // Items held last frame (see renderIndex) that are out of range now keep rendering, at their place.
        for (const std::size_t i : m_pinned) {
            if (i >= n || (i >= first && i < end)) continue;
            if (!pushRange(node, i + 1, i + 1)) continue;
            lua::rawgeti(state, -1, 1);
            const float at = fixed ? static_cast<float>(i) * fixedStep : top(i, gap, est);
            const float h = renderIndex(i, origin.y + at);
            if (!fixed) changed = setHeight(i, h) || changed;
            lua::settop(state, wrapper);
        }
    }
    lua::settop(state, stackTop);
    m_pinned = std::move(pinned);

    // The content's height by what is known now, and the scroll moved by as much as the anchor moved: items above it
    // that were measured at another height than assumed (or the estimate that changed) would otherwise shift what
    // is in view on the next frame.
    const float est2 = fixed ? *fixed : estimate(frame);
    float total = 0.0f;
    if (fixed) {
        if (n > 0) total = std::max(0.0f, static_cast<float>(n - 1) * fixedStep + *fixed); // the last item's bottom
    } else {
        const std::size_t spaced = n - m_spaceless;
        total = static_cast<float>(m_measuredSum) + static_cast<float>(n - m_measured - m_spaceless) * est2 +
                static_cast<float>(spaced) * gap;
        if (total > 0.0f) total = std::max(0.0f, total - gap); // no gap after the last item
    }
    m_contentHeight = total;
    m_first = n > 0 && end > first ? static_cast<std::int64_t>(first) + 1 : 0;
    m_last = n > 0 && end > first ? static_cast<std::int64_t>(end) : -1;
    ImGui::SetCursorScreenPos({origin.x, origin.y + total});
    ImGui::Dummy({0.0f, 0.0f}); // the content's extent: the scroll range

    // A position asked for wins over following the end, and both over keeping the first item in view still.
    bool scrolled = false;
    // ImGui's scroll range next frame: the content's height (which it truncates to whole pixels) minus the view's.
    const float maxScroll = std::max(0.0f, std::trunc(total) - visibleHeight);
    if (m_position && steer) {
        if (n == 0) {
            m_position.reset();
        } else {
            const auto index = static_cast<std::size_t>(
                std::clamp<std::int64_t>(m_position->index, 1, static_cast<std::int64_t>(n)));
            const float target = std::clamp(
                std::round(positionScroll(index - 1, m_position->mode, scrollY, visibleHeight, gap, est2, fixed)),
                0.0f, maxScroll);
            // There: the scroll is at the target by the heights this frame rendered at, and none of them changed.
            const bool landed = !changed && std::abs(total - prevTotal) <= 0.5f &&
                                std::abs(scrollY - std::min(target, ImGui::GetScrollMaxY())) <= 1.0f;
            if (landed || m_position->frames >= maxPositionFrames) {
                m_position.reset();
            } else {
                ++m_position->frames;
                ImGui::SetScrollY(target);
                frame.requestFrame();
                scrolled = true;
            }
        }
    } else if (following && std::abs(maxScroll - scrollY) > 0.5f) {
        // The same target as last frame, which left the scroll where it was: ImGui's range ends short of it (content
        // that ends above the last item's bottom), and asking again would never settle.
        const bool stuck = std::abs(maxScroll - m_followTarget) <= 0.5f && std::abs(scrollY - m_followScroll) <= 0.5f;
        if (!stuck) {
            ImGui::SetScrollY(maxScroll);
            frame.requestFrame();
        }
        m_followTarget = maxScroll;
        m_followScroll = scrollY;
        scrolled = true;
    } else {
        m_followTarget = -1.0f;
    }
    if (m_followBottom && steer) {
        m_following = following;
        m_viewHeight = visibleHeight;
    }
    if (!fixed && n > 0 && !scrolled && !following) {
        const float moved = top(anchor, gap, est2) - anchorTop;
        if (std::abs(moved) > 0.5f && !restoring) ImGui::SetScrollY(scrollY + moved);
    }
    // New heights change the content's size, which ImGui's scroll range follows on the next frame: render it, unless
    // the heights keep changing (an animated item), like the layout cache's settle limit.
    m_unsettled = changed ? m_unsettled + 1 : 0;
    if (changed && m_unsettled <= layout::maxSettleFrames) frame.requestFrame();
    // The first item in view is the anchor, unless no more than half a pixel of it is (its gap, the scroll's
    // rounding): a list positioned at an item starts with that item.
    std::size_t firstVisible = anchor;
    if (n > 0 && anchor + 1 < n) {
        const float s = stepOf(anchor);
        if (anchorTop + (s > 0.0f ? s - gap : 0.0f) <= viewTop + 0.5f) ++firstVisible;
    }
    if (steer) publishFirstVisible(n > 0 ? static_cast<std::int64_t>(firstVisible) + 1 : 0);
}

} // namespace imlui
