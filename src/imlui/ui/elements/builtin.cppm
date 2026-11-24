// registerBuiltinElements() / registerBuiltinTypes(): register every built-in element with a
// Renderer (handler functions) or a TypeRegistry (typed elements). Each group module registers its own.
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
import imlui.types.registry;

export namespace imlui {

/// Registers the built-in element handlers with `renderer`.
void registerBuiltinElements(Renderer& renderer);

/// Registers the built-in typed elements with `types`. Call it after registerBuiltinElements(): a type
/// replaces a handler of the same name.
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
    registerWindowTypes(types);
    registerLayoutTypes(types);
    registerNavigationTypes(types);
    registerContentTypes(types);
    registerControlTypes(types);
    registerInputTypes(types);
    registerDataTypes(types);
    registerSystemTypes(types);
}

} // namespace imlui
