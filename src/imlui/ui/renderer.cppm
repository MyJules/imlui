// Renderer: walks the Lua element tree every frame and draws it with ImGui
// (through the Material widgets in imlui.widgets).
//
// Every element type ("Button", "Column", ...) is a typed Element registered with the TypeRegistry, which the
// Renderer reaches through the Renderer::Types interface (the registry implements it; this module can't import
// the registry, whose Frames render children through the Renderer). render() wraps each element (visible,
// enabled, layout group, tooltip, ID scope) and lets the registry sync and render its instance.
//
// An element's identity is its instance's id (ScriptObject::objectId(), through Types::elementId), not its table's
// address: Lua may collect a table and allocate a new one at the same address, which must start fresh instead of
// inheriting the dead element's ImGui state (an open tree, a scroll position, a text edit) or its size in the layout
// cache. The ImGui ID scope of an element and its layout cache key are both made from that id.
export module imlui.ui.renderer;

import std;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.widgets;
import imlui.image.image_cache;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.ui.culling;
import imlui.ui.layout_box;
import imlui.ui.layout_cache;
import imlui.ui.frame_request;
import imlui.ui.item_size;
import imlui.ui.props;

export namespace imlui {

class Renderer {
public:
    /// The element types the renderer dispatches to, by type name (`__type`). TypeRegistry implements it.
    class Types {
    public:
        /// Whether a type is registered under a name, and whether it takes layout space.
        enum class Kind { Unknown, Visual, NonVisual };

        /// What the element `node` is: by its instance's type once it has one (no `__type` read or name lookup),
        /// else by its `__type`.
        [[nodiscard]] virtual Kind kind(const luaui::Node& node) const = 0;
        /// Syncs and renders the element `node`, of a registered type. Runs inside the renderer's per-element
        /// wrapper.
        virtual void renderElement(Renderer& renderer, const luaui::Node& node) = 0;
        /// Whether the element `node` fills the available width when its `width` is unset: its type's own default
        /// (TypeBuilder::fillsWidth / fillsWidthWhen) or its content (Element::contentFillsWidth). Syncs the
        /// element's properties; computed once a frame.
        [[nodiscard]] virtual bool fillsWidth(Renderer& renderer, const luaui::Node& node) = 0;
        /// The element's intrinsic size with `maxWidth` available (Element::intrinsicSize): nullopt when its type
        /// can't tell before rendering. Syncs the element's properties; computed once a frame per `maxWidth`.
        [[nodiscard]] virtual std::optional<ImVec2> intrinsicSize(Renderer& renderer, const luaui::Node& node,
                                                                  float maxWidth) = 0;
        /// The element's identity: its instance's ScriptObject::objectId(), unique for the process's lifetime and
        /// never reused (created now if the table has none yet). 0 when it has no instance: an unregistered type,
        /// or a constructor that threw (reported).
        [[nodiscard]] virtual std::uint64_t elementId(const luaui::Node& node) = 0;

        virtual ~Types() = default;
    };

    /// `types` must outlive the renderer.
    explicit Renderer(Types& types);

    /// The deepest an element tree nests: render(), intrinsicSize(), fillsWidth() and the inline containers
    /// forEachChild() splices each count a level. A deeper tree is almost surely a cycle (`a[1] = b; b[1] = a`, a
    /// Loader that shows its own ancestor), which would overflow the native stack: the levels past it are skipped
    /// and reported as a script error.
    static constexpr int maxDepth = 256;

    /// Native window title: the root ApplicationWindow's `title` prop, else the
    /// last ui.setTitle(), else "imlui".
    [[nodiscard]] const std::string& windowTitle() const;
    void setWindowTitle(std::string title);

    /// The images that Image elements draw (decoded from the owner's resources); null when there are none.
    /// Set by the owner (the Engine, the test harness); it must outlive the renderer's use of it.
    [[nodiscard]] ImageCache* images() const;
    void setImages(ImageCache* images);

    /// Renders the element at stack index `idx`. Returns false if it was hidden (visible = false), of an unknown
    /// type ("unknown element type" is reported) or has no instance (its constructor threw). It renders in its own
    /// ImGui ID scope (pushElementId), and a visual element's size (its group's) is recorded in the layout cache
    /// (imlui.ui.layout_cache) under elementImGuiId(): the key Frame::Child::id() gives it.
    bool render(lua::State* state, int idx);

    /// The element's identity (Types::elementId): unique, never reused; 0 without an instance.
    [[nodiscard]] std::uint64_t elementId(const luaui::Node& node);
    /// The ImGui ID of the element in the current ID scope, made from elementId(): the ID of the scope render()
    /// pushes for it, and its key in the layout cache.
    [[nodiscard]] ImGuiID elementImGuiId(const luaui::Node& node);
    /// Pushes the element's ImGui ID scope, the one render() renders it in (pop with ImGui::PopID()): for a
    /// container that renders a child's children itself (TabBar renders the current Tab's content).
    void pushElementId(const luaui::Node& node);

