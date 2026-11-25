# Roadmap

Work planned or in progress, in order. Each step lands as its own commit on `material-ui`; [changelog.md](changelog.md)
records what landed.

## Fixes from the November 2026 review

Found by reading the code (the resource-tool items marked *reproduced* were also run). Ordered by priority within
each group; fix the first two groups first.

### Aborts

Every `IM_ASSERT` ends in `std::abort()` (`external/imlui_imgui_assert.cpp`), in release too, so a script or theme
value that reaches ImGui unchecked is fatal.

- **`p:icon` with a size of 0, negative or NaN** (`src/imlui/lua/painter.cppm`, `icon`): the size goes unchecked into
  `CalcTextSizeA`, which asserts `font_size > 0`. Return early when the pixel size is below 1 and clamp the top end.
  Check `md::drawIcon` (`src/imlui/widgets/common.cppm`) for the same pattern.
- **`WindowMinSize` after zoom-out** (`src/imlui/theme/theme.cppm`): validation accepts 1 dp, `ScaleAllSizes`
  truncates it to 0 at zoom 0.9 and `NewFrame` asserts. Clamp to at least 1 after scaling; test every accepted style
  override at zoom 0.5 and 3.0.
- **Cyclic element trees** (`a[1] = b; b[1] = a`, a `Loader` that returns its own ancestor) overflow the native
  stack. Add a depth cap (about 256) in `Renderer::render` that reports a script error.

### Frames that are never requested

Something is scheduled during or outside a frame and nothing asks for the frame that would service it; input hides
it because of the 0.3 s settle period.

- **A `Timer` created during render doesn't start** until an unrelated frame (`Timer::Timer` calls
  `setTicking(true)`, which requests nothing). Request a frame in `Element::setTicking(true)`.
- **`setTimeout` / `setInterval`** have the same gap: call `native.requestFrame(ms / 1000)` in `addTimer`
  (`src/resources/imlui.lua`).
- **`native.time()` is stale outside a frame** (it is `ImGui::GetTime()`): a snackbar shown from a posted function
  after an idle period expires before it is drawn, and a `setTimeout` set there fires on the next frame. Back
  `native.time`, the Lua timers, `Timer` and the snackbar with one monotonic clock.
- **Content of a clipped `Card` / `ScrollView`** still renders under `SkipItems` and records near-zero sizes in the
  layout cache (settle frames when it scrolls out, one misplaced frame when it scrolls back). Skip the content, or at
  least `recordSize`, when the child window is clipped.
- **An element whose constructor throws** is constructed again on every query (`instanceBox` in
  `src/imlui/types/registry.cppm`) and keeps a `Row` requesting frames. Remember the failure on the table.

### Lua runtime

- **A delegate that returns `nil`** leaves a hole in the children list, and both `#list` and the renderer's `rawlen`
  stop there: store `false` for a missing node.
- **A `nil` hole among positional children**: the renderer walks `rawlen`, the Lua `forEachChild` walks `ipairs`, so
  later children render without a `parent`. Walk `1..rawlen` in Lua too.
- **`LuaConvert<std::optional<T>>`** tells an omitted argument from `nil` (`isnil`): use `isnoneornil`.
- **`toDisplayString`** calls `safeCall`; it should be `safeRead`, so showing a boolean or table doesn't count as a
  state change.
- **`ui.clearTimer(nil)`** raises "table index is nil".
- Timers due in the same frame fire in `pairs` order: sort by due time, then id.

### Resource tools and mounts

- **Non-ASCII paths abort `imlui_rc` / `imlui_pack` on Windows** (*reproduced*): `main(char**)` gets ANSI arguments
  and passes them to `pathFromUtf8`. Take wide arguments (`wmain`, or `processArguments`) and add a top-level catch
  that prints the error; the same catch covers a scanned file name that has no UTF-8 form (*reproduced*).
- **`DirectoryMount` trusts a cached folder listing while the folder's time is unchanged**, so a file created in the
  same timestamp tick stays missing. List again once on a miss.
- **A literal folder in an `include` pattern spelled in the wrong case** (*reproduced*) gives names that don't exist
  on disk: apply the on-disk spelling check to include roots as it is applied to `files`.
- **`pcall` defeats the manifest instruction limit** (*reproduced*): make the limit sticky.
- Pak hardening: reject overlapping blobs and a path that is both a file and a folder, cap the total declared size,
  and let the fuzz target reach entries over 16 MiB.
- Image memory: decoding holds up to three copies of the pixels and the cache has no budget.

### Smaller

- Escape closes a menu and the `Dialog` or drawer under it in one press (ImGui's nav closes the popup in `NewFrame`
  before `beginDialog` looks). Not run yet: confirm first.
- `Slider`: an infinite `from` / `to` writes NaN, and a stepped value can clamp off its steps. Validate numeric
  properties where elements read them.
- `Column { padding }` below the item spacing pads less horizontally than vertically (a negative `Dummy` height).
- The chart's hover readout calls `std::clamp` with inverted bounds when the chart is narrower than the readout.
- `Timer` and `setInterval` start each period at the frame time and drift: accumulate (`m_last += interval`).
- With `vsync = false` nothing caps the frame rate while an animation runs.
- A grey seed color gets its primary hue from rounding noise.
- Painter functions aren't wrapped in `catchCxx`; theme `style` values are narrowed with a plain cast, not `toFloat`.
- `cmake/ImluiSanitizers.cmake` hard-codes the x86_64 ASan DLL name; multi-config generators aren't exercised.
- Tests for each fix above: none of these paths is covered today.

## Next

Follow-ups not scheduled yet:

- **Run `tidy` (and the sanitizer preset) in CI**, and extend the naming check to `tests/`.
- The default test presets skip the slow `package` tests; label the GL smoke tests so CI can skip them.
- Lua language-server definitions (`---@meta`) generated from the type registry.
- `ListView` virtualization (skip off-screen delegates).
- Per-child cost in big containers: `Frame::Child` rebuilds each child and looks up its type string on every pass,
  and `Frame::forEachChild` reads `visible` twice (the children cache made `Row { Repeater { model = 2000 } }`
  only ~10 % faster).
- `font_check` assumes glyphs up to ~128 px; `fontScale` is capped at 4, but zoom × DPI can still exceed it: cap the
  final pixel size.
- A file reached through a linked folder is readable but not listed, so hot reload sees it only once loaded.
- `TextField` copies its text every frame (`md::textField` edits in place).

## Kept as designed

- `onFrame` callbacks run only on rendered frames (rendering is on demand); use `ui.requestFrame()`, a timer or
  `Canvas { animated = true }` for self-changing state.
- Text-caret blink is driven by 100 ms frame requests (approximate).
- Monitor DPI changes are handled but can't be verified on a single-monitor 100 % setup.
- Injected mouse positions are overridden by the real cursor while the window is focused and the cursor is
  outside it (test tooling only; `native.injectKey` covers most automated checks).
