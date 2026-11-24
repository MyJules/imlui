# Development

## Code style

The full rules are in [`CODE_STYLE.md`](../CODE_STYLE.md). `.clang-format` and `.clang-tidy` enforce most of
them. In short:

- **C++20 modules only.** `#include` appears only in a module's global module fragment, and only for third-party
  or standard headers. `app/main.cpp` uses `import` only. Test files in `tests/` are the one exception: they
  include `<gtest/gtest.h>`, because the GoogleTest macros can't cross a module boundary, and nothing else.
- **Third-party code goes through wrappers** in `src/wrap/` (`lua`, `imgui`, `imlui.log`,
  `imlui.platform.window`).
- **One thing per module**: one class with its nested types, or one family of free functions with their
  option/result structs. Umbrella modules (`imlui`, `imlui.widgets`, `imlui.ui.elements`) only `export import`.
- **Naming**: `PascalCase` types and enumerators, `camelCase` functions, variables and constants, `m_` +
  `camelCase` for private data members (the only prefix), `lower_case` namespaces, dotted lowercase module names,
  `snake_case.cppm` files named after the module's last segment. Name `lua::State*` parameters `state`, not `L`.
- **Formatting**: 4-space indent, 120 columns, braces on the same line, `T* p`. Comments explain *why*.

## Module conventions

Each module is one file. The interface comes first and holds declarations only. Everything else goes after
`module :private;`:

```cpp
module;
#include <string>                 // global module fragment: std / third-party only

export module imlui.example;

import imlui.log;

export namespace imlui {
class Example {
public:
    explicit Example(std::string name);
    [[nodiscard]] const std::string& name() const;
private:
    std::string m_name;
};
} // namespace imlui

module :private;                  // bodies, file-local state, helpers

namespace imlui {
Example::Example(std::string name) : m_name{std::move(name)} {}
const std::string& Example::name() const { return m_name; }
} // namespace imlui
```

- Function bodies, static/global state and non-exported helpers (in an anonymous namespace) go in the private
  fragment. Templates, `constexpr` functions and `= default`/`= delete` members are the exceptions and stay in
  the interface.
- **No module re-exports another** (umbrellas aside). Import the module that defines each type you name, for
  example `imlui.theme.scheme` for `Scheme` or `imlui.color` for `Color`.
- Namespaces: `imlui` (core), `imlui::md` (widgets), `imlui::luaui` (Lua bridge), `imlui::log`,
  `imlui::platform`, `lua`.
- CMake picks up `src/**/*.cppm` with a `CONFIGURE_DEPENDS` glob. New module files need no CMake edits, but you
  do need to re-run the build (which re-checks the glob).