    /// Calls f(stackIndex) for every child element. Children come from the
    /// element's array part, or from its `__children(self)` function
    /// (Repeater / ListView / If / Loader on the Lua side), called once a frame per element (pushChildren).
    /// Inline containers are spliced into the parent.
    template <class F>
    void forEachChild(const luaui::Node& node, F&& f) {
        lua::State* state = node.state();
        // Room for pushChildren() and a child: each inline container spliced in nests a level on the Lua stack.
        if (!lua::checkstack(state, 16) || !pushChildren(node)) return;
        const int src = lua::gettop(state);
        const lua::Integer len = lua::rawlen(state, src);
        for (lua::Integer i = 1; i <= len; ++i) {
            lua::rawgeti(state, src, i);
            if (lua::istable(state, -1)) {
                lua::getfield(state, -1, "__type");
                bool element = lua::isstring(state, -1);
                lua::pop(state);
                lua::rawget(state, -1, luaui::keys::inlined); // raw: only the table's own marker counts
                bool inl = lua::toboolean(state, -1);
                lua::pop(state);
                if (element && inl) {
                    luaui::Node c{state, lua::gettop(state)};
                    const Nesting level{*this};
                    if (level && c.boolean(luaui::keys::visible, true)) forEachChild(c, f);
                } else if (element) {
                    f(lua::gettop(state));
                }
            }
            lua::settop(state, src);
        }
        lua::settop(state, src - 1);
    }

    void renderChildren(const luaui::Node& node);

    /// True when the element at `idx` is of a registered visual type: false for the non-visual types (Timer,
    /// Dialog, ...) and for an unknown type, which render() only reports, so neither takes a layout slot.
    [[nodiscard]] bool isVisual(lua::State* state, int idx) const;
    /// Whether the element at `idx` fills the available width when its `width` is unset (Types::fillsWidth: its
    /// type's policy or its content).
    [[nodiscard]] bool fillsWidth(lua::State* state, int idx);
    /// The element's intrinsic size (Element::intrinsicSize) with `maxWidth` available, if its type knows it.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const luaui::Node& node, float maxWidth);
    /// Renders the element at `idx` like render(), measured but not seen (clipped away), and asks for another
    /// frame: for a layout that can't place it yet, on the first frame it has neither an intrinsic size nor a
    /// size in the layout cache, so it doesn't flash in the wrong place.
    bool renderUnseen(lua::State* state, int idx);
    /// render() of an element whose `visible` the caller has just read as true (Frame::Child): not read again.
    bool renderShown(lua::State* state, int idx);

    /// Renders children top-to-bottom. `spacing` (dp) overrides the gap
    /// between visible items; `align` positions them horizontally in the layout box (imlui.ui.layout_box), by their
    /// intrinsic widths or else last frame's (imlui.ui.layout_cache). Returns the right edge (screen x) of the widest
    /// visual child, or the stack's left edge without one: for a container that pads its content on the right.
    float renderStack(const luaui::Node& node);
    /// The size renderStack(node) takes with `maxWidth` available, composed from the visual children's intrinsic
    /// sizes (and ImGui's whole-pixel line advance); {0, 0} without visual children. nullopt when a child has none,
    /// or when an aligned stack is in an auto-sized window (it aligns in the widest child of the last frame).
    [[nodiscard]] std::optional<ImVec2> stackSize(const luaui::Node& node, float maxWidth);

    /// Whether the children of `node` that are out of view are skipped (imlui.ui.culling): culling is on
    /// (ui.setCulling) and `node`'s `cull` prop isn't false. renderStack() and Grid cull.
    [[nodiscard]] bool cullsChildren(const luaui::Node& node) const;
    /// Culls a visible visual child, `key` its elementImGuiId(): when it isn't pinned (cull::pinned) and its size in
    /// the layout cache puts it out of view at `at` (its x moved right by `(alignIn - width) * align` for a stack's
    /// `align`), lays out an item of that size there (a Dummy), as its group would, and returns where and
    /// how big. Else (no cached size yet, pinned, in view) returns nullopt and changes nothing. The cached size is only
    /// read (kept from decaying), never re-recorded or compared, so a culled child asks for no settle frames. The size
    /// is its settled one (layout::settledSize). A child below the view (cull::belowView) without one yet (its first
    /// frames, a Row placing children it measured the frame before, a width that changed with a scrollbar) renders
    /// while takeConvergeRenders() allows, so its size settles, and past that goes by its last size and asks for
    /// another frame: nothing in view is placed after it, and the scroll range converges over a few cheap frames
    /// instead of every child of a long page rendering on each of them. `belowOnly`: only a child below the view is
    /// culled (Culling::cullsBelow()).
    struct Skipped {
        ImVec2 at;   ///< where its size was laid out
        ImVec2 size; ///< the size from the layout cache
    };
    std::optional<Skipped> skipOutOfView(ImGuiID key, ImVec2 at, float alignIn = 0.0f, float align = 0.0f,
                                         bool belowOnly = false);

