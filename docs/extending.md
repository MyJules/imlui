# Registering C++ types

imlui UIs are written in Lua, but the interesting work of an app usually happens in C++. This page shows how to
expose C++ to Lua the way Qt exposes C++ to QML (`qmlRegisterType`, `Q_PROPERTY`, `Q_SIGNAL`, `Q_INVOKABLE`
and singletons):

- an **element type** is a C++ class that scripts instantiate like a built-in: `Gauge { value = ... }`
- a **singleton** is one C++ object that scripts see as a global: `backend.load`, `backend:tick()`

Both declare their properties, signals and methods once, in a static `declare()` function, and get conversion,
bindings, two-way write-back, signal dispatch and lifetime handling for free.

```cpp
import imlui;
import imlui.embedded.my_app; // main.lua and friends: imlui_add_resources(my_app resources/resources.lua NAME my_app)

class Gauge final : public imlui::Element {
public:
    static void declare(imlui::TypeBuilder<Gauge>& type) {
        type.property("value", &Gauge::value, &Gauge::setValue)
            .property("label", &Gauge::label, &Gauge::setLabel)
            .signal("onPeak")
            .method("reset", &Gauge::reset);
    }

    void render(imlui::Frame& frame) override; // draw with md:: widgets or frame.drawList()

    double value() const { return m_value; }
    void setValue(double v) {
        if (m_value < 0.9 && v >= 0.9) emit("onPeak", v);
        m_value = v;
    }
    const std::string& label() const { return m_label; }
    void setLabel(std::string l) { m_label = std::move(l); }
    void reset() {
        m_value = 0;
        notify("value"); // tell Lua: writes the value back, emits onValueChanged, requests a frame
    }

private:
    double m_value = 0;
    std::string m_label;
};

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, imlui::embedded::my_app::packages())};
    engine.registerType<Gauge>("Gauge");

    Backend backend;                              // derives imlui::ScriptObject, has a declare()
    engine.registerSingleton("backend", backend); // the Lua global `backend`
    return engine.run();
}
```

```lua
local s = state { cpu = 0.4 }

Gauge { id = "cpu", value = bind(s, "cpu"), label = "CPU",
        onPeak = function(self, v) snackbar(("%s peaked at %.0f%%"):format(self.label, v * 100)) end,
        onValueChanged = function(self, v) print("now", v) end },
Button { text = "Reset", onClicked = function() cpu:reset() end },
Text { text = function() return ("load %.0f%%"):format(backend.load * 100) end },

backend.onLoadChanged = function(v) print("load", v) end
```

