// Frame: what an Element's render() gets. The render-time context of one element: its children,
// dp scaling, the ImGui draw list, the theme, frame requests, and the underlying luaui::Node as an
// escape hatch to everything the low-level element API can do.
module;

export module imlui.types.frame;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.theme;
import imlui.lua.node;
import imlui.ui.frame_request;
import imlui.ui.props;
import imlui.ui.renderer;

export namespace imlui {

class Frame {
public:
    Frame(Renderer& renderer, const luaui::Node& node);

    /// Renders all children top to bottom (inline containers spliced in, like built-in containers).
    void renderChildren();
    /// Renders children with the `spacing`/`align` props, like Column.
    void renderStack();
    /// Renders one child (as passed to forEachChild). False when it's hidden or unknown.
    bool render(const luaui::Node& child);

    /// Calls f(const luaui::Node& child) for every child element, for custom layouts. Call
    /// render(child) from f to draw it where you placed the cursor.
    template <class F>
    void forEachChild(F&& f) {
        lua::State* state = m_node.state();
        m_renderer.forEachChild(m_node, [&](int idx) {
            const luaui::Node child{state, idx};
            f(child);
        });
    }

    /// dp -> pixels at the current scale (monitor content scale x user zoom).
    [[nodiscard]] float dp(float v) const;
    /// A size prop in pixels, like the built-ins read `width`/`height`: > 0 fixed, -1 fill, < -1 fill
    /// minus, 0 auto; `fillWidth = true` counts as -1 for width keys.
    [[nodiscard]] float size(const char* key) const;

    /// The current window's draw list.
    [[nodiscard]] ImDrawList* drawList() const;

    /// Render another frame as soon as possible / after `seconds` (frames are rendered on demand).
    void requestFrame() const;
    void requestFrameAfter(double seconds) const;

    [[nodiscard]] const Theme& theme() const;
    [[nodiscard]] const Scheme& scheme() const;

    /// The element table, for anything the typed API doesn't cover (raw props, signals, menus).
    [[nodiscard]] const luaui::Node& node() const;
    [[nodiscard]] Renderer& renderer() const;

private:
    Renderer& m_renderer;
    const luaui::Node& m_node;
};

} // namespace imlui

module :private;

namespace imlui {

Frame::Frame(Renderer& renderer, const luaui::Node& node) : m_renderer{renderer}, m_node{node} {}

void Frame::renderChildren() { m_renderer.renderChildren(m_node); }
void Frame::renderStack() { m_renderer.renderStack(m_node); }
bool Frame::render(const luaui::Node& child) { return m_renderer.render(child.state(), child.index()); }

float Frame::dp(float v) const { return imlui::dp(v); }
float Frame::size(const char* key) const { return sizeProp(m_node, key); }

ImDrawList* Frame::drawList() const { return ImGui::GetWindowDrawList(); }

void Frame::requestFrame() const { imlui::requestFrame(); }
void Frame::requestFrameAfter(double seconds) const { imlui::requestFrameAfter(seconds); }

const Theme& Frame::theme() const { return imlui::theme(); }
const Scheme& Frame::scheme() const { return imlui::theme().scheme; }

const luaui::Node& Frame::node() const { return m_node; }
Renderer& Frame::renderer() const { return m_renderer; }

} // namespace imlui