    /// How many children below the view without a settled size may still render on a frame (skipOutOfView, Grid's
    /// rows), so that their sizes settle and the scroll range becomes exact.
    static constexpr int convergeRenders = 256;
    /// Whether the subtree of `node` (its children, inline containers spliced in, recursively) holds a non-visual
    /// element (a Dialog, a Timer, a Window, ...) or one of an unknown type: a child that does must render at least
    /// once, so that it pins what it holds (and reports what it must), before it is laid out by an estimate. Reads the
    /// tables only: no sync, no render (a Repeater's delegates are built, as its render would).
    [[nodiscard]] bool holdsNonVisual(const luaui::Node& node);

    /// Notes that a child was laid out by a size that hasn't settled (below the view: an estimate, or a last size):
    /// the element (and the Grid row) that holds it records its own size as provisional (layout::recordSize), so it
    /// doesn't settle at a size made of estimates and stops being rendered before its content converged.
    void noteProvisional() { ++m_provisional; }
    /// How many noteProvisional() calls there were: compare a count taken before a render with one after.
    [[nodiscard]] std::uint64_t provisionals() const { return m_provisional; }

    /// Takes `n` of this frame's convergeRenders; false (nothing taken) when fewer are left.
    bool takeConvergeRenders(int n = 1);
    /// Consecutive frames requestConvergeFrame() asks for, at most: children whose sizes never settle (more than
    /// convergeRenders of them, changing on every frame) don't keep the app rendering. Past it, they settle on the
    /// frames rendered anyway.
    static constexpr int maxConvergeFrames = 120;
    /// Asks for another frame for the children below the view laid out by a size that hasn't settled yet (past
    /// this frame's convergeRenders), unless maxConvergeFrames frames in a row asked already.
    void requestConvergeFrame();

    /// A container's culling for one render of its children (renderStack, Grid). Whether it pins and culls, and a
    /// memo of its children by position from the last frame (their tables, ImGui IDs and whether they are visual),
    /// so a child out of view costs no type lookup or id: a long list walks its culled children cheaply. Visit the
    /// children in order with next(); the memo is kept per container (by an ImGui ID in its scope).
    class Culling {
    public:
        /// `width`: the width the container gives its children. Culled children keep the sizes they had at the
        /// width they last rendered in, so on a frame it differs from the last one's (a resize) nothing is culled.
        Culling(Renderer& renderer, const luaui::Node& node, float width);
        ~Culling();
        Culling(const Culling&) = delete;
        Culling& operator=(const Culling&) = delete;
        Culling(Culling&&) = delete;
        Culling& operator=(Culling&&) = delete;

        /// The container may cull: pins are recorded (also on a frame it doesn't cull, for the next).
        [[nodiscard]] bool pins() const { return m_pins; }
        /// It culls on this frame.
        [[nodiscard]] bool culls() const { return m_culls; }
        /// It culls the children below the view on this frame: whenever it culls, and also on its first frame and on
        /// a frame its width changed (a resize, a scrollbar that appeared), where only they may be culled: by their
        /// last sizes (at the old width) or, never measured, by an estimate, which place nothing in view.
        [[nodiscard]] bool cullsBelow() const { return m_cullsBelow; }
        /// Moves to the next child, at stack index `idx`; returns whether it is visual (isVisual()), from the memo
        /// when its table is the one at this position last frame.
        bool next(int idx);
        /// The current child's elementImGuiId(): from the memo when it may be culled (`fresh` false), else looked
        /// up (and remembered). A memo entry can only be stale for a table Lua collected and reallocated at the
        /// same address and position since the last frame: then a culled child is laid out at the dead one's size
        /// until it comes into view, where it renders and is looked up afresh.
        ImGuiID key(const luaui::Node& child, bool fresh);
        /// Whether the current child's subtree holds a non-visual element (Renderer::holdsNonVisual), remembered
        /// with its memo entry: walked once per child table.
        bool holdsNonVisual(const luaui::Node& child);

        /// A child as the memo remembers it.
        struct Child {
            const void* table = nullptr;
            ImGuiID key = 0;
            bool visual = false;
            std::int8_t nonVisual = -1; ///< holdsNonVisual(): -1 not walked yet, else 0 / 1
        };

    private:
        Renderer& m_renderer;
        lua::State* m_state;
        std::vector<Child>* m_children = nullptr; ///< the memo (null: not pinning, or a second render this frame)
        std::size_t m_index = 0;                  ///< children visited
        bool m_hit = false;                       ///< the current child matches its memo entry
        bool m_pins = false;
        bool m_culls = false;
        bool m_cullsBelow = false;
    };

