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

## Build (clang + Ninja)

```
cmake --preset clang-debug
cmake --build build/clang-debug
build/clang-debug/bin/imlui_app                      # gallery
build/clang-debug/bin/imlui_app examples/counter.lua
```

Dependencies are vendored in `external/` (Dear ImGui 1.91.9b, Lua 5.4.8, GLFW 3.4)
and are built as plain libraries. Project code only uses them through module wrappers.

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
- Indices are **1-based**.

### Elements

| Group | Elements |
|---|---|
| Windows | `ApplicationWindow` (root; `title`, `width`, `height`, `menuBar`, `padding`), `Window`, `Dialog` |
| Layout | `Column`, `Row` (`spacing`, `align`), `Grid`, `ScrollView`, `ListView`, `Card`, `TabBar`/`Tab`, `Section`, `TreeNode` |
| Menus | `MenuBar`, `Menu`, `MenuItem` |
| Text | `Text`/`Label` (`style` = Material type scale, `color`, `wrap`), `Link`, `Separator`, `Spacer` |
| Controls | `Button` (`variant` = filled, tonal, outlined, text, elevated, fab), `Checkbox`, `Switch`, `RadioButton`, `RadioGroup`, `Chip`, `SegmentedButton`, `ListItem` |
| Inputs | `Slider`, `SpinBox`, `TextField`, `TextArea`, `ComboBox`, `ColorPicker` |
| Feedback & data | `ProgressBar`, `BusyIndicator`, `Plot`, `Table`, `Canvas` (draw from Lua with `onPaint(self, p)`) |
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

`themeState{...}` gives you a state table that re-applies the theme on every change, so
you can bind switches, sliders and color pickers straight to it.
