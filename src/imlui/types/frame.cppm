// Frame: what an Element's render() gets. The render-time context of one element: its children, its common
// props (sizes, alignment, the `menu` popup), dp scaling, the ImGui draw list, the theme, frame requests, and
// the underlying luaui::Node as an escape hatch to everything the low-level element API can do.
//
// Children are visited as Frame::Child handles. A handle renders the child through the renderer (so the
// per-element wrapper applies: visible, enabled, group, tooltip, ID scope), tells whether it takes layout
// space, gives its node, type and ImGui ID (made from its identity, which is never reused), gives its size from
// the last frame (the layout cache: immediate-mode layouts place children by the sizes they had on the previous
// frame, and get another frame when that was stale), and gives the C++ instance of a typed child with its
// properties synced (as<T>()).
export module imlui.types.frame;

import std;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.theme;
import imlui.image.image_cache;
import imlui.lua.node;
import imlui.types.script_object;
import imlui.ui.frame_request;
import imlui.ui.item_size;
import imlui.ui.layout_box;
import imlui.ui.layout_cache;
import imlui.ui.props;
import imlui.ui.renderer;

export namespace imlui {

class Frame {
public:
    /// Syncs the declared properties of a typed element table and returns its C++ instance; nullptr for an
    /// element that isn't a registered type (TypeRegistry::sync).
    using InstanceLookup = std::function<ScriptObject*(const luaui::Node&)>;

    /// A child element, valid only inside the forEachChild / withElementProp callback that gives it.
    class Child {
    public:
        Child(const Frame& frame, const luaui::Node& node);

        /// Renders the child at the ImGui cursor, through the renderer's per-element wrapper. False when it's
        /// hidden or of an unknown type. A rendered visual child is the last ImGui item afterwards (its group),
        /// and its size is recorded in the layout cache, for lastWidth()/lastHeight() on the next frame.
        bool render();
        /// Renders the child inside a layout box that ends at screen x `right`: its available width (fills, `align`,
        /// text wrapping, raw ImGui item widths) is `right - cursor x` instead of its parent's.
        bool render(float right);
        /// Renders the child measured but unseen (clipped away), and asks for another frame: for a layout that
        /// can't place it yet because expectedSize() doesn't know its size (its first frame), so it doesn't flash
        /// in the wrong place. Its size is in the cache on the next frame.
        bool renderUnseen();

        /// False for non-visual types (Timer, Dialog, ...): they take no layout space. Layouts render them
        /// where they are and don't place, count or space them.
        [[nodiscard]] bool isVisual() const;
        /// Whether the child fills the available width: a fill or fill-minus `width` (`fillWidth`), or no `width`
        /// on a type that fills by default (TypeBuilder::fillsWidth / fillsWidthWhen: Card, TextField, wrapping or
        /// aligned Text, Spacer { fill }, ...) or on an element that fills by its content
        /// (Element::contentFillsWidth: a Column holding a fill, a Row with fills). Layouts that share space (Row)
        /// give such children a share, read before rendering them.
        [[nodiscard]] bool fillsWidth() const;
        [[nodiscard]] const luaui::Node& node() const;
        /// The element type (`__type`), e.g. "Spacer".
        [[nodiscard]] std::string type() const;
        /// The child's ImGui ID in the parent's ID scope, made from its identity (Renderer::elementId: its
        /// instance's id, never reused, so a child that replaces a collected one at the same table address starts
        /// fresh): the ID of the scope it renders in, the key of its size in the layout cache
        /// (imlui.ui.layout_cache), and a key for per-child state in ImGui::GetStateStorage().
        [[nodiscard]] ImGuiID id() const;
        /// Pushes the child's ImGui ID scope, the one render() renders it in; pop with ImGui::PopID(). For a
        /// container that renders the child's children itself (TabBar renders the current Tab's content), so
        /// their ImGui state belongs to the child.
        void pushId() const;

        /// The child's intrinsic size with `maxWidth` available (Element::intrinsicSize), when it can tell before
        /// rendering: the built-in leaves, and stacks (Column, Row, Card) whose children all can. Pass the width it
        /// will render in: a fill's size depends on it.
        [[nodiscard]] std::optional<ImVec2> intrinsicSize(float maxWidth) const;
        /// The size the child will render at with `maxWidth` available, as far as is known before rendering it: its
        /// intrinsic size, else its size on the last frame (lastWidth / lastHeight), else nullopt (its first frame).
        [[nodiscard]] std::optional<ImVec2> expectedSize(float maxWidth) const;
        /// expectedSize()'s width / height, or `fallback`. Like lastWidth(), reading one tells the layout cache that
        /// the caller places the child by it (another frame follows when the child renders at another size).
        [[nodiscard]] float expectedWidth(float maxWidth, float fallback = 0.0f) const;
        [[nodiscard]] float expectedHeight(float maxWidth, float fallback = 0.0f) const;

