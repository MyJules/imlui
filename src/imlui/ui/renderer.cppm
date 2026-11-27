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
import imlui.skin;
import imlui.image.image_cache;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.platform.stack;
import imlui.ui.culling;
import imlui.ui.layout_box;
import imlui.ui.layout_cache;
import imlui.ui.frame_request;
import imlui.ui.id_map;
import imlui.ui.item_size;
import imlui.types.lua_convert;
import imlui.ui.enum_names;
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
        /// elementId() and kind() at once: what Frame::Child asks of every child a layout visits, on every pass.
        struct Identity {
            std::uint64_t id = 0;
            Kind kind = Kind::Unknown;
        };
        /// By default elementId() and kind(); an implementation can answer both with one lookup of the instance.
        [[nodiscard]] virtual Identity identity(const luaui::Node& node);

        virtual ~Types() = default;
    };

    /// `types` must outlive the renderer.
    explicit Renderer(Types& types);

    /// The deepest an element tree nests: render(), intrinsicSize(), fillsWidth() and the inline containers
    /// forEachChild() splices each count a level. A deeper tree is almost surely a cycle (`a[1] = b; b[1] = a`, a
    /// Loader that shows its own ancestor), which would overflow the native stack: the levels past it are skipped
    /// and reported as a script error.
    static constexpr int maxDepth = 256;
    /// The native stack a level needs left (platform::stackRemaining()): a level entered with less is skipped and
    /// reported like one past maxDepth, so a tree too deep for the thread's stack is cut instead of overflowing it.
    /// What runs below the deepest level (a leaf's widgets, a Lua handler, a thrown error's unwinding) has this much.
    static constexpr std::size_t stackReserve = std::size_t{256} * 1024;

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
    /// elementId() and the element's kind at once (Types::identity), for Frame::Child.
    [[nodiscard]] Types::Identity identity(const luaui::Node& node);
    /// The ImGui ID in the current ID scope of the element whose elementId() is `id`: elementImGuiId().
    [[nodiscard]] static ImGuiID imguiId(std::uint64_t id);
    /// The ImGui ID of the element in the current ID scope, made from elementId(): the ID of the scope render()
    /// pushes for it, and its key in the layout cache.
    [[nodiscard]] ImGuiID elementImGuiId(const luaui::Node& node);
    /// Pushes the element's ImGui ID scope, the one render() renders it in (pop with ImGui::PopID()): for a
    /// container that renders a child's children itself (TabBar renders the current Tab's content).
    void pushElementId(const luaui::Node& node);

private:
    struct ChildMemo;