    /// Pops up `node.menu` (a Menu element) anchored to the last item when
    /// `clicked`; renders it while open.
    void attachMenu(const luaui::Node& node, bool clicked);

private:
    /// Counts a level of nesting while it lives; false past maxDepth (reportTooDeep()).
    class Nesting {
    public:
        explicit Nesting(Renderer& renderer) : m_depth{renderer.m_depth}, m_ok{++m_depth <= maxDepth} {
            if (!m_ok) reportTooDeep();
        }
        ~Nesting() { --m_depth; }
        Nesting(const Nesting&) = delete;
        Nesting& operator=(const Nesting&) = delete;
        Nesting(Nesting&&) = delete;
        Nesting& operator=(Nesting&&) = delete;
        explicit operator bool() const { return m_ok; }

    private:
        int& m_depth;
        bool m_ok;
    };
    /// Reports a tree nested deeper than maxDepth (luaui::reportError, which reports a message once).
    static void reportTooDeep();

    /// Pushes the table that holds the children of `node`: the node itself, or what its `__children(self)` returned.
    /// That call (a read: the model function, the delegates it builds) runs once a frame per element while
    /// luaui::changes() stands still, so the passes of a layout (a Row's: which children fill, their sizes, the
    /// render; the fills and intrinsic sizes its parent asks about) share it; a handler that ran in between makes
    /// the next pass call it again. Returns false, with nothing pushed, when it failed (reported) or returned no
    /// table.
    bool pushChildren(const luaui::Node& node);
    /// render() from the element's ID scope on, for a known type.
    bool renderElement(const luaui::Node& node, Types::Kind kind);

    /// Culling's memo of a container's children, by an ImGui ID in the container's scope.
    struct CullMemo {
        float width = -1.0f;
        int frame = -1;
        std::vector<Culling::Child> children;
    };
    struct CullMemos {
        ImGuiContext* context = nullptr;
        int swept = 0;
        std::unordered_map<ImGuiID, CullMemo> byId;
    };

    Types& m_types;
    CullMemos m_cullMemos;
    std::string m_title{"imlui"};
    ImageCache* m_images = nullptr;
    int m_depth = 0;          ///< levels of nesting entered (Nesting)
    int m_convergeFrame = -1; ///< the frame m_convergeLeft is for
    int m_convergeLeft = 0;   ///< takeConvergeRenders() left on it
    std::uint64_t m_provisional = 0; ///< noteProvisional() calls
    int m_convergeAsked = -1; ///< the last frame requestConvergeFrame() ran on
    int m_convergeStreak = 0; ///< consecutive frames it ran on, up to that one
};

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

/// Its address is the registry key of the children cache (Renderer::pushChildren). Not const: identical const
/// objects may be folded into one address by the linker.
char childrenCacheKey = 0;

/// An element id as the "pointer" ImGui hashes for an ID (ImGui::GetID/PushID(const void*) hash the pointer's bytes,
/// unlike the string overloads, which treat "###" specially).

const void* idKey(std::uint64_t id) {
    static_assert(sizeof(void*) >= sizeof(std::uint64_t), "element ids are hashed as pointers");
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(id));
}

/// Restores the Material widgets' own stacks (md::pushSurface, md::beginCard) to their depth before an element
/// rendered, when an error unwinds its render: luaui::renderProtected recovers the ImGui stacks, but not these, and
/// a stale surface would color what renders after it (md::surfaceColor) for the rest of the frame.
class MaterialStacksGuard {
public:
    MaterialStacksGuard() = default;
    ~MaterialStacksGuard() {
        if (std::uncaught_exceptions() == m_exceptions) return;
        md::restoreSurfaceDepth(m_surfaces);
        md::restoreCardDepth(m_cards);
    }
    MaterialStacksGuard(const MaterialStacksGuard&) = delete;
    MaterialStacksGuard& operator=(const MaterialStacksGuard&) = delete;
    MaterialStacksGuard(MaterialStacksGuard&&) = delete;
    MaterialStacksGuard& operator=(MaterialStacksGuard&&) = delete;

private:
    std::size_t m_surfaces = md::surfaceDepth();
    std::size_t m_cards = md::cardDepth();
    int m_exceptions = std::uncaught_exceptions();
};

} // namespace

Renderer::Renderer(Types& types) : m_types{types} {}

const std::string& Renderer::windowTitle() const { return m_title; }
void Renderer::setWindowTitle(std::string title) { m_title = std::move(title); }
ImageCache* Renderer::images() const { return m_images; }
void Renderer::setImages(ImageCache* images) { m_images = images; }

bool Renderer::render(lua::State* state, int idx) {
    lua::checkstack(state, 32);
    const Node n{state, idx};
    const Types::Kind kind = m_types.kind(n);
    if (kind == Types::Kind::Unknown) {
        luaui::reportError("unknown element type '" + n.type() + "'");
        return false;
    }
    if (!n.boolean(luaui::keys::visible, true)) return false;
    return renderElement(n, kind);
}

