// Overlays and non-visual elements: Snackbar, Timer, ImGuiDemo.
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

void timer(Renderer&, const Node& n) {
    lua::State* state = n.state();
    if (!n.boolean("running", true)) {
        lua::pushnil(state);
        lua::setfield(state, n.index(), "__last");
        return;
    }
    const double now = ImGui::GetTime();
    lua::getfield(state, n.index(), "__last");
    const bool started = lua::isnumber(state, -1);
    double last = started ? lua::tonumber(state, -1) : now;
    lua::pop(state);
    const double interval = n.number("interval", 1000) / 1000.0;
    if (!started) {
        lua::pushnumber(state, now);
        lua::setfield(state, n.index(), "__last");
        requestFrameAfter(interval);
        if (n.boolean("triggeredOnStart", false)) n.emit("onTriggered");
        return;
    }
    if (now - last >= interval) {
        lua::pushnumber(state, now);
        lua::setfield(state, n.index(), "__last");
        const bool repeat = n.boolean("repeat", true);
        if (!repeat)
            n.set("running", false);
        else
            requestFrameAfter(interval);
        n.emit("onTriggered");
        return;
    }
    requestFrameAfter(interval - (now - last));
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
    renderer.registerElement("Timer", timer, false);
    renderer.registerElement("ImGuiDemo", imguiDemo, false);
}

} // namespace imlui
