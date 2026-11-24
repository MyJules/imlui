# Roadmap

Work planned or in progress, in order. Each step lands as its own commit on `material-ui`; [changelog.md](changelog.md)
records what landed.

## 1. Typed built-ins: preparation *(in progress)*

- Rename `imlui::Object` → `imlui::ScriptObject` (module `imlui.types.script_object`).
- Extend `Frame` so containers, windows and menus can be written as typed `Element`s: child handles, stacking,
  menus, window title, node-valued props, navigation-style context, child property access.
- Per-group `register<Group>Types(TypeRegistry&)` functions, so the porting work can run in parallel without
  shared files.
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

## 3. Port all built-in elements to typed `Element`s

Five groups in parallel, one class per module; the old `Renderer::registerElement` lambdas are removed per group.

| Group | Elements | Limitations fixed on the way |
|---|---|---|
| Windows, system, inline containers | ApplicationWindow, Window, Dialog, Snackbar, ImGuiDemo, Repeater, If, Loader | `Timer`-style scheduling for elements independent of rendering |
| Layout | Column/Item, Row, Grid, ScrollView/ListView, Card, TabBar, Tab, Section/Expander, TreeNode | ScrollView keeps its scroll position across size-class switches |
| Navigation, content | TopAppBar, NavigationItem, NavigationBar, NavigationRail, ModalNavigationDrawer, MenuBar, Menu, MenuItem, Text/Label, Icon, Link, Separator, Divider, Spacer | NavigationRail scrolls when the window is short; drawer `open` works as a plain value |
| Controls | Button, IconButton, Checkbox, Switch, RadioButton, RadioGroup, Chip, SegmentedButton, ListItem | — |
| Inputs, data | Slider, SpinBox, TextField, TextArea, ComboBox, ColorPicker, CircularProgress, BusyIndicator, Plot, Table, Canvas | Canvas painter draws with the crisp shape helpers; `Timer` ticks even when its page is hidden |

## 4. Cleanup

- Remove the lambda path (`Renderer::registerElement`); every element is a registered type.
- Full visual regression against the step-1 baseline (all pages, desktop and phone).
- Docs: element reference generated from / checked against the type declarations, extending guide, architecture.

## Kept as designed

- `onFrame` callbacks run only on rendered frames (rendering is on demand); use `ui.requestFrame()`, a timer or
  `Canvas { animated = true }` for self-changing state.
- Text-caret blink is driven by 100 ms frame requests (approximate).
- Monitor DPI changes are handled but can't be verified on a single-monitor 100 % setup.
- Injected mouse positions are overridden by the real cursor while the window is focused and the cursor is
  outside it (test tooling only; `native.injectKey` covers most automated checks).
