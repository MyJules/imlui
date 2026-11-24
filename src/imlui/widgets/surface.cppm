// Surface stack: the background color widgets are currently drawn on, so
// cutouts (e.g. chart markers) can match it.
export module imlui.widgets.surface;

import std;

import imgui;
import imlui.color;

export namespace imlui::md {

/// Color of the surface widgets are currently drawn on (for cutouts).
[[nodiscard]] Color surfaceColor();
void pushSurface(Color c);
void popSurface();

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {
std::vector<Color> surfaces;
} // namespace

Color surfaceColor() {
    if (!surfaces.empty()) return surfaces.back();
    return Color::from(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
}

void pushSurface(Color c) { surfaces.push_back(c); }

void popSurface() {
    if (!surfaces.empty()) surfaces.pop_back();
}

} // namespace imlui::md
