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

The paths below are the target layout. On `material-ui`, until phases B and C land, the widget goes in
`src/imlui/widgets.cppm` and the handler goes in `Renderer::registerBuiltins()` in `src/imlui/renderer.cppm`.

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

**2. Element handler (C++).** Register it in the matching `imlui.ui.elements.<group>` module, inside its
`register<Group>Elements(Renderer&)`:

```cpp
// Example: a hypothetical "Rating" element backed by a new md::rating() widget.
r.registerElement("Rating", [](Renderer&, const luaui::Node& n) {
    int v = static_cast<int>(n.number("value", 0));
    if (md::rating("##rating", &v, static_cast<int>(n.number("count", 5)))) {
        n.set("value", static_cast<double>(v));   // two-way write-back (honours bind/binding)
        n.emit("onChanged", {static_cast<double>(v)});
    }
});
```

- Read props through `Node` (`string`, `number`, `numf`, `boolean`, `color`, `text`, `strings`, `floats`). It
  resolves plain values, bindings and `bind()`.
- Use `sizeProp(n, "width")` from `imlui.ui.props` for sizes, and `labelId(text, "id")` for ImGui labels whose
  visible text changes.
- Write two-way props with `n.set` **before** `n.emit`.
- Containers call `r.renderStack(n)` (stacked children with `spacing`/`align`) or `r.renderChildren(n)`. Use
  `r.forEachChild(n, fn)` for custom layout, and `r.attachMenu(n, clicked)` to support a `menu` prop.
- Pass `visual = false` to `registerElement` for elements that take no layout space (windows, timers).

**3. Lua side.** Add the name to the `BUILTIN` list in `lua/imlui.lua`. Any Capitalized name already works as a
constructor, but a `BUILTIN` entry makes it a real `ui.*` field, so an element id or a global with the same name
can't shadow it.

**4. Docs and gallery.** Add the element to [Elements](elements.md), the README element table, and a demo in
`examples/gallery.lua`.

Apps can also add elements without touching imlui:
`engine.renderer().registerElement("MyThing", fn, visual)` before `engine.run()`.

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
  out vendored headers. It works with modules here:

  ```sh
  clang-tidy -p build/clang-debug src/imlui/theme/scheme.cppm src/imlui/lua/node.cppm
  ```

  Configure a build first, because it needs `build/clang-debug/compile_commands.json`.
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

## Scripted UI tests

`native.injectMouse(x, y, down?, wheel?)` feeds ImGui a mouse event: a position in window coordinates, an optional
left-button state, and an optional vertical wheel delta. Combine it with timers to drive the real UI from a script.
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
