# Roadmap

Work planned or in progress, in order. Each step lands as its own commit on `material-ui`; [changelog.md](changelog.md)
records what landed.

## In progress: review fixes (branch `review-fixes`)

Findings of a whole-project review (2026-11-25). Each item gets a regression test where one can be written.

### Correctness

1. **Posted functions run protected.** `runPosted` calls posted functions outside any `lua_pcall`, so a Lua error
   raised by `notify()`/`emit()` (a script `__index`/`__newindex`, out of memory) panics and aborts instead of
   reaching the error overlay. Run them through a protected call.
2. **Singleton lifetime.** Re-registering a singleton (or destroying a by-reference one) while a state is installed
   leaves the Lua global's box pointing at a freed object. Clear the box when the object goes away.
3. **Hot-reload watcher and directory walks survive symlink loops.** Don't descend into linked directories
   (or track visited folders); the manifest scan keeps its own `error_code` per entry, so one dangling link doesn't
   fail the build; Windows' spelling check fails closed.
4. **Script values are validated before they reach C++ / ImGui.** Root window sizes that are huge, infinite or NaN;
   double→float narrowing in `LuaConvert<float>` and `Node`; theme `style` values (`Alpha`, `WindowMinSize`, ...);
   `ui.setTimeout` with a non-finite delay.
5. **Recovered render errors leave no stale state.** The Material surface and card stacks are unwound with the
   ImGui stacks; non-`std` exceptions from C++ report a message; the scope registry keys can't be merged by the
   linker; an unknown element type takes no slot in a Row/stack.
6. **Column padding** insets the box on every side when `width` is set, as `elements.md` says.
7. **Repeater/ListView delegates keep their state** when a model function returns fresh item tables (`key`).

### Performance

8. **Children resolved once per frame** per element instead of once per layout pass; a numeric model isn't
   materialised into a table.
9. **Large lists:** `Table` clips rows outside the view (`ImGuiListClipper`).
10. **Fonts are checked once** per font data, not on every theme change.
11. **Method calls** don't re-evaluate every binding (`Sync::OncePerFrame`).
12. **Glob matching** is linear-time (no exponential backtracking); icon lookups don't allocate.

## Next

Follow-ups not scheduled yet:

- **Run `tidy` (and the sanitizer preset) in CI**, and extend the naming check to `tests/`.
- The default test presets skip the slow `package` tests; label the GL smoke tests so CI can skip them.
- Lua language-server definitions (`---@meta`) generated from the type registry.
- `ListView` virtualization (skip off-screen delegates).

## Kept as designed

- `onFrame` callbacks run only on rendered frames (rendering is on demand); use `ui.requestFrame()`, a timer or
  `Canvas { animated = true }` for self-changing state.
- Text-caret blink is driven by 100 ms frame requests (approximate).
- Monitor DPI changes are handled but can't be verified on a single-monitor 100 % setup.
- Injected mouse positions are overridden by the real cursor while the window is focused and the cursor is
  outside it (test tooling only; `native.injectKey` covers most automated checks).
