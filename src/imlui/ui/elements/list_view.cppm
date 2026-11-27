// ListView: a ScrollView whose children come from `model` / `delegate`, virtualized: it builds and renders only the
// delegates in view (and a margin around it), and stands in empty space for the others.
//
// The Lua side (src/resources/imlui.lua) builds the delegates: `__count(self)` resolves the model once a render and
// returns its length, `__range(self, first, last)` returns the delegates for model indices first..last (built on
// demand, cached). This element decides which indices that is, from the scroll position and the items' heights:
// `itemHeight` when it's set, otherwise the heights it measured, with their average standing in for the items it
// hasn't rendered yet. Items above the first one in view are measured too (the margin), and when that changes where
// the first one in view starts, the scroll moves by the same amount on the next frame, so what is on screen doesn't
// jump. With a C++ list model (`__count`'s second result), the measured heights follow their rows: the model's change
// log (ListModel::changesSince) moves them along with insertions, removals, moves and sorts, instead of each index
// keeping the height of the row it held. A ListView without a delegate is a plain ScrollView of its own children.
//
// Scrolling from a script: `followBottom` keeps the view at the end while it is there (checked at the start of a
// frame against ImGui's scroll range, which is the last frame's content), and positionViewAtIndex() remembers a
// request that every frame turns into a scroll target from the heights known by then, until the scroll is at the
// target and no height changed (the item is then where it was asked to be), for at most maxPositionFrames frames.
// Without a delegate both work on the children as the stack placed them: their sizes in the layout cache, stacked the
// way Renderer::renderStack stacks them.
export module imlui.ui.elements.list_view;

import std;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.types.frame;
import imlui.types.list_model;
import imlui.types.lua_convert;
import imlui.types.type_builder;
import imlui.ui.culling;
import imlui.ui.elements.scroll_view;
import imlui.ui.frame_request;
import imlui.ui.item_size;
import imlui.ui.layout_box;
import imlui.ui.layout_cache;
import imlui.ui.props;
import imlui.ui.renderer;

export namespace imlui {

/// A ScrollView whose children come from `model` / `delegate` (Lua side), every ScrollView prop. Only the delegates
/// in view, and a margin of half the view's height above and below it, are built and rendered.
///
/// Props:   itemHeight = unset (measured), followBottom = false, cacheSize = 256, firstVisibleIndex (read-only)
/// Methods: positionViewAtIndex(index, mode), positionViewAtBeginning(), positionViewAtEnd()
class ListView final : public ScrollView {
public:
    /// Where positionViewAtIndex() puts the item: its top at the view's top, its middle at the view's middle, its
    /// bottom at the view's bottom, or the least scroll that shows all of it (its top, when it is taller than the
    /// view).
    enum class Position { Beginning, Center, End, Contain };

    static void declare(TypeBuilder<ListView>& type);

    /// The most items a model shows: the first ones of a longer model (`model = math.huge`, a count gone wrong), which
    /// would otherwise take gigabytes of measured heights, are shown, and the excess is reported once.
    static constexpr std::size_t maxItems = std::size_t{1} << 22;

    /// The height of every item (dp). Unset: each item's height is measured when it renders, and the average of the
    /// measured ones stands in for the others.
    [[nodiscard]] std::optional<float> itemHeight() const;
    void setItemHeight(std::optional<float> itemHeight);

    /// The height of the content (pixels, without the padding) when the element last rendered: every item's height
    /// (measured or estimated) and the gaps between them.
    [[nodiscard]] float contentHeight() const;
    /// The model indices (1-based, inclusive) rendered last time; {0, -1} when none were.
    [[nodiscard]] std::pair<std::int64_t, std::int64_t> renderedRange() const;
    /// The measured height (pixels) of item `index` (1-based); nullopt when it isn't measured (never rendered, taking
    /// no space, out of range, or `itemHeight` is set).
    [[nodiscard]] std::optional<float> measuredHeight(std::int64_t index) const;

    /// Keeps the view at the end while it is scrolled there (a log, a chat): new items, taller items, a smaller view.
    [[nodiscard]] bool followBottom() const;
    void setFollowBottom(bool followBottom);
    /// The delegates the Lua side keeps built: at most max(cacheSize, 4 x the items shown), the ones shown longest ago
    /// dropped first. At least 0.
    [[nodiscard]] int cacheSize() const;
    void setCacheSize(int cacheSize);
    /// The default cacheSize.
    static constexpr int defaultCacheSize = 256;
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
    /// renderContent() without a delegate: the own children, stacked by ScrollView, with followBottom and positions.
    void renderOwnChildren(Frame& frame);
    /// The scroll that puts an item at `t` (content y), `h` tall, where `mode` says in a view `viewHeight` tall
    /// scrolled to `scrollY`.
    [[nodiscard]] static float placeScroll(float t, float h, Position mode, float scrollY, float viewHeight);
    /// followBottom's scroll to `maxScroll` from `scrollY`, unless the last one left the scroll where it was.
    void followEnd(Frame& frame, float maxScroll, float scrollY);
    /// Moves the measured heights along with the changes of `model` since m_modelRevision (its change log), so that
    /// each row keeps its height wherever it moved; nothing when the log doesn't reach back that far (the heights then
    /// stay per index).
    void followRows(const ListModel& model);
    /// Recomputes the sums over m_heights and the step tree from scratch.
    void recount();

    /// Per index: the measured height in pixels, `unmeasured`, or `noSpace` (nothing visual: a delegate that returned
    /// nil, a hidden or non-visual element), which takes no space and no gap.
    static constexpr float unmeasured = -1.0f;
    static constexpr float noSpace = -2.0f;