        /// Size of the child when it was last rendered (the layout cache keeps it while the child is hidden), or
        /// `fallback` when it never was. Reading it tells the layout cache that the caller places the child by it:
        /// when the child then renders at another size (by more than half a pixel), another frame is requested, so
        /// the layout settles.
        [[nodiscard]] float lastWidth(float fallback = 0.0f) const;
        [[nodiscard]] float lastHeight(float fallback = 0.0f) const;

        /// Syncs the declared properties of a typed child into its instance and returns it; nullptr when the
        /// child isn't a registered type. Rendering the child later in the frame reuses this sync, unless Lua code
        /// that may have changed its table (a handler, a write-back) ran in between.
        ScriptObject* syncProperties() const;

        /// The child's C++ instance as a T, with its properties synced: `child.as<Tab>()->title()`. nullptr
        /// when the child isn't a typed element or not a T.
        template <class T>
            requires std::derived_from<T, ScriptObject>
        [[nodiscard]] T* as() const {
            return dynamic_cast<T*>(syncProperties());
        }

    private:
        const Frame& m_frame;
        luaui::Node m_node;
        ImGuiID m_id;
        bool m_visual;
        /// The last intrinsicSize() query (maxWidth, result): a layout asks for the width and the height.
        mutable std::optional<std::pair<float, std::optional<ImVec2>>> m_intrinsic;
    };

    /// `instances` (may be null) resolves typed children for Child::as(); the TypeRegistry passes its own.
    /// `fillsWidth`: the element's type fills the width when `width` is unset (TypeBuilder::fillsWidth /
    /// fillsWidthWhen), so size("width") and resolvedWidth() treat an unset width as a fill.
    Frame(Renderer& renderer, const luaui::Node& node, const InstanceLookup* instances = nullptr,
          bool fillsWidth = false);

    // ---- children

    /// Renders all children top to bottom (inline containers spliced in, like built-in containers).
    void renderChildren();
    /// Renders the children with the `spacing`/`align` props, like Column.
    void renderStack();
    /// The same for the children of another element, e.g. the content of the current Tab:
    /// `frame.renderStack(child.node())`. Uses that element's `spacing`/`align`.
    void renderChildren(const luaui::Node& parent);
    void renderStack(const luaui::Node& parent);
    /// The size renderStack() takes with `maxWidth` available, composed from the children's intrinsic sizes: for a
    /// container's intrinsicSize(). nullopt when a visual child has none; {0, 0} without visual children.
    [[nodiscard]] std::optional<ImVec2> stackSize(float maxWidth) const;
    [[nodiscard]] std::optional<ImVec2> stackSize(const luaui::Node& parent, float maxWidth) const;

    /// Calls f(Frame::Child&) for every visible child element (inline containers such as Repeater and If
    /// spliced in, hidden children skipped), for custom layouts: place the cursor, then child.render().
    /// Non-visual children come too (child.isVisual() is false): render them, don't lay them out.
    template <class F>
    void forEachChild(F&& f) const {
        forEachChild(m_node, f);
    }
    /// The same for the children of another element.
    template <class F>
    void forEachChild(const luaui::Node& parent, F&& f) const {
        lua::State* state = parent.state();
        m_renderer.forEachChild(parent, [&](int idx) {
            const luaui::Node node{state, idx};
            if (!node.boolean(luaui::keys::visible, true)) return;
            Child child{*this, node};
            f(child);
        });
    }

    /// Calls f(Frame::Child&) with the element held by prop `key` (e.g. ApplicationWindow's `menuBar`,
    /// NavigationRail's `header`) when the prop is a visible element. Returns whether f was called.
    template <class F>
    bool withElementProp(const char* key, F&& f) const {
        lua::State* state = m_node.state();
        const int top = lua::gettop(state);
        bool called = false;
        if (m_node.push(key) == lua::Type::Table) {
            const luaui::Node node{state, -1};
            if (!node.type().empty() && node.boolean("visible", true)) {
                Child child{*this, node};
                f(child);
                called = true;
            }
        }
        lua::settop(state, top);
        return called;
    }
    /// Renders the element held by prop `key` at the cursor. False when there is none or it's hidden.
    bool renderElementProp(const char* key);

