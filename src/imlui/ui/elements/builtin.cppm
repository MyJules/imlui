// registerBuiltinElements(): registers every built-in element family with a Renderer.
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
