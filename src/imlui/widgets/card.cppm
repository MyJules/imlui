// Cards: elevated, filled and outlined containers (ImGui child windows).
export module imlui.widgets.card;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.ui.layout_cache;
import imlui.widgets.common;
import imlui.widgets.surface;

export namespace imlui::md {

enum class CardVariant { Elevated, Filled, Outlined };

/// "elevated" (default), "filled", "outlined".
[[nodiscard]] CardVariant cardVariant(std::string_view n);

/// Begins a card container (auto-height unless `height` > 0). Pair with endCard(). `width` follows resolveSize
/// (itemWidth): 0 is the natural 320dp, -1 fills (the Card element passes -1 when its width is unset).
void beginCard(const char* id, CardVariant v, float width = 0.0f, float padding = 16.0f, float height = 0.0f,
               std::optional<Color> color = std::nullopt, float rounding = -1.0f);
void endCard();

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

struct CardFrame {
    ImGuiID sizeKey; ///< the card's size in the layout cache, for the next frame's shadow
};

std::vector<CardFrame> cards;

} // namespace

CardVariant cardVariant(std::string_view n) {
    if (n == "filled") return CardVariant::Filled;
    if (n == "outlined") return CardVariant::Outlined;
    return CardVariant::Elevated;
}

void beginCard(const char* id, CardVariant v, float width, float padding, float height, std::optional<Color> color,
               float rounding) {
    const Scheme& s = scheme();
    ImGuiID key = ImGui::GetID(id);
    const float rnd = rounding >= 0 ? dp(rounding) : dp(shape().medium);
    const float w = itemWidth(width, dp(320));
    Color bg = color.value_or(v == CardVariant::Filled     ? s.surfaceContainerHighest
                              : v == CardVariant::Outlined ? s.surface
                                                           : s.surfaceContainerLow);
    if (v == CardVariant::Elevated) {
        // The shadow is drawn before the card's content, so it takes the card's height from the last frame; another
        // frame follows when the height changed (or on the first, which has no shadow yet).
        const float lastH = layout::lastSize(key).value_or(ImVec2{}).y;
        layout::assumeHeight(key, lastH);
        if (lastH > 0) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            drawShadow(ImGui::GetWindowDrawList(), p, {p.x + w, p.y + lastH}, rnd, 1.0f);
        }
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg.vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, s.outlineVariant.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rnd);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, v == CardVariant::Outlined ? dp(1) : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {dp(padding), dp(padding)});
    ImGuiChildFlags cf = ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height <= 0) cf |= ImGuiChildFlags_AutoResizeY;
    if (v == CardVariant::Outlined) cf |= ImGuiChildFlags_Borders;
    ImGui::BeginChild(id, {w, height > 0 ? height : 0.0f}, cf, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    pushSurface(bg);
    cards.push_back({key});
}

void endCard() {
    popSurface();
    const ImVec2 size = ImGui::GetWindowSize();
    ImGui::EndChild();
    if (!cards.empty()) {
        layout::recordSize(cards.back().sizeKey, size);
        cards.pop_back();
    }
}

} // namespace imlui::md