public:
    /// A child that forEachChild() visits: its table at a stack index, and what the passes over its container's
    /// children learned of it on this frame. A layout asks the same of a child on every pass (a Row's fills, sizes and
    /// render; its parent's contentFillsWidth and size questions), so each answer (`visible`, the kind and identity,
    /// the `width`) is looked up once a frame while luaui::changes() stands still and the container's child at its
    /// place is the same table (ChildMemo): a handler that ran in between, or a changed child list, looks it up again.
    /// Valid only inside the forEachChild() callback that gives it.
    class ChildRef {
    public:
        /// A child with nothing remembered (not met by forEachChild(): a ListView's item): every answer is looked up.
        ChildRef(Renderer& renderer, lua::State* state, int idx);

        [[nodiscard]] lua::State* state() const { return m_state; }
        [[nodiscard]] int index() const { return m_index; }
        [[nodiscard]] luaui::Node node() const { return {m_state, m_index}; }
        /// imlui::shown(): its `visible`.
        [[nodiscard]] bool shown() const;
        /// Types::kind(): what the element is, without creating its instance.
        [[nodiscard]] Types::Kind kind() const;
        /// Types::identity(): its instance's id (the instance created now if it has none) and its kind.
        [[nodiscard]] Types::Identity identity() const;
        /// sizeProp(node, "width"): its `width` (or `fillWidth`) in pixels.
        [[nodiscard]] float width() const;
        /// Whether it fills the available width: a fill or fill-minus width(), or none on an element that fills
        /// (Renderer::fillsWidth).
        [[nodiscard]] bool fillsWidth() const;

        /// What a ChildMemo holds of a child, taken on `frame` at luaui::changes() `changes` from `table`.
        struct Fact {
            const void* table = nullptr;
            int frame = -1;
            std::uint64_t changes = 0;
            bool element = false;     ///< it has a `__type`
            bool inl = false;         ///< `__inline`
            std::int8_t visible = -1; ///< -1: not read yet
            std::int8_t kind = -1;    ///< a Types::Kind, -1: not looked up yet
            std::int8_t fills = -1;   ///< fillsWidth(), -1: not asked yet
            bool hasId = false;
            bool hasWidth = false;
            float width = 0.0f;
            std::uint64_t id = 0;
        };

    private:
        friend class Renderer;
        ChildRef(Renderer& renderer, lua::State* state, int idx, ChildMemo* memo, std::size_t pos);
        /// What the memo holds of this child while it is of this frame, of this luaui::changes() and of this table;
        /// else null. Not kept across a call into Lua: the memo may grow meanwhile.
        [[nodiscard]] Fact* fact() const;

        Renderer* m_renderer;
        lua::State* m_state;
        int m_index;
        ChildMemo* m_memo = nullptr; ///< null: nothing remembered
        std::size_t m_pos = 0;       ///< its place in m_memo
    };

    /// What a forEachChild() does with the container's children memo (ChildRef).
    enum class Memo {
        /// Remembers what it learns of the children, for the passes after it: a layout that asks about its children
        /// before it renders them (Frame::forEachChild: a Row's plan, contentFillsWidth, a stack's size).
        Record,
        /// Uses what earlier passes of the frame remembered, and remembers nothing new: a container's render pass,
        /// often the only pass over its children (a long page's stack, most of whose children are culled), where
        /// recording would cost more than it saves.
        Reuse,
    };

    /// Calls f(ChildRef&) for every child element, or f(stackIndex) for an `f` that takes an int. Children come from
    /// the element's array part, or from its `__children(self)` function (Repeater / ListView / If / Loader on the
    /// Lua side), called once a frame per element (pushChildren). Inline containers are spliced into the parent.
    template <class F>
    void forEachChild(const luaui::Node& node, F&& f) {
        forEachChild(node, Memo::Record, f);
    }
    /// The same, with what it does with the children memo.
    template <class F>
    void forEachChild(const luaui::Node& node, Memo memo, F&& f) {
        lua::State* state = node.state();
        // Room for pushChildren() and a child: each inline container spliced in nests a level on the Lua stack.
        if (!lua::checkstack(state, 16) || !pushChildren(node)) return;
        const int src = lua::gettop(state);
        {
            ChildWalk walk{*this, node, memo};
            walkChildren(walk, src, f);
        }
        lua::settop(state, src - 1);
    }

    void renderChildren(const luaui::Node& node);
    /// render() of a child that forEachChild() gave.
    bool render(const ChildRef& child);

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
    /// render() of an element whose `visible` the caller has just read as true: not read again.
    bool renderShown(lua::State* state, int idx);
    /// renderShown() of an element whose identity() the caller knows (Frame::Child): not looked up again.
    bool renderShown(const luaui::Node& node, const Types::Identity& identity);

    /// Renders children top-to-bottom. `spacing` (dp) overrides the gap
    /// between visible items; `align` positions them horizontally in the layout box (imlui.ui.layout_box), by their
    /// intrinsic widths or else last frame's (imlui.ui.layout_cache). Returns the right edge (screen x) of the widest
    /// visual child, or the stack's left edge without one: for a container that pads its content on the right.
    float renderStack(const luaui::Node& node);
    /// The same with the gap given: an element that holds its `spacing` synced (Column, ScrollView, Card) passes it, so
    /// the stack doesn't read it from the table again. (`align` is read from the table: a name that isn't an Align
    /// places the children at the start, while the element's property keeps the value it had.)
    float renderStack(const luaui::Node& node, std::optional<double> spacing);
    /// The `align` renderStack(node) places the children by, read from the table (a value or a binding): Start when
    /// it is unset or not an Align name. The element's `align` property (Column's, StackAlign) reports such a name,
    /// once, when its field is written: this read doesn't, as it runs every frame.
    [[nodiscard]] static Align stackAlign(const luaui::Node& node);
    /// The size renderStack(node) takes with `maxWidth` available, composed from the visual children's intrinsic
    /// sizes (and ImGui's whole-pixel line advance); {0, 0} without visual children. nullopt when a child has none,
    /// or when an aligned stack is in an auto-sized window (it aligns in the widest child of the last frame).
    [[nodiscard]] std::optional<ImVec2> stackSize(const luaui::Node& node, float maxWidth);
    [[nodiscard]] std::optional<ImVec2> stackSize(const luaui::Node& node, float maxWidth,
                                                  std::optional<double> spacing);

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
        /// width and the DPI scale (dpiScale(): a zoom, another monitor) they last rendered at, so on a frame either
        /// differs from the last one's (a resize, a zoom) nothing is culled.
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
        /// Moves to the next child; returns whether it is visual (isVisual()), from the memo when its table is the one
        /// at this position last frame.
        bool next(const ChildRef& child);
        /// The current child's elementImGuiId(): from the memo when it may be culled (`fresh` false), else looked
        /// up (and remembered). A memo entry can only be stale for a table Lua collected and reallocated at the
        /// same address and position since the last frame: then a culled child is laid out at the dead one's size
        /// until it comes into view, where it renders and is looked up afresh.
        ImGuiID key(const ChildRef& child, bool fresh);
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
    /// Counts a level of nesting while it lives; false past maxDepth or with less than stackReserve of the native stack
    /// left (reported).
    class Nesting {
    public:
        explicit Nesting(Renderer& renderer);
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
    /// Reports a tree nested deeper than maxDepth, or than the native stack holds (`stack`) at `depth` levels
    /// (luaui::reportError, which reports a message once).
    static void reportTooDeep(int depth, bool stack);

    /// What the passes over a container's children on a frame learned of them (ChildRef), by their place in the walk
    /// (inline containers' children spliced in, the inline containers themselves too). Kept per container table;
    /// each fact holds the frame, the luaui::changes() and the child table it was taken at, so a handler that ran
    /// since, or a child list that changed (another table at a place), has the child looked up again.
    struct ChildMemo {
        lua::State* state = nullptr;
        int frame = -1; ///< the last frame a walk used it
        int walks = 0;  ///< walks of it running (a container that holds itself, cut at maxDepth)
        std::vector<ChildRef::Fact> facts;
    };
    struct ChildMemos {
        ImGuiContext* context = nullptr;
        int swept = 0;
        int walks = 0; ///< walks running: memos are dropped only while none runs (a ChildRef points into one)
        IdMap<std::unique_ptr<ChildMemo>> byTable; ///< (a ChildRef points into a memo: they stay put)
    };

    /// One forEachChild() over a container: its memo (null outside a frame), and the place of the next child.
    class ChildWalk {
    public:
        ChildWalk(Renderer& renderer, const luaui::Node& node, Memo mode);
        ~ChildWalk();
        ChildWalk(const ChildWalk&) = delete;
        ChildWalk& operator=(const ChildWalk&) = delete;
        ChildWalk(ChildWalk&&) = delete;
        ChildWalk& operator=(ChildWalk&&) = delete;

        /// The table at stack index `idx`, the next item of the walk.
        struct Item {
            bool element; ///< it has a `__type`
            bool inl;     ///< an inline container (`__inline`): its children are spliced in
            ChildRef ref;
        };
        [[nodiscard]] Item next(int idx);
        [[nodiscard]] lua::State* state() const { return m_state; }

    private:
        Renderer& m_renderer;
        lua::State* m_state;
        ChildMemo* m_memo = nullptr;
        std::size_t m_pos = 0;
        bool m_record;
    };

    /// forEachChild()'s loop over the children list at stack index `src`, inline containers spliced in.
    template <class F>
    void walkChildren(ChildWalk& walk, int src, F& f) {
        lua::State* state = walk.state();
        const lua::Integer len = lua::rawlen(state, src);
        for (lua::Integer i = 1; i <= len; ++i) {
            lua::rawgeti(state, src, i);
            if (lua::istable(state, -1)) {
                const int idx = lua::gettop(state);
                ChildWalk::Item item = walk.next(idx);
                if (item.element && item.inl) {
                    const Nesting level{*this};
                    if (level && item.ref.shown() && lua::checkstack(state, 16) &&
                        pushChildren(luaui::Node{state, idx}))
                        walkChildren(walk, lua::gettop(state), f);
                } else if (item.element) {
                    if constexpr (std::invocable<F&, int>)
                        f(idx);
                    else
                        f(item.ref);
                }
            }
            lua::settop(state, src);
        }
    }

    /// Pushes the table that holds the children of `node`: the node itself, or what its `__children(self)` returned.
    /// That call (a read: the model function, the delegates it builds) runs once a frame per element while
    /// luaui::changes() stands still, so the passes of a layout (a Row's: which children fill, their sizes, the
    /// render; the fills and intrinsic sizes its parent asks about) share it; a handler that ran in between makes
    /// the next pass call it again. Returns false, with nothing pushed, when it failed (reported) or returned no
    /// table.
    bool pushChildren(const luaui::Node& node);
    /// render() from the element's ID scope on, for a known type; `key` its elementId().
    bool renderElement(const luaui::Node& node, Types::Kind kind, std::uint64_t key);

    /// Culling's memo of a container's children, by an ImGui ID in the container's scope.
    struct CullMemo {
        float width = -1.0f;
        float scale = 0.0f; ///< dpiScale() on that frame
        int frame = -1;
        std::vector<Culling::Child> children;
    };
    struct CullMemos {
        ImGuiContext* context = nullptr;
        int swept = 0;
        IdMap<std::unique_ptr<CullMemo>> byId; ///< (a Culling points into its memo: they stay put)
    };

    Types& m_types;
    CullMemos m_cullMemos;
    ChildMemos m_childMemos;
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

Renderer::Types::Identity Renderer::Types::identity(const Node& node) { return {elementId(node), kind(node)}; }

bool Renderer::render(lua::State* state, int idx) {
    lua::checkstack(state, 32);
    const Node n{state, idx};
    const Types::Kind kind = m_types.kind(n);
    if (kind == Types::Kind::Unknown) {
        luaui::reportError("unknown element type '" + n.type() + "'");
        return false;
    }
    if (!shown(n)) return false;
    return renderElement(n, kind, elementId(n));
}

bool Renderer::renderShown(lua::State* state, int idx) {
    lua::checkstack(state, 32);
    const Node n{state, idx};
    return renderShown(n, identity(n));
}

bool Renderer::renderShown(const Node& node, const Types::Identity& identity) {
    lua::checkstack(node.state(), 32);
    if (identity.kind == Types::Kind::Unknown) {
        luaui::reportError("unknown element type '" + node.type() + "'");
        return false;
    }
    return renderElement(node, identity.kind, identity.id);
}

Renderer::Nesting::Nesting(Renderer& renderer) : m_depth{renderer.m_depth}, m_ok{++m_depth <= maxDepth} {
    if (!m_ok) {
        reportTooDeep(m_depth, false);
        return;
    }
    const std::optional<std::size_t> left = platform::stackRemaining();
    if (left && *left < stackReserve) {
        m_ok = false;
        reportTooDeep(m_depth, true);
    }
}

void Renderer::reportTooDeep(int depth, bool stack) {
    if (stack)
        luaui::reportError("element tree nested deeper than the native stack holds (" + std::to_string(depth) +
                           " levels; does an element contain itself?); the deeper levels are skipped");
    else
        luaui::reportError("element tree nested deeper than " + std::to_string(maxDepth) +
                           " levels (does an element contain itself?); the deeper levels are skipped");
}

bool Renderer::renderElement(const Node& n, Types::Kind kind, std::uint64_t key) {
    // Every visual element is exactly one layout item (a group), so composite elements (label + field, ...) behave
    // inside Rows.
    const bool visual = kind == Types::Kind::Visual;
    // A visual element that can't render (past maxDepth, or no instance) still records a size, zero, under the ID its
    // parent knows it by (Frame::Child::id(): idKey(0) without an instance). Layouts that place children by their
    // expected sizes (Row, a stack's `align`, the TopAppBar's actions, the NavigationRail) would otherwise wait for
    // its first size forever: a Row with a fill would render unseen on every frame and ask for the next.
    const auto recordNothing = [&](std::uint64_t key) {
        if (visual) layout::recordSize(ImGui::GetID(idKey(key)), {0.0f, 0.0f});
    };
    const Nesting level{*this};
    if (!level || key == 0) { // key 0: no instance, its constructor threw (reported)
        recordNothing(key);
        return false;
    }
    const ImGuiID id = imguiId(key);
    const MaterialStacksGuard stacks;
    ImGui::PushOverrideID(id); // what PushID(idKey(key)) pushes, without hashing it again
    const bool disabled = n.has(luaui::keys::enabled) && !n.boolean(luaui::keys::enabled, true);
    if (disabled) ImGui::BeginDisabled();
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
    // The text (a binding, often a formatted string) is read only while the element is hovered: the tooltip's own
    // delay, and the frames it asks for meanwhile, start there.
    if (n.has(luaui::keys::tooltip) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        skin().tooltip(n.string("tooltip"));
    if (disabled) ImGui::EndDisabled();
    ImGui::PopID();
    return true;
}

std::uint64_t Renderer::elementId(const Node& node) { return m_types.elementId(node); }
Renderer::Types::Identity Renderer::identity(const Node& node) { return m_types.identity(node); }
ImGuiID Renderer::imguiId(std::uint64_t id) { return ImGui::GetID(idKey(id)); }
ImGuiID Renderer::elementImGuiId(const Node& node) { return imguiId(elementId(node)); }
void Renderer::pushElementId(const Node& node) { ImGui::PushOverrideID(elementImGuiId(node)); }

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
        lua::settop(state, entry); // stale: refilled below, so an element costs no new table a frame
    } else {
        lua::pop(state);
        lua::createtable(state, 3, 0);
        lua::pushvalue(state, node.index());
        lua::pushvalue(state, -2);
        lua::rawset(state, cache);
    }
    const int entry = lua::gettop(state);
    lua::pushvalue(state, fn);
    lua::pushvalue(state, node.index());
    // A read (safeRead): it doesn't move luaui::changes(), so the stamp taken before the call still holds after it.
    if (!luaui::safeRead(state, 1, 1) || !lua::istable(state, -1)) {
        lua::settop(state, fn - 1); // the entry keeps its old stamp: the next pass calls the function again
        return false;
    }
    const int list = lua::gettop(state);
    lua::pushvalue(state, list);
    lua::rawseti(state, entry, 1);
    lua::pushinteger(state, frame);
    lua::rawseti(state, entry, 2);
    lua::pushinteger(state, changes);
    lua::rawseti(state, entry, 3);
    lua::insert(state, fn); // the children where the function was
    lua::settop(state, fn);
    return true;
}

