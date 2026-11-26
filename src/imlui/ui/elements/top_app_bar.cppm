// TopAppBar: the Material 3 top app bar (small, center-aligned, medium, large), as a typed element.
export module imlui.ui.elements.top_app_bar;

import std;

import imgui;
import imlui.color;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.widgets;
import imlui.skin;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// The Material top app bar. The children are actions, right-aligned in the 64dp top row by their widths, each
/// centered vertically by its height (intrinsic sizes, else last frame's: Frame::Child::expectedWidth / Height).
///
/// Props:   title = "", navigationIcon = "", variant = "small" ("center", "medium", "large"),
///          color = unset (the current surface)
/// Raw:     width
/// Signals: onNavigation(self) (the navigation icon was clicked)
class TopAppBar final : public Element {
public:
    static void declare(TypeBuilder<TopAppBar>& type);

    void render(Frame& frame) override;
    /// Its width (a fill of `maxWidth` unless set) and its variant's height (taller only for an action taller than
    /// the 64dp row). None while an action has no intrinsic size.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

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
    type.doc("The Material 3 top app bar: an optional navigation icon, a title and, as children, **actions** "
             "(`IconButton`s, ...) right-aligned in the 64dp top row, each centered vertically.\n"
             "Natural size: the available width (unless `width` is set), the `variant`'s height.")
        .fillsWidth(); // an unset width fills
    type.property("title", &TopAppBar::title, &TopAppBar::setTitle)
        .doc("The title: `titleLarge` for `\"small\"` and `\"center\"`, `headlineSmall` for `\"medium\"`, "
             "`headlineMedium` for `\"large\"`.")
        .property("navigationIcon", &TopAppBar::navigationIcon, &TopAppBar::setNavigationIcon)
        .doc("A Material icon name for a leading icon button (clicks fire `onNavigation`); `\"\"` for none.")
        .property("variant", &TopAppBar::variant, &TopAppBar::setVariant)
        .doc("`\"small\"` (default, 64dp), `\"center\"` (64dp, centered title), `\"medium\"` (112dp) or `\"large\"` "
             "(152dp); `medium` and `large` put the title below the top row.")
        .property("color", &TopAppBar::color, &TopAppBar::setColor)
        .doc("The container color; unset: the current surface.")
        .signal<>("onNavigation")
        .doc("Called when the navigation icon is clicked.");
}

void TopAppBar::render(Frame& frame) {
    const Scheme& s = frame.scheme();
    const bool medium = m_variant == "medium", large = m_variant == "large";
    const float barH = frame.dp(64);
    const float h = medium ? frame.dp(112) : large ? frame.dp(152) : barH;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = frame.resolvedWidth();
    ImDrawList* dl = frame.drawList();
    const Color bg = m_color.value_or(md::surfaceColor());
    dl->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::ColorConvertFloat4ToU32(bg.vec4()));

    float titleX = p.x + frame.dp(16);
    if (!m_navigationIcon.empty()) {
        ImGui::SetCursorScreenPos({p.x + frame.dp(4), p.y + (barH - frame.dp(48)) * 0.5f});
        if (skin().iconButton("##nav", m_navigationIcon, md::IconButtonVariant::Standard, false, false, std::nullopt))
            emit("onNavigation");
        titleX = p.x + frame.dp(56);
    }
    // Actions, right-aligned by their total width, each centered in the 64dp row (intrinsic sizes, else last
    // frame's). On an action's first frame without either, they render unseen.
    float total = 0.0f;
    bool unknown = false;
    frame.forEachChild([&](Frame::Child& child) {
        if (!child.isVisual()) return;
        unknown = unknown || !child.expectedSize(w).has_value();
        total += child.expectedWidth(w);
    });
    float x = p.x + w - frame.dp(4) - total;
    frame.forEachChild([&](Frame::Child& child) {
        if (!child.isVisual()) {
            child.render();
            return;
        }
        ImGui::SetCursorScreenPos({x, p.y + (barH - child.expectedHeight(w, frame.dp(48))) * 0.5f});
        const bool rendered = unknown ? child.renderUnseen() : child.render();
        // Not rendered (no instance, an unknown type, past maxDepth): no item was submitted, so the last item's rect
        // is the previous action's. It takes no width, and an empty item ends the cursor move.
        if (!rendered) ImGui::Dummy({0.0f, 0.0f});
        x += rendered ? ImGui::GetItemRectSize().x : 0.0f;
    });

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

std::optional<ImVec2> TopAppBar::intrinsicSize(const Frame& frame, float maxWidth) {
    const float barH = frame.dp(64);
    const float w = frame.widthIn(maxWidth);
    float h = m_variant == "medium" ? frame.dp(112) : m_variant == "large" ? frame.dp(152) : barH;
    // The actions are centered in the 64dp row, asked with the bar's width like render() asks them.
    bool known = true;
    frame.forEachChild([&](Frame::Child& child) {
        if (!child.isVisual() || !known) return;
        const std::optional<ImVec2> size = child.intrinsicSize(w);
        known = size.has_value();
        if (known) h = std::max(h, (barH - size->y) * 0.5f + size->y);
    });
    if (!known) return std::nullopt;
    return ImVec2{w, h};
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
