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

- [Element types](#element-types): `Element`, `render(Frame&)`, the lifecycle hooks, custom layouts (layout boxes, the
  layout cache)
- [Declaring members](#declaring-members): `TypeBuilder<T>`: properties, signals, methods, docs (`doc`),
  `nonVisual`, `defaultProperty`, the size policy (`fillsWidth`), [intrinsic sizes](#intrinsic-sizes), aliases,
  subclasses (`extends`)
- [Property sync](#property-sync): how Lua values reach the setters every frame
- [Telling Lua about changes](#telling-lua-about-changes): `notify` and `emit`
- [Singletons](#singletons)
- [Value conversion](#value-conversion): `LuaConvert<T>`, enums and structs, your own types, Lua callbacks
  (`LuaFunction`)
- [Lifetime](#lifetime) and [hot reload](#hot-reload)
- [Resources and the host config](#resources-and-the-host-config): mounts, the entry script
- [Editor support: Lua definitions](#editor-support-lua-definitions): LuaLS `---@meta` files of the DSL and the
  registered types, `--lua-defs <dir>`, hover docs, the `luaType` hook
- [Testing elements](#testing-elements): the headless frame harness
- [Threading and frame requests](#threading-and-frame-requests): `runAsync` (and its `Task`), [futures](#futures)
  (`Future<T>`, `Promise<T>`, progress, Lua futures as arguments), `Live<T>`, the background pool;
  [ticking elements](#ticking-elements)
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
    virtual std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth); // size before rendering
    virtual bool contentFillsWidth(const Frame& frame); // fills because of what it holds (a container)
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
| `size(key)` | A size prop in pixels, read like the built-ins read `width`/`height` (`fillWidth`, fill-minus values; 0 when unset). An unset `width` of a type declared [`fillsWidth()`](#nonvisual-defaultpropertyname-and-fillswidth) reads as `-1` (fill): the size policy lives in the type, not in the widget. Pass it to the `md::` widgets, which resolve it (0 is their natural size). |
| `available()` | The space available at the cursor (`ImVec2`, pixels): the width to the right edge of the layout box the parent gave (a fixed `Column`, a `Row` share, a `Grid` cell, else the window's content region), and the height left in the window. Use it instead of `ImGui::GetContentRegionAvail()`, which only knows the window. |
| `resolvedWidth(natural = 0)`, `resolvedHeight(natural = 0)` | `width` / `height` (`size()`) resolved to pixels against `available()` with the one size rule every element follows (`resolveSize`, module `imlui.ui.item_size`): fixed, `-1` fill, `< -1` fill minus, `natural` when unset; a type that fills by default fills. Use it when the element sizes itself (`ImGui::Dummy`, a draw-list box). |
| `labelId(text, id)`, `alignFactor(align)` | `"text###id"` for labels whose text changes; `"start"`/`"center"`/`"end"` as 0/0.5/1. |
| `dp(v)` | dp to pixels at the current scale (monitor content scale times the user zoom). |
| `drawList()` | The current window's `ImDrawList*`. Use the [md shape helpers](development.md#adding-an-element) (`strokeArc`, `fillRoundRect`, `textTop`, ...) for Material-quality shapes; they build nothing outside the clip rect, and `md::rectVisible(dl, a, b)` lets your own drawing skip an off-screen element the same way. |
| `requestFrame()`, `requestFrameAfter(s)` | Frames are [rendered on demand](architecture.md#on-demand-rendering): anything that changes on its own asks for the next one. |
| `theme()`, `scheme()` | The current theme and Material color scheme. The md surface stack (`md::pushSurface`/`popSurface`/`surfaceColor`, `imlui.widgets`) works as in the widgets. |
| `images()` | The `ImageCache` (module `imlui.image.image_cache`) that `Image` draws from: `images()->get("app:/x.png")` gives a decoded image (`ready()`, `width()`, `texture()` for `drawList()->AddImage`), or its `error()`. The reference is valid until the end of the frame: an image not drawn for two frames may be evicted (the cache's byte budget) and is decoded again on the next `get`. Null when the host set none. |
| `setWindowTitle(title)` | The native window title (what `ApplicationWindow`'s `title` sets). |
| `node()` | The element table as a `luaui::Node`: an escape hatch to raw Lua (`node().pushRaw("__children")`, a painter callback, a table of rows). |
| `renderer()` | The `Renderer`, for the rare layout the members above don't cover (`Grid` walks every child, hidden ones too, with `renderer().forEachChild`). Prefer the members. |

### Child handles

`Frame::Child` is a lightweight handle, valid inside the callback that gives it:

| `Frame::Child` member | What |
|---|---|
| `render()` | Renders the child at the cursor through the renderer, with the same per-element wrapper as everywhere (see below). False if it's hidden or unknown. Afterwards a visual child is the last ImGui item (`ImGui::GetItemRectSize()`), and its size is in the layout cache for the next frame. |
| `render(right)` | The same inside a layout box ending at screen x `right`: the child's available width (fills, `align`, text wrapping, raw ImGui item widths) is `right - cursor x`. How a layout gives a child less than its own width (a `Row` share, a column of a custom grid). |
| `renderUnseen()` | Renders the child measured but not drawn (clipped away) and asks for another frame: for a child you can't place yet because `expectedSize()` is unknown (its first frame), so it doesn't flash in the wrong place. |
| `fillsWidth()` | Whether the child fills the available width: a fill or fill-minus `width` (`fillWidth`), or no `width` on a type that fills by default (`fillsWidth()` / `fillsWidthWhen()` in its `declare`) or on an element that fills by its content (`Element::contentFillsWidth`: a `Column` holding a fill, a `Row` with fills or a `justify`). Read it before rendering to share space, like `Row`. Syncs the child's properties; computed once a frame. |
| `isVisual()` | False for non-visual types (`Timer`, `Dialog`, ...). Layouts render those where they are and don't place, count or space them. |
| `node()`, `type()` | The child's element table and its type name (`"Spacer"`). |
| `id()` | A stable ImGui ID for the child in the parent's ID scope: the key of its size in the layout cache, and for your own per-child state in `ImGui::GetStateStorage()`. |
| `intrinsicSize(maxWidth)` | The child's [intrinsic size](#intrinsic-sizes) with `maxWidth` available, when it can tell before rendering (the built-in leaves, stacks of children that can, your own); `nullopt` otherwise. Pass the width the child will render in (a fill's size depends on it). Syncs its properties; computed once a frame per `maxWidth`. |
| `expectedSize(maxWidth)` | What a layout should place the child by: its intrinsic size, else its size on the last frame (the layout cache), else `nullopt` (its first frame). |
| `expectedWidth(maxWidth, fallback)`, `expectedHeight(maxWidth, fallback)` | `expectedSize()`'s width / height, or `fallback`, **and** a note to the layout cache that you place the child by it: if the child then renders at another size (by more than half a pixel), another frame is requested, so the layout settles even when nothing else asks for frames. Use these for placement. |
| `lastWidth(fallback)`, `lastHeight(fallback)` | The same from the layout cache only: the child's size when it was last rendered, or `fallback`. |
| `as<T>()` | The C++ instance of a typed child as a `T*`, with its declared properties **synced first**, so the parent reads this frame's values (`child.as<Tab>()->title()`). `nullptr` when the child isn't a typed element or not a `T`. `syncProperties()` does the sync and returns the `ScriptObject*`. |

```cpp
// A simplified Row: each child gets an equal column of the available width, vertically centered in the tallest.
void Columns::render(imlui::Frame& frame) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    int n = 0;
    frame.forEachChild([&](imlui::Frame::Child& child) { n += child.isVisual() ? 1 : 0; });
    const float colW = n > 0 ? frame.available().x / static_cast<float>(n) : 0.0f;
    // Pass 1: the sizes the children are expected to take in their column (intrinsic, else last frame's).
    float tallest = 0.0f;
    bool unknown = false;
    frame.forEachChild([&](imlui::Frame::Child& child) {
        if (!child.isVisual()) return;
        unknown = unknown || !child.expectedSize(colW);
        tallest = std::max(tallest, child.expectedHeight(colW));
    });
    // Pass 2: place and render.
    float x = start.x, bottom = start.y;
    frame.forEachChild([&](imlui::Frame::Child& child) {
        if (!child.isVisual()) { child.render(); return; }   // a Timer, a Dialog, ...
        const float h = child.expectedHeight(colW);          // the cache checks it against what renders
        ImGui::SetCursorScreenPos({x, start.y + std::max(0.0f, (tallest - h) * 0.5f)});
        if (unknown) {
            const imlui::layout::BoxScope box{imlui::layout::boxTo(x + colW)};
            child.renderUnseen();                            // a first frame: measure, show it on the next
        } else {
            child.render(x + colW);                          // fills in the child stop at its column
        }
        bottom = std::max(bottom, ImGui::GetItemRectMax().y);
        x += colW;
    });
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy({std::max(1.0f, x - start.x), std::max(1.0f, bottom - start.y)}); // the layout's own extent
}
```

How a container lays out its children, in short: **constraints go down** (give each child a layout box:
`child.render(right)`, or a `layout::BoxScope` around several), **sizes come up** (place children by
`expectedWidth`/`expectedHeight`, which are this frame's intrinsic sizes or last frame's measured ones), and the
layout cache **converges** (a size you placed by that turns out stale requests another frame). Ask a child with the
width it will render in: a fill's size depends on it (a `Row` asks its fills with their share). If your container's
own size follows from its children's, give it an [intrinsic size](#composed-sizes) too, so containers around it place
it on its first frame. Read sizes in the ID
scope the children render in (inside your `BeginChild`, if you open one). Never keep your own copies of children's
sizes in `ImGui::GetStateStorage()` or members: the cache drops the sizes of elements that went away, and the
copies would miss the convergence. For a size of your own that you place by before you know it (a shadow drawn
under content of last frame's height, a total), use the cache directly (module `imlui.ui.layout_cache`):
`layout::lastSize(id)`, `layout::assumeWidth/assumeHeight(id, used)` before, `layout::recordSize(id, size)` after.

Layout boxes (module `imlui.ui.layout_box`, namespace `imlui::layout`) are what `available()` reads. To give all
your children a narrower width, push one around them: `const layout::BoxScope box{layout::boxTo(right)};` (for one
child, `child.render(right)` does it). An element that opens its own region (`ImGui::BeginChild`, a popup) can push
`layout::BoxScope{layout::regionBox(), layout::Mirror::None}` after it; without one, its children measure
that window's content region anyway. A region that **takes its content's size** (an `ImGuiWindowFlags_AlwaysAutoResize`
window, a popup) must push `layout::BoxScope{layout::shrinkToFitBox(minWidth, maxWidth)}` instead: there a fill is
`minWidth`, never the window's current width (which would keep the window from ever shrinking), and text wraps at
`maxWidth`. An item that spans the window but needs its own width (a menu row) takes `layout::claimWidth(natural)`:
the fill width, which grows to the widest claim from the next frame on. For begin/end APIs, `layout::pushBox(box)`
and `layout::popBox()` do the same as a `BoxScope`; pop before `ImGui::End()`/`EndPopup()`. A box also sets ImGui's item width and text wrap position, so **raw ImGui
widgets follow it**: `ImGui::SliderFloat` takes the box's width and `ImGui::TextWrapped` wraps at its edge, inside a
fixed `Column` too. (Plain `ImGui::Text` wraps there as well while a box is pushed; `md::text` without `wrap` stays on
one line.)

For a child that is still a handler-function element, read its props through `child.node()`
(`child.node().string("title")`).

Each visual element is wrapped in an ImGui group and an ImGui ID scope (made from its instance's
`ScriptObject::objectId()`, unique and never reused), so plain ImGui IDs such as `"##gauge"` are unique per element,
ImGui state kept under them (`ImGui::GetStateStorage()`, an open tree, a scroll position) belongs to this element
only, and the element counts as one item in a `Row`. A container that renders a child's children itself, rather than
the child, pushes the child's scope around them with `child.pushId()` / `ImGui::PopID()`.

The common props keep working on typed elements without being declared, exactly as on handler-function elements:
the renderer skips the element when `visible` is false, wraps it in `BeginDisabled` when `enabled` is false, in an
ImGui group when it's visual, and in `PushID`, and shows `tooltip` after it. `id` registers the element for lookup
by name, and sizes are read with `frame.size("width")` / `frame.size("height")` (or resolved to pixels with
`frame.resolvedWidth(natural)` / `frame.resolvedHeight(natural)`), the type's fill policy applied.

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
  (so C++ member initializers are the defaults; captured once per type, from its first instance, and shared by all:
  a constructor that gives each instance a different value resets them all to the first one's), or the value given
  as the fourth argument,
  `property(name, getter, setter, defaultValue)`. An `std::optional<T>` property takes no default: `nil` is
  `std::nullopt`. A value type that can't be copied keeps its current value on `nil`.

### Signals

```cpp
type.signal("onPeak");
```

Declares a signal name for documentation and validation (it reserves the name). Emit it from C++ with
`emit("onPeak", args...)`. `type.signal<double>("onPeak", {"value"})` also gives its argument types (after `self`) and
names, for the [Lua definitions](#editor-support-lua-definitions) (only there: `emit` isn't checked against them). Handlers are ordinary `on*` fields of the element table. Every property also has a
change signal `on<Name>Changed`, emitted by `notify(name)`, without declaring it.

A signal has one handler slot (`onPeak = fn`) and any number of **listeners**, which scripts add with
[`connect(obj, "onPeak", fn)`](lua-api.md#connecttarget-signal-fn---disconnect). `emit` and `notify` call the slot
first, then the listeners in connect order, with the same arguments. `connect` takes only the declared signals and
the `on<Name>Changed` signals of declared properties (a typo raises `Gauge has no signal 'onPaek'`): an undeclared
name you `emit` still reaches the slot, but can't be connected. This holds for every type, the built-ins too
(`Button`'s `onClicked`, `Slider`'s `onValueChanged`): all of them emit through `ScriptObject`. Listeners live in
the Lua state: there is no C++ `connect` (yet); C++ that wants to observe an object calls into it directly.

### Methods

```cpp
type.method("reset", &Gauge::reset)
    .method("setRange", &Gauge::setRange); // void setRange(double min, double max)
```

Methods are member function pointers, `const` and `noexcept` ones included. A third argument names the parameters for
the [Lua definitions](#editor-support-lua-definitions): `.method("setRange", &Gauge::setRange, {"min", "max"})`. Lua calls them with `:` on the element
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

A method whose result arrives later returns an [`imlui::Future<T>`](#futures): the script gets a future it can
`await`.

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

### Docs: `doc(text)`

`doc(text)` documents what the call right before it declared, for the [Lua definitions](#editor-support-lua-definitions)
(the editor shows it on hover and in completion): a property, signal or method, or the type itself as the **first**
call in `declare()`:

```cpp
static void declare(imlui::TypeBuilder<Gauge>& type) {
    type.doc("A gauge with a needle.")
        .property("value", &Gauge::value, &Gauge::setValue)
        .doc("The needle's position, 0..`max`.")
        .property("max", &Gauge::max, &Gauge::setMax, 1.0)
        .doc("The end of the scale.")
        .signal<double>("onPeak", {"value"})
        .doc("Called when the needle reaches `max`.")
        .method("setRange", &Gauge::setRange, {"min", "max"})
        .doc("Sets both ends of the scale.\n\n`min` must be below `max`.");
}
```

- The text is Markdown; `\n` starts a line, `\n\n` a paragraph. `""` (or no `doc`) leaves it undocumented.
- Misuse throws `std::invalid_argument` at registration: a second `doc` for the same thing, or a `doc` after a call
  that declares no member (`extends`, `defaultProperty`, `nonVisual`, `fillsWidth`, ...: put the `doc` right after the
  member).
- The `on<Name>Changed` signal of a property is documented for you ("Called when `name` changes, with the new value.").
  The common props (`id`, `visible`, `width`, ...) are documented once, in `ElementProps` and `Element`.
- A [subclass](#subclasses-extendsbase) keeps its base's member docs; `docMember(name, text)` replaces one of them
  (`type.extends<TextField>().docMember("text", "...")`, throws for an unknown name). The base's type doc isn't the
  subclass's: document the subclass with `doc()` before `extends()`.

Every built-in documents its type and members (`tests/element_docs_test.cpp` checks it, see
[Adding an element](development.md#adding-an-element)).

### `nonVisual()`, `defaultProperty(name)` and `fillsWidth()`

- `type.nonVisual()` makes the element take no layout space or spacing, like `Timer` or `Dialog`.
- `type.defaultProperty("value")` lets a positional first value set the property, `Gauge { 0.5 }`, like `text`
  for `Button { "OK" }`. A table in that position is a child, not a value.
- `type.fillsWidth()` is the type's **size policy**: an unset `width` fills the available width (like
  `width = -1`), as for `Card`, `TextField` or `Slider`. It is the one place the default lives: the element's
  `frame.size("width")` reads `-1` and `frame.resolvedWidth()` fills, so `render()` passes them on and the `md::`
  widgets need no default of their own (to them, 0 is their natural width). A `Row` reads it **before** rendering
  the child and gives it a share of the free width instead of letting it run to the row's edge.
  `type.fillsWidthWhen(pred)` decides it per element from the synced instance, `pred(const T&)`: `Text` declares
  `.fillsWidthWhen(&Text::wrap)`, `Spacer` `.fillsWidthWhen(&Spacer::fill)`. A `width` prop always wins
  (`Frame::Child::fillsWidth()`).

### Intrinsic sizes

`intrinsicSize(frame, maxWidth)` returns the size `render()` will take where `maxWidth` is the width available at
its cursor, when the element can tell before rendering (`frame` is the element's own, its properties synced).
Layouts that place children by their sizes (`Row`, the stack `align`, `TopAppBar` actions, `NavigationRail`, and
containers composing their own size) use it instead of the size of the last frame, so the element is in place on its
first frame and on the frame it changes, with no extra frame and no unseen first frame. The default returns
`std::nullopt` (unknown: layouts use the layout cache, one frame behind). **Implement it whenever your size is
predictable** from your props, the font and the theme; every built-in leaf does.

It must be **exactly** the size of what `render()` then draws (its group), or the layout cache keeps requesting
frames to settle. Don't compute the size twice: **share a size helper with your render**, the way the `md::`
widgets do (`md::button` draws at `md::buttonSize`, `md::checkbox` at `md::checkboxSize`, `md::textField` at
`md::textFieldSize`, `md::text` at `md::textSize`), so the two can't drift. Mind ImGui's whole pixels: ImGui starts
each new line (after an item) on a whole pixel, and sizes a child window in whole pixels.

- **Fills report too.** A fill's size is the width it takes of `maxWidth` (resolve the `width` prop against it with
  `frame.widthIn(maxWidth, natural)`, not `frame.resolvedWidth()`, whose layout box doesn't exist yet), and the
  parent asks with the width it gives it: a `Row` asks its fills with their share. Wrapping text reports the size it
  wraps to in `maxWidth`.
- **A size that depends on `maxWidth` means you fill.** Declare it (`type.fillsWidth()`, `fillsWidthWhen(pred)`, or
  `contentFillsWidth()`), so a `Row` gives you a share and asks with it; otherwise the row asks with its whole width
  and renders you in less, and the size never matches.
- **A height that fills** (`height = -1`) is known only where the element renders: return `std::nullopt`.
- It is computed **once a frame** per `maxWidth` (the registry memoizes it, and drops the answer when Lua that may
  change tables ran), so a container may ask, and so may the container around it.

```cpp
std::optional<ImVec2> Badge::intrinsicSize(const imlui::Frame& frame, float) {
    return badgeSize(frame); // render() draws ImGui::Dummy(badgeSize(frame)) and paints into it
}
```

### Composed sizes

A container whose size follows from its children's composes its intrinsic size from theirs, like the built-in
stacks: `Column` (its stack, padding and `width`), `Row` (the same plan its render uses: widths side by side, fills
sharing the rest, the tallest height), `Card` (its stack plus padding; a fixed `height`), `Section` (header and,
expanded, the stack), `TopAppBar` and `NavigationBar` (their fixed heights). `frame.stackSize(maxWidth)` composes a
`renderStack()` (the children's intrinsic sizes, `spacing`, `align` and ImGui's whole-pixel lines). As soon as one
visual child has no intrinsic size (a custom element without one, a `ScrollView`), return `std::nullopt`: the
container then falls back to the layout cache like any other element, and settles a frame later. Ask the children
with the width they will render in (your content width: less your padding).

A container whose **width** follows the available width because of what it holds (a `Column` holding a fill, a `Row`
with fills or a `justify`) says so with `contentFillsWidth(frame)` (asked only while its `width` is unset), so a `Row`
gives it a share, like a fill, instead of rendering it in whatever the children after it leave.

```cpp
std::optional<ImVec2> Panel::intrinsicSize(const imlui::Frame& frame, float maxWidth) {
    const float pad = frame.dp(12);
    const std::optional<ImVec2> stack = frame.stackSize(maxWidth - 2 * pad); // render(): Indent + renderStack()
    if (!stack) return std::nullopt;
    return ImVec2{stack->x + 2 * pad, stack->y + 2 * pad};
}

bool Panel::contentFillsWidth(const imlui::Frame& frame) {
    bool fills = false;
    frame.forEachChild([&](imlui::Frame::Child& c) { fills = fills || (c.isVisual() && c.fillsWidth()); });
    return fills;
}
```

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
without members. The inherited members keep their [docs](#docs-doctext); `docMember(name, text)` replaces one.

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

**Static properties are converted once.** A property whose raw value in the element table is a plain value (a
string, number, boolean, or nil) converts to the same thing until that field is written, so a sync converts it only
when something changed: the element table's declared fields (a fingerprint of their raw values: an assignment from
Lua, a `notify` write-back, a raw write), its metatable, or the theme (a color role). Otherwise only the **dynamic**
properties are read again: functions, `bind()`s and any other table (it may have changed in place), userdata, and,
on an element table with a metatable of its own, the fields its `__index` answers. A full sync also follows one that
an error cut short. The setter rule is unchanged, so this only saves work: a static property's setter wasn't called
on those frames anyway. A `LuaConvert<V>::get` must therefore depend only on the Lua value (and the theme), not on
other state that changes between frames.

**Lazy sync.** An element that isn't rendered (on a hidden page, `visible = false`, a model item that isn't shown)
is synced when something needs it:

- before a **method called from Lua** runs on it (`cpu:reset()`), so an element that never rendered still sees
  its Lua values on its first method call;
- when a **parent or another element** asks for it: `Frame::Child::as<T>()` and `TypeRegistry::sync(node)`.

While a frame renders, an element is synced **once**: a layout that asks a child whether it fills, for its intrinsic
size or for `as<T>()`, and then renders it, syncs it on the first of these, and the others reuse that sync (a
`Row` child's function-bound prop runs once a frame, not three times). Lua code that may have changed element
tables in between (a signal handler, a `notify` write-back: anything run through `luaui::safeCall`, which counts in
`luaui::changes()`) makes the next of them sync again, so a change a handler made earlier in the frame is rendered
in the same frame. Property reads (bindings, bind sources, `__children`) run through `luaui::safeRead` and don't
count. A method called from such a read (a binding that calls `other:formatted()`) reuses the frame's sync the same
way; called from anything else (a handler, a script), it syncs, because Lua code there may have changed what the
bindings read without the count seeing it. `TypeRegistry::sync(node)` and `tick()` always sync. A skip also checks a fingerprint of the
element's declared fields, so a write to them that bypassed the count is synced anyway, and a debug build reports it
as a script error: C++ that writes a Lua table writes through `luaui::Node::set` (or calls `luaui::noteChange()`
after a raw `lua::setfield`), and a binding only reads.

`componentComplete()` runs once, right after the first sync, wherever that happens (first render, first method
call, or a parent's `as<T>()`). An exception thrown by `componentComplete()`, `tick()` or `destroyed()` is
reported as a script error (`Gauge.componentComplete(): ...`) and the element carries on (or is deleted anyway,
for `destroyed()`). A Lua error during the sync itself is reported too, and the next sync runs normally. A binding
that calls a method of its own element
(`text = function(self) return self:describe() end`) doesn't sync it again from inside its sync. Lua never reads an
element's property through the instance (it reads the element table), so that needs no sync.

Because the sync compares with the getter, the element table is the source of truth: a C++ change that isn't
`notify`'d is overwritten by the Lua value at the next sync that converts the property, which for a plain value is
only after its field (or the theme) changes, for a binding or `bind()` at the next sync. Always `notify` after
changing a property in C++: it writes the value to the table, so Lua sees it and every sync after it agrees.

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
handler error goes to the error overlay and doesn't propagate. Then the [listeners](#signals) run, in connect
order; each one's error is reported and the others still run. `emit` returns `true` when the handler or a listener
ran without an error (`false` with neither, or without a Lua link), and requests a frame, since handlers usually
change state. An object nobody connected to pays one flag check for listeners: the slot is all it reads.

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

If it dies earlier anyway, or a second `registerSingleton` under the same name replaces it (which destroys an
engine-owned object), the global that running scripts still hold doesn't reach the dead object: any use of it raises
the Lua error `singleton 'backend' was destroyed`. A replaced object that lives on is unlinked from the running
state; scripts see the new object after the next reload.

Scripts see a global userdata:

| Lua | C++ |
|---|---|
| `backend.load` | the `load` getter |
| `backend.paused = true` | the `paused` setter (when the value changed). A read-only property raises `backend.time is read-only`, an unknown name `... has no property 'x'`. `backend.paused = nil` resets the property to its default (its value at registration, or the declared `defaultValue`). |
| `backend:tick()` | the `tick` method |
| `backend.onTick = function(n) ... end` | stores a handler for `emit("onTick", n)`. Any `on*` name accepts a function or `nil`. |
| `backend.onLoadChanged = function(v) ... end` | called by `notify("load")` |
| `connect(backend, "onTick", function(n) ... end)` | adds a listener, after the handler; any number of them (returns a disconnect function) |

Singleton handlers get no `self` (the singleton is a global anyway). `notify` on a singleton doesn't write
anything back (Lua always reads it through the getter), but it does call the `on<Name>Changed` handler and
request a frame, so bindings that read it show the new value.

To bind a widget two-way to a singleton property, use `bind(getter, setter)`:

```lua
Switch { text = "Paused", checked = bind(function() return backend.paused end,
                                         function(v) backend.paused = v end) }
```

**Work a singleton started outlives the script that started it.** A singleton doesn't die on a
[hot reload](#hot-reload), so neither do its [handles](#lower-level-handles-and-posts): a sampler it started with
`runAsync`, the `Live<T>` writers it handed out, a `runAsync(work) -> Future` job keep running after the script that
asked for them is gone. Their results still arrive (`done` runs, `Live` values apply and reach the new script's
bindings), but nothing in the new script asked for them, and a script that starts the work again on load starts a
second copy. Override **`scriptReloading()`** to stop it:

```cpp
void Lab::scriptReloading() { stopTelemetry(); } // the new script starts it again if it wants it
```

`TypeRegistry::install()` calls it on the main thread for every singleton an earlier state had (not on the first
load), before the new state links the singleton and before the new script runs. The singleton is still linked to
the old script's state then (if it is still open), so a `notify`/`emit` in the hook reaches the old script, one
last time. An exception is reported and the reload goes on. The default does nothing; elements never get it (they
die with their state).

Whether the new script loads can't be known before it runs, and running the hook after it loaded would stop what
the new script just started. So a reload whose script **fails** has already stopped the work: the old UI keeps
running without it (behind the error overlay) until the next successful reload starts it again. The hook isn't
called on the way back to the old state. Also useful:

- **Starting again replaces the old run.** Keep the run's `std::stop_source` (and a run counter) in the singleton;
  `start` stops the previous run before it starts a new one, and a `done` from an older run is ignored. Then
  calling `backend:startSampling()` twice gets one sampler, not two.
- A future the old script awaited is settled on the C++ side only (a no-op for Lua). Its job runs to the end unless
  it is cancelled first: cancel long jobs with `fut:cancel()` when the script moves on, or in `scriptReloading()`.

## Value conversion

`imlui::LuaConvert<T>` (module `imlui.types.lua_convert`) converts property values, method arguments and
results, and signal arguments. Built in:

| C++ | Lua | Notes |
|---|---|---|
| `bool` | any | Lua truthiness: only `nil` and `false` are false. |
| integral types | number | Numeric strings convert. Floats truncate toward zero; out-of-range values clamp. |
| `float`, `double` | number | Numeric strings convert. A `float` reads NaN as 0 and clamps finite values beyond its range; infinities stay. |
| `std::string` | string | Numbers convert (`7` reads as `"7"`). |
| `std::string_view`, `const char*` | string | Push only (results, signal arguments, read-only properties). |
| `imlui::Color` | color spec | Reads a role name, `"role/alpha"`, hex or `{r, g, b, a}`, like every color prop. Pushes `"#RRGGBB[AA]"`. |
| `std::optional<T>` | `nil` or `T` | A method argument that is left out reads as `nil` too, so trailing optional parameters may be omitted. |
| `std::vector<T>` | sequence | Every key must be an integer 1..n (`{ x = 1 }` and `{ 1, x = 2 }` fail instead of losing `x`; `{}` is an empty vector) and every element convert. |
| `std::map<std::string, T>`, `std::unordered_map<std::string, T>` | table with string keys | Every key must be a string (a number key fails, so `{ "a" }` is no map) and every value convert. |
| `std::pair<A, B>`, `std::tuple<Ts...>` | sequence `{a, b, ...}` | Element *i* converts as the *i*-th type. A longer sequence, or any other key, fails; a missing trailing element is `nil` (a `std::optional` element takes it). |
| `std::variant<Ts...>` | any alternative | Pushes the active alternative. Reads the **first** alternative whose check passes, so list the specific ones first: `bool` takes every value, `std::string` takes numbers, an integer takes numeric strings. A vector and a map tell sequences from string-keyed tables in either order; `{}` is both an empty sequence and an empty map, so the one listed first takes it. |
| `std::monostate` | `nil` | The empty alternative of a variant. |
| an enum with [`EnumNames<E>`](#enums-and-structs) | one of its names | Pushes the name. Reads the names only (case-sensitive): an integer or an unknown name fails. A value without a name pushes its underlying integer, which doesn't read back. |
| a struct with [`LuaFields<T>`](#enums-and-structs) | table | One key per field. A missing field keeps its default (see below). |
| [`imlui::LuaFunction`](#lua-functions-luafunction) | function | A callback the script hands to C++. |

They compose: a `std::vector` of structs whose fields are maps of variants converts, and so does a method taking a
`std::optional<LuaFunction>`. Tables are read raw (no `__index`, `__len` or `__pairs`), so a check never runs Lua
code. Each type has a `name` for error messages, built at compile time from its parts: `bad argument #1 to
'Plot:setSeries' ({[string] = {x, y}...} expected, got string)`, `Plot.quality: 'low'|'high' expected, got number`.

**Errors name the field.** When a composite value doesn't convert, the message points at the part that failed, with a
path into the value (`.field`, `[index]`, `.key` or `["some key"]` for a map), and an enum shows the string it got:

```text
bad argument #1 to 'lab:crunch' (CrunchOptions.method: 'trial'|'wheel' expected, got string "fast")
Plot.series.points[3].x: number expected, got string
bad argument #1 to 'Plot:setTags' ({[string] = number} expected, got table with key 1)
LuaFunction: bad result (y: number expected, got string)
```

A property's path starts at `Type.property`; a method argument's or a callback result's at the struct's name when it
has a [`name`](#enums-and-structs) that is a plain identifier, else at the path itself (`[2].x: ...`). A variant
reports the one alternative the value had the shape of (a table that is a sequence, for the vector), else
`integer|{x, y} expected, got string`.

### Enums and structs

Both are opt-in: specialize a trait in namespace `imlui` (in your module, next to the type). Macros can't be
exported from a module, so the traits are plain templates.

```cpp
enum class Quality { Low, High };
struct Vec2 { double x = 0, y = 0; bool operator==(const Vec2&) const = default; };
struct Series {
    std::string label = "series";
    std::vector<Vec2> points;
    std::optional<double> max;       // a missing field reads as std::nullopt
    Quality quality = Quality::High; // a missing field keeps this default
    bool operator==(const Series&) const = default;
};

template <> struct imlui::EnumNames<Quality> {
    static constexpr std::pair<Quality, std::string_view> values[] = {{Quality::Low, "low"}, {Quality::High, "high"}};
};
template <> struct imlui::LuaFields<Vec2> {
    static constexpr std::tuple fields{imlui::field("x", &Vec2::x), imlui::field("y", &Vec2::y)};
};
template <> struct imlui::LuaFields<Series> {
    static constexpr std::string_view name = "Series"; // optional: errors say "Series", not "{label, points, ...}"
    static constexpr std::tuple fields{imlui::field("label", &Series::label), imlui::field("points", &Series::points),
                                       imlui::field("max", &Series::max), imlui::field("quality", &Series::quality)};
};

type.property("series", &Plot::series, &Plot::setSeries); // const Series& series() const; void setSeries(Series)
```

```lua
Plot { series = { label = "cpu", points = { { x = 0, y = 1 }, { x = 1, y = 3 } }, quality = "low" } }
```

- `EnumNames<E>::values` lists `{value, "name"}` pairs. An optional `name` replaces the default error name
  (`'low'|'high'`).
- `LuaFields<T>::fields` is a tuple of `imlui::field("key", &T::member)` (an `imlui::LuaField<T, M>`). An optional
  `name` replaces the default error name (`{x, y}`). `T` must be default-constructible.
- **Missing fields.** Reading starts from `T{}`. A missing (`nil`) field keeps its default member value, like an
  element property that is `nil`; a missing `std::optional` field reads as `std::nullopt`. A field that is present
  but doesn't convert fails the check, which rejects the whole value like any other mismatch. Keys that aren't
  fields are ignored. Pushing writes every field; a `std::nullopt` field stays absent.
- Give structs an `operator==`, so the property sync can skip unchanged values.

### Your own types

Specialize `LuaConvert` itself for anything else (in your module, in namespace `imlui`):

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

Optionally, say *why* a value doesn't convert with `static std::string mismatch(lua::State*, int idx)`. It is only
called after `check` returned false, and like `check` must not raise or leave anything on the stack. Return
`"<what> expected, got <what>"`, or for a part inside the value that part's path first: `".x: number expected, got
string"` (`imlui::luaMismatch<M>(s, idx)` gives a member type's text to build on). Without it, errors say
`{x, y} expected, got table` (`luaMismatch<T>` falls back to `name`), also inside a vector or struct of your type.

For the editor's [Lua definitions](#editor-support-lua-definitions), add `static std::string luaType()`, the LuaLS type
of its values (`"{ x: number, y: number }"`); without it the type is `any` unless `name` is a Lua type name.

### Lua functions: `LuaFunction`

`imlui::LuaFunction` (module `imlui.types.lua_function`) is a C++ handle to a Lua function. A property, method
argument or result, struct field or signal argument of that type takes a function from the script.

```cpp
type.method("forEach", &Backend::forEach)                       // int forEach(const imlui::LuaFunction& fn)
    .property("filter", &Backend::filter, &Backend::setFilter); // std::optional<imlui::LuaFunction>

int Backend::forEach(const imlui::LuaFunction& fn) {
    int n = 0;
    for (const Item& item : m_items) {
        ++n;
        if (fn.call<std::optional<bool>>(item) == false) break; // only an explicit `return false` stops
    }
    return n;
}
```

`call<bool>` would read Lua truthiness, so a callback that returns nothing (`nil`) would read as `false` and stop after
the first item; `call<std::optional<bool>>` reads `nil` as `std::nullopt` (and an error as an empty outer optional),
which keep going.

```lua
backend:forEach(function(item) print(item.name); return item.name ~= "stop" end)
backend.filter = function(item) return item.size > 10 end
Chart { formatter = function(v) return ("%.1f"):format(v) end } -- a LuaFunction property: not a binding
```

| Member | What |
|---|---|
| `call(args...)` | Calls the function with `args` converted by `LuaConvert`. Returns whether it ran without an error (`false` when expired). |
| `call<R>(args...)` | The same, returning the first result as `std::optional<R>`: `std::nullopt` when expired, after an error, or when the result doesn't convert (reported: `LuaFunction: bad result (integer expected, got string)`). `R = bool` is Lua truthiness (`nil` is `false`); use `std::optional<bool>` to tell `nil` from `false`. |
| `expired()`, `explicit operator bool` | Whether the handle is empty or its Lua state is closing or closed. |
| `push(state)` | Pushes the function, or `nil` when expired or when `state` is another Lua state. `LuaConvert` uses it. |
| `operator==` | The same Lua function in the same state (copies, or two handles taken from one function). |

- **Errors.** A Lua error in the function (with its traceback) and a C++ exception while converting the arguments
  go to the error overlay, like a handler's, and never propagate to the caller. A call doesn't request a frame (it
  may run during a render, as a comparator, say): call `imlui::requestFrame()` when the callback changed what is
  shown and nothing else asks for a frame.
- **Properties.** On an element, a function is normally a [binding](lua-api.md#property-values), called every
  frame. A property whose type takes functions (`LuaFunction`, or an `optional`/`variant` holding one) gets the
  function itself as its value instead, and syncs it like a plain value: the setter runs again only when the field
  is set to another function.
- **Lifetime.** The handle never keeps its Lua state alive and never dangles. Each state carries a liveness token
  (a userdata in its registry, whose finalizer runs when [hot reload](#hot-reload) or shutdown closes the state,
  before Lua frees anything), and every handle holds its state's token. Once the state is closing or closed, the
  handle is expired: calling it does nothing and returns `false`/`std::nullopt`, pushing it pushes `nil`
  (`backend.filter` reads `nil` after a reload until the new script sets it again), and destroying it doesn't
  touch the state. While the state lives, the last copy's destructor releases the function's registry reference.
  An element holding a handle is destroyed with its state; whichever finalizer runs first, both orders are safe.
- **Threads.** Main thread only, like everything that touches Lua: take, copy, call and destroy handles there (the
  copies share one reference without a lock, and the last one releases it on the state). A worker hands its result
  back with `handle.post`, whose function calls the handle the object keeps:

  ```cpp
  std::thread{[h = handle(), this, input] {    // no LuaFunction copy on the worker
      Result r = compute(input);
      h.post([this, r] { m_onDone.call(r); }); // main thread, only while `this` lives
  }}.detach();
  ```

## Lifetime

**Element instances** belong to Lua:

- The DSL constructor (`Gauge { ... }`) creates the C++ instance **immediately**, through the default
  constructor, and stores it in the element table as `__native`, a full userdata with a `__gc` metamethod. The
  element table gets the metatable all elements of the type share (`require("imlui.types").meta(name)`), whose
  `__index` finds the type's methods, which is how `cpu:reset()` works, and the read-only
  [`parent`](lua-api.md#selfparent). Property reads from C++ (`Node`) skip a shared metatable: they read the table
  itself.
  Element tables otherwise stay what they always were: plain tables with `__type` and array children.
- Nothing else holds the instance: its link back to the table is weak. When Lua collects the element table, the
  userdata goes with it, `destroyed()` runs and the instance is deleted. Elements that the UI keeps (the tree
  returned by the script, captured locals, tables) live as long as the Lua state. Ids don't count: they are
  [weak](lua-api.md#element-ids). Nor do [connections](lua-api.md#connecttarget-signal-fn---disconnect): a
  listener connected to an element never keeps it alive, even one that refers to it.
- Properties are synced at the first render, the first method call or a parent's `as<T>()`, whichever comes
  first, not in the constructor, so the constructor sees no Lua values. Defaults for `nil` are captured right
  after the constructor. `componentComplete()` runs once after the first sync.
- A table built without the constructor (`{ __type = "Gauge" }`) gets its instance at its first render.

**Singletons** registered by `std::shared_ptr` are shared with the registry, which releases them when the engine
is destroyed (after its last Lua state closed). Singletons registered by reference belong to you: the registry
keeps a non-owning pointer, and every Lua state the engine creates gets a global userdata pointing at the object.
That object must outlive `engine.run()`. Declaring it before the `Engine` in `main` is enough.

Don't keep `Element*` pointers to instances in other C++ objects: Lua decides when they die. Keep shared state
in a singleton (or a model the singleton owns) and let elements read it through bindings. Background work that
reports back to an element goes through its `runAsync` or a `Live<T>` (see
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
singletons are re-linked to the new state. `LuaFunction` handles taken from the old state expire (calls become
no-ops; see [Lua functions](#lua-functions-luafunction)). Singleton handlers (`backend.onTick = ...`) and listeners
(`connect(backend, ...)`) belong to a state, so the reloaded script sets them again. If the new script fails to load,
the engine keeps the old state and points the singletons back at it, with that state's handlers and listeners.
Singletons themselves live on, and so does the background work they started: `install()` first calls each
singleton's `scriptReloading()`, which stops it (see [work a singleton started](#singletons)).

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

## Editor support: Lua definitions

An app can write [LuaLS](https://luals.github.io/) (lua-language-server, "sumneko") `---@meta` definitions of
everything its scripts see: the DSL and every element type and singleton it registered from C++, built-in or its own.
An editor with the language server then completes props, signals and methods, shows their types and flags mistakes
(a prop of the wrong type, a misspelled method, a signal name `connect` doesn't know).

**Generating.** Run the app with `--lua-defs <dir>` (or `--lua-defs=<dir>`), or with the environment variable
`IMLUI_LUA_DEFS=<dir>`:

```sh
build/clang-debug/bin/my_app --lua-defs build/lua-definitions
```

`defaultConfig()` takes the option out of the arguments (so it's never taken for a script) and sets
`EngineConfig::luaDefinitions`; `Engine::run()` then writes the files and returns 0 (1 when that failed) without
opening a window, after `main()` registered its types. So it works from CI and from a build step, and the host
writes no paths. A relative `<dir>` is relative to the current folder; it is created when missing. A file whose
content didn't change isn't written again. The C++ side, for tests and hosts of their own:
`engine.writeLuaDefinitions(dir)`, or from module `imlui.types.lua_definitions`, `luaTypeDefinitions(registry)` (the
text of `types.lua`) and `writeLuaDefinitions(registry, dir, prelude)`.

As a build step, with `cmake/ImluiLuaDefinitions.cmake` (included with imlui, like `ImluiResources.cmake`):

```cmake
imlui_add_lua_definitions(my_app)       # target my_app_lua_definitions -> <current binary dir>/lua-definitions/
imlui_add_lua_definitions(my_app ALL OUTPUT ${PROJECT_SOURCE_DIR}/.lua-definitions) # with every build, elsewhere
imlui_add_lua_definitions(my_app ALL LUARC ${PROJECT_SOURCE_DIR})   # and the editor's .luarc.json: it just works
```

It runs the built app on the build machine, so not when cross-compiling.

**`LUARC <dir>`** also writes `<dir>/.luarc.json` at configure time, so the editor needs no setup: `ALL LUARC
${PROJECT_SOURCE_DIR}` and an editor opened on the project folder completes and checks the scripts after the first
build. The file sets Lua 5.4, the definitions folder as `workspace.library`, and `workspace.ignoreDir`: the build
tree and a copy of imlui when they're inside `<dir>` (a submodule: its own `imlui.lua` and tests would define the DSL a
second time), plus `IGNORE <dir>...` (relative to `<dir>`). With `LUARC`, `OUTPUT` defaults to
`<dir>/.lua-definitions/`: every build tree (preset) writes that one folder, so the library path doesn't depend on
which preset was configured or built last, and any build provides the definitions. Git-ignore both (`.luarc.json`,
`.lua-definitions/`): they're generated, and the file names the build tree it ignores. Its first line (a `//`
comment, which the language server accepts) marks it as generated: it's rewritten at every configure while that line
is there. Delete the line to keep your own edits; a `.luarc.json` without it is never touched, and configuring warns
with the path to add.

**The files.** Both are Lua files (stubs and `---@` comments) and deterministic: types sorted by name, members in
declaration order, so they can be committed and diffed.

| File | What |
|---|---|
| `imlui.lua` | The DSL (`---@meta imlui`, so `require "imlui"` is typed too): `state`, `watch`, `bind`, `component`, the theme, timers, `async`/`await`/`sleep`/`all`/`future`, `snackbar`, `log`, `ui.resource`, the misc functions, `native`, all also as `ui.*`; the classes `Element` (the common props), `ElementProps` (what every constructor takes: the common props and the children), `Binding`, `Future<T>`, the aliases `Color`, `SizeClass`, `Scheme`, `TextStyle`, and the Lua-side props of `Repeater`, `ListView`, `If`, `Loader`, `Responsive`. Hand-written, shipped with the library's files as `imlui:/definitions/imlui.lua` (`src/resources/definitions/imlui.lua`) and copied as it is. |
| `types.lua` | Generated from the `TypeRegistry`. Per element type `Gauge`: `GaugeProps: ElementProps` (each writable property as its value, a binding function or a `bind`; the `on*` handlers), `Gauge: Element` (the element table: properties, handlers, methods), the constructor `Gauge(props)` and `ui.Gauge`, and the alias `GaugeSignal` (its signal names). Per singleton `backend`: a class `backend` and the global. The named structs and enums the members use. `connect`, with an overload per type and the alias `ImluiSignal` of every signal name. |

```lua
-- types.lua (excerpt)
---A gauge with a needle.
---@class GaugeProps: ElementProps
---The needle's position, 0..`max`.
---@field value? number|(fun(self: Gauge): number?)|Binding
---Called when the needle reaches `max`.
---@field onPeak? (fun(self: Gauge, value: number))
---Called when `value` changes, with the new value.
---@field onValueChanged? (fun(self: Gauge, value: number))

---A gauge with a needle.
---@class Gauge: Element
---The needle's position, 0..`max`.
---@field value number
---Called when the needle reaches `max`.
---@field onPeak? (fun(self: Gauge, value: number))
---Called when `value` changes, with the new value.
---@field onValueChanged? (fun(self: Gauge, value: number))
---Sets both ends of the scale.
---
---`min` must be below `max`.
---@field setRange (fun(self: Gauge, min: number, max: number))

---A gauge with a needle.
---@param props? GaugeProps|number
---@return Gauge
function Gauge(props) end
ui.Gauge = Gauge
```

**Using them.** In VS Code, install the **Lua** extension (by sumneko, `sumneko.lua`). With `LUARC` (above) that's
all. Without it, generate the files and point the language server at the folder with a `.luarc.json` in the folder
the editor opens:

```json
{
    "runtime.version": "Lua 5.4",
    "workspace.library": ["../build/lua-definitions"]
}
```

`workspace.library` takes paths relative to the `.luarc.json`'s folder, or absolute ones. Other editors with an LSP
client use the same server and the same file.

**How C++ declarations appear.**

- **Properties**: their `LuaConvert` type (below). A read-only one is in the element class only, not in the props.
  An `std::optional<T>` is `T?`; a binding may always return `nil` (that resets the prop). A property that takes
  functions (`LuaFunction`) takes a function as its value, not as a binding.
- **Signals**: `on*` fields of both classes, and of the singleton's class. `type.signal("onPeak")` doesn't say its
  arguments, so the handler is `fun(self: Gauge, ...: any)`; declare them with
  `type.signal<double>("onPeak", {"value"})` (the types after `self`, and optionally their names) to get
  `fun(self: Gauge, value: number)`. They only document: `emit` isn't checked against them. The change signal of
  every property is `fun(self: Gauge, value: T)`. Singleton handlers have no `self`.
- **Methods**: `fun(self: Gauge, ...)` fields with the parameter and result types; a `Future<T>` result is
  `Future<T>`. Parameters are `arg1`, `arg2`, ... unless named:
  `type.method("setRange", &Gauge::setRange, {"min", "max"})` (one name per parameter; another count throws at
  registration). An `std::optional` parameter is optional (`max?: number`).
- **Constructors** take `<Name>Props`, or a value of the
  [default property](#nonvisual-defaultpropertyname-and-fillswidth) (`Gauge(0.5)`).
- **Docs** ([`doc(text)`](#docs-doctext)): the type's above `---@class <Name>Props`, `---@class <Name>` and the
  constructor; a member's as `---` lines above its `---@field` in each class it's in (the singleton's class too); the
  change signals get "Called when `name` changes, with the new value.". Each line of the text is a `---` line (a `\r`,
  trailing spaces and empty lines at the ends dropped, control characters made spaces); a line starting with `@` or
  `|` gets a `\` first, so it isn't read as an annotation. Handler and method types are in parentheses,
  `(fun(self: Gauge))`: LuaLS (3.19) drops the doc of the field after a line that ends in a bare `fun(...)`.
- **`connect`** takes the signal names of the registered types (`ImluiSignal`), so a misspelled one is flagged.

**Types of values.** Each `LuaConvert<T>` describes its values as a LuaLS type expression (`luaTypeOf<T>()`, module
`imlui.types.lua_convert`):

| C++ | LuaLS |
|---|---|
| `bool`, integers, floats, strings | `boolean`, `integer`, `number`, `string` |
| `imlui::Color` | `Color` (a role, `"role/alpha"`, hex or `{r, g, b, a}`) |
| `std::optional<T>` | `T?` |
| `std::vector<T>` | `T[]` |
| string-keyed maps | `table<string, T>` |
| `std::pair<A, B>`, `std::tuple<Ts...>` | `[A, B]` (a LuaLS tuple) |
| `std::variant<Ts...>` | `A\|B` (`nil` for `std::monostate`) |
| an enum with `EnumNames<E>` | its names, `"low"\|"high"`; with a `name` that is a Lua identifier, an `---@alias` of that name |
| a struct with `LuaFields<T>` | `{ x: number, y: number }`; with a `name` that is a Lua identifier, a `---@class` of that name with a field per `field()`, each optional (a missing one keeps its default) |
| `imlui::LuaFunction` | `function` |
| `imlui::Future<T>` | `Future<T>` (`Future<nil>` for `Future<void>`) |

Named structs and enums are declared once in `types.lua`, wherever they're used. A `LuaConvert` of your own may add
the hook `static std::string luaType()`, or `static std::string luaType(imlui::LuaTypes& types)` when it's built from
other types (`luaTypeOf<M>(types)`) or declares named ones (`if (types.claim(name)) types.define(name, "---@class
...\n");`, module `imlui.types.lua_types`):

```cpp
template <> struct imlui::LuaConvert<Vec2> {
    static constexpr std::string_view name = "{x, y}";
    static std::string luaType() { return "{ x: number, y: number }"; }
    // push, check, get as before
};
```

Without one, its `name` is used when it is a LuaLS built-in type (`number`, `string`, `table`, `function`, ...), and
`any` otherwise.

**What LuaLS isn't told.** Free names that are element ids (`nameField.text` at app level) aren't declared, so the
language server reports them as undefined globals: reach them through `ui.ids` (an `Ids`: `table<string, any>`, like a component's
and a delegate's `ids`, since an id can name any type), annotate a local (`---@type TextField` above `local nameField = ui.ids.nameField`), or disable `undefined-global`
for UI scripts. Props an element reads raw from its table (`Table`'s `rows`, `menu`, a `Canvas`'s painter; sizes but
the common `width`/`height`) aren't declared in C++, so they aren't in its classes (an unknown key in a constructor's
table isn't flagged either). Most built-in signals are declared without argument types yet (`...: any`; `Button`'s
`onClicked` is typed): [Elements](elements.md) lists their arguments. `connect`'s overloads match the target's type loosely, so a signal of
another type isn't always flagged. An element's class has what the C++ side holds (`nameField.text` is a `string`),
though a field a script set to a binding function holds that function. `state(init)` returns a
`table<string, any>` (a field initialized to `nil` would otherwise be typed `nil` for good), and `await(f)` unwraps a
`Future<T>` to `T` (anything else passes through as `any`).

## Threading and frame requests

Lua, the element tree, `notify` and `emit` are **main-thread only**. Three tools hand work between threads, from the
highest level down:

| Tool | For |
|---|---|
| `runAsync(work, done[, fail]) -> Task` (on every `ScriptObject`) | Work an element or singleton starts: `work` on the background pool, its result to `done` on the main thread, only while the object lives. The [`Task`](#cancelling-a-job-task) cancels it. |
| `runAsync(work) -> Future<R>`, `imlui::Promise<T>` (module `imlui.types.future`) | A result a method returns to Lua, which the script [awaits](lua-api.md#async), with optional [progress](#progress); a method can also take a Lua future as a `Future<T>` argument. |
| `imlui::Live<T>` (module `imlui.types.live`) | A property a worker updates at any rate (telemetry, progress, status text): the UI applies the latest value at most once per loop iteration. |
| `ScriptObject::Handle`, `imlui::post`, `runInBackground` | The lower level both are built on: your own threads, objects the host keeps alive. |

All three are in `import imlui`.

### Background work owned by an element

`runAsync` runs `work` on the [background pool](#the-background-pool) and `done(result)` on the main thread:

```cpp
class Fetch final : public imlui::Element {
public:
    static void declare(imlui::TypeBuilder<Fetch>& type) {
        type.property("url", &Fetch::url, &Fetch::setUrl).property("text", &Fetch::text).signal("onError").nonVisual();
    }
    void render(imlui::Frame&) override {}

    void setUrl(std::string url) {
        if (url == m_url) return;
        m_url = std::move(url);
        runAsync(
            [url = m_url](std::stop_token stop) { return download(url, stop); }, // a worker thread: copies only
            [this, url = m_url](std::string body) {                           // the main thread, while `this` lives
                if (url != m_url) return; // a newer request replaced this one
                m_text = std::move(body);
                notify("text");
            },
            [this](std::string error) { emit("onError", error); });          // optional: what `work` threw
    }
    // url(), text() ...

private:
    std::string m_url, m_text;
};
```

| Argument | Called | Rules |
|---|---|---|
| `work` | On a worker thread, as `work(std::stop_token)` or `work()`. Returns `R` or `void`. | Never touches the object: give it copies of its inputs (and [`Live` writers](#live-properties)). Poll the token: it stops when the object dies (its handles go dead) or the pool shuts down. Work whose token stopped before it started is skipped. |
| `done` | On the main thread, as `done(R&&)` (`done()` for `void`), after `work` returned, **only if the object still lives**. | May use `this`, `notify`, `emit`. Also runs after a pool shutdown stopped the token, if the object lives: `work` decides what it returns then. |
| `fail` (optional) | On the main thread, as `fail(std::string message)`, when `work` threw; only while the object lives. | The message is `what()`, or `unknown C++ exception`. Without `fail`, the exception is reported like a script error (the error overlay and the log): `runAsync (<type>): <what>`. |

`work`, `done`, `fail` and `R` may be move-only. They are destroyed on whichever thread drops them last (a worker,
or the main thread), so their captures must not need the main thread (no Lua references). An exception thrown by
`done` or `fail` is reported like any posted function's (`posted function: <what>`). Call `runAsync` on the main
thread (it takes `handle()`); after `cancelHandles()` it does nothing.

#### Cancelling a job: `Task`

`runAsync(work, done[, fail])` returns an `imlui::Task` (module `imlui.types.task`), which call sites may ignore:

```cpp
void Finder::setQuery(std::string query) {
    m_search.cancel(); // the previous search: its token stops, its `done` never runs
    m_search = runAsync([query](std::stop_token stop) { return search(query, stop); },
                        [this](std::vector<Hit> hits) { m_hits = std::move(hits); notify("hits"); });
}
imlui::Task m_search;
```

| `Task` member | What |
|---|---|
| `cancel()` | Ends a running task as cancelled: stops `work`'s token (work not started yet is skipped) and **`done`/`fail` are never called** (an exception isn't reported either). `false` once the task ended. |
| `running()` | True until the task ends: `done`/`fail` is about to run (it is already false inside them), it was cancelled, or the job was dropped (the object died, the pool shut down before it started). |
| `cancelled()` | True once `cancel()` ended it. |

Copies share one state; every member is thread-safe. The end is decided once, atomically, so `cancel()` returning
`true` guarantees that `done`/`fail` won't run, from any thread. A default-constructed `Task` isn't running.
(`make()`, `stopToken()` and `finish()` are for the code that runs the job.)

The object's death is the cancellation: every way an element dies goes through the registry's finalizer, which calls
`cancelHandles()` before `destroyed()` and the destructors (garbage collection, a rebuilt page, a hot reload or
shutdown closing the Lua state). A singleton isn't cancelled by a hot reload: its work keeps running into the next
script ([work a singleton started](#singletons)). A singleton the host destroys keeps its handles alive until
`ScriptObject`'s destructor, after your own: call `cancelHandles()` first in your destructor if its work must see the
stop while your members still live.

### Futures

`imlui::Future<T>` (module `imlui.types.future`) is a result that arrives later. A method returns it like any value;
the script gets a [Lua future](lua-api.md#async) to `await` or chain:

```cpp
struct Report { std::string title; int pages = 0; };
template <> struct imlui::LuaFields<Report> {
    static constexpr std::tuple fields{imlui::field("title", &Report::title), imlui::field("pages", &Report::pages)};
};

imlui::Future<Report> Backend::fetch(std::string url) {
    return runAsync([url](std::stop_token stop) { return download(url, stop); }); // a worker: copies only
}
type.method("fetch", &Backend::fetch);
```

```lua
local load = async(function()
    local ok, report = pcall(await, backend:fetch(url))
    s.text = ok and report.title or ("failed: " .. report)
end)
```

**`runAsync(work) -> Future<R>`** runs `work` like the [two-argument form](#background-work-owned-by-an-element)
(`work(std::stop_token, imlui::Progress)`, `work(std::stop_token)` or `work()`, on the pool; it may be move-only)
and returns a future of its result (`Future<void>` for `void` work):

- done with what `work` returned; failed with the message of what it threw (`what()`, or `unknown C++ exception`);
- `work`'s token also stops when the future is cancelled (`fut:cancel()` in Lua, `Future::cancel()`);
- **the object's death cancels the future** (on the main thread, at the next loop iteration), and a result that
  arrives after that is dropped: the script's `await` raises `"cancelled"` (when its state is still alive), and
  nothing the object would have been needed for runs. Work stopped before it started is skipped, its future
  cancelled.

**Promises.** A result that comes from elsewhere (a callback API, another thread, a dialog) goes through a promise:

```cpp
imlui::Future<int> Backend::ask(std::string question) {
    auto [promise, future] = imlui::makePromise<int>(handle().stopToken()); // owner token: optional
    m_api.ask(question, [promise](int answer) { promise.resolve(answer); }, // any thread
              [promise](std::string error) { promise.reject(std::move(error)); });
    return future;
}
```

| `Promise<T>` member | What |
|---|---|
| `resolve(value)` (`resolve()` for `void`), `reject(message)` | Thread-safe. Settles the future on the main thread (posted with `imlui::post`, so at the next loop iteration). The first call wins; later ones do nothing. |
| `stopToken()` | Thread-safe. Stopped when the future is cancelled (by the script, C++ or the owner). |
| `progress(value)`, `reporter()` | Thread-safe. [Progress](#progress) before the result; `reporter()` is an `imlui::Progress` for work that shouldn't hold the promise (it doesn't keep it alive). |
| destructor | When the last copy goes without settling, the future is **cancelled**: a dropped job never leaves a script waiting. |

`makePromise<T>(owner)` takes an optional `std::stop_token`: once it stops, the future is cancelled and a value that
arrives later is dropped. `runAsync` passes its object's `handle().stopToken()`.

| `Future<T>` member (main thread) | What |
|---|---|
| `status()`, `pending()` | `FutureStatus::Pending`, `Done`, `Failed` or `Cancelled`. A default-constructed future is cancelled. |
| `value()` | `const T*`: the value once done, else `nullptr` (not for `Future<void>`). |
| `error()` | The message once failed; `"cancelled"` once cancelled. |
| `cancel()` | Settles it as cancelled and stops the promise's token (an adopted Lua future is cancelled too). `false` once settled. |
| `onSettled(fn)` | Calls `fn(const Future&)` once settled (now if it is). An exception it throws is reported. |
| `progress()` | `std::optional<double>`: the last progress applied, `nullopt` before the first. |
| `onProgress(fn)` | Calls `fn(double)` at each progress applied from now until it settles. An exception it throws is reported. |

- **Conversion.** Pushing needs `LuaConvert<T>::push` (a C++-only future of any `T` works without it). The value is
  converted when the future settles, into the Lua state it was pushed to. Pushing the same future again into that
  state pushes the same Lua object; a future that settled (or got progress) before it was pushed arrives that way.
  Reading needs `LuaConvert<T>::check`/`get` (or `T = void`): see [futures as arguments](#lua-futures-as-arguments).
- **Lifetime.** The Lua side is linked through a [`LuaFunction`](#lua-functions-luafunction), so a future whose
  state closed (hot reload, shutdown) settles only on the C++ side: a safe no-op for Lua. The shared state is
  released on the main thread (a promise's destructor hands it there), so promises may die on any thread.
- **Errors.** A rejection reaches the script as the error of `await` (or `catch`). One nothing handles is reported
  in the error overlay at the next frame ([unhandled rejections](lua-api.md#async)); a C++ `onSettled` handler counts
  as handling it. C++ itself reports nothing.
- `Future`, `onSettled` and `cancel` are main-thread only; a worker gets a `Promise` (or `runAsync`'s token and
  `Progress`).

#### Progress

A future may report how far it got before it settles: a `double`, by convention 0..1 (not clamped; NaN is dropped).
Work passed to `runAsync` that takes a second parameter gets an `imlui::Progress` (module `imlui.types.progress`);
a promise reports with `promise.progress(v)`, or hands out `promise.reporter()`:

```cpp
imlui::Future<Report> Backend::fetch(std::string url) {
    return runAsync([url](std::stop_token stop, imlui::Progress progress) {
        return download(url, stop, [&](double done) { progress.report(done); }); // any thread, any rate
    });
}
```

Reports are coalesced like [`Live<T>`](#live-properties): the latest value wins, and the main thread applies at most
one per loop iteration (`imlui::post`) to `Future::progress()`, the `onProgress` handlers and the Lua future's
`f.progress` field (which requests a frame, so bindings update; `f:onProgress(fn)` in [Lua](lua-api.md#async)).
Reports made before `resolve`/`reject` apply before the settlement; reports after it (or after the future settled
another way) are dropped. `Progress` is cheap to copy, thread-safe, and an empty one drops every report.

#### Lua futures as arguments

A method may take an `imlui::Future<T>`: the script passes any Lua future.

```cpp
void Lab::after(imlui::Future<Report> f) { // lab:after(f), lab:after(async(fn)()), lab:after(backend:fetch(url))
    f.onSettled([this](const imlui::Future<Report>& done) { if (done.value()) show(*done.value()); });
}
```

- **A C++ future read back is itself.** The Lua future a `Future<T>` was pushed as reads back as that same
  `Future<T>` (same state, no extra hop), when `T` matches; and pushing a read future back gives the same Lua object.
- **Any other Lua future is adopted**: `future()`, an async function's, `sleep`, `all`, `next`/`catch`. The C++
  future settles when the Lua one does, synchronously inside Lua's settle: done with its first value converted by
  `LuaConvert<T>` (`nil` when there is none; `T = void` ignores values), failed with its error (as a string), cancelled
  when it is. A value that doesn't convert **fails** the C++ future with the conversion's mismatch, field path
  included: `bad future value (pages: integer expected, got string)`. Progress the Lua future reports
  reaches `progress()`/`onProgress`. Passing a future to C++ counts as handling its rejection (not reported).
- **Cancelling either side cancels the other**: they are one future. `Future::cancel()` cancels the Lua future (and
  what it waits on, per the [Lua rules](lua-api.md#async)); `f:cancel()` cancels the C++ one.
- **Lifetime.** While the Lua future is pending it keeps the C++ state alive (like a promise does), so `onSettled`
  handlers run even when the method kept no `Future`. If the Lua state closes first (hot reload, shutdown), the C++
  future is cancelled (at the next loop iteration).
- A non-future argument raises `bad argument #1 to 'lab:after' (future expected, got number)`.

### Live properties

`imlui::Live<T>` is a property value that any thread sets and the UI sees at most once per main-loop iteration,
latest value wins:

```cpp
class Backend : public imlui::ScriptObject {
public:
    static void declare(imlui::TypeBuilder<Backend>& t) { t.property("load", &Backend::load); }
    [[nodiscard]] double load() const { return m_load.get(); } // the last applied value

    void startSampling() {
        runAsync([load = m_load.writer()](std::stop_token stop) { // the writer, not `this`
            while (!stop.stop_requested()) load.set(sampleCpuLoad()); // any rate
        }, [] {});
    }

private:
    imlui::Live<double> m_load{*this, "load"}; // the owner and its declared property (and an initial value)
};
```

| `Live<T>` member | What |
|---|---|
| `Live(owner, "name", initial = T{})` | A member of `owner` (element or singleton), constructed in its constructor (main thread). |
| `set(v)` | Any thread, while the owner lives: stores `v` under a lock; if no apply is queued, posts one through the owner's handle. Sets on the main thread are deferred the same way. |
| `get()` | Main thread: the last applied value. The declared getter returns it. |
| `writer()` | A `Live<T>::Writer`: copyable, thread-safe `set(v)` that stays safe after the owner died (it does nothing then). Give workers this, not `this`. |

The apply runs on the main thread, only while the owner lives: it moves the latest value to `get()`'s side and calls
`owner.notify("name")`, which writes it back to Lua, emits `on<Name>Changed` and requests a frame. A thousand sets
between two loop iterations cost one apply and one `notify`.

### The background pool

`imlui::runInBackground(work)` (module `imlui.ui.background`) queues `std::function<void(std::stop_token)> work` from
any thread on a process-wide pool that starts lazily: threads are created as work comes in, up to
`backgroundThreadLimit()` (`hardware_concurrency() - 1`, clamped to [2, 8]). Tasks start in the order they were
queued. One that throws is reported on the main thread (`background task: <what>`).

`Engine::run()` shuts the pool down when it returns (`shutdownBackground()`, after the Lua state closed and the window
went):

1. Work queued from then on starts a new pool.
2. Every task's stop token is stopped (stop callbacks run on the shutting-down thread).
3. Queued tasks are dropped: destroyed without running.
4. If every running task returns within 1 s (`backgroundShutdownWait`; `shutdownBackground(wait)` takes another),
   the workers are joined. Otherwise they are detached: each ends after its current task, keeping the pool's state
   alive itself.

So a task should poll its token and return soon after it stops; one that doesn't delays the exit by at most the
wait. A pool never shut down (no `Engine::run()`, or work queued after it returned) is not joined: the OS ends its
threads at process exit. The pool's and the main-thread queue's state are never destroyed, so a worker that outlives
`main()` can't meet them half-destroyed; what its own captures use is up to it.

### Lower level: handles and posts

`imlui::post(fn)` (module `imlui.ui.main_thread`) queues a function on the main thread from any thread:

```cpp
imlui::post([&backend, result] { backend.setResult(result); }); // thread-safe; wakes the loop
```

`engine.post(fn)` is the same call for code that has the `Engine`. The function runs at the next loop iteration,
outside any frame, in the order things were posted. It may call `notify` or `emit`, which request a frame. The
loop is woken through `glfwPostEmptyEvent`, so this works while the app is idle and sleeping. A posted function
that throws is reported like a script error (the error overlay and the log, `posted function: <what>`), and the
functions after it still run. `notify` and `emit` run protected, so a Lua error they meet (a script's `__newindex` on
the element table, out of memory) is reported the same way instead of aborting the process. Functions posted before
`run()` wait and run at its first loop iteration. Functions still queued when `run()` returns are dropped (destroyed
on the main thread without running).

This is fine for objects the host owns and keeps alive (the `Engine`, a singleton held by a `shared_ptr`). It is
**not** enough for an element: Lua owns it and deletes it when it collects the element table, when a page is
rebuilt, and when a hot reload closes the Lua state, possibly while a worker is still busy. A worker that posts
`[this] { notify("value"); }` would then touch a deleted object. For your own threads, take a **handle** on the main
thread and give the worker only the handle and copies of its inputs (what `runAsync` does for you):

```cpp
std::thread{[handle = handle(), this, url = m_url] {
    std::string body = download(url, handle.stopToken()); // slow; stops when the element dies
    handle.post([this, body = std::move(body)] {           // main thread, and only while `this` lives
        m_text = body;
        notify("text");
    });
}}.detach();
```

`ScriptObject::handle()` (main thread) returns a `ScriptObject::Handle`, a weak, copyable, thread-safe token. Elements
and singletons both have it:

| `Handle` member | What |
|---|---|
| `alive()` | True until the object is destroyed (or `cancelHandles()` ran). Thread-safe; a worker polls it as its "cancelled" flag. |
| `stopToken()` | A `std::stop_token` stopped when `alive()` turns false (already stopped for an empty handle). Stop callbacks on it run on the main thread, inside `cancelHandles()` or the destructor. |
| `post(fn)` | Queues `fn` like `imlui::post`. On the main thread, `fn` runs **only if the object is still alive** (otherwise it is destroyed without running), so it may use the object's `this`. Returns false, dropping `fn`, when the object is already gone. |

`cancelHandles()` or, at the latest, `ScriptObject`'s destructor stops the handles on the main thread, where posted
functions run too, so the object can't die between the check and `fn`. Rules for your own threads:

- **Don't join slow work in the destructor** (or in `destroyed()`): that freezes the UI, a reload or the app's exit
  until the work finishes. Detach the thread (or let a shared object own it) and let it notice the cancellation
  through `handle.alive()` / `handle.stopToken()`, or a flag of your own that your destructor sets.
- The worker never touches the object or its members itself, only the copies it was given. Everything that reads
  or writes the object goes into `handle.post`.
- A detached worker may outlive the element, the `Engine`, even `main()`'s return: what it uses must stay alive
  through its own captures, and the work should be short or cancellable.

### Frame requests

Frames are rendered on demand. `notify` and `emit` request one, and so do input and animations. An element whose
look changes over time by itself (a clock, an indeterminate animation, a timer) must ask for the next frame from
`render()`: `frame.requestFrame()` for "as soon as possible", or `frame.requestFrameAfter(seconds)` for the next
due time. The built-in `Timer` does the latter.

## Ticking elements

`render()` only runs while the element is rendered. An element that must act on its own, also when its page isn't
shown (a timer, a poller), turns on **ticking** with `setTicking(true)` (protected; typically in the constructor or
in a setter). While it's on, `tick(now)` runs at the start of every frame, before the tree renders, whether the
element is rendered or not, with its properties synced first. `now` is the app clock's time in seconds
(`clockTime()` from `imlui.ui.clock`, what `ui.time()` returns). Turning ticking on requests a frame, so the first
`tick()` comes on the next frame; after that, frames are rendered on demand, so ask for the frame you need with
`requestFrameAfter(seconds)` (module `imlui.ui.frame_request`). `setTicking(false)` stops it, and so does the
destructor.

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
away. An element dropped some other way (a table you edit yourself) stops at the next garbage collection. An `id`
doesn't keep a dropped element alive ([ids are weak](lua-api.md#element-ids)). Give the element a `running`
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
| `imlui.types.lua_convert` | `LuaConvert<T>` and its built-in specializations (scalars, strings, `Color`, `optional`, `vector`, string-keyed maps, `pair`/`tuple`, `variant`/`monostate`); `EnumNames<E>`, `LuaFields<T>` with `field(key, &T::member)`; the `LuaPushable` / `LuaReadable` concepts, `takesLuaFunction<T>()` |
| `imlui.types.lua_function` | `LuaFunction`: a handle to a Lua function (`call(args...)`, `call<R>(args...)`, `expired()`, `push`), expired once its state closes; its `LuaConvert` |
| `imlui.types.progress` | `Progress`: `report(double)`; thread-safe, copyable |
| `imlui.types.future` | `Future<T>` (`status`, `value`, `error`, `cancel`, `onSettled`, `progress`, `onProgress`; `LuaConvert`: pushes a Lua future, reads one back or adopts it), `Promise<T>` (`resolve`, `reject`, `stopToken`, `progress`, `reporter`; thread-safe), `makePromise<T>(owner)`, `FutureStatus` |
| `imlui.types.task` | `Task`: `cancel`, `running`, `cancelled` (thread-safe); what `runAsync(work, done[, fail])` returns |
| `imlui.types.script_object` | `ScriptObject`: `notify`, `emit`, `runAsync(work, done[, fail]) -> Task`, `runAsync(work) -> Future<R>`, `handle()` (`ScriptObject::Handle`: `alive()`, `stopToken()`, `post(fn)`, for worker threads), `cancelHandles()`, `scriptReloading()` (virtual, singletons' reload hook), the Lua link (`luaState`, `pushSelf`, `attach`/`relink`/`detach`, `noteConnection`, used by the registry) |
| `imlui.types.live` | `Live<T>`: a property any thread `set()`s, applied on the main thread at most once per loop iteration (latest wins); `Live<T>::Writer` |
| `imlui.ui.background` | `runInBackground(work)`: the process-wide worker pool; `shutdownBackground(wait)` (the engine calls it), `backgroundThreadLimit`, `backgroundTaskCount` |
| `imlui.ui.main_thread` | `post(fn)`: hand work to the main thread from any thread; `runPosted`, `clearPosted`, `postedCount`, `setPostWakeHandler` (used by the engine) |
| `imlui.types.frame` | `Frame`: the render-time context; `Frame::Child`: a child handle |
| `imlui.types.element` | `Element`: `render`, `intrinsicSize`, `contentFillsWidth`, `componentComplete`, `destroyed`, `tick` / `setTicking` |
| `imlui.types.type_info` | `TypeInfo`: the type-erased member table a `TypeBuilder` fills in, with the members' Lua types |
| `imlui.types.type_builder` | `TypeBuilder<T>`: `property` (with an optional default), `signal` (optionally typed: `signal<Args...>(name, argNames)`), `method` (optionally with parameter names), `nonVisual`, `defaultProperty`, `fillsWidth` / `fillsWidthWhen`, `extends` |
| `imlui.types.lua_types` | `LuaTypes`: the named types (`claim`, `define`) the Lua definitions declare, and helpers for type expressions (`fromName`, `grouped`, `optional`, `required`, `isIdentifier`, `quoted`) |
| `imlui.types.lua_definitions` | `luaTypeDefinitions(registry)`, `writeLuaDefinitions(registry, dir, prelude)`, `luaPreludeDefinitions`: the LuaLS definitions |
| `imlui.ui.layout_box` | Layout boxes (`imlui::layout`): `BoxScope`, `boxTo`, `regionBox`, `shrinkToFitBox`, `insetBox`, `pushBox`/`popBox`, `availableWidth`, `claimWidth`: the space a container gives its children |
| `imlui.ui.layout_cache` | The layout cache (`imlui::layout`): `lastSize`, `assumeWidth`/`assumeHeight`, `recordSize`: last frame's sizes by ImGui ID and convergence (another frame when a layout placed something by a stale size) |
| `imlui.types.overload` | `overload<Args...>(&T::fn)`, `constOverload<Args...>(&T::fn)`: pick one of overloaded members |
| `imlui.types.registry` | `TypeRegistry`: `registerType<T>`, `registerSingleton<T>` (reference or `shared_ptr`), `install`, `activate`, `sync`, `typeNames`, `singletonTypes`; it is the `Renderer::Types` the renderer dispatches every element to |
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
