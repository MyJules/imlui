// Element: base class of C++ element types registered with Engine::registerType<T>(name).
//
// One instance exists per element table (created by the Lua constructor, destroyed when Lua collects
// the table or the state closes). Before each render() the declared properties are synced from the
// element table into the instance through its setters; an element that isn't rendered is synced before a
// method called from Lua runs on it and when a parent asks for it (Frame::Child::as<T>()).
//
// An element can also ask for tick() at the start of every frame (setTicking(true)), whether or not it is
// rendered, e.g. a Timer on a hidden page. Its properties are synced before each tick(). The ticking list holds the
// elements by id (ScriptObject::objectId()) next to their address: a tick() may destroy other elements and create new
// ones (at a dead one's address), and a stale entry must not match them.
export module imlui.types.element;

import std;

import imgui;
import imlui.types.frame;
import imlui.types.script_object;
import imlui.ui.frame_request;

export namespace imlui {

class Element : public ScriptObject {
public:
    Element() = default;
    ~Element() override; ///< stops ticking

    /// Draws the element (Material widgets, the draw list) and its children (frame.renderChildren()).
    /// Called on every rendered frame the element is visible, after the property sync.
    virtual void render(Frame& frame) = 0;

    /// The size render() will take when the width available at its cursor is `maxWidth`, known before rendering:
    /// layouts that place children by their sizes (Row, the stack `align`, TopAppBar actions) use it instead of the
    /// size of the last frame, so the element is in place from its first frame and on the frame it changes. It must
    /// match what render() then takes exactly (the group of what it draws), or the layout cache keeps asking for
    /// frames to settle: measure with the helper render() draws with (md::buttonSize for md::button, ...). A fill
    /// reports the width it takes in `maxWidth` (its parent passes the width it gives it: a Row a fill's share), and
    /// a container composes its children's sizes (Frame::stackSize). nullopt (the default): unknown, the layout
    /// uses the last frame's size. An element whose width depends on `maxWidth` must fill (TypeBuilder::fillsWidth,
    /// or contentFillsWidth()), so that a Row gives it a share and asks with that. Its properties are synced;
    /// `frame` is the element's own. Computed once a frame per `maxWidth` (TypeRegistry memoizes it).
    [[nodiscard]] virtual std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth);

    /// Whether the element's width follows the available width because of what it holds, beyond its type's size
    /// policy (TypeBuilder::fillsWidth): a Column that holds a fill, a Row that has fills or justifies its
    /// children. Layouts treat it like a fill (a Row gives it a share of the free width). Not consulted when a
    /// `width` is set. Default false. Computed once a frame (TypeRegistry memoizes it).
    [[nodiscard]] virtual bool contentFillsWidth(const Frame& frame);

    /// Called once, right after the first property sync (the first render, method call from Lua or parent
    /// access, whichever comes first), so before the first render().
    virtual void componentComplete();

    /// Called right before the instance is deleted: its element table was collected, or the Lua state
    /// is closing (hot reload, shutdown). The Lua link is already gone, so notify()/emit() do nothing.
    virtual void destroyed();

    /// While ticking is on (setTicking(true)): called at the start of every rendered frame, before the tree
    /// renders, whether or not the element is rendered, with its properties synced first. `now` is the
    /// app clock's time in seconds (clockTime(), module imlui.ui.clock, what ui.time() returns). Frames are rendered on demand: request the frame you need (requestFrameAfter).
    virtual void tick(double now);

    /// Whether tick() is called every frame.
    [[nodiscard]] bool isTicking() const;

    /// The ids (objectId()) of the ticking elements, in the order they started ticking (TypeRegistry runs them).
    [[nodiscard]] static std::vector<std::uint64_t> tickingElements();
    /// tickingElements() into `out` (cleared first), so a loop that runs every frame reuses its buffer.
    static void tickingElements(std::vector<std::uint64_t>& out);
    /// The ticking element with id `id`, or nullptr when it stopped ticking or died since the id was taken. Ids are
    /// never reused, so an element created later at a dead one's address can't answer for it. O(1).
    [[nodiscard]] static Element* findTicking(std::uint64_t id);

protected:
    /// Turns tick() on or off. The destructor turns it off. Turning it on requests a frame, so the first tick()
    /// comes on the next frame also when the element was created while the app sleeps or during a render.
    void setTicking(bool on);
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

// The ticking list, main thread only (like everything that touches Lua). Looked up by id on every tick (findTicking
// runs twice per element and frame) and appended to in start order, so it is two structures:
//  * `live`: id -> (element, sequence number), the elements ticking now;
//  * `order`: (id, sequence number) in the order they started, with stale entries left in place by a stop.
// An entry of `order` is live while `live` holds its id with the same sequence number: an element that stops and
// starts again gets a new number (and a new entry at the end), so its old entry stays stale. Stopping is O(1); the
// stale entries are swept when they outnumber the live ones (amortized O(1) per stop).
struct Ticking {
    Element* element;
    std::uint64_t sequence;
};
struct Entry {
    std::uint64_t id;
    std::uint64_t sequence;
};

std::unordered_map<std::uint64_t, Ticking> live;
std::vector<Entry> order;
std::uint64_t nextSequence = 0;

bool isLive(const Entry& e) {
    const auto it = live.find(e.id);
    return it != live.end() && it->second.sequence == e.sequence;
}

/// Drops the stale entries of `order` once they are more than the live ones (and more than a few).
void sweep() {
    if (order.size() < 16 || order.size() - live.size() <= live.size()) return;
    std::erase_if(order, [](const Entry& e) { return !isLive(e); });
}

} // namespace

Element::~Element() { setTicking(false); }

std::optional<ImVec2> Element::intrinsicSize(const Frame&, float) { return std::nullopt; }
bool Element::contentFillsWidth(const Frame&) { return false; }
void Element::componentComplete() {}
void Element::destroyed() {}
void Element::tick(double) {}

bool Element::isTicking() const { return live.contains(objectId()); }

std::vector<std::uint64_t> Element::tickingElements() {
    std::vector<std::uint64_t> ids;
    tickingElements(ids);
    return ids;
}

void Element::tickingElements(std::vector<std::uint64_t>& out) {
    out.clear();
    out.reserve(live.size());
    for (const Entry& e : order)
        if (isLive(e)) out.push_back(e.id);
}

Element* Element::findTicking(std::uint64_t id) {
    const auto it = live.find(id);
    return it == live.end() ? nullptr : it->second.element;
}

void Element::setTicking(bool on) {
    const std::uint64_t id = objectId();
    if (on) {
        if (live.contains(id)) return;
        // `order` first: if it throws (out of memory), `live` has no entry without its place in the order.
        const std::uint64_t sequence = nextSequence++;
        order.push_back({id, sequence});
        live.emplace(id, Ticking{this, sequence});
        requestFrame(); // frames are on demand: nothing else would run the first tick()
    } else if (live.erase(id) != 0) {
        sweep();
    }
}

} // namespace imlui
