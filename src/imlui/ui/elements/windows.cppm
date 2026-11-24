// Window elements: ApplicationWindow, Window, Dialog (typed elements, one module each).
export module imlui.ui.elements.windows;

import imlui.ui.renderer;
import imlui.types.registry;
import imlui.ui.elements.application_window;
import imlui.ui.elements.window;
import imlui.ui.elements.dialog;

export namespace imlui {

/// Registers this group's handler-function elements (none left: they are all typed).
void registerWindowElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerWindowTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerWindowElements(Renderer&) {}

void registerWindowTypes(TypeRegistry& types) {
    types.registerType<ApplicationWindow>("ApplicationWindow");
    types.registerType<Window>("Window");
    types.registerType<Dialog>("Dialog");
}

} // namespace imlui
