// Color field: swatch + hex value that opens a color picker popup.
module;

#include <algorithm>
#include <string>
#include <string_view>

export module imlui.widgets.color_field;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.widgets.common;
import imlui.widgets.anchored_popup;
import imlui.widgets.text_field;

export namespace imlui::md {

/// Color swatch + hex, opening a picker popup. `c` is edited in place. Returns true when it changed.
bool colorField(const char* id, Color* c, std::string_view label, bool alpha, float width = 0.0f);

} // namespace imlui::md

module :private;

namespace imlui::md {

bool colorField(const char* id, Color* c, std::string_view label, bool alpha, float width) {
    const Scheme& s = scheme();
    const float h = dp(56);
    const std::string hex = toHex(*c);
    const float natural = dp(8) + dp(40) + dp(16) +
                          std::max(measure(TextStyle::BodyLarge, hex).x, measure(TextStyle::BodySmall, label).x) +
                          dp(16) + dp(24) + dp(16);
    const float w = itemWidth(width, natural);
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##swatch", {w, h});
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const ImGuiID iid = ImGui::GetItemID();
    if (clicked) ImGui::OpenPopup("##picker");
    const bool open = ImGui::IsPopupOpen("##picker");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = dp(shape().medium);
    dl->AddRect(p, {p.x + w, p.y + h}, col(dis ? disabledContainer() : (open ? s.primary : s.outlineVariant)), rnd,
                open ? dp(2) : dp(1));
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) dl->AddRectFilled(p, {p.x + w, p.y + h}, col(s.onSurface.withAlpha(layer)), rnd);
    const ImVec2 sw{p.x + dp(8), p.y + dp(8)};
    dl->AddRectFilled(sw, {sw.x + dp(40), sw.y + dp(40)}, col(c->withAlpha(1.0f)), dp(shape().small));
    dl->AddRect(sw, {sw.x + dp(40), sw.y + dp(40)}, col(s.outlineVariant), dp(shape().small), dp(1));
    const float tx = sw.x + dp(40) + dp(16);
    if (!label.empty()) {
        drawText(dl, TextStyle::BodySmall, {tx, p.y + dp(9)}, dis ? disabledContent() : s.onSurfaceVariant, label);
        drawText(dl, TextStyle::BodyLarge, {tx, p.y + dp(26)}, dis ? disabledContent() : s.onSurface, hex);
    } else {
        drawTextV(dl, TextStyle::BodyLarge, tx, p.y + h * 0.5f, dis ? disabledContent() : s.onSurface, hex);
    }
    drawIcon(dl, "palette", {p.x + w - dp(16) - dp(12), p.y + h * 0.5f}, 24.0f,
             dis ? disabledContent() : s.onSurfaceVariant);

    bool changed = false;
    // Picker (240dp) + 16dp padding on each side: the width assumed before the first open.
    const AnchoredPopup anchored = placeAnchoredPopup("##picker", p, {p.x + w, p.y + h}, dp(240) + dp(32), dp(4));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, s.surfaceContainerHigh.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(16), dp(16)});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(shape().large));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    const bool popup = ImGui::BeginPopup("##picker", ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    trackAnchoredPopup(anchored, popup);
    if (popup) {
        float v[4] = {c->r, c->g, c->b, c->a};
        ImGui::SetNextItemWidth(dp(240));
        ImGuiColorEditFlags fl = ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                                 ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_PickerHueBar |
                                 ImGuiColorEditFlags_NoInputs;
        if (alpha) fl |= ImGuiColorEditFlags_AlphaBar;
        else fl |= ImGuiColorEditFlags_NoAlpha;
        if (ImGui::ColorPicker4("##pick", v, fl)) {
            *c = {v[0], v[1], v[2], alpha ? v[3] : 1.0f};
            changed = true;
        }
        ImGui::Dummy({0, dp(4)});
        std::string buf = toHex(*c);
        TextFieldOptions o;
        o.label = "Hex";
        o.outlined = true;
        o.width = dp(240);
        o.leadingIcon = "tag";
        auto r = textField("##hex", buf, o);
        if (r.changed)
            if (auto nc = parseHex(buf)) { *c = *nc; changed = true; }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

} // namespace imlui::md
