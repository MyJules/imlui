// Window elements: ApplicationWindow, Window, Dialog.
module;

#include <string>

export module imlui.ui.elements.windows;

import lua;
import imgui;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.theme;
import imlui.widgets;
import imlui.lua.node;
import imlui.ui.props;
import imlui.ui.renderer;

export namespace imlui {

void registerWindowElements(Renderer& renderer);

} // namespace imlui

module :private;

namespace imlui {

using luaui::Node;

namespace {

void renderMenuBar(Renderer& r, const Node& n) {
    n.push("menuBar");
    if (lua::istable(n.state(), -1)) r.render(n.state(), -1);
    lua::pop(n.state());
}

void applicationWindow(Renderer& r, const Node& n) {
    r.setWindowTitle(n.string("title", "imlui"));
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    const bool menu = n.has("menuBar");
    if (menu) flags |= ImGuiWindowFlags_MenuBar;
    if (!n.boolean("scrollable", true)) flags |= ImGuiWindowFlags_NoScrollbar;
    float pad = n.numf("padding", 24.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(pad), dp(pad)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    auto bg = n.color("color");
    if (bg) ImGui::PushStyleColor(ImGuiCol_WindowBg, bg->vec4());
    ImGui::Begin("##ApplicationWindow", nullptr, flags);
    if (bg) ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (menu) renderMenuBar(r, n);
    r.renderStack(n);
    ImGui::End();
}

void window(Renderer& r, const Node& n) {
    bool open = n.boolean("open", true);
    if (!open) return;
    std::string title = labelId(n.string("title", "Window"), "win");
    float x = n.numf("x", 60), y = n.numf("y", 60);
    float w = n.numf("width", 0), h = n.numf("height", 0);
    ImGui::SetNextWindowPos({dp(x), dp(y)}, ImGuiCond_FirstUseEver);
    if (w > 0 || h > 0) ImGui::SetNextWindowSize({dp(w), dp(h)}, ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    if (n.boolean("autoResize", w <= 0 && h <= 0)) flags |= ImGuiWindowFlags_AlwaysAutoResize;
    if (!n.boolean("titleBar", true)) flags |= ImGuiWindowFlags_NoTitleBar;
    if (!n.boolean("resizable", true)) flags |= ImGuiWindowFlags_NoResize;
    if (!n.boolean("movable", true)) flags |= ImGuiWindowFlags_NoMove;
    if (n.has("menuBar")) flags |= ImGuiWindowFlags_MenuBar;
    const bool closable = n.has("open");
    bool keep = true;
    const Scheme& s = theme().scheme;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, s.surfaceContainerLow.vec4());
    ImGui::PushStyleColor(ImGuiCol_TitleBg, s.surfaceContainer.vec4());
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, s.surfaceContainerHigh.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, dp(1));
    ImGui::PushStyleColor(ImGuiCol_Border, s.outlineVariant.vec4());
    const bool shown = ImGui::Begin(title.c_str(), closable ? &keep : nullptr, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (shown) {
        md::pushSurface(s.surfaceContainerLow);
        if (n.has("menuBar")) renderMenuBar(r, n);
        r.renderStack(n);
        md::popSurface();
    }
    ImGui::End();
    if (!keep) {
        n.set("open", false);
        n.emit("onClosed");
    }
}

void dialog(Renderer& r, const Node& n) {
    bool open = n.boolean("open", false);
    const bool was = open;
    std::string title = labelId(n.string("title", ""), "dlg");
    if (md::beginDialog(title.c_str(), &open, sizeProp(n, "width"), n.string("icon", ""))) {
        r.renderStack(n);
        md::endDialog();
    }
    if (open != was) {
        n.set("open", open);
        if (!open) n.emit("onClosed");
    }
}

} // namespace

void registerWindowElements(Renderer& renderer) {
    renderer.registerElement("ApplicationWindow", applicationWindow, false);
    renderer.registerElement("Window", window, false);
    renderer.registerElement("Dialog", dialog, false);
}

} // namespace imlui
