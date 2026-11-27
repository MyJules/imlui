// Color field: swatch + hex value that opens a color picker popup.
export module imlui.widgets.color_field;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.widgets.common;
import imlui.widgets.anchored_popup;
import imlui.widgets.text_field;
import imlui.ui.layout_box;

export namespace imlui::md {

/// Color swatch + hex, opening a picker popup. `c` is edited in place. Returns true when it changed.
bool colorField(const char* id, Color* c, std::string_view label, bool alpha, float width = 0.0f);
/// The size of colorField() with the same color, label and `width` (resolved like itemWidth()).
[[nodiscard]] ImVec2 colorFieldSize(Color c, std::string_view label, float width = 0.0f);

/// Applies the text of a color field's hex box to `*c`. While the user types (`final` false) only a complete
/// "#RRGGBB" or "#RRGGBBAA" counts, so "#123" on the way to "#123456" isn't taken for #112233 (nor "#1234" for an
/// RGBA color); when the edit ends (`final`: Enter, or the box lost focus) the short "#RGB" and "#RGBA" count too. An
/// alpha ("#RGBA", "#RRGGBBAA") is kept only with `alpha`, else the color is opaque. Returns whether `text` was taken.
bool applyHexInput(Color* c, std::string_view text, bool final, bool alpha);

/// The hex box under the picker, drawn by the skin: textField()'s parameters but the ID ("##hex").
using HexField = std::function<TextFieldResult(std::string& value, const TextFieldOptions& o)>;
/// A color field's flyout content, shared by the skins: ImGui's color picker `width` wide and, under it, the hex box
/// (`hexField`, editing by applyHexInput()). `hex` is toHex(*c) on the way in, and is kept so when the picker
/// changes the color. Call it inside the open popup. Returns whether `*c` changed.
bool colorPickerBody(Color* c, std::string& hex, bool alpha, float width, const HexField& hexField);

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

/// colorFieldSize() for a color whose toHex() is `hex`.
ImVec2 colorFieldSizeOf(std::string_view hex, std::string_view label, float width) {
    const float natural = dp(8) + dp(40) + dp(16) +
                          std::max(measure(TextStyle::BodyLarge, hex).x, measure(TextStyle::BodySmall, label).x) +
                          dp(16) + dp(24) + dp(16);
    return {itemWidth(width, natural), dp(56)};
}

/// The hex box being edited, and its text. The box's buffer is made from the color each frame, so a short form
/// typed but not applied yet ("#123") is kept here until the edit ends. One box at a time has the keyboard.
struct HexEdit {
    ImGuiID id = 0;
    std::string text;
};
HexEdit hexEdit;

} // namespace

ImVec2 colorFieldSize(Color c, std::string_view label, float width) { return colorFieldSizeOf(toHex(c), label, width); }

bool applyHexInput(Color* c, std::string_view text, bool final, bool alpha) {
    const bool hash = text.starts_with('#');
    const std::size_t digits = text.size() - (hash ? 1 : 0);
    if (!final && digits != 6 && digits != 8) return false;
    // The field holds hex digits only, so typing them without the '#' a color spec needs is fine here.
    const auto parsed = hash ? parseHex(text) : parseHex("#" + std::string{text});
    if (!parsed) return false;
    // #RGBA / #RRGGBBAA parse with an alpha, which only an alpha picker keeps.
    *c = alpha ? *parsed : parsed->withAlpha(1.0f);
    return true;
}

bool colorPickerBody(Color* c, std::string& hex, bool alpha, float width, const HexField& hexField) {
    const Color before = *c;
    // The popup takes its content's width: a fill in it doesn't read the popup's width.
    layout::pushBox(layout::shrinkToFitBox(width, width));
    float v[4] = {c->r, c->g, c->b, c->a};
    ImGui::SetNextItemWidth(width);
    ImGuiColorEditFlags fl = ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                             ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_PickerHueBar |
                             ImGuiColorEditFlags_NoInputs;
    if (alpha) fl |= ImGuiColorEditFlags_AlphaBar;
    else fl |= ImGuiColorEditFlags_NoAlpha;
    if (ImGui::ColorPicker4("##pick", v, fl)) {
        *c = {v[0], v[1], v[2], alpha ? v[3] : 1.0f};
        hex = toHex(*c);
    }
    ImGui::Dummy({0, dp(4)});
    TextFieldOptions o;
    o.label = "Hex";
    o.outlined = true;
    o.width = width;
    o.leadingIcon = "tag";
    const ImGuiID id = ImGui::GetID("##hex");
    std::string buf = hex; // fits the small-string buffer
    const TextFieldResult r = hexField(buf, o);
    if (r.changed) {
        hexEdit.id = id;
        hexEdit.text = buf;
        applyHexInput(c, buf, false, alpha);
    }
    if (r.accepted || r.committed) {
        // The box's buffer holds what it was given this frame; the text typed is the last edit's.
        applyHexInput(c, hexEdit.id == id ? std::string_view{hexEdit.text} : std::string_view{buf}, true, alpha);
        hexEdit.id = 0;
    }
    layout::popBox();
    return *c != before;
}

bool colorField(const char* id, Color* c, std::string_view label, bool alpha, float width) {
    const Scheme& s = scheme();
    std::string hex = toHex(*c);
    const ImVec2 size = colorFieldSizeOf(hex, label, width);
    const float w = size.x, h = size.y;
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
    strokeRoundRect(dl, p, {p.x + w, p.y + h}, rnd, dis ? disabledContainer() : (open ? s.primary : s.outlineVariant),
                    open ? dp(2) : dp(1));
    float layer = dis ? 0.0f : stateLayer(iid, hov, act);
    if (layer > 0) fillRoundRect(dl, p, {p.x + w, p.y + h}, rnd, s.onSurface.withAlpha(layer));
    const ImVec2 sw{p.x + dp(8), p.y + dp(8)};
    fillRoundRect(dl, sw, {sw.x + dp(40), sw.y + dp(40)}, dp(shape().small), c->withAlpha(1.0f));
    strokeRoundRect(dl, sw, {sw.x + dp(40), sw.y + dp(40)}, dp(shape().small), s.outlineVariant, dp(1));
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
        changed = colorPickerBody(c, hex, alpha, dp(240), [](std::string& value, const TextFieldOptions& o) {
            return textField("##hex", value, o);
        });
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

} // namespace imlui::md