void Renderer::renderChildren(const Node& node) {
    forEachChild(node, Memo::Reuse, [&](const ChildRef& c) { render(c); });
}

bool Renderer::render(const ChildRef& child) {
    lua::checkstack(child.state(), 32);
    const Types::Kind kind = child.kind();
    const Node n = child.node();
    if (kind == Types::Kind::Unknown) {
        luaui::reportError("unknown element type '" + n.type() + "'");
        return false;
    }
    if (!child.shown()) return false;
    return renderElement(n, kind, child.identity().id);
}

// ------------------------------------------------------------ the children memo

namespace {

/// The current ImGui frame; -1 without a context (nothing is remembered then).
int memoFrame() { return ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1; }

} // namespace

Renderer::ChildRef::ChildRef(Renderer& renderer, lua::State* state, int idx)
    : m_renderer{&renderer}, m_state{state}, m_index{lua::absindex(state, idx)} {}

Renderer::ChildRef::ChildRef(Renderer& renderer, lua::State* state, int idx, ChildMemo* memo, std::size_t pos)
    : m_renderer{&renderer}, m_state{state}, m_index{idx}, m_memo{memo}, m_pos{pos} {}

Renderer::ChildRef::Fact* Renderer::ChildRef::fact() const {
    if (!m_memo || m_pos >= m_memo->facts.size()) return nullptr;
    Fact& f = m_memo->facts[m_pos];
    if (f.frame < 0 || f.frame != memoFrame() || f.changes != luaui::changes() ||
        f.table != lua::topointer(m_state, m_index))
        return nullptr;
    return &f;
}