    /// The `menu` prop (a Menu element) as a popup anchored to the last item: opens it when `clicked`, and
    /// renders it while it's open. Call it right after the widget that opens it.
    void attachMenu(bool clicked);

    // ---- common props

    /// A size prop in pixels, like the built-ins read `width`/`height`: > 0 fixed, -1 fill, < -1 fill minus
    /// |v| dp, 0 auto; `fillWidth = true` counts as -1 for width keys. An unset `width` is -1 (fill) for a type
    /// that fills by default (TypeBuilder::fillsWidth / fillsWidthWhen): the size policy lives in the type, so a
    /// widget given this value needs no default of its own.
    [[nodiscard]] float size(const char* key) const;
    /// The space available at the cursor: the width to the right edge of the layout box the parent gave (a fixed
    /// Column's width, a Row's share, a Grid cell, else the window's content region), and the height left in the
    /// window or scroll region.
    [[nodiscard]] ImVec2 available() const;
    /// The `width` / `height` prop (size()) resolved to pixels against available() by resolveSize(), the rule every
    /// element follows: fixed, fill, fill minus, or `natural` when unset (a type that fills by default fills).
    [[nodiscard]] float resolvedWidth(float natural = 0.0f) const;
    [[nodiscard]] float resolvedHeight(float natural = 0.0f) const;
    /// resolvedWidth() with `maxWidth` available instead of the layout box's width: for intrinsicSize(), which
    /// knows the width it will render in before its layout box exists.
    [[nodiscard]] float widthIn(float maxWidth, float natural = 0.0f) const;
    /// "Label###id": the visible text may change every frame without changing the ImGui ID.
    [[nodiscard]] static std::string labelId(std::string_view text, const char* id);
    /// Align::Start -> 0, Center -> 0.5, End -> 1 (imlui::alignFactor).
    [[nodiscard]] static float alignFactor(Align align);

    // ---- environment

    /// dp -> pixels at the current scale (monitor content scale x user zoom).
    [[nodiscard]] float dp(float v) const;
    /// The current window's draw list.
    [[nodiscard]] ImDrawList* drawList() const;

    /// Render another frame as soon as possible / after `seconds` (frames are rendered on demand).
    void requestFrame() const;
    void requestFrameAfter(double seconds) const;

    [[nodiscard]] const Theme& theme() const;
    [[nodiscard]] const Scheme& scheme() const;

    /// The image cache that Image elements draw from (Renderer::images()); null when the host set none.
    [[nodiscard]] ImageCache* images() const;

    /// Sets the native window title (ApplicationWindow's `title`).
    void setWindowTitle(std::string title);

    /// The element table, for anything the typed API doesn't cover (raw props, `__children`, painters).
    [[nodiscard]] const luaui::Node& node() const;
    /// The renderer. Prefer the Frame members; they cover what the built-in elements need.
    [[nodiscard]] Renderer& renderer() const;

private:
    Renderer& m_renderer;
    const luaui::Node& m_node;
    const InstanceLookup* m_instances;
    bool m_fillsWidth;
};

} // namespace imlui

module :private;

