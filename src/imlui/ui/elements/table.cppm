// Table: a Material data table with a header row and selectable rows, as a typed element. The
// rows are Lua data (a sequence of sequences of cell values), read through frame.node() every frame.
export module imlui.ui.elements.table;

import std;

import lua;
import imgui;
import imlui.color;
import imlui.theme.scheme;
import imlui.widgets.card;
import imlui.widgets.table;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A Material data table with selectable rows.
///
/// Props:   columns = {}, rowHeight = 52, outlined = true, currentRow = 0 (1-based; 0 = none)
/// Raw:     rows (a list of lists), width
/// Signals: onRowClicked(self, row)
/// Two-way: currentRow
///
/// A row that isn't a list renders empty and is reported as a script error (when it is in view: only the rows in
/// view are submitted, through ImGuiListClipper). At most 511 columns (ImGui's limit) are shown; more are reported.
class Table final : public Element {
public:
    static void declare(TypeBuilder<Table>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::vector<std::string>& columns() const;
    void setColumns(std::vector<std::string> columns);
    [[nodiscard]] double rowHeight() const; ///< dp
    void setRowHeight(double height);
    [[nodiscard]] bool outlined() const;
    void setOutlined(bool outlined);
    [[nodiscard]] int currentRow() const;
    void setCurrentRow(int row);

private:
    void renderRows(const Frame& frame, float rowHeight, int columnCount);

    std::vector<std::string> m_columns;
    double m_rowHeight = 52.0;
    bool m_outlined = true;
    int m_currentRow = 0;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr std::size_t maxColumns = 511; // BeginTable() asserts columns < IMGUI_TABLE_MAX_COLUMNS (512)

} // namespace

void Table::declare(TypeBuilder<Table>& type) {
    type.doc("A Material data table: a 56dp header and selectable rows, in an outlined card.\n"
             "Raw `rows`: a list of lists of cell values (converted to strings); only the rows in view are drawn and "
             "read. Natural size: fills the available width (raw `width`), as tall as all its rows.");
    type.fillsWidth(); // an unset width fills
    type.property("columns", &Table::columns, &Table::setColumns)
        .doc("The header labels; they set the column count (nothing is drawn without columns). At most 511 are shown.")
        .property("rowHeight", &Table::rowHeight, &Table::setRowHeight)
        .doc("Every row's height (dp), 52 by default.")
        .property("outlined", &Table::outlined, &Table::setOutlined)
        .doc("Wraps the table in an outlined card (`true`).")
        .property("currentRow", &Table::currentRow, &Table::setCurrentRow)
        .doc("Two-way. The selected row, 1-based; 0 (default) means none.")
        .signal<int>("onRowClicked", {"row"})
        .doc("Called when a row is clicked, with its 1-based index (after the rows are drawn: the handler may change "
             "`rows`).");
}

void Table::render(Frame& frame) {
    if (m_columns.empty()) return;
    if (m_columns.size() > maxColumns)
        luaui::reportError(std::format("Table: {} columns, at most {} are shown", m_columns.size(), maxColumns));
    const int columnCount = static_cast<int>(std::min(m_columns.size(), maxColumns));
    const Scheme& s = frame.scheme();
    const float rowH = frame.dp(static_cast<float>(m_rowHeight)), headH = frame.dp(56);
    if (m_outlined) md::beginCard("##tablecard", md::CardVariant::Outlined, frame.size("width"), 0.0f);
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, s.outlineVariant.vec4());
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, s.outlineVariant.vec4());
    ImGui::PushStyleColor(ImGuiCol_Header, s.secondaryContainer.vec4());
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {frame.dp(16), 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {frame.dp(8), 0});
    const ImGuiTableFlags flags =
        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX;
    // An outlined table fills its card, which takes the width; a plain one takes it itself (unset: the available).
    const ImVec2 outerSize{m_outlined ? 0.0f : frame.resolvedWidth(), 0.0f};
    if (ImGui::BeginTable("##table", columnCount, flags, outerSize)) {
        for (int c = 0; c < columnCount; ++c)
            ImGui::TableSetupColumn(m_columns[static_cast<std::size_t>(c)].c_str());
        ImGui::TableNextRow(0, headH);
        for (int c = 0; c < columnCount; ++c) {
            ImGui::TableNextColumn();
            md::tableHeaderCell(m_columns[static_cast<std::size_t>(c)], headH);
        }
        renderRows(frame, rowH, columnCount);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (m_outlined) md::endCard();
}

void Table::renderRows(const Frame& frame, float rowH, int columnCount) {
    const luaui::Node& n = frame.node();
    lua::State* state = n.state();
    const Scheme& s = frame.scheme();
    // A click is handled after the loop: its handlers may change `rows` (remove the clicked row, ...), while the
    // loop walks this frame's rows table and count.
    int clicked = 0;
    if (n.push("rows") == lua::Type::Table) {
        const int rows = lua::gettop(state);
        const lua::Integer count = lua::rawlen(state, rows);
        // Clicking a row changes the selection, but the rest of this frame still shows the row selected when it
        // started, like the cells drawn before the click.
        const int selected = m_currentRow;
        // Only the rows in view are submitted (and their cells read): every row is rowH tall, so the clipper
        // measures the first and skips the others' space.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(std::min<lua::Integer>(count, std::numeric_limits<int>::max())));
        while (clipper.Step()) {
            for (lua::Integer i = clipper.DisplayStart + 1; i <= clipper.DisplayEnd; ++i) {
                const bool isRow = lua::rawgeti(state, rows, i) == lua::Type::Table;
                if (!isRow) luaui::reportError(std::format("Table: rows[{}] is not a list of cells", i));
                ImGui::TableNextRow(0, rowH);
                for (int c = 0; c < columnCount; ++c) {
                    ImGui::TableNextColumn();
                    std::string cell;
                    if (isRow) {
                        lua::rawgeti(state, -1, c + 1);
                        if (!lua::isnil(state, -1)) cell = lua::toStringAny(state, -1);
                        lua::pop(state);
                    }
                    if (c == 0) {
                        ImGui::PushID(static_cast<int>(i));
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        if (ImGui::Selectable("##row", selected == i,
                                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                              {0, rowH}))
                            clicked = static_cast<int>(i);
                        ImGui::SetCursorScreenPos(p);
                        ImGui::PopID();
                    }
                    md::tableCell(cell, rowH,
                                  selected == i ? std::optional<Color>{s.onSecondaryContainer} : std::nullopt);
                }
                lua::pop(state);
            }
        }
    }
    lua::pop(state);
    if (clicked > 0) {
        m_currentRow = clicked;
        notify("currentRow");
        emit("onRowClicked", m_currentRow);
    }
}

const std::vector<std::string>& Table::columns() const { return m_columns; }
void Table::setColumns(std::vector<std::string> columns) { m_columns = std::move(columns); }
double Table::rowHeight() const { return m_rowHeight; }
void Table::setRowHeight(double height) { m_rowHeight = height; }
bool Table::outlined() const { return m_outlined; }
void Table::setOutlined(bool outlined) { m_outlined = outlined; }
int Table::currentRow() const { return m_currentRow; }
void Table::setCurrentRow(int row) { m_currentRow = row; }

} // namespace imlui