// Each answer is stored only when the fact still holds after the lookup: a lookup that counted as a change (a binding
// that failed) leaves nothing, like the prop memo (imlui.ui.props).
bool Renderer::ChildRef::shown() const {
    if (const Fact* f = fact(); f && f->visible >= 0) return f->visible != 0;
    const bool v = imlui::shown(node());
    if (Fact* f = fact()) f->visible = v ? 1 : 0;
    return v;
}

Renderer::Types::Kind Renderer::ChildRef::kind() const {
    if (const Fact* f = fact(); f && f->kind >= 0) return static_cast<Types::Kind>(f->kind);
    const Types::Kind k = m_renderer->m_types.kind(node());
    if (Fact* f = fact()) f->kind = static_cast<std::int8_t>(k);
    return k;
}

Renderer::Types::Identity Renderer::ChildRef::identity() const {
    if (const Fact* f = fact(); f && f->hasId) return {f->id, static_cast<Types::Kind>(f->kind)};
    const Types::Identity identity = m_renderer->identity(node());
    if (Fact* f = fact()) {
        f->hasId = true;
        f->id = identity.id;
        f->kind = static_cast<std::int8_t>(identity.kind);
    }
    return identity;
}

float Renderer::ChildRef::width() const {
    if (const Fact* f = fact(); f && f->hasWidth) return f->width;
    const float w = sizeProp(node(), "width");
    if (Fact* f = fact()) {
        f->hasWidth = true;
        f->width = w;
    }
    return w;
}

