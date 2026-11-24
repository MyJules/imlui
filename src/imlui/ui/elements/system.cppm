// Overlays and non-visual elements: Snackbar, ImGuiDemo.
// (Timer is a typed element: imlui.ui.elements.timer.)
module;

#include <algorithm>

export module imlui.ui.elements.system;

import lua;
import imgui;
import imlui.widgets;
import imlui.lua.node;
import imlui.ui.frame_request;
import imlui.ui.renderer;

export namespace imlui {

void registerSystemElements(Renderer& renderer);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

// Snackbar { text, actionText, onAction, shownAt, duration (ms), closable }
// Normally driven by ui.snackbar(); rendered by the engine as an overlay.
void snackbar(Renderer&, const Node& n) {
    lua::State* state = n.state();
    const double shownAt = n.number("shownAt", -1.0);
    if (shownAt < 0) return;
    const double now = ImGui::GetTime();
    const double dur = n.number("duration", 4000) / 1000.0;
    const double age = now - shownAt;
    float t = static_cast<float>(std::min(1.0, age / 0.18));
    if (age > dur) t = static_cast<float>(std::max(0.0, 1.0 - (age - dur) / 0.15));
    if (age > dur + 0.15) {
        lua::pushnumber(state, -1);
        lua::setfield(state, n.index(), "shownAt");
        return;
    }
    // Animate the slide-in and the fade-out; in between, wake up when the fade starts.
    if (age < 0.18 || age > dur)
        requestFrame();
    else
        requestFrameAfter(dur - age);
    bool closed = false;
    if (md::snackbar(n.string("text", ""), n.string("actionText", ""), t, n.boolean("closable", false), &closed)) {
        n.emit("onAction");
        closed = true;
    }
    if (closed) {
        // fade out now
        lua::pushnumber(state, now - dur);
        lua::setfield(state, n.index(), "shownAt");
    }
}

void imguiDemo(Renderer&, const Node& n) {
    bool open = n.boolean("open", true);
    if (!open) return;
    ImGui::ShowDemoWindow(&open);
    if (!open) n.set("open", false);
}

} // namespace

void registerSystemElements(Renderer& renderer) {
    renderer.registerElement("Snackbar", snackbar, false);
    renderer.registerElement("ImGuiDemo", imguiDemo, false);
}

} // namespace imlui
