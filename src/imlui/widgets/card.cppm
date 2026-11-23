// Cards: elevated, filled and outlined containers (ImGui child windows).
module;

#include <optional>
#include <string_view>
#include <vector>

export module imlui.widgets.card;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.widgets.common;
import imlui.widgets.surface;

export namespace imlui::md {

enum class CardVariant { Elevated, Filled, Outlined };

/// "elevated" (default), "filled", "outlined".
[[nodiscard]] CardVariant cardVariant(std::string_view n);

/// Begins a card container (auto-height unless `height` > 0). Pair with endCard().
void beginCard(const char* id, CardVariant v, float width = 0.0f, float padding = 16.0f, float height = 0.0f,
               std::optional<Color> color = std::nullopt, float rounding = -1.0f);
void endCard();

} // namespace imlui::md

module :private;

namespace imlui::md {

namespace {

struct CardFrame {
    ImGuiID sizeKey;  ///< where the card's height is stored for next frame's shadow
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
    const float w = itemWidth(width == 0.0f ? -1.0f : width, dp(320));
    Color bg = color.value_or(v == CardVariant::Filled     ? s.surfaceContainerHighest
                              : v == CardVariant::Outlined ? s.surface
                                                           : s.surfaceContainerLow);
    if (v == CardVariant::Elevated) {
        float lastH = ImGui::GetStateStorage()->GetFloat(key, 0.0f);
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
    float h = ImGui::GetWindowSize().y;
    ImGui::EndChild();
    if (!cards.empty()) {
        ImGui::GetStateStorage()->SetFloat(cards.back().sizeKey, h);
        cards.pop_back();
    }
}

} // namespace imlui::md