namespace imlui {

// ------------------------------------------------------------ Frame::Child

Frame::Child::Child(const Frame& frame, const luaui::Node& node)
    : m_frame{frame}, m_node{node}, m_id{frame.m_renderer.elementImGuiId(node)},
      m_visual{frame.m_renderer.isVisual(node.state(), node.index())} {}

// Every Child is made right after its `visible` was read as true (forEachChild, withElementProp, ListView), so the
// render doesn't read it again.
bool Frame::Child::render() { return m_frame.m_renderer.renderShown(m_node.state(), m_node.index()); }

bool Frame::Child::render(float right) {
    const layout::BoxScope box{layout::boxTo(right)};
    return render();
}

bool Frame::Child::renderUnseen() { return m_frame.m_renderer.renderUnseen(m_node.state(), m_node.index()); }

bool Frame::Child::isVisual() const { return m_visual; }
bool Frame::Child::fillsWidth() const {
    const float w = sizeProp(m_node, "width");
    if (w != 0.0f) return w < 0.0f;
    return m_frame.m_renderer.fillsWidth(m_node.state(), m_node.index());
}
const luaui::Node& Frame::Child::node() const { return m_node; }
std::string Frame::Child::type() const { return m_node.type(); }
ImGuiID Frame::Child::id() const { return m_id; }
void Frame::Child::pushId() const { m_frame.m_renderer.pushElementId(m_node); }

std::optional<ImVec2> Frame::Child::intrinsicSize(float maxWidth) const {
    if (!m_visual) return std::nullopt;
    if (!m_intrinsic || m_intrinsic->first != maxWidth)
        m_intrinsic.emplace(maxWidth, m_frame.m_renderer.intrinsicSize(m_node, maxWidth));
    return m_intrinsic->second;
}

std::optional<ImVec2> Frame::Child::expectedSize(float maxWidth) const {
    if (const std::optional<ImVec2> intrinsic = intrinsicSize(maxWidth)) return intrinsic;
    return layout::lastSize(m_id);
}

float Frame::Child::expectedWidth(float maxWidth, float fallback) const {
    const std::optional<ImVec2> size = expectedSize(maxWidth);
    const float w = size ? size->x : fallback;
    layout::assumeWidth(m_id, w);
    return w;
}

float Frame::Child::expectedHeight(float maxWidth, float fallback) const {
    const std::optional<ImVec2> size = expectedSize(maxWidth);
    const float h = size ? size->y : fallback;
    layout::assumeHeight(m_id, h);
    return h;
}

float Frame::Child::lastWidth(float fallback) const {
    const std::optional<ImVec2> last = layout::lastSize(m_id);
    const float w = last ? last->x : fallback;
    layout::assumeWidth(m_id, w);
    return w;
}
float Frame::Child::lastHeight(float fallback) const {
    const std::optional<ImVec2> last = layout::lastSize(m_id);
    const float h = last ? last->y : fallback;
    layout::assumeHeight(m_id, h);
    return h;
}

ScriptObject* Frame::Child::syncProperties() const {
    const InstanceLookup* instances = m_frame.m_instances;
    return instances && *instances ? (*instances)(m_node) : nullptr;
}

// ------------------------------------------------------------ Frame

Frame::Frame(Renderer& renderer, const luaui::Node& node, const InstanceLookup* instances, bool fillsWidth)
    : m_renderer{renderer}, m_node{node}, m_instances{instances}, m_fillsWidth{fillsWidth} {}

void Frame::renderChildren() { m_renderer.renderChildren(m_node); }
void Frame::renderStack() { m_renderer.renderStack(m_node); }
void Frame::renderChildren(const luaui::Node& parent) { m_renderer.renderChildren(parent); }
void Frame::renderStack(const luaui::Node& parent) { m_renderer.renderStack(parent); }
std::optional<ImVec2> Frame::stackSize(float maxWidth) const { return m_renderer.stackSize(m_node, maxWidth); }
std::optional<ImVec2> Frame::stackSize(const luaui::Node& parent, float maxWidth) const {
    return m_renderer.stackSize(parent, maxWidth);
}

bool Frame::renderElementProp(const char* key) {
    bool rendered = false;
    withElementProp(key, [&](Child& child) { rendered = child.render(); });
    return rendered;
}

void Frame::attachMenu(bool clicked) { m_renderer.attachMenu(m_node, clicked); }

float Frame::size(const char* key) const {
    const float v = sizeProp(m_node, key);
    if (v == 0.0f && m_fillsWidth && std::string_view{key} == "width") return -1.0f;
    return v;
}
ImVec2 Frame::available() const { return {layout::availableWidth(), layout::availableHeight()}; }
float Frame::resolvedWidth(float natural) const {
    return resolveSize(size("width"), layout::availableWidth(), natural);
}
float Frame::resolvedHeight(float natural) const {
    return resolveSize(size("height"), layout::availableHeight(), natural);
}
float Frame::widthIn(float maxWidth, float natural) const { return resolveSize(size("width"), maxWidth, natural); }
std::string Frame::labelId(std::string_view text, const char* id) { return imlui::labelId(text, id); }
float Frame::alignFactor(Align align) { return imlui::alignFactor(align); }

float Frame::dp(float v) const { return imlui::dp(v); }
ImDrawList* Frame::drawList() const { return ImGui::GetWindowDrawList(); }

void Frame::requestFrame() const { imlui::requestFrame(); }
void Frame::requestFrameAfter(double seconds) const { imlui::requestFrameAfter(seconds); }

const Theme& Frame::theme() const { return imlui::theme(); }
const Scheme& Frame::scheme() const { return imlui::theme().scheme; }

void Frame::setWindowTitle(std::string title) { m_renderer.setWindowTitle(std::move(title)); }

const luaui::Node& Frame::node() const { return m_node; }
Renderer& Frame::renderer() const { return m_renderer; }
ImageCache* Frame::images() const { return m_renderer.images(); }

} // namespace imlui
