// Fluent snackbar: WinUI's InfoBar (informational) floating at the bottom of the viewport. The flyout fill with a
// flyout-stroke border, the control radius, an accent "info" icon, the message in body text (textPrimary), the action
// a standard button (fluent::button, variant outlined), the close button a standard icon button (fluent::iconButton:
// subtle, as high as a control).
//
// md::snackbar's behavior: `t` slides it in from 24dp lower and fades it, it returns whether the action was clicked
// and sets `*closed` when the close button was; the width comes from the content within 344-672dp, at most the
// viewport less 16dp on each side (md::snackbarWidth), and a message wider than its room wraps (md::snackbarLines),
// the bar growing by 20dp a line from 48dp.
export module imlui.skin.fluent.snackbar;

import std;

import imgui;
import imlui.color;
import imlui.skin.fluent.button;
import imlui.skin.fluent.common;
import imlui.skin.fluent.icon_button;
import imlui.theme.dpi;
import imlui.theme.fluent_colors;
import imlui.widgets.button;
import imlui.widgets.common;
import imlui.widgets.snackbar;
import imlui.widgets.surface;

export namespace imlui::fluent {

/// Draws the snackbar (md::snackbar's parameters); returns whether the action was clicked.
bool snackbar(const Look& look, std::string_view message, std::string_view action, float t, bool closeIcon,
              bool* closed);

/// Where the snackbar's parts lie, in pixels from the bar's top-left corner.
struct SnackbarLayout {
    float width = 0, height = 0;
    ImVec2 iconCenter{};
    float messageX = 0;
    float firstLineCenter = 0; ///< the first line's vertical center; the others follow every 20dp
    std::vector<std::string_view> lines;
    ImVec2 actionPos{}, actionSize{}; ///< a zero size: no action
    ImVec2 closePos{}, closeSize{};   ///< a zero size: no close button
};
/// The layout snackbar() draws on a viewport `viewportWidth` wide.
[[nodiscard]] SnackbarLayout snackbarLayout(const Look& look, std::string_view message, std::string_view action,
                                            bool closeIcon, float viewportWidth);

/// The action button's ImGui label (its ID inside the bar), in a buffer the next call reuses.
[[nodiscard]] const char* snackbarActionLabel(std::string_view action);

} // namespace imlui::fluent

module :private;

namespace imlui::fluent {

namespace {

constexpr float padStart = 16.0f;      ///< dp before the icon
constexpr float padEnd = 16.0f;        ///< dp after the last part (without a close button)
constexpr float padEndClose = 8.0f;    ///< dp after the close button
constexpr float iconGap = 12.0f;       ///< dp between the icon and the message
constexpr float actionGap = 12.0f;     ///< dp between the message and the action
constexpr float closeGap = 4.0f;       ///< dp before the close button
constexpr float lineHeight = 20.0f;    ///< dp a message line
constexpr float linePad = 14.0f;       ///< dp above and below the lines
constexpr float minHeight = 48.0f;     ///< dp, InfoBar's minimum
constexpr float slideDistance = 24.0f; ///< dp the bar slides in from

/// Multiplies the alpha of the vertices `dl` got from `from` on by `alpha` (the bar's fade: its parts draw with
/// explicit colors, which ignore ImGui's style alpha).
void fade(ImDrawList* dl, int from, float alpha) {
    if (alpha >= 1.0f) return;
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
        ImU32& col = dl->VtxBuffer.Data[i].col;
        const auto a = static_cast<float>(col >> 24) * std::clamp(alpha, 0.0f, 1.0f);
        col = (col & 0x00FFFFFFu) | (static_cast<ImU32>(std::lround(a)) << 24);
    }
}

} // namespace

const char* snackbarActionLabel(std::string_view action) {
    // Reused (the UI runs on one thread): the label is built twice a frame while a snackbar shows.
    static std::string label;
    label.assign(action).append("###snack_action");
    return label.c_str();
}

SnackbarLayout snackbarLayout(const Look& look, std::string_view message, std::string_view action, bool closeIcon,
                              float viewportWidth) {
    SnackbarLayout l;
    const float iconPx = dp(iconSize(look));
    const float start = dp(padStart) + iconPx + dp(iconGap);
    const float end = closeIcon ? dp(padEndClose) : dp(padEnd);
    if (!action.empty())
        l.actionSize = buttonSize(look, snackbarActionLabel(action), md::ButtonVariant::Outlined, {}, {});
    if (closeIcon) l.closeSize = iconButtonSize(look);
    float right = 0;
    if (!action.empty()) right += dp(actionGap) + l.actionSize.x;
    if (closeIcon) right += dp(closeGap) + l.closeSize.x;
    const float msgW = md::measure(bodyStyle, message).x;
    l.width = md::snackbarWidth(start + msgW + right + end, viewportWidth);
    const float msgRoom = std::max(dp(48), l.width - start - right - end);
    l.lines = md::snackbarLines(message, msgW > msgRoom ? msgRoom : md::hugeExtent);
    const float lineH = dp(lineHeight);
    l.height = std::max(dp(minHeight), static_cast<float>(l.lines.size()) * lineH + 2.0f * dp(linePad));
    l.firstLineCenter = l.lines.size() == 1 ? l.height * 0.5f : dp(linePad) + lineH * 0.5f;
    l.iconCenter = {dp(padStart) + iconPx * 0.5f, l.firstLineCenter};
    l.messageX = start;
    float x = l.width - end;
    if (closeIcon) {
        x -= l.closeSize.x;
        l.closePos = {x, (l.height - l.closeSize.y) * 0.5f};
        x -= dp(closeGap);
    }
    if (!action.empty()) l.actionPos = {x - l.actionSize.x, (l.height - l.actionSize.y) * 0.5f};
    return l;
}

bool snackbar(const Look& look, std::string_view message, std::string_view action, float t, bool closeIcon,
              bool* closed) {
    if (t <= 0.001f) return false;
    const Colors& c = look.colors;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float ease = 1.0f - (1.0f - t) * (1.0f - t);
    const SnackbarLayout l = snackbarLayout(look, message, action, closeIcon, vp->WorkSize.x);
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                             vp->WorkPos.y + vp->WorkSize.y - dp(16) + dp(slideDistance) * (1 - ease)},
                            ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowSize({l.width, l.height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const auto name = md::scopedWindowId("snackbar");
    ImGui::Begin(name.data(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);
    md::pushSurface(c.flyout);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int from = dl->VtxBuffer.Size;
    const ImVec2 p = ImGui::GetWindowPos();
    drawFlyout(dl, p, {p.x + l.width, p.y + l.height}, controlRadius(look), look);
    md::drawIcon(dl, "info", {p.x + l.iconCenter.x, p.y + l.iconCenter.y}, iconSize(look), c.accent);
    const float lineH = dp(lineHeight);
    for (std::size_t i = 0; i < l.lines.size(); ++i)
        md::drawTextV(dl, bodyStyle, p.x + l.messageX, p.y + l.firstLineCenter + lineH * static_cast<float>(i), c.text,
                      l.lines[i]);
    bool clicked = false;
    if (!action.empty()) {
        ImGui::SetCursorPos(l.actionPos);
        clicked = button(look, snackbarActionLabel(action), md::ButtonVariant::Outlined, l.actionSize, {});
    }
    if (closeIcon) {
        ImGui::SetCursorPos(l.closePos);
        if (iconButton(look, "##snack_close", "close", md::IconButtonVariant::Standard, false, false, {}) && closed)
            *closed = true;
    }
    fade(dl, from, ease);
    md::popSurface();
    ImGui::End();
    return clicked;
}

} // namespace imlui::fluent
