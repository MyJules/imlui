// Overlays, non-visual elements and the root handlers of the inline containers: Snackbar, ImGuiDemo, Timer,
// Repeater / If / Loader (typed elements, one module each).
export module imlui.ui.elements.system;

import imlui.ui.renderer;
import imlui.types.registry;
import imlui.ui.elements.snackbar;
import imlui.ui.elements.imgui_demo;
import imlui.ui.elements.timer;
import imlui.ui.elements.inline_container;

export namespace imlui {

/// Registers this group's handler-function elements (none left: they are all typed).
void registerSystemElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerSystemTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerSystemElements(Renderer&) {}

void registerSystemTypes(TypeRegistry& types) {
    types.registerType<Snackbar>("Snackbar");
    types.registerType<ImGuiDemo>("ImGuiDemo");
    types.registerType<Timer>("Timer");
    // The Lua side implements these (`__inline`, `__children`); parents splice their children in, so this
    // only renders when one is the root element.
    types.registerType<InlineContainer>("Repeater");
    types.registerType<InlineContainer>("If");
    types.registerType<InlineContainer>("Loader");
}

} // namespace imlui
