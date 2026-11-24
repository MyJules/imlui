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
and helper scripts follow the new paths. (Since moved out: see [6. Library repository](#6-library-repository-done).)

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

## 5. Resource system *(done)*

Ship an app as one executable with its Lua scripts, fonts, images and data. Decisions:

- **Names:** each prefix is a namespace: `app:/main.lua` for the app, `imlui:/imlui.lua` and
  `imlui:/fonts/...` for the library. Libraries and plugins get their own prefix. Paths are normalized, and `..`
  can't leave a mount.
- **`imlui.resources` (`Resources`):** prefixes map to stacked mounts, and the last mount wins.
  - The API is `read` (a zero-copy span when the file is stored uncompressed), `exists`, `list` and `modified`.
  - **Directory mount:** dev builds and loose overrides. Reports file times for hot reload.
  - **Embedded mount:** data compiled into the exe.
  - **`.pak` mount:** an archive next to the exe.
- **Embedding:** `imlui_add_resources(target resources.lua [NAME name])` generates a module per manifest that uses
  `#embed`. The `-Wpedantic` extension warning is turned off only inside that generated file. What a package holds
  is a Lua resource manifest (like Qt's `.qrc`): groups with a prefix, a mount folder, a base folder, files,
  include/exclude globs and per-file compression overrides.
- **Compression:** zstd, vendored. Compression is per file and size-based: a file is stored compressed only when
  that saves enough (fonts and text, yes; already-compressed data, no). Files are decompressed on first read and
  cached.
- **`.pak` format:** imlui's own simple format: a header, an index, then per-file blobs, using the same layout as
  embedded data. A small `imlui_pack` tool builds it from CMake.
- **Release default:** with `IMLUI_DEV_PATHS=OFF`, everything is embedded, so the exe is self-contained. A
  directory or `.pak` next to the exe can still override files. Dev builds mount each embedded block's source folder
  (baked into its `ResourcePackage` by `imlui_add_resources`) over the embedded data, so names resolve the same way
  and hot reload keeps working. Hosts name no file paths: the app is `app:/main.lua`.
- **Lua:**
  - A `package.searchers` entry finds modules through `Resources`.
  - `ui.__load` and component loading go through it, with chunk names like `app:/main.lua`.
  - New calls: `ui.resource.read`, `ui.resource.exists` and `ui.resource.url`.
- **Fonts:** the library embeds Roboto, Material Icons and the codepoints, and loads them with
  `AddFontFromMemoryTTF`. Apps can declare their own fonts from Lua (`theme { fonts = { ... } }`).
- **Images:** an `Image { source = "app:/logo.png" }` element for PNG and JPG, decoded with stb_image (vendored)
  and uploaded as textures through ImGui's texture list (the OpenGL backend creates them). Each image is decoded
  once and cached per resource name. It is drawn at its natural size × the DPI scale unless `width`/`height` are
  set, and stays sharp only up to its pixel size.

Order, one commit per step:

1. *(done)* `Resources` with directory mounts, and the engine and Lua switched to it (no visible change).
2. *(done)* Embedded mounts, `imlui_add_resources` and zstd. The library embeds its own Lua and fonts.
3. *(done)* Self-contained examples, and a test that runs a release build without the source tree.
4. *(done)* `.pak` mounts and the `imlui_pack` tool.
5. *(done)* Fonts declared from Lua: `theme { fonts = { regular, medium, fallback, <name> } }`, a `font` prop on
   `Text`/`Label`/`Button`, `p:text(..., font)`, `textSize(..., font)`.
6. *(done)* The PNG/JPG `Image` element.
7. *(done)* Path-free hosts: `defaultConfig(argc, argv, {packages...})` with `ResourcePackage`s that carry their dev
   source folder; `<exe folder>/<prefix>/` override folders; no `IMLUI_SOURCE_DIR`.
8. *(done)* Resource manifests: `resources.lua` files (one group or a list; `prefix`, `mount`, `base`, `files`,
   `include`/`exclude` globs, `compress`) evaluated by the tools in a Lua sandbox, replacing the arguments of
   `imlui_add_resources(<target> <manifest> [NAME])` and `imlui_add_pak(<name> <manifest> [OUTPUT])`. Generated
   modules export `packages()`, one `ResourcePackage` per group. Dependencies through a Ninja dependency file plus a
   file list compared on every build; no configure-time globbing. A `.pak` of several groups needs one prefix and is
   mounted at their common folder.

## 6. Library repository *(done)*

imlui is only the library now (library, its resources, the tools, tests, docs, CMake helpers), consumable as a
subproject (`add_subdirectory(vendor/imlui)`). There is no generic runner (`imlui_app`) and no `examples/`: the one
example app is the separate repository imlui-gallery (the gallery plus the C++ types page), which vendors imlui as
a git submodule. The library's smoke and `.pak` tests use their own fixtures (`tests/smoke_host/`,
`tests/data/smoke_app/`).

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
