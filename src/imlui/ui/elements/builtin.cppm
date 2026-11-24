// registerBuiltinElements() / registerBuiltinTypes(): register every built-in element with a
// Renderer (handler functions) or a TypeRegistry (typed elements).
export module imlui.ui.elements.builtin;

import imlui.ui.renderer;
import imlui.ui.elements.windows;
import imlui.ui.elements.layout;
import imlui.ui.elements.navigation;
import imlui.ui.elements.content;
import imlui.ui.elements.controls;
import imlui.ui.elements.inputs;
import imlui.ui.elements.data;
import imlui.ui.elements.system;
import imlui.ui.elements.progress_bar;
import imlui.ui.elements.timer;
import imlui.types.registry;

export namespace imlui {

/// Registers every built-in element type with `renderer`.
void registerBuiltinElements(Renderer& renderer);

/// Registers the built-in typed elements (ProgressBar, Timer) with `types`.
void registerBuiltinTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerBuiltinElements(Renderer& renderer) {
    registerWindowElements(renderer);
    registerLayoutElements(renderer);
    registerNavigationElements(renderer);
    registerContentElements(renderer);
    registerControlElements(renderer);
    registerInputElements(renderer);
    registerDataElements(renderer);
    registerSystemElements(renderer);
}

void registerBuiltinTypes(TypeRegistry& types) {
    types.registerType<ProgressBar>("ProgressBar");
    types.registerType<Timer>("Timer");
}

} // namespace imlui
