// Row: children side by side, as a typed element.
//
// Sizes are measured on the previous frame (immediate mode), so a layout settles within one frame.
module;

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

export module imlui.ui.elements.row;

import imgui;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Children side by side, measured on the previous frame.
///
/// Props:   spacing = unset (ImGui item spacing), align = "center" ("top", "bottom"),
///          justify = "start" ("center", "end", "spaceBetween"; ignored while there are fills)
/// Raw:     width; the children's `fill` (Spacer { fill = true } children share the remaining width)
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
    // Measured on the last frame.
    float m_height = 0.0f; ///< tallest child
    float m_fixed = 0.0f;  ///< width of the children that aren't fills, plus the gaps
    int m_fills = 0;       ///< Spacer { fill = true } children
    int m_count = 0;       ///< laid-out children
};

} // namespace imlui

module :private;

namespace imlui {

void Row::declare(TypeBuilder<Row>& type) {
    type.property("spacing", &Row::spacing, &Row::setSpacing)
        .property("align", &Row::align, &Row::setAlign)
        .property("justify", &Row::justify, &Row::setJustify);
}

void Row::render(Frame& frame) {
    const float sp = m_spacing ? frame.dp(static_cast<float>(*m_spacing)) : ImGui::GetStyle().ItemSpacing.x;
    const float availW = frame.resolvedWidth(ImGui::GetContentRegionAvail().x);
    const float free = std::max(0.0f, availW - m_fixed);
    const float fillW = m_fills > 0 ? free / static_cast<float>(m_fills) : 0.0f;
    float offset = 0.0f, extraGap = 0.0f;
    if (m_fills == 0) {
        if (m_justify == "center")
            offset = free * 0.5f;
        else if (m_justify == "end")
            offset = free;
        else if (m_justify == "spaceBetween" && m_count > 1)
            extraGap = free / static_cast<float>(m_count - 1);
    }

    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float top = start.y;
    float maxH = 0.0f, fixed = 0.0f;
    int fills = 0, count = 0;
    float x = start.x + offset;
    frame.forEachChild([&](Frame::Child& c) {
        if (!c.isVisual()) {
            c.render();
            return;
        }
        if (count > 0) x += sp + extraGap;
        if (c.type() == "Spacer" && c.node().boolean("fill", false)) {
            ImGui::SetCursorScreenPos({x, top});
            ImGui::Dummy({std::max(0.0f, fillW), 0.0f});
            x += std::max(0.0f, fillW);
            ++fills;
            ++count;
            return;
        }
        const float ch = c.lastHeight();
        float y = top;
        if (m_height > 0 && ch > 0) {
            if (m_align == "center")
                y = top + std::max(0.0f, (m_height - ch) * 0.5f);
            else if (m_align == "bottom")
                y = top + std::max(0.0f, m_height - ch);
        }
        ImGui::SetCursorScreenPos({x, y});
        c.render();
        const ImVec2 sz = ImGui::GetItemRectSize();
        maxH = std::max(maxH, sz.y);
        fixed += sz.x;
        x += sz.x;
        ++count;
    });
    if (count > 1) fixed += sp * static_cast<float>(count - 1);
    // Make the row's extent explicit (fills may end the row).
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy({std::max(1.0f, x - start.x), std::max(1.0f, maxH)});
    m_height = maxH;
    m_fixed = fixed;
    m_fills = fills;
    m_count = count;
}

std::optional<double> Row::spacing() const { return m_spacing; }
void Row::setSpacing(std::optional<double> spacing) { m_spacing = spacing; }
const std::string& Row::align() const { return m_align; }
void Row::setAlign(std::string align) { m_align = std::move(align); }
const std::string& Row::justify() const { return m_justify; }
void Row::setJustify(std::string justify) { m_justify = std::move(justify); }

} // namespace imlui
