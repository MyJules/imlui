// TopAppBar: the Material 3 top app bar (small, center-aligned, medium, large), as a typed element.
module;

#include <optional>
#include <string>

export module imlui.ui.elements.top_app_bar;

import imgui;
import imlui.color;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.widgets;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The Material top app bar. The children are actions, right-aligned in the 64dp top row, each centered
/// vertically by its height on the last frame.
///
/// Props:   title = "", navigationIcon = "", variant = "small" ("center", "medium", "large"),
///          color = unset (the current surface)
/// Raw:     width
/// Signals: onNavigation(self) (the navigation icon was clicked)
class TopAppBar final : public Element {
public:
    static void declare(TypeBuilder<TopAppBar>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& title() const;
    void setTitle(std::string title);
    /// An icon name; empty for none.
    [[nodiscard]] const std::string& navigationIcon() const;
    void setNavigationIcon(std::string icon);
    [[nodiscard]] const std::string& variant() const;
    void setVariant(std::string variant);
    /// The container color; unset: the current surface.
    [[nodiscard]] const std::optional<Color>& color() const;
    void setColor(std::optional<Color> color);

private:
    std::string m_title;
    std::string m_navigationIcon;
    std::string m_variant = "small";
    std::optional<Color> m_color;
};

} // namespace imlui

module :private;

namespace imlui {

void TopAppBar::declare(TypeBuilder<TopAppBar>& type) {
    type.property("title", &TopAppBar::title, &TopAppBar::setTitle)
        .property("navigationIcon", &TopAppBar::navigationIcon, &TopAppBar::setNavigationIcon)
        .property("variant", &TopAppBar::variant, &TopAppBar::setVariant)
        .property("color", &TopAppBar::color, &TopAppBar::setColor)
        .signal("onNavigation");
}

void TopAppBar::render(Frame& frame) {
    const Scheme& s = frame.scheme();
    const bool medium = m_variant == "medium", large = m_variant == "large";
    const float barH = frame.dp(64);
    const float h = medium ? frame.dp(112) : large ? frame.dp(152) : barH;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = frame.resolvedWidth(ImGui::GetContentRegionAvail().x);
    ImDrawList* dl = frame.drawList();
    const Color bg = m_color.value_or(md::surfaceColor());
    dl->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::ColorConvertFloat4ToU32(bg.vec4()));
    ImGuiStorage* st = ImGui::GetStateStorage();

    float titleX = p.x + frame.dp(16);
    if (!m_navigationIcon.empty()) {
        ImGui::SetCursorScreenPos({p.x + frame.dp(4), p.y + (barH - frame.dp(48)) * 0.5f});
        if (md::iconButton("##nav", m_navigationIcon)) emit("onNavigation");
        titleX = p.x + frame.dp(56);
    }
    // Actions, right-aligned by their total width on the last frame, each centered in the 64dp row.
    const ImGuiID widthKey = ImGui::GetID("##actionsW");
    float x = p.x + w - frame.dp(4) - st->GetFloat(widthKey, 0.0f), total = 0;
    frame.forEachChild([&](Frame::Child& child) {
        if (!child.isVisual()) {
            child.render();
            return;
        }
        ImGui::SetCursorScreenPos({x, p.y + (barH - child.lastHeight(frame.dp(48))) * 0.5f});
        child.render();
        const float cw = ImGui::GetItemRectSize().x;
        x += cw;
        total += cw;
    });
    st->SetFloat(widthKey, total);

    const ImU32 titleColor = ImGui::ColorConvertFloat4ToU32(s.onSurface.vec4());
    if (medium || large) {
        const TextStyle ts = large ? TextStyle::HeadlineMedium : TextStyle::HeadlineSmall;
        const float th = font(ts)->CalcTextSizeA(fontPixels(ts), 3.4e38f, 0.0f, m_title.c_str()).y;
        dl->AddText(font(ts), fontPixels(ts), {p.x + frame.dp(16), p.y + h - frame.dp(large ? 28 : 24) - th},
                    titleColor, m_title.c_str());
    } else {
        const TextStyle ts = TextStyle::TitleLarge;
        const ImVec2 tsz = font(ts)->CalcTextSizeA(fontPixels(ts), 3.4e38f, 0.0f, m_title.c_str());
        const float tx = m_variant == "center" ? p.x + (w - tsz.x) * 0.5f : titleX;
        dl->AddText(font(ts), fontPixels(ts), {tx, p.y + (barH - tsz.y) * 0.5f}, titleColor, m_title.c_str());
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
}

const std::string& TopAppBar::title() const { return m_title; }
void TopAppBar::setTitle(std::string title) { m_title = std::move(title); }
const std::string& TopAppBar::navigationIcon() const { return m_navigationIcon; }
void TopAppBar::setNavigationIcon(std::string icon) { m_navigationIcon = std::move(icon); }
const std::string& TopAppBar::variant() const { return m_variant; }
void TopAppBar::setVariant(std::string variant) { m_variant = std::move(variant); }
const std::optional<Color>& TopAppBar::color() const { return m_color; }
void TopAppBar::setColor(std::optional<Color> color) { m_color = color; }

} // namespace imlui
