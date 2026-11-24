# Architecture

> The module layout below is the **target layout** of the ongoing module refactor (`docs/refactor-spec.md`).
> Phase A (foundation modules) has landed. Phases B (widgets) and C (UI and app) are being split, so on the
> `material-ui` branch `widgets.cppm`, `renderer.cppm` and `engine.cppm` are still single files. Behavior is
> the same either way.

## Layers

```mermaid
flowchart TD
    S["Lua script<br/>(returns the root element)"] --> P["DSL prelude<br/>lua/imlui.lua: constructors, state, bind, Repeater, theme, timers"]
    P --> T["Element tree<br/>plain Lua tables { __type = 'Button', ... }"]
    T --> R["Renderer + element handlers<br/>imlui.ui.renderer, imlui.ui.elements.*"]
    R --> N["Lua bridge<br/>imlui.lua.node / value / errors / painter / native"]
    R --> W["Material widgets<br/>imlui.widgets.* (namespace imlui::md)"]
    W --> TH["Theme<br/>imlui.theme.*, imlui.color.*"]
    W --> I["Dear ImGui 1.92<br/>(import imgui)"]
    I --> G["GLFW + OpenGL 3.3<br/>imlui.platform.window"]
    E["Engine<br/>imlui.app.engine"] -.owns.-> G
    E -.runs.-> S
    E -.drives.-> R
```

| Layer | Responsibility |
|---|---|
| **Lua script** | Builds the element tree and returns the root. It owns all state and all decisions, including theme, window title and size. |
| **DSL prelude** (`lua/imlui.lua`) | Turns tables into elements (`Button {...}` tags the table with `__type`), and provides observable state, bindings, dynamic children, theme helpers, timers, the snackbar and logging. It also sets up the script environment and the `require` searcher for components. |
| **Element tree** | Ordinary Lua tables. Props are fields, children are the array part, and signals are `on*` functions. A table's identity (its pointer) is its ImGui ID, so elements keep per-widget state such as animations and focus across frames. |
| **Renderer** | Walks the tree on every rendered frame (immediate mode). It looks up a handler for each `__type` and applies the common props (`visible`, `enabled`, `tooltip`). |
| **Element handlers** | One per element type, grouped by family in `imlui.ui.elements.*`. They read props through `luaui::Node`, call Material widgets, write two-way props back, and emit signals. |
| **Material widgets** | Material 3 controls drawn with ImGui primitives (`InvisibleButton` plus the draw list), using the current `imlui::theme()`. |
| **ImGui / platform** | Dear ImGui with the GLFW and OpenGL 3 backends, behind the `imgui` and `imlui.platform.window` wrapper modules. |

## Frame loop

`Engine::run()` does the following:

1. Creates the window **hidden**, sets the content scale from the monitor, loads Roboto and Material Icons,
   applies the default theme, and loads the script.
2. Applies the root element's `title` and `width`/`height` (dp, multiplied by the content scale) to the native
   window, centers it and shows it.
3. Each loop iteration:
   - Every 400 ms, compares `.lua` modification times (hot reload). A change, or a `reload()` call, triggers
     `reloadNow()`.
   - Computes how long it may sleep: until the next **requested frame** (see below) or the next file check,
     whichever comes first. With nothing pending, that's the 400 ms hot-reload interval (or forever without hot
     reload). While minimized it sleeps at least 100 ms.
   - `window.waitEvents(timeout)` (`glfwWaitEventsTimeout`). Input or window events start a **settle period** of
     0.3 s, during which every iteration renders, so transitions and hover layers finish.
   - `renderFrame()`, but only if a frame is due. Otherwise the loop goes back to sleep without rendering.

### On-demand rendering

`imlui.ui.frame_request` keeps the pending frame requests: `requestFrame()` (as soon as possible),
`requestFrameAfter(seconds)` and `requestFramesFor(seconds)` (continuously, the settle period).
`secondsUntilFrame()` tells the loop when the next frame is due. At the start of a frame,
`beginFrameRequests()` drops the requests that frame serves, so anything that still needs frames asks again while
it renders. Requests that come in while the loop sleeps wake it through `glfwPostEmptyEvent()`.

Who requests frames:

