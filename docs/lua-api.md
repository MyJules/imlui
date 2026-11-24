# Lua API

Everything here is defined in `lua/imlui.lua` (the DSL prelude) or in the native `imlui.native` library it wraps.

## The script environment

A UI script runs in `ui.env`, not in `_G`. It must **return the root element**, usually an `ApplicationWindow`.
The engine reports an error if the script returns anything other than a table.

A free name `x` in a script is looked up in this order:

1. **`ui.x`**: everything on this page, plus the built-in element constructors. `ui` itself is also available.
2. **Lua globals** (`_G`): `math`, `string`, `table`, `os`, `io`, `require`, ..., and the
   [C++ singletons](extending.md#singletons) the app registered (for example `backend`).
3. **Element ids**: `TextField { id = "nameField" }` makes `nameField` refer to that element table, QML style.
4. **Any other Capitalized name** becomes an element constructor (`ui.element(name)`), so elements registered
   from C++ work without any Lua changes. For a [registered C++ type](extending.md) it's a typed constructor.

```lua
TextField { id = "nameField", label = "Name", text = "" },
Text { text = function() return "Hello, " .. nameField.text end },
```

Ids are global to the loaded UI. The lookup is lazy, so a binding can refer to an id that is declared later in the
file.

**Id collisions.** Because ids are looked up after `ui.*` and the globals, an id such as `"table"`, `"io"` or
`"state"` can't be referenced by name: the name finds the global or the `ui` member instead. The element
constructor reports such an id in the error overlay when the element is created, for example
`element id 'io' collides with Lua global 'io'; it can't be referenced by name — rename it` (or
`... collides with ui member 'ui.state' ...`). The element is still created, and `ui.ids.io` still holds it; only
the free name doesn't. Rename the id. (`ui.checkId(id)` runs the same check and returns `false` for such an id.)

Indices (`currentIndex`, `currentRow`, delegate `index`) are **1-based**.

## Elements

### `element(name) -> constructor`

Returns a constructor for element type `name`. Calling it with a table **turns that table into the element**
(it is not copied): the prelude sets `__type` and registers `id`. The table's identity matters because it is the
element's ImGui ID and holds its per-element state.

```lua
Button { text = "OK", onClicked = function(self) print("clicked", self.text) end }
Button "OK"      -- a non-table argument becomes the first positional value: { "OK" }
```

For a type registered from C++ with `Engine::registerType<T>()`, the constructor also creates the C++ instance
(stored in the table as `__native`) and gives the table a metatable through which the type's methods are
callable: `gauge:reset()`. See [Registering C++ types](extending.md).

### `types() -> { name, ... }`

The registered element type names, sorted: every built-in (`Button`, `Column`, ..., aliases such as `Item` and
`Label` included, and `Repeater`, `If`, `Loader`, `Responsive`) and the app's own types.

Built-in constructors (the `BUILTIN` list): `ApplicationWindow`, `Window`, `Dialog`, `Column`, `Item`, `Row`,
`Grid`, `ScrollView`, `Card`, `TabBar`, `Tab`, `Section`, `Expander`, `TreeNode`, `TopAppBar`, `NavigationItem`,
`NavigationBar`, `NavigationRail`, `ModalNavigationDrawer`, `MenuBar`, `Menu`, `MenuItem`, `Text`, `Label`, `Icon`, `Image`, `Link`, `Separator`, `Divider`, `Spacer`, `Button`,
`IconButton`, `Checkbox`, `Switch`, `RadioButton`, `RadioGroup`, `Chip`, `SegmentedButton`, `ListItem`, `Slider`,
`SpinBox`, `TextField`, `TextArea`, `ComboBox`, `ColorPicker`, `ProgressBar`, `CircularProgress`, `BusyIndicator`,
`Plot`, `Table`, `Canvas`, `Timer`, `ImGuiDemo`. `Repeater`, `ListView`, `If`, `Loader` and `Responsive` have
their own constructors ([below](#dynamic-children)). Props and signals are in [Elements](elements.md).

### Property values

| Form | Example | Meaning |
|---|---|---|
| plain | `text = "Hello"` | A constant. If a widget changes it (for example, typing), the new value is stored on the element. |
| binding | `text = function(self) return ... end` | Called on every read, every frame. Widget edits are ignored, because the function owns the value. |
| two-way | `value = bind(s, "volume")` | Reads `s.volume`. Widget edits write `s.volume`. |

### Signals

Signals are `on*` handlers called as `handler(self, ...)`, for example `onClicked(self)`,
`onToggled(self, checked)`, `onMoved(self, value)`, `onTextChanged(self, text)`, `onAccepted(self, text)` and
`onActivated(self, index, text)`. Errors in handlers are caught and shown in the error overlay.

## State and bindings

### `state(init) -> proxy`

Creates an observable table. Reads and writes look like a plain table. A write that changes a value (`old ~= new`)
calls the watchers for that key and then the `"*"` watchers. `pairs(s)` and `#s` work. Only top-level keys are
observed: `table.insert(s.items, x)` doesn't notify. Bindings still show the change, because they run every frame.

```lua
local s = state { count = 0, items = {} }
s.count = s.count + 1
```

### `watch(state, key, fn) -> stop`

Calls `fn(new, old, key)` whenever `state[key]` changes. Use `"*"` for any key. Asserts that `state` was created by
`state()`. Returns a function that removes the watcher; calling it again does nothing. A watcher may remove itself
while it runs.

```lua
watch(s, "count", function(new, old) log.info("count %d -> %d", old, new) end)
local stop = watch(look, "*", applyTheme)
stop() -- applyTheme no longer runs
```

### `raw(state) -> table`

Returns the underlying (non-proxy) data table, for example for serialization or to pass to `theme()`. For any other
value it returns the value unchanged.

### `bind(t, key)` / `bind(getter, setter)`

Creates a two-way binding object. `t` can be any table: a `state`, or a plain table like `sel.tags`.

```lua
Switch { checked = bind(s, "wifi") }
Chip   { checked = bind(sel.tags, tag) }               -- plain nested table
Slider { value = bind(function() return s.v * 100 end,  -- getter
                      function(v) s.v = v / 100 end) }  -- setter
```

## Components and `require`

### `component(fn) -> constructor`

Wraps `fn(props)` so that `MyComp { ... }` calls it with the props table, or `{}` if there are none. Locals created
inside `fn` are per instance.

### `require "Name"`

Inside a UI script, `require "Name"` first looks for `Name.lua` (then `Name/init.lua`) **in the script's folder**
(for the entry script, `app:/`) and runs it in the UI environment, the way QML files work as components. `a.b` is
`a/b.lua`. Results are cached in `package.loaded` like any `require`. `import` is an alias of `require`. If the
module isn't there, it's looked up in `imlui:/` (in the global environment), then on Lua's default `package.path`.
Errors and tracebacks name the file as a resource: `app:/StatCard.lua:3: ...`.

```lua
-- StatCard.lua
return component(function(props)
    return Card { variant = props.variant or "filled", padding = 20,
        Text { text = props.title, style = "titleSmall", color = "onSurfaceVariant" },
        Text { text = props.value, style = "headlineMedium", color = props.color or "primary" },
    }
end)

-- main.lua (next to StatCard.lua)
local StatCard = require "StatCard"
StatCard { title = "Frame rate", value = function() return ("%.0f fps"):format(fps()) end }
```

## Resources

Scripts, fonts and data files are **resources**, named `prefix:/path`: `app:/main.lua` and the files next to it
belong to the app, `imlui:/imlui.lua` and `imlui:/fonts/...` to the library. In a dev build `app:/` is the script's
folder on disk. In a shipped app the files can be embedded in the executable or packed into a `.pak` archive next
to it; the names stay the same, and files on disk override packed ones. Names use forward slashes; `.` and `..`
work, but `..` can't leave the prefix, and OS paths (`C:/x`, `/etc/x`) are not resource names.

A relative name is resolved against the folder of the **script that makes the call**: in `app:/pages/home.lua`,
`"icons/x.svg"` is `app:/pages/icons/x.svg` and `"../data.json"` is `app:/data.json`. Full names work anywhere.

| Function | Result |
|---|---|
| `ui.resource.read(name)` | The file's contents as a string, or `nil` and a message when there is no such file. |
| `ui.resource.exists(name)` | `true` when `name` is a file or a folder. |
| `ui.resource.url(name)` | The full, normalized name (`"app:/data.json"`), e.g. to pass on to other code. |

An invalid name (or one that isn't a string) raises an error at the caller.

```lua
local items = parseJson(ui.resource.read("data/items.json"))
if ui.resource.exists("theme.lua") then ... end
```

The calling script is found from the caller's chunk name, which Lua drops in a **tail call**: in
`return ui.resource.read("x")` a relative name raises an error. Use a full name, a local
(`local text = ui.resource.read("x"); return text`), or parentheses (`return (ui.resource.read("x"))`).

Every resource a script reads or loads is watched for hot reload when it comes from a folder on disk.

[`Image`](elements.md#image) takes a resource name as its `source`, resolved the same way against the script that
creates the `Image`: `Image { source = "img/logo.png" }`. An image file that changes on disk is decoded again
without a script reload.

## Dynamic children

`Repeater`, `If`, `Loader` and [`Responsive`](#responsive-compact-medium-expanded-large-extralarge) are **inline**: their children are laid out as if they were direct children of the
parent. A `Repeater` in a `Row` flows horizontally, and in a `Column` it stacks vertically. `visible = false` on
one of them hides all of its children.

### `Repeater { model, delegate }`

| Prop | Type | Meaning |
|---|---|---|
| `model` | list, number, function or `bind` | Items. A number `n` means items `1..n`. |
| `delegate` | `function(item, index) -> element` | Builds the element for one item. |

Delegate instances are **cached per index**. They are rebuilt only when the item at that index changes
(`~=` comparison), so per-delegate state survives frames. Extra cached entries are dropped when the model shrinks.

```lua
Row { spacing = 8,
    Repeater { model = { "lua", "cpp" }, delegate = function(tag, i)
        return Chip { text = tag, checked = bind(sel.tags, tag) }
    end },
}
```

### `ListView`

A scrolling `Repeater`: a `ScrollView` whose children come from `model`/`delegate`. It isn't inline. Without
`delegate`, its array children are used. It takes all `ScrollView` props (`height`, `width`, `padding`, `color`,
`rounding`, `border`, `spacing`, `align`).

```lua
ListView { height = 240, model = function() return todo.items end,
    delegate = function(item, index) return ListItem { text = item } end }
```

### `If { condition, ...children, otherwise = element }`

Shows its array children while `condition` (a value, function or `bind`) is truthy. Otherwise it shows the single
element `otherwise`, if one is given.

```lua
If { condition = function() return s.loggedIn end,
     Text { text = "Welcome back" },
     otherwise = Button { text = "Log in", onClicked = login } }
```

### `Loader { item }`

Shows exactly one element: `item`, or the result of calling `item`. Useful for page switching.

```lua
Loader { item = function() return pages[nav.page] end }
```

## Window size and adaptive layouts

The UI can adapt to the window with the Material 3 **window size classes**, picked by the window width in dp:

| Class | Width |
|---|---|
| `"compact"` | < 600dp (phones) |
| `"medium"` | 600–839dp (tablets in portrait, foldables) |
| `"expanded"` | 840–1199dp |
| `"large"` | 1200–1599dp |
| `"extraLarge"` | ≥ 1600dp |

| Function | Description |
|---|---|
| `windowSize() -> w, h` | The window's size in dp: pixels / `dpiScale()`, so zooming in makes it smaller. It's the last rendered frame's size (the display size before the first frame). |
| `sizeClass() -> name` | The size class of `windowSize()`'s width. |
| `sizeClassFor(w) -> name` | The size class of any width in dp. |
| `setWindowSize(w, h)` | Resizes the native window to `w` x `h` dp, scaled by the monitor content scale (not by the zoom), like the root `ApplicationWindow`'s `width`/`height`. Applied after the current frame. |
| `pickSizeClass(t, class) -> value` | `t[class]`, else the value for the nearest smaller class that `t` has, else the smallest one it has. `false` counts as a value. `Responsive` uses it. |
| `SIZE_CLASSES` | `{ "compact", "medium", "expanded", "large", "extraLarge" }`. |

Bindings are re-evaluated on every rendered frame, and resizing the window renders frames, so function props that
read `sizeClass()` or `windowSize()` adapt by themselves:

```lua
local function compact() return sizeClass() == "compact" end
ScrollView { padding = function() return compact() and 16 or 24 end, ... }
Grid { columns = function() return compact() and 1 or 2 end, ... }
TopAppBar { navigationIcon = function() return compact() and "menu" or "" end, ... }
```

### `Responsive { compact, medium, expanded, large, extraLarge }`

An inline container (like `Loader`) that shows the element given for the current size class. A class that isn't
given falls back to the nearest smaller class that is, or else to the smallest one given. `false` shows nothing.
Build each element once, as below: switching classes then keeps their state, scroll positions included (a
`ScrollView` restores its offset when it's rendered in another place).

```lua
Row { align = "top", spacing = 0,
    Responsive {
        compact = false,                                     -- no side navigation on phones
        medium = NavigationRail { header = menuButton, destinations },
        expanded = ScrollView { width = 296, destinations }, -- also used for large and extraLarge
    },
    content,
}
```

The gallery (`app/main.lua` in the [imlui-gallery](getting-started.md#the-example-app-imlui-gallery) repository)
shows the full pattern: a modal drawer opened from the top app bar on compact
windows, a rail on medium ones, the standard drawer from expanded up, and grids whose `columns` follow the width.

## Theme

See [Theming](theming.md) for the table fields and the color roles.

| Function | Description |
|---|---|
| `theme(t) -> scheme` | Applies theme table `t` **at the start of the next frame**. Returns the scheme `t` produces, as `{ role = "#RRGGBB" }`. Also stores `t` in `ui.__theme`. Fonts in `t.fonts` are loaded at once and errors reported (see [Fonts](theming.md#fonts)). |
| `scheme() -> scheme` | The scheme of the theme **currently in effect**. Right after `theme{}` it still returns the old scheme until the next frame. |
| `schemeFor(t) -> scheme` | The scheme `t` would produce, without applying it. |
| `themeState(init) -> state` | A `state` that applies itself as the theme now and on every change. Bind controls straight to it. |

```lua
local look = themeState { seed = "#6750A4", dark = true, fontScale = 1.0 }
Switch { text = "Dark mode", checked = bind(look, "dark") }
ColorPicker { label = "Seed", color = bind(look, "seed") }
```

### `color.*`

These take [color specs](theming.md#color-specs) (role, `role/alpha`, hex, `{r,g,b,a}`) and return a hex string:
`"#RRGGBB"`, or `"#RRGGBBAA"` when the color isn't opaque. An invalid color raises an error.

| Function | Description |
|---|---|
| `color.mix(a, b, t)` | Linear sRGB interpolation from `a` to `b` (`t` in 0..1). |
| `color.tone(c, t)` | Material tone `t` (0 = black, 100 = white) of `c`'s hue and chroma. |
| `color.alpha(c, a)` | `c` with alpha `a`. |
| `color.resolve(c)` | Resolves a spec against the current scheme, for example `color.resolve("primary")`. |

## Timers and frames

Frames are **rendered on demand** (see [Architecture](architecture.md#frame-loop)): after input, while an animation
or indeterminate indicator is moving, when a timer comes due, and when a script asks for one. When nothing changes,
the app sleeps. Timers run at the start of a frame, and the engine wakes up for the next one, so an interval keeps
its rate even with no input. Callbacks may add or clear timers.

| Function | Description |
|---|---|
| `setTimeout(fn, ms) -> id` | Calls `fn()` once after `ms` milliseconds. |
| `setInterval(fn, ms) -> id` | Calls `fn()` every `ms` milliseconds. |
| `clearTimer(id)` | Cancels a timeout or interval. |
| `onFrame(fn) -> stop` | Calls `fn(time)` at the start of every **rendered** frame, after the timers. Call the returned `stop()` to remove it (also from inside `fn`). |
| `requestFrame(ms?)` | Asks for a frame now, or after `ms` milliseconds. Call it from `onFrame` (or a binding) while something you draw keeps changing on its own. |

Anything that changes because of a click, a key or a timer needs nothing extra. Only state that changes by
itself over time, such as a binding that reads `time()`, needs `requestFrame()`, a timer, or a `Canvas` with
`animated = true`.

```lua
local id = setInterval(function() s.seconds = s.seconds + 1 end, 1000)
setTimeout(function() clearTimer(id) end, 10000)
```

For a timer declared as an element, see the [`Timer` element](elements.md#timer).

## Scaling and zoom

Sizes in scripts are in **dp**. One dp is `dpiScale()` pixels, where

`dpiScale() = contentScale() × zoom()`

- `contentScale()` is the monitor's content scale (1.5 at 150 % Windows scaling). It follows the window when it
  moves to a monitor with a different DPI, and the theme is re-applied at the new scale.
- `zoom()` is a user zoom factor (1 = 100 %), for example from a settings slider.

The root `ApplicationWindow`'s `width`/`height` are dp at 100 % zoom: they're multiplied by the content scale (not
by the zoom) when the native window size is set.

| Function | Description |
|---|---|
| `zoom() -> number` | The user zoom, including a change requested this frame. |
| `setZoom(z)` | Sets the user zoom, clamped to 0.5..3.0. It takes effect at the start of the next frame. |
| `dpiScale() -> number` | The effective dp → pixel factor. |
| `contentScale() -> number` | The monitor content scale. |
| `setZoomShortcuts(on)` | Turns the built-in zoom shortcuts on or off (on by default, and again after a reload). |

**Built-in shortcuts:** Ctrl + `=` or `+` zooms in, Ctrl + `-` zooms out, Ctrl + `0` resets to 100 %, and
Ctrl + mouse wheel zooms in and out. The steps are 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250 and 300 %.

```lua
Slider { value = bind(zoom, setZoom), from = 0.5, to = 2, stepSize = 0.1 }
```

The gallery's slider applies the zoom on release instead, because zooming moves the slider under the mouse while
you drag it.

## Snackbar

### `snackbar(text, opts?)`

Shows a brief message at the bottom of the window. There is one app-wide snackbar, and a new call replaces the
current message. It's rendered as an overlay above everything else.

| Option | Default | Meaning |
|---|---|---|
| `action` | `""` | Action button label (none if empty). |
| `onAction` | `nil` | Called when the action is clicked. The snackbar then closes. |
| `duration` | `4000` | Visible time in ms (plus a short fade). |
| `closable` | `false` | Shows a close icon. |

```lua
snackbar("Message archived", { action = "Undo", onAction = function() restore() end })
```

## Logging

Logging goes through spdlog on the C++ side, with the source tag `[lua]`.

| Function | Description |
|---|---|
| `log.trace/debug/info/warn/error(fmt, ...)` | `string.format(fmt, ...)` when arguments are given, otherwise `tostring(fmt)`. |
| `print(...)` | In UI scripts, logs its tab-joined arguments at info level. |

```lua
log.info("loaded %d items", #items)
log.warn("slow frame")
```

Script errors are logged at error level automatically.

## Misc

| Function | Description |
|---|---|
| `time() -> seconds` | Seconds since the app started (`ImGui::GetTime`). |
| `deltaTime() -> seconds` | Duration of the last frame. |
| `fps() -> number` | Current frame rate (ImGui's moving average). |
| `isDark() -> bool` | Whether the current theme is dark. |
| `imguiVersion() -> string` | Dear ImGui version, for example `"1.92.9b"`. |
| `quit()` | Ends the frame loop and exits. |
| `reload()` | Reloads the script before the next frame (like saving a file). |
| `setTitle(title)` | Sets the native window title, unless the `ApplicationWindow` has a `title` prop. See the note below. |
| `textSize(text, style?, font?) -> w, h` | Size of `text` in dp, for a [text style](theming.md#typography) (default `"bodyLarge"`), in a [named font](theming.md#fonts) when `font` is given. Uses the fonts in effect: right after `theme{}` the old ones until the next frame. |

> **Note.** The native title comes from, in order: the root `ApplicationWindow`'s `title` prop (re-read every
> frame, so it can be a binding), the last `setTitle()` call, and then the default `"imlui"`. A script that calls
> `setTitle()` should leave `title` off its `ApplicationWindow`.

## `native`

`native` (also `require "imlui.native"`) is the C++ library behind the functions above. The prelude wraps most of
it. The ones not wrapped are marked **native only**.

| Function | Description |
|---|---|
| `native.setTheme(t) -> scheme` | Backs `theme()`. |
| `native.schemeFor(t) -> scheme` | Backs `schemeFor()`. |
| `native.scheme() -> scheme` | Backs `scheme()`. |
| `native.isDark() -> bool` | Backs `isDark()`. |
| `native.color(c) -> hex` | Backs `color.resolve`. |
| `native.tone(c, t) -> hex` | Backs `color.tone`. |
| `native.mix(a, b, t) -> hex` | Backs `color.mix`. |
| `native.alpha(c, a) -> hex` | Backs `color.alpha`. |
| `native.time() -> seconds` | Backs `time()`. |
| `native.deltaTime() -> seconds` | Backs `deltaTime()`. |
| `native.fps() -> number` | Backs `fps()`. |
| `native.dpiScale() -> number` | Backs `dpiScale()`. |
| `native.contentScale() -> number` | Backs `contentScale()`. |
| `native.zoom() -> number` | Backs `zoom()`. |
| `native.setZoom(z)` | Backs `setZoom()`. |
| `native.setZoomShortcuts(on)` | Backs `setZoomShortcuts()`. |
| `native.windowSize() -> w, h` | Backs `windowSize()`. |
| `native.sizeClass() -> name` | Backs `sizeClass()`. |
| `native.sizeClassFor(w) -> name` | Backs `sizeClassFor()`. |
| `native.setWindowSize(w, h)` | Backs `setWindowSize()`. |
| `native.requestFrame(seconds?)` | Backs `requestFrame()`. Note: **seconds**, where `requestFrame` takes ms. |
| `native.textSize(text, style?, font?) -> w, h` | Backs `textSize()`. |
| `native.setTitle(title)` | Backs `setTitle()`. |
| `native.quit()` | Backs `quit()`. |
| `native.reload()` | Backs `reload()`. |
| `native.imguiVersion() -> string` | Backs `imguiVersion()`. |
| `native.log(level, message)` | Backs `log.*`. `level` is `"trace"`, `"debug"`, `"info"`, `"warn"` or `"error"` (anything else logs at info). |
| `native.injectMouse(x, y, down?, wheel?)` | **Native only.** Feeds ImGui a mouse event for scripted UI tests: position in window coordinates, an optional left-button state, and an optional vertical wheel delta. See [Development](development.md#scripted-ui-tests). |
| `native.injectKey(key, down, ctrl?, shift?, alt?)` | **Native only.** Feeds ImGui a key event for scripted UI tests, with the modifier states (sent first, so the key sees them). `key` is an ImGui key name, case-insensitive: `"Escape"`, `"Enter"`, `"Tab"`, `"Space"`, `"A"`..`"Z"`, `"0"`..`"9"`, `"F1"`..`"F12"`, `"Equal"`, `"Minus"`, `"LeftArrow"`, `"Keypad1"`, ..., or an alias (`"Esc"`, `"Return"`, `"Left"`/`"Right"`/`"Up"`/`"Down"`, `"Ctrl"`, `"Shift"`, `"Alt"`, `"="`, `"-"`). An unknown name raises an error. Starts the same settle period as real input. A press and a release sent together are seen in consecutive frames. |
| `native.reportError(message)` | **Native only.** Shows `message` in the error overlay (and logs it once). `ui.reportError(message)` wraps it. |
