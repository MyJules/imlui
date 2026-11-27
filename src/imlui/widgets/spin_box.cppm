// Numeric field with decrement / increment icon buttons.
export module imlui.widgets.spin_box;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.typography;
import imlui.widgets.common;
import imlui.widgets.field;
import imlui.widgets.button;

export namespace imlui::md {

/// Numeric field with decrement / increment icon buttons. Returns true when the value changed.
bool spinBox(const char* id, double* v, double vmin, double vmax, double step, int decimals, std::string_view label,
             float width = 0.0f, bool outlined = false);
/// The size of spinBox() with the same `label` and `width` (resolved like itemWidth(): 0 is the natural 200dp).
[[nodiscard]] ImVec2 spinBoxSize(std::string_view label, float width, bool outlined);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

/// The input's top in a spin box `h` tall, for a `lineH` line.
float spinInputY(std::string_view label, bool outlined, float h, float lineH) {
    return (!label.empty() && !outlined) ? dp(24) + (dp(24) - lineH) * 0.5f : (h - lineH) * 0.5f;
}

} // namespace

ImVec2 spinBoxSize(std::string_view label, float width, bool outlined) {
    const float w = itemWidth(width == 0.0f ? dp(200) : width, dp(200));
    const float h = dp(56);
    const float lineH = linePixels(TextStyle::BodyLarge); // GetFontSize() inside pushFont(BodyLarge), without the push
    // The container, and the input on its line (taller only with a large font).
    return {w, std::max(h, spinInputY(label, outlined, h, lineH) + lineH)};
}

bool spinBox(const char* id, double* v, double vmin, double vmax, double step, int decimals, std::string_view label,
             float width, bool outlined) {
    const float w = spinBoxSize(label, width, outlined).x;
    const float h = dp(56);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    field::FieldFrame f;
    f.p = ImGui::GetCursorScreenPos();
    f.w = w;
    f.h = h;
    f.outlined = outlined;
    f.inputX = f.p.x + dp(16);
    // At least a pixel, also in a box narrower than its insets: ImGui takes a width <= 0 as "the available width minus
    // that", which would stretch the input past the box, over its siblings.
    f.inputW = std::max(1.0f, w - dp(16) - dp(96));
    bool changed = false;
    // The input's focus from the last frame: the container is drawn before the input, to stay under its text.
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID focusKey = ImGui::GetID("##focused");
    field::drawFieldBackground(f, st->GetBool(focusKey, false), field::containerHovered(f), field::scopeDisabled());

    pushFont(TextStyle::BodyLarge);
    const float lineH = linePixels(TextStyle::BodyLarge);
    const float inputY = spinInputY(label, outlined, h, lineH);
    ImGui::SetCursorScreenPos({f.inputX, f.p.y + inputY});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color{0, 0, 0, 0}.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGui::SetNextItemWidth(f.inputW);
    char fmt[16];
    std::snprintf(fmt, sizeof fmt, "%%.%df", std::max(0, decimals));
    if (ImGui::InputScalar("##value", ImGuiDataType_Double, v, nullptr, nullptr, fmt)) changed = true;
    const bool active = ImGui::IsItemActive();
    const bool hov = ImGui::IsItemHovered();
    const bool dis = itemDisabled();
    st->SetBool(focusKey, active);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PopFont();

    field::drawFieldDecor(f, label, active, hov, true, false, dis, {}, {}, ImGui::GetID("##lbl"), false);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::SetCursorScreenPos({f.p.x + w - dp(96) - dp(2), f.p.y + (h - dp(48)) * 0.5f});
    ImGui::BeginDisabled(*v <= vmin);
    if (iconButton("##dec", "remove")) { *v -= step; changed = true; }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 0);
    ImGui::BeginDisabled(*v >= vmax);
    if (iconButton("##inc", "add")) { *v += step; changed = true; }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    // from > to (or a NaN bound) is an empty range: like the Slider, leave the value alone instead of clamping.
    if (changed && vmin <= vmax) *v = std::clamp(*v, vmin, vmax);

    ImGui::SetCursorScreenPos(f.p);
    ImGui::Dummy({w, h});
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace imlui::md
