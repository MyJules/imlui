// Buttons and selection controls: Button, IconButton, Checkbox, Switch,
// RadioButton, RadioGroup, Chip, SegmentedButton, ListItem.
module;

export module imlui.ui.elements.controls;

import imlui.ui.renderer;
import imlui.types.registry;
import imlui.ui.elements.button;
import imlui.ui.elements.icon_button;
import imlui.ui.elements.checkbox;
import imlui.ui.elements.switch_toggle;
import imlui.ui.elements.radio_button;
import imlui.ui.elements.radio_group;
import imlui.ui.elements.chip;
import imlui.ui.elements.segmented_button;
import imlui.ui.elements.list_item;

export namespace imlui {

/// Every element of this group is a typed element now (registerControlTypes); kept for the group interface.
void registerControlElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerControlTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerControlElements(Renderer&) {}

void registerControlTypes(TypeRegistry& types) {
    types.registerType<Button>("Button");
    types.registerType<IconButton>("IconButton");
    types.registerType<Checkbox>("Checkbox");
    types.registerType<Switch>("Switch");
    types.registerType<RadioButton>("RadioButton");
    types.registerType<RadioGroup>("RadioGroup");
    types.registerType<Chip>("Chip");
    types.registerType<SegmentedButton>("SegmentedButton");
    types.registerType<ListItem>("ListItem");
}

} // namespace imlui
