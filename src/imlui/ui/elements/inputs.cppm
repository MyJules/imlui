// Value inputs: Slider, SpinBox, TextField, TextArea, ComboBox, ColorPicker. All are typed elements, one module
// each (imlui.ui.elements.slider, ...); this group module registers them.
module;

export module imlui.ui.elements.inputs;

import imlui.ui.renderer;
import imlui.types.registry;
import imlui.ui.elements.slider;
import imlui.ui.elements.spin_box;
import imlui.ui.elements.text_field;
import imlui.ui.elements.text_area;
import imlui.ui.elements.combo_box;
import imlui.ui.elements.color_picker;

export namespace imlui {

/// Registers this group's handler elements (none left: every input is a typed element).
void registerInputElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerInputTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerInputElements(Renderer&) {}

void registerInputTypes(TypeRegistry& types) {
    types.registerType<Slider>("Slider");
    types.registerType<SpinBox>("SpinBox");
    types.registerType<TextField>("TextField");
    types.registerType<TextArea>("TextArea");
    types.registerType<ComboBox>("ComboBox");
    types.registerType<ColorPicker>("ColorPicker");
}

} // namespace imlui
