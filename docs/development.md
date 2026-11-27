# Development

## Code style

The full rules are in [`CODE_STYLE.md`](../CODE_STYLE.md). `.clang-format` and `.clang-tidy` enforce most of
them. In short:

- **C++20 modules only.** `#include` appears only in a module's global module fragment, and only for third-party
  headers. The standard library is `import std;` (built by `cmake/ImluiStd.cmake`), never a standard header; a C
  header only for a macro or a non-standard function `import std` doesn't export. `tests/smoke_host/main.cpp` (and
  an app's `main.cpp`) uses `import` only. Test files in `tests/` are the one exception: they include
  `<gtest/gtest.h>`, because the GoogleTest macros can't cross a module boundary, and nothing else.
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
export module imlui.example;    // a global module fragment (`module;` + #include) only for third-party headers

import std;

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
  the interface. One function lives in a separate implementation unit instead: `defaultConfig()`
  (`src/imlui/app/default_config.cpp`, `module imlui.app.config;`), compiled with the library's own files rather than
  with the modules ([Architecture](architecture.md#embedding-or-not)).
- **No module re-exports another** (umbrellas aside). Import the module that defines each type you name, for
  example `imlui.theme.scheme` for `Scheme` or `imlui.color` for `Color`.
- Namespaces: `imlui` (core), `imlui::md` (widgets), `imlui::luaui` (Lua bridge), `imlui::log`,
  `imlui::platform`, `lua`.
- `src/CMakeLists.txt` picks up `src/**/*.cppm` with a `CONFIGURE_DEPENDS` glob. New module files need no CMake
  edits, but you do need to re-run the build (which re-checks the glob). Keep `.cppm` files out of
  `src/resources/`, which holds the embedded resources only.

See [Architecture](architecture.md#module-map) for the module map.

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
- `scopedWindowName(label, id)` to name a top-level window or a modal popup (`ImGui::Begin`, `BeginPopupModal`).
  ImGui names those by the string alone, ignoring the ID stack, so a fixed name would make two instances share one
  window; this one adds the hash of `id` in the element's ID scope and stays the same when the label changes.
- The shape helpers for anything rounded or outlined: `fillRoundRect`/`strokeRoundRect` (with optional
  `ImDrawFlags_RoundCorners*`), `fillCircle`/`strokeCircle`, `strokeArc`, and `hLine`/`vLine` for dividers. Don't
  use `ImDrawList::AddRect`/`AddRectFilled` with a rounding, or `AddCircle*`: ImGui shrinks the radius (a pill gets
  flat ends), uses few segments at small radii, and draws 1px strokes at fractional positions, so they look soft.
  The helpers use the true radius (clamped to half the side, so `radius = h / 2` is an exact pill), tessellate by
  arc length, snap to whole pixels, and keep strokes whole-pixel wide and inside the shape.
- `drawTextV(style, x, cy, ...)`, or `textTop(style, cy)` for the y of `drawText`, to center a label vertically.
  It centers the cap height on a whole pixel, so labels line up the same way in every widget.
- **Off-screen drawing is culled for you.** The shape, text, icon and shadow helpers check the draw list's clip rect
  (`rectVisible(dl, a, b)`) and build nothing outside it, so a widget scrolled out of view adds no vertices. Submit
  the item and update every animation (`animate`, `stateLayer`) as usual, out of view too, so a widget scrolling back
  in isn't mid-transition; only the drawing is skipped. Drawing that bypasses the helpers (`ImDrawList::AddPolyline`,
  `PrimReserve`, expensive label formatting) checks `rectVisible` over the item's rect first, as `md::chart` does.
  Never skip side effects (tooltips, popups, an active item's value label in the foreground draw list) by it.

Return what happened (`bool` changed/clicked, or a result struct).

**Then route it through the skin.** Add a virtual pair to `Skin` (`src/imlui/skin/skin.cppm`), `rating` and
`ratingSize`, with the widget's parameters and no default arguments, whose bodies call `md::rating` and
`md::ratingSize`. The element calls `skin().rating(...)`, so another design can draw the control its own way
([Skins](architecture.md#skins)). A helper that decides no look (a shape, text or icon drawing helper) stays a direct
`md::` call.

**2. Element (C++).** One class per module, `src/imlui/ui/elements/<snake_name>.cppm`
(`export module imlui.ui.elements.<snake_name>;`), added to the `imlui.ui.elements` umbrella (`elements.cppm`) and
registered in `registerBuiltinTypes` (`builtin.cppm`). See [Writing a built-in element](#writing-a-built-in-element)
for the conventions.

```cpp
// Example: a hypothetical "Rating" element backed by a new md::rating() widget, drawn through Skin::rating().

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
    type.doc("A row of stars to rate with.")
        .property("value", &Rating::value, &Rating::setValue)
        .doc("The number of filled stars, 0..`count`.")
        .property("count", &Rating::count, &Rating::setCount)
        .doc("The number of stars.")
        .signal<int>("onChanged", {"value"})
        .doc("Called when the user picks a value.");
}

void Rating::render(Frame&) {
    if (skin().rating("##rating", &m_value, m_count)) {
        notify("value"); // two-way write-back (honours bind/binding), then onValueChanged
        emit("onChanged", m_value);
    }
}

// builtin.cppm
types.registerType<Rating>("Rating");
```

- Declared properties arrive through the setters before each `render()`, only when they changed.
- Use `frame.size("width")` for sizes (it handles `fillWidth`, fill-minus and the type's fill policy: declare
  `type.fillsWidth()` if an unset width should fill, don't default it in the widget), `frame.dp()` and
  `frame.drawList()`. If the element knows its size before drawing, implement `intrinsicSize()` (exactly what
  `render()` takes), so layouts place it right on its first frame
  ([intrinsic sizes](extending.md#intrinsic-sizes)).
- After changing a property in C++, call `notify(name)` **before** `emit`, so the handler sees the new state.
- Containers call `frame.renderChildren()` or `frame.renderStack()`, or `frame.forEachChild(f)` with
  `Frame::Child` handles (`child.render()`, `child.expectedWidth()`, `child.as<T>()`) for custom layout.
  `frame.attachMenu(clicked)` supports a `menu` prop. `frame.node()` reaches raw Lua. See
  [Registering C++ types](extending.md#element-types) for the whole `Frame`.
- A prop `render()` reads from the table itself (`frame.size("size")`, an element prop, `menu`, a lazily read table)
  is declared with [`type.rawProperty`](extending.md#raw-properties) for the editor; a stacking container derives from `StackAlign`
  and calls `stackProps(type)` for `spacing`, `align` and `cull`.
- `type.nonVisual()` for elements that take no layout space (windows, timers); `setTicking(true)` for elements
  that act while they aren't rendered ([ticking elements](extending.md#ticking-elements)).

**3. Lua side.** Add the name to the `BUILTIN` list in `src/resources/imlui.lua`. Any Capitalized name already works as
a constructor, but a `BUILTIN` entry makes it a real `ui.*` field, so an element id or a global with the same name can't
shadow it. The type registry is installed before the DSL loads, so `BUILTIN` constructors of typed elements create their
C++ instances too.

**4. Docs.** Add the element to [Elements](elements.md) and the README element table, and try it in a real window
(see [Trying changes in an app](#trying-changes-in-an-app)). Document it in `declare()` for the editor's hover text
([`doc(text)`](extending.md#docs-doctext)): the type and every property, raw property, signal and method, condensed from its
Elements section (the common props are documented already), with typed signals (`signal<Args...>(name, {"names"})`)
and named method parameters. `tests/element_docs_test.cpp` requires every built-in to be documented completely, so
a new element (or a new member of one) fails `ElementDocs.BuiltinsAreDocumented` until it has its docs; the failure
lists what's missing.

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
   is the reference built from these; keep both in step. `element_reference_test.cpp` checks it: every declared
   property and signal of every registered built-in must appear in the class doc comment (with its base class's,
   for a subclass) and in the type's section of elements.md (or its table row), each default property in the
   "Positional strings" table, and the "Non-visual elements" list must match the non-visual flag.
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
8. **Test it** with the harness (`tests/elements_<family>_test.cpp`) and **compare it in an app** built against your checkout
   (see [Trying changes in an app](#trying-changes-in-an-app)) with screenshots from before the change, at desktop and
   phone size. The tag `full-gallery` of imlui-example has a page for every built-in element. Animated content (progress, canvas,
   chart) and timer texts differ by nature.
9. **Sizes** go through one rule: pass `frame.size("width")` to a skin control (the `md::` widget resolves it with
   `md::itemWidth`), or resolve it yourself with `frame.resolvedWidth(natural)` / `frame.resolvedHeight(natural)`
   when the element sizes itself. Both use `resolveSize` (`imlui.ui.item_size`), so `-1` is exactly the available
   space and `< -1` exactly that minus `|v|` dp everywhere. Don't compute `avail + w` by hand, and don't read
   `ImGui::GetContentRegionAvail()` for a width: `frame.available()` (the layout box, `imlui.ui.layout_box`) knows
   what the parent gave, the window region doesn't.

## Adding a native function

`imlui.native` is the C++ library behind the Lua API. It lives in `imlui.lua.native`
(`luaui::registerNative(lua::State*, NativeHooks*)`, `src/imlui/lua/native.cppm`), which `Engine` calls when it
creates the Lua state. App actions (`quit`, `reload`, `setTitle`) go through `NativeHooks` callbacks, so the module
doesn't depend on `Engine`.

1. Add an entry `{"name", [](lua::State* state) -> int { ...; return nResults; }}` to the function table. Use
   `lua::error(state, "...")` for bad arguments. A C++ exception it throws becomes the Lua error
   `imlui.native.name: what`. Lua is compiled as C++, so a Lua error unwinds like an exception: the function may raise
   with C++ objects alive (a `std::string` message, ...). If the function needs the app, add a hook to `NativeHooks`.
2. Expose it from the prelude if scripts should call it directly: `ui.name = native.name` in `src/resources/imlui.lua`.
   Scripts can always reach it as `native.name`.
3. Document it in [Lua API](lua-api.md#native).

## Logging

- **C++**: `import imlui.log;`, then `imlui::log::info("loaded {} in {} ms", path, ms)`. The levels are `trace`,
  `debug`, `info`, `warn` and `error`, with `std::format` syntax. Formatting happens on the caller's side, so
  spdlog only receives the finished string. There's also `log::write(level, msg, source)` (source tag, for
  example `"lua"`), `log::setLevel(level)` and `log::addFileSink(path)` (a `std::filesystem::path`, so any file name on Windows;
  returns false, with the reason logged, when the file can't be opened; safe to call while other threads log).
- Output goes to stderr in color: `[HH:MM:SS.mmm] [imlui] [level] msg`. The default level is `debug`.
  Messages at `warn` and above are flushed immediately.
- **Lua**: `log.info("x = %d", x)` and the other levels, plus `print(...)` at info level. Script errors are logged
  at error level with the tag `[lua]`. GLFW errors are tagged `[glfw]`.

## clang-tidy and clice

- `.clang-tidy` turns on only `readability-identifier-naming`, configured for the naming rules above, and filters
  out vendored headers. Run it on every module (`src/**/*.cppm`, `tools/**/*.cppm`) and the test host
  (`tests/smoke_host/main.cpp`) with the `tidy` target, which exists only when imlui is the top-level project:

  ```sh
  cmake --build build/clang-debug --target tidy
  ```

  It builds `imlui`, `imlui_pack` and `imlui_smoke_host` first, then runs `clang-tidy -p build/clang-debug --warnings-as-errors=*` on
  each file, one Ninja command per file (so in parallel), and fails on any finding. A file is checked again when it
  or any module changed. `cmake/ImluiTidy.cmake` defines it; it's skipped when `clang-tidy` isn't found. A single
  file: `clang-tidy -p build/clang-debug src/imlui/theme/scheme.cppm`.
- **Module BMIs must be current.** clang-tidy parses the file itself but loads every imported module from the
  BMIs that the build's compile commands reference (the `.modmap` files). Earlier, clang-tidy 22.1.7 crashed with
  an access violation in `readability-identifier-naming` on many modules that import `imlui.widgets` or another
  umbrella (`progress_bar.cppm`, `button.cppm`, `link.cppm`, ...), so the naming checks didn't cover them. With
  freshly built BMIs it didn't reproduce: all 114 modules of the time passed, run one by one, all in one process and in
  parallel, with the default options and with `--header-filter=.*`, `--system-headers`, `--fix`, against
  `clang-debug` both at this commit and at the commit before the cleanup. It also doesn't reproduce with a stale
  BMI (a changed imported module, not rebuilt). If it comes back, rebuild (`cmake --build build/clang-debug`) and run
  the `tidy` target, which builds first. Running it while another build rewrites the BMIs is the most likely way to
  hit it, so don't. The `clang-release` and `clang-asan` builds work too after a build; before one, their compile
  commands lack the new modules and clang-tidy falls back to a guessed command (errors about `module;`).
- **clice** (a C++ language server with modules support) is configured by `clice.toml` to use
  `build/clang-debug`'s compile commands. Its cache lives in `.clice/`, which is git-ignored.
- Format with `clang-format -i <files>` (`.clang-format` at the repo root).

## Trying changes in an app

The library has no runner of its own: the tests cover it headlessly, and the smoke test runs `imlui_smoke_host`.
To see a change in a real UI, build an app against your working copy, usually imlui-example. Its `main.lua` is
one window with a button; run another script with `imlui_example path/to/try.lua`, or check out its tag
`full-gallery` for a page of every element:

- **Edit inside the app's submodule.** In `imlui-example/vendor/imlui`, check out a branch, work and commit there, and
  push to (or fetch from) this repository. The app's build picks up every change, and with `IMLUI_DEV_PATHS=ON` the
  library's `src/resources/` (`vendor/imlui/src/resources/`) hot-reloads like the app's own scripts.
- **Point the submodule folder at this checkout.** In the app, `git submodule deinit -f vendor/imlui` (empties the
  folder), `rmdir vendor\imlui`, then `cmd /c mklink /J vendor\imlui <path to this checkout>` (a symlink elsewhere) and
  build. Edits here, including `src/resources/`, show up in the running app at once. Before committing in the
  app, put the submodule back: `rmdir vendor\imlui` (removes only the junction), then `git submodule update --init`.

imlui is built inside the app's build tree (`imlui-example/build/<preset>/vendor/imlui`), so this repository's own
`build/` isn't involved. Once the library change is committed, update the app: `git submodule update --remote
vendor/imlui` (or check out the commit in `vendor/imlui`), build, run, and commit the new submodule commit in the
app.

## Building and testing

Presets (`CMakePresets.json`), all clang + Ninja, build into `build/<preset>/`:

| Preset | What |
|---|---|
| `clang-debug` | Debug, `/MDd` (STL checked iterators), `-ftrapv` on our targets. The everyday build. |
| `clang-release` | Release. Build it too before merging: optimization can surface other warnings. |
| `clang-ship` | Release with `IMLUI_DEV_PATHS=OFF`: everything embedded, no source paths, as shipped or installed. |
| `clang-asan` | Debug code with AddressSanitizer + UndefinedBehaviorSanitizer on every target, vendored ones included. |
| `clang-fuzz` | RelWithDebInfo with ASan + UBSan + libFuzzer coverage on every target, and the libFuzzer executables. See [Fuzzing](#fuzzing). |

```sh
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --preset clang-debug

cmake --preset clang-release
cmake --build --preset clang-release

cmake --preset clang-ship
cmake --build --preset clang-ship
ctest --preset clang-ship

cmake --preset clang-asan
cmake --build --preset clang-asan
ctest --preset clang-asan           # includes the smoke test: the test host runs under the sanitizers
```

The presets but `clang-ship` are dev builds (`IMLUI_DEV_PATHS=ON`), so `imlui_add_resources` doesn't embed
(`IMLUI_EMBED_RESOURCES` defaults to `OFF`), the library's own files included: the target `imlui` (what the tests and
the tools link) has an `imlui.embedded.imlui` that only names `src/resources/` and `external/fonts/`, as in an app's
dev build. Built on its own, imlui has its install rules on (`IMLUI_INSTALL`), so the build also has an embedded copy
of those files for the install (`imlui_files_install`, see [Architecture](architecture.md#embedding-or-not)), which
nothing in the build links: after editing `src/resources/imlui.lua`, a build only re-embeds that copy (embed, compile,
archive; the always-run file-list checks aside, nothing else runs). The tests check whichever shape the build has,
plus both shapes of the smoke app's manifest in every configuration (`imlui.embedded.smoke_embedded`, `EMBED ON`;
`imlui.embedded.smoke_sources`, `EMBED OFF`, dev builds only); the package tests check the install's copy. Before
merging resource changes, also build and test the embedded configurations:

```sh
cmake --preset clang-debug -B build/embed-on -DIMLUI_EMBED_RESOURCES=ON   # dev paths over embedded copies
cmake --preset clang-ship                                                 # as shipped: embedded, no source paths
```

Our targets build with a strict warning set and `-Werror`, so any warning fails the build. See
[Warnings and compiler checks](../CODE_STYLE.md#warnings-and-compiler-checks) for the flags and the rules for
suppressions. Run `clang-asan` before merging larger changes.

**Sanitizers on Windows (`x86_64-pc-windows-msvc`).** `IMLUI_SANITIZERS` (`cmake/ImluiSanitizers.cmake`) adds
`-fsanitize=address,undefined -fno-sanitize-recover=undefined` to every target of imlui's (vendored ones included), and
makes the flags usage requirements of the library (`imlui_lua`, `src/CMakeLists.txt`): a module built with them can't
be imported without them, so an app that adds imlui with `IMLUI_SANITIZERS` set gets them on every target that links
`imlui::imlui`, with nothing else to do (the package test `package.subdirectory` checks it in a `clang-asan` build).
On this target:

- ASan needs the release dynamic CRT, so the preset sets `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`. The code
  is still `-O0 -g`, but the STL's debug iterators are off in this preset, since they need `/MDd`. Use
  `clang-debug` for those.
- The ASan runtime DLL (`clang_rt.asan_dynamic-<arch>.dll`, for the architecture the compiler targets) is copied
  to `build/clang-asan/bin` at configure time (to `bin/<config>/` for each configuration with a multi-config
  generator), so the tests and the app run without changing `PATH`.
- ASan is built without its stack-use-after-return instrumentation (`-fsanitize-address-use-after-return=never`,
  also a usage requirement): with it, throwing from a catch block (`throw;`, or a Lua error raised in a handler:
  Lua is compiled as C++) crashes with a bogus "stack-overflow". The check is off at run time by default anyway.
- Covered: ASan (heap, stack and global buffer overflows, use-after-free, double free), and the UBSan `undefined`
  group (signed overflow, shifts, null, alignment, bounds, enum/bool loads, float-cast overflow, unreachable,
  `function`, and more). UB aborts the process, so a test with UB fails.
- Vendored Lua is compiled with `-fwrapv`: it relies on signed integer arithmetic wrapping (a hex float's exponent
  in `lua_strx2number`, found by the `manifest` fuzz target), which UBSan would otherwise abort on.
- Executables get an 8 MiB stack (`/STACK:8388608`, also a usage requirement): ASan's redzones make a level of the
  element tree about 8 KiB of stack instead of 2, and the default 1 MiB would stop the renderer's stack check
  (`Renderer::stackReserve`, see [Architecture](architecture.md)) well before `Renderer::maxDepth`.
- Not covered: LeakSanitizer (not available on Windows), UBSan `vptr` (not supported for the MSVC ABI), and
  ThreadSanitizer/MemorySanitizer (not available for this target). Data races are looked for with stress tests
  instead (`async_stress_test.cpp`, see [Testing](#testing)), which turn a race into the use-after-free or double
  free ASan reports. To stress the threading suites, repeat them, in `clang-asan` and in `clang-release` (other
  timings):

  ```sh
  build/clang-asan/bin/imlui_tests --gtest_brief=1 --gtest_shuffle --gtest_repeat=200 \
      --gtest_filter='AsyncStress*:BackgroundTest*:RunAsyncTest*:LiveTest*:FutureTest*:LuaFutureTest*:Signal*:LuaFunctionTest*:LuaValues*:MainThreadTest*:ScriptObjectHandleTest*'
  ```

## Testing

- Tests live in `tests/` and are ordinary (non-module) `.cpp` files. They may `#include <gtest/gtest.h>` and
  nothing else, and `import` the modules they test (and `import std;`, after the include). Run them with
  `ctest --preset clang-debug` (and `ctest --preset clang-asan`).
- Good candidates are the pure modules: `imlui.color*` (hex parsing, OKLCH round trips), `imlui.theme.scheme`
  (role lookup, generation), `imlui.theme.theme` (`resolveColor`), and `imlui.lua.*` against a bare `lua::State`
  (prop resolution, `bind` write-back, `themeFromTable`).
- `element_reference_test.cpp` keeps the element reference honest: it registers the built-ins and checks
  `docs/elements.md` and the element classes' doc comments (read from the source tree through the
  `IMLUI_DOCS_DIR` and `IMLUI_ELEMENTS_DIR` compile definitions) against the declared members (see
  [Writing a built-in element](#writing-a-built-in-element)).
- `lua_wrapper_test.cpp` covers the Lua wrapper and the error boundary: a Lua error runs the destructors of the C++
  frames it unwinds (also across Lua frames), a `std::bad_alloc` under a protected call is a memory error,
  `catchCxx` converts C++ exceptions and passes Lua errors, `StackGuard` keeps an error's value on top.
- `types_test.cpp` covers the C++ type system without a window. It installs a `TypeRegistry` into a Lua state
  that runs `src/resources/imlui.lua` with a stub `imlui.native`, then checks conversion, sync, `nil` defaults,
  `notify`/`emit`, `__gc` lifetime, singletons (also `shared_ptr` ones), `overload` and reinstalling into a new
  state.
- `resources_test.cpp` covers the resource system: name normalization and resolving (`..` can't leave a prefix, OS
  paths are rejected), `DirectoryMount` (read, exists, list, modified, the read cache and the data it releases, a `ResourceSelection`
  serving only the manifest's files and new files matching a pattern, a listed manifest served, the previous bytes
  served while a changed file is locked (`test::FileLock`, `imlui.test.file_lock`: a handle without sharing on
  Windows, no permissions elsewhere), no exceptions for names that aren't UTF-8, files whose names have no UTF-8 form left out, links to folders neither listed nor
  walked, a selection listed recursively in one walk), mount stacking and mount points in
  `Resources` (`listRecursive` of a folder with or without its trailing slash), and the Lua side through the harness: `require "imlui"` and components through the
  searchers, chunk names in errors, and `ui.resource.*` resolving against the calling script. It uses two support
  modules: `test::TempDir` (`imlui.test.temp_dir`, a temporary folder removed afterwards, with `write`, `touch` and
  `linkFolder`: a symbolic link to a folder, or on Windows without the privilege for one a directory junction)
  and `test::MemoryMount` (`imlui.test.memory_mount`, an immutable in-memory mount, like an embedded one).
- `resources_packed_test.cpp` covers the packed mounts: the compression rule, `EmbeddedMount` (stored and
  compressed files, zero-copy reads, decompression on first read, decompressed files kept within a budget and
  decompressed again once let go, concurrent reads, bad paths, corrupt data),
  `.pak` archives written by the `imlui_pack` code (`tools::packFiles`) and read by `PakMount` (every truncation,
  a bad magic or version, an escaping path, a mount point that isn't a folder name, a path over the length or segment
  limit (a 1 MiB `a/a/a/...` path rejected at once) and corrupt compressed data are rejected cleanly; `PakMount::open` checks the header before reading a large junk file and never throws; an archive built by
  `imlui_add_pak` in `tests/CMakeLists.txt` from the smoke app, `tests/data/smoke_pak.lua`, opens), the override order embedded < `.pak` < directory, the library's
  embedded files and their packages against the sources, `defaultConfig` (release-style from embedded data alone;
  `.pak` archives in name order under the `app/` and `imlui/` override folders next to the executable; a package's
  dev source folder on top, serving only its selection, skipped with `IMLUI_DEV_PATHS=0` or when missing; the real
  executable path from `imlui.platform.process`; a script argument replacing every
  `app:/` mount, relative paths falling back to the executable's folder; a package without files mounting its source
  folder even with `IMLUI_DEV_PATHS=0`, and nothing but a logged error when that folder is missing), and the harness
  on the library's packages (`Options::embedded`: embedded data, or the source folders in a build that doesn't embed). `test::ScopedEnv` (`imlui.test.scoped_env`) sets an environment variable for one test. The tests link `imlui_pack_lib` for the writer.
- `lua_definitions_test.cpp` covers the [Lua definitions](extending.md#editor-support-lua-definitions): the LuaLS type
  of every built-in `LuaConvert` (and of one without the hook), named structs and enums declared once, a golden
  `types.lua` of a small registry (struct, enums, map, pair, variant, `Future`, `LuaFunction`, typed and untyped
  signals, parameter names, a default property, a singleton, a type name that isn't a Lua name), that the built-ins'
  `types.lua` and `src/resources/definitions/imlui.lua` compile as Lua, that the hand-written file defines every `ui`
  member the prelude has (and nothing else), `writeLuaDefinitions` (unchanged files aren't rewritten) and the
  `--lua-defs` / `IMLUI_LUA_DEFS` options of `defaultConfig`. The ctest `lua_definitions.imlui_smoke_host`
  (`tests/lua_definitions_run.cmake`) runs the test host alone with both and checks the files.
- `host_settings_test.cpp` covers `HostSettings`, the reload transaction `Engine::reloadNow` runs every load
  through (the Engine itself needs a window and has no unit tests): a script's top-level `setZoomShortcuts(false)`
  holds after the load and a reload, and a load that fails puts back the title, zoom, window size, zoom shortcuts,
  culling and theme it set.
- `log_test.cpp` covers `log::addFileSink`: a log file whose name is outside the ANSI code page, and a file that
  can't be opened reported as `false` without an exception.
- `resource_manifest_test.cpp` covers resource manifests (`imlui.tools.resource_manifest`): glob patterns (their corners, and pathological ones
  matched quickly), one group
  and a list of groups, include/exclude, the manifest never matching itself, new files found on the next parse (and
  the file listing changing), the compression override, errors (missing prefix, missing file, paths leaving the
  base folder, a listed file or an `include` folder spelled differently on disk or by a name that isn't an entry's
  own (Win32's trailing `.` or space, 8.3 short names), the sandbox and its instruction and memory limits, strings
  that aren't UTF-8, bad shapes; `evaluateManifest` and `resolveManifest` on their own), the scan and `collectFiles` past entries
that can't be queried, linked files named by their own path, `replaceFile` (concurrent writers, a reader holding the file open), `manifestDependencies` and the dependency file format, merging groups into
  one `.pak` (`tools::mergeGroups`), and end to end: generated modules match their manifest. The library's packages
  hold exactly what `src/resources/resources.lua` lists (and not the manifest itself, which sits in the folder it
  embeds), or, not embedded, name its groups' folders; `imlui.embedded.smoke_embedded` (forced `EMBED ON`) is
  checked the embedded way and `imlui.embedded.smoke_sources` (forced `EMBED OFF`, dev builds) the other way, with
  its `mount()` mounting the folders.
- **Fuzz corpus replays** (`fuzz.pak`, `fuzz.font`, `fuzz.manifest`, `fuzz.names`, `fuzz.image`, label `fuzz`): every fuzz
  target's seed corpus, crash regressions included, run once through the target in every build. See
  [Fuzzing](#fuzzing).
- **Smoke test** (`smoke.imlui_smoke_host`, `tests/smoke_run.cmake`) runs the test app host like a shipped release.
  `imlui_smoke_host` (`tests/smoke_host/main.cpp`, built only with the tests) embeds the smoke app
  (`tests/data/smoke_app/`: `main.lua`, a `Tile.lua` component, a `widgets/Label.lua` component in a nested folder,
  `data/notes.txt` and the manifest itself listed in `files`, and a test image from a second manifest group at
  `app:/images/`; always embedded, `EMBED ON`) and registers a layout probe as `Probe`. The executable is copied alone (plus the DLLs next to it, such as the ASan runtime) into an
  empty folder under `build/<preset>/tests/smoke/` and started with `IMLUI_DEV_PATHS=0` and
  `IMLUI_EXIT_AFTER_FRAMES=5`. It must exit with 0, log that its entry script came from its embedded data, and log
  no error. In a build that doesn't embed the library (`IMLUI_EMBED_RESOURCES=OFF`), `imlui:/` comes from the
  library's source folders, which `IMLUI_DEV_PATHS=0` doesn't skip as they're its only copy. It opens a window, so it needs a desktop session. An app can reuse `tests/smoke_run.cmake` for its own
  executable (`cmake -DEXE=... -DWORK=... -DEXPECT=... -P vendor/imlui/tests/smoke_run.cmake`).
- **Dev/shipped parity test** (`smoke.modes`, `tests/smoke_modes.cmake`) runs the same host three ways, each copied
  alone into its own folder under `build/<preset>/tests/smoke/smoke.modes/`, and fails unless all three see the same
  thing: `dev` with the build's defaults (in a dev build the smoke app's source folders, filtered by the manifest,
  over the embedded copy; in `clang-ship` the same as the next), `embedded` with `IMLUI_DEV_PATHS=0`, and `pak` with
  `IMLUI_DEV_PATHS=0`, `IMLUI_SMOKE_NO_EMBEDDED_APP=1` (a switch of the test host only: it passes no app packages)
  and `smoke_app.pak` next to the executable (`imlui_add_pak` from the same manifest, built into
  `build/<preset>/tests/smoke_app_pak/`, not `bin/`, where every executable would mount it). Each run must exit with 0,
  log no error and log that `app:/main.lua` came from its mode's mount (a folder, the embedded data, the archive).
  After the run the host prints `summary:` lines: `list()` of every folder under `app:/`, every file of
  `listRecursive("app:/")` with its size and FNV-1a hash, and where the probe (the column's last element) was laid
  out and how many draw commands its window had by then, which follows from the fonts, texts and image the app got.
  The app prints a line per name it checks (size read, `exists()`) and asserts the expected answer: the manifest,
  `data/notes.txt`, `widgets/Label.lua` and the image are served; `hidden.txt`, `data/unlisted.txt` (on disk, not
  selected), `scratch/draft.lua` (excluded), the `scratch` folder, `data/Notes.txt` (wrong case) and the rest of the
  image group's folder are not. The script compares each run's lines with the `dev` run's and lists the lines only
  one of them printed (`only in dev: summary: file app:/data/notes.txt 93 ...`). In a dev build the `dev` run reads
  the source folders while the other two use what the last build embedded or packed, so run it after building: an
  edited smoke app file fails it until the next build.

### Environment variables

| Variable | Effect |
|---|---|
| `IMLUI_EXIT_AFTER_FRAMES=n` | Quit after `n` rendered frames, with exit code 1 when a script error was reported (`EngineConfig::exitAfterFrames`, read by `defaultConfig`). For smoke runs. |
| `IMLUI_DEV_PATHS=0` | Ignore the source folders compiled into a dev build's embedded packages (`ResourcePackage::sourceDir`): run from the embedded data and the files next to the executable, like a release build. Packages that aren't embedded (`IMLUI_EMBED_RESOURCES=OFF`) still mount their folder, their only copy. |
| `IMLUI_LUA_DEFS=<dir>` | Write the Lua language-server definitions into `<dir>` and exit instead of running (`EngineConfig::luaDefinitions`, read by `defaultConfig`, like the option `--lua-defs <dir>`, which wins). See [Editor support](extending.md#editor-support-lua-definitions). |
| `IMLUI_SMOKE_NO_EMBEDDED_APP=1` | Test host only (`imlui_smoke_host`, read by `tests/smoke_host/main.cpp`, not the library): leave the embedded smoke app out, so `app:/` comes from the archives next to the executable. For the `pak` run of `smoke.modes`. |

### Resource tools

`tools/` builds two host tools with the same toolchain (see [Architecture](architecture.md#resource-manifests)).
Both read a Lua resource manifest (`resources.lua`). `imlui_rc`, which `imlui_add_resources(<target> <manifest>)`
runs, writes the blob, the `imlui.embedded.<name>` module and the dependency file `<name>.d` into
`build/<preset>/<dir>/imlui_resources/<target>/` (without embedding, `imlui_rc --source-dirs` writes only the module,
which names the groups' folders and depends on the manifest alone); `imlui_rc --list` (target
`imlui_resources_<target>_<name>_list`, run on every
build, only when embedding) rewrites `<name>.files` when the manifest's set of files changed, so a file added to a globbed folder
re-embeds even where Ninja misses the folder's new time. `imlui_pack` builds `.pak` archives
(`imlui_add_pak(<name> <manifest>)`, or by hand: `imlui_pack out.pak app:/ <folder> [files...]`). Their shared code
is the `imlui_resource_tools` library (`tools/lib/`, links the zstd compressor, `imlui_lua`, the Lua wrapper, and
`imlui_glob`, the manifest patterns, which the library also uses for dev source folders; it doesn't link `imlui`,
which is embedded with it), and the archive writer is `imlui_pack_lib` (links `imlui_core` for
`imlui.resources.pak_format`, not the library's files). Generated modules are not checked by `tidy`; the generator is. To see what a manifest
selects, run `build/<preset>/bin/imlui_rc --list <manifest> <file>` and read the file.

### Package tests

`tests/package/` checks that an app can consume imlui each way [Getting started](getting-started.md#1-adding-imlui-to-your-project)
describes. `tests/package/consumer/` is a minimal app (`main.cpp`, `src/resources/` embedded as
`imlui.embedded.consumer`, `src/pak/` as `consumer_data.pak`, its own `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, every helper)
whose `CMakeLists.txt` adds imlui as `IMLUI_CONSUMER_MODE` says. `tests/package/run.cmake` configures and builds it
with this build's generator, compilers and build type, then runs it alone in an empty folder with
`IMLUI_DEV_PATHS=0` and `IMLUI_EXIT_AFTER_FRAMES=5`; its `main.lua` asserts that `imlui:/`, `imlui:/fonts/` and the
archive are mounted. It must exit with 0 and log no error.

| Test (label `package`) | imlui added with |
|---|---|
| `package.subdirectory` | `add_subdirectory(<this checkout>)`, in an app that has its own GLFW (added before imlui: imlui must use it) and spdlog (added after: imlui's copy must not be named `spdlog`), `IMLUI_CONSUMER_OWN_DEPS` |
| `package.fetchcontent` | `FetchContent_Declare(imlui SOURCE_DIR <this checkout>)` + `FetchContent_MakeAvailable` (a `GIT_REPOSITORY` works the same: `-DIMLUI_GIT_REPOSITORY=<url> -DIMLUI_GIT_TAG=<rev>` on the consumer) |
| `package.install` | not an app: `cmake --install` of this build into `build/<preset>/tests/package/prefix`, the fixture of the two below |
| `package.find_package` | `find_package(imlui 0.1 CONFIG REQUIRED)` on that prefix; the app is configured with `IMLUI_DEV_PATHS=OFF`, so the executable carries everything |
| `package.fetchcontent_find` | `FetchContent_Declare(... FIND_PACKAGE_ARGS 0.1 CONFIG)`: fails unless it took the installed package |

They're slow the first time: each consumer builds the whole library (add_subdirectory, FetchContent) or compiles the
installed modules' BMIs again (find_package), a few minutes in all. Their build folders
(`build/<preset>/tests/package/<mode>/`) are kept, so later runs only rebuild what changed. They run one at a time
(`RUN_SERIAL`). `ctest --preset clang-debug -L package` runs only them, `-LE package` everything else, and
`-DIMLUI_PACKAGE_TESTS=OFF` leaves them out. They aren't added with a multi-config generator, and the two install
tests need `IMLUI_INSTALL=ON`. A sanitizer build (`clang-asan`) has only `package.subdirectory`, without the app's
own GLFW and spdlog but with `IMLUI_SANITIZERS` passed on: the consumer sets nothing else, so it checks that linking
`imlui::imlui` gives the app's targets the flags importing imlui's modules needs.

### The headless frame harness

Tests that render use `imlui::test::HeadlessFrame`, a test-only module in `tests/support/headless_frame.cppm`
(`import imlui.test.headless_frame;`). `tests/CMakeLists.txt` adds every `tests/support/*.cppm` to
`imlui_tests` as a `CXX_MODULES` file set, so test files import it like any module (they still include only
`<gtest/gtest.h>`).

| Member | What |
|---|---|
| `HeadlessFrame{}` / `HeadlessFrame{{.width = ..., .height = ..., .deltaTime = ..., .builtins = true}}` | Creates an ImGui context with no window, backend or GPU (`RendererHasTextures`, so fonts aren't prebuilt; `RendererHasVtxOffset`, so a window may hold more than 64k vertices, like the OpenGL 3 backend), a `TypeRegistry` and a `Renderer`. It mounts `imlui:/` on `IMLUI_RESOURCES_DIR` (`src/resources/`) and `imlui:/fonts/` on `IMLUI_FONT_DIR` (`external/fonts`), or the library's embedded files with `.embedded = true`, and loads the Roboto text fonts and the icon font through those resources into the context, like the engine, so the widgets measure and draw as in the app; tests don't load fonts themselves. `builtins` registers the built-in element types first, like the engine. Frames don't apply a pending theme: call `applyPendingTheme()` between frames to apply one (it also switches the theme's fonts; see `fonts_test.cpp`), and set the default theme back at the end. |
| `registerType<T>(name)`, `renderer()`, `types()` | Register element types before the Lua state starts; the renderer (window title) and the registry. |
| `resources()`, `loadedResources()` | The harness's `Resources`: mount more (`app:/` on a `test::TempDir`) before the state starts. The names the Lua state read or loaded. |
| `images()` | The harness's `ImageCache`, which `Image` elements draw from (decoding from `resources()`). Its textures stay `WantCreate`: there is no backend. `renderFrame` ends with its `collect()`, like the Engine, so the byte budget applies. Test images live in `tests/data/` (`IMLUI_TEST_DATA_DIR`): mount `app:/` on it, or copy one into a `TempDir` with `write`. |
| `run(code) -> std::string` | Runs Lua in the UI script environment (`ui.env`: constructors, `state`, `bind`, ids, `ui`), starting the state on first use: the registered types, the real `imlui.native` with empty host hooks, the painter library, the resource loader and `imlui:/imlui.lua`. Returns the error message, `""` on success: `ASSERT_EQ(h.run("..."), "")`. Assignments to free names stay in the environment. |
| `renderFrame(name)`, `renderFrames(name, n)` | One ImGui frame each (time advances by `deltaTime`): runs `ui.__tick` (timers, `onFrame`), then renders the element held by `name` in a full-display window, inside `luaui::renderProtected` like the engine (an escaped Lua error is reported and ImGui recovered). Events queued before (`native.injectKey`, `native.injectMouse`, `ImGui::GetIO()`) are seen. |
| `click(root, x, y, framesAfter = 1)` | A left click at `(x, y)` (screen pixels) while rendering `root`: move, press and release through `native.injectMouse`, a frame after each, then `framesAfter - 1` more. Render one frame first on a fresh tree, so the window exists to be hovered. |
| `instance<T>(name)` | The C++ instance of the element held by `name`, synced like a parent's `Frame::Child::as<T>()`. |
| `state()`, `frames()` | The Lua state and the number of frames rendered. |
| `time()`, `advanceTime(seconds)` | The harness replaces the app clock (`imlui.ui.clock`) with a simulated one that starts at 0 and advances by `deltaTime` at the start of each frame. `advanceTime` moves it between frames, like the app idling (the next frame's ImGui `DeltaTime` includes the idle time, as with a backend). `ui.time()`, the timers, `Timer`, the snackbar and the frame requests all run on it. |

Script errors go to `luaui::errors()`, which the harness clears when it's created and destroyed. One harness at a
time (ImGui's context, the font pointers and the error list are global; every harness loads its own fonts).

Two test elements in `tests/support/` are shared by the element tests; register them under any name
(`h.registerType<test::LastItemProbe>("Probe")`):

| Module | Class | What |
|---|---|---|
| `imlui.test.last_item_probe` | `test::LastItemProbe` | Renders its children and remembers the rect of the last visual one (`min()`, `max()`, `size()`, `center()`), so a test knows where to click. |
| `imlui.test.render_counter` | `test::RenderCounter` | A 10 x 10 dp box that counts its renders (`renders()`) and remembers the draw list of the ImGui window it last rendered into (`drawList()`). |

Tests of worker threads (`background_test.cpp`, `run_async_test.cpp`, `live_test.cpp`, `future_test.cpp`) wait with
`test::PostWaiter` (`imlui.test.post_waiter`): it installs the post wake handler, and `runUntil(pred)` runs the
main-thread queue until `pred()` holds, woken by each post; `waitForPosts(n)` waits without running. No sleeps or
busy loops: workers signal with latches, condition variables and stop tokens. `async_stress_test.cpp` races them for
the sanitizers: objects dying while their `runAsync` jobs spin, report progress and hold stop callbacks; `Task::cancel`
from two threads against the jobs' ends (each task ends exactly once); the pool shut down while other threads queue
work (every task destroyed once); promise copies settling and reporting from several threads; main-thread cancels
against settling futures; `Live` writers against their owner's death. Their outcomes vary; the checks hold for every
interleaving.

`frame_test.cpp` (Frame, child handles, re-registration, unknown types), `element_test.cpp`
(`componentComplete`/`render` lifecycle, lazy sync, built-in defaults, Escape closing a `Dialog` and a
`ModalNavigationDrawer` through `native.injectKey`) and one `elements_<family>_test.cpp` per element family
(windows, layout, navigation, controls, inputs, data, system) use it. So does `culling_test.cpp`
([culling](architecture.md#culling): bindings of culled children don't run, positions and the scroll range are the
same with culling on and off, also on the first frame after a scroll, a `Timer`, a `Dialog`, a focused `TextField`
and nested `ScrollView`s out of view, no settle frames while scrolling, `cull = false` and `ui.setCulling`).

## Benchmark

`imlui_bench` (`tests/bench/`) measures big UIs headlessly. It renders the stress scenarios of
`tests/bench/stress.lua` (the gallery app's stress page: `tiles`, a `Grid` of `Card`s; `controls`, a `Column` of
`Row`s of eight bound controls; `listview`, ten `ListItem`s per count unit; `table`, a hundred `Table` rows per count
unit; `nested`, cards with stacks eight deep; `plots`) through the [headless frame harness](#the-headless-frame-harness)
in a 1180 x 820 display, each inside a page-like `ScrollView`. Per scenario and count it prints the time to build the
tree (`build`: the constructors; a `Repeater` builds its delegates on its first frame, so they count in `first`), the
first frame, and the median and mean time per frame over 100 frames (after 10 to settle) twice: `anim`, with a `Timer`
that changes a value every item reads through a binding on every frame, and `idle`, with nothing changing (a forced
frame: what any redraw costs). `vertices` is ImGui's vertex count of the last frame. The list-model scenarios
(`model`, `modelkey`, `luarows`, `luarowskey`) print a table of their own: one `ListView` of `--rows` rows (100 000 by
default) of `ListItem`s, from a C++ [`VectorModel`](extending.md#list-models) without and with keys, or from a Lua list
of row tables without and with a `key` function; `build` is filling the rows and building the tree, then the first
frame, the steady frame (median), the frame after one visible row changed and the frame after a row was inserted at
the top (medians), each with the delegates it built. It is built with the tests when imlui is the top-level project
and never run by ctest. Take numbers from a Release build:

```sh
cmake --preset clang-release
cmake --build --preset clang-release --target imlui_bench
build/clang-release/bin/imlui_bench                                  # every scenario at counts 1000 and 5000
build/clang-release/bin/imlui_bench controls listview --count 1000   # some scenarios, one count
build/clang-release/bin/imlui_bench nested --count 100,1000 --frames 300 --warmup 20 --size 1920x1080
build/clang-release/bin/imlui_bench model modelkey luarows luarowskey --rows 100000
```

A frame here is ImGui's `NewFrame`, `ui.__tick`, the render pass and `ImGui::Render` (the draw lists), without a
backend or a swap. It exits with 1 when a scenario failed or reported a script error. Add a scenario as a builder in
`stress.lua` and its name to `stress.scenarios` and to the default list in `main.cpp`.

## Fuzzing

The code that reads bytes it can't trust has fuzz targets in `tests/fuzz/`. Each is one file, `<name>.cpp`,
defining libFuzzer's `LLVMFuzzerTestOneInput`:

| Target | Input | What it checks |
|---|---|---|
| `pak` | a `.pak` archive | `PakMount::fromBytes` (`parsePak`) accepts or rejects it with a reason; every file of an accepted archive reads back at its declared size (zstd decompression in `PackedMount`, a file declaring more than 16 MiB is read only when that read is rejected before allocating: a compressed one whose zstd frame declares another size), and `exists`/`list` work on every folder. |
| `font` | font data | A font `checkFont` accepts is read with stb_truetype the way ImGui reads it (`imlui.fuzz.truetype`, `tests/fuzz/support/`): `stbtt_GetFontOffsetForIndex`, `stbtt_InitFont`, the vertical metrics, `stbtt_FindGlyphIndex` for every code point an `ImWchar` holds (each must map to a glyph the font has), every glyph found but 0 (the glyphs `checkFont` checks, and all ImGui ever loads) with its box and horizontal metrics, the outline of each that ImGui rasterizes (`stbtt_GetGlyphShape`, up to 8192 glyphs), and a bitmap for up to 64 of the glyphs found, spread over the font, as ImGui's `FontBakedLoadGlyph` makes it (`stbtt_MakeGlyphBitmapSubpixelPrefilter` with 2x horizontal oversampling, at a size from 4 to 64px picked by the input's length; a bitmap size ImGui would `IM_ASSERT` on aborts), with `STBTT_assert` aborting (for CFF fonts the box runs the glyph's charstring). |
| `manifest` | a resource manifest's Lua text | `evaluateManifest` (the sandbox, the instruction and memory limits) returns groups or an error; groups whose `base` stays inside the fixture `tests/fuzz/manifest_root/` are resolved against it (`resolveManifest`, `manifestListing`). |
| `names` | two strings, split at the first newline | Resource names and glob patterns (`imlui.resources.name`, `imlui.resources.glob`): a normalized path or name is a fixed point, a parsed name round-trips through `full()` and has a folder, a resolved name is normalized, `globMatch` agrees with a backtracking reference matcher on inputs up to 12 characters and with plain comparison on patterns without wildcards, and `globRoot` is a prefix of a valid pattern. |
| `image` | PNG or JPEG bytes | `decodeImage` (stb_image) decodes or fails with a reason; a decoded image has `width * height * 4` bytes. Images over 4096 x 4096 pixels are skipped. |

Each target is built two ways (`tests/fuzz/CMakeLists.txt`, `imlui_add_fuzz_target`):

- **Always**, as `imlui_fuzz_replay_<name>`: the target with `tests/fuzz/replay_main.cpp`, which feeds it every file
  of its seed corpus once. The ctest test `fuzz.<name>` (label `fuzz`) runs it on `tests/fuzz/corpus/<name>/` plus
  the seeds too large to check in or built (the bundled fonts, the smoke app's `.pak`), so every `ctest` run, in
  every preset, replays the corpora, crash regressions included. It prints each file before running it, so the last
  name in a failed test's output is the input that crashed.
- **With `IMLUI_FUZZ=ON`** (the `clang-fuzz` preset), as `imlui_fuzz_<name>`: the libFuzzer executable
  (`-fsanitize=fuzzer`). The option adds `fuzzer-no-link` (libFuzzer's coverage instrumentation) to the sanitizer
  list, so every target, vendored ones included, is instrumented. When the compiler can't link a libFuzzer executable,
  configuring warns and only the replay tests are built.

On Windows (`x86_64-pc-windows-msvc`), LLVM's `clang_rt.fuzzer` is built against the static release CRT and without
the STL's ASan annotations, and the MSVC STL refuses to link objects that disagree on either. So `clang-fuzz` uses
`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (unlike `clang-asan`; ASan works with it), and `IMLUI_FUZZ` defines
`_DISABLE_STRING_ANNOTATION` and `_DISABLE_VECTOR_ANNOTATION` everywhere. The ASan runtime DLL is copied to
`build/clang-fuzz/bin` as for `clang-asan`.

```sh
cmake --preset clang-fuzz
cmake --build --preset clang-fuzz
ctest --preset clang-fuzz -L fuzz       # the replays, under the sanitizers

# Fuzz one target for 10 minutes. The first folder is the working corpus (new inputs go there), the others are read.
mkdir -p fuzz-work/pak
build/clang-fuzz/bin/imlui_fuzz_pak -max_total_time=600 -timeout=20 -rss_limit_mb=4096 \
    -dict=tests/fuzz/pak.dict -artifact_prefix=fuzz-work/ fuzz-work/pak tests/fuzz/corpus/pak build/clang-fuzz/tests/smoke.pak
```

`tests/fuzz/<name>.dict` are libFuzzer dictionaries (field names, magics, Lua tokens). A crash leaves
`crash-<hash>` (or `timeout-`, `oom-`) under `-artifact_prefix`; rerun it with `imlui_fuzz_<name> <file>`, shrink it
with `-minimize_crash=1 -runs=10000 <file>`, fix the bug, and check the minimized input in as
`tests/fuzz/corpus/<name>/regress-<what>` so the replay test keeps it fixed. Keep corpus files small (a few KiB);
larger seeds go in the `SEEDS` of `imlui_add_fuzz_target` from where they already live.

**Adding a target:** write `tests/fuzz/<name>.cpp` (declare `extern "C" int LLVMFuzzerTestOneInput(const
std::uint8_t* data, std::size_t size);`, then define it; `import` what it tests; return 0; call `std::abort()` when
an invariant breaks, such as a rejection without a reason), add a few seeds under `tests/fuzz/corpus/<name>/` (valid
samples and hand-made broken ones), and call `imlui_add_fuzz_target(<name> LIBRARIES <libs> [DEFINITIONS ...]
[SEEDS ...])` in `tests/fuzz/CMakeLists.txt`. Copy the input into a buffer of exactly `size` bytes when the code
under test might read past it, so ASan sees the overrun, and skip inputs that are valid but only slow (huge
declared sizes), which would trip the fuzzer's memory or time limit.

## Scripted UI tests

`native.injectMouse(x, y, down?, wheel?)` feeds ImGui a mouse event: a position in window coordinates, an optional
left-button state, and an optional vertical wheel delta. `native.injectKey(key, down, ctrl?, shift?, alt?)` feeds
a key event by name (`"Escape"`, `"Enter"`, `"Tab"`, `"A"`, `"Equal"`, ...), with the modifier states;
`native.injectKey("Escape", true); native.injectKey("Escape", false)` presses and releases Escape over two frames. Combine it with timers to drive the real UI from a script.

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