bool Renderer::ChildRef::fillsWidth() const {
    if (const Fact* f = fact(); f && f->fills >= 0) return f->fills != 0;
    const float w = width();
    const bool fills = w != 0.0f ? w < 0.0f : m_renderer->fillsWidth(m_state, m_index);
    if (Fact* f = fact()) f->fills = fills ? 1 : 0;
    return fills;
}

Renderer::ChildWalk::ChildWalk(Renderer& renderer, const Node& node, Memo mode)
    : m_renderer{renderer}, m_state{node.state()}, m_record{mode == Memo::Record} {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context) return; // outside a frame: nothing is remembered
    ChildMemos& memos = renderer.m_childMemos;
    const int frame = ImGui::GetFrameCount();
    if (memos.walks == 0) {
        // Memos are dropped only while no walk runs: a ChildRef points into its container's.
        if (memos.context != context) {
            memos.byTable.clear();
            memos.context = context;
            memos.swept = frame;
        }
        // Containers that stopped rendering leave their memo, whose facts are of an old frame: dropped every
        // layout::ageSweepFrames.
        if (frame - memos.swept >= layout::ageSweepFrames || frame < memos.swept) {
            memos.swept = frame;
            memos.byTable.eraseIf(
                [&](std::uint64_t, const std::unique_ptr<ChildMemo>& m) { return m->frame != frame; });
        }
    } else if (memos.context != context) {
        return;
    }
    const auto key = reinterpret_cast<std::uintptr_t>(node.ptr());
    ChildMemo* found = nullptr;
    if (m_record) {
        std::unique_ptr<ChildMemo>& slot = memos.byTable[key];
        if (!slot) slot = std::make_unique<ChildMemo>();
        found = slot.get();
    } else {
        const std::unique_ptr<ChildMemo>* slot = memos.byTable.find(key);
        if (!slot || (*slot)->state != m_state || (*slot)->frame != frame) return; // nothing of this frame to reuse
        found = slot->get();
    }
    ChildMemo& memo = *found;
    if (memo.state != m_state) {
        if (memo.walks > 0) return; // (another state's walk of a table at this address: none to share)
        memo.state = m_state;
        memo.facts.clear();
    }
    memo.frame = frame;
    ++memo.walks;
    ++memos.walks;
    m_memo = &memo;
}

