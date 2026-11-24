// Table: a Material data table with a header row and selectable rows, as a typed element (TypeBuilder). The
// rows are Lua data (a sequence of sequences of cell values), read through frame.node() every frame.
module;

#include <optional>
#include <string>
#include <utility>
#include <vector>

export module imlui.ui.elements.table;

import lua;
import imgui;
import imlui.color;
import imlui.theme.scheme;
import imlui.widgets.card;
import imlui.widgets.table;
import imlui.lua.node;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// Table { columns = {"Name", ...}, rows = {{...}, ...}, rowHeight = 52, outlined = true, width / fillWidth,
/// currentRow (two-way, 1-based, 0 = none), onRowClicked(row) }
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
    void renderRows(const Frame& frame, float rowHeight);

    std::vector<std::string> m_columns;
    double m_rowHeight = 52.0;
    bool m_outlined = true;
    int m_currentRow = 0;
};

} // namespace imlui

module :private;

namespace imlui {

void Table::declare(TypeBuilder<Table>& type) {
    type.property("columns", &Table::columns, &Table::setColumns)
        .property("rowHeight", &Table::rowHeight, &Table::setRowHeight)
        .property("outlined", &Table::outlined, &Table::setOutlined)
        .property("currentRow", &Table::currentRow, &Table::setCurrentRow)
        .signal("onRowClicked");
}

void Table::render(Frame& frame) {
    if (m_columns.empty()) return;
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
    if (ImGui::BeginTable("##table", static_cast<int>(m_columns.size()), flags)) {
        for (const auto& h : m_columns)
            ImGui::TableSetupColumn(h.c_str());
        ImGui::TableNextRow(0, headH);
        for (const auto& h : m_columns) {
            ImGui::TableNextColumn();
            md::tableHeaderCell(h, headH);
        }
        renderRows(frame, rowH);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (m_outlined) md::endCard();
}

void Table::renderRows(const Frame& frame, float rowH) {
    const luaui::Node& n = frame.node();
    lua::State* state = n.state();
    const Scheme& s = frame.scheme();
    if (n.push("rows") == lua::Type::Table) {
        const int rows = lua::gettop(state);
        const lua::Integer count = lua::rawlen(state, rows);
        // Clicking a row changes the selection, but the rest of this frame still shows the row selected when it
        // started, like the cells drawn before the click.
        const int selected = m_currentRow;
        for (lua::Integer i = 1; i <= count; ++i) {
            lua::rawgeti(state, rows, i);
            ImGui::TableNextRow(0, rowH);
            for (int c = 0; c < static_cast<int>(m_columns.size()); ++c) {
                ImGui::TableNextColumn();
                lua::rawgeti(state, -1, c + 1);
                const std::string cell = lua::isnil(state, -1) ? std::string{} : lua::toStringAny(state, -1);
                lua::pop(state);
                if (c == 0) {
                    ImGui::PushID(static_cast<int>(i));
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##row", selected == i,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                          {0, rowH})) {
                        m_currentRow = static_cast<int>(i);
                        notify("currentRow");
                        emit("onRowClicked", m_currentRow);
                    }
                    ImGui::SetCursorScreenPos(p);
                    ImGui::PopID();
                }
                md::tableCell(cell, rowH, selected == i ? std::optional<Color>{s.onSecondaryContainer} : std::nullopt);
            }
            lua::pop(state);
        }
    }
    lua::pop(state);
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
