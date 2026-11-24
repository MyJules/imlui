// Progress, data and custom drawing: ProgressBar, CircularProgress, BusyIndicator, Plot, Table, Canvas. All are
// typed elements, one module each (imlui.ui.elements.progress_bar, ...); this group module registers them.
module;

export module imlui.ui.elements.data;

import imlui.ui.renderer;
import imlui.types.registry;
import imlui.ui.elements.progress_bar;
import imlui.ui.elements.circular_progress;
import imlui.ui.elements.busy_indicator;
import imlui.ui.elements.plot;
import imlui.ui.elements.table;
import imlui.ui.elements.canvas;

export namespace imlui {

/// Registers this group's handler elements (none left: every data element is a typed element).
void registerDataElements(Renderer& renderer);

/// Registers this group's typed elements (one class per module, see docs/development.md, "Porting a built-in
/// to the typed API").
void registerDataTypes(TypeRegistry& types);

} // namespace imlui

module :private;

namespace imlui {

void registerDataElements(Renderer&) {}

void registerDataTypes(TypeRegistry& types) {
    types.registerType<ProgressBar>("ProgressBar");
    types.registerType<CircularProgress>("CircularProgress");
    types.registerType<BusyIndicator>("BusyIndicator");
    types.registerType<Plot>("Plot");
    types.registerType<Table>("Table");
    types.registerType<Canvas>("Canvas");
}

} // namespace imlui
