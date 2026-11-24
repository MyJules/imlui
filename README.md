# imlui

Declarative, QML-like UI written in **Lua**, rendered by **C++20 modules** with
**Dear ImGui** and a **Material 3** theme. All UI (window title, size, layout,
theme) lives in Lua; C++ is the backend.

```lua
local s = state { count = 0 }
theme { seed = "#006A6A", dark = false }

return ApplicationWindow {
    title = "Counter", width = 420, height = 260,
    Column {
        spacing = 16,
        Text { text = function() return "Clicked " .. s.count .. " times" end, style = "headlineSmall" },
        Button { text = "Increment", onClicked = function() s.count = s.count + 1 end },
    },
}
```

## Documentation

Full docs are in [`docs/`](docs/README.md): [getting started](docs/getting-started.md),
[architecture](docs/architecture.md), [Lua API](docs/lua-api.md), [elements](docs/elements.md),
[registering C++ types](docs/extending.md),
[theming](docs/theming.md), [development](docs/development.md) and [status / changelog](docs/changelog.md).

## Using it

imlui is a library (CMake package `imlui`, version 0.1.0): an app adds it with `add_subdirectory` (e.g. a git
submodule), `FetchContent`, or `find_package` on an installed copy, and links `imlui::imlui`. The
[imlui-gallery](docs/getting-started.md#the-example-app-imlui-gallery) repository is the example app (every element,
a live theme editor, and C++ types). In short ([Getting started](docs/getting-started.md#1-adding-imlui-to-your-project)
has the details):

```cmake
# one of:
add_subdirectory(vendor/imlui)                          # git submodule add -b material-ui <url> vendor/imlui

include(FetchContent)
FetchContent_Declare(imlui GIT_REPOSITORY <url> GIT_TAG <commit> FIND_PACKAGE_ARGS 0.1 CONFIG)
FetchContent_MakeAvailable(imlui)

find_package(imlui 0.1 CONFIG REQUIRED)                 # after cmake --install of imlui; CMAKE_PREFIX_PATH=<prefix>

add_subdirectory(src)                                   # the app's targets: src/CMakeLists.txt
```

```cmake
# src/CMakeLists.txt
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE imlui::imlui)
imlui_set_warnings(my_app)
imlui_set_hardening(my_app)
imlui_add_resources(my_app resources/resources.lua NAME my_app) # resources/main.lua etc., embedded as app:/
```

```cpp
import imlui;
import imlui.embedded.my_app;

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, imlui::embedded::my_app::packages())};
    return engine.run();
}
```

## Build and test the library (clang + Ninja)

```
cmake --preset clang-debug
cmake --build build/clang-debug
ctest --preset clang-debug               # ctest ... -L package: only the package tests (slow the first time)
cmake --install build/clang-debug --prefix <prefix>   # the package for find_package(imlui)
```

Dependencies are vendored in `external/` (Dear ImGui 1.92.9b, Lua 5.4.8, GLFW 3.4, spdlog 1.15.3, stb_image 2.34,
zstd 1.5.7) and are built as plain libraries. Project code only uses them through module wrappers.

`external/fonts/` holds Roboto Regular and Medium plus Material Icons Round (all Apache 2.0). The library embeds
them and `src/resources/imlui.lua` (zstd-compressed, listed in the resource manifest `src/resources/resources.lua`),
and `imlui_add_resources` embeds an app's own files the same way, from its own manifest, so an executable runs on
its own; a `.pak` archive or loose files next to it still override.
ImGui 1.92 rasterizes glyphs on demand, so every text style and icon size stays sharp, including
when the theme's `fontScale` changes.

Scripts **hot-reload** when you save any `.lua` file next to the script (or a file it read). In dev builds
(`IMLUI_DEV_PATHS=ON`, the default) every resource package knows its source folder, so `src/resources/` and the
app's scripts are read from the source tree and you don't need to rebuild; nothing is embedded then
(`IMLUI_EMBED_RESOURCES` follows, `OFF`), so editing a resource doesn't even make the next build do work. A build
you ship is configured with `-DIMLUI_DEV_PATHS=OFF`: it embeds everything and holds no source paths. Hosts name no file paths: the app is the
resource `app:/main.lua`, e.g. `imlui::defaultConfig(argc, argv, imlui::embedded::my_app::packages())`.

## Layout

| Path | What |
|---|---|
| `src/wrap/` | third-party wrappers: `lua` (+ `lua.stack_guard`), `imgui`, `imlui.log` (spdlog), `imlui.platform.window` (GLFW + OpenGL3 + ImGui backends) |
| `src/imlui/color/` | `imlui.color.*`: colors and hex, OKLCH, tonal palettes |
| `src/imlui/theme/` | `imlui.theme.*`: Material 3 scheme from a seed, typography, icons, DPI, mapping onto ImGuiStyle |
| `src/imlui/lua/` | `imlui.lua.*`: Lua bridge (`Node` bindings and signals, values, errors, theme tables, Canvas painter, `imlui.native`) |
| `src/imlui/widgets/` | `imlui.widgets.*`: Material widgets drawn with ImGui (buttons, selection, slider, fields, menus, ...) |
| `src/imlui/ui/` | `imlui.ui.renderer` (walks the Lua element tree every frame and renders each element through the type registry), `imlui.ui.props`, `imlui.ui.frame_request`, `imlui.ui.navigation_scope`, `imlui.ui.size_class` |
| `src/imlui/ui/elements/` | `imlui.ui.elements.*`: the built-in elements, one typed `Element` class per module (`button.cppm`, `row.cppm`, ...), and `registerBuiltinTypes` (`builtin.cppm`) |
| `src/imlui/types/` | `imlui.types.*`: the C++ type system: `ScriptObject`, `Element`, `Frame`, `TypeBuilder`, `LuaConvert`, `TypeRegistry` |
| `src/imlui/resources/` | `imlui.resources*`: resource names (`app:/main.lua`), the `Mount` interface, `DirectoryMount`, `EmbeddedMount`, `PakMount` and the `.pak` format, `Resources` (stacked mounts) |
| `tools/` | build-time tools: `imlui_rc` (embeds the files a Lua resource manifest lists, for `imlui_add_resources`) and `imlui_pack` (builds `.pak` archives, `imlui_add_pak`) |
| `cmake/` | the helpers an app calls after adding imlui (in every mode): `imlui_add_resources`/`imlui_add_pak` (`ImluiResources.cmake`), `imlui_set_warnings`/`imlui_set_hardening` (`ImluiWarnings.cmake`); sanitizers, the `tidy` target, the install rules and package config (`ImluiInstall.cmake`, `imluiConfig.cmake.in`) |
| `src/imlui/app/` | `imlui.app.*`: engine (window, Lua state, frame loop, hot reload) and its config |
| `src/imlui/imlui.cppm` | `import imlui;` umbrella |
| `tests/` | GoogleTest unit tests and the smoke test (`ctest --preset clang-debug`); `smoke_host/` is a tiny test app host, `data/` the fixtures (`smoke_app/`, test images), `package/` the package tests (a consumer app built with add_subdirectory, FetchContent and find_package) |
| `src/CMakeLists.txt` | the library targets `imlui_lua` and `imlui` (`imlui::imlui`; module globs), the embedding of `src/resources/` and their install rules |
| `src/resources/` | the library's resources: `resources.lua` (manifest: this folder at `imlui:/`, the fonts from `external/fonts/` at `imlui:/fonts/`) and `imlui.lua`, the DSL (elements, `state`, `bind`, `Repeater`, `theme`, timers) |

## Concepts

- **Elements** are tables: `Button { text = "OK", onClicked = fn }`. Child elements go in the array part.
- **Property values**:
  - plain values
  - a `function(self)`, which is a *binding* re-evaluated every frame
  - `bind(state, "key")`, a *two-way binding* that the widget writes back to
- **Signals** are `on*` handlers called as `handler(self, ...)`, e.g. `onClicked`, `onToggled`,
  `onMoved`, `onTextChanged`, `onAccepted`, `onActivated`, `onTriggered`.
- **`id`** makes an element reachable by name, QML style: `TextField { id = "name" }`, then `name.text`.
- **`state{}`** is an observable table. `watch(state, key | "*", fn)` reacts to changes and returns a
  function that removes the watcher (so does `onFrame(fn)`).
- **Dynamic children**:
  - `Repeater { model, delegate }`, which flows inside its parent (e.g. horizontally in a `Row`)
  - `ListView`, a scrolling repeater
  - `If { condition }`
  - `Loader { item }`
  - `Responsive { compact = A, medium = B, expanded = C }`, which picks by window size class
- **Components**: `require "StatCard"` loads `StatCard.lua` from the script's folder, the same way
  QML files work as components.
- **Resources**: files are named `app:/data.json` (the app) or `imlui:/...` (the library);
  `ui.resource.read("data.json")` reads one relative to the calling script.
- **Common props**: `visible`, `enabled`, `tooltip`, `width`/`height` (dp; `-1` or `fillWidth = true` to fill).
- **Adaptive layouts**: `sizeClass()` returns the Material 3 window size class (`"compact"`, `"medium"`,
  `"expanded"`, `"large"`, `"extraLarge"`) of `windowSize()` (dp). Read it in function props or use `Responsive`;
  `NavigationBar`, `NavigationRail` and `ModalNavigationDrawer` cover phone and tablet navigation, and
  `setWindowSize(w, h)` resizes the window.
- **Scaling and zoom**: dp follow the monitor DPI times a user zoom: `setZoom(1.25)`, `zoom()`, or Ctrl + `=`/`-`/`0`
  and Ctrl + wheel. Frames are rendered on demand; `requestFrame()` asks for one.
- **Icons**: any [Material Icons](https://fonts.google.com/icons) name, e.g. `icon = "add"` on `Button`,
  `IconButton`, `Chip`, `ListItem` and `NavigationItem`, or `leadingIcon`/`trailingIcon` on fields.
- **Layout**:
  - `Row { justify = "start" | "center" | "end" | "spaceBetween", align = "center" | "top" | "bottom" }`
  - `Spacer { fill = true }` in a row takes the remaining width, e.g. `Row { Spacer { fill = true }, Button {...} }`
  - `Column { align = "center" }` centers its children
- **Menus**: give any `Button` or `IconButton` a `menu = Menu { MenuItem {...}, Divider {}, ... }`.
- **Logging**: C++ uses `import imlui.log;` (spdlog), e.g. `imlui::log::info("loaded {}", path)`.
  In Lua, use `ui.log.info("x = %d", x)` (also `debug`/`warn`/`error`); `print(...)` in UI scripts logs at info level.
  Script errors are logged too.
- **Snackbars**: `snackbar("Saved", { action = "Undo", onAction = fn })` from any handler.
- Indices are **1-based**.

### Elements

| Group | Elements |
|---|---|
| Windows | `ApplicationWindow` (root; `title`, `width`, `height`, `menuBar`, `padding`, `color`, `scrollable`), `Window`, `Dialog` (`icon`, `width`) |
| Layout | `Column`, `Row`, `Grid`, `ScrollView` (`color`, `rounding`, `padding`, `border`), `ListView`, `Card` (`variant` = elevated, filled, outlined; `width`, `height`, `padding`, `color`, `rounding`), `TabBar`/`Tab` (`icon`, `variant` = primary, secondary), `Expander`/`Section` (`expanded`, default true), `TreeNode` |
| App bars & navigation | `TopAppBar` (`title`, `navigationIcon`, `variant` = small, center, medium, large; children are actions), `NavigationItem`, `NavigationBar`, `NavigationRail` (`header`), `ModalNavigationDrawer` (`open`), `MenuBar`, `Menu`, `MenuItem` (`icon`, `shortcut`, `checked`) |
| Text | `Text`/`Label` (`style` = Material type scale, `color`, `wrap`, `align`), `Icon`, `Image` (PNG/JPEG resources; `fillMode`, `radius`, `tint`), `Link`, `Divider`, `Separator`, `Spacer` |
| Controls | `Button` (`variant` = filled, tonal, outlined, text, elevated, fab; `icon`), `IconButton` (`variant` = standard, filled, tonal, outlined; `checked`/`checkedIcon` toggle), `Checkbox`, `Switch`, `RadioButton` (`group`), `RadioGroup`, `Chip` (`variant` = filter, assist, suggestion, input), `SegmentedButton`, `ListItem` (`icon`, `supportingText`, `trailingText`, `trailingIcon`) |
| Inputs | `TextField`/`TextArea` (`variant` = filled, outlined; `label`, `placeholderText`, `leadingIcon`, `trailingIcon`, `supportingText`, `error`, `password`), `ComboBox` (Material dropdown), `Slider` (`onMoved`, `onReleased`), `SpinBox`, `ColorPicker` |
| Feedback & data | `ProgressBar`, `CircularProgress`, `BusyIndicator`, `Plot` (`kind` = lines, bars), `Table`, `Canvas` (draw from Lua with `onPaint(self, p)`; `p:line/rect/circle/text/icon/polyline`) |
| Other | `Timer`, `ImGuiDemo` |

C++ apps register their own types, QML style (see [Registering C++ types](docs/extending.md)):

```cpp
class Gauge final : public imlui::Element {
public:
    static void declare(imlui::TypeBuilder<Gauge>& type) {
        type.property("value", &Gauge::value, &Gauge::setValue).signal("onPeak").method("reset", &Gauge::reset);
    }
    void render(imlui::Frame& frame) override;
    // value(), setValue(double), reset() { ...; notify("value"); }
};

engine.registerType<Gauge>("Gauge");          // Gauge { value = bind(s, "cpu"), onPeak = fn }, then g:reset()
engine.registerSingleton("backend", backend); // backend.load, backend:tick(), backend.onTick = fn
```

Every built-in element is written the same way (`src/imlui/ui/elements/`), and a registered type under a
built-in's name replaces it. Any Capitalized name in a script is a constructor, so `Gauge {}` works as soon as the
type is registered.

### Theme (decided in Lua)

```lua
theme {
    preset = "material",         -- or "imgui-dark" | "imgui-light" | "imgui-classic"
    seed = "#6750A4", dark = true,
    spacing = 8, fontScale = 1.0,
    shape  = { extraSmall = 4, small = 8, medium = 12, large = 16, extraLarge = 28 },
    colors = { primary = "#FF5722", surface = "surfaceContainerLow" },  -- any Material role
    imgui  = { WindowBg = "surface/0.9" },   -- any ImGuiCol_*
    style  = { FrameRounding = 6, ItemSpacing = { 10, 6 } },  -- any ImGuiStyle field
}
```

Color props accept:
- a role name (`"primary"`)
- a role with alpha (`"onSurface/0.6"`)
- a hex string
- `{r, g, b, a}`

So do `seed`, `colors` and `imgui` in the theme table; there, `colors`/`imgui` roles mean the scheme generated
from that table's seed.

Widgets use Material's disabled colors (container at 12%, content at 38% of `onSurface`) and
animated state layers for hover and press.

`themeState{...}` gives you a state table that re-applies the theme on every change, so
you can bind switches, sliders and color pickers straight to it.