bool Renderer::renderShown(lua::State* state, int idx) {
    lua::checkstack(state, 32);
    const Node n{state, idx};
    const Types::Kind kind = m_types.kind(n);
    if (kind == Types::Kind::Unknown) {
        luaui::reportError("unknown element type '" + n.type() + "'");
        return false;
    }
    return renderElement(n, kind);
}

void Renderer::reportTooDeep() {
    luaui::reportError("element tree nested deeper than " + std::to_string(maxDepth) +
                       " levels (does an element contain itself?); the deeper levels are skipped");
}

bool Renderer::renderElement(const Node& n, Types::Kind kind) {
    const Nesting level{*this};
    if (!level) return false;
    const std::uint64_t key = elementId(n);
    if (key == 0) return false; // no instance: its constructor threw, which was reported
    const ImGuiID id = ImGui::GetID(idKey(key));
    const MaterialStacksGuard stacks;
    ImGui::PushID(idKey(key));
    const bool disabled = n.has(luaui::keys::enabled) && !n.boolean(luaui::keys::enabled, true);
    if (disabled) ImGui::BeginDisabled();
    // Every visual element is exactly one layout item (a group), so
    // composite elements (label + field, ...) behave inside Rows.
    const bool visual = kind == Types::Kind::Visual;
    const std::uint64_t provisional = m_provisional;
    if (visual)
        ImGui::BeginGroup();
    else
        cull::noteNonVisual(); // it has no rect in the flow: its ancestors must not be culled
    m_types.renderElement(*this, n);
    if (visual) {
        ImGui::EndGroup();
        layout::recordSize(id, ImGui::GetItemRectSize(), m_provisional != provisional);
    }
    if (n.has(luaui::keys::tooltip)) md::tooltip(n.string("tooltip"));
    if (disabled) ImGui::EndDisabled();
    ImGui::PopID();
    return true;
}

std::uint64_t Renderer::elementId(const Node& node) { return m_types.elementId(node); }
ImGuiID Renderer::elementImGuiId(const Node& node) { return ImGui::GetID(idKey(elementId(node))); }
void Renderer::pushElementId(const Node& node) { ImGui::PushID(idKey(elementId(node))); }

bool Renderer::pushChildren(const Node& node) {
    lua::State* state = node.state();
    if (node.pushRaw(luaui::keys::children) != lua::Type::Function) {
        lua::pop(state);
        lua::pushvalue(state, node.index());
        return true;
    }
    const int fn = lua::gettop(state);
    const auto frame = static_cast<lua::Integer>(ImGui::GetFrameCount());
    const auto changes = static_cast<lua::Integer>(luaui::changes());
    // The cache, in the registry: element -> { children, frame, changes }, weak keys.
    if (lua::rawgetp(state, lua::registryIndex, &childrenCacheKey) != lua::Type::Table) {
        lua::pop(state);
        lua::createtable(state, 0, 0);
        lua::createtable(state, 0, 1);
        lua::pushcstring(state, "k");
        lua::setfield(state, -2, "__mode");
        lua::setmetatable(state, -2);
        lua::pushvalue(state, -1);
        lua::rawsetp(state, lua::registryIndex, &childrenCacheKey);
    }
    const int cache = lua::gettop(state);
    lua::pushvalue(state, node.index());
    if (lua::rawget(state, cache) == lua::Type::Table) {
        const int entry = lua::gettop(state);
        lua::rawgeti(state, entry, 2);
        lua::rawgeti(state, entry, 3);
        const bool fresh = lua::tointeger(state, -2) == frame && lua::tointeger(state, -1) == changes;
        lua::settop(state, entry);
        if (fresh && lua::rawgeti(state, entry, 1) == lua::Type::Table) {
            lua::insert(state, fn); // the children where the function was
            lua::settop(state, fn);
            return true;
        }
    }
    lua::settop(state, cache);
    lua::pushvalue(state, fn);
    lua::pushvalue(state, node.index());
    // A read (safeRead): it doesn't move luaui::changes(), so the stamp taken before the call still holds after it.
    if (!luaui::safeRead(state, 1, 1) || !lua::istable(state, -1)) {
        lua::settop(state, fn - 1);
        return false;
    }
    const int list = lua::gettop(state);
    lua::pushvalue(state, node.index());
    lua::createtable(state, 3, 0);
    lua::pushvalue(state, list);
    lua::rawseti(state, -2, 1);
    lua::pushinteger(state, frame);
    lua::rawseti(state, -2, 2);
    lua::pushinteger(state, changes);
    lua::rawseti(state, -2, 3);
    lua::rawset(state, cache);
    lua::insert(state, fn); // the children where the function was
    lua::settop(state, fn);
    return true;
}

void Renderer::renderChildren(const Node& node) {
    forEachChild(node, [&](int idx) { render(node.state(), idx); });
}

bool Renderer::isVisual(lua::State* state, int idx) const {
    return m_types.kind(Node{state, idx}) == Types::Kind::Visual;
}

