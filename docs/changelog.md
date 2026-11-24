# Status / changelog

## What exists today

The `material-ui` branch, newest first:

| Commit | Change |
|---|---|
| (shapes) | **Crisp Material shapes.** New shape helpers in `imlui.widgets.common` (`fillRoundRect`, `strokeRoundRect`, `fillCircle`, `strokeCircle`, `strokeArc`, `hLine`, `vLine`) replace ImGui's `AddRect*`/`AddCircle*` in every widget. They use the true corner radius (ImGui clamps it to `min(w, h)/2 - 1`, so pills had flat ends), tessellate arcs by length (about one segment per 2px), snap rects to whole pixels and keep strokes whole-pixel wide and inside the shape, so 1dp outlines are single crisp pixels. Buttons, chips, segmented buttons, the switch track, linear progress and the slider track are now exact pills. The unchecked checkbox outline is now inside its 18dp box (it was 20dp wide, so the box changed size when checked). `textTop()` centers text on the cap height and rounds to a whole pixel; `drawTextV` and the buttons, chips, segmented buttons, tabs and bubbles use it (labels sat about 1px low). The theme sets `CircleTessellationMaxError = 0.10` for smoother ImGui-drawn roundings (cards, popups, scrollbars). The Slider's `format` is validated (`isValidFloatFormat` in the new `imlui.widgets.value_format`: one `%f`/`%e`/`%g` conversion, `%%` allowed). An invalid one falls back to `"%.2f"` and is logged once, instead of reaching `snprintf` unchecked. |
| (warnings) | **Zero-warning policy.** Our targets (`imlui`, `imlui_app`, `imlui_tests`) build with a strict clang warning set (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast`, and more, in `cmake/ImluiWarnings.cmake`) as errors. Vendored targets are `SYSTEM`, so their headers don't trigger our warnings, and keep their own flags. Fixed the existing warnings (float-to-double promotions when pushing numbers to Lua, signed container indices). New `clang-asan` preset: ASan + UBSan on every target, vendored included. Debug builds of our targets trap on signed overflow (`-ftrapv`). The policy is in [CODE_STYLE.md](../CODE_STYLE.md#warnings-and-compiler-checks), the commands in [Development](development.md#building-and-testing). |
| (phase D) | **DPI scaling, user zoom and on-demand rendering.** The effective scale is the monitor content scale times a user zoom (`ui.zoom`/`ui.setZoom`, `ui.dpiScale`, Ctrl + `=`/`-`/`0` and Ctrl + wheel, `ui.setZoomShortcuts`). Moving the window to a monitor with another DPI re-applies the theme at the new scale. The root window's `width`/`height` are dp, and negative "fill minus" sizes are scaled too. The engine sleeps in `glfwWaitEventsTimeout` and renders only after input (a 0.3 s settle period), while animations, indeterminate indicators, `Canvas { animated = true }`, snackbars, tooltips, text carets and timers request frames, or on `ui.requestFrame()`. Idle CPU on a static gallery page dropped from a full core to almost nothing. New module `imlui.ui.frame_request`. The gallery gets a UI zoom slider. |
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

Nothing scheduled yet.
