---@meta imlui
-- LuaLS (lua-language-server) definitions of the imlui DSL: what src/resources/imlui.lua (the prelude) gives a UI
-- script, as free names and as `ui.*`. Not loaded at run time: an app writes it, with types.lua (the element types and
-- singletons it registered from C++), into a folder by running with `--lua-defs <dir>`, and points the editor's
-- `workspace.library` at that folder (docs/extending.md, "Editor support: Lua definitions").
-- Hand-written: keep it in step with the prelude (lua_definitions_test.cpp checks that every `ui` member is here, or,
-- for `connect` and the element constructors, which depend on the registered types, in the generated types.lua).

---The `ui` table: everything below, plus the element constructors (types.lua). Its members are also free names.
---@class ui
ui = {}

-------------------------------------------------------------------------------
-- Values
-------------------------------------------------------------------------------

---A color spec: a scheme role (`"primary"`), `"role/alpha"` (`"primary/0.5"`), hex (`"#6750A4"`, `"#6750A480"`) or
---`{ r, g, b, a }` with components 0..1.
---@alias Color string|number[]

---A two-way binding made by `bind(t, key)` or `bind(getter, setter)`: a widget's edits write through it.
---@class Binding

---A Material 3 window size class.
---@alias SizeClass "compact"|"medium"|"expanded"|"large"|"extraLarge"

---A color scheme: `{ role = "#RRGGBB" }`.
---@alias Scheme table<string, string>

