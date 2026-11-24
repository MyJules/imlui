// Layout elements: the typed Column (Item), Row, Grid, ScrollView (ListView), Card, TabBar, Tab,
// Section (Expander) and TreeNode, one module each, and the handlers of the inline containers Repeater, If,
// Loader and Responsive (implemented in Lua).
module;

#include <initializer_list>

export module imlui.ui.elements.layout;

import imlui.lua.node;
import imlui.ui.renderer;
import imlui.types.registry;
import imlui.ui.elements.card;
import imlui.ui.elements.column;
import imlui.ui.elements.grid;
import imlui.ui.elements.row;
import imlui.ui.elements.scroll_view;
import imlui.ui.elements.section;
import imlui.ui.elements.tab;
import imlui.ui.elements.tab_bar;
import imlui.ui.elements.tree_node;

export namespace imlui {

void registerLayoutElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerLayoutTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerLayoutElements(Renderer& renderer) {
    // Inline containers are normally spliced into their parent by forEachChild; these handlers only run when
    // one is used as the root. Their children come from Lua (`__children`).
    for (const char* name : {"Repeater", "If", "Loader", "Responsive"})
        renderer.registerElement(name, [](Renderer& r, const luaui::Node& n) { r.renderChildren(n); });
}

void registerLayoutTypes(TypeRegistry& types) {
    types.registerType<Column>("Column");
    types.registerType<Column>("Item");
    types.registerType<Row>("Row");
    types.registerType<Grid>("Grid");
    types.registerType<ScrollView>("ScrollView");
    types.registerType<ScrollView>("ListView"); // children come from model/delegate (Lua side, `__children`)
    types.registerType<Card>("Card");
    types.registerType<TabBar>("TabBar");
    // Tabs are rendered by TabBar; a stray Tab renders its content inline.
    types.registerType<Tab>("Tab");
    types.registerType<Section>("Section");
    types.registerType<Section>("Expander");
    types.registerType<TreeNode>("TreeNode");
}

} // namespace imlui