Renderer::ChildWalk::~ChildWalk() {
    if (!m_memo) return;
    --m_memo->walks;
    --m_renderer.m_childMemos.walks;
    // A shorter child list than at the last walk: its facts past the end are of children no longer there.
    if (m_record && m_memo->walks == 0 && m_memo->facts.size() > m_pos) m_memo->facts.resize(m_pos);
}

Renderer::ChildWalk::Item Renderer::ChildWalk::next(int idx) {
    const auto look = [&](bool& element, bool& inl) {
        // `__type` is the table's own field in every element: read raw first (through its Key), and only a table
        // without one through its metatable (a script's __index may answer).
        if (lua::rawget(m_state, idx, luaui::keys::type) == lua::Type::Nil) {
            lua::pop(m_state);
            lua::getfield(m_state, idx, "__type");
        }
        element = lua::isstring(m_state, -1);
        lua::pop(m_state);
        lua::rawget(m_state, idx, luaui::keys::inlined); // raw: only the table's own marker counts
        inl = lua::toboolean(m_state, -1);
        lua::pop(m_state);
    };
    if (!m_memo) {
        Item item{false, false, ChildRef{m_renderer, m_state, idx, nullptr, 0}};
        look(item.element, item.inl);
        return item;
    }
    const std::size_t pos = m_pos++;
    const void* table = lua::topointer(m_state, idx);
    const int frame = ImGui::GetFrameCount();
    const std::uint64_t changes = luaui::changes();
    if (pos < m_memo->facts.size()) {
        const ChildRef::Fact& f = m_memo->facts[pos];
        if (f.table == table && f.frame == frame && f.changes == changes)
            return {f.element, f.inl, ChildRef{m_renderer, m_state, idx, m_memo, pos}};
    }
    if (!m_record) {
        Item item{false, false, ChildRef{m_renderer, m_state, idx, nullptr, 0}};
        look(item.element, item.inl);
        return item;
    }
    if (pos >= m_memo->facts.size()) m_memo->facts.resize(pos + 1);
    Item item{false, false, ChildRef{m_renderer, m_state, idx, m_memo, pos}};
    look(item.element, item.inl); // (a `__type` read through a script's __index runs Lua: `f` is looked up again)
    ChildRef::Fact& g = m_memo->facts[pos];
    g = ChildRef::Fact{};
    g.table = table;
    g.frame = frame;
    g.changes = changes;
    g.element = item.element;
    g.inl = item.inl;
    return item;
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
    if (rendered) {
        requestFrame(); // to show it, placed by the size it just took
        layout::noteStale();
    }
    return rendered;
}

