# Roadmap

Work planned or in progress, in order. Each step lands as its own commit on `material-ui`; [changelog.md](changelog.md)
records what landed.

## Next

Follow-ups not scheduled yet:

- Skins, after Material and Fluent landed: the control vocabulary types are still `md::` (`md::ButtonVariant`,
  `md::TextFieldOptions`, ...) — move them to a neutral namespace; under Material the tokens are informational only;
  clang-tidy 22.1.7 crashes in `readability-identifier-naming` on some modules, a clang-tidy bug triggered by the
  mix of global-module-fragment includes and `import std` (see [Development](development.md#clang-tidy-and-clice)):
  report it upstream with a reduced repro, and re-check with the next LLVM release.
- **Run `tidy` (and the sanitizer preset) in CI**: no CI yet, GitHub Actions deferred. When it lands, it runs `tidy`,
  `lua_check` and the `clang-debug-headless` / `clang-asan-headless` test presets (no GL smoke tests).
- Per-child cost in big containers: a `Row` builds a `Frame::Child` per pass (fills, sizes, render, and its parent's
  `contentFillsWidth` / size questions), each an instance lookup and an ImGui ID; in the `nested` bench an element's
  props are read ~18 times a frame (`visible` 3, `width`/`fillWidth` 3, `__children` 2.5). Keep a container's
  children (table, kind, id, `visible`, size props) for the frame while `luaui::changes()` stands still. The kind is
  answered from the `__native` box already.
- The Lua call path: read `bind(t, k)` without a pcall when `t` has no metatable or a plain `__index` table; push a
  delegate's scope from C++ instead of Lua `inScope`; reuse the `__children` cache entry instead of a new table.
- First frame of a big page: only what is in view (and 256 children below it) renders now, and elements cost about
  half as much to build (a pool allocator per Lua state, the constructors' short path: `ui.Text` ~0.6 µs against
  0.06 µs for a plain table). What's left is mostly building every `Repeater` delegate in Lua: the C++ instance's own
  heap allocation (`operator new` through `TypeInfo::Factory`; an in-place construction in the userdata would need a
  factory that knows the type's size), Lua's incremental GC steps and the `Box` userdata. A lazily built `Repeater`
  would change what it means (all its children exist), so long lists stay `ListView`'s job.
- Lua's generational GC made no measurable difference on the bench (tried; not switched).

## Kept as designed

- `onFrame` callbacks run only on rendered frames (rendering is on demand); use `ui.requestFrame()`, a timer or
  `Canvas { animated = true }` for self-changing state.
- Text-caret blink is driven by 100 ms frame requests (approximate).
- Monitor DPI changes are handled but can't be verified on a single-monitor 100 % setup.
- Injected mouse positions are overridden by the real cursor while the window is focused and the cursor is
  outside it (test tooling only; `native.injectKey` covers most automated checks).
