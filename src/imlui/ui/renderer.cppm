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

        /// What `type` is.
        [[nodiscard]] virtual Kind kind(std::string_view type) const = 0;
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
    /// (Repeater / ListView / If / Loader on the Lua side). Inline
    /// containers are spliced into the parent.
    template <class F>
    void forEachChild(const luaui::Node& node, F&& f) {
        lua::State* state = node.state();
        node.pushRaw("__children");
        if (lua::isfunction(state, -1)) {
            lua::pushvalue(state, node.index());
            if (!luaui::safeRead(state, 1, 1)) return; // a read: the model, the delegates it builds
            if (!lua::istable(state, -1)) {
                lua::pop(state);
                return;
            }
        } else {
            lua::pop(state);
            lua::pushvalue(state, node.index());
        }
        const int src = lua::gettop(state);
        const lua::Integer len = lua::rawlen(state, src);
        for (lua::Integer i = 1; i <= len; ++i) {
            lua::rawgeti(state, src, i);
            if (lua::istable(state, -1)) {
                lua::getfield(state, -1, "__type");
                bool element = lua::isstring(state, -1);
                lua::pop(state);
                lua::pushcstring(state, "__inline"); // raw: only the table's own marker counts
                lua::rawget(state, -2);
                bool inl = lua::toboolean(state, -1);
                lua::pop(state);
                if (element && inl) {
                    luaui::Node c{state, lua::gettop(state)};
                    if (c.boolean("visible", true)) forEachChild(c, f);
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

    /// Renders children top-to-bottom. `spacing` (dp) overrides the gap
    /// between visible items; `align` positions them horizontally in the layout box (imlui.ui.layout_box), by their
    /// intrinsic widths or else last frame's (imlui.ui.layout_cache).
    void renderStack(const luaui::Node& node);
    /// The size renderStack(node) takes with `maxWidth` available, composed from the visual children's intrinsic
    /// sizes (and ImGui's whole-pixel line advance); {0, 0} without visual children. nullopt when a child has none,
    /// or when an aligned stack is in an auto-sized window (it aligns in the widest child of the last frame).
    [[nodiscard]] std::optional<ImVec2> stackSize(const luaui::Node& node, float maxWidth);

    /// Pops up `node.menu` (a Menu element) anchored to the last item when
    /// `clicked`; renders it while open.
    void attachMenu(const luaui::Node& node, bool clicked);

private:
    Types& m_types;
    std::string m_title{"imlui"};
    ImageCache* m_images = nullptr;
};

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

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
    Node n{state, idx};
    const std::string type = n.type();
    const Types::Kind kind = m_types.kind(type);
    if (kind == Types::Kind::Unknown) {
        luaui::reportError("unknown element type '" + type + "'");
        return false;
    }
    if (!n.boolean("visible", true)) return false;
    const std::uint64_t key = elementId(n);
    if (key == 0) return false; // no instance: its constructor threw, which was reported
    const ImGuiID id = ImGui::GetID(idKey(key));
    const MaterialStacksGuard stacks;
    ImGui::PushID(idKey(key));
    const bool disabled = n.has("enabled") && !n.boolean("enabled", true);
    if (disabled) ImGui::BeginDisabled();
    // Every visual element is exactly one layout item (a group), so
    // composite elements (label + field, ...) behave inside Rows.
    const bool visual = kind == Types::Kind::Visual;
    if (visual) ImGui::BeginGroup();
    m_types.renderElement(*this, n);
    if (visual) {
        ImGui::EndGroup();
        layout::recordSize(id, ImGui::GetItemRectSize());
    }
    if (n.has("tooltip")) md::tooltip(n.string("tooltip"));
    if (disabled) ImGui::EndDisabled();
    ImGui::PopID();
    return true;
}

std::uint64_t Renderer::elementId(const Node& node) { return m_types.elementId(node); }
ImGuiID Renderer::elementImGuiId(const Node& node) { return ImGui::GetID(idKey(elementId(node))); }
void Renderer::pushElementId(const Node& node) { ImGui::PushID(idKey(elementId(node))); }

void Renderer::renderChildren(const Node& node) {
    forEachChild(node, [&](int idx) { render(node.state(), idx); });
}

bool Renderer::isVisual(lua::State* state, int idx) const {
    return m_types.kind(Node{state, idx}.type()) == Types::Kind::Visual;
}

bool Renderer::fillsWidth(lua::State* state, int idx) { return m_types.fillsWidth(*this, Node{state, idx}); }

std::optional<ImVec2> Renderer::intrinsicSize(const Node& node, float maxWidth) {
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

void Renderer::renderStack(const Node& node) {
    const auto spacing = node.number("spacing");
    const float align = alignFactor(node.string("align", "start"));
    lua::State* state = node.state();
    const float boxAvail = layout::availableWidth();
    float avail = boxAvail;
    const float x0 = ImGui::GetCursorScreenPos().x;
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
    bool first = true;
    forEachChild(node, [&](int idx) {
        if (!isVisual(state, idx)) {
            render(state, idx);
            return;
        }
        Node c{state, idx};
        if (!c.boolean("visible", true)) return;
        if (!first && spacing)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + static_cast<float>(*spacing) * dpiScale() -
                                 ImGui::GetStyle().ItemSpacing.y);
        bool unseen = false;
        float fillOffset = 0.0f;
        // Every child starts at the stack's left edge, set explicitly: ImGui starts a new line at the window's
        // whole-pixel indent, which would put the children after the first a fraction of a pixel left of it in a
        // stack that starts at a fractional x (after a Text in a Row), and their sizes would no longer compose.
        ImGui::SetCursorScreenPos({x0, ImGui::GetCursorScreenPos().y});
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
                const ImGuiID key = elementImGuiId(c);
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
        if (unseen) {
            renderUnseen(state, idx);
        } else if (fillOffset > 0.0f) {
            const layout::BoxScope box{layout::insetBox(-fillOffset)};
            render(state, idx);
        } else {
            render(state, idx);
        }
        if (align > 0.0f) widest = std::max(widest, ImGui::GetItemRectSize().x);
        first = false;
    });
    if (fit) layout::recordSize(widestKey, {widest, 0.0f});
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
        if (!c.boolean("visible", true)) return;
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
