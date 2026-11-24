# Status / changelog

## What exists today

The `material-ui` branch, newest first:

| Commit | Change |
|---|---|
| `fixes-1` | **Docs-review fixes.** `ui.setTitle()` sticks unless the `ApplicationWindow` has a `title` prop (default `"imlui"`). Theme tables accept role names and `"role/alpha"` in `seed`, `colors.*` and `imgui.*`; `colors`/`imgui` roles refer to the scheme generated from the same table, and an invalid `imgui.*` color is reported. `RadioButton { group = ... }` unchecks the other buttons of its group. `watch()` and `onFrame()` return a function that removes the callback. New `ui.isDark`, `ui.deltaTime`, `ui.imguiVersion`. `registerBuiltinElements` moved to `imlui.ui.elements.builtin`. No more gtest `-Wcharacter-conversion` warning. README layout and element tables updated; the temporary refactor spec is removed. Tests for theme-table colors and `lua/imlui.lua`. |
| `afd358a` | **Module refactor phase B** merged: `imlui.widgets.*` modules and viewport-aware anchored popups. |
| `6d1a447` | **Module refactor phase C** merged: `imlui.ui.renderer`, `imlui.ui.props`, `imlui.ui.elements.*`, `imlui.lua.painter`, `imlui.lua.native`, `imlui.app.*`. |
| `472974a` | **Unit tests** merged: GoogleTest in `tests/` and a `ctest --preset clang-debug` preset. See [Development](development.md#testing). |
| `31dc92b` | Code style: an exception for including `<gtest/gtest.h>` in tests. Binding notes from phase A. |
| `0714daf` | **Module refactor phase A.** Foundation modules split to the code style: `src/wrap/` (`lua`, `lua.stack_guard`, `imgui`, `imlui.log`, `imlui.platform.window`), `imlui.color.*`, `imlui.theme.*`, `imlui.lua.*`. CMake globs `src/*.cppm`. |
| `1e92ca3` | Temporary module refactor spec (since removed). |
| `af0c076` | C++ code style guide (`CODE_STYLE.md`), `.clang-format`, and a `.clang-tidy` naming config. |
| `6687027` | Live redraw during window resize: the engine renders from GLFW's refresh and framebuffer-size callbacks while the OS runs its modal resize loop. spdlog 1.15.3 vendored behind `imlui.log`. `ui.log.*` and `print()` route to spdlog. |
| `2ef54d1` | **Material 3 polish.** Dear ImGui 1.92.9b (dynamic font sizes). Roboto Regular/Medium and Material Icons Round, with the M3 type scale and icons by name. Reworked widgets: icon buttons, extended FAB, filled/outlined text fields with icons, supporting and error text, dropdown menus, spin box, color field, tabs, navigation items, expanders, menus, snackbar, M3 progress indicators, charts and data table. Material disabled colors. `Row { justify }`, fill spacers, `Column { align }`, `menu` props. New elements: `TopAppBar`, `NavigationItem`, `IconButton`, `Icon`, `Divider`, `CircularProgress`, `Expander`, `Snackbar` (`ui.snackbar`). Fixed timer iteration when callbacks schedule timers. Redesigned gallery. `native.injectMouse` for scripted UI tests. |
| `0b189a3` | Initial version: the Lua DSL, renderer and engine with hot reload, a Material 3 theme from a seed, Dear ImGui 1.91.9b, Lua 5.4.8 and GLFW 3.4 vendored, and the gallery, counter and StatCard examples. |

**Feature summary:**

- A declarative Lua UI with plain, binding and two-way property values, `on*` signals, ids, observable `state`
  with `watch`, `Repeater`/`ListView`/`If`/`Loader`, and file-based components through `require`.
- 55 built-in element types covering windows, layout, app bars and navigation, menus, text, controls, inputs,
  feedback, charts, tables and Canvas.
- A Material 3 theme generated from a seed in OKLCH, with role/ImGui/style overrides and ImGui presets, decided
  entirely in Lua and applied between frames.
- Hot reload of any `.lua` file, an in-app error overlay, logging through spdlog, and live redraw during resize.

## In progress

The work happens in separate worktrees and branches, which merge into `material-ui`.

The module refactor (phases A–C) and the unit tests have landed; see the table above.

## Planned

- **Phase D: DPI scaling and user zoom.** Correct scaling when the window moves between monitors with different
  DPI, plus a user-controlled zoom factor.
- **On-demand rendering.** Render only when something changed (input, animation, timers, bindings that may have
  changed) instead of rendering every frame.
