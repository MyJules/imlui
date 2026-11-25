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
export module imlui.ui.elements.list_view;

import std;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.types.frame;
import imlui.types.type_builder;
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
/// Props:   itemHeight = unset (measured)
class ListView final : public ScrollView {
public:
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

    std::optional<float> m_itemHeight;

    std::vector<float> m_heights; ///< per model index (0-based), when `itemHeight` is unset
    double m_measuredSum = 0.0;   ///< the measured heights' sum
    std::size_t m_measured = 0;   ///< how many are measured
    std::size_t m_spaceless = 0;  ///< how many take no space

    float m_contentHeight = 0.0f;
    std::int64_t m_first = 0;
    std::int64_t m_last = -1;
    int m_unsettled = 0; ///< consecutive frames on which heights changed
};

} // namespace imlui

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
    type.extends<ScrollView>();
    type.property("itemHeight", &ListView::itemHeight, &ListView::setItemHeight);
}

std::optional<float> ListView::itemHeight() const { return m_itemHeight; }
void ListView::setItemHeight(std::optional<float> itemHeight) {
    if (itemHeight && !(*itemHeight > 0.0f)) itemHeight.reset(); // 0, negative or NaN: measured
    m_itemHeight = itemHeight;
}
float ListView::contentHeight() const { return m_contentHeight; }
std::pair<std::int64_t, std::int64_t> ListView::renderedRange() const { return {m_first, m_last}; }

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

void ListView::renderContent(Frame& frame) {
    const luaui::Node& node = frame.node();
    lua::State* state = node.state();
    const int stackTop = lua::gettop(state);
    lua::checkstack(state, 24);
    const std::optional<std::size_t> count = callCount(node);
    if (!count) {
        // No delegate: the ListView's own children, like a ScrollView.
        ScrollView::renderContent(frame);
        return;
    }
    const std::size_t n = *count;

    const float gap = spacing() ? frame.dp(static_cast<float>(*spacing())) : ImGui::GetStyle().ItemSpacing.y;
    const std::optional<float> fixed = m_itemHeight ? std::optional<float>{frame.dp(*m_itemHeight)} : std::nullopt;
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
        lua::pushcstring(state, "__inline"); // raw, like the renderer: only the table's own marker counts
        lua::rawget(state, item);
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

    const float est = fixed ? *fixed : estimate(frame);
    const auto stepOf = [&](std::size_t i) { return fixed ? *fixed + gap : step(i, gap, est); };
    bool changed = false;
    std::size_t first = 0, anchor = 0, end = 0; // rendered: [first, end); anchor: the first item in view
    float anchorTop = 0.0f;
    if (n > 0) {
        // The first item in view, and the margin above it.
        if (fixed) {
            const float s = *fixed + gap;
            anchor = std::min(n - 1, static_cast<std::size_t>(std::max(0.0f, viewTop) / s));
            anchorTop = static_cast<float>(anchor) * s;
        } else {
            while (anchor + 1 < n && anchorTop + stepOf(anchor) <= viewTop)
                anchorTop += stepOf(anchor++);
        }
        first = anchor;
        for (float above = 0.0f; first > 0 && above < margin;)
            above += stepOf(--first);

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
                const float h = renderItem(origin.y + y);
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
        while (end < n && y < limit) {
            const float perItem = std::max(1.0f, (fixed ? *fixed : est) + gap);
            const std::size_t want = static_cast<std::size_t>(std::ceil((limit - y) / perItem)) + 1;
            const std::size_t chunkEnd = std::min(n, end + want);
            if (!pushRange(node, end + 1, chunkEnd)) break;
            const int list = lua::gettop(state);
            const std::size_t chunkFirst = end;
            for (; end < chunkEnd && y < limit; ++end) {
                lua::rawgeti(state, list, static_cast<lua::Integer>(end - chunkFirst + 1));
                const float h = renderItem(y);
                lua::settop(state, list);
                if (fixed) {
                    y += *fixed + gap;
                } else {
                    changed = setHeight(end, h) || changed;
                    y += step(end, gap, est);
                }
            }
            lua::settop(state, list - 1);
        }
    }
    lua::settop(state, stackTop);

    // The content's height by what is known now, and the scroll moved by as much as the anchor moved: items above it
    // that were measured at another height than assumed (or the estimate that changed) would otherwise shift what
    // is in view on the next frame.
    const float est2 = fixed ? *fixed : estimate(frame);
    float total = 0.0f;
    if (fixed) {
        total = static_cast<float>(n) * (*fixed + gap);
    } else {
        const std::size_t spaced = n - m_spaceless;
        total = static_cast<float>(m_measuredSum) + static_cast<float>(n - m_measured - m_spaceless) * est2 +
                static_cast<float>(spaced) * gap;
    }
    if (total > 0.0f) total = std::max(0.0f, total - gap); // no gap after the last item
    m_contentHeight = total;
    m_first = n > 0 && end > first ? static_cast<std::int64_t>(first) + 1 : 0;
    m_last = n > 0 && end > first ? static_cast<std::int64_t>(end) : -1;
    ImGui::SetCursorScreenPos({origin.x, origin.y + total});
    ImGui::Dummy({0.0f, 0.0f}); // the content's extent: the scroll range

    if (!fixed && n > 0) {
        const float moved = top(anchor, gap, est2) - anchorTop;
        if (std::abs(moved) > 0.5f && !restoringScroll()) ImGui::SetScrollY(scrollY + moved);
    }
    // New heights change the content's size, which ImGui's scroll range follows on the next frame: render it, unless
    // the heights keep changing (an animated item), like the layout cache's settle limit.
    m_unsettled = changed ? m_unsettled + 1 : 0;
    if (changed && m_unsettled <= layout::maxSettleFrames) frame.requestFrame();
}

} // namespace imlui