bool Renderer::fillsWidth(lua::State* state, int idx) {
    const Nesting level{*this};
    return level && m_types.fillsWidth(*this, Node{state, idx});
}

std::optional<ImVec2> Renderer::intrinsicSize(const Node& node, float maxWidth) {
    const Nesting level{*this};
    if (!level) return std::nullopt;
    return m_types.intrinsicSize(*this, node, maxWidth);
}

bool Renderer::renderUnseen(lua::State* state, int idx) {
    // An empty clip rect: ImGui still lays the items out (and child windows inherit the clip), nothing is drawn or
    // hovered. Unlike the style alpha, it also hides what widgets draw in their own colors.
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(at, at, true);
    const bool rendered = render(state, idx);
    ImGui::PopClipRect();
    if (rendered) requestFrame(); // to show it, placed by the size it just took
    return rendered;
}

float Renderer::renderStack(const Node& node) {
    const auto spacing = node.number("spacing");
    const float align = alignFactor(node.string("align", "start"));
    lua::State* state = node.state();
    const float boxAvail = layout::availableWidth();
    float avail = boxAvail;
    const float x0 = ImGui::GetCursorScreenPos().x;
    float right = x0;
    // In an auto-sized window the stack is as wide as its widest child (a fill there is only the window's minimum
    // width), so `align` places the children in that width, as measured on the last frame. Not in the window's
    // width: it would follow the window, which follows the aligned children.
    const bool fit = align > 0.0f && layout::shrinkToFit();
    const ImGuiID widestKey = fit ? ImGui::GetID("##imlui.stackWidth") : 0;
    float widest = 0.0f;
    if (fit) {
        const float last = layout::lastSize(widestKey).value_or(ImVec2{}).x;
        layout::assumeWidth(widestKey, last);
        avail = std::max(avail, last);
    }
    // Culled children keep the sizes they had at the width they last rendered in: on a frame the stack's width
    // changed (a window resize), every child renders, and those whose size changed with it until they settle.
    // Pins are recorded whenever the stack may cull (`pins`), also on such a frame, for the next.
    Culling culling{*this, node, boxAvail};
    const bool pins = culling.pins();
    const bool cull = culling.culls();
    const bool cullBelow = culling.cullsBelow();
    // The heights of the visual children laid out so far (rendered or culled): their average is the estimate for a
    // child never measured yet.
    float knownHeights = 0.0f;
    int known = 0;
    bool first = true;
    forEachChild(node, [&](int idx) {
        if (!culling.next(idx)) {
            render(state, idx);
            return;
        }
        Node c{state, idx};
        if (!c.boolean(luaui::keys::visible, true)) return;
        if (!first && spacing)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + static_cast<float>(*spacing) * dpiScale() -
                                 ImGui::GetStyle().ItemSpacing.y);
        bool unseen = false;
        float fillOffset = 0.0f;
        // Every child starts at the stack's left edge, set explicitly: ImGui starts a new line at the window's
        // whole-pixel indent, which would put the children after the first a fraction of a pixel left of it in a
        // stack that starts at a fractional x (after a Text in a Row), and their sizes would no longer compose.
        ImGui::SetCursorScreenPos({x0, ImGui::GetCursorScreenPos().y});
        if (cullBelow) {
            // Out of view: only its size from the layout cache, placed where `align` would place it.
            if (const auto culled =
                    skipOutOfView(culling.key(c, false), ImGui::GetCursorScreenPos(), avail, align, !cull)) {
                if (align > 0.0f) widest = std::max(widest, culled->size.x);
                right = std::max(right, culled->at.x + culled->size.x);
                knownHeights += culled->size.y;
                ++known;
                first = false;
                return;
            }
            // Never measured (a long page's first frame), below the view, and past this frame's converging renders:
            // laid out at the average height of its siblings so far (no width: it widens nothing), and measured on a
            // later frame (asked for). Not when its subtree holds a non-visual element: that renders now, to pin it.
            const ImGuiID k = culling.key(c, false);
            if (known > 0 && cull::belowView(ImGui::GetCursorScreenPos().y) && !cull::pinned(k) &&
                !layout::lastSize(k) && !takeConvergeRenders() && !culling.holdsNonVisual(c)) {
                ImGui::Dummy({0.0f, knownHeights / static_cast<float>(known)});
                noteProvisional();
                requestConvergeFrame();
                first = false;
                return;
            }
        }
        const ImGuiID key = pins ? culling.key(c, true) : align > 0.0f ? elementImGuiId(c) : 0;
        if (align > 0.0f) {
            const float width = sizeProp(c, "width");
            if (width != 0.0f ? width < 0.0f : fillsWidth(state, idx)) {
                // A fill or fill-minus child's width is known up front: the box's width (less N). It is placed by it
                // and rendered in a box that starts where it does and is as wide as the stack's, so it resolves to
                // that width again: settled on its first frame, with no cached width (which, read back through its
                // own offset, would converge only geometrically).
                const float w = resolveSize(width != 0.0f ? width : -1.0f, boxAvail, 0.0f);
                fillOffset = std::max(0.0f, (avail - w) * align);
                ImGui::SetCursorScreenPos({x0 + fillOffset, ImGui::GetCursorScreenPos().y});
            } else {
                // Placed by its intrinsic width, else by its width on the last frame; render() records the one it
                // takes (another frame if it differs). On its first frame without either, it renders unseen.
                std::optional<float> w;
                if (const auto intrinsic = intrinsicSize(c, avail))
                    w = intrinsic->x;
                else if (const auto last = layout::lastSize(key))
                    w = last->x;
                unseen = !w;
                layout::assumeWidth(key, w.value_or(0.0f));
                ImGui::SetCursorScreenPos(
                    {x0 + std::max(0.0f, (avail - w.value_or(0.0f)) * align), ImGui::GetCursorScreenPos().y});
            }
        }
        bool rendered = false;
        const cull::Watch watch = pins ? cull::watch() : cull::Watch{};
        if (unseen) {
            rendered = renderUnseen(state, idx);
        } else if (fillOffset > 0.0f) {
            const layout::BoxScope box{layout::insetBox(-fillOffset)};
            rendered = renderShown(state, idx);
        } else {
            rendered = renderShown(state, idx);
        }
        if (pins && rendered && cull::held(watch)) cull::pin(key);
        if (rendered) {
            if (align > 0.0f) widest = std::max(widest, ImGui::GetItemRectSize().x);
            right = std::max(right, ImGui::GetItemRectMax().x);
            knownHeights += ImGui::GetItemRectSize().y;
            ++known;
        } else {
            // Not rendered after all (no instance, or past maxDepth): ImGui asserts when the cursor was moved past the
            // content and no item follows, so end the move with an empty one.
            ImGui::Dummy({0.0f, 0.0f});
        }
        first = false;
    });
    if (fit) layout::recordSize(widestKey, {widest, 0.0f});
    return right;
}

