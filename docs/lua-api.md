# Lua API

Everything here is defined in `src/resources/imlui.lua` (the DSL prelude) or in the native `imlui.native` library it
wraps. For completion and type checks in an editor, an app writes Lua language-server definitions of all of it and of
its registered types with `--lua-defs <dir>` ([Editor support](extending.md#editor-support-lua-definitions)).

## The script environment

A UI script runs in `ui.env`, not in `_G`. It must **return the root element**, usually an `ApplicationWindow`.
The engine reports an error if the script returns anything other than a table. (Precisely: each script loaded as a
resource, the entry script and every component, gets its own thin environment over `ui.env`, whose `ui`, `Image`,
`theme`, `themeState` and `resource` know the script's name, so [relative names](#resources) resolve against its
folder. Globals, app-level ids and fields set on `ui` are shared by all scripts, as if they ran in `ui.env` itself.)

A free name `x` in a script is looked up in this order:

1. **`ui.x`**: everything on this page, plus the built-in element constructors. `ui` itself is also available.
2. **Lua globals** (`_G`): `math`, `string`, `table`, `os`, `io`, `require`, ..., and the
   [C++ singletons](extending.md#singletons) the app registered (for example `backend`).
3. **App-level element ids**: `TextField { id = "nameField" }` makes `nameField` refer to that element table, QML
   style (see [Element ids](#element-ids)).
4. **Any other Capitalized name** becomes an element constructor (`ui.element(name)`), so elements registered
   from C++ work without any Lua changes. For a [registered C++ type](extending.md) it's a typed constructor.
   The constructor stays below the ids: a name read before an element registers it as an id (a binding that runs
   before a `Loader` builds `Card { id = "Details" }`) is the id from then on, not stuck as a constructor.

```lua
TextField { id = "nameField", label = "Name", text = "" },
Text { text = function() return "Hello, " .. nameField.text end },
```

### Element ids

Every constructor registers its `id` (`ui.registerId(props)`), the Lua-side `Repeater`, `ListView`, `If`,
`Loader` and `Responsive` included: `ListView { id = "list", ... }` is `list`. Where it goes depends on the
**scope** the element is built in:

| Built ... | Registers in | Free name? |
|---|---|---|
| outside every component instance and delegate: the entry script, the top level of a `require`d module (whoever requires it first) | the **app** scope, `ui.ids` | yes |
| while a [component](#componentfn---constructor) function runs (nested elements included) | that **instance's** `ids` table, the second argument of `fn(props, ids)` | no |
| while a [`Repeater` / `ListView` delegate](#repeater--model-delegate-) runs | that **delegate instance's** `ids`, the third argument of `delegate(item, index, ids)` | no |
| later, by a function of an element built in a component instance or delegate (see below) | that instance's or delegate's `ids` | no |
| later, by a timer, watcher or `onFrame` callback set up while an instance or delegate was being built | that instance's or delegate's `ids` | no |
| later, by any other code (functions of app-level elements, timers and watchers set at app level) | the app scope | yes |

**Functions run in their element's scope.** Every element built in a component instance or delegate remembers that
scope, and its functions run in it whenever imlui calls them: its bindings (`text = function(self) ... end`), its
`on*` handlers (from C++ widgets and registered types alike, `on<Name>Changed` included), a `Canvas`'s `onPaint`,
and the functions the Lua-side containers call (`model`, `condition`, a `Loader`'s `item`, a `Responsive` class
function). So a button inside a component that builds a `Text { id = "row" }` when clicked registers it in that
instance's `ids`, and two instances don't clash. `ui.setTimeout`, `ui.setInterval`, `ui.watch` and `ui.onFrame`
capture the scope that is active when they're called, and run their callback in it, the app scope included: an
app-level watcher that a component's handler triggers (by writing the state) builds into `ui.ids`, not into the
component's `ids`. So do a future's `next`/`catch`/`onProgress` handlers and an async function's coroutine. The scope
is popped however the call ends (a to-be-closed variable), so a handler that raises can't leave it behind. An async
function keeps its scopes to itself: one that awaits while a component it builds is in its scope doesn't leave that
scope active for other code while it waits, gets it back when it resumes, and pops it when it's cancelled. Not covered: the getter and
setter of a `bind(getter, setter)` (they run in whatever scope is active, app level when imlui calls them), and
functions of an app-level element, which run in whatever scope is active (app level whenever the engine calls them).

So two instances of a component can both declare `id = "field"`: each reaches its own as `ids.field`, and neither
is a free name. A nested component instance has its own `ids`, which the outer one doesn't see. The app-level ids
stay free names everywhere, inside components too. Lookup of a free name is lazy, so a binding can refer to an id
that is declared later in the file.

```lua
local LabeledField = component(function(props, ids)
    return Column {
        TextField { id = "field", label = props.label, text = "" },
        Text { text = function() return #ids.field.text .. " characters" end },
    }
end)
```

**Ids are weak.** `ui.ids` and every `ids` table don't keep their elements alive: the tree does (or a variable, a
table, a closure). When an element leaves the tree (a `Loader` showing another item, a delegate the model dropped)
and nothing else references it, it is collected and its id reads `nil`; a binding that still uses it reports the
usual error (`attempt to index a nil value (global 'field')`) in the error overlay. An element registered under an
id is kept until the next frame starts, so a script can build it in one statement and put it in the tree later:

```lua
Text { id = "title", text = "Hello" }  -- nothing but the id refers to it yet
return ApplicationWindow { title }     -- fine: the tree holds it from here on
```

**Duplicates.** An id that a scope already has for another element is reported in the error overlay, for example
`duplicate element id 'name' in the app scope; the first element keeps it`, or `... in component (app:/Form.lua:3)`
/ `... in Repeater delegate (app:/main.lua:12)`, naming where the component or delegate function is defined. The
first element keeps the id. Registering the same element again is not a duplicate, and a load (a hot reload too)
starts without ids.

**Id collisions.** Because free names are looked up in `ui.*` and the globals first, an app-level id such as
`"table"`, `"io"` or `"state"` can't be referenced by name: the name finds the global or the `ui` member instead.
The element constructor reports such an id in the error overlay when the element is created, for example
`element id 'io' collides with Lua global 'io'; it can't be referenced by name — rename it` (or
`... collides with ui member 'ui.state' ...`). The element is still created, and `ui.ids.io` still holds it; only
the free name doesn't. Rename the id. (`ui.checkId(id)` runs the same check and returns `false` for such an id.)
Ids in a component or delegate scope aren't free names, so they aren't checked.

### `self.parent`

Every element has a read-only `parent`: the element that lays it out, or `nil` for the root (and for an element no
container took yet). Bindings and handlers reach it through `self`:

```lua
Card { title = "Totals",
    Text { text = function(self) return self.parent.title end } }
```

- The constructor of a container sets it for its children: positional children, and the element props it renders
  (`menu`, `menuBar`, `header`, `otherwise`, a `Responsive`'s classes). Children are built first, so `parent` is
  set once the container's constructor runs, not while the child's own does. Positional children are taken up to
  `rawlen`, as the renderer takes them, so a child after a `nil` (an unset variable) gets its parent too.
- **Inline containers are skipped**, as in QML: a child of a `Repeater`, `If`, `Loader` or `Responsive` gets the
  element that lays the container out (the `Column` around the `Repeater`), at any nesting depth. An inline
  container that is the root is its children's parent. `Repeater` delegates and a `Loader`'s or `Responsive`'s
  item get theirs when they're built or shown (at render time). A `ListView` (not inline) is its delegates' parent.
- An element that is a child in several places has the parent that took it last.
- It's a **weak** reference: it never keeps the parent alive.
- It's read-only. `parent = ...` in a constructor is reported and ignored. Assigning it later stores a field that the
  next sync of the element drops (debug builds report it), so it has no lasting effect.

`parent` isn't stored in the element. It's in one side table (`parents[element] = parent`, weak keys and values),
which the metatable that all elements of a type share reads: `rawget(el, "parent")` is `nil`, and it isn't a
property the C++ side syncs. Writes to elements don't go through any hook.

Indices (`currentIndex`, `currentRow`, delegate `index`) are **1-based**.

## Elements

### `element(name) -> constructor`

Returns a constructor for element type `name`. Calling it with a table **turns that table into the element**
(it is not copied): the prelude sets `__type`, gives the table the metatable that all elements of the type share
(for its methods and [`parent`](#selfparent)), sets its children's `parent`, remembers the id scope it's built in and
registers `id`. The table's identity matters because it is the element's ImGui ID and
holds its per-element state.

```lua
Button { text = "OK", onClicked = function(self) print("clicked", self.text) end }
Button "OK"      -- a non-table argument becomes the first positional value: { "OK" }
```

For a type registered from C++ with `Engine::registerType<T>()`, the constructor also creates the C++ instance
(stored in the table as `__native`, an opaque userdata whose metatable `getmetatable` doesn't return; it lives as
long as the table, even if a script clears the field), and the type's shared metatable makes its methods callable:
`gauge:reset()`. Singleton globals hide their metatable the same way. (`Repeater`, `ListView`, `If`, `Loader` and `Responsive` get the metatable at once and their
instance at their first render.) See [Registering C++ types](extending.md).

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

A registered C++ type may declare a property that takes a function (a callback, `LuaFunction` in C++): there a
function is the value itself, not a binding, and isn't called on reads. The same holds for method arguments:
`backend:forEach(function(item) ... end)`. Tables passed to C++ (structs, maps, sequences) are read raw, without
their metatables, and enums are passed as their names (`quality = "high"`). See
[Value conversion](extending.md#value-conversion).

### Signals

Signals are `on*` handlers called as `handler(self, ...)`, for example `onClicked(self)`,
`onToggled(self, checked)`, `onMoved(self, value)`, `onTextChanged(self, text)`, `onAccepted(self, text)` and
`onActivated(self, index, text)`. Errors in handlers are caught and shown in the error overlay. A handler field holds
one function; to listen from several places, [`connect`](#connecttarget-signal-fn---disconnect).

### `connect(target, signal, fn) -> disconnect`

Adds a listener to a signal of a C++ element (built-in or [registered](extending.md#signals)) or a
[singleton](extending.md#singletons): `fn` runs whenever `target` emits `signal`, with the arguments its handler gets
(`fn(self, ...)` on an element, `fn(...)` on a singleton). The signal is one the type declares or a property's
`on<Name>Changed`; anything else raises (`connect: sensor has no signal 'onPaek'`), as does a target that isn't a C++
element or singleton (a Lua-side container, a plain table). Returns a function that disconnects it; calling it again
does nothing.

```lua
local stop = connect(lab, "onTelemetryChanged", function(t) s.cpu = t.cpu end)
connect(saveButton, "onClicked", function(self) log.info("saved") end)
stop() -- that listener no longer runs
```

- **Order.** The handler field (`onClicked = ...`) first, then the listeners in connect order.
- **During a dispatch.** A listener may disconnect itself or others: a disconnected one that hasn't run yet doesn't.
  One connected during a dispatch runs from the next one on.
- **Errors.** A listener's error goes to the error overlay; the other listeners still run.
- **Lifetime.** A connection never keeps its target alive: an element held only by a connection (or by its own
  listener) is collected, and the connection with it. One made inside a [component](#componentfn---constructor)
  instance or delegate (while it is built, or by its elements' handlers) runs in its id scope, like a `watch`, and
  belongs to it: once the instance's elements are collected, the connection goes too. Collection isn't immediate;
  call `disconnect` to stop it at a known time. One made at app level stays until disconnected.
- **Hot reload.** Connections belong to the Lua state: a reload starts without them, so the new script connects
  again.

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

A watcher added inside a [component](#componentfn---constructor) instance or a `Repeater`/`ListView` delegate (while
it is built, or by its elements' handlers) runs in its id scope and **belongs to it**, like a
[connection](#connecttarget-signal-fn---disconnect): once the instance's elements are collected, the watcher goes too
(and it doesn't keep them alive). So a delegate that watches a state can be rebuilt any number of times (scrolling,
a changed model) without leaving watchers behind. Collection isn't immediate; call `stop` to end it at a known time.
One added at app level stays until stopped.

```lua
watch(s, "count", function(new, old) log.info("count %d -> %d", old, new) end)
local stop = watch(look, "*", applyTheme)
stop() -- applyTheme no longer runs
```

### `raw(state) -> table`

Returns the underlying (non-proxy) data table, for example for serialization or to pass to `theme()`. For any other
value it returns the value unchanged.

### `bind(t, key)` / `bind(getter, setter)`

Creates a two-way binding object. `t` can be any table: a `state`, or a plain table like `sel.tags` (or any value
that can be indexed). Reads and writes of `t[key]` run protected: an error in them, such as one raised by a
`watch()` callback that the write triggers, is reported in the error overlay and doesn't stop the frame.

```lua
Switch { checked = bind(s, "wifi") }
Chip   { checked = bind(sel.tags, tag) }               -- plain nested table
Slider { value = bind(function() return s.v * 100 end,  -- getter
                      function(v) s.v = v / 100 end) }  -- setter
```

## Components and `require`

### `component(fn) -> constructor`

Wraps `fn(props, ids)` so that `MyComp { ... }` calls it with the props table (or `{}` if there are none) and a
new `ids` table for that instance. Locals created inside `fn` are per instance, and so are ids: the elements
built while `fn` runs register their ids in `ids`, not as free names (see [Element ids](#element-ids)), so every
instance has its own `ids.field`. `MyComp { id = "x" }` registers the instance's root element as `x` in the scope
that creates the instance (`ui.ids` at app level). A component that raises leaves the scopes as they were: the
next element registers where it would have anyway.

### `require "Name"`

Inside a UI script, `require "Name"` first looks for the file `Name.lua` (then `Name/init.lua`; a folder named
`Name.lua` doesn't count) **in the entry script's folder**
(`app:/` for `app:/main.lua`) and runs it in the UI environment, the way QML files work as components. `a.b` is
`a/b.lua`. Module names are global (one `package.loaded` entry per name), so `require "pages.home"` means
`app:/pages/home.lua` from every script; relative resource names *inside* `pages/home.lua` (`ui.resource`, `Image`,
theme fonts) resolve against `app:/pages/`. Results are cached in `package.loaded` like any `require`. `import` is an alias of `require`. If the
module isn't there, it's looked up in `imlui:/` (in the global environment), and nowhere else: `require` never reads
files from disk or loads DLLs (`package.path` and `package.cpath` are `""`, `package.loadlib` is nil, and only
`package.preload` is searched besides the resources), so a plain Lua library goes into the app's resources too. A
module that isn't found is an error that lists each resource name looked at. Errors and tracebacks name the file as a
resource: `app:/StatCard.lua:3: ...`.

```lua
-- StatCard.lua
return component(function(props, ids)
    return Card { variant = props.variant or "filled", padding = 20,
        Text { id = "title", text = props.title, style = "titleSmall", color = "onSurfaceVariant" },
        Text { text = props.value, style = "headlineMedium",
               color = function() return ids.title.text == "" and "outline" or props.color or "primary" end },
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

The script is known when it is loaded, not looked up on the call stack, so tail calls work too:
`return ui.resource.read("x")` and a function defined in `app:/pages/home.lua` and called from elsewhere both resolve
against `app:/pages/`. Only code that doesn't run as a resource script (code using `require("imlui")` directly, or
code loaded from a string) finds the calling script from the caller's chunk name, falls back to the main script, and
raises an error for a relative name in `return ui.resource.read("x")`, where Lua drops the caller's frame: use a full
name, a local, or parentheses (`return (ui.resource.read("x"))`) there.

Every resource a script reads or loads is watched for hot reload when it comes from a folder on disk.

[`Image`](elements.md#image) takes a resource name as its `source`, resolved the same way against the script that
creates the `Image`: `Image { source = "img/logo.png" }`. So do the font names of
[`theme { fonts = ... }`](theming.md#fonts). An image file that changes on disk is decoded again
without a script reload.

## Dynamic children

`Repeater`, `If`, `Loader` and [`Responsive`](#responsive-compact-medium-expanded-large-extralarge) are **inline**: their children are laid out as if they were direct children of the
parent. A `Repeater` in a `Row` flows horizontally, and in a `Column` it stacks vertically. `visible = false` on
one of them hides all of its children.

### `Repeater { model, delegate }`

| Prop | Type | Meaning |
|---|---|---|
| `model` | list, [C++ list model](#c-list-models), number, function or `bind` | Items. A number `n` means items `1..n`. |
| `delegate` | `function(item, index, ids) -> element` | Builds the element for one item. `ids` is the delegate instance's own [id scope](#element-ids). |
| `key` | `function(item, index) -> key` | *Optional.* Identifies an item: delegates are kept by key instead of per index (below). A list model's own keys are used without one. |

Delegate instances are **cached per index**. They are rebuilt only when the item at that index changes
(`~=` comparison), so per-delegate state survives frames. Extra cached entries are dropped when the model shrinks.
A delegate that returns `nil` shows nothing for its item (the item's entry in the child list is `false`).
Each instance's ids go to its own `ids` (a new table per instance), so every row can declare `id = "name"`. The
delegates' [`parent`](#selfparent) is the element the `Repeater` is in.

**Keys.** A `model` function that builds new item tables on every call (`map`, a query result) would rebuild every
delegate on every call, and the delegates would lose their state (focus, a text being edited, a scroll position). Give
the items a `key` (any value but `nil` or NaN; those fall back to the item itself, and items that are NaN themselves
share one stand-in key): a delegate is then kept while an
item with its key is in the model, also when it moves to another index (a sort, an insertion above it), and dropped
when its key goes away. Items with the same key get one delegate each, in order. A kept delegate **keeps the `item`
and `index` it was built with**: the delegate function isn't called again, so make the key change with whatever the
delegate shows from the item (`key = function(t) return t.id .. ":" .. t.title end`), or have it read changing data
through a binding (a `state{}` looked up by the key) instead of from the item table.

```lua
Repeater { model = people, delegate = function(person, i, ids)
    return Row {
        TextField { id = "name", text = person.name },
        Button { text = "Save", onClicked = function() person.name = ids.name.text end },
    }
end }
```

```lua
Row { spacing = 8,
    Repeater { model = { "lua", "cpp" }, delegate = function(tag, i)
        return Chip { text = tag, checked = bind(sel.tags, tag) }
    end },
}
```

### C++ list models

Rows that live in C++ come as a **list model** (`imlui::ListModel`, usually a `VectorModel<T>`: see
[List models](extending.md#list-models)): a property of a singleton (`backend.logs`), a method's result
(`backend:query("...")`, `view:rows()` on an element) or a singleton of its own (`logs`). Give it to a `Repeater` or `ListView` as its `model`,
directly or from a function:

```lua
ListView { height = 400, model = backend.logs,
    delegate = function(line) return ListItem { line.text, supportingText = line.level } end }
Text { text = function() return #backend.logs .. " lines" end }
```

In the editor, the delegate's `item` is `any` unless typed where it is written: `---@type Delegate<LogLine>` on the
line above `delegate = ...` makes `line` a `LogLine` (LuaLS doesn't infer it from the model; see
[Editor support](extending.md#editor-support-lua-definitions)).

Scripts read it like a list (`model[i] = v` raises an error; a model the C++ side made writable takes
`model:set(i, v)`):

| Lua | |
|---|---|
| `model[i]` | Row `i` (1-based) as its Lua value (a struct is a table), `nil` out of range. A row is converted when it is first read; an unchanged row is the same table again while something (a delegate) holds it, and a changed row a new one. |
| `#model`, `model:count()` | The number of rows, without converting any. |
| `ipairs(model)` | The rows in order. |
| `model:key(i)`, `model:hasKeys()` | Row `i`'s key (an integer or a string; `nil` without keys); whether the model has keys. |
| `model:revision()` | A number that changes with every change of the model (`-1` once the model was destroyed). |
| `model:set(i, v)` | Replaces row `i` (1-based, `1..#model`) with `v`, converted to the row type like a method argument (a struct from a table), when the C++ side allows writes (`VectorModel::setScriptWritable(true)`); else, for an index out of range, a value that doesn't convert or a destroyed model, it raises. It notifies like a C++ edit (`onRowsChanged`, the row's delegate rebuilt); a value equal to the row changes nothing. `model[i]` reads the converted row back, a new table. |

The same model is the same value every time (`rawequal(backend.logs, backend.logs)`). Changes come from C++, row by
row; each one renders a frame, so bindings that read the model (`#backend.logs`) follow. A model that was destroyed (its
element was collected, its owner deleted) reads as empty, and a model of a singleton is read fresh by the script that
a hot reload loads. What the containers do with one:

- Only the rows they show are converted: a `ListView` of a hundred thousand rows converts the dozen in view.
- While the model doesn't change, a frame reads none of its rows or keys.
- A delegate is rebuilt when its row changed, with or without keys.
- With **keys** (the model's, or a `key` function, which overrides them), delegates follow their rows: an insertion at
  the top builds one delegate, a removal, move or sort none. Without keys, delegates are kept **per index** as for a
  Lua list: an insertion at the top rebuilds the delegate of every index whose row moved (in a `ListView`, of the
  ones built).
- A `ListView`'s measured item heights follow their rows, keys or not: an insertion above the view leaves every row
  with its own height (see [ListView](elements.md#listview)).

**Signals.** Each change is a signal to [`connect`](#connecttarget-signal-fn---disconnect) to, called right after the
change with 1-based indices, like a singleton's listeners (no `self`):

| Signal | After |
|---|---|
| `onRowsInserted(first, count)` | rows `first .. first + count - 1` were inserted |
| `onRowsRemoved(first, count)` | the `count` rows from `first` were removed (`first` is where they were) |
| `onRowsChanged(first, count)` | rows `first .. first + count - 1` have new contents (a C++ edit or `model:set`) |
| `onRowsMoved(first, count, destination)` | the `count` rows at `first` moved, unchanged, to start at `destination` (an index after the move) |
| `onRowsRemapped()` | every row was replaced: a sort, a refreshed query (`replace`) |
| `onReset()` | anything changed |

```lua
connect(backend.logs, "onRowsInserted", function(first, count)
    s.unread = s.unread + count
end)
```

A listener may read and edit the model (an edit it makes is signalled in turn). The connections hold the model's Lua
value, so `connect(backend:query("..."), ...)` keeps a model that Lua owns alive until it is disconnected. For
"scroll to the end as rows come", a `ListView`'s `followBottom` needs no signal.

### `ListView`

A scrolling `Repeater`: a `ScrollView` whose children come from `model`/`delegate`. It isn't inline. Without
`delegate`, its array children are used. Its delegates get `ids` like a `Repeater`'s, and the `ListView` is
their `parent`. It takes all `ScrollView` props (`height`, `width`, `padding`, `color`,
`rounding`, `border`, `spacing`, `align`), `itemHeight` (dp) for items that are all the same height, and `cacheSize`
(256): how many built delegates it keeps, at most max(`cacheSize`, 4 x the items shown).

A `ListView` is **virtualized**: it builds and renders only the delegates of the items in view and a margin around
it, so a model of a hundred thousand items costs about what the dozen on screen do. A delegate is built when its
item first comes near the view (its ids, timers and other side effects start then), and kept while the cache holds
it; see [ListView](elements.md#listview) for the cache, the item heights and what navigation reaches.

```lua
ListView { height = 240, model = function() return todo.items end,
    delegate = function(item, index) return ListItem { text = item } end }
```

It takes `key` like a `Repeater`:

```lua
ListView { height = 240, model = function() return db.query("select id, title from notes") end,
    key = function(row) return row.id .. ":" .. row.title end,
    delegate = function(row) return ListItem { text = row.title } end }
```

Scrolling from a script (see [Scrolling a ListView from a script](elements.md#scrolling-a-listview-from-a-script)):

- `followBottom = true` keeps the view at the end while it is there, as items are appended (a Lua list or a C++
  model): a log or a chat. Scrolling up stops it, scrolling back to the end resumes it.
- `list:positionViewAtIndex(index, mode)` scrolls item `index` (1-based, clamped to the model) to `"beginning"`
  (default), `"center"`, `"end"` or `"contain"` (the least scroll that shows it), also an item whose delegate was
  never built: it scrolls by the known heights and corrects over the next frames as the items there are measured.
  `list:positionViewAtBeginning()`, `list:positionViewAtEnd()`. They work before the first render.
- `list.firstVisibleIndex` (read-only) is the first item in view, with `onFirstVisibleIndexChanged(self, index)`.
- Without `delegate`, `followBottom` and `positionViewAtIndex` work on its own children (the visible ones, an inline
  container's spliced in, counted from 1); `firstVisibleIndex` stays 0.

```lua
ListView { id = "chat", height = 400, followBottom = true, model = backend.messages,
    delegate = function(m) return ListItem { m.text, supportingText = m.author } end }
Button { "Oldest", onClicked = function() chat:positionViewAtBeginning() end }
Button { "Unread", onClicked = function() chat:positionViewAtIndex(s.firstUnread, "center") end }
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

Shows exactly one element: `item`, or the result of calling `item`. Useful for page switching. An `item` function
runs in the [id scope](#element-ids) the `Loader` was built in, so elements it builds lazily inside a component
register their ids in that instance's `ids`.

```lua
Loader { item = function() return pages[nav.page] end }
```

When the item changes, the old one is **dropped**: a `Timer` in it stops on the next frame unless something else
still references it (here `pages` does, so a timer on another page keeps ticking). A `Repeater` or `ListView`
drops the delegates it rebuilds or removes the same way. See the [`Timer` element](elements.md#timer).

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
`ScrollView` restores its offset when it's rendered in another place). A class may also be a function `fn(self)` returning the element,
called whenever the `Responsive` renders, in the [id scope](#element-ids) the `Responsive` was built in.

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

The former gallery's shell (`src/resources/main.lua` at the tag `full-gallery` of the
[imlui-example](getting-started.md#the-example-app-imlui-example) repository) shows the full pattern: a modal drawer opened from the top app bar on compact windows, a rail on medium
ones, the standard drawer from expanded up, and grids whose `columns` follow the width.

## Theme

See [Theming](theming.md) for the table fields and the color roles.

| Function | Description |
|---|---|
| `theme(t) -> scheme` | Applies theme table `t` **at the start of the next frame**. Returns the scheme `t` produces, as `{ role = "#RRGGBB" }`. Also stores `t` in `ui.__theme`. Fonts in `t.fonts` are loaded at once and errors reported (see [Fonts](theming.md#fonts)). |
| `scheme() -> scheme` | The scheme of the theme **currently in effect**. Right after `theme{}` it still returns the old scheme until the next frame. Cheap enough for a binding: the table is built once per theme and each call returns a fresh copy of it (changing your copy changes nothing else). |
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
| `color.mix(a, b, t)` | Linear sRGB interpolation from `a` to `b` (`t` in 0..1, clamped to it; NaN reads as 0). |
| `color.tone(c, t)` | Material tone `t` (0 = black, 100 = white) of `c`'s hue and chroma. |
| `color.alpha(c, a)` | `c` with alpha `a`. |
| `color.resolve(c)` | Resolves a spec against the current scheme, for example `color.resolve("primary")`. |

## Timers and frames

Frames are **rendered on demand** (see [Architecture](architecture.md#frame-loop)): after input, while an animation
or indeterminate indicator is moving, when a timer comes due, and when a script asks for one. When nothing changes,
the app sleeps. Timers run at the start of a frame, and the engine wakes up for the next one, so an interval keeps
its rate even with no input. An interval's periods start where the last one ended, not at the frame that ran it,
so it doesn't drift; a frame more than a period late runs it once and skips the missed ones. Timers due in the same
frame run in the order they came due (then the order they were set). Callbacks may add or clear timers. A callback
that raises an error is reported (with a traceback) in the error overlay; the other timers and callbacks of that
frame still run.

Timers and `onFrame` callbacks set up inside a component instance or delegate run in its id scope and belong to it,
like [watchers](#watchstate-key-fn---stop): once its elements are collected, they stop (an interval no longer fires,
a frame callback no longer runs). Set up at app level, they run until cleared or stopped.

| Function | Description |
|---|---|
| `setTimeout(fn, ms) -> id` | Calls `fn()` once after `ms` milliseconds (`nil` or `0`: on the next frame). `ms` must be a finite number `>= 0`: a negative, infinite or NaN delay raises an error (it would never fire). |
| `setInterval(fn, ms) -> id` | Calls `fn()` every `ms` milliseconds. `ms` must be a positive, finite number: `nil`, `0`, a negative or a non-finite interval raises an error (it would fire every frame and keep the app rendering). |
| `clearTimer(id)` | Cancels a timeout or interval. `nil` (a handle that was never set) is ignored. |
| `onFrame(fn) -> stop` | Calls `fn(time)` at the start of every **rendered** frame, after the timers. Call the returned `stop()` to remove it (also from inside `fn`). |
| `requestFrame(ms?)` | Asks for a frame now, or after `ms` milliseconds (a finite number; `math.huge` or NaN raise an error). Call it from `onFrame` (or a binding) while something you draw keeps changing on its own. |

Anything that changes because of a click, a key or a timer needs nothing extra. Only state that changes by
itself over time, such as a binding that reads `time()`, needs `requestFrame()`, a timer, or a `Canvas` with
`animated = true`.

```lua
local id = setInterval(function() s.seconds = s.seconds + 1 end, 1000)
setTimeout(function() clearTimer(id) end, 10000)
```

For a timer declared as an element, see the [`Timer` element](elements.md#timer).

## Async

A **future** is a value that arrives later: a C++ method's result ([`imlui::Future<T>`](extending.md#futures)),
`sleep(ms)`, an async function's result, `future()`. An **async function** runs in a coroutine and waits for futures
with `await` without blocking the UI: the coroutine yields, frames keep rendering, and it resumes on the main thread
when the future settles.

```lua
local s = state { status = "idle" }

local refresh = async(function()
    s.status = "loading"
    local ok, err = pcall(function()
        local report = await(backend:fetch("https://example.com/report")) -- a C++ Future<Report>
        s.title = report.title
        await(sleep(500))
    end)
    s.status = ok and "done" or ("failed: " .. tostring(err))
end)

Button { text = "Refresh", onClicked = refresh } -- the handler returns a future; nobody needs to keep it
Text { text = function() return s.status end }
```

| Function | Description |
|---|---|
| `async(fn) -> function` | Calling the result runs `fn(...)` in a new coroutine and returns a future of `fn`'s results (as soon as `fn` first awaits, or returns). An error `fn` raises fails the future. The coroutine runs in the [scope](#element-ids) the function was called in, like a timer. |
| `await(f) -> values...` | Only inside an async function (raises otherwise, also in a plain coroutine inside one). Waits until future `f` settles and returns its values, or raises its error: a Lua error, catchable with `pcall(await, f)`, `"cancelled"` for a cancelled future. A value that isn't a future is returned as it is. |
| `sleep(ms) -> future` | Done after `ms` milliseconds, a timer like `setTimeout` (same check on `ms`). Cancelling it clears the timer. |
| `all({f1, f2, ...}) -> future` | Done with the list of their values (the first value of each, in order, `n` = the count) once all are; failed or cancelled like the first input that fails or is cancelled. Values that aren't futures are taken as they are. Cancelling it cancels the inputs. |
| `timeout(f, ms) -> future` | Settles like future `f` (all its values, its error, its cancellation; its progress is forwarded), unless `f` is still pending after `ms` milliseconds (a timer like `sleep`'s, same check on `ms`): then it fails with `"timed out after <ms> ms"` and **releases** `f` (cancelled unless something else consumes it). Cancelling it clears the timer and releases `f`. |
| `race({f1, f2, ...}) -> future` | Settles like the first of them to settle, **whichever way** (done with all its values, failed or cancelled). An input settled already, or a value that isn't a future, wins at once (the first in order). The others are then released, as they are when the race is cancelled. An empty list gives a cancelled future. |
| `future() -> f` | A pending future the script settles: `f:resolve(values...)`, `f:reject(err)` (each returns whether it settled it; only futures made by `future()` take them); `f:report(p)` reports its progress. |

A future `f`:

| Field / method | Description |
|---|---|
| `f.state` | `"pending"`, `"done"`, `"failed"` or `"cancelled"`. Bindings read it like any field; settling a future requests a frame, so they update. |
| `f.value` | The first value, once done. |
| `f.error` | The error, once failed; `"cancelled"` once cancelled. |
| `f.progress` | The last progress reported while pending (a number, by convention 0..1), or `nil` before the first. Reporting requests a frame, so bindings update. A C++ job's reports are coalesced: at most one per loop iteration, the latest. |
| `f:next(onValue, onError) -> future` | When `f` is done, `onValue(values...)`; when it failed, `onError(err)`. The new future settles with what the handler returns (a future it returns is followed) or fails with what it raises. A missing handler passes the outcome on. When `f` is cancelled, the new future is cancelled (no handler runs). Handlers run in the scope `next` was called in. |
| `f:catch(onError) -> future` | `f:next(nil, onError)`. It doesn't see cancellation: that isn't an error. |
| `f:cancel() -> boolean` | Settles a pending `f` as cancelled (a future from C++: its C++ side first, so its C++ `onSettled` handlers have run and a `pending` property backed by it reads `false` when the Lua awaiters and handlers run), which cancels the futures `next`/`catch` made from it, and stops what it waits on: the C++ work behind it (its stop token), a `sleep`'s timer, an async function (it never resumes; its to-be-closed variables are closed). What it waits on as a consumer (`next`'s source or the future its handler returned, what an async function awaits, `all`'s inputs) is **released**: cancelled when nothing else consumes it. `false` when `f` already settled. |
| `f:onProgress(fn) -> f` | Calls `fn(progress)` at each progress reported from now until `f` settles (`f.progress` is the current one). Returns `f`, to chain: `backend:fetch(url):onProgress(show):next(use)`. `fn` runs in the scope `onProgress` was called in. Progress isn't forwarded through `next`. |
| `f:report(p) -> boolean` | Only for a future made by `future()`: reports progress `p` (a number). `false` once settled. |

```lua
backend:fetch(url)
    :next(function(report) return report.title end)
    :catch(function(err) return "unavailable: " .. tostring(err) end)
    :next(function(title) s.title = title end)

local report = timeout(backend:fetch(url), 5000) -- fails "timed out after 5000 ms"; the fetch is cancelled
local first = race({ backend:fetch(a), backend:fetch(b) }) -- the slower one is cancelled
local loading = all({ backend:fetch(a), backend:fetch(b) })
Text { text = function() return loading.state == "pending" and "Loading..." or "" end }
```

- **When code runs.** Handlers and resumed coroutines run synchronously when a future settles, on the main thread:
  a C++ future settles in a function posted to the main loop (outside a frame), `sleep` in its timer (at the start of
  a frame, in `ui.__tick`), a `future()` inside `resolve`/`reject`. `await` on a future that already settled returns
  at once, without yielding. Settling requests a frame, so what the resumed code changed is shown.
- **Unhandled rejections.** A future that fails while nothing takes its outcome (no `await`, `next`, `catch` or
  `all`, and no C++ `onSettled`) is reported in the error overlay at the start of the next frame, unless a handler was
  attached by then: `unhandled future rejection: <error>` (with the traceback of a Lua error). So an async function
  used as a handler (`onClicked = async(...)`) reports its errors like a plain handler. Cancelled futures are never
  reported.
- **Cancellation.** Cancelling flows down a chain and, where nothing else waits, up: a cancelled future cancels what
  `next`/`catch` made from it, makes `all` over it cancelled, and raises `"cancelled"` in an `await` on it (catchable
  with `pcall`). Cancelling a future that consumes others releases them: a source with one consumer (a `next`, an
  `await`, an `all`, a `timeout`, a `race`, a C++ method that took it) is cancelled with it; with two, once both let
  go. A `timeout` that fires and a `race` that is decided release their inputs the same way. A C++ method that took
  the future and cancels it (a C++ `withTimeout` or `race` does) cancels it outright. Cancelled futures are never
  reported.
- **Futures as C++ arguments.** A method declared with an `imlui::Future<T>` parameter takes any future (`lab:after(f)`);
  it settles from this one (its first value converted to `T`; one that doesn't convert fails the C++ side with
  the field path). Passing a future counts as handling its rejection. A `Future<void>` parameter takes every future
  and ignores its values. See [Extending](extending.md#lua-futures-as-arguments).
- **Futures from C++.** A C++ future's value is converted to Lua when it settles. A value whose conversion raises or
  throws (a custom `LuaConvert<T>::push`, out of memory) fails the Lua future with that error instead
  (`Future value: <what>` for a C++ exception), while the C++ side stays done.
- **Hot reload.** Futures and coroutines belong to the Lua state and die with it. A C++ future that settles after a
  reload settles only on the C++ side; a C++ future that was waiting on a Lua future is cancelled.
- `await` yields, so it works where a coroutine can yield: through `pcall`, `xpcall` and metamethods, not inside a
  C function such as a `table.sort` comparator (that raises `attempt to yield across a C-call boundary`). An async
  function's body must not call `coroutine.yield` itself (its future fails).

## Scaling and zoom

Sizes in scripts are in **dp**. One dp is `dpiScale()` pixels, where

`dpiScale() = contentScale() × zoom()`

- `contentScale()` is the monitor's content scale (1.5 at 150 % Windows scaling). It follows the window when it
  moves to a monitor with a different DPI, and the theme is re-applied at the new scale.
- `zoom()` is a user zoom factor (1 = 100 %), for example from a settings slider.

The root `ApplicationWindow`'s `width`/`height` are dp at 100 % zoom: they're multiplied by the content scale (not
by the zoom) when the native window size is set. A size that isn't a finite positive number is ignored, and one above
100000 dp is clamped to it, like `setWindowSize`'s limit.

| Function | Description |
|---|---|
| `zoom() -> number` | The user zoom, including a change requested this frame. |
| `setZoom(z)` | Sets the user zoom, clamped to 0.5..3.0. It takes effect at the start of the next frame. |
| `dpiScale() -> number` | The effective dp → pixel factor. |
| `contentScale() -> number` | The monitor content scale. |
| `setZoomShortcuts(on)` | Turns the built-in zoom shortcuts on or off (on by default, and again after a reload: a call at the top level of the script holds). |

**Built-in shortcuts:** Ctrl + `=` or `+` zooms in, Ctrl + `-` zooms out, Ctrl + `0` resets to 100 %, and
Ctrl + mouse wheel zooms in and out. The steps are 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250 and 300 %.

```lua
Slider { value = bind(zoom, setZoom), from = 0.5, to = 2, stepSize = 0.1 }
```

Applying the zoom on release instead avoids a jump: zooming moves the slider under the mouse while
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
| `log.trace/debug/info/warn/error(fmt, ...)` | `string.format(fmt, ...)` when arguments are given, otherwise `tostring(fmt)`. At a level that's off (the host's `log::setLevel`; debug and up by default) the message isn't built: no `string.format`, no `__tostring`. `print(...)` logs at info the same way. |
| `log.enabled(level) -> bool` | Whether `log[level]` logs (`"trace"`, `"debug"`, `"info"`, `"warn"`, `"error"`): check it before gathering an expensive message. |
| `print(...)` | In UI scripts, logs its tab-joined arguments at info level. |

```lua
log.info("loaded %d items", #items)
log.warn("slow frame")
```

Script errors are logged at error level automatically.

## Misc

| Function | Description |
|---|---|
| `time() -> seconds` | Seconds on the app clock (monotonic, counted from its first use; current between frames too). The timers, `Timer` and the snackbar run on it. |
| `deltaTime() -> seconds` | Duration of the last frame. |
| `fps() -> number` | Current frame rate (ImGui's moving average). |
| `isDark() -> bool` | Whether the current theme is dark. |
| `imguiVersion() -> string` | Dear ImGui version, for example `"1.92.9b"`. |
| `quit()` | Ends the frame loop and exits. |
| `reload()` | Reloads the script before the next frame (like saving a file). A reload that fails keeps the old UI and undoes what the failed script set while loading: the theme, the title, the zoom, the window size, the zoom shortcuts and culling stay as they were. |
| `setTitle(title)` | Sets the native window title, unless the `ApplicationWindow` has a `title` prop. See the note below. |
| `setCulling(on)` | Turns [culling](architecture.md#culling) on or off for every container (on by default, and again after a reload). Off, every child renders every frame, also those scrolled out of view: for debugging and comparison. A single container takes `cull = false`. |
| `culling() -> bool` | Whether culling is on. |
| `textSize(text, style?, font?) -> w, h` | Size of `text` (a string or number; anything else raises an error) in dp, for a [text style](theming.md#typography) (default `"bodyLarge"`; any other value raises an error naming the styles), in a [named font](theming.md#fonts) when `font` is given. Uses the fonts in effect: right after `theme{}` the old ones until the next frame. |

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
| `native.setCulling(on)` | Backs `setCulling()`. |
| `native.culling() -> bool` | Backs `culling()`. |
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
| `native.log(level, message)` | Backs `log.*`. `level` is `"trace"`, `"debug"`, `"info"`, `"warn"` or `"error"` (anything else logs at info). At a level that's off, `message` isn't converted (its `__tostring` doesn't run). |
| `native.logEnabled(level) -> bool` | Backs `log.enabled()`: whether `native.log(level, ...)` logs. |
| `native.injectMouse(x, y, down?, wheel?)` | **Native only.** Feeds ImGui a mouse event for scripted UI tests: position in window coordinates, an optional left-button state, and an optional vertical wheel delta. See [Development](development.md#scripted-ui-tests). |
| `native.injectKey(key, down, ctrl?, shift?, alt?)` | **Native only.** Feeds ImGui a key event for scripted UI tests, with the modifier states (sent first, so the key sees them). `key` is an ImGui key name, case-insensitive: `"Escape"`, `"Enter"`, `"Tab"`, `"Space"`, `"A"`..`"Z"`, `"0"`..`"9"`, `"F1"`..`"F12"`, `"Equal"`, `"Minus"`, `"LeftArrow"`, `"Keypad1"`, ..., or an alias (`"Esc"`, `"Return"`, `"Left"`/`"Right"`/`"Up"`/`"Down"`, `"Ctrl"`, `"Shift"`, `"Alt"`, `"="`, `"-"`). An unknown name raises an error. Starts the same settle period as real input. A press and a release sent together are seen in consecutive frames. |
| `native.setScopeHooks(scopes, callIn)` | **Internal.** Called once by the DSL: hands C++ the element scope table and the function that runs an element's function in its id scope, so C++ calls of handlers and bindings run in their instance's scope ([Architecture](architecture.md)). Not for scripts. |
| `native.reportError(message)` | **Native only.** Shows `message` in the error overlay (and logs it once). `ui.reportError(message)` wraps it. |
