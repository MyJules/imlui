// Text and decoration: Text, Label, Icon, Link, Separator, Divider, Spacer. Each is a typed element in its own
// module (Label is Text under a second name).
export module imlui.ui.elements.content;

import imlui.ui.renderer;
import imlui.types.registry;
import imlui.ui.elements.text;
import imlui.ui.elements.icon;
import imlui.ui.elements.link;
import imlui.ui.elements.divider;
import imlui.ui.elements.separator;
import imlui.ui.elements.spacer;

export namespace imlui {

/// The group's handler elements: none left (every element is a typed one). Kept for registerBuiltinElements.
void registerContentElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerContentTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerContentElements(Renderer&) {}

void registerContentTypes(TypeRegistry& types) {
    types.registerType<Text>("Text");
    types.registerType<Text>("Label");
    types.registerType<Icon>("Icon");
    types.registerType<Link>("Link");
    types.registerType<Separator>("Separator");
    types.registerType<Divider>("Divider");
    types.registerType<Spacer>("Spacer");
}

} // namespace imlui
