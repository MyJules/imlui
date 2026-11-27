// The Material top app bar's variants. The TopAppBar element draws the bar itself (with the skin's
// Skin::topAppBarChrome), so this module only names the variants the skin and the element share.
export module imlui.widgets.top_app_bar;

export namespace imlui::md {

/// A top app bar's size and title placement; its Lua names ("small", "center", ...) are in imlui.ui.enum_names.
enum class TopAppBarVariant { Small, Center, Medium, Large };

} // namespace imlui::md