Align Renderer::stackAlign(const Node& node) {
    lua::State* state = node.state();
    node.push("align");
    const Align align = LuaConvert<Align>::get(state, -1); // Start when it isn't a name
    lua::pop(state);
    return align;
}

float Renderer::renderStack(const Node& node) { return renderStack(node, node.number("spacing")); }

float Renderer::renderStack(const Node& node, std::optional<double> spacing) {
    const float align = alignFactor(stackAlign(node));
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
    forEachChild(node, Memo::Reuse, [&](const ChildRef& ref) {
        if (!culling.next(ref)) {
            render(ref);
            return;
        }
        if (!ref.shown()) return;
        const int idx = ref.index();
        const Node c{state, idx};
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
                    skipOutOfView(culling.key(ref, false), ImGui::GetCursorScreenPos(), avail, align, !cull)) {
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
            const ImGuiID k = culling.key(ref, false);
            if (known > 0 && cull::belowView(ImGui::GetCursorScreenPos().y) && !cull::pinned(k) &&
                !layout::lastSize(k) && !takeConvergeRenders() && !culling.holdsNonVisual(c)) {
                ImGui::Dummy({0.0f, knownHeights / static_cast<float>(known)});
                noteProvisional();
                requestConvergeFrame();
                first = false;
                return;
            }
        }
        const ImGuiID key = pins ? culling.key(ref, true) : align > 0.0f ? imguiId(ref.identity().id) : 0;
        if (align > 0.0f) {
            const float width = ref.width();
            if (ref.fillsWidth()) {
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
            rendered = renderShown(c, ref.identity());
        } else {
            rendered = renderShown(c, ref.identity());
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
        memos.byId.eraseIf(
            [&](std::uint64_t, const std::unique_ptr<CullMemo>& m) { return frame - m->frame >= layout::keepFrames; });
    }
    std::unique_ptr<CullMemo>& slot = memos.byId[ImGui::GetID("##imlui.cull")];
    if (!slot) slot = std::make_unique<CullMemo>();
    CullMemo& memo = *slot;
    if (memo.frame == frame) return; // rendered twice in a frame (one element in two places): no memo, no culling
    m_cullsBelow = true;
    // The cached sizes are in pixels: a zoom or another monitor's scale changes them, as a resize does.
    const float scale = dpiScale();
    m_culls = memo.frame >= 0 && std::fabs(memo.width - width) <= 0.5f && memo.scale == scale;
    memo.frame = frame;
    memo.width = width;
    memo.scale = scale;
    m_children = &memo.children;
}

Renderer::Culling::~Culling() {
    if (m_children && m_children->size() > m_index) m_children->resize(m_index);
}

bool Renderer::Culling::next(const ChildRef& child) {
    if (!m_children) {
        m_hit = false;
        return child.kind() == Types::Kind::Visual;
    }
    const void* table = lua::topointer(m_state, child.index());
    const std::size_t i = m_index++;
    if (i < m_children->size() && (*m_children)[i].table == table) {
        m_hit = true;
        return (*m_children)[i].visual;
    }
    m_hit = false;
    const bool visual = child.kind() == Types::Kind::Visual;
    if (i >= m_children->size()) m_children->resize(i + 1);
    (*m_children)[i] = {table, 0, visual};
    return visual;
}

ImGuiID Renderer::Culling::key(const ChildRef& child, bool fresh) {
    Child* c = m_children && m_index > 0 ? &(*m_children)[m_index - 1] : nullptr;
    if (c && m_hit && !fresh && c->key != 0) return c->key;
    const ImGuiID id = imguiId(child.identity().id);
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
    bool holds = false;
    forEachChild(node, Memo::Reuse, [&](const ChildRef& c) {
        if (holds) return;
        holds = c.kind() != Types::Kind::Visual || holdsNonVisual(c.node());
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
    return stackSize(node, maxWidth, node.number("spacing"));
}

std::optional<ImVec2> Renderer::stackSize(const Node& node, float maxWidth, std::optional<double> spacing) {
    const float align = alignFactor(stackAlign(node));
    if (align > 0.0f && layout::shrinkToFit()) return std::nullopt; // aligned in last frame's widest child
    lua::State* state = node.state();
    const float lineGap = ImGui::GetStyle().ItemSpacing.y;
    float width = 0.0f, bottom = 0.0f, y = 0.0f;
    bool first = true, known = true;
    forEachChild(node, [&](const ChildRef& ref) {
        if (!known || ref.kind() != Types::Kind::Visual || !ref.shown()) return;
        const int idx = ref.index();
        const Node c{state, idx};
        if (!first && spacing) y += static_cast<float>(*spacing) * dpiScale() - lineGap;
        const std::optional<ImVec2> size = intrinsicSize(c, maxWidth);
        if (!size) {
            known = false;
            return;
        }
        float offset = 0.0f;
        if (align > 0.0f) {
            // Placed like renderStack places it: a fill by the box's width (less N), anything else by its own.
            const float w = ref.width();
            const bool fills = ref.fillsWidth();
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
    if (skin().beginAnchoredMenu("##menu", a, b, dp(112))) {
        renderChildren(menu);
        skin().endAnchoredMenu();
    }
    lua::settop(state, menuIdx - 1);
}

} // namespace imlui