See [Architecture](architecture.md#module-map-target-layout) for the module map.

## Adding an element

Every element is a **typed element**: an `Element` class whose `declare(TypeBuilder<T>&)` lists its properties,
signals and methods, registered under a Lua name. It has per-element C++ state, declared properties synced into
setters, `notify`/`emit`, and methods callable from Lua. [Registering C++ types](extending.md) is the full guide;
the built-ins in `src/imlui/ui/elements/` are the examples (see
[the built-ins as examples](extending.md#the-built-ins-as-examples)).

**1. Widget (C++, `imlui::md`).** Add a Material widget in `src/imlui/widgets/<name>.cppm`
(`export module imlui.widgets.<name>;`) and add it to the `imlui.widgets` umbrella. Draw it with ImGui primitives
using `imlui.widgets.common`:

- `InvisibleButton` for interaction.
- `scheme()` and `shape()` for colors and radii.
- `stateLayer(id, hovered, active)` for the hover and press overlay.
- `itemDisabled()` with `disabledContent()`/`disabledContainer()` for the disabled state.
- `dp()` for every size.
- `animate(id, target)` for transitions. It requests frames while the value moves. Anything else that changes on
  its own over time must call `requestFrame()` from `imlui.ui.frame_request` (frames are rendered on demand).
- `itemWidth(requested, natural)` so `width`/`fillWidth` behave like the other widgets.
- The shape helpers for anything rounded or outlined: `fillRoundRect`/`strokeRoundRect` (with optional
  `ImDrawFlags_RoundCorners*`), `fillCircle`/`strokeCircle`, `strokeArc`, and `hLine`/`vLine` for dividers. Don't
  use `ImDrawList::AddRect`/`AddRectFilled` with a rounding, or `AddCircle*`: ImGui shrinks the radius (a pill gets
  flat ends), uses few segments at small radii, and draws 1px strokes at fractional positions, so they look soft.
  The helpers use the true radius (clamped to half the side, so `radius = h / 2` is an exact pill), tessellate by
  arc length, snap to whole pixels, and keep strokes whole-pixel wide and inside the shape.
- `drawTextV(style, x, cy, ...)`, or `textTop(style, cy)` for the y of `drawText`, to center a label vertically.
  It centers the cap height on a whole pixel, so labels line up the same way in every widget.

Return what happened (`bool` changed/clicked, or a result struct).

**2. Element (C++).** One class per module, `src/imlui/ui/elements/<snake_name>.cppm`
(`export module imlui.ui.elements.<snake_name>;`), added to the `imlui.ui.elements` umbrella (`elements.cppm`) and
registered in `registerBuiltinTypes` (`builtin.cppm`). See [Writing a built-in element](#writing-a-built-in-element)
for the conventions.

```cpp
// Example: a hypothetical "Rating" element backed by a new md::rating() widget.

/// A row of stars.
///
/// Props:   value = 0, count = 5
/// Signals: onChanged(self, value)
/// Two-way: value
class Rating final : public Element {
public:
    static void declare(TypeBuilder<Rating>& type);
    void render(Frame& frame) override;
    // value()/setValue(int), count()/setCount(int) ...
private:
    int m_value = 0;
    int m_count = 5;
};

module :private;

void Rating::declare(TypeBuilder<Rating>& type) {
    type.property("value", &Rating::value, &Rating::setValue)
        .property("count", &Rating::count, &Rating::setCount)
        .signal("onChanged");
}

void Rating::render(Frame&) {
    if (md::rating("##rating", &m_value, m_count)) {
        notify("value"); // two-way write-back (honours bind/binding), then onValueChanged
        emit("onChanged", m_value);
    }
}

// builtin.cppm
types.registerType<Rating>("Rating");
```

- Declared properties arrive through the setters before each `render()`, only when they changed.
- Use `frame.size("width")` for sizes (it handles `fillWidth` and fill-minus), `frame.dp()` and
  `frame.drawList()`.
- After changing a property in C++, call `notify(name)` **before** `emit`, so the handler sees the new state.
- Containers call `frame.renderChildren()` or `frame.renderStack()`, or `frame.forEachChild(f)` with
  `Frame::Child` handles (`child.render()`, `child.lastWidth()`, `child.as<T>()`) for custom layout.
  `frame.attachMenu(clicked)` supports a `menu` prop. `frame.node()` reaches raw Lua. See
  [Registering C++ types](extending.md#element-types) for the whole `Frame`.
- `type.nonVisual()` for elements that take no layout space (windows, timers); `setTicking(true)` for elements
  that act while they aren't rendered ([ticking elements](extending.md#ticking-elements)).

**3. Lua side.** Add the name to the `BUILTIN` list in `lua/imlui.lua`. Any Capitalized name already works as a
constructor, but a `BUILTIN` entry makes it a real `ui.*` field, so an element id or a global with the same name
can't shadow it. The type registry is installed before the DSL loads, so `BUILTIN` constructors of typed
elements create their C++ instances too.

**4. Docs and gallery.** Add the element to [Elements](elements.md), the README element table, and a demo in
`examples/gallery/main.lua`.

**5. Tests.** A test file `tests/elements_<family>_test.cpp` renders it through the
[headless frame harness](#the-headless-frame-harness): defaults, props reaching the instance, `nil` resets, two-way
write-back and signal arguments (with `h.click(...)` or `native.injectKey`).

Apps add elements the same way without touching imlui: `engine.registerType<MyThing>("MyThing")` before
`engine.run()`. See [Registering C++ types](extending.md).

## Writing a built-in element

The built-ins were ported from handler functions to typed elements in five groups at once; these are the
conventions they share, and new built-ins follow them:

1. **One module per element**, `src/imlui/ui/elements/<snake_name>.cppm`, one class. The file starts with a
   one-line `// Name: what it is, as a typed element.` comment. Keep `declare()` and every body in the private
   module fragment.
2. **The class doc comment** lists the Lua interface in one format: a summary, then `Props:` (declared, with
   defaults; `unset` for an empty `std::optional`; `(positional)` for the default property), `Raw:` (props read
   from the element table in `render()`: sizes, element-valued props, presence checks), `Signals:` with their
   arguments (`self` first) and `Two-way:`, each line left out when empty, then notes. [Elements](elements.md)
   is the reference built from these; keep both in step.
3. **Register it** in `registerBuiltinTypes` (`builtin.cppm`) under its family comment, aliases on the next line
   (`registerType<Column>("Column")`, then `("Item")`: Lua sees separate types).
4. **Two-way props**: set the member, `notify("prop")`, then `emit(...)`. `notify` writes the value back and emits
   `on<Prop>Changed`; don't also `emit` a declared signal of that same name (`onTextChanged` for `text`), or the
   handler runs twice.
5. **Props that are not declared** are readable through `frame.node()` (`node().has("checked")` for "is it
   checkable", a `__children` function, the `rows` table, a painter callback). Declared `std::optional<T>`
   properties distinguish "unset" from a value. Props that only the renderer reads (`visible`, `enabled`,
   `tooltip`, `id`) need nothing.
6. **Subclasses**: `TextArea::declare(TypeBuilder<TextArea>& type)` calls `type.extends<TextField>()`, then adds
   its own members. A subclass without its own `declare` doesn't compile.
7. **Shared state between elements** goes through a small module of its own, like `imlui.ui.navigation_scope`
   (`NavigationScope`: the navigation containers tell `NavigationItem` which style to draw in).
8. **Test it** with the harness (`tests/elements_<family>_test.cpp`) and **compare the gallery** with the
   screenshots from before the change, every page and a phone-sized window. Animated content (progress, canvas,
   chart) and timer texts differ by nature.

## Adding a native function

`imlui.native` is the C++ library behind the Lua API. In the target layout it lives in `imlui.lua.native`
(`luaui::registerNative(lua::State*, NativeHooks*)`). App actions (`quit`, `reload`, `setTitle`) go through
`NativeHooks` callbacks, so the module doesn't depend on `Engine`. On `material-ui` it's still
`Engine::registerNative` in `engine.cppm`.

1. Add an entry `{"name", [](lua::State* state) -> int { ...; return nResults; }}` to the function table. Use
   `lua::error(state, "...")` for bad arguments. If the function needs the app, add a hook to `NativeHooks`.
2. Expose it from the prelude if scripts should call it directly: `ui.name = native.name` in `lua/imlui.lua`.
   Scripts can always reach it as `native.name`.
3. Document it in [Lua API](lua-api.md#native).

## Logging

- **C++**: `import imlui.log;`, then `imlui::log::info("loaded {} in {} ms", path, ms)`. The levels are `trace`,
  `debug`, `info`, `warn` and `error`, with `std::format` syntax. Formatting happens on the caller's side, so
  spdlog only receives the finished string. There's also `log::write(level, msg, source)` (source tag, for
  example `"lua"`), `log::setLevel(level)` and `log::addFileSink(path)`.
- Output goes to stderr in color: `[HH:MM:SS.mmm] [imlui] [level] msg`. The default level is `debug`.
  Messages at `warn` and above are flushed immediately.
- **Lua**: `log.info("x = %d", x)` and the other levels, plus `print(...)` at info level. Script errors are logged
  at error level with the tag `[lua]`. GLFW errors are tagged `[glfw]`.

## clang-tidy and clice

- `.clang-tidy` turns on only `readability-identifier-naming`, configured for the naming rules above, and filters
  out vendored headers. Run it on every module (`src/**/*.cppm`) and `app/main.cpp` with the `tidy` target:

  ```sh
  cmake --build build/clang-debug --target tidy
  ```

  It builds `imlui` and `imlui_app` first, then runs `clang-tidy -p build/clang-debug --warnings-as-errors=*` on
  each file, one Ninja command per file (so in parallel), and fails on any finding. A file is checked again when it
  or any module changed. `cmake/ImluiTidy.cmake` defines it; it's skipped when `clang-tidy` isn't found. A single
  file: `clang-tidy -p build/clang-debug src/imlui/theme/scheme.cppm`.
- **Module BMIs must be current.** clang-tidy parses the file itself but loads every imported module from the
  BMIs that the build's compile commands reference (the `.modmap` files). Earlier, clang-tidy 22.1.7 crashed with
  an access violation in `readability-identifier-naming` on many modules that import `imlui.widgets` or another
  umbrella (`progress_bar.cppm`, `button.cppm`, `link.cppm`, ...), so the naming checks didn't cover them. With
  freshly built BMIs it doesn't reproduce: all 114 modules pass, run one by one, all in one process and in
  parallel, with the default options and with `--header-filter=.*`, `--system-headers`, `--fix`, against
  `clang-debug` both at this commit and at the commit before the cleanup. It also doesn't reproduce with a stale
  BMI (a changed imported module, not rebuilt). If it comes back, rebuild (`cmake --build build/clang-debug`) and run
  the `tidy` target, which builds first. Running it while another build rewrites the BMIs is the most likely way to
  hit it, so don't. The `clang-release` and `clang-asan` builds work too after a build; before one, their compile
  commands lack the new modules and clang-tidy falls back to a guessed command (errors about `module;`).
- **clice** (a C++ language server with modules support) is configured by `clice.toml` to use
  `build/clang-debug`'s compile commands. Its cache lives in `.clice/`, which is git-ignored.
- Format with `clang-format -i <files>` (`.clang-format` at the repo root).

## Building and testing

Presets (`CMakePresets.json`), all clang + Ninja, build into `build/<preset>/`:

| Preset | What |
|---|---|
| `clang-debug` | Debug, `/MDd` (STL checked iterators), `-ftrapv` on our targets. The everyday build. |
| `clang-release` | Release. Build it too before merging: optimization can surface other warnings. |
| `clang-asan` | Debug code with AddressSanitizer + UndefinedBehaviorSanitizer on every target, vendored ones included. |

```sh
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --preset clang-debug

cmake --preset clang-release
cmake --build --preset clang-release

cmake --preset clang-asan
cmake --build --preset clang-asan
ctest --preset clang-asan
build/clang-asan/bin/imlui_app      # run the gallery once under the sanitizers
```

Our targets build with a strict warning set and `-Werror`, so any warning fails the build. See
[Warnings and compiler checks](../CODE_STYLE.md#warnings-and-compiler-checks) for the flags and the rules for
suppressions. Run `clang-asan` before merging larger changes.

**Sanitizers on Windows (`x86_64-pc-windows-msvc`).** `IMLUI_SANITIZERS` (`cmake/ImluiSanitizers.cmake`) adds
`-fsanitize=address,undefined -fno-sanitize-recover=undefined` to every target. On this target:

- ASan needs the release dynamic CRT, so the preset sets `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`. The code
  is still `-O0 -g`, but the STL's debug iterators are off in this preset, since they need `/MDd`. Use
  `clang-debug` for those.
- The ASan runtime DLL (`clang_rt.asan_dynamic-x86_64.dll`) is copied to `build/clang-asan/bin` at configure
  time, so the tests and the app run without changing `PATH`.
- Covered: ASan (heap, stack and global buffer overflows, use-after-free, double free), and the UBSan `undefined`
  group (signed overflow, shifts, null, alignment, bounds, enum/bool loads, float-cast overflow, unreachable,
  `function`, and more). UB aborts the process, so a test with UB fails.
- Not covered: LeakSanitizer (not available on Windows), UBSan `vptr` (not supported for the MSVC ABI), and
  ThreadSanitizer/MemorySanitizer (not available for this target).

## Testing

- Tests live in `tests/` and are ordinary (non-module) `.cpp` files. They may `#include <gtest/gtest.h>` and
  nothing else, and `import` the modules they test. Run them with `ctest --preset clang-debug` (and
  `ctest --preset clang-asan`).
- Good candidates are the pure modules: `imlui.color*` (hex parsing, OKLCH round trips), `imlui.theme.scheme`
  (role lookup, generation), `imlui.theme.theme` (`resolveColor`), and `imlui.lua.*` against a bare `lua::State`
  (prop resolution, `bind` write-back, `themeFromTable`).
- `types_test.cpp` covers the C++ type system without a window. It installs a `TypeRegistry` into a Lua state
  that runs `lua/imlui.lua` with a stub `imlui.native`, then checks conversion, sync, `nil` defaults,
  `notify`/`emit`, `__gc` lifetime, singletons (also `shared_ptr` ones), `overload` and reinstalling into a new
  state.
- `resources_test.cpp` covers the resource system: name normalization and resolving (`..` can't leave a prefix, OS
  paths are rejected), `DirectoryMount` (read, exists, list, modified, the read cache), mount stacking and mount
  points in `Resources`, and the Lua side through the harness: `require "imlui"` and components through the
  searchers, chunk names in errors, and `ui.resource.*` resolving against the calling script. It uses two support
  modules: `test::TempDir` (`imlui.test.temp_dir`, a temporary folder removed afterwards, with `write` and `touch`)
  and `test::MemoryMount` (`imlui.test.memory_mount`, an immutable in-memory mount, like an embedded one).

### The headless frame harness

Tests that render use `imlui::test::HeadlessFrame`, a test-only module in `tests/support/headless_frame.cppm`
(`import imlui.test.headless_frame;`). `tests/CMakeLists.txt` adds every `tests/support/*.cppm` to
`imlui_tests` as a `CXX_MODULES` file set, so test files import it like any module (they still include only
`<gtest/gtest.h>`).

| Member | What |
|---|---|
| `HeadlessFrame{}` / `HeadlessFrame{{.width = ..., .height = ..., .deltaTime = ..., .builtins = true}}` | Creates an ImGui context with no window, backend or GPU (`RendererHasTextures`, so fonts aren't prebuilt), a `TypeRegistry` and a `Renderer`. It mounts `imlui:/` on `IMLUI_LUA_DIR` (`lua/`) and `imlui:/fonts/` on `IMLUI_FONT_DIR` (`external/fonts`), and loads the Roboto text fonts and the icon font through those resources into the context, like the engine, so the widgets measure and draw as in the app; tests don't load fonts themselves. `builtins` registers the built-in element types first, like the engine. Frames don't apply a pending theme: call `applyPendingTheme()` between frames to apply one (it also switches the theme's fonts; see `fonts_test.cpp`), and set the default theme back at the end. |
| `registerType<T>(name)`, `renderer()`, `types()` | Register element types before the Lua state starts; the renderer (window title) and the registry. |
| `resources()`, `loadedResources()` | The harness's `Resources`: mount more (`app:/` on a `test::TempDir`) before the state starts. The names the Lua state read or loaded. |
| `run(code) -> std::string` | Runs Lua in the UI script environment (`ui.env`: constructors, `state`, `bind`, ids, `ui`), starting the state on first use: the registered types, the real `imlui.native` with empty host hooks, the painter library, the resource loader and `imlui:/imlui.lua`. Returns the error message, `""` on success: `ASSERT_EQ(h.run("..."), "")`. Assignments to free names stay in the environment. |
| `renderFrame(name)`, `renderFrames(name, n)` | One ImGui frame each (time advances by `deltaTime`): runs `ui.__tick` (timers, `onFrame`), then renders the element held by `name` in a full-display window. Events queued before (`native.injectKey`, `native.injectMouse`, `ImGui::GetIO()`) are seen. |
| `click(root, x, y, framesAfter = 1)` | A left click at `(x, y)` (screen pixels) while rendering `root`: move, press and release through `native.injectMouse`, a frame after each, then `framesAfter - 1` more. Render one frame first on a fresh tree, so the window exists to be hovered. |
| `instance<T>(name)` | The C++ instance of the element held by `name`, synced like a parent's `Frame::Child::as<T>()`. |
| `state()`, `frames()` | The Lua state and the number of frames rendered. |

Script errors go to `luaui::errors()`, which the harness clears when it's created and destroyed. One harness at a
time (ImGui's context, the font pointers and the error list are global; every harness loads its own fonts).

Two test elements in `tests/support/` are shared by the element tests; register them under any name
(`h.registerType<test::LastItemProbe>("Probe")`):

| Module | Class | What |
|---|---|---|
| `imlui.test.last_item_probe` | `test::LastItemProbe` | Renders its children and remembers the rect of the last visual one (`min()`, `max()`, `size()`, `center()`), so a test knows where to click. |
| `imlui.test.render_counter` | `test::RenderCounter` | A 10 x 10 dp box that counts its renders (`renders()`). |

`frame_test.cpp` (Frame, child handles, re-registration, unknown types), `element_test.cpp`
(`componentComplete`/`render` lifecycle, lazy sync, built-in defaults, Escape closing a `Dialog` and a
`ModalNavigationDrawer` through `native.injectKey`) and one `elements_<family>_test.cpp` per element family
(windows, layout, navigation, controls, inputs, data, system) use it.

## Scripted UI tests

`native.injectMouse(x, y, down?, wheel?)` feeds ImGui a mouse event: a position in window coordinates, an optional
left-button state, and an optional vertical wheel delta. `native.injectKey(key, down, ctrl?, shift?, alt?)` feeds
a key event by name (`"Escape"`, `"Enter"`, `"Tab"`, `"A"`, `"Equal"`, ...), with the modifier states;
`native.injectKey("Escape", true); native.injectKey("Escape", false)` presses and releases Escape over two frames. Combine it with timers to drive the real UI from a script.
Use the `GALLERY_PAGE` environment variable to start the gallery on a given page.

```lua
-- click at (200, 150) half a second after start, then check the result
setTimeout(function() native.injectMouse(200, 150) end, 500)
setTimeout(function() native.injectMouse(200, 150, true) end, 550)
setTimeout(function() native.injectMouse(200, 150, false) end, 600)
setTimeout(function()
    if s.count ~= 1 then log.error("expected one click, got %d", s.count) end
    quit()
end, 800)
```

ImGui processes a press and its release in different frames, so leave at least a frame between them, as above.
`injectMouse` starts the same 0.3 s settle period as real input, so the UI renders and reacts without real input.
While the window is focused and the real cursor is outside it, the ImGui GLFW backend replaces injected positions
with the real cursor position on every frame. Injected clicks are reliable while the real cursor rests over the
window, or while the window isn't focused.
Script errors and `log.error` output appear on stderr, which a CI job can scan.
