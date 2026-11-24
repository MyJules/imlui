// Row: children side by side, as a typed element.
//
// A flex layout: children that fill (Frame::Child::fillsWidth(): a fill width, a type that fills by default, wrapping
// Text, Spacer { fill }) share the width the others leave, each rendered in a layout box of its share. The others'
// widths and the heights (for `align`) are the ones of the previous frame (Frame::Child::lastWidth / lastHeight,
// immediate mode); when a child renders at another size, the layout cache asks for another frame, so the row settles.
export module imlui.ui.elements.row;

import std;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.layout_box;

export namespace imlui {

/// Children side by side. Children that fill the width share what the others leave.
///
/// Props:   spacing = unset (ImGui item spacing), align = "center" ("top", "bottom"),
///          justify = "start" ("center", "end", "spaceBetween"; ignored while there are fills)
/// Raw:     width (unset: the available width; it bounds the children)
class Row final : public Element {
public:
    static void declare(TypeBuilder<Row>& type);

    void render(Frame& frame) override;

    [[nodiscard]] std::optional<double> spacing() const;
    void setSpacing(std::optional<double> spacing);
    [[nodiscard]] const std::string& align() const;
    void setAlign(std::string align);
    [[nodiscard]] const std::string& justify() const;
    void setJustify(std::string justify);

private:
    std::optional<double> m_spacing;
    std::string m_align = "center";
    std::string m_justify = "start";
};

} // namespace imlui

module :private;

namespace imlui {

void Row::declare(TypeBuilder<Row>& type) {
    type.property("spacing", &Row::spacing, &Row::setSpacing)
        .property("align", &Row::align, &Row::setAlign)
        .property("justify", &Row::justify, &Row::setJustify);
}

namespace {

/// A visual child as the row sees it before rendering.
struct Slot {
    bool fills;  ///< takes a share of the free width
    float lastW; ///< its width on the last frame (0 on its first; unused for fills)
};

} // namespace

void Row::render(Frame& frame) {
    const float sp = m_spacing ? frame.dp(static_cast<float>(*m_spacing)) : ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float rowW = frame.resolvedWidth(frame.available().x);
    const float rowRight = start.x + rowW;

    // Pass 1: which children fill, how wide the others were, and (to align them) how tall the tallest was.
    const bool alignsHeights = m_align == "center" || m_align == "bottom";
    std::vector<Slot> slots;
    float rowH = 0.0f;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) return;
        const bool fills = c.fillsWidth();
        slots.push_back({fills, fills ? 0.0f : c.lastWidth()});
        if (alignsHeights) rowH = std::max(rowH, c.lastHeight());
    });
    const int count = static_cast<int>(slots.size());
    float fixedW = 0.0f;
    int fills = 0;
    for (const Slot& s : slots) {
        if (s.fills)
            ++fills;
        else
            fixedW += s.lastW;
    }
    const float gaps = count > 1 ? sp * static_cast<float>(count - 1) : 0.0f;
    const float free = std::max(0.0f, rowW - fixedW - gaps);
    const float share = fills > 0 ? free / static_cast<float>(fills) : 0.0f;
    float offset = 0.0f, extraGap = 0.0f;
    if (fills == 0) {
        if (m_justify == "center")
            offset = free * 0.5f;
        else if (m_justify == "end")
            offset = free;
        else if (m_justify == "spaceBetween" && count > 1)
            extraGap = free / static_cast<float>(count - 1);
    }
    // What the children after each one need: their last widths (the shares of fills) and the gaps before them.
    std::vector<float> after(slots.size(), 0.0f);
    for (int i = count - 2; i >= 0; --i) {
        const auto next = static_cast<std::size_t>(i + 1);
        after[static_cast<std::size_t>(i)] =
            after[next] + sp + extraGap + (slots[next].fills ? 0.0f : slots[next].lastW);
    }

    // Pass 2: place and render. A fill renders in a box of its share; any other child's box ends where the children
    // after it start, so a fill inside it (a Column with a filling child) stops there.
    const float top = start.y;
    float maxH = 0.0f;
    float x = start.x + offset;
    std::size_t i = 0;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) {
            c.render();
            return;
        }
        if (i >= slots.size()) return; // the children changed between the passes (a binding with side effects)
        const Slot& slot = slots[i];
        if (i > 0) x += sp + extraGap;
        float y = top;
        if (alignsHeights) {
            const float ch = c.lastHeight();
            if (rowH > 0 && ch > 0) y = top + std::max(0.0f, (rowH - ch) * (m_align == "center" ? 0.5f : 1.0f));
        }
        ImGui::SetCursorScreenPos({x, y});
        const float right = slot.fills ? x + share : std::max(x, rowRight - after[i]);
        c.render(right);
        const ImVec2 sz = ImGui::GetItemRectSize();
        maxH = std::max(maxH, sz.y);
        x += slot.fills ? std::max(share, sz.x) : sz.x;
        ++i;
    });
    // Make the row's extent explicit (fills may end the row).
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy({std::max(1.0f, x - start.x), std::max(1.0f, maxH)});
}

std::optional<double> Row::spacing() const { return m_spacing; }
void Row::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
const std::string& Row::align() const { return m_align; }
void Row::setAlign(std::string align) { m_align = std::move(align); }
const std::string& Row::justify() const { return m_justify; }
void Row::setJustify(std::string justify) { m_justify = std::move(justify); }

} // namespace imlui