bool Renderer::takeConvergeRenders(int n) {
    const int frame = ImGui::GetFrameCount();
    if (m_convergeFrame != frame) {
        m_convergeFrame = frame;
        m_convergeLeft = convergeRenders;
    }
    if (m_convergeLeft < n) return false;
    m_convergeLeft -= n;
    return true;
}

void Renderer::requestConvergeFrame() {
    const int frame = ImGui::GetFrameCount();
    if (m_convergeAsked == frame) return;
    m_convergeStreak = m_convergeAsked == frame - 1 ? m_convergeStreak + 1 : 1;
    m_convergeAsked = frame;
    if (m_convergeStreak <= maxConvergeFrames) requestFrame();
}

bool Renderer::cullsChildren(const Node& node) const {
    return cull::enabled() && node.boolean(luaui::keys::cull, true);
}

Renderer::Culling::Culling(Renderer& renderer, const Node& node, float width)
    : m_renderer{renderer}, m_state{node.state()}, m_pins{renderer.cullsChildren(node)} {
    if (!m_pins) return;
    CullMemos& memos = renderer.m_cullMemos;
    const int frame = ImGui::GetFrameCount();
    if (memos.context != ImGui::GetCurrentContext()) {
        memos.byId.clear();
        memos.context = ImGui::GetCurrentContext();
    }
    // Containers that stopped rendering leave their memo: dropped once it is old, checked every keepFrames.
    if (frame - memos.swept >= layout::keepFrames) {
        memos.swept = frame;
        std::erase_if(memos.byId, [&](const auto& e) { return frame - e.second.frame >= layout::keepFrames; });
    }
    CullMemo& memo = memos.byId[ImGui::GetID("##imlui.cull")];
    if (memo.frame == frame) return; // rendered twice in a frame (one element in two places): no memo, no culling
    m_cullsBelow = true;
    m_culls = memo.frame >= 0 && std::fabs(memo.width - width) <= 0.5f;
    memo.frame = frame;
    memo.width = width;
    m_children = &memo.children;
}

Renderer::Culling::~Culling() {
    if (m_children && m_children->size() > m_index) m_children->resize(m_index);
}

bool Renderer::Culling::next(int idx) {
    if (!m_children) {
        m_hit = false;
        return m_renderer.isVisual(m_state, idx);
    }
    const void* table = lua::topointer(m_state, idx);
    const std::size_t i = m_index++;
    if (i < m_children->size() && (*m_children)[i].table == table) {
        m_hit = true;
        return (*m_children)[i].visual;
    }
    m_hit = false;
    const bool visual = m_renderer.isVisual(m_state, idx);
    if (i >= m_children->size()) m_children->resize(i + 1);
    (*m_children)[i] = {table, 0, visual};
    return visual;
}