-- `TextStyle` (ui.textSize's style) is declared in types.lua, with the other C++ enums.

-------------------------------------------------------------------------------
-- Elements
-------------------------------------------------------------------------------

---An element table: what a constructor returns. Its fields are its props (the C++ ones in types.lua), its `on*`
---handlers and its methods (`element:method()`).
---Element tables reached by id (`ids.name`, a component's `ids`): any element type, so any field.
---@alias Ids table<string, any>

---@class Element
---@field id? string
---@field parent? Element The element that lays this one out (read-only; nil for the root).
---@field visible? boolean
---@field enabled? boolean
---@field tooltip? string
---@field hovered? boolean True while the mouse is over the element (opt-in: give it `hovered = false` or `onHoverChanged`).
---@field onHoverChanged? fun(self: Element, hovered: boolean)
---@field width? number dp; -1 fills, < -1 fills minus |v| dp, 0 is automatic.
---@field height? number dp; -1 fills, < -1 fills minus |v| dp, 0 is automatic.
---@field fillWidth? boolean
---@field anchor? Anchor Inside a `Box`: where the element sits in it.
---@field margin? number Inside a `Box`: dp from its anchored edges, every side.
---@field marginLeft? number
---@field marginTop? number
---@field marginRight? number
---@field marginBottom? number

---Where an element sits in a `Box`: a corner, the middle of an edge, or the center.
---@alias Anchor "topLeft"|"top"|"topRight"|"left"|"center"|"right"|"bottomLeft"|"bottom"|"bottomRight"

---What every constructor takes: the common props and the children (positional values).
---@class ElementProps
---@field [integer] Element|string|number|false|nil A child, or the type's positional prop.
---@field id? string Makes the element reachable by name (`nameField.text`, `ids.nameField` in a component).
---@field visible? boolean|(fun(self: Element): boolean?)|Binding
---@field enabled? boolean|(fun(self: Element): boolean?)|Binding
---@field tooltip? string|(fun(self: Element): string?)|Binding
---@field hovered? boolean|Binding Opt-in hover tracking: imlui keeps it true while the mouse is over the element.
---@field onHoverChanged? fun(self: Element, hovered: boolean) Called when `hovered` changes; turns hover tracking on.
---@field width? number|(fun(self: Element): number?)|Binding dp; -1 fills, < -1 fills minus |v| dp.
---@field height? number|(fun(self: Element): number?)|Binding dp; -1 fills, < -1 fills minus |v| dp.
---@field fillWidth? boolean|(fun(self: Element): boolean?)|Binding
---@field anchor? Anchor|(fun(self: Element): Anchor?)|Binding Inside a `Box`: where the element sits in it (`"topLeft"` by default).
---@field margin? number|(fun(self: Element): number?)|Binding Inside a `Box`: dp from its anchored edges, every side.
---@field marginLeft? number|(fun(self: Element): number?)|Binding Inside a `Box`; overrides `margin` on that side.
---@field marginTop? number|(fun(self: Element): number?)|Binding Inside a `Box`; overrides `margin` on that side.
---@field marginRight? number|(fun(self: Element): number?)|Binding Inside a `Box`; overrides `margin` on that side.
---@field marginBottom? number|(fun(self: Element): number?)|Binding Inside a `Box`; overrides `margin` on that side.

---Returns a constructor for element type `name`. Calling it with a table turns that table into the element.
---@param name string
---@return fun(props?: table|string|number): Element
function element(name) end

---The registered element type names, sorted.
---@return string[]
function types() end

---Registers `props.id` in the current id scope (constructors do this).
---@param props table
function registerId(props) end

---Whether an app-level id can be referenced by name (not a `ui` member or a Lua global); reports it otherwise.
---@param id any
---@return boolean
function checkId(id) end

---App-level element ids (weak): `ids.nameField`, also as the free name `nameField`.
---@type Ids
ids = {}

---The environment UI scripts run in.
---@type table
env = {}

---Rows that live in C++ (`imlui::ListModel`, `imlui::VectorModel<T>`): `model[i]` is row `i` (1-based; nil out of
---range), `#model` the row count, `ipairs(model)` walks the rows. A row is converted when it is first read, and an
---unchanged row reads as the same table while something holds it. Give it to a `Repeater` or `ListView` as its
---`model`. A model that was destroyed reads as empty. `model:set(i, v)` writes a row when the C++ side allows it, and
---`connect(model, "onRowsInserted", fn)` listens to its changes (`ListModelSignal`).
---@class ListModel<T>: { [integer]: T }
---@operator len: integer
local ListModel = {}

---The number of rows (`#model`).
---@return integer
function ListModel:count() end

---Row `i`'s key (an integer or a string); nil when the model has no keys.
---@param i integer
---@return integer|string|nil
function ListModel:key(i) end

---Whether the model identifies its rows by key: delegates are then kept by key without a `key` function.
---@return boolean
function ListModel:hasKeys() end

---A number that changes with every change of the model; -1 once the model was destroyed.
---@return integer
function ListModel:revision() end

---Replaces row `i` (1-based) with `v`, converted to the row type, when the C++ side made the model writable
---(`VectorModel::setScriptWritable`); raises otherwise, out of range or for a value that doesn't convert. Notifies like
---a C++ edit (`onRowsChanged`).
---@param i integer
---@param v any A row: a value of the model's row type (a table for a struct).
function ListModel:set(i, v) end

---The signals of a list model, for `connect(model, signal, fn)`: `onRowsInserted(first, count)`,
---`onRowsRemoved(first, count)`, `onRowsChanged(first, count)`, `onRowsMoved(first, count, destination)` (1-based),
---`onRowsRemapped()` (a sort, a replace) and `onReset()`. Called right after the change.
---@alias ListModelSignal "onRowsInserted"|"onRowsRemoved"|"onRowsChanged"|"onRowsMoved"|"onRowsRemapped"|"onReset"

-- The Lua-side containers' own props (their constructors and classes are in types.lua).

---@alias ModelValue any[]|ListModel<any>|integer

---A `Repeater` / `ListView` delegate whose `item` is a `T`. LuaLS doesn't infer a callback's parameter from a generic
---field, so type the delegate where it is written, with the model's row type (`ListModel<LogLine>` -> `LogLine`):
---
---```lua
---ListView { model = backend.logs,
---    ---@type Delegate<LogLine>
---    delegate = function(line) return Text { line.text } end }
---```
---@alias Delegate<T> fun(item: T, index: integer, ids: Ids): Element?

---A `key` function for items of type `T` (see `Delegate`).
---@alias KeyFunction<T> fun(item: T, index: integer): any

---@class RepeaterProps
---@field model ModelValue|(fun(self: Element): ModelValue)|Binding The items: a list, a C++ list model, or a number n for 1..n.
---@field delegate Delegate<any> Builds one item's element (`---@type Delegate<Row>` above it types `item`).
---@field key? KeyFunction<any> Keeps delegates by key instead of by index (a list model's own keys without it).

---@class ListViewProps
---@field model? ModelValue|(fun(self: Element): ModelValue)|Binding The items: a list, a C++ list model, or a number n for 1..n.
---@field delegate? Delegate<any> Builds one item's element (`---@type Delegate<Row>` above it types `item`).
---@field key? KeyFunction<any> Keeps delegates by key instead of by index (a list model's own keys without it).

---@class IfProps
---@field condition any|(fun(self: Element): any)|Binding Shows the children while truthy.
---@field otherwise? Element Shown while `condition` is falsy.

---@class LoaderProps
---@field item Element|(fun(self: Element): Element?)|nil The element shown.

---@class ResponsiveProps
---@field compact? Element|false|(fun(self: Element): Element?)
---@field medium? Element|false|(fun(self: Element): Element?)
---@field expanded? Element|false|(fun(self: Element): Element?)
---@field large? Element|false|(fun(self: Element): Element?)
---@field extraLarge? Element|false|(fun(self: Element): Element?)

-------------------------------------------------------------------------------
-- State and bindings
-------------------------------------------------------------------------------

---An observable table: a write that changes a top-level value calls the watchers of its key, then the `"*"` ones.
---@param init? table
---@return table<string, any>
function state(init) end

---Calls `fn(new, old, key)` whenever `state[key]` changes (`"*"`: any key). Returns a function that removes it.
---@param state table A table made by state().
---@param key any
---@param fn fun(new: any, old: any, key: any)
---@return fun() stop
function watch(state, key, fn) end

---The data table behind a state() proxy (any other value as it is).
---@generic T
---@param state T
---@return T
function raw(state) end

---A two-way binding to `t[key]`, or to a getter and a setter.
---@param t table|fun(): any A table, or a getter.
---@param key any The key in `t`, or the setter `fun(value)`.
---@return Binding
function bind(t, key) end

---A reusable component: `fn(props, ids)` builds one instance; `ids` is the instance's own id scope.
---@generic P: table
---@param fn fun(props: P, ids: Ids): Element?
---@return fun(props?: P): Element
function component(fn) end

---`require`: `require "Name"` in a UI script loads `Name.lua` from the entry script's folder as a component.
import = require

-------------------------------------------------------------------------------
-- Resources
-------------------------------------------------------------------------------

---@class ui.resource
local resource = {}

---The full, normalized resource name of `name` (relative names resolve against the calling script's folder).
---@param name string
---@return string
function resource.url(name) end

---The contents of resource `name`, or nil and a message when there is no such file.
---@param name string
---@return string? text
---@return string? error
function resource.read(name) end

---Whether resource `name` is a file or a folder.
---@param name string
---@return boolean
function resource.exists(name) end

ui.resource = resource

-------------------------------------------------------------------------------
-- Window size and adaptive layouts
-------------------------------------------------------------------------------

---The size classes, smallest first.
---@type SizeClass[]
SIZE_CLASSES = { "compact", "medium", "expanded", "large", "extraLarge" }

---`t[class]`, else the value of the nearest smaller class `t` has, else of the smallest one it has.
---@generic V
---@param t table<SizeClass, V>
---@param class SizeClass
---@return V?
function pickSizeClass(t, class) end

---The window's size in dp.
---@return number w
---@return number h
function windowSize() end

---The size class of windowSize()'s width.
---@return SizeClass
function sizeClass() end

---The size class of a width in dp.
---@param width number
---@return SizeClass
function sizeClassFor(width) end

---Resizes the native window to `w` x `h` dp, after the current frame.
---@param w number
---@param h number
function setWindowSize(w, h) end

-------------------------------------------------------------------------------
-- Theme
-------------------------------------------------------------------------------

---A theme table (docs/theming.md). Every field is optional.
---@class ThemeTable
---@field design? "material"|"fluent"|string The skin that draws the controls: `"material"` (default), `"fluent"` or one the app registered in C++ (`registerSkin`). An unknown name reports an error and keeps `"material"`.
---@field tokens? ThemeTokens Overrides of the design's metrics (control height, radii, strokes, ...); Fluent draws with them, Material ignores them.
---@field preset? "material"|"imgui-dark"|"imgui-light"|"imgui-classic" How ImGui's own style is set (default `"material"`).
---@field seed? Color The color the scheme is generated from (default `"#6750A4"`).
---@field dark? boolean Dark or light scheme (default `true`).
---@field spacing? number Base spacing unit in dp (default 8).
---@field fontScale? number Multiplies every text size, 0.25 to 4 (default 1).
---@field shape? table<string, number> Corner radii in dp: `extraSmall`, `small`, `medium`, `large`, `extraLarge`.
---@field colors? table<string, Color> Color role overrides.
---@field imgui? table<string, Color> Raw `ImGuiCol_*` overrides by name.
---@field style? table<string, number|number[]> Raw `ImGuiStyle` overrides by field name.
---@field fonts? table<string, string|string[]> Text fonts by resource name: `regular`, `medium`, `fallback`, named fonts.

---A design's metrics (docs/theming.md, Tokens): what `theme { tokens = { ... } }` overrides. Every field is optional;
---lengths are dp. An unknown token or a value out of range reports an error and keeps the default.
---@class ThemeTokens
---@field controlHeight? number A standard control's height: buttons, text fields, combo boxes (8..200; Fluent 32).
---@field compactHeight? number A compact control's height: menu rows, small buttons (8..200; Fluent 24).
---@field controlRadius? number The corner radius of controls (0..100; Fluent 4).
---@field overlayRadius? number The corner radius of overlays and containers: cards, flyouts, dialogs (0..100; Fluent 8).
---@field strokeWidth? number A control's border width; 0: none (0..8; Fluent 1).
---@field focusStrokeWidth? number The keyboard focus rectangle's outer stroke width; 0: none (0..8; Fluent 2).
---@field focusOffset? number How far the focus rectangle lies outside the control (0..16; Fluent 3).
---@field inputUnderline? boolean Whether inputs carry an accent underline when focused (Fluent true).
---@field inputUnderlineWidth? number The focused input underline's width (0..8; Fluent 2).
---@field buttonFill? "filled"|"outline" How accent buttons (`filled`, `tonal`) are drawn (Fluent `"filled"`).
---@field paddingX? number The horizontal padding inside controls (0..100; Fluent 12).
---@field gap? number The gap between a control's parts: icon and label, label and glyph (0..100; Fluent 8).
---@field iconSize? number The size of icons inside controls (8..64; Fluent 16).
---@field density? number Multiplies heights, paddings and gaps, not radii or strokes (0.5..2; Fluent 1).

---Applies theme table `t` at the start of the next frame and returns the scheme it produces (docs/theming.md).
---@param t ThemeTable
---@return Scheme
function theme(t) end

---The scheme of the theme in effect.
---@return Scheme
function scheme() end

---The scheme `t` would produce, without applying it.
---@param t ThemeTable
---@return Scheme
function schemeFor(t) end

---A state() that applies itself as the theme now and on every change.
---@generic T: table
---@param init T
---@return T
function themeState(init) end

---Color functions; they return `"#RRGGBB"` (or `"#RRGGBBAA"`) and raise for an invalid color.
---@class ui.color
color = {}

---Linear sRGB interpolation from `a` to `b`.
---@param a Color
---@param b Color
---@param t number 0..1
---@return string
function color.mix(a, b, t) end

---Material tone `t` (0 = black, 100 = white) of `c`'s hue and chroma.
---@param c Color
---@param t number
---@return string
function color.tone(c, t) end

---`c` with alpha `a`.
---@param c Color
---@param a number
---@return string
function color.alpha(c, a) end

---A spec resolved against the current scheme.
---@param c Color
---@return string
function color.resolve(c) end

---Whether the current theme is dark.
---@return boolean
function isDark() end

-------------------------------------------------------------------------------
-- Timers and frames
-------------------------------------------------------------------------------

---Calls `fn()` once after `ms` milliseconds (nil or 0: next frame).
---@param fn fun()
---@param ms? number
---@return integer id
function setTimeout(fn, ms) end

---Calls `fn()` every `ms` milliseconds (a positive, finite number).
---@param fn fun()
---@param ms number
---@return integer id
function setInterval(fn, ms) end

---Cancels a timeout or interval (nil is ignored).
---@param id integer?
function clearTimer(id) end

---Calls `fn(time)` at the start of every rendered frame, after the timers. Returns a function that removes it.
---@param fn fun(time: number)
---@return fun() stop
function onFrame(fn) end

---Asks for a frame now, or after `ms` milliseconds.
---@param ms? number
function requestFrame(ms) end

-------------------------------------------------------------------------------
-- Async
-------------------------------------------------------------------------------

---A value that arrives later: a C++ method's result, sleep(), an async function's result, future().
---@class Future<T>
---@field state "pending"|"done"|"failed"|"cancelled"
---@field value? T The first value, once done.
---@field error? any The error, once failed; "cancelled" once cancelled.
---@field progress? number The last progress reported while pending.
local Future = {}

---When done, `onValue(values...)`; when failed, `onError(err)`. The new future settles with what the handler returns.
---@param onValue? fun(...: any): any
---@param onError? fun(err: any): any
---@return Future<any>
function Future:next(onValue, onError) end

---`f:next(nil, onError)`.
---@param onError fun(err: any): any
---@return Future<any>
function Future:catch(onError) end

---Settles a pending future as cancelled and stops what it waits on. False when it already settled.
---@return boolean
function Future:cancel() end

---Calls `fn(progress)` at each progress reported until the future settles. Returns the future.
---@param fn fun(progress: number)
---@return self
function Future:onProgress(fn) end

---Only for a future made by future(): reports progress `p`. False once settled.
---@param p number
---@return boolean
function Future:report(p) end

---Only for a future made by future(): settles it with `values`. Whether it settled it.
---@param ... any
---@return boolean
function Future:resolve(...) end

---Only for a future made by future(): fails it with `err`. Whether it settled it.
---@param err any
---@return boolean
function Future:reject(err) end

---Calling the result runs `fn(...)` in a coroutine and returns a future of its results.
---@param fn fun(...: any): ...
---@return fun(...: any): Future<any>
function async(fn) end

---Inside an async function: waits for future `f` and returns its values, or raises its error.
---@generic T
---@param f Future<T>
---@return T
---@overload fun(v: any): any
function await(f) end

---A future done after `ms` milliseconds.
---@param ms? number
---@return Future<nil>
function sleep(ms) end

---A future done with the list of the first values of `list` once all are done.
---@param list any[]
---@return Future<any[]>
function all(list) end

---A future that settles like `f`, or fails with "timed out after <ms> ms" (releasing `f`) if `f` is still pending
---after `ms` milliseconds.
---@generic T
---@param f Future<T>
---@param ms number
---@return Future<T>
function timeout(f, ms) end

---A future that settles like the first of `list` to settle (done, failed or cancelled); the others are released.
---@param list any[]
---@return Future<any>
function race(list) end

---A pending future the script settles with `f:resolve(...)` / `f:reject(err)`.
---@return Future<any>
function future() end

-------------------------------------------------------------------------------
-- Snackbar, logging, misc
-------------------------------------------------------------------------------

---@class SnackbarOptions
---@field action? string Action button label.
---@field onAction? fun() Called when the action is clicked.
---@field duration? number Visible time in ms (default 4000).
---@field closable? boolean Shows a close icon.

---Shows a brief message at the bottom of the window.
---@param text string
---@param opts? SnackbarOptions
function snackbar(text, opts) end

---Logging through the C++ logger: `string.format(fmt, ...)` when arguments are given, else `tostring(fmt)`. At a
---level that's off the message isn't built (no `string.format`, no `__tostring`).
---@class ui.log
log = {}
---@param fmt any
---@param ... any
function log.trace(fmt, ...) end
---@param fmt any
---@param ... any
function log.debug(fmt, ...) end
---@param fmt any
---@param ... any
function log.info(fmt, ...) end
---@param fmt any
---@param ... any
function log.warn(fmt, ...) end
---@param fmt any
---@param ... any
function log.error(fmt, ...) end
---Whether `log[level]` logs: check it before gathering an expensive message.
---@param level "trace"|"debug"|"info"|"warn"|"error"
---@return boolean
function log.enabled(level) end

---Shows a script error in the error overlay (logged once).
---@param message string
function reportError(message) end

---Seconds on the app clock.
---@return number
function time() end

---Duration of the last frame, in seconds.
---@return number
function deltaTime() end

---Current frame rate.
---@return number
function fps() end

---The Dear ImGui version.
---@return string
function imguiVersion() end

---Ends the frame loop and exits.
function quit() end

---Reloads the script before the next frame.
function reload() end

---Sets the native window title (unless the ApplicationWindow has a `title`).
---@param title string
function setTitle(title) end

---Size of `text` in dp for a text style (default "bodyLarge"), in a named font when given.
---@param text string|number
---@param style? TextStyle
---@param font? string
---@return number w
---@return number h
function textSize(text, style, font) end

---The user zoom (1 = 100 %).
---@return number
function zoom() end

---Sets the user zoom, clamped to 0.5..3, from the next frame.
---@param z number
function setZoom(z) end

---The effective dp -> pixel factor: contentScale() * zoom().
---@return number
function dpiScale() end

---The monitor content scale.
---@return number
function contentScale() end

---Turns the built-in zoom shortcuts (Ctrl +/-/0/wheel) on or off.
---@param on boolean
function setZoomShortcuts(on) end

---Turns culling on or off for every container.
---@param on boolean
function setCulling(on) end

---Whether culling is on.
---@return boolean
function culling() end

-------------------------------------------------------------------------------
-- native
-------------------------------------------------------------------------------

---The C++ library behind the functions above (`require "imlui.native"`).
---@class imlui.native
---@field setTheme fun(t: ThemeTable): Scheme
---@field schemeFor fun(t: table): Scheme
---@field scheme fun(): Scheme
---@field isDark fun(): boolean
---@field color fun(c: Color): string
---@field tone fun(c: Color, t: number): string
---@field mix fun(a: Color, b: Color, t: number): string
---@field alpha fun(c: Color, a: number): string
---@field time fun(): number
---@field deltaTime fun(): number
---@field fps fun(): number
---@field dpiScale fun(): number
---@field contentScale fun(): number
---@field zoom fun(): number
---@field setZoom fun(z: number)
---@field setZoomShortcuts fun(on: boolean)
---@field setCulling fun(on: boolean)
---@field culling fun(): boolean
---@field windowSize fun(): number, number
---@field sizeClass fun(): SizeClass
---@field sizeClassFor fun(width: number): SizeClass
---@field setWindowSize fun(w: number, h: number)
---@field requestFrame fun(seconds?: number)
---@field textSize fun(text: string|number, style?: TextStyle, font?: string): number, number
---@field setTitle fun(title: string)
---@field quit fun()
---@field reload fun()
---@field imguiVersion fun(): string
---@field log fun(level: "trace"|"debug"|"info"|"warn"|"error", message: any)
---@field logEnabled fun(level: "trace"|"debug"|"info"|"warn"|"error"): boolean
---@field injectMouse fun(x: number, y: number, down?: boolean, wheel?: number)
---@field injectKey fun(key: string, down: boolean, ctrl?: boolean, shift?: boolean, alt?: boolean)
---@field reportError fun(message: string)
---@field setScopeHooks fun(scopes: table, callIn: fun(scope: table|false, fn: function, ...: any): ...) Internal: called once by the prelude.
native = {}

-------------------------------------------------------------------------------
-- The same functions as members of `ui`
-------------------------------------------------------------------------------

ui.native = native
ui.element = element
ui.types = types
ui.registerId = registerId
ui.checkId = checkId
ui.ids = ids
ui.env = env
ui.state = state
ui.watch = watch
ui.raw = raw
ui.bind = bind
ui.component = component
ui.import = import
ui.SIZE_CLASSES = SIZE_CLASSES
ui.pickSizeClass = pickSizeClass
ui.windowSize = windowSize
ui.sizeClass = sizeClass
ui.sizeClassFor = sizeClassFor
ui.setWindowSize = setWindowSize
ui.theme = theme
ui.scheme = scheme
ui.schemeFor = schemeFor
ui.themeState = themeState
ui.color = color
ui.isDark = isDark
ui.setTimeout = setTimeout
ui.setInterval = setInterval
ui.clearTimer = clearTimer
ui.onFrame = onFrame
ui.requestFrame = requestFrame
ui.async = async
ui.await = await
ui.sleep = sleep
ui.all = all
ui.timeout = timeout
ui.race = race
ui.future = future
ui.snackbar = snackbar
ui.log = log
ui.print = print
ui.reportError = reportError
ui.time = time
ui.deltaTime = deltaTime
ui.fps = fps
ui.imguiVersion = imguiVersion
ui.quit = quit
ui.reload = reload
ui.setTitle = setTitle
ui.textSize = textSize
ui.zoom = zoom
ui.setZoom = setZoom
ui.dpiScale = dpiScale
ui.contentScale = contentScale
ui.setZoomShortcuts = setZoomShortcuts
ui.setCulling = setCulling
ui.culling = culling

return ui