| Source | Request |
|---|---|
| Input and window events (mouse, keys, focus, resize, DPI change) | the 0.3 s settle period |
| `md::animate` / `md::stateLayer` (every Material transition) | a frame while the value is still moving |
| Indeterminate `ProgressBar`, `CircularProgress`, `BusyIndicator` | every frame |
| `Canvas { animated = true }` | every frame |
| `Timer` elements | a frame when the interval is due |
| Lua timers: `ui.__tick` returns the seconds until the next `setTimeout`/`setInterval` | a frame then |
| `Snackbar` | every frame while it slides in or fades out, and a frame when the fade starts |
| Tooltips | a frame every 100 ms while an item is hovered but the tooltip delay hasn't passed |
| An active text field | a frame every 100 ms for the caret blink |
| A held mouse button | frames at 30 Hz (repeat buttons, drags that pause) |
| `ui.requestFrame(ms?)`, `ui.setZoom`, `native.injectMouse` | a frame (or the settle period) |
| A reload | the settle period |

`md::animate` caps the frame time it advances by at 1/30 s, so a transition that starts on the first frame after
a long sleep doesn't jump to its end.

### Scaling

`imlui.theme.dpi` holds the monitor **content scale** and the user **zoom**. `dpiScale()` is their product, and
`dp(v)` converts with it. The window reports content scale changes (`glfwSetWindowContentScaleCallback`), and
`ui.setZoom` and the zoom shortcuts schedule a zoom change. At the start of the next frame, `syncScale()` applies
the zoom and, if the effective scale changed, calls `reapplyTheme()`: the style is rebuilt from an unscaled
`ImGuiStyle`, then `ScaleAllSizes(dpiScale())` and `FontScaleDpi = dpiScale()` are applied, as for any new
theme. With `GLFW_SCALE_TO_MONITOR`, Windows resizes the native window itself when it moves to another monitor.

### Rendering a frame

`renderFrame()` renders one complete frame:

1. `beginFrameRequests()`, then **`syncScale()`**, which applies a pending zoom and the pending theme. `theme{}`
   and `setTheme` only *schedule* a theme. It's applied here, between frames, so a click handler can change the
   theme in the middle of a frame without breaking that frame's ImGui style stack. If only the scale changed, the
   current theme is re-applied.
2. `window.beginFrame()`, which starts a new ImGui frame. Then the zoom shortcuts are handled.
3. `ui.__tick(now)`: runs due `setTimeout`/`setInterval` timers, then the `onFrame` callbacks, and returns the
   seconds until the next timer.
4. Renders the root element.
5. Renders **overlays**: the children of `ui.__overlays`. Today that's the app-wide `Snackbar` element driven by
   `snackbar()`. Overlays draw above everything else.
6. Draws the **error overlay** if any script errors were recorded (see below).
7. `window.endFrame()`: clears to the current `WindowBg` color, renders the ImGui draw data and swaps buffers.
8. Copies the title from the root `ApplicationWindow` to the native window. The native title is only updated
   when it changes.

**Live resize.** On Windows, the OS runs a modal loop while the user drags the window border, so
`glfwWaitEventsTimeout()` doesn't return. The engine registers `renderFrame` as the window's **refresh callback**, and
GLFW calls it from its refresh and framebuffer-size callbacks. The UI then lays itself out again during the drag.

**Error overlay.** All calls into Lua go through `luaui::safeCall`, which runs `pcall` with a traceback handler.
Errors are logged with the source tag `lua`, deduplicated, and capped at the last 32. While any are recorded, a
panel with the `errorContainer` color sits at the bottom center of the window, with **Dismiss** (clears the list)
and **Reload** buttons. An error in a binding or handler only affects that value or call, and the rest of the UI
keeps rendering.

## Property resolution

`luaui::Node` wraps an element table on the Lua stack. Every prop read (`n.string("text")`, `n.number("value")`,
`n.color("color")`, ...) **resolves** the raw field:

| Raw value | Read | Write-back (`Node::set`, when a widget changes it) |
|---|---|---|
| plain value, `text = "Hi"` | the value | stored on the element (`self.text = ...`) |
| function, `text = function(self) ... end` | called as `fn(self)` on **every read**, every frame (a *binding*) | **ignored**: the binding owns the value, so the widget snaps back to what the function returns |
| `bind(t, "k")` | `t.k` | `t.k = v` |
| `bind(getter, setter)` | `getter()` | `setter(v)` |

