// Exposed dropdown menu: a text-field-looking anchor that opens a menu of items.
export module imlui.widgets.dropdown;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.widgets.common;
import imlui.widgets.field;
import imlui.widgets.menu;

export namespace imlui::md {

struct DropdownOptions {
    std::string_view label, leadingIcon, supporting;
    bool outlined = false, error = false;
    float width = 0;
};

/// Exposed dropdown menu (text-field look + menu). `current` is 0-based, -1 = none.
bool dropdown(const char* id, const std::vector<std::string>& items, int* current, const DropdownOptions& o,
              const std::vector<std::string>& icons = {});
/// The size of dropdown() with the same options (`o.width` resolved like itemWidth(): 0 is the natural 240dp).
[[nodiscard]] ImVec2 dropdownSize(const DropdownOptions& o);

} // namespace imlui::md

module :private;

namespace imlui::md {

ImVec2 dropdownSize(const DropdownOptions& o) {
    const float w = itemWidth(o.width == 0.0f ? dp(240) : o.width, dp(240));
    const float h = dp(56);
    // The supporting text's dummy is on the line below the field's (ImGui's next line: a whole pixel, the item
    // spacing below).
    if (o.supporting.empty()) return {w, h};
    return {w, std::trunc(h + ImGui::GetStyle().ItemSpacing.y) + dp(20)};
}

bool dropdown(const char* id, const std::vector<std::string>& items, int* current, const DropdownOptions& o,
              const std::vector<std::string>& icons) {
    const float w = dropdownSize(o).x;
    const float h = dp(56);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    field::FieldFrame f;
    f.p = ImGui::GetCursorScreenPos();
    f.w = w;
    f.h = h;
    f.outlined = o.outlined;
    const bool lead = hasIcon(o.leadingIcon);
    f.inputX = f.p.x + (lead ? dp(52) : dp(16));
    f.inputW = w - (f.inputX - f.p.x) - dp(52);

    const bool clicked = navButton("##field", {w, h});
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered();
    const bool focused = focusVisible();
    if (clicked) ImGui::OpenPopup("##menu");
    const bool open = ImGui::IsPopupOpen("##menu");
    field::drawFieldBackground(f, open, hov, dis);
    const bool has = *current >= 0 && *current < static_cast<int>(items.size());
    field::drawFieldDecor(f, o.label, open, hov, has, o.error, dis, o.leadingIcon,
                          open ? "arrow_drop_up" : "arrow_drop_down", ImGui::GetID("##lbl"), false);
    if (has) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Scheme& s = scheme();
        float y = (o.label.empty() || o.outlined) ? f.p.y + h * 0.5f : f.p.y + dp(24) + dp(12);
        dl->PushClipRect({f.inputX, f.p.y}, {f.inputX + f.inputW, f.p.y + h}, true);
        drawTextV(dl, TextStyle::BodyLarge, f.inputX, y, dis ? disabledContent() : s.onSurface, items[static_cast<std::size_t>(*current)]);
        dl->PopClipRect();
    }
    if (focused) focusRing(ImGui::GetWindowDrawList(), f.p, {f.p.x + w, f.p.y + h}, dp(shape().extraSmall));
    bool changed = false;
    if (beginAnchoredMenu("##menu", f.p, {f.p.x + w, f.p.y + h}, w)) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            ImGui::PushID(i);
            const auto idx = static_cast<std::size_t>(i);
            std::string_view ic = idx < icons.size() ? std::string_view{icons[idx]} : std::string_view{};
            if (menuItem("##it", items[idx], ic, {}, i == *current) && i != *current) { *current = i; changed = true; }
            if (i == *current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            ImGui::PopID();
        }
        endAnchoredMenu();
    }
    if (!o.supporting.empty()) {
        field::drawSupporting({f.p.x, f.p.y + h}, w, o.supporting, o.error, dis);
        ImGui::Dummy({w, dp(20)});
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

} // namespace imlui::md