    /// Prefix sums over the items' steps, so where an index starts and which index is at a y are O(log n), not a walk
    /// from index 0 (a million-row list scrolled to its end). A Fenwick tree whose nodes hold, for their range of
    /// indices, the measured heights' sum, how many are measured and how many take space (measured or not). The
    /// steps of a range are then `heights + (spaced - measured) * estimate + spaced * gap`: the estimate and the gap
    /// apply arithmetically, so a new estimate (each measured height moves the average) or `spacing` needs no update.
    class StepTree {
    public:
        /// Makes the tree hold `heights` (per index: a height, `unmeasured` or `noSpace`): O(n), or O(k log n) when
        /// it held all but the last k of them (the model grew).
        void assign(const std::vector<float>& heights);
        /// The least height the tree counts a measured item (and the estimate) at: 1 - gap for a negative gap, so that
        /// each step is at least a pixel (else 0). Changing it rebuilds the tree from `heights`.
        void setFloor(float floor, const std::vector<float>& heights);
        [[nodiscard]] float floor() const { return m_floor; }
        /// Keeps the first `n` indices (the model shrank).
        void truncate(std::size_t n);
        /// Index `i` changed from `old` to `h`.
        void update(std::size_t i, float old, float h);
        /// Where index `i` starts: the steps of the indices before it.
        [[nodiscard]] float top(std::size_t i, float gap, float estimate) const;
        /// The last index (up to size()) whose top is at most `y`. Needs every step >= 0 (floor() + gap >= 0).
        [[nodiscard]] std::size_t lastStartingBy(float y, float gap, float estimate) const;
        /// The first index from `i` on that takes space (size() when none does): the end of a run of noSpace items.
        [[nodiscard]] std::size_t nextSpaced(std::size_t i) const;
        /// The last index before `i` that takes space, if any.
        [[nodiscard]] std::optional<std::size_t> previousSpaced(std::size_t i) const;

    private:
        struct Node {
            double heights = 0.0;
            std::uint32_t measured = 0; // unsigned: a change's difference wraps, and the sums come out right
            std::uint32_t spaced = 0;
        };
        [[nodiscard]] Node entry(float h) const;
        [[nodiscard]] static double value(const Node& sum, float gap, float estimate);
        /// The sum of the indices before `i`.
        [[nodiscard]] Node prefix(std::size_t i) const;
        /// The largest i (up to size()) for which `fits(prefix(i))`, a predicate that holds up to some i.
        template <class Fits>
        [[nodiscard]] std::size_t descend(Fits fits) const;
        [[nodiscard]] std::size_t size() const;
        std::vector<Node> m_nodes; ///< 1-based (m_nodes[0] is unused): node j sums the indices (j - lowbit(j), j]
        float m_floor = 0.0f;
    };

    /// Sets the height of index `i`; returns whether it changed by more than half a pixel.
    bool setHeight(std::size_t i, float h);
    /// The space index `i` takes, its gap below included: its height (at least the tree's floor) and the gap, 0 for
    /// noSpace. `estimate` is floored already (see renderContent).
    [[nodiscard]] float step(std::size_t i, float gap, float estimate) const;
    /// Where index `i` starts, from the top of the content.
    [[nodiscard]] float top(std::size_t i, float gap, float estimate) const;
    /// When index `i` starts a run of more than longRunLength items known to take no space (before `to`): the
    /// run's end. Such a run is visited only in part on a frame (spacelessProbe of its items, see renderContent).
    [[nodiscard]] std::optional<std::size_t> longSpacelessRun(std::size_t i, std::size_t to) const;
    /// The end of the range from `i` (before `to`) that holds no long run of noSpace items: what can be asked of
    /// `__range` and rendered in one go.
    [[nodiscard]] std::size_t spanEnd(std::size_t i, std::size_t to) const;
    /// The part of the long run [s, e) visited on this frame: spacelessProbe items, from an offset that moves on
    /// every frame that visits one, so every item of the run is checked again over a few frames.
    [[nodiscard]] std::pair<std::size_t, std::size_t> spacelessWindowOf(std::size_t s, std::size_t e) const;
    /// The height of an item that wasn't measured: the measured ones' average, else a list item's.
    [[nodiscard]] float estimate(const Frame& frame) const;
    /// The scroll that puts index `i` where `mode` says in a view `viewHeight` tall scrolled to `scrollY`.
    [[nodiscard]] float positionScroll(std::size_t i, Position mode, float scrollY, float viewHeight, float gap,
                                       float estimate, std::optional<float> fixed) const;
    /// Sets firstVisibleIndex, and tells Lua when it changed.
    void publishFirstVisible(std::int64_t index);
    /// Scales the measured heights by `ratio` (a new DPI scale: a zoom, another monitor), so they stay what the items
    /// measure until they are measured again.
    void rescaleHeights(double ratio);

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
    int m_cacheSize = defaultCacheSize;
    bool m_following = true;    ///< the view was at the end last frame (it starts there)
    float m_viewHeight = -1.0f; ///< the visible content height last frame (pixels)
    float m_followTarget = -1.0f; ///< the scroll followBottom asked for last frame (-1: none), and the scroll then
    float m_followScroll = -1.0f;
    std::int64_t m_firstVisible = 0;
    std::optional<PendingPosition> m_position;
    int m_steerFrame = -1; ///< ImGui frame of the last render that scrolled for the above (the first one of a frame)
    /// Without a delegate: where the item a position is for was placed last frame (top, height), to tell it landed.
    std::optional<std::pair<float, float>> m_ownPlaced;