ImGuiID Renderer::Culling::key(const Node& child, bool fresh) {
    Child* c = m_children && m_index > 0 ? &(*m_children)[m_index - 1] : nullptr;
    if (c && m_hit && !fresh && c->key != 0) return c->key;
    const ImGuiID id = m_renderer.elementImGuiId(child);
    if (c) c->key = id;
    return id;
}

bool Renderer::Culling::holdsNonVisual(const Node& child) {
    Child* c = m_children && m_index > 0 ? &(*m_children)[m_index - 1] : nullptr;
    if (c && c->nonVisual >= 0) return c->nonVisual != 0;
    const bool holds = m_renderer.holdsNonVisual(child);
    if (c) c->nonVisual = holds ? 1 : 0;
    return holds;
}

bool Renderer::holdsNonVisual(const Node& node) {
    const Nesting level{*this};
    if (!level) return true; // too deep (a cycle): rendered, which reports it
    lua::State* state = node.state();
    bool holds = false;
    forEachChild(node, [&](int idx) {
        if (holds) return;
        const Node c{state, idx};
        holds = m_types.kind(c) != Types::Kind::Visual || holdsNonVisual(c);
    });
    return holds;
}

std::optional<Renderer::Skipped> Renderer::skipOutOfView(ImGuiID key, ImVec2 at, float alignIn, float align,
                                                         bool belowOnly) {
    if (cull::pinned(key)) return std::nullopt;
    if (belowOnly && !cull::belowView(at.y)) return std::nullopt;
    std::optional<ImVec2> size = layout::settledSize(key); // also keeps the entry from being dropped
    // Not settled yet (its first frames, a Row placing children it measured on the frame before): below the view, its
    // last size will do. Nothing in view is placed after it, so a size still converging only moves what is out of
    // view (and the scroll range) until it renders, in view, at its real size. Above the view it would move what is
    // in view: rendered until settled.
    if (!size) {
        if (!cull::belowView(at.y)) return std::nullopt;
        size = layout::lastSize(key);
        if (!size || takeConvergeRenders()) return std::nullopt; // rendered (unseen, below the view): it settles
        noteProvisional();                                       // laid out by a size that may still change
        requestConvergeFrame();                                  // the rest settle on the next frames
    }
    if (align > 0.0f) at.x += std::max(0.0f, (alignIn - size->x) * align);
    if (!cull::outOfView(at, {at.x + size->x, at.y + size->y})) return std::nullopt;
    // What its group would take: EndGroup and Dummy both lay out an item of this size from here (ItemSize) and add it,
    // so the last item is where the child would have been, as code after it may read.
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy(*size);
    return Skipped{at, *size};
}

std::optional<ImVec2> Renderer::stackSize(const Node& node, float maxWidth) {
    const auto spacing = node.number("spacing");
    const float align = alignFactor(node.string("align", "start"));
    if (align > 0.0f && layout::shrinkToFit()) return std::nullopt; // aligned in last frame's widest child
    lua::State* state = node.state();
    const float lineGap = ImGui::GetStyle().ItemSpacing.y;
    float width = 0.0f, bottom = 0.0f, y = 0.0f;
    bool first = true, known = true;
    forEachChild(node, [&](int idx) {
        if (!known || !isVisual(state, idx)) return;
        const Node c{state, idx};
        if (!c.boolean(luaui::keys::visible, true)) return;
        if (!first && spacing) y += static_cast<float>(*spacing) * dpiScale() - lineGap;
        const std::optional<ImVec2> size = intrinsicSize(c, maxWidth);
        if (!size) {
            known = false;
            return;
        }
        float offset = 0.0f;
        if (align > 0.0f) {
            // Placed like renderStack places it: a fill by the box's width (less N), anything else by its own.
            const float w = sizeProp(c, "width");
            const bool fills = w != 0.0f ? w < 0.0f : fillsWidth(state, idx);
            const float placed = fills ? resolveSize(w != 0.0f ? w : -1.0f, maxWidth, 0.0f) : size->x;
            offset = std::max(0.0f, (maxWidth - placed) * align);
        }
        width = std::max(width, offset + size->x);
        bottom = std::max(bottom, y + size->y);
        // ImGui's next line starts on a whole pixel (ItemSize truncates the cursor), from a stack that starts on one.
        y = std::trunc(y + size->y + lineGap);
        first = false;
    });
    if (!known) return std::nullopt;
    return ImVec2{width, bottom};
}

void Renderer::attachMenu(const Node& node, bool clicked) {
    lua::State* state = node.state();
    if (node.pushRaw("menu") != lua::Type::Table) {
        lua::pop(state);
        return;
    }
    const int menuIdx = lua::gettop(state);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (clicked) ImGui::OpenPopup("##menu");
    Node menu{state, menuIdx};
    if (md::beginAnchoredMenu("##menu", a, b, dp(112))) {
        renderChildren(menu);
        md::endAnchoredMenu();
    }
    lua::settop(state, menuIdx - 1);
}

} // namespace imlui
