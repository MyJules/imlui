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
[theming](docs/theming.md), [development](docs/development.md) and [status / changelog](docs/changelog.md).

## Build (clang + Ninja)

```
cmake --preset clang-debug
cmake --build build/clang-debug
build/clang-debug/bin/imlui_app                      # gallery
build/clang-debug/bin/imlui_app examples/counter.lua
```

Dependencies are vendored in `external/` (Dear ImGui 1.92.9b, Lua 5.4.8, GLFW 3.4, spdlog 1.15.3) and are
built as plain libraries. Project code only uses them through module wrappers.

`external/fonts/` holds Roboto Regular and Medium plus Material Icons Round (all Apache 2.0).
ImGui 1.92 rasterizes glyphs on demand, so every text style and icon size stays sharp, including
when the theme's `fontScale` changes.

Scripts **hot-reload** when you save any `.lua` file next to the script. In dev builds
(`IMLUI_DEV_PATHS=ON`, the default) `lua/` and `examples/` are read from the source
tree, so you don't need to rebuild.

## Layout

| Path | What |
|---|---|
| `src/wrap/lua.cppm` | `import lua;` (Lua C API, with its macros turned into functions) |
| `src/wrap/imgui.cppm` | `import imgui;` (re-exported ImGui API) |
| `src/wrap/platform.cppm` | `import imlui.platform;` (GLFW + OpenGL3 + ImGui backends) |
| `src/imlui/color.cppm` | colors, hex, OKLCH tonal palettes |
| `src/imlui/theme.cppm` | Material 3 scheme from a seed, typography, mapping onto ImGuiStyle |
| `src/imlui/widgets.cppm` | Material widgets drawn with ImGui (buttons, switch, slider, fields, ...) |
| `src/imlui/luaui.cppm` | Lua bridge: property bindings, signals, theme parsing |
| `src/imlui/renderer.cppm` | walks the Lua element tree every frame and renders it |
| `src/imlui/engine.cppm` | window, Lua state, hot reload, `imlui.native` |
| `lua/imlui.lua` | the DSL: elements, `state`, `bind`, `Repeater`, `theme`, timers |
| `examples/` | `gallery.lua` (every element and a live theme editor), `counter.lua`, `StatCard.lua` |

## Concepts

- **Elements** are tables: `Button { text = "OK", onClicked = fn }`. Child elements go in the array part.
- **Property values**:
  - plain values
  - a `function(self)`, which is a *binding* re-evaluated every frame
  - `bind(state, "key")`, a *two-way binding* that the widget writes back to
- **Signals** are `on*` handlers called as `handler(self, ...)`, e.g. `onClicked`, `onToggled`,
  `onMoved`, `onTextChanged`, `onAccepted`, `onActivated`, `onTriggered`.
- **`id`** makes an element reachable by name, QML style: `TextField { id = "name" }`, then `name.text`.
- **`state{}`** is an observable table. `watch(state, key | "*", fn)` reacts to changes.
- **Dynamic children**:
  - `Repeater { model, delegate }`, which flows inside its parent (e.g. horizontally in a `Row`)
  - `ListView`, a scrolling repeater
  - `If { condition }`
  - `Loader { item }`
- **Components**: `require "StatCard"` loads `StatCard.lua` from the script's folder, the same way
  QML files work as components.
- **Common props**: `visible`, `enabled`, `tooltip`, `width`/`height` (dp; `-1` or `fillWidth = true` to fill).
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
| Windows | `ApplicationWindow` (root; `title`, `width`, `height`, `menuBar`, `padding`), `Window`, `Dialog` (`icon`, `width`) |
| Layout | `Column`, `Row`, `Grid`, `ScrollView` (`color`, `rounding`, `padding`), `ListView`, `Card` (`variant` = elevated, filled, outlined), `TabBar`/`Tab` (`icon`, `variant` = primary, secondary), `Expander`/`Section`, `TreeNode` |
| App bars & navigation | `TopAppBar` (`title`, `navigationIcon`, `variant` = small, center, medium, large; children are actions), `NavigationItem`, `MenuBar`, `Menu`, `MenuItem` (`icon`, `shortcut`, `checked`) |
| Text | `Text`/`Label` (`style` = Material type scale, `color`, `wrap`, `align`), `Icon`, `Link`, `Divider`, `Separator`, `Spacer` |
| Controls | `Button` (`variant` = filled, tonal, outlined, text, elevated, fab; `icon`), `IconButton` (`variant` = standard, filled, tonal, outlined; `checked`/`checkedIcon` toggle), `Checkbox`, `Switch`, `RadioButton`, `RadioGroup`, `Chip` (`variant` = filter, assist, suggestion), `SegmentedButton`, `ListItem` (`icon`, `supportingText`, `trailingText`, `trailingIcon`) |
| Inputs | `TextField`/`TextArea` (`variant` = filled, outlined; `label`, `placeholderText`, `leadingIcon`, `trailingIcon`, `supportingText`, `error`, `password`), `ComboBox` (Material dropdown), `Slider`, `SpinBox`, `ColorPicker` |
| Feedback & data | `ProgressBar`, `CircularProgress`, `BusyIndicator`, `Plot` (`kind` = lines, bars), `Table`, `Canvas` (draw from Lua with `onPaint(self, p)`; `p:line/rect/circle/text/icon/polyline`) |
| Other | `Timer`, `ImGuiDemo` |

C++ apps can add elements with `engine.renderer().registerElement("MyThing", fn)`. Any
Capitalized name in a script is a constructor, so `MyThing {}` works immediately.

### Theme (decided in Lua)

```lua
theme {
    preset = "material",         -- or "imgui-dark" | "imgui-light" | "imgui-classic"
    seed = "#6750A4", dark = true,
    spacing = 8, fontScale = 1.0,
    shape  = { extraSmall = 4, small = 8, medium = 12, large = 16, extraLarge = 28 },
    colors = { primary = "#FF5722" },        -- any Material role
    imgui  = { WindowBg = "#101010" },       -- any ImGuiCol_*
    style  = { FrameRounding = 6, ItemSpacing = { 10, 6 } },  -- any ImGuiStyle field
}
```

Color props accept:
- a role name (`"primary"`)
- a role with alpha (`"onSurface/0.6"`)
- a hex string
- `{r, g, b, a}`

Widgets use Material's disabled colors (container at 12%, content at 38% of `onSurface`) and
animated state layers for hover and press.

`themeState{...}` gives you a state table that re-applies the theme on every change, so
you can bind switches, sliders and color pickers straight to it.
