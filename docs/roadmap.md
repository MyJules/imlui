# Roadmap

Work planned or in progress, in order. Each step lands as its own commit on `material-ui`; [changelog.md](changelog.md)
records what landed.

## 1. Typed built-ins: preparation *(done)*

- Rename `imlui::Object` → `imlui::ScriptObject` (module `imlui.types.script_object`).
- Extend `Frame` so containers, windows and menus can be written as typed `Element`s: child handles, stacking,
  menus, window title, node-valued props, navigation-style context, child property access.
- Per-group `register<Group>Types(TypeRegistry&)` functions (folded into `registerBuiltinTypes` in step 4), so the
  porting work could run in parallel without shared files.
- Type aliases (Column/Item, Text/Label, ...) and subclass `declare` chaining (TextArea extends TextField).
- Baseline screenshots of every gallery page (desktop and phone size) plus `regress.ps1` for visual regression checks.

## 2a. Examples reorganization *(done)*

Each example is one self-contained folder holding all of its code:

```
examples/
  gallery/        main.lua, StatCard.lua                 (runs on the generic imlui_app host)
  counter/        main.lua
  custom_types/   CMakeLists.txt, main.cpp, gauge.cppm, system_info.cppm, main.lua
```

Examples with C++ have their own `CMakeLists.txt` (`add_subdirectory`) and executable. The default script, docs
and helper scripts follow the new paths.

## 2b. Type-system limitations *(done)*

| Limitation | Fix |
|---|---|
| A property that becomes `nil` keeps its last value | Reset to the declared default (captured from the getter at construction, or `.defaultValue(v)`) |
| Elements that are not rendered are not synced | Sync lazily before method calls, child access and id lookups |
| Singletons must outlive `Engine::run()` | `registerSingleton(std::shared_ptr<T>)` overload that keeps the object alive |
| Overloaded methods need a `static_cast` | `imlui::overload<Args...>(&T::fn)` helper |
| `render()` / `componentComplete()` untested | Headless ImGui frame harness for element tests (no window/GPU) |
| An element id like `io` is silently shadowed by a Lua global | Report an error when an id collides with a global or a `ui` name |
| Escape-to-close untested; no key injection | `native.injectKey(...)`; tests for drawer and dialog Escape handling |
| "Phone preview" resize can push the window off-screen | Clamp the window into the monitor work area after `setWindowSize` |

## 3. Port all built-in elements to typed `Element`s *(done)*

Five groups in parallel, one class per module; the old `Renderer::registerElement` lambdas were removed per group.

| Group | Elements | Limitations fixed on the way |
|---|---|---|
| Windows, system, inline containers | ApplicationWindow, Window, Dialog, Snackbar, ImGuiDemo, Repeater, If, Loader, Responsive | Elements can run independently of rendering: `Element::tick` / `setTicking`, so a `Timer` ticks even when its page is hidden |
| Layout | Column/Item, Row, Grid, ScrollView/ListView, Card, TabBar, Tab, Section/Expander, TreeNode | ScrollView keeps its scroll position across size-class switches (when it moves) |
| Navigation, content | TopAppBar, NavigationItem, NavigationBar, NavigationRail, ModalNavigationDrawer, MenuBar, Menu, MenuItem, Text/Label, Icon, Link, Separator, Divider, Spacer | NavigationRail scrolls when the window is short; drawer `open` works as a plain value, a `bind` and a function binding |
| Controls | Button, IconButton, Checkbox, Switch, RadioButton, RadioGroup, Chip, SegmentedButton, ListItem | — |
| Inputs, data | Slider, SpinBox, TextField, TextArea, ComboBox, ColorPicker, CircularProgress, BusyIndicator, Plot, Table, Canvas | Canvas painter draws with the crisp shape helpers; whole numbers reach Lua as integers |

## 4. Cleanup *(done)*

- The lambda path is removed (`Renderer::registerElement`, `ElementFn`, `Engine::renderer()`, the group
  registration modules): the Renderer renders every element through the `TypeRegistry` (`Renderer::Types`), and
  `registerBuiltinTypes` registers every built-in.
- Consistency pass over the element modules: one doc-comment format (props, defaults, raw props, signals,
  two-way), uniform headers, shared test helpers (`test::LastItemProbe`, `test::RenderCounter`,
  `HeadlessFrame::click`, fonts and icons loaded by the harness). Fixed on the way: `TextField` / `ColorPicker`
  called `onTextChanged` / `onColorChanged` twice per edit.
- clang-tidy covers every module: the `tidy` target (the reported crash doesn't reproduce with current BMIs).
- Full visual regression against the step-1 baseline (all pages, desktop and phone): no change above noise.
- Docs: [Elements](elements.md) rewritten from the classes, extending guide (ticking, the built-ins as examples),
  architecture, development.

## Next

Follow-ups noticed during the cleanup, not scheduled yet:

- **Check the element reference automatically.** A test that walks `TypeRegistry` (declared properties, signals,
  default property, non-visual flag) and checks that [Elements](elements.md) and each class's doc comment list
  them, so the reference can't drift.
- **Ticking elements outlive their use.** A `Timer` ticks until Lua collects its table, so one dropped from the
  tree (a rebuilt `Repeater` delegate, a reloaded `Loader` item) keeps firing until the next GC. Stop ticking
  when an element hasn't been reachable from the root or a live reference for a while, or tie ticking to
  attachment to the tree.
- **`id` on the Lua-side containers.** The `Repeater`, `ListView`, `If`, `Loader` and `Responsive` constructors
  don't register `id`, so `ListView { id = "list" }` can't be referenced by name.
- **Slider precision.** `Slider` goes through `float`, so a fractional value is written back with float noise
  (0.1 becomes 0.10000000149...), and `onReleased` also fires after a click that didn't change the value.
- **Size rules in one place.** `Column` and `Canvas` compute fill-minus widths as `avail + w + 1`, one pixel off
  from the other elements, and `Spacer` clamps negative sizes to 1px (fill values do nothing). Route them through
  one helper.
- **Run `tidy` (and the sanitizer preset) in CI**, and extend the naming check to `tests/`.

## Kept as designed

- `onFrame` callbacks run only on rendered frames (rendering is on demand); use `ui.requestFrame()`, a timer or
  `Canvas { animated = true }` for self-changing state.
- Text-caret blink is driven by 100 ms frame requests (approximate).
- Monitor DPI changes are handled but can't be verified on a single-monitor 100 % setup.
- Injected mouse positions are overridden by the real cursor while the window is focused and the cursor is
  outside it (test tooling only; `native.injectKey` covers most automated checks).
