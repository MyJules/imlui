// App bars, navigation and menus: TopAppBar, NavigationItem, NavigationBar, NavigationRail,
// ModalNavigationDrawer, MenuBar, Menu, MenuItem. Each is a typed element in its own module.
//
// NavigationItem is the one destination element for every navigation container: it draws itself in the style of
// the current NavigationScope (imlui.ui.navigation_scope), which the containers open around their children.
export module imlui.ui.elements.navigation;

import imlui.ui.renderer;
import imlui.types.registry;
import imlui.ui.elements.top_app_bar;
import imlui.ui.elements.navigation_item;
import imlui.ui.elements.navigation_bar;
import imlui.ui.elements.navigation_rail;
import imlui.ui.elements.modal_navigation_drawer;
import imlui.ui.elements.menu_bar;
import imlui.ui.elements.menu;
import imlui.ui.elements.menu_item;

export namespace imlui {

/// The group's handler elements: none left (every element is a typed one). Kept for registerBuiltinElements.
void registerNavigationElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerNavigationTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerNavigationElements(Renderer&) {}

void registerNavigationTypes(TypeRegistry& types) {
    types.registerType<TopAppBar>("TopAppBar");
    types.registerType<NavigationItem>("NavigationItem");
    types.registerType<NavigationBar>("NavigationBar");
    types.registerType<NavigationRail>("NavigationRail");
    types.registerType<ModalNavigationDrawer>("ModalNavigationDrawer");
    types.registerType<MenuBar>("MenuBar");
    types.registerType<Menu>("Menu");
    types.registerType<MenuItem>("MenuItem");
}

} // namespace imlui
