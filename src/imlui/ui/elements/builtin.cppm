// registerBuiltinTypes(): registers every built-in element type with a TypeRegistry, under its Lua name and its
// aliases. Each element class lives in its own module (imlui.ui.elements.<name>).
export module imlui.ui.elements.builtin;

import imlui.types.registry;
import imlui.ui.elements.application_window;
import imlui.ui.elements.busy_indicator;
import imlui.ui.elements.button;
import imlui.ui.elements.canvas;
import imlui.ui.elements.card;
import imlui.ui.elements.checkbox;
import imlui.ui.elements.chip;
import imlui.ui.elements.circular_progress;
import imlui.ui.elements.color_picker;
import imlui.ui.elements.column;
import imlui.ui.elements.combo_box;
import imlui.ui.elements.dialog;
import imlui.ui.elements.divider;
import imlui.ui.elements.grid;
import imlui.ui.elements.icon;
import imlui.ui.elements.icon_button;
import imlui.ui.elements.imgui_demo;
import imlui.ui.elements.inline_container;
import imlui.ui.elements.link;
import imlui.ui.elements.list_item;
import imlui.ui.elements.menu;
import imlui.ui.elements.menu_bar;
import imlui.ui.elements.menu_item;
import imlui.ui.elements.modal_navigation_drawer;
import imlui.ui.elements.navigation_bar;
import imlui.ui.elements.navigation_item;
import imlui.ui.elements.navigation_rail;
import imlui.ui.elements.plot;
import imlui.ui.elements.progress_bar;
import imlui.ui.elements.radio_button;
import imlui.ui.elements.radio_group;
import imlui.ui.elements.row;
import imlui.ui.elements.scroll_view;
import imlui.ui.elements.section;
import imlui.ui.elements.segmented_button;
import imlui.ui.elements.separator;
import imlui.ui.elements.slider;
import imlui.ui.elements.snackbar;
import imlui.ui.elements.spacer;
import imlui.ui.elements.spin_box;
import imlui.ui.elements.switch_toggle;
import imlui.ui.elements.tab;
import imlui.ui.elements.tab_bar;
import imlui.ui.elements.table;
import imlui.ui.elements.text;
import imlui.ui.elements.text_area;
import imlui.ui.elements.text_field;
import imlui.ui.elements.timer;
import imlui.ui.elements.top_app_bar;
import imlui.ui.elements.tree_node;
import imlui.ui.elements.window;

export namespace imlui {

/// Registers the built-in element types with `types`. Types registered afterwards under the same name replace
/// a built-in.
void registerBuiltinTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerBuiltinTypes(TypeRegistry& types) {
    // Windows and overlays
    types.registerType<ApplicationWindow>("ApplicationWindow");
    types.registerType<Window>("Window");
    types.registerType<Dialog>("Dialog");
    types.registerType<Snackbar>("Snackbar");
    types.registerType<ImGuiDemo>("ImGuiDemo");

    // Layout
    types.registerType<Column>("Column");
    types.registerType<Column>("Item");
    types.registerType<Row>("Row");
    types.registerType<Grid>("Grid");
    types.registerType<ScrollView>("ScrollView");
    types.registerType<ScrollView>("ListView"); // children come from model/delegate (Lua side, `__children`)
    types.registerType<Card>("Card");
    types.registerType<TabBar>("TabBar");
    types.registerType<Tab>("Tab"); // rendered by TabBar; a stray Tab renders its content inline
    types.registerType<Section>("Section");
    types.registerType<Section>("Expander");
    types.registerType<TreeNode>("TreeNode");
    // Implemented on the Lua side (`__inline`, `__children`): parents splice their children in, so these only
    // render themselves as the root element.
    types.registerType<InlineContainer>("Repeater");
    types.registerType<InlineContainer>("If");
    types.registerType<InlineContainer>("Loader");
    types.registerType<InlineContainer>("Responsive");

    // App bars, navigation and menus
    types.registerType<TopAppBar>("TopAppBar");
    types.registerType<NavigationItem>("NavigationItem");
    types.registerType<NavigationBar>("NavigationBar");
    types.registerType<NavigationRail>("NavigationRail");
    types.registerType<ModalNavigationDrawer>("ModalNavigationDrawer");
    types.registerType<MenuBar>("MenuBar");
    types.registerType<Menu>("Menu");
    types.registerType<MenuItem>("MenuItem");

    // Text and decoration
    types.registerType<Text>("Text");
    types.registerType<Text>("Label");
    types.registerType<Icon>("Icon");
    types.registerType<Link>("Link");
    types.registerType<Separator>("Separator");
    types.registerType<Divider>("Divider");
    types.registerType<Spacer>("Spacer");

    // Buttons and selection controls
    types.registerType<Button>("Button");
    types.registerType<IconButton>("IconButton");
    types.registerType<Checkbox>("Checkbox");
    types.registerType<Switch>("Switch");
    types.registerType<RadioButton>("RadioButton");
    types.registerType<RadioGroup>("RadioGroup");
    types.registerType<Chip>("Chip");
    types.registerType<SegmentedButton>("SegmentedButton");
    types.registerType<ListItem>("ListItem");

    // Value inputs
    types.registerType<Slider>("Slider");
    types.registerType<SpinBox>("SpinBox");
    types.registerType<TextField>("TextField");
    types.registerType<TextArea>("TextArea");
    types.registerType<ComboBox>("ComboBox");
    types.registerType<ColorPicker>("ColorPicker");

    // Progress, data and custom drawing
    types.registerType<ProgressBar>("ProgressBar");
    types.registerType<CircularProgress>("CircularProgress");
    types.registerType<BusyIndicator>("BusyIndicator");
    types.registerType<Plot>("Plot");
    types.registerType<Table>("Table");
    types.registerType<Canvas>("Canvas");

    // Non-visual
    types.registerType<Timer>("Timer");
}

} // namespace imlui
