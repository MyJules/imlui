# Roadmap

Work planned or in progress, in order. Each step lands as its own commit on `material-ui`; [changelog.md](changelog.md)
records what landed.

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
