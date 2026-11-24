// Frame: what an Element's render() gets. The render-time context of one element: its children, its common
// props (sizes, alignment, the `menu` popup), dp scaling, the ImGui draw list, the theme, frame requests, and
// the underlying luaui::Node as an escape hatch to everything the low-level element API can do.
//
// Children are visited as Frame::Child handles. A handle renders the child through the renderer (so the
// per-element wrapper applies: visible, enabled, group, tooltip, ID scope), tells whether it takes layout
// space, gives its node, type and stable ImGui ID, remembers its size from the last frame (immediate-mode
// layouts measure on the previous frame), and gives the C++ instance of a typed child with its properties
// synced (as<T>()).
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
        Child(Frame& frame, const luaui::Node& node);

        /// Renders the child at the ImGui cursor, through the renderer's per-element wrapper. False when it's
        /// hidden or of an unknown type. A rendered visual child is the last ImGui item afterwards (its group),
        /// and its size is stored for lastWidth()/lastHeight() on the next frame.
        bool render();
        /// Renders the child inside a layout box that ends at screen x `right`: its available width (fills, `align`,
        /// text wrapping, raw ImGui item widths) is `right - cursor x` instead of its parent's.
        bool render(float right);

        /// False for non-visual types (Timer, Dialog, ...): they take no layout space. Layouts render them
        /// where they are and don't place, count or space them.
        [[nodiscard]] bool isVisual() const;
        [[nodiscard]] const luaui::Node& node() const;
        /// The element type (`__type`), e.g. "Spacer".
        [[nodiscard]] std::string type() const;
        /// A stable ImGui ID for the child in the parent's ID scope (the key the built-in layouts use for
        /// per-child state in ImGui::GetStateStorage()).
        [[nodiscard]] ImGuiID id() const;

        /// Size of the child when it was last rendered through a Child handle (render()), or `fallback`.
        [[nodiscard]] float lastWidth(float fallback = 0.0f) const;
        [[nodiscard]] float lastHeight(float fallback = 0.0f) const;

        /// Syncs the declared properties of a typed child into its instance and returns it; nullptr when the
        /// child isn't a registered type. Rendering the child syncs again (setters only run on change).
        ScriptObject* syncProperties() const;

        /// The child's C++ instance as a T, with its properties synced: `child.as<Tab>()->title()`. nullptr
        /// when the child isn't a typed element or not a T.
        template <class T>
            requires std::derived_from<T, ScriptObject>
        [[nodiscard]] T* as() const {
            return dynamic_cast<T*>(syncProperties());
        }

    private:
        [[nodiscard]] ImGuiID heightKey() const;

        Frame& m_frame;
        luaui::Node m_node;
        ImGuiID m_id;
        bool m_visual;
    };

    /// `instances` (may be null) resolves typed children for Child::as(); the TypeRegistry passes its own.
    Frame(Renderer& renderer, const luaui::Node& node, const InstanceLookup* instances = nullptr);

    // ---- children

    /// Renders all children top to bottom (inline containers spliced in, like built-in containers).
    void renderChildren();
    /// Renders the children with the `spacing`/`align` props, like Column.
    void renderStack();
    /// The same for the children of another element, e.g. the content of the current Tab:
    /// `frame.renderStack(child.node())`. Uses that element's `spacing`/`align`.
    void renderChildren(const luaui::Node& parent);
    void renderStack(const luaui::Node& parent);

    /// Calls f(Frame::Child&) for every visible child element (inline containers such as Repeater and If
    /// spliced in, hidden children skipped), for custom layouts: place the cursor, then child.render().
    /// Non-visual children come too (child.isVisual() is false): render them, don't lay them out.
    template <class F>
    void forEachChild(F&& f) {
        forEachChild(m_node, f);
    }
    /// The same for the children of another element.
    template <class F>
    void forEachChild(const luaui::Node& parent, F&& f) {
        lua::State* state = parent.state();
        m_renderer.forEachChild(parent, [&](int idx) {
            const luaui::Node node{state, idx};
            if (!node.boolean("visible", true)) return;
            Child child{*this, node};
            f(child);
        });
    }

    /// Calls f(Frame::Child&) with the element held by prop `key` (e.g. ApplicationWindow's `menuBar`,
    /// NavigationRail's `header`) when the prop is a visible element. Returns whether f was called.
    template <class F>
    bool withElementProp(const char* key, F&& f) {
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
    /// |v| dp, 0 auto; `fillWidth = true` counts as -1 for width keys.
    [[nodiscard]] float size(const char* key) const;
    /// The same, with `ifAuto` in place of 0 (auto): `size("width", -1)` fills unless a width is set.
    [[nodiscard]] float size(const char* key, float ifAuto) const;
    /// The space available at the cursor: the width to the right edge of the layout box the parent gave (a fixed
    /// Column's width, a Row's share, a Grid cell, else the window's content region), and the height left in the
    /// window or scroll region.
    [[nodiscard]] ImVec2 available() const;
    /// The `width` / `height` prop resolved to pixels against available() by resolveSize(), the rule every element
    /// follows: fixed, fill, fill minus, or `natural` when unset.
    [[nodiscard]] float resolvedWidth(float natural) const;
    [[nodiscard]] float resolvedHeight(float natural) const;
    /// "Label###id": the visible text may change every frame without changing the ImGui ID.
    [[nodiscard]] static std::string labelId(std::string_view text, const char* id);
    /// "start" -> 0, "center" -> 0.5, "end" / "right" / "bottom" -> 1.
    [[nodiscard]] static float alignFactor(std::string_view align);

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
};

} // namespace imlui

