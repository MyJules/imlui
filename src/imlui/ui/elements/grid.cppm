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
import imlui.ui.culling;
import imlui.ui.layout_box;
import imlui.ui.layout_cache;

export namespace imlui {

/// A table-based grid: the children fill `columns` equal-width cells per row. A hidden child keeps its empty
/// cell; non-visual children take none. At most 511 columns (ImGui's limit); more are reported. Cells out of view
/// are culled by whole rows unless `cull = false`.
///
/// Props:   columns = 2, spacing = 8, border = false
/// Raw:     width, cull (default true)
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
    const float width = frame.resolvedWidth();
    Renderer& r = frame.renderer();
    if (ImGui::BeginTable("##grid", cols, f, {width, 0.0f})) {
        // Not frame.forEachChild(): that skips hidden children, and a hidden child keeps its (empty) cell here,
        // so the other cells don't move when it toggles. Non-visual children (timers, dialogs) take no cell.
        //
        // Culling, by whole rows: a row whose height on the last frames (kept in the layout cache under a key made
        // from its first cell's child) puts it out of view is made that tall (cull::reserveTableRow) and its cells
        // aren't entered, so the rows after it start where they would. A row is measured as the distance to the
        // next row's top, so the last row is never culled. A row whose cells held the active item, the nav focus,
        // an open popup or a non-visual element is pinned (cull::pin) and renders on the next frame too.
        lua::State* state = frame.node().state();
        Renderer::Culling culling{r, frame.node(), width};
        int cell = cols;        // cells taken in the current row; `cols`: the next visual child starts a row
        bool rowCulled = false; // the current row is culled: its cells are skipped
        bool rowEstimated = false;    // ... at an estimated height (never measured): its height isn't recorded
        float knownRowHeights = 0.0f; // the heights of the rows laid out so far, but estimated ones
        int knownRows = 0;
        std::uint64_t rowProvisional = 0; // Renderer::provisionals() when the current row started
        ImGuiID rowKey = 0;     // the current row's key in the layout cache
        float rowTop = 0.0f;    // its top (screen y)
        cull::Watch rowWatch;   // ImGui's state when it started (pins)
        const auto endRow = [&] {
            if (rowKey != 0 && !rowCulled && cull::held(rowWatch)) cull::pin(rowKey);
        };
        r.forEachChild(frame.node(), [&](int idx) {
            if (!culling.next(idx)) {
                r.render(state, idx);
                return;
            }
            if (cell == cols) {
                cell = 0;
                if (!culling.pins()) {
                    ImGui::TableNextRow();
                } else {
                    endRow();
                    const ImGuiID last = rowKey;
                    const float lastTop = rowTop;
                    rowTop = cull::nextTableRow();
                    if (last != 0 && !rowEstimated) {
                        // Provisional when a cell held something laid out by an unsettled size, or the row was.
                        layout::recordSize(last, {0.0f, rowTop - lastTop}, r.provisionals() != rowProvisional);
                        knownRowHeights += rowTop - lastTop;
                        ++knownRows;
                    }
                    const luaui::Node c{state, idx};
                    rowKey = ImGui::GetID(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                        culling.key(c, false)))); // not the child's own ID: GetID hashes it again
                    rowCulled = false;
                    rowEstimated = false;
                    rowProvisional = r.provisionals();
                    if (culling.cullsBelow() && !cull::pinned(rowKey)) {
                        // Its settled height (only on a frame the grid culls in view too), else, for a row below
                        // the view, its last one once the frame's converging renders are taken: nothing in view is
                        // placed after it (Renderer::skipOutOfView).
                        std::optional<ImVec2> size;
                        if (culling.culls()) size = layout::settledSize(rowKey);
                        if (!size && cull::belowView(rowTop)) {
                            size = layout::lastSize(rowKey);
                            if (size && r.takeConvergeRenders(cols))
                                size.reset(); // rendered: its height settles
                            else if (size) {
                                r.noteProvisional();
                                r.requestConvergeFrame(); // the rest settle on the next frames
                            }
                        }
                        if (size && cull::outOfView(rowTop, rowTop + size->y)) {
                            cull::reserveTableRow(size->y);
                            rowCulled = true;
                        } else if (!size && knownRows > 0 && cull::belowView(rowTop) && !layout::lastSize(rowKey) &&
                                   !r.takeConvergeRenders(cols)) {
                            // Never measured (a long grid's first frame), below the view, past this frame's
                            // converging renders: the average height of the rows so far, measured on a later frame
                            // (asked for). A cell holding a non-visual element still renders (below).
                            cull::reserveTableRow(knownRowHeights / static_cast<float>(knownRows));
                            rowCulled = true;
                            rowEstimated = true;
                            r.noteProvisional();
                            r.requestConvergeFrame();
                        }
                    }
                    if (!rowCulled) rowWatch = cull::watch();
                }
            }
            ++cell;
            if (rowCulled) {
                // In a row laid out at an estimate, a cell whose subtree holds a non-visual element (a Dialog, a
                // Timer) renders anyway, in its column, so it pins what it holds (Renderer::holdsNonVisual).
                if (!rowEstimated || !culling.holdsNonVisual(luaui::Node{state, idx})) return;
                ImGui::TableSetColumnIndex(cell - 1);
            } else {
                ImGui::TableNextColumn();
            }
            const layout::BoxScope box{layout::regionBox()}; // the cell is the child's box
            r.render(state, idx);
        });
        if (culling.pins()) endRow();
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
