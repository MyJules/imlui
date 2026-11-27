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
- Per-child cost left after the children memo: every question that reaches the `TypeRegistry` (`fillsWidth` once a
  frame, each `intrinsicSize`, the render) runs the sync's skip check, which takes the fingerprint of the element's
  declared fields (~10% of a `nested` frame: `syncInstance`, `mixValue`, the raw reads); an element's own
  `width`/`height` are read through `Frame::size` on each of its passes (not memoized when plain); a render pass over
  a long list still reads `__type`/`__inline` of every child (the memo is per frame). A `bind(t, k)` read without the
  pcall (a plain table or `__index` table) was tried and measured no gain (a protected call is a try/catch here).
- First frame of a big page: only what is in view (and 256 children below it) renders now, and elements cost about
  half as much to build (a pool allocator per Lua state, the constructors' short path: `ui.Text` ~0.6 µs against
  0.06 µs for a plain table). What's left is mostly building every `Repeater` delegate in Lua: the C++ instance's own
  heap allocation (`operator new` through `TypeInfo::Factory`; an in-place construction in the userdata would need a
  factory that knows the type's size), Lua's incremental GC steps and the `Box` userdata. A lazily built `Repeater`
  would change what it means (all its children exist), so long lists stay `ListView`'s job.
- Lua's generational GC made no measurable difference on the bench (tried; not switched).
- What SDL3 opens up: a renderer on SDL_GPU (Direct3D 12 / Vulkan / Metal) next to OpenGL 3.3, mobile targets
  (Android, iOS), and pen and touch input (the ImGui backend already tells touch from mouse).

## Kept as designed

- `onFrame` callbacks run only on rendered frames (rendering is on demand); use `ui.requestFrame()`, a timer or
  `Canvas { animated = true }` for self-changing state.
- Text-caret blink is driven by 100 ms frame requests (approximate).
- Monitor DPI changes are handled but can't be verified on a single-monitor 100 % setup.
- Injected mouse positions are overridden by the real cursor while the window is focused and the cursor is
  outside it (test tooling only; `native.injectKey` covers most automated checks).
