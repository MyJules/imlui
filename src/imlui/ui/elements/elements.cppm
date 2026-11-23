// Umbrella for the built-in elements, plus registerBuiltinElements().
export module imlui.ui.elements;

export import imlui.ui.elements.windows;
export import imlui.ui.elements.layout;
export import imlui.ui.elements.navigation;
export import imlui.ui.elements.content;
export import imlui.ui.elements.controls;
export import imlui.ui.elements.inputs;
export import imlui.ui.elements.data;
export import imlui.ui.elements.system;

import imlui.ui.renderer;

export namespace imlui {

/// Registers every built-in element type with `renderer`.
void registerBuiltinElements(Renderer& renderer);

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

} // namespace imlui