Because bindings are evaluated every frame, a UI that reads a `state{}` (or any table) inside a function always
shows current data. No dependency tracking is needed. `watch()` is only for side effects. "Every frame" means
every *rendered* frame: state changed by input handlers or timers is shown because those render a frame anyway.
Only a binding whose value changes by itself (for example from `time()`) needs `requestFrame()`.

Type coercion: numbers read where strings are expected are converted. Colors accept a role name, `role/alpha`,
hex, or `{r, g, b, a}` (see [Theming](theming.md#color-specs)). `Node::text()` returns the `text` prop, or the
first positional string: `Button { "OK" }` is the same as `Button { text = "OK" }`.

## Signals

Handlers are `on*` fields called as `handler(self, ...args)` through `safeCall`. Return values are ignored, and
errors go to the error overlay. Which signals exist and what arguments they get is listed per element in
[Elements](elements.md). A widget that changes a two-way prop writes the value first and then emits the signal,
so the handler already sees the new state.

## Children and inline containers

`Renderer::forEachChild` gets an element's children from one of two places:

- the array part of the table, or
- the result of `self.__children(self)` if that function exists. The prelude sets it for `Repeater`, `ListView`,
  `If` and `Loader`.

Children whose table has `__inline = true` (`Repeater`, `If`, `Loader`) are **spliced into the parent**: their
own children are laid out as direct children of the parent. That's why a `Repeater` inside a `Row` flows
horizontally. `visible = false` on an inline container hides all of its children. `ListView` isn't inline. It's
a scrolling `ScrollView` whose children come from `model`/`delegate`.

Layout notes:

- Each **visual** element is wrapped in an ImGui group, so it counts as exactly one layout item (a label plus a
  field behaves as one item in a `Row`).
- **Non-visual** elements (`ApplicationWindow`, `Window`, `Dialog`, `Snackbar`, `Timer`, `ImGuiDemo`, and any
  registered with `visual = false`) take no layout space or spacing.
- Immediate-mode layout uses *last frame's* sizes for `Row` alignment, `justify`, fill spacers, `Column { align }`
  and `TopAppBar` actions. A layout settles within one frame.

## Hot reload

`Engine::reloadNow()`:

1. Clears recorded errors and resets the theme to `Theme{}` (seed `#6750A4`, dark).
2. Creates a new `lua_State`, opens the standard libraries, registers `imlui.native` (a `package.preload`
   entry) and the Canvas painter metatable.
3. Sets `package.path` to `<luaPaths>/?.lua;<luaPaths>/?/init.lua;<scriptDir>/?.lua;` followed by the default.
4. Runs a boot chunk: `require("imlui")`, `ui.__load(script)` (the script runs in `ui.env`), and returns the
   root, `ui.__tick` and `ui.__overlays`.
5. On failure (syntax error, runtime error, or a non-table return), it closes the new state, restores the
   previous theme and keeps the old UI. On success, it replaces the old state and applies the window title and
   size (size only if it changed since the last load).

The file watcher scans the configured Lua paths and the script's directory recursively for `*.lua`.

## Module map (target layout)

All project code is C++20 modules in `src/`. Each module exports one class or one family of functions, and the
implementation goes after `module :private;`. See [Development](development.md#module-conventions).

### Third-party wrappers (`src/wrap/`)

| Module | Purpose |
|---|---|
| `lua` | Lua 5.4 C API, with its macros turned into real functions; `lua::Type` enum, `lua::ok`, `registryIndex`, ... |
| `lua.stack_guard` | `lua::StackGuard`: restores the stack top on scope exit |
| `imgui` | Re-exports the ImGui API subset that imlui uses (types, flags, `ImGui::` functions); macros become `constexpr` functions |
| `imlui.log` | spdlog behind a `std::format` API: `imlui::log::info(...)`, `write(level, msg, source)`, `addFileSink`, `setLevel` |
| `imlui.platform.window` | `platform::Window` + `Window::Config`: GLFW window, GL 3.3 context, ImGui context and backends, refresh and content-scale callbacks, `waitEvents`, `postEmptyEvent`, `setScaledSize` |

### Color (`src/imlui/color/`)

| Module | Purpose |
|---|---|
| `imlui.color` | `Color` (sRGB `r,g,b,a`), `mix`, `overlay`, `parseHex`, `toHex` |
| `imlui.color.oklch` | `Oklch`, `toOklch`, `fromOklch` (gamut mapped), `toneToOkL` |
| `imlui.color.tonal_palette` | `TonalPalette { hue, chroma }` with `tone(t)` |

### Theme (`src/imlui/theme/`)

| Module | Purpose |
|---|---|
| `imlui.theme.dpi` | `contentScale`/`setContentScale`, `zoom`/`setZoom`, `zoomStep`, `dpiScale` (their product), `dp(v)` |
| `imlui.theme.scheme` | `Scheme` (all M3 color roles), `roles()`, `findRole`, `generateScheme(seed, dark)` |
| `imlui.theme.typography` | `TextStyle`, `Weight`, `textStyleInfo`, `textStyleFromName`, `loadFonts`, `font`, `fontSize`, `fontPixels`, `pushFont` |
| `imlui.theme.icons` | `loadIcons`, `iconFont`, `iconText(name)`, `hasIcon(name)` |
| `imlui.theme.theme` | `Theme` (+ `Shape`, `StyleOverride`), `theme()`, `setTheme`, `applyPendingTheme`, `reapplyTheme`, `resolveColor`, `imguiColorIndex`, `isStyleField` |
| `imlui.theme.font_scope` | `FontScope`: RAII push/pop of a text style's font |

### Lua bridge (`src/imlui/lua/`, namespace `imlui::luaui`)

| Module | Purpose |
|---|---|
| `imlui.lua.errors` | `reportError`, `errors`, `clearErrors`, `tracebackHandler`, `safeCall` |
| `imlui.lua.value` | `Value` variant (nil/bool/number/string), `pushValue`, `isBind` |
| `imlui.lua.node` | `Node`: typed, binding-aware prop reads, `set` (write-back), `emit` (signals) |
| `imlui.lua.theme_table` | `themeFromTable` (Lua table → `Theme`), `pushScheme` (`Scheme` → `{ role = "#hex" }`) |
| `imlui.lua.painter` | Canvas painter: `registerPainter`, `beginPaint`, `endPaint` |
| `imlui.lua.native` | `NativeHooks` and `registerNative`: the `imlui.native` Lua library |

### Widgets (`src/imlui/widgets/`, namespace `imlui::md`)

| Module | Purpose |
|---|---|
| `imlui.widgets.common` | Shared helpers: `col`, `scheme()`, `shape()`, disabled colors, `animate`, `stateLayer`, `itemWidth`, text/icon/shadow drawing |
| `imlui.widgets.surface` | Surface stack (`pushSurface`, `popSurface`, `surfaceColor`) for cutouts on cards and dialogs |
| `imlui.widgets.text` | `text`, `icon`, `tooltip`, `divider` |
| `imlui.widgets.button` | `button` (+ `ButtonVariant`), `iconButton` (+ `IconButtonVariant`) |
| `imlui.widgets.selection` | `checkbox`, `radio`, `switchToggle`, `chip` (+ `ChipVariant`), `segmented` |
| `imlui.widgets.slider` | `slider` |
| `imlui.widgets.progress` | `progress` (linear), `circularProgress` |
| `imlui.widgets.field` | Shared text-field plumbing (`FieldFrame`, decoration, background, supporting text) |
| `imlui.widgets.text_field` | `textField` (+ `TextFieldOptions`, `TextFieldResult`) |
| `imlui.widgets.menu` | Menu styling, `menuItem`, `menuDivider`, `beginAnchoredMenu`/`endAnchoredMenu` |
| `imlui.widgets.dropdown` | `dropdown` (+ `DropdownOptions`) |
| `imlui.widgets.spin_box` | `spinBox` |
| `imlui.widgets.color_field` | `colorField` (swatch and picker popup) |
| `imlui.widgets.tabs` | `tabs` (+ `TabInfo`) |
| `imlui.widgets.navigation` | `navigationItem` |
| `imlui.widgets.list` | `listItem` (+ `ListItemOptions`), `expander` |
| `imlui.widgets.card` | `beginCard`/`endCard` (+ `CardVariant`) |
| `imlui.widgets.dialog` | `beginDialog`/`endDialog` |
| `imlui.widgets.snackbar` | `snackbar` |
| `imlui.widgets.chart` | `chart` (+ `ChartOptions`) |
| `imlui.widgets.table` | `tableHeaderCell`, `tableCell` |
| `imlui.widgets` | Umbrella: `export import` of all widget modules |

### UI (`src/imlui/ui/`)

| Module | Purpose |
|---|---|
| `imlui.ui.frame_request` | On-demand rendering: `requestFrame`, `requestFrameAfter`, `requestFramesFor`, `secondsUntilFrame`, `beginFrameRequests`, wake handler |
| `imlui.ui.props` | Prop helpers: `sizeProp`, `labelId`, `alignFactor` |
| `imlui.ui.renderer` | `Renderer`: `registerElement`, `render`, `forEachChild`, `renderChildren`, `renderStack`, `attachMenu`, window title |
| `imlui.ui.elements.windows` | `ApplicationWindow`, `Window`, `Dialog` |
| `imlui.ui.elements.layout` | `Column`, `Item`, `Row`, `Grid`, `ScrollView`, `ListView`, `Card`, `TabBar`, `Tab`, `Section`, `Expander`, `TreeNode`, `Repeater`, `If`, `Loader` |
| `imlui.ui.elements.navigation` | `TopAppBar`, `NavigationItem`, `MenuBar`, `Menu`, `MenuItem` |
| `imlui.ui.elements.content` | `Text`, `Label`, `Icon`, `Link`, `Separator`, `Divider`, `Spacer` |
| `imlui.ui.elements.controls` | `Button`, `IconButton`, `Checkbox`, `Switch`, `RadioButton`, `RadioGroup`, `Chip`, `SegmentedButton`, `ListItem` |
| `imlui.ui.elements.inputs` | `Slider`, `SpinBox`, `TextField`, `TextArea`, `ComboBox`, `ColorPicker` |
| `imlui.ui.elements.data` | `ProgressBar`, `CircularProgress`, `BusyIndicator`, `Plot`, `Table`, `Canvas` |
| `imlui.ui.elements.system` | `Snackbar`, `Timer`, `ImGuiDemo` |
| `imlui.ui.elements` | `registerBuiltinElements(Renderer&)`: calls all of the above |

### App (`src/imlui/app/`)

| Module | Purpose |
|---|---|
| `imlui.app.config` | `EngineConfig` (script, Lua paths, font dir, window config, hot reload), `defaultConfig(argc, argv, sourceDir)` |
| `imlui.app.engine` | `Engine`: window, Lua state, frame loop, hot reload, error overlay; `renderer()`, `requestQuit`, `requestReload`, `window()`, `run()` |
| `imlui` | Umbrella for apps: `import imlui;` |

`app/main.cpp` is the host. It has no `#include`, only `import imlui;`, and calls
`Engine{defaultConfig(argc, argv, IMLUI_SOURCE_DIR)}.run()`.

## Third-party code

Vendored code in `external/` is built as ordinary static libraries (`ext::lua`, `ext::imgui`, `ext::spdlog`,
`glfw`), with module scanning turned off. Project code never includes their headers directly. Only the wrapper
modules above do, in their global module fragment.

- **Dear ImGui 1.92.9b** is built with the GLFW and OpenGL 3 backends, `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, and a
  user config (`external/imlui_imconfig.h`). The config routes `IM_ASSERT` to a handler that reports the failing
  expression and the ImGui window it happened in, then aborts. ImGui 1.92 rasterizes glyphs on demand, so every
  text size and icon size stays sharp.
- **Lua 5.4.8** is compiled as C, without the `lua`/`luac` executables.
- **GLFW 3.4** and **spdlog 1.15.3** are built via `add_subdirectory`.
- **Fonts**: Roboto Regular and Medium, plus Material Icons Round with its `.codepoints` file (Apache 2.0).
