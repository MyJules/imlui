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
/// How many surfaces are pushed: to restore with restoreSurfaceDepth() when an error cut a render pass short.
[[nodiscard]] std::size_t surfaceDepth();
/// Drops the surfaces pushed above `depth` (what a render pass that threw left behind).
void restoreSurfaceDepth(std::size_t depth);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

struct Surface {
    Color color;
    int frame; ///< the ImGui frame it was pushed in
};

std::vector<Surface> surfaces; // main thread only, like all ImGui code

/// Every frame ends with an empty stack, unless a render pass threw past a popSurface() and nothing restored the
/// depth: drop what an earlier frame left.
void dropStale() {
    if (!surfaces.empty() && surfaces.front().frame != ImGui::GetFrameCount()) surfaces.clear();
}

} // namespace

Color surfaceColor() {
    dropStale();
    if (!surfaces.empty()) return surfaces.back().color;
    return Color::from(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
}

void pushSurface(Color c) {
    dropStale();
    surfaces.push_back({c, ImGui::GetFrameCount()});
}

void popSurface() {
    if (!surfaces.empty()) surfaces.pop_back();
}

std::size_t surfaceDepth() {
    dropStale();
    return surfaces.size();
}

void restoreSurfaceDepth(std::size_t depth) {
    if (surfaces.size() > depth) surfaces.resize(depth);
}

} // namespace imlui::md
