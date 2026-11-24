// Grid: children in a table with `columns` cells per row, as a typed element.
export module imlui.ui.elements.grid;

import std;

import lua;
import imgui;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.ui.renderer;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;
import imlui.ui.layout_box;

export namespace imlui {

/// A table-based grid: the children fill `columns` equal-width cells per row. A hidden child keeps its empty
/// cell; non-visual children take none. At most 511 columns (ImGui's limit); more are reported.
///
/// Props:   columns = 2, spacing = 8, border = false
/// Raw:     width
class Grid final : public Element {
public:
    static void declare(TypeBuilder<Grid>& type);

    void render(Frame& frame) override;

    [[nodiscard]] int columns() const;
    void setColumns(int columns);
    [[nodiscard]] float spacing() const; ///< dp between cells
    void setSpacing(float spacing);
    [[nodiscard]] bool border() const;
    void setBorder(bool border);

private:
    int m_columns = 2;
    float m_spacing = 8.0f;
    bool m_border = false;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr int maxColumns = 511; // BeginTable() asserts columns < IMGUI_TABLE_MAX_COLUMNS (512)

} // namespace

void Grid::declare(TypeBuilder<Grid>& type) {
    type.fillsWidth(); // an unset width fills
    type.property("columns", &Grid::columns, &Grid::setColumns)
        .property("spacing", &Grid::spacing, &Grid::setSpacing)
        .property("border", &Grid::border, &Grid::setBorder);
}

void Grid::render(Frame& frame) {
    if (m_columns > maxColumns)
        luaui::reportError(std::format("Grid: columns = {}, at most {} are allowed", m_columns, maxColumns));
    const int cols = std::clamp(m_columns, 1, maxColumns);
    ImGuiTableFlags f = ImGuiTableFlags_SizingStretchSame;
    if (m_border) f |= ImGuiTableFlags_Borders;
    const float pad = frame.dp(m_spacing) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {pad, pad});
    if (ImGui::BeginTable("##grid", cols, f, {frame.resolvedWidth(), 0.0f})) {
        // Not frame.forEachChild(): that skips hidden children, and a hidden child keeps its (empty) cell here,
        // so the other cells don't move when it toggles. Non-visual children (timers, dialogs) take no cell.
        Renderer& r = frame.renderer();
        lua::State* state = frame.node().state();
        r.forEachChild(frame.node(), [&](int idx) {
            if (!r.isVisual(state, idx)) {
                r.render(state, idx);
                return;
            }
            ImGui::TableNextColumn();
            const layout::BoxScope cell{layout::regionBox()}; // the cell is the child's box
            r.render(state, idx);
        });
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

int Grid::columns() const { return m_columns; }
void Grid::setColumns(int columns) { m_columns = columns; }
float Grid::spacing() const { return m_spacing; }
void Grid::setSpacing(float spacing) { m_spacing = spacing; }
bool Grid::border() const { return m_border; }
void Grid::setBorder(bool border) { m_border = border; }

} // namespace imlui
