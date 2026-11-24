// Renderer: walks the Lua element tree every frame and draws it with ImGui
// (through the Material widgets in imlui.widgets).
//
// Every element type ("Button", "Column", ...) is a typed Element registered with the TypeRegistry, which the
// Renderer reaches through the Renderer::Types interface (the registry implements it; this module can't import
// the registry, whose Frames render children through the Renderer). render() wraps each element (visible,
// enabled, layout group, tooltip, ID scope) and lets the registry sync and render its instance.
export module imlui.ui.renderer;

import std;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.widgets;
import imlui.image.image_cache;
import imlui.lua.errors;
import imlui.lua.node;
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

    /// Renders the element at stack index `idx`. Returns false if it was
    /// hidden (visible = false) or of an unknown type ("unknown element type" is reported).
    bool render(lua::State* state, int idx);

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
            if (!luaui::safeCall(state, 1, 1)) return;
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
                lua::getfield(state, -1, "__inline");
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

    /// True unless the element at `idx` is of a registered non-visual type (Timer, Dialog, ...).
    [[nodiscard]] bool isVisual(lua::State* state, int idx) const;

    /// Renders children top-to-bottom. `spacing` (dp) overrides the gap
    /// between visible items; `align` positions them horizontally.
    void renderStack(const luaui::Node& node);

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
    ImGui::PushID(n.ptr());
    const bool disabled = n.has("enabled") && !n.boolean("enabled", true);
    if (disabled) ImGui::BeginDisabled();
    // Every visual element is exactly one layout item (a group), so
    // composite elements (label + field, ...) behave inside Rows.
    const bool visual = kind == Types::Kind::Visual;
    if (visual) ImGui::BeginGroup();
    m_types.renderElement(*this, n);
    if (visual) ImGui::EndGroup();
    if (n.has("tooltip")) md::tooltip(n.string("tooltip"));
    if (disabled) ImGui::EndDisabled();
    ImGui::PopID();
    return true;
}

void Renderer::renderChildren(const Node& node) {
    forEachChild(node, [&](int idx) { render(node.state(), idx); });
}

bool Renderer::isVisual(lua::State* state, int idx) const {
    return m_types.kind(Node{state, idx}.type()) != Types::Kind::NonVisual;
}

void Renderer::renderStack(const Node& node) {
    const auto spacing = node.number("spacing");
    const float align = alignFactor(node.string("align", "start"));
    lua::State* state = node.state();
    ImGuiStorage* st = ImGui::GetStateStorage();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float x0 = ImGui::GetCursorScreenPos().x;
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
        const ImGuiID key = ImGui::GetID(c.ptr());
        if (align > 0.0f) {
            float w = st->GetFloat(key, 0.0f);
            ImGui::SetCursorScreenPos({x0 + std::max(0.0f, (avail - w) * align), ImGui::GetCursorScreenPos().y});
        }
        render(state, idx);
        if (align > 0.0f) st->SetFloat(key, ImGui::GetItemRectSize().x);
        first = false;
    });
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
