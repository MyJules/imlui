# Roadmap

Work planned or in progress, in order. Each step lands as its own commit on `material-ui`; [changelog.md](changelog.md)
records what landed.

## Next

Follow-ups not scheduled yet:

- **Run `tidy` (and the sanitizer preset) in CI**, and extend the naming check to `tests/`.
- The default test presets skip the slow `package` tests; label the GL smoke tests so CI can skip them.
- Lua language-server definitions (`---@meta`) generated from the type registry.
- Per-child cost in big containers: a `Row` builds a `Frame::Child` per pass (fills, sizes, render, and its parent's
  `contentFillsWidth` / size questions), each an instance lookup and an ImGui ID; in the `nested` bench an element's
  props are read ~18 times a frame (`visible` 3, `width`/`fillWidth` 3, `__children` 2.5). Keep a container's
  children (table, kind, id, `visible`, size props) for the frame while `luaui::changes()` stands still. The kind is
  answered from the `__native` box already.
- Property reads still push their key through `lua_pushstring` (Lua's address cache, then a `strcmp`): ~6% of a
  steady frame. Intern the keys once per state (registry refs, or upvalues of the hot C functions).
- The Lua call path: read `bind(t, k)` without a pcall when `t` has no metatable or a plain `__index` table; push a
  delegate's scope from C++ instead of Lua `inScope`; reuse the `__children` cache entry instead of a new table.
- First frame of a big stack: every child still renders once (controls at 5000: ~200 ms; the second and third
  frames no longer do). About half of it is Lua building the `Repeater`'s delegates (`ui.Text` ~1.2 µs each against
  0.1 µs for a plain table: `types.attach` ~0.6 µs, mostly allocation, and `finishElement`), the rest rendering. Render
  only the children in view on it (estimating the others, as `ListView` does) once a cheap walk can tell that a
  subtree holds no non-visual element to pin; a pooled allocator for Lua (`lua_newstate` with a size-class free list)
  for the allocation share.
- Lua's generational GC made no measurable difference on the bench (tried; not switched).
- `ListView`: a `positionViewAtIndex` method (a script can't scroll to an item that isn't built), and a prop for the
  delegate cache size.
- `font_check` assumes glyphs up to ~128 px; `fontScale` is capped at 4, but zoom × DPI can still exceed it: cap the
  final pixel size.
- A file reached through a linked folder is readable but not listed, so hot reload sees it only once loaded.
- `TextField` copies its text every frame (`md::textField` edits in place).
- Follow-ups from the November 2026 review: a test for the painter's `catchCxx` path (nothing in it throws on
  demand); the 240 fps cap with `vsync = false` is a constant, not the monitor's refresh rate; free an image's CPU
  pixels once the backend has uploaded it, if no backend re-uploads from them.

## Kept as designed

- `onFrame` callbacks run only on rendered frames (rendering is on demand); use `ui.requestFrame()`, a timer or
  `Canvas { animated = true }` for self-changing state.
- Text-caret blink is driven by 100 ms frame requests (approximate).
- Monitor DPI changes are handled but can't be verified on a single-monitor 100 % setup.
- Injected mouse positions are overridden by the real cursor while the window is focused and the cursor is
  outside it (test tooling only; `native.injectKey` covers most automated checks).
