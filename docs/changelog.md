# Status / changelog

## What exists today

The `material-ui` branch, newest first:

| Commit | Change |
|---|---|
| (phase D) | **DPI scaling, user zoom and on-demand rendering.** The effective scale is the monitor content scale times a user zoom (`ui.zoom`/`ui.setZoom`, `ui.dpiScale`, Ctrl + `=`/`-`/`0` and Ctrl + wheel, `ui.setZoomShortcuts`). Moving the window to a monitor with another DPI re-applies the theme at the new scale. The root window's `width`/`height` are dp, and negative "fill minus" sizes are scaled too. The engine sleeps in `glfwWaitEventsTimeout` and renders only after input (a 0.3 s settle period), while animations, indeterminate indicators, `Canvas { animated = true }`, snackbars, tooltips, text carets and timers request frames, or on `ui.requestFrame()`. Idle CPU on a static gallery page dropped from a full core to almost nothing. New module `imlui.ui.frame_request`. The gallery gets a UI zoom slider. |
| `31dc92b` | Code style: an exception for including `<gtest/gtest.h>` in tests. Binding notes from phase A in the refactor spec. |
| `0714daf` | **Module refactor phase A.** Foundation modules split to the code style: `src/wrap/` (`lua`, `lua.stack_guard`, `imgui`, `imlui.log`, `imlui.platform.window`), `imlui.color.*`, `imlui.theme.*`, `imlui.lua.*`. CMake globs `src/*.cppm`. |
| `1e92ca3` | Temporary module refactor spec (`docs/refactor-spec.md`). |
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

- **Module refactor phase B: widgets.** Split `widgets.cppm` into `imlui.widgets.*` modules (common, surface, text,
  button, selection, slider, progress, field, text_field, menu, dropdown, spin_box, color_field, tabs, navigation,
  list, card, dialog, snackbar, chart, table) plus the `imlui.widgets` umbrella. This phase also includes a
  **menu viewport fix**: anchored menus, including the dropdown and color-field popups, will stay inside the main
  viewport. They shift left when they would overflow the right edge, and open upward when there's no room below.
- **Module refactor phase C: renderer, elements, engine.** Split `renderer.cppm` into `imlui.ui.renderer`,
  `imlui.ui.props` and the `imlui.ui.elements.*` groups. Move the Canvas painter to `imlui.lua.painter` and
  `imlui.native` to `imlui.lua.native` (with `NativeHooks`, so it doesn't depend on `Engine`). Split
  `engine.cppm` into `imlui.app.config` and `imlui.app.engine`. Behavior stays the same.
- **Unit tests (GoogleTest).** Add a `tests/` directory and a `ctest --preset clang-debug` preset. See
  [Development](development.md#testing).

## Planned

Nothing scheduled yet.