A complete, runnable version is described in [A complete example](#a-complete-example).

## Contents

- [Element types](#element-types): `Element`, `render(Frame&)`, the lifecycle hooks
- [Declaring members](#declaring-members): `TypeBuilder<T>`: properties, signals, methods, `nonVisual`,
  `defaultProperty`, aliases, subclasses (`extends`)
- [Property sync](#property-sync): how Lua values reach the setters every frame
- [Telling Lua about changes](#telling-lua-about-changes): `notify` and `emit`
- [Singletons](#singletons)
- [Value conversion](#value-conversion): `LuaConvert<T>` and your own types
- [Lifetime](#lifetime) and [hot reload](#hot-reload)
- [Resources and the host config](#resources-and-the-host-config): mounts, the entry script
- [Testing elements](#testing-elements): the headless frame harness
- [Threading and frame requests](#threading-and-frame-requests), [ticking elements](#ticking-elements)
- [The built-ins as examples](#the-built-ins-as-examples)
- [The example app](#the-example-app)
- [Reference](#reference): modules and classes

## Element types

An element type derives from `imlui::Element` (module `imlui.types.element`), is default-constructible, and
registers with `Engine::registerType<T>(name)`, usually before `engine.run()`. A registration takes effect at
once, in a running Lua state too: elements created from then on get the type (registering a name again replaces
it, and elements that already have an instance keep it). The name must start with a capital letter, like every
element constructor.

```cpp
class Element : public ScriptObject {
public:
    virtual void render(Frame& frame) = 0; // every rendered frame the element is visible
    virtual void componentComplete();      // once: right after the first property sync (before the first render)
    virtual void destroyed();              // right before the instance is deleted (no Lua access any more)
    virtual void tick(double now);         // every frame while ticking, rendered or not (see Ticking elements)

protected:
    void setTicking(bool on);              // turns tick() on or off
};
```

Every built-in element is written this way, so the built-ins are the reference examples: see
[the built-ins as examples](#the-built-ins-as-examples).

`render()` draws the element at the ImGui cursor. Everything the built-in elements can do is available through the
`Frame` (module `imlui.types.frame`):

| `Frame` member | What |
|---|---|
| `renderChildren()` | Renders the children top to bottom. Inline containers (`Repeater`, `If`, ...) are spliced in. |
| `renderStack()` | Renders the children with the `spacing`/`align` props, like `Column`. |
| `renderChildren(node)`, `renderStack(node)` | The same for another element's children, e.g. the current tab's content: `frame.renderStack(child.node())`. |
| `forEachChild(f)` | Custom layouts: `f(Frame::Child& child)` is called for every **visible** child (inline containers spliced in). Place the cursor, then `child.render()`. See [Child handles](#child-handles). |
| `withElementProp(key, f)`, `renderElementProp(key)` | A prop whose value is an element (`menuBar = MenuBar { ... }`, `header = ...`): `f(Frame::Child&)` is called when it's set and visible; `renderElementProp` just renders it at the cursor. |
| `attachMenu(clicked)` | The `menu` prop (a `Menu` element) as a popup anchored to the last item: opened when `clicked`, rendered while open. Call it right after the widget that opens it. |
| `size(key)`, `size(key, ifAuto)` | A size prop in pixels, read like the built-ins read `width`/`height` (`fillWidth`, fill-minus values). The second form returns `ifAuto` for an unset (0) size: `size("width", -1)` fills by default. Pass it to the `md::` widgets, which resolve it. |
| `resolvedWidth(natural)`, `resolvedHeight(natural)` | `width` / `height` resolved to pixels against the available space with the one size rule every element follows (`resolveSize`, module `imlui.ui.item_size`): fixed, `-1` fill, `< -1` fill minus, `natural` when unset. Use it when the element sizes itself (`ImGui::Dummy`, a draw-list box). |
| `labelId(text, id)`, `alignFactor(align)` | `"text###id"` for labels whose text changes; `"start"`/`"center"`/`"end"` as 0/0.5/1. |
| `dp(v)` | dp to pixels at the current scale (monitor content scale times the user zoom). |
| `drawList()` | The current window's `ImDrawList*`. Use the [md shape helpers](development.md#adding-an-element) (`strokeArc`, `fillRoundRect`, `textTop`, ...) for Material-quality shapes. |
| `requestFrame()`, `requestFrameAfter(s)` | Frames are [rendered on demand](architecture.md#on-demand-rendering): anything that changes on its own asks for the next one. |
| `theme()`, `scheme()` | The current theme and Material color scheme. The md surface stack (`md::pushSurface`/`popSurface`/`surfaceColor`, `imlui.widgets`) works as in the widgets. |
| `images()` | The `ImageCache` (module `imlui.image.image_cache`) that `Image` draws from: `images()->get("app:/x.png")` gives a decoded image (`ready()`, `width()`, `texture()` for `drawList()->AddImage`), or its `error()`. Null when the host set none. |
| `setWindowTitle(title)` | The native window title (what `ApplicationWindow`'s `title` sets). |
| `node()` | The element table as a `luaui::Node`: an escape hatch to raw Lua (`node().pushRaw("__children")`, a painter callback, a table of rows). |
| `renderer()` | The `Renderer`, for the rare layout the members above don't cover (`Grid` walks every child, hidden ones too, with `renderer().forEachChild`). Prefer the members. |

### Child handles

`Frame::Child` is a lightweight handle, valid inside the callback that gives it:

| `Frame::Child` member | What |
|---|---|
| `render()` | Renders the child at the cursor through the renderer, with the same per-element wrapper as everywhere (see below). False if it's hidden or unknown. Afterwards a visual child is the last ImGui item (`ImGui::GetItemRectSize()`), and its size is stored for the next frame. |
| `isVisual()` | False for non-visual types (`Timer`, `Dialog`, ...). Layouts render those where they are and don't place, count or space them. |
| `node()`, `type()` | The child's element table and its type name (`"Spacer"`). |
| `id()` | A stable ImGui ID for the child in the parent's ID scope, for your own per-child state in `ImGui::GetStateStorage()`. |
| `lastWidth(fallback)`, `lastHeight(fallback)` | The child's size when it was last rendered through `render()`, or `fallback` on its first frame. Immediate-mode layouts (centering, right alignment, rows) measure on the previous frame and settle within one frame. |
| `as<T>()` | The C++ instance of a typed child as a `T*`, with its declared properties **synced first**, so the parent reads this frame's values (`child.as<Tab>()->title()`). `nullptr` when the child isn't a typed element or not a `T`. `syncProperties()` does the sync and returns the `ScriptObject*`. |

```cpp
void Row::render(imlui::Frame& frame) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    float x = start.x;
    frame.forEachChild([&](imlui::Frame::Child& child) {
        if (!child.isVisual()) { child.render(); return; }   // a Timer, a Dialog, ...
        const float h = child.lastHeight();                  // measured last frame
        ImGui::SetCursorScreenPos({x, start.y + std::max(0.0f, (m_height - h) * 0.5f)});
        child.render();
        x += ImGui::GetItemRectSize().x + frame.dp(8);
    });
}
```

For a child that is still a handler-function element, read its props through `child.node()`
(`child.node().string("title")`).

Each visual element is wrapped in an ImGui group and an ImGui ID scope (the element table's address), so plain
ImGui IDs such as `"##gauge"` are unique per element, and the element counts as one item in a `Row`.

The common props keep working on typed elements without being declared, exactly as on handler-function elements:
the renderer skips the element when `visible` is false, wraps it in `BeginDisabled` when `enabled` is false, in an
ImGui group when it's visual, and in `PushID`, and shows `tooltip` after it. `id` registers the element for lookup
by name, and sizes are read with `frame.size("width")` / `frame.size("height")` (or resolved to pixels with
`frame.resolvedWidth(natural)` / `frame.resolvedHeight(natural)`).

## Declaring members

`static void declare(imlui::TypeBuilder<T>& type)` (module `imlui.types.type_builder`) lists what Lua sees. The
calls chain. A name can be used only once across properties, methods and signals (a clash throws at registration).

### Properties

```cpp
type.property("value", &Gauge::value, &Gauge::setValue)       // read/write
    .property("max", &Gauge::max, &Gauge::setMax, 1.0)        // read/write, nil resets it to 1.0
    .property("peak", &Gauge::peak);                          // read-only
```

- The **getter** is anything `std::invoke` can call with a `const T&`, usually `V (T::*)() const`. Its return type
  (without `const`/`&`) is the property type `V`, which needs a [`LuaConvert<V>`](#value-conversion).
- The **setter** is anything `std::invoke` can call with `(T&, V)`, usually `void (T::*)(V)` or
  `void (T::*)(const V&)`.
- Read-only properties are readable from Lua on singletons and through `notify` write-back, but never synced from
  the element table.
- **Defaults.** When a property's Lua value is `nil` (never set, set back to `nil`, or a binding that returns
  `nil`), the property is reset to its default. The default is the getter's value **right after construction**
  (so C++ member initializers are the defaults), or the value given as the fourth argument,
  `property(name, getter, setter, defaultValue)`. An `std::optional<T>` property takes no default: `nil` is
  `std::nullopt`. A value type that can't be copied keeps its current value on `nil`.

### Signals

```cpp
type.signal("onPeak");
```

Declares a signal name for documentation and validation (it reserves the name). Emit it from C++ with
`emit("onPeak", args...)`. Handlers are ordinary `on*` fields of the element table. Every property also has a
change signal `on<Name>Changed`, emitted by `notify(name)`, without declaring it.

### Methods

```cpp
type.method("reset", &Gauge::reset)
    .method("setRange", &Gauge::setRange); // void setRange(double min, double max)
```

Methods are member function pointers, `const` and `noexcept` ones included. Lua calls them with `:` on the element
(through its id or `self`) or on the singleton:

```lua
cpu:reset()
cpu:setRange(0, 200)
local text = backend:describe("cpu")   -- results are converted back
```

Arguments are checked with `LuaConvert<A>::check`. A wrong argument raises a normal Lua error,
`bad argument #1 to 'Gauge:setRange' (number expected, got string)`. A C++ exception thrown by the method becomes
a Lua error too. One thrown by a property getter or setter is reported as a script error (`Gauge.value: ...`)
instead: a setter that threw leaves the property unchanged, a getter that threw reads as `nil`. Before a method runs on an element, the element's properties are [synced](#property-sync), so it
sees the current Lua values even if the element never rendered.

**Overloads.** `&T::fn` of an overloaded member doesn't name one function. Pick one by its parameter types with
`imlui::overload<Args...>` (non-`const` members) or `imlui::constOverload<Args...>` (`const` members), from module
`imlui.types.overload`. The class and the return type are deduced; no `static_cast` to the full member pointer
type is needed. They work for getters and setters too:

```cpp
type.method("grow", imlui::overload<double>(&Shape::grow))              // double grow(double)
    .method("growBoth", imlui::overload<double, double>(&Shape::grow))  // double grow(double, double)
    .method("area", imlui::constOverload<>(&Shape::area))               // double area() const
    .property("size", imlui::constOverload<>(&Shape::size), imlui::overload<double>(&Shape::setSize));
```

### `nonVisual()` and `defaultProperty(name)`

- `type.nonVisual()` makes the element take no layout space or spacing, like `Timer` or `Dialog`.
- `type.defaultProperty("value")` lets a positional first value set the property, `Gauge { 0.5 }`, like `text`
  for `Button { "OK" }`. A table in that position is a child, not a value.

### Aliases

One class can be registered under several names. Each name is its own type to Lua (`ui.types()` lists both); an
instance belongs to the name it was created for.

```cpp
types.registerType<Column>("Column");
types.registerType<Column>("Item");
```

### Subclasses: `extends<Base>()`

A subclass declares its own members and pulls in its base's with `extends<Base>()`, which runs
`Base::declare` on the subclass's type:

```cpp
class TextField : public imlui::Element {
public:
    static void declare(imlui::TypeBuilder<TextField>& type); // text, label, ... onTextChanged
    void render(imlui::Frame& frame) override;                // reads m_multiline
protected:
    bool m_multiline = false;
};

class TextArea final : public TextField {
public:
    static void declare(imlui::TypeBuilder<TextArea>& type) {
        type.extends<TextField>();     // every TextField property, signal and method
        // .property("rows", ...)      // plus TextArea's own
    }
    TextArea() { m_multiline = true; }
};
```

A subclass must have its own `declare(TypeBuilder<Sub>&)`: one inherited unchanged from the base takes
`TypeBuilder<Base>&`, and `registerType<Sub>` rejects it at compile time instead of silently registering a type
without members.

## Property sync

Before every `render()`, the renderer **syncs** the declared writable properties from the element table into the
instance:

1. Each property is read through `luaui::Node`, so all three [property forms](lua-api.md#property-values) work:
   a plain value, a function binding (re-evaluated every frame), or a two-way `bind()`.
2. A `nil` value resets the property to its [default](#properties) (the getter's value after construction, or the
   declared `defaultValue`). An `std::optional<T>` property receives `std::nullopt`.
3. The value is converted with `LuaConvert<V>`. If the conversion isn't possible, the error goes to the error
   overlay (`Gauge.value: number expected, got string`) and the setter isn't called.
4. The **setter runs only if the value differs** from what the getter returns (`operator==`; types without one are
   always set). Setters can therefore do real work or emit signals without running every frame.

Bindings that read singletons (`function() return backend.load end`) are re-evaluated on every rendered frame,
like any binding.

**Lazy sync.** An element that isn't rendered (on a hidden page, `visible = false`, a model item that isn't shown)
is synced when something needs it:

- before a **method called from Lua** runs on it (`cpu:reset()`), so an element that never rendered still sees
  its Lua values on its first method call;
- when a **parent or another element** asks for it: `Frame::Child::as<T>()` and `TypeRegistry::sync(node)`.

`componentComplete()` runs once, right after the first sync, wherever that happens (first render, first method
call, or a parent's `as<T>()`). An exception thrown by `componentComplete()`, `tick()` or `destroyed()` is
reported as a script error (`Gauge.componentComplete(): ...`) and the element carries on (or is deleted anyway,
for `destroyed()`). A Lua error during the sync itself is reported too, and the next sync runs normally. A binding
that calls a method of its own element
(`text = function(self) return self:describe() end`) doesn't sync it again from inside its sync. Lua never reads an
element's property through the instance (it reads the element table), so that needs no sync.

Because the sync compares with the getter, the element table is the source of truth: a C++ change that isn't
`notify`'d is overwritten by the Lua value at the next sync. Always `notify` after changing a property in C++.

## Telling Lua about changes

Both functions live on `imlui::ScriptObject` (module `imlui.types.script_object`), the base of elements and
singletons: anything a Lua script can see, with properties, methods and signals.

### `notify(name)`

Call it after the C++ side changed property `name`. It

1. reads the value through the getter,
2. on an element, **writes it back** to the element table with `Node::set` semantics: a `bind(t, k)` writes
   `t[k]`, a `bind(get, set)` calls `set`, a function binding is left alone (it owns the value and wins at the
   next sync, like a widget edit on a bound prop), a plain value is replaced,
3. emits `on<Name>Changed` with the new value (`onValueChanged(self, v)` on elements, `onValueChanged(v)` on
   singletons),
4. requests a frame (it works outside a frame too).

```cpp
void Gauge::reset() {
    setValue(m_min);
    notify("value"); // a gauge with value = bind(s, "cpu") now sets s.cpu
}
```

### `emit(signal, args...)`

Calls the `signal` handler with the arguments converted by `LuaConvert`. Element handlers are called as
`handler(self, args...)`, singleton handlers as `handler(args...)`. A string literal argument is fine. A
handler error goes to the error overlay and doesn't propagate. `emit` returns `false` when there's no handler,
and requests a frame, since handlers usually change state.

```cpp
emit("onPeak", m_value);
emit("onRowMoved", from, to, std::string{"drag"});
```

`emit` is a variadic template, so it lives in the module interface.

## Singletons

A singleton is any object deriving `imlui::ScriptObject` with a `declare()`. Register it by `std::shared_ptr`,
and the engine keeps it alive as long as the engine exists, so it outlives `run()` and every Lua state:

```cpp
auto backend = std::make_shared<SystemInfo>();
engine.registerSingleton("backend", backend); // owned (shared) by the engine from now on
```

or by reference, when something else owns it. Then the object **must outlive `engine.run()`**, because every Lua
state the engine creates points at it until it closes:

```cpp
SystemInfo backend;                           // declared before the Engine, so it dies after it
engine.registerSingleton("backend", backend);
```

Scripts see a global userdata:

| Lua | C++ |
|---|---|
| `backend.load` | the `load` getter |
| `backend.paused = true` | the `paused` setter (when the value changed). A read-only property raises `backend.time is read-only`, an unknown name `... has no property 'x'`. `backend.paused = nil` resets the property to its default (its value at registration, or the declared `defaultValue`). |
| `backend:tick()` | the `tick` method |
| `backend.onTick = function(n) ... end` | stores a handler for `emit("onTick", n)`. Any `on*` name accepts a function or `nil`. |
| `backend.onLoadChanged = function(v) ... end` | called by `notify("load")` |

Singleton handlers get no `self` (the singleton is a global anyway). `notify` on a singleton doesn't write
anything back (Lua always reads it through the getter), but it does call the `on<Name>Changed` handler and
request a frame, so bindings that read it show the new value.

To bind a widget two-way to a singleton property, use `bind(getter, setter)`:

```lua
Switch { text = "Paused", checked = bind(function() return backend.paused end,
                                         function(v) backend.paused = v end) }
```

## Value conversion

`imlui::LuaConvert<T>` (module `imlui.types.lua_convert`) converts property values, method arguments and
results, and signal arguments. Built in:

| C++ | Lua | Notes |
|---|---|---|
| `bool` | any | Lua truthiness: only `nil` and `false` are false. |
| integral types | number | Numeric strings convert. Floats truncate toward zero; out-of-range values clamp. |
| `float`, `double` | number | Numeric strings convert. |
| `std::string` | string | Numbers convert (`7` reads as `"7"`). |
| `std::string_view`, `const char*` | string | Push only (results, signal arguments, read-only properties). |
| `imlui::Color` | color spec | Reads a role name, `"role/alpha"`, hex or `{r, g, b, a}`, like every color prop. Pushes `"#RRGGBB[AA]"`. |
| `std::optional<T>` | `nil` or `T` | |
| `std::vector<T>` | sequence | Every element must convert. |

Add your own by specializing it (in your module, in namespace `imlui`):

```cpp
struct Vec2 { double x = 0, y = 0; bool operator==(const Vec2&) const = default; };

template <>
struct imlui::LuaConvert<Vec2> {
    static constexpr std::string_view name = "{x, y}";            // used in error messages
    static void push(lua::State* s, const Vec2& v) {
        lua::createtable(s, 0, 2);
        lua::pushnumber(s, v.x);
        lua::setfield(s, -2, "x");
        lua::pushnumber(s, v.y);
        lua::setfield(s, -2, "y");
    }
    static bool check(lua::State* s, int idx) { return lua::istable(s, idx); }
    static Vec2 get(lua::State* s, int idx) {
        Vec2 v;
        lua::getfield(s, idx, "x");
        v.x = lua::tonumber(s, -1);
        lua::getfield(s, idx < 0 ? idx - 1 : idx, "y");
        v.y = lua::tonumber(s, -1);
        lua::pop(s, 2);
        return v;
    }
};
```

`push` pushes exactly one value. `check` must not raise errors or leave anything on the stack, and `get` is only
called after `check` returned true. Leave out `check`/`get` for a push-only type. Give the type an `operator==`
so the sync can skip unchanged values.

## Lifetime

**Element instances** belong to Lua:

- The DSL constructor (`Gauge { ... }`) creates the C++ instance **immediately**, through the default
  constructor, and stores it in the element table as `__native`, a full userdata with a `__gc` metamethod. The
  element table gets a metatable whose `__index` is the type's methods table, which is how `cpu:reset()` works.
  Element tables otherwise stay what they always were: plain tables with `__type` and array children.
- Nothing else holds the instance: its link back to the table is weak. When Lua collects the element table, the
  userdata goes with it, `destroyed()` runs and the instance is deleted. Elements that the UI keeps (the tree
  returned by the script, ids, captured locals) live as long as the Lua state.
- Properties are synced at the first render, the first method call or a parent's `as<T>()`, whichever comes
  first, not in the constructor, so the constructor sees no Lua values. Defaults for `nil` are captured right
  after the constructor. `componentComplete()` runs once after the first sync.
- A table built without the constructor (`{ __type = "Gauge" }`) gets its instance at its first render.

**Singletons** registered by `std::shared_ptr` are shared with the registry, which releases them when the engine
is destroyed (after its last Lua state closed). Singletons registered by reference belong to you: the registry
keeps a non-owning pointer, and every Lua state the engine creates gets a global userdata pointing at the object.
That object must outlive `engine.run()`. Declaring it before the `Engine` in `main` is enough.

Don't keep `Element*` pointers to instances in other C++ objects: Lua decides when they die. Keep shared state
in a singleton (or a model the singleton owns) and let elements read it through bindings. A worker thread that
must report back to an element holds its `handle()` instead (see
[background work owned by an element](#background-work-owned-by-an-element)).

## Testing elements

The tests build a headless ImGui frame harness, `imlui::test::HeadlessFrame` (test-only module
`imlui.test.headless_frame` in `tests/support/headless_frame.cppm`). It owns an ImGui context without a window, backend
or GPU, a `Renderer` and a `TypeRegistry`, and a Lua state set up like the engine's (the real `imlui.native`, the
painter library, `src/resources/imlui.lua`). Register types, run Lua in the UI script environment, render frames and
look at the instances:

```cpp
#include <gtest/gtest.h>
import imlui.test.headless_frame;

TEST(GaugeTest, RendersAndSyncs) {
    imlui::test::HeadlessFrame h{{.builtins = true}};   // also Column, Dialog, ... like the engine
    h.registerType<Gauge>("Gauge");
    ASSERT_EQ(h.run(R"(
        s = state { v = 0.25 }
        root = Column { Gauge { id = "gauge", value = bind(s, "v") } })"), "");  // "" = no Lua error
    h.renderFrames("root", 2);                             // NewFrame .. Render, with ui.__tick
    EXPECT_DOUBLE_EQ(h.instance<Gauge>("gauge")->value(), 0.25);
    ASSERT_EQ(h.run("ui.native.injectKey('Escape', true)"), ""); // seen by the next frame
}
```

`render()`, `componentComplete()` and children rendering through `Frame` run exactly as in the app. The harness
loads the Roboto text fonts and the icon font, so the Material widgets measure as in the app. `h.click(root, x, y)`
injects a left click (move, press, release, a frame each). Errors that scripts report land in `luaui::errors()`.
`tests/support/` also has two small test elements to register under any name: `test::LastItemProbe` (renders its
children and remembers the last one's rect, to know where to click) and `test::RenderCounter`. See
`tests/element_test.cpp`, `tests/frame_test.cpp` and the `tests/elements_*_test.cpp` files, and
[Development](development.md#testing) for more. The harness has its own `Resources` (`imlui:/` and
`imlui:/fonts/`); mount `app:/` with `h.resources().mount(...)` before the first `run()` to test scripts that
`require` components or call `ui.resource` (see `tests/resources_test.cpp`).

## Hot reload

On reload the engine creates a new Lua state and calls `TypeRegistry::install(state)` before the DSL loads. The
types, methods tables and singleton globals are set up again, so registered types keep working after every
reload. When the old state closes, every element instance in it is destroyed (`destroyed()` first), and the
singletons are re-linked to the new state. Singleton handlers (`backend.onTick = ...`) belong to a state, so the
reloaded script sets them again. If the new script fails to load, the engine keeps the old state and points
the singletons back at it.

## Resources and the host config

The engine loads every file through `EngineConfig::resources` (see [Architecture](architecture.md#resources)).
`defaultConfig(argc, argv, packages)` takes the host's embedded files as `ResourcePackage`s
(`imlui::embedded::<name>::packages()`, generated by `imlui_add_resources` from a resource manifest, one per group)
and stacks, from the bottom: the embedded
files (the library's `imlui:/` and `imlui:/fonts/`, then the host's packages), every `*.pak` next to the executable,
the override folders next to it named after a prefix (`app/` at `app:/`, `imlui/` at `imlui:/`), and in dev builds
each package's source folder (hot reload; `IMLUI_DEV_PATHS`; dev builds don't embed by default,
`IMLUI_EMBED_RESOURCES`, so there the folders are the only copy). The entry is `app:/main.lua`; a script argument
replaces everything at `app:/` with its own folder. The host names no file paths. A host can mount more, before
`run()`, on the config or through `Engine::resources()`, and change `entry`. A later mount overrides files of an
earlier one at the same names:

```cpp
imlui::EngineConfig config = imlui::defaultConfig(argc, argv, imlui::embedded::my_app::packages());
config.resources.mount("app:/", std::make_shared<imlui::DirectoryMount>("overrides")); // loose files win
config.resources.mount("plugin:/", std::make_shared<imlui::DirectoryMount>("plugins/charts"));
imlui::Engine engine{std::move(config)};
```

To carry files inside the executable, list them in a resource manifest (a Lua file, see
[Architecture](architecture.md#resource-manifests)) and embed them with `imlui_add_resources`
(`cmake/ImluiResources.cmake`). Each call generates a module `imlui.embedded.<name>` whose `packages()` (one per
group of the manifest) go to `defaultConfig` (with each group's dev source folder on top) and whose
`mount(Resources&)` mounts the embedded files alone:

```lua
-- plugins/charts/resources.lua
return { prefix = "plugin", mount = "charts", include = { "**/*.lua", "data/*.json" } }
```

```cmake
imlui_add_resources(my_host plugins/charts/resources.lua) # module imlui.embedded.charts, after the folder
```

```cpp
import imlui.embedded.charts;
// plugin:/charts/...: embedded; a plugin/ folder next to the executable and, in dev builds, plugins/charts override it
imlui::EngineConfig config =
    imlui::defaultConfig(argc, argv, {imlui::embedded::my_app::packages(), imlui::embedded::charts::packages()});
```

Scripts then use `require` and `ui.resource.read("plugin:/data.json")`. A new kind of file source implements
`imlui::Mount` (`read`, `exists`, `list`, `modified`, `origin`). Its `read` returns an `imlui::ResourceData`: the bytes
plus an optional shared owner. The bytes must stay valid while that handle (or a copy) and the mount live: the font
atlas points into them without copying and keeps the handle as long as it does. Memory the mount owns for its whole
lifetime needs no owner (`ResourceData{bytes}`); bytes that can be replaced (a changed file) go with a
`shared_ptr` to their buffer (`ResourceData{bytes, buffer}`), so they're freed once nothing holds them.

## Threading and frame requests

Lua, the element tree, `notify` and `emit` are **main-thread only**. To hand results from another thread to the
UI, post a function to the main thread with `imlui::post` (module `imlui.ui.main_thread`, also in `import imlui`):

```cpp
imlui::post([&backend, result] { backend.setResult(result); }); // thread-safe; wakes the loop
```

`engine.post(fn)` is the same call for code that has the `Engine`. The function runs at the next loop iteration,
outside any frame, in the order things were posted. It may call `notify` or `emit`, which request a frame. The
loop is woken through `glfwPostEmptyEvent`, so this works while the app is idle and sleeping. A posted function
that throws is reported like a script error (the error overlay and the log, `posted function: <what>`), and the
functions after it still run.

Functions posted before `run()` wait and run at its first loop iteration. Functions still queued when `run()`
returns are dropped (destroyed on the main thread without running).

This is fine for objects the host owns and keeps alive (the `Engine`, a singleton held by a `shared_ptr`). It is
**not** enough for an element: Lua owns it and deletes it when it collects the element table, when a page is
rebuilt, and when a hot reload closes the Lua state, possibly while a worker is still busy. A worker that posts
`[this] { notify("value"); }` would then touch a deleted object.

### Background work owned by an element

Take a **handle** on the main thread and give the worker only the handle and copies of its inputs:

```cpp
class Fetch final : public imlui::Element {
public:
    static void declare(imlui::TypeBuilder<Fetch>& type) {
        type.property("url", &Fetch::url, &Fetch::setUrl).property("text", &Fetch::text, &Fetch::setText).nonVisual();
    }
    void render(imlui::Frame&) override {}

    void setUrl(std::string url) {
        if (url == m_url) return;
        m_url = std::move(url);
        // The worker gets copies and the handle. `this` is captured only to be used inside handle.post().
        std::thread{[handle = handle(), this, url = m_url] {
            std::string body = download(url, [&] { return !handle.alive(); }); // slow; polls for cancellation
            handle.post([this, url, body = std::move(body)] { // main thread, and only while `this` lives
                if (url != m_url) return; // a newer request replaced this one
                m_text = body;
                notify("text");
            });
        }}.detach();
    }
    // url(), text(), setText() ...

private:
    std::string m_url, m_text;
};
```

`ScriptObject::handle()` (main thread) returns a `ScriptObject::Handle`, a weak, copyable, thread-safe token:

| `Handle` member | What |
|---|---|
| `alive()` | True until the object is destroyed. Thread-safe; a worker polls it as its "cancelled" flag. |
| `post(fn)` | Queues `fn` like `imlui::post`. On the main thread, `fn` runs **only if the object is still alive** (otherwise it is destroyed without running), so it may use the object's `this`. Returns false, dropping `fn`, when the object is already gone. |

Elements and singletons both have it (it lives on `ScriptObject`). `ScriptObject`'s destructor clears the token
on the main thread, where posted functions run too, so the object can't die between the check and `fn`. Every way
an element dies goes through that destructor: garbage collection, a rebuilt page, and a hot reload or shutdown
closing the Lua state (a reload closes the old state before the loop runs posted functions again).

Rules for the worker thread:

- **Don't join slow work in the destructor** (or in `destroyed()`): that freezes the UI, a reload or the app's exit
  until the work finishes. Detach the thread (or let a shared object own it) and let it notice the cancellation:
  through `handle.alive()`, which turns false once `ScriptObject`'s destructor ran (after your own destructor), or
  a flag of your own that your destructor sets (a `std::stop_source`, or an `atomic<bool>` shared through a
  `shared_ptr`). If you do join (`std::jthread`), stop the worker first with your own flag: `alive()` is still
  true while your destructor runs.
- The worker never touches the element or its members itself, only the copies it was given. Everything that reads
  or writes the element goes into `handle.post`.
- A detached worker may outlive the element, the `Engine`, even `main()`'s return: what it uses must stay alive
  through its own captures, and the work should be short or cancellable.

Frames are rendered on demand. `notify` and `emit` request one, and so do input and animations. An element whose
look changes over time by itself (a clock, an indeterminate animation, a timer) must ask for the next frame from
`render()`: `frame.requestFrame()` for "as soon as possible", or `frame.requestFrameAfter(seconds)` for the next
due time. The built-in `Timer` does the latter.

## Ticking elements

`render()` only runs while the element is rendered. An element that must act on its own, also when its page isn't
shown (a timer, a poller), turns on **ticking** with `setTicking(true)` (protected; typically in the constructor or
in a setter). While it's on, `tick(now)` runs at the start of every frame, before the tree renders, whether the
element is rendered or not, with its properties synced first. `now` is the ImGui time in seconds. Frames are
rendered on demand, so ask for the frame you need with `requestFrameAfter(seconds)` (module
`imlui.ui.frame_request`). `setTicking(false)` stops it, and so does the destructor.

```cpp
class Poller final : public imlui::Element {
public:
    static void declare(imlui::TypeBuilder<Poller>& type) {
        type.property("interval", &Poller::interval, &Poller::setInterval).signal("onPoll").nonVisual();
    }
    Poller() { setTicking(true); }
    void render(imlui::Frame&) override {}
    void tick(double now) override {
        if (now - m_last >= m_interval) {
            m_last = now;
            emit("onPoll");
        }
        imlui::requestFrameAfter(m_interval - (now - m_last));
    }
    // interval() / setInterval() ...

private:
    double m_interval = 1.0;
    double m_last = 0.0;
};
```

An element ticks until ticking is turned off or the element is destroyed, that is until Lua collects its element
table. Ticking follows the table's lifetime, not whether it is in the rendered tree: an element on a hidden page, in
an unselected `Loader` page kept in a table, or held only by a variable keeps ticking. When an inline container
**drops** children (a `Repeater`/`ListView` delegate rebuilt or removed because its model changed, a `Loader` or
`Responsive` item replaced by another), the next frame's `ui.__tick` runs a full garbage collection before ticking
(only while ticking elements exist), so a dropped element that nothing else references is destroyed and stops right
away. An element dropped some other way (a table you edit yourself) stops at the next garbage collection. A dropped
element that still has an `id` stays referenced by `ui.ids` and keeps ticking. Give the element a `running`
property (like the built-in `Timer`) when scripts need to stop it. `timer.cppm` is the complete example.

## The built-ins as examples

Every built-in element (`Button`, `Column`, `TabBar`, ...) is an `Element` class in its own module in
`src/imlui/ui/elements/`, registered under its Lua name(s) by `registerBuiltinTypes()` (`builtin.cppm`). The class
doc comment lists its props with defaults, the *raw* props it reads from the element table, its signals and its
two-way props. Good places to start:

| Module | Shows |
|---|---|
| `button.cppm` | A plain control: declared props, a default property (`Button { "OK" }`), a signal, raw `width`/`height` through `frame.size()`, the `menu` popup (`frame.attachMenu`). |
| `checkbox.cppm` | A two-way prop: after a user change, the new value, `notify("checked")` and `emit("onToggled", ...)`. |
| `row.cppm` | A custom layout with `Frame::Child` handles: last-frame sizes, fill spacers, `justify`, non-visual children. |
| `tab_bar.cppm` | Typed children (`child.as<Tab>()`), rendering only one child's content (`frame.renderStack(child.node())`). |
| `text_area.cppm` | A subclass: `TextArea` extends `TextField` with `type.extends<TextField>()` and one constructor line. |
| `navigation_rail.cppm` | An element-valued prop (`withElementProp("header")`) and a scrolling child region. |
| `modal_navigation_drawer.cppm` | A non-visual overlay, and a two-way prop that also works under a function binding. |
| `timer.cppm` | A [ticking element](#ticking-elements). |

Registering a type under a built-in's name replaces the built-in: the last registration of a name wins.

## A complete example

The [imlui-example](getting-started.md#the-example-app-imlui-example) repository is a minimal app without C++ types; its tag
`full-gallery` keeps the earlier gallery, which registers two. Its C++ is in `src/`, and its *C++ types* page
(`src/resources/pages/cpp_types.lua`) uses both types:

```sh
git checkout full-gallery && git submodule update                 # in imlui-example
cmake --preset clang-debug && cmake --build build/clang-debug
build/clang-debug/bin/imlui_gallery                               # the last page: C++ types
```

- `src/main.cpp` is an import-only translation unit (per CODE_STYLE). It calls
  `defaultConfig(argc, argv, imlui::embedded::gallery::packages())` (no paths, the entry script is
  `app:/main.lua`), registers `Gauge` and the `backend` singleton, and starts the singleton's worker thread.
- `src/gauge.cppm` (module `gallery.gauge`): **`Gauge`**, a Material arc gauge drawn with `md::strokeArc`,
  `md::textTop` and `md::animate`. Properties `value`, `min`, `max`, `label`, `size`, signal `onPeak` (fired by the
  setter when the value rises past `0.9 * max`), method `reset()`, default property `value`. Clicking a gauge
  advances it by a tenth of the range and calls `notify("value")`: the first gauge is bound with
  `bind(s, "level")`, so the click writes `s.level`, and the slider and the other two gauges follow.
- `src/system_info.cppm` (module `gallery.system_info`): **`SystemInfo`**, the singleton `backend`. Read-only
  `time`, `frame` and `load`, read/write `paused`, signal `onTick`, method `tick()`. A worker thread posts
  `tick()` to the main thread twice a second with `Engine::post`. The gallery pauses it while another page is
  shown, so idle pages render no frames.
- `CMakeLists.txt` adds imlui (`add_subdirectory(vendor/imlui)`) and `src/`. `src/CMakeLists.txt` lists both module
  files in the executable's own `CXX_MODULES` file set, links `imlui::imlui`, applies
  `imlui_set_warnings`/`imlui_set_hardening`, and embeds `src/resources/` (listed in `src/resources/resources.lua`)
  with `imlui_add_resources(imlui_gallery resources/resources.lua NAME gallery)`. In dev builds the package carries
  the `src/resources/` folder's path and `defaultConfig` mounts it over the embedded copy, so the scripts
  hot-reload.

An app of your own follows the same pattern; [Getting started](getting-started.md#using-imlui-in-your-app) has the
skeleton.

## Reference

| Module | Contents |
|---|---|
| `imlui.types.lua_convert` | `LuaConvert<T>` and its built-in specializations; the `LuaPushable` / `LuaReadable` concepts |
| `imlui.types.script_object` | `ScriptObject`: `notify`, `emit`, `handle()` (`ScriptObject::Handle`: `alive()`, `post(fn)`, for worker threads), the Lua link (`luaState`, `pushSelf`, `attach`/`relink`/`detach`, used by the registry) |
| `imlui.ui.main_thread` | `post(fn)`: hand work to the main thread from any thread; `runPosted`, `clearPosted`, `postedCount`, `setPostWakeHandler` (used by the engine) |
| `imlui.types.frame` | `Frame`: the render-time context; `Frame::Child`: a child handle |
| `imlui.types.element` | `Element`: `render`, `componentComplete`, `destroyed`, `tick` / `setTicking` |
| `imlui.types.type_info` | `TypeInfo`: the type-erased member table a `TypeBuilder` fills in |
| `imlui.types.type_builder` | `TypeBuilder<T>`: `property` (with an optional default), `signal`, `method`, `nonVisual`, `defaultProperty`, `extends` |
| `imlui.types.overload` | `overload<Args...>(&T::fn)`, `constOverload<Args...>(&T::fn)`: pick one of overloaded members |
| `imlui.types.registry` | `TypeRegistry`: `registerType<T>`, `registerSingleton<T>` (reference or `shared_ptr`), `install`, `activate`, `sync`, `typeNames`; it is the `Renderer::Types` the renderer dispatches every element to |
| `imlui.ui.elements.builtin` | `registerBuiltinTypes(TypeRegistry&)`: every built-in element |

`Engine` forwards `registerType<T>(name)` and both `registerSingleton(name, obj)` overloads to its registry, which
`types()` returns. All of these come with `import imlui;`.

From Lua, `ui.types()` lists the registered element type names, and `require("imlui.types")` is the small native
library the DSL uses (`names()`, `isType(name)`, `attach(element)`, `tick(now)`).

**Limitations**

- Properties are synced, not observed: a setter runs at the element's next render, method call or `as<T>()`,
  not when the Lua value changes.
- Methods are member function pointers. Default arguments aren't seen (pass all arguments, or wrap the
  method).
- A method can't share a name with a property or signal of the same type.
- Singleton handlers don't receive `self`. Element handlers do.
