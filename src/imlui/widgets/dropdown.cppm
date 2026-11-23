// Exposed dropdown menu: a text-field-looking anchor that opens a menu of items.
module;

#include <string>
#include <string_view>
#include <vector>

export module imlui.widgets.dropdown;

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

} // namespace imlui::md

module :private;

namespace imlui::md {

bool dropdown(const char* id, const std::vector<std::string>& items, int* current, const DropdownOptions& o,
              const std::vector<std::string>& icons) {
    const float w = itemWidth(o.width == 0.0f ? dp(240) : o.width, dp(240));
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

    const bool clicked = ImGui::InvisibleButton("##field", {w, h});
    const bool dis = itemDisabled();
    const bool hov = ImGui::IsItemHovered();
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
        drawTextV(dl, TextStyle::BodyLarge, f.inputX, y, dis ? disabledContent() : s.onSurface, items[*current]);
        dl->PopClipRect();
    }
    bool changed = false;
    if (beginAnchoredMenu("##menu", f.p, {f.p.x + w, f.p.y + h}, w)) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            ImGui::PushID(i);
            std::string_view ic = i < static_cast<int>(icons.size()) ? std::string_view{icons[i]} : std::string_view{};
            if (menuItem("##it", items[i], ic, {}, i == *current) && i != *current) { *current = i; changed = true; }
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