    /// The C++ list model the last render showed (its objectId(), 0 for none) and its revision then: the heights
    /// follow its rows (followRows).
    std::uint64_t m_modelId = 0;
    std::uint64_t m_modelRevision = 0;

    std::vector<float> m_heights; ///< per model index (0-based), when `itemHeight` is unset
    float m_scale = 0.0f;         ///< dpiScale() m_heights were measured at (0: none yet)
    bool m_tooMany = false;       ///< a model longer than maxItems was reported
    double m_measuredSum = 0.0;   ///< the measured heights' sum
    std::size_t m_measured = 0;   ///< how many are measured
    std::size_t m_spaceless = 0;  ///< how many take no space
    StepTree m_steps;             ///< m_heights' prefix sums

    /// A run of more items than this known to take no space (delegates that returned nil, hidden elements: a filter)
    /// is not visited in full on every frame, which would cost as much as the run is long: spacelessProbe of its
    /// items are (asked of `__range` and checked whether they take space now), at an offset (m_spacelessOffset) that
    /// moves by as much on each frame that probes one. A shorter run is visited in full, as every other item in range.
    /// The probe is small: the items it visits are new to the Lua side's delegate cache on most frames, so built.
    static constexpr std::size_t longRunLength = 256;
    static constexpr std::size_t spacelessProbe = 32;
    std::size_t m_spacelessOffset = 0;

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

/// What `__count` returned: the model's length, and the model when it is a C++ list model.
struct Counted {
    std::size_t n = 0;
    const ListModel* model = nullptr;
};

/// Calls `__count(self, cacheSize)`: the model's length (resolving it for the __range calls of this render), and the
/// model when it is a C++ list model. nullopt when it failed (reported) or the element has no `__count`.
std::optional<Counted> callCount(const luaui::Node& node, int cacheSize) {
    lua::State* state = node.state();
    if (node.pushRaw("__count") != lua::Type::Function) {
        lua::pop(state);
        return std::nullopt;
    }
    lua::pushvalue(state, node.index());
    lua::pushinteger(state, cacheSize);
    if (!luaui::safeRead(state, 2, 2)) return std::nullopt;
    const lua::Integer n = lua::isinteger(state, -2) ? lua::tointeger(state, -2) : 0;
    const ListModel* model = ListModel::fromLua(state, -1);
    lua::pop(state, 2);
    return Counted{static_cast<std::size_t>(std::max<lua::Integer>(0, n)), model};
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
             "back to the end starts it again. A list starts at the end. Works with its own children too.")
        .property("cacheSize", &ListView::cacheSize, &ListView::setCacheSize)
        .doc("How many built delegates are kept (256): at most max(`cacheSize`, 4 x the items shown), the ones shown "
             "longest ago dropped first. A delegate dropped is built again when it comes back into view.")
        .property("firstVisibleIndex", &ListView::firstVisibleIndex)
        .doc("Read-only: the model index (1-based) of the first item at least partly in view when the list last "
             "rendered; 0 before that, with an empty model or without a `delegate`.")
        .method("positionViewAtIndex", &ListView::positionViewAtIndex, {"index", "mode"})
        .doc("Scrolls so that item `index` (1-based; clamped to 1..the model's length) is placed by `mode`: "
             "`\"beginning\"` (default: its top at the view's top), `\"center\"`, `\"end\"` (its bottom at the view's "
             "bottom) or `\"contain\"` (the least scroll that shows all of it). Works for items never built: it "
             "scrolls by the heights known (`itemHeight`, else the measured ones and their average), and corrects "
             "on the next frames as the items there are measured. Takes effect at the next render; nothing with an "
             "empty model. Without a `delegate`, `index` counts its visible children (an inline container's spliced "
             "in).")
        .method("positionViewAtBeginning", &ListView::positionViewAtBeginning)
        .doc("Scrolls to the top: `positionViewAtIndex(1, \"beginning\")`.")
        .method("positionViewAtEnd", &ListView::positionViewAtEnd)
        .doc("Scrolls to the end: the last item's bottom at the view's bottom.")
        .docMember("spacing", "The gap between items (dp). Unset: ImGui's item spacing. A negative one overlaps "
                              "the items, each still at least a pixel below the one before.");
}

std::optional<float> ListView::itemHeight() const { return m_itemHeight; }
void ListView::setItemHeight(std::optional<float> itemHeight) {
    if (itemHeight && !(*itemHeight > 0.0f)) itemHeight.reset(); // 0, negative or NaN: measured
    m_itemHeight = itemHeight;
}
float ListView::contentHeight() const { return m_contentHeight; }
std::pair<std::int64_t, std::int64_t> ListView::renderedRange() const { return {m_first, m_last}; }
std::optional<float> ListView::measuredHeight(std::int64_t index) const {
    if (m_itemHeight || index < 1 || static_cast<std::uint64_t>(index) > m_heights.size()) return std::nullopt;
    const float h = m_heights[static_cast<std::size_t>(index - 1)];
    return h >= 0.0f ? std::optional<float>{h} : std::nullopt;
}
bool ListView::followBottom() const { return m_followBottom; }
void ListView::setFollowBottom(bool followBottom) { m_followBottom = followBottom; }
int ListView::cacheSize() const { return m_cacheSize; }
void ListView::setCacheSize(int cacheSize) { m_cacheSize = std::max(0, cacheSize); }
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

void ListView::rescaleHeights(double ratio) {
    for (float& h : m_heights)
        if (h >= 0.0f) h = static_cast<float>(static_cast<double>(h) * ratio); // unmeasured, noSpace: at any scale
    recount();
}

void ListView::recount() {
    m_measuredSum = 0.0;
    m_measured = 0;
    m_spaceless = 0;
    for (const float h : m_heights) {
        if (h >= 0.0f) {
            m_measuredSum += static_cast<double>(h);
            ++m_measured;
        } else if (h == noSpace) {
            ++m_spaceless;
        }
    }
    const float floor = m_steps.floor();
    m_steps = StepTree{};
    m_steps.setFloor(floor, {});
    m_steps.assign(m_heights);
}

void ListView::followRows(const ListModel& model) {
    const std::optional<std::span<const ListModel::Change>> changes = model.changesSince(m_modelRevision);
    if (!changes) return; // too long ago: the heights stay per index
    using Kind = ListModel::Change::Kind;
    // Only appends (a log) and changed rows (whose heights stay the best guess until they render again): the length
    // check in renderContent appends them, with no rebuild.
    std::size_t length = m_heights.size();
    const bool appendsOnly = std::ranges::all_of(*changes, [&](const ListModel::Change& c) {
        if (c.kind == Kind::Changed) return true;
        if (c.kind != Kind::Inserted || c.first != length) return false;
        length += c.count;
        return true;
    });
    if (appendsOnly) return;
    // Else on a copy, kept only when every change applies: notifications that don't match what was shown leave the
    // heights per index.
    std::vector<float> h = m_heights;
    const auto at = [&h](std::size_t i) { return h.begin() + static_cast<std::ptrdiff_t>(i); };
    bool reset = false; // everything is new: no height is known
    // The pinned items follow their rows too; one whose row went is dropped.
    std::vector<std::optional<std::size_t>> pins(m_pinned.begin(), m_pinned.end());
    const auto shiftPins = [&](auto&& to) {
        for (std::optional<std::size_t>& p : pins)
            if (p) p = to(*p);
    };
    for (const ListModel::Change& c : *changes) {
        if (reset) break;
        const std::size_t size = h.size();
        switch (c.kind) {
        case Kind::Inserted:
            if (c.first > size) return; // notifications that don't match what was shown: per index
            if (c.first < size) {
                h.insert(at(c.first), c.count, unmeasured);
                shiftPins([&](std::size_t i) -> std::optional<std::size_t> { return i < c.first ? i : i + c.count; });
            } else {
                h.resize(size + c.count, unmeasured);
            }
            break;
        case Kind::Removed:
            if (c.first > size || c.count > size - c.first) return;
            h.erase(at(c.first), at(c.first + c.count));
            shiftPins([&](std::size_t i) -> std::optional<std::size_t> {
                if (i < c.first) return i;
                if (i < c.first + c.count) return std::nullopt;
                return i - c.count;
            });
            break;
        case Kind::Changed: break;
        case Kind::Moved:
            if (c.count > size || c.first > size - c.count || c.destination > size - c.count) return;
            if (c.destination < c.first)
                std::rotate(at(c.destination), at(c.first), at(c.first + c.count));
            else if (c.destination > c.first)
                std::rotate(at(c.first), at(c.first + c.count), at(c.destination + c.count));
            shiftPins([&](std::size_t i) -> std::optional<std::size_t> {
                // Where old index i is after the move (as std::rotate moved it).
                if (i >= c.first && i < c.first + c.count) return c.destination + (i - c.first);
                if (c.destination < c.first && i >= c.destination && i < c.first) return i + c.count;
                if (c.destination > c.first && i >= c.first + c.count && i < c.destination + c.count)
                    return i - c.count;
                return i;
            });
            break;
        case Kind::Remapped: {
            if (!c.previous) { // an older sort: its mapping isn't kept
                reset = true;
                break;
            }
            const std::vector<std::optional<std::size_t>>& previous = *c.previous;
            std::vector<float> next(previous.size(), unmeasured);
            std::vector<bool> taken(size, false);
            std::vector<std::optional<std::size_t>> newIndex(size);
            for (std::size_t r = 0; r < previous.size(); ++r) {
                const std::optional<std::size_t> p = previous[r];
                if (p && *p < size && !taken[*p]) {
                    taken[*p] = true;
                    next[r] = h[*p];
                    newIndex[*p] = r;
                }
            }
            h = std::move(next);
            shiftPins([&](std::size_t i) { return i < newIndex.size() ? newIndex[i] : std::nullopt; });
            break;
        }
        case Kind::Reset: reset = true; break;
        }
    }
    if (reset) {
        h.assign(model.count(), unmeasured);
        pins.clear();
    }
    m_heights = std::move(h);
    m_pinned.clear();
    for (const std::optional<std::size_t>& p : pins)
        if (p) m_pinned.push_back(*p);
    recount();
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
    m_steps.update(i, old, h);
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
    if (h >= 0.0f) return std::max(h, m_steps.floor()) + gap;
    return h == noSpace ? 0.0f : estimate + gap;
}

float ListView::top(std::size_t i, float gap, float estimate) const { return m_steps.top(i, gap, estimate); }

std::optional<std::size_t> ListView::longSpacelessRun(std::size_t i, std::size_t to) const {
    if (i >= to || m_heights[i] != noSpace) return std::nullopt;
    const std::size_t e = std::min(to, m_steps.nextSpaced(i));
    if (e - i <= longRunLength) return std::nullopt;
    return e;
}

std::size_t ListView::spanEnd(std::size_t i, std::size_t to) const {
    // Over the items that take space one by one, over a run of noSpace ones at once (the tree finds its end): the
    // work is the items in range that take space, not the ones that don't.
    std::size_t k = i;
    while (k < to) {
        if (m_heights[k] != noSpace) {
            ++k;
            continue;
        }
        const std::size_t e = std::min(to, m_steps.nextSpaced(k));
        if (e - k > longRunLength) return k;
        k = e;
    }
    return to;
}

std::pair<std::size_t, std::size_t> ListView::spacelessWindowOf(std::size_t s, std::size_t e) const {
    const std::size_t from = s + m_spacelessOffset % (e - s);
    return {from, std::min(e, from + spacelessProbe)};
}

// ------------------------------------------------------------ ListView::StepTree

namespace {

constexpr std::size_t lowbit(std::size_t j) { return j & (~j + 1); }

} // namespace

ListView::StepTree::Node ListView::StepTree::entry(float h) const {
    if (h >= 0.0f) return {static_cast<double>(std::max(h, m_floor)), 1, 1};
    return h == noSpace ? Node{} : Node{0.0, 0, 1};
}

void ListView::StepTree::setFloor(float floor, const std::vector<float>& heights) {
    if (floor == m_floor) return;
    m_floor = floor;
    m_nodes.clear();
    assign(heights);
}

double ListView::StepTree::value(const Node& sum, float gap, float estimate) {
    return sum.heights + static_cast<double>(sum.spaced - sum.measured) * static_cast<double>(estimate) +
           static_cast<double>(sum.spaced) * static_cast<double>(gap);
}

std::size_t ListView::StepTree::size() const { return m_nodes.empty() ? 0 : m_nodes.size() - 1; }

void ListView::StepTree::assign(const std::vector<float>& heights) {
    const std::size_t old = size();
    const std::size_t n = heights.size();
    if (n < old) {
        truncate(n);
        return;
    }
    m_nodes.resize(n + 1);
    if (n - old > old) {
        // Mostly new: build it whole. Each node adds itself to its parent, which covers it.
        std::ranges::fill(m_nodes, Node{});
        for (std::size_t j = 1; j <= n; ++j) {
            const Node e = entry(heights[j - 1]);
            m_nodes[j].heights += e.heights;
            m_nodes[j].measured += e.measured;
            m_nodes[j].spaced += e.spaced;
            if (const std::size_t parent = j + lowbit(j); parent <= n) {
                m_nodes[parent].heights += m_nodes[j].heights;
                m_nodes[parent].measured += m_nodes[j].measured;
                m_nodes[parent].spaced += m_nodes[j].spaced;
            }
        }
        return;
    }
    // A few appended: a new node j covers (j - lowbit(j), j], its own entry and the nodes that cover the rest, all
    // before it, so already there.
    for (std::size_t j = old + 1; j <= n; ++j) {
        Node node = entry(heights[j - 1]);
        for (std::size_t k = j - 1; k > j - lowbit(j); k -= lowbit(k)) {
            node.heights += m_nodes[k].heights;
            node.measured += m_nodes[k].measured;
            node.spaced += m_nodes[k].spaced;
        }
        m_nodes[j] = node;
    }
}

// A node only covers indices before it, so the first n nodes are the tree of the first n indices.
void ListView::StepTree::truncate(std::size_t n) {
    if (n < size()) m_nodes.resize(n + 1);
}

void ListView::StepTree::update(std::size_t i, float old, float h) {
    const Node from = entry(old), to = entry(h);
    const double heights = to.heights - from.heights;
    const std::uint32_t measured = to.measured - from.measured, spaced = to.spaced - from.spaced; // may wrap
    if (heights == 0.0 && measured == 0 && spaced == 0) return;
    for (std::size_t j = i + 1; j < m_nodes.size(); j += lowbit(j)) {
        m_nodes[j].heights += heights;
        m_nodes[j].measured += measured;
        m_nodes[j].spaced += spaced;
    }
}

ListView::StepTree::Node ListView::StepTree::prefix(std::size_t i) const {
    Node sum;
    for (std::size_t j = std::min(i, size()); j > 0; j -= lowbit(j)) {
        sum.heights += m_nodes[j].heights;
        sum.measured += m_nodes[j].measured;
        sum.spaced += m_nodes[j].spaced;
    }
    return sum;
}

template <class Fits>
std::size_t ListView::StepTree::descend(Fits fits) const {
    // From the top bit down: a node is taken when the prefix that ends with it still fits, so the prefixes taken
    // grow, and end at the largest one that fits (the predicate holds up to some i and not after).
    const std::size_t n = size();
    std::size_t pos = 0;
    Node acc;
    for (std::size_t bit = std::bit_floor(std::max<std::size_t>(n, 1)); bit > 0; bit >>= 1) {
        const std::size_t next = pos + bit;
        if (next > n) continue;
        const Node sum{acc.heights + m_nodes[next].heights, acc.measured + m_nodes[next].measured,
                       acc.spaced + m_nodes[next].spaced};
        if (!fits(sum)) continue;
        pos = next;
        acc = sum;
    }
    return pos;
}

float ListView::StepTree::top(std::size_t i, float gap, float estimate) const {
    return static_cast<float>(value(prefix(i), gap, estimate));
}

std::size_t ListView::StepTree::lastStartingBy(float y, float gap, float estimate) const {
    const double limit = static_cast<double>(y);
    return descend([&](const Node& sum) { return value(sum, gap, estimate) <= limit; });
}

std::size_t ListView::StepTree::nextSpaced(std::size_t i) const {
    // The indices [i, p) take no space while the count of those that do stays at the count before i.
    const std::uint32_t before = prefix(i).spaced;
    return descend([&](const Node& sum) { return sum.spaced <= before; });
}

std::optional<std::size_t> ListView::StepTree::previousSpaced(std::size_t i) const {
    const std::uint32_t before = prefix(i).spaced;
    if (before == 0) return std::nullopt;
    // The last prefix with one spaced index fewer ends right before that index.
    return descend([&](const Node& sum) { return sum.spaced < before; });
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
    return placeScroll(t, h, mode, scrollY, viewHeight);
}

float ListView::placeScroll(float t, float h, Position mode, float scrollY, float viewHeight) {
    switch (mode) {
    case Position::Beginning: return t;
    case Position::Center: return t + (h - viewHeight) * 0.5f;
    case Position::End: return t + h - viewHeight;
    case Position::Contain: break;
    }
    // The least scroll that shows it: its bottom first, then its top, which wins for an item taller than the view.
    return std::min(std::max(scrollY, t + h - viewHeight), t);
}

void ListView::followEnd(Frame& frame, float maxScroll, float scrollY) {
    // The same target as last frame, which left the scroll where it was: ImGui's range ends short of it (content that
    // ends above the last item's bottom), and asking again would never settle.
    const bool stuck = std::abs(maxScroll - m_followTarget) <= 0.5f && std::abs(scrollY - m_followScroll) <= 0.5f;
    if (!stuck) {
        ImGui::SetScrollY(maxScroll);
        frame.requestFrame();
    }
    m_followTarget = maxScroll;
    m_followScroll = scrollY;
}

void ListView::renderOwnChildren(Frame& frame) {
    // The scroll state at the start, as renderContent reads it (see there).
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float scrollY = ImGui::GetScrollY();
    const float visibleHeight = std::max(0.0f, ImGui::GetContentRegionAvail().y);
    const bool restoring = restoringScroll();
    const int frameNo = ImGui::GetFrameCount();
    const bool steer = !restoring && frameNo != m_steerFrame;
    if (steer) m_steerFrame = frameNo;
    bool following = false;
    if (m_followBottom && steer) {
        following =
            scrollY >= ImGui::GetScrollMaxY() - 1.0f || (m_following && std::abs(visibleHeight - m_viewHeight) > 0.5f);
    }
    const float prevTotal = m_contentHeight;

    ScrollView::renderContent(frame);

    // The content's bottom as ImGui records it (CursorMaxPos: the cursor after the last item, less the item spacing).
    const float itemSpacing = ImGui::GetStyle().ItemSpacing.y;
    const float total = std::max(0.0f, ImGui::GetCursorScreenPos().y - itemSpacing - origin.y);
    m_contentHeight = total;
    m_first = 0;
    m_last = -1;
    const float maxScroll = std::max(0.0f, std::trunc(total) - visibleHeight);
    bool positioned = false;
    if (m_position && steer) {
        // Where each visible visual child was placed: its size in the layout cache (the average of the known ones for a
        // child never rendered), stacked as Renderer::renderStack stacks them, ImGui's whole-pixel line advance
        // included. Only on the frames a position is pending.
        std::vector<float> heights;
        float known = 0.0f;
        int knownCount = 0;
        frame.forEachChild([&](Frame::Child& child) {
            if (!child.isVisual()) return;
            const std::optional<ImVec2> size = layout::lastSize(child.id());
            heights.push_back(size ? size->y : -1.0f);
            if (size) {
                known += size->y;
                ++knownCount;
            }
        });
        if (heights.empty()) {
            m_position.reset();
            m_ownPlaced.reset();
        } else {
            const float average = knownCount > 0 ? known / static_cast<float>(knownCount) : 0.0f;
            const float extra = spacing() ? frame.dp(static_cast<float>(*spacing())) - itemSpacing : 0.0f;
            const auto index = static_cast<std::size_t>(
                std::clamp<std::int64_t>(m_position->index, 1, static_cast<std::int64_t>(heights.size())));
            float y = origin.y, t = 0.0f, h = 0.0f;
            for (std::size_t k = 0; k < index; ++k) {
                if (k > 0) y += extra;
                h = heights[k] >= 0.0f ? heights[k] : average;
                t = y - origin.y;
                y = std::trunc(y + h + itemSpacing);
            }
            const float target =
                std::clamp(std::round(placeScroll(t, h, m_position->mode, scrollY, visibleHeight)), 0.0f, maxScroll);
            const bool same =
                m_ownPlaced && std::abs(m_ownPlaced->first - t) <= 0.5f && std::abs(m_ownPlaced->second - h) <= 0.5f;
            const bool landed = same && std::abs(total - prevTotal) <= 0.5f &&
                                std::abs(scrollY - std::min(target, ImGui::GetScrollMaxY())) <= 1.0f;
            m_ownPlaced = std::pair{t, h};
            if (landed || m_position->frames >= maxPositionFrames) {
                m_position.reset();
                m_ownPlaced.reset();
            } else {
                ++m_position->frames;
                ImGui::SetScrollY(target);
                frame.requestFrame();
                positioned = true;
            }
        }
    }
    if (!positioned && following && std::abs(maxScroll - scrollY) > 0.5f)
        followEnd(frame, maxScroll, scrollY);
    else if (!positioned)
        m_followTarget = -1.0f;
    if (m_followBottom && steer) {
        m_following = following;
        m_viewHeight = visibleHeight;
    }
    if (steer) publishFirstVisible(0);
}

void ListView::renderContent(Frame& frame) {
    const luaui::Node& node = frame.node();
    lua::State* state = node.state();
    const int stackTop = lua::gettop(state);
    lua::checkstack(state, 24);
    const std::optional<Counted> count = callCount(node, m_cacheSize);
    if (!count) {
        // No delegate: the ListView's own children, like a ScrollView.
        m_modelId = 0;
        renderOwnChildren(frame);
        return;
    }
    std::size_t n = count->n;
    if (n > maxItems) {
        if (!m_tooMany)
            luaui::reportError("ListView: the model has more than " + std::to_string(maxItems) +
                               " items; only the first " + std::to_string(maxItems) + " are shown");
        m_tooMany = true;
        n = maxItems;
    }

    const float gap = spacing() ? frame.dp(static_cast<float>(*spacing())) : ImGui::GetStyle().ItemSpacing.y;
    const std::optional<float> fixed = m_itemHeight ? std::optional<float>{frame.dp(*m_itemHeight)} : std::nullopt;
    // The distance from one fixed-height item's top to the next one's. At least a pixel: a negative `spacing` as large
    // as the height would put every item at one place, so no number of them would fill the view (and the index in
    // view, the scroll divided by it, would divide by zero).
    const float fixedStep = fixed ? std::max(1.0f, *fixed + gap) : 0.0f;
    // The heights are pixels: at another DPI scale (a zoom, another monitor) they are scaled with it, until the items
    // are measured again.
    const float scale = frame.dp(1.0f);
    if (m_scale > 0.0f && scale != m_scale && m_measured > 0) {
        rescaleHeights(static_cast<double>(scale) / static_cast<double>(m_scale));
        frame.requestFrame();
    }
    m_scale = scale;
    // A C++ list model: the heights follow its rows as they move (inserted above the view, sorted).
    if (const ListModel* model = count->model) {
        if (model->objectId() == m_modelId && model->revision() != m_modelRevision && !fixed)
            followRows(*model);
        else if (model->objectId() != m_modelId)
            (void)model->changesSince(model->revision()); // starts its change log
        m_modelId = model->objectId();
        m_modelRevision = model->revision();
    } else {
        m_modelId = 0;
    }
    // A negative gap as large as an item's height would put items at one place (or move up), so that no number of them
    // fills the view: each step is at least a pixel, as with `itemHeight`.
    if (!fixed) m_steps.setFloor(gap < 0.0f ? 1.0f - gap : 0.0f, m_heights);
    if (!fixed && m_heights.size() != n) {
        // The model changed length (an insertion, a removal, a reset): keep the heights of the indices that are still
        // there. The tree drops the ones past the end as they are, or appends the new ones.
        for (std::size_t i = n; i < m_heights.size(); ++i) {
            const float h = m_heights[i];
            if (h >= 0.0f) {
                m_measuredSum -= static_cast<double>(h);
                --m_measured;
            } else if (h == noSpace) {
                --m_spaceless;
            }
        }
        m_heights.resize(n, unmeasured);
        m_steps.assign(m_heights);
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

    // Horizontal placement: the stack's `align`, read and applied as Frame::renderStack does it.
    const float align = alignFactor(Renderer::stackAlign(frame.node()));
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

    // Renders the delegate at the top of the stack (an element, or false) at screen y `y`. Returns its height, or
    // noSpace when it took none. An inline delegate's children (a Repeater's, an If's) stack with the gap between
    // them, and together they are the item: spliced like the renderer splices an inline child.
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
        lua::getfield(state, item, "__type");
        const bool element = lua::isstring(state, -1);
        lua::pop(state);
        const luaui::Node itemNode{state, item};
        if (!element || !shown(itemNode)) return noSpace;
        lua::rawgetfield(state, item, "__inline"); // raw, like the renderer: only the table's own marker counts
        const bool isInline = lua::toboolean(state, -1);
        lua::pop(state);
        if (isInline) {
            frame.forEachChild(itemNode, one);
        } else {
            Frame::Child child{frame, itemNode};
            one(child);
        }
        return spaced ? std::max(0.0f, bottom - y) : noSpace;
    };
    // renderItem() for index `i`, remembering it when it holds what must keep rendering out of view (the culling
    // rules: the active item, the nav focus, an open popup, a non-visual element). On a whole pixel: ImGui truncates
    // the cursor after each item, so an item's measured height (its group's bottom) depended on the fraction of the y
    // it was placed at, which follows the estimate; the estimate moved with the heights measured, and the two could
    // keep each other changing for frames (a list that follows its bottom lost it). Whole pixels measure the same.
    std::vector<std::size_t> pinned;
    const auto renderIndex = [&](std::size_t i, float y) -> float {
        const cull::Watch before = cull::watch();
        const float h = renderItem(std::floor(y));
        if (cull::held(before) && pinned.size() < maxPinned) pinned.push_back(i);
        return h;
    };

    // The estimate, floored like a measured height (see the step tree's floor).
    const float est = fixed ? *fixed : std::max(estimate(frame), m_steps.floor());
    const auto stepOf = [&](std::size_t i) { return fixed ? fixedStep : step(i, gap, est); };
    bool changed = false;
    bool windowed = false; // a long run of noSpace items was visited only in part (spacelessProbe)
    // A delegate's handler that shrank the model while this render runs (model:clear()) makes `__range` return fewer
    // delegates than asked for: the model ends there, and the render stops (the next one counts it again).
    bool shrank = false;
    const auto pushSpan = [&](std::size_t from, std::size_t& to) {
        if (!pushRange(node, from + 1, to)) return false;
        const auto got = static_cast<std::size_t>(std::max<lua::Integer>(0, lua::rawlen(state, -1)));
        if (got < to - from) {
            to = from + got;
            shrank = true;
        }
        return true;
    };
    std::size_t first = 0, anchor = 0, end = 0; // rendered: [first, end); anchor: the first item in view
    float anchorTop = 0.0f;
    if (n > 0) {
        // The first item in view, and the margin above it. Measured: the last index that starts by the view's top,
        // found in the step tree in O(log n) (its steps are >= 0: a negative gap is floored, see above).
        if (fixed) {
            anchor = std::min(n - 1, static_cast<std::size_t>(std::max(0.0f, viewTop) / fixedStep));
            anchorTop = static_cast<float>(anchor) * fixedStep;
        } else {
            anchor = std::min(n - 1, m_steps.lastStartingBy(viewTop, gap, est));
            anchorTop = top(anchor, gap, est);
        }
        // The margin, and more while a nav move scores items (cull::outOfView widens the view then): the item it
        // lands on must be submitted. A run of items that take no space adds nothing to it: stepped over at once.
        first = anchor;
        for (float above = 0.0f; first > 0;) {
            const float bottom = origin.y + anchorTop - above;
            if (above >= margin && cull::outOfView(bottom - stepOf(first - 1), bottom)) break;
            if (!fixed && m_heights[first - 1] == noSpace) {
                const std::optional<std::size_t> spaced = m_steps.previousSpaced(first);
                first = spaced ? *spaced + 1 : 0;
                continue;
            }
            above += stepOf(--first);
        }

        // Above the anchor: placed by the heights known so far (out of view, so a wrong estimate isn't seen), and
        // measured. In spans without long noSpace runs, and of each such run its window.
        if (first < anchor) {
            float y = fixed ? anchorTop - static_cast<float>(anchor - first) * fixedStep
                            : anchorTop - (top(anchor, gap, est) - top(first, gap, est));
            for (std::size_t i = first; i < anchor;) {
                std::size_t from = i, to = anchor, next = anchor;
                if (const std::optional<std::size_t> run = fixed ? std::nullopt : longSpacelessRun(i, anchor)) {
                    std::tie(from, to) = spacelessWindowOf(i, *run); // the run's others take no space: y stays
                    next = *run;
                    windowed = true;
                } else if (!fixed) {
                    to = next = spanEnd(i, anchor);
                }
                if (!pushSpan(from, to)) break;
                const int list = lua::gettop(state);
                for (std::size_t k = from; k < to; ++k) {
                    const float before = stepOf(k);
                    lua::rawgeti(state, list, static_cast<lua::Integer>(k - from + 1));
                    const float h = renderIndex(k, origin.y + y);
                    lua::settop(state, list);
                    if (!fixed) changed = setHeight(k, h) || changed;
                    y += before;
                }
                lua::settop(state, list - 1);
                if (shrank) break;
                i = next;
            }
        }

        // From the anchor down: one after the other, at the heights they render at, until past the view and the
        // margin. The delegates are asked for in chunks of the estimated number still needed.
        float y = origin.y + anchorTop;
        end = anchor;
        const auto more = [&] { return y < limit || !cull::outOfView(y, y + 1.0f); };
        while (end < n && !shrank && more()) {
            if (const std::optional<std::size_t> run = fixed ? std::nullopt : longSpacelessRun(end, n)) {
                // A long run of items that took no space: its window, and on after the run (or where the window's
                // items, taking space now, filled the view).
                auto [from, to] = spacelessWindowOf(end, *run);
                windowed = true;
                if (!pushSpan(from, to)) break;
                const int list = lua::gettop(state);
                std::size_t k = from;
                for (; k < to && more(); ++k) {
                    lua::rawgeti(state, list, static_cast<lua::Integer>(k - from + 1));
                    const float h = renderIndex(k, y);
                    lua::settop(state, list);
                    changed = setHeight(k, h) || changed;
                    y += step(k, gap, est);
                }
                lua::settop(state, list - 1);
                end = k < to || shrank ? k : *run;
                continue;
            }
            const float perItem = fixed ? fixedStep : std::max(1.0f, est + gap);
            // Past `limit` only while a nav move widens the view (see more()): by a view's height, so a page more.
            // The distance is positive either way; a negative one cast to size_t is undefined (in practice a huge
            // count that wraps, and a chunk that never advances).
            const float reach = y < limit ? limit - y : std::max(1.0f, viewHeight);
            const std::size_t want = static_cast<std::size_t>(std::ceil(reach / perItem)) + 1;
            // Up to a long noSpace run, which the loop takes next (above).
            std::size_t chunkEnd = fixed ? std::min(n, end + want) : spanEnd(end, std::min(n, end + want));
            if (!pushSpan(end, chunkEnd)) break;
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
            if (shrank || i >= n || (i >= first && i < end)) continue;
            std::size_t to = i + 1;
            if (!pushSpan(i, to)) continue;
            const int list = lua::gettop(state);
            if (to > i) {
                lua::rawgeti(state, list, 1);
                const float at = fixed ? static_cast<float>(i) * fixedStep : top(i, gap, est);
                const float h = renderIndex(i, origin.y + at);
                if (!fixed) changed = setHeight(i, h) || changed;
            }
            lua::settop(state, list - 1);
        }
    }
    lua::settop(state, stackTop);
    if (shrank) frame.requestFrame(); // to count the model again, and show what it holds now
    m_pinned = std::move(pinned);
    // The next frame visits the long runs' next windows: an item in one that takes space now (a hidden item shown, a
    // nil delegate rebuilt for a changed row) shows within the run's length / spacelessProbe rendered frames.
    if (windowed) m_spacelessOffset += spacelessProbe;

    // The content's height by what is known now, and the scroll moved by as much as the anchor moved: items above it
    // that were measured at another height than assumed (or the estimate that changed) would otherwise shift what
    // is in view on the next frame.
    const float est2 = fixed ? *fixed : std::max(estimate(frame), m_steps.floor());
    float total = 0.0f;
    if (fixed) {
        if (n > 0) total = std::max(0.0f, static_cast<float>(n - 1) * fixedStep + *fixed); // the last item's bottom
    } else {
        total = top(n, gap, est2);                             // every step: heights, estimates and gaps
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
        followEnd(frame, maxScroll, scrollY);
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