module :private;

namespace imlui {

// ------------------------------------------------------------ Frame::Child

Frame::Child::Child(Frame& frame, const luaui::Node& node)
    : m_frame{frame}, m_node{node}, m_id{ImGui::GetID(node.ptr())},
      m_visual{frame.m_renderer.isVisual(node.state(), node.index())} {}

ImGuiID Frame::Child::heightKey() const {
    // Seeded with the child's own ID, so it is distinct from id() and from every other child's keys.
    ImGui::PushID(m_node.ptr());
    const ImGuiID key = ImGui::GetID("##imlui.lastHeight");
    ImGui::PopID();
    return key;
}

bool Frame::Child::render() {
    const bool rendered = m_frame.m_renderer.render(m_node.state(), m_node.index());
    if (rendered && m_visual) {
        const ImVec2 size = ImGui::GetItemRectSize();
        ImGuiStorage* storage = ImGui::GetStateStorage();
        storage->SetFloat(m_id, size.x);
        storage->SetFloat(heightKey(), size.y);
    }
    return rendered;
}

bool Frame::Child::render(float right) {
    const layout::BoxScope box{layout::boxTo(right)};
    return render();
}

bool Frame::Child::isVisual() const { return m_visual; }
const luaui::Node& Frame::Child::node() const { return m_node; }
std::string Frame::Child::type() const { return m_node.type(); }
ImGuiID Frame::Child::id() const { return m_id; }

float Frame::Child::lastWidth(float fallback) const { return ImGui::GetStateStorage()->GetFloat(m_id, fallback); }
float Frame::Child::lastHeight(float fallback) const {
    return ImGui::GetStateStorage()->GetFloat(heightKey(), fallback);
}

ScriptObject* Frame::Child::syncProperties() const {
    const InstanceLookup* instances = m_frame.m_instances;
    return instances && *instances ? (*instances)(m_node) : nullptr;
}

// ------------------------------------------------------------ Frame

Frame::Frame(Renderer& renderer, const luaui::Node& node, const InstanceLookup* instances)
    : m_renderer{renderer}, m_node{node}, m_instances{instances} {}

void Frame::renderChildren() { m_renderer.renderChildren(m_node); }
void Frame::renderStack() { m_renderer.renderStack(m_node); }
void Frame::renderChildren(const luaui::Node& parent) { m_renderer.renderChildren(parent); }
void Frame::renderStack(const luaui::Node& parent) { m_renderer.renderStack(parent); }

bool Frame::renderElementProp(const char* key) {
    bool rendered = false;
    withElementProp(key, [&](Child& child) { rendered = child.render(); });
    return rendered;
}

void Frame::attachMenu(bool clicked) { m_renderer.attachMenu(m_node, clicked); }

float Frame::size(const char* key) const { return sizeProp(m_node, key); }
float Frame::size(const char* key, float ifAuto) const {
    const float v = sizeProp(m_node, key);
    return v == 0.0f ? ifAuto : v;
}
ImVec2 Frame::available() const { return {layout::availableWidth(), layout::availableHeight()}; }
float Frame::resolvedWidth(float natural) const {
    return resolveSize(sizeProp(m_node, "width"), layout::availableWidth(), natural);
}
float Frame::resolvedHeight(float natural) const {
    return resolveSize(sizeProp(m_node, "height"), layout::availableHeight(), natural);
}
std::string Frame::labelId(std::string_view text, const char* id) { return imlui::labelId(text, id); }
float Frame::alignFactor(std::string_view align) { return imlui::alignFactor(align); }

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
