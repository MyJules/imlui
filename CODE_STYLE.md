# C++ code style

These rules apply to all C++ in `src/` and `app/`. Vendored code in `external/` keeps its own style.
`.clang-format` and `.clang-tidy` (naming checks) at the repo root enforce most of them.

## Language and modules

- C++20, **modules only**. Headers are included only in the global module fragment
  (`module;` ... `export module x;`) of a module, and only third-party headers or the standard library.
- `app/main.cpp` and every other non-module file use `import` only. They never `#include`.
- Third-party libraries are reached through wrapper modules in `src/wrap/` (`lua`, `imgui`, `imlui.log`,
  `imlui.platform.window`). Other code imports the wrapper, never the library header.

### One thing per module

A module exports exactly one of:

- **one class**, including its nested types (`Window`, `Window::Config`), or
- **a family of related free functions**, together with the plain option/result structs and enums those functions
  take or return (`textField` + `TextFieldOptions` + `TextFieldResult`).

If a module starts to hold two unrelated things, split it. Umbrella modules (`imlui`, `imlui.widgets`,
`imlui.ui.elements`) only `export import` other modules.

### Interface first, implementation in the private module fragment

Each module is a single file:

```cpp
module;
#include <string>               // global module fragment: third-party / std headers only

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

module :private;               // everything below is implementation

namespace imlui {

Example::Example(std::string name) : m_name{std::move(name)} {}
const std::string& Example::name() const { return m_name; }

} // namespace imlui
```

- The exported part holds **declarations only**: class definitions with member declarations, function
  declarations, and types.
- All function bodies, static/global state and helpers go **after `module :private;`**. Helpers that are not
  exported go in an anonymous namespace there.
- Exceptions, because importers need the body: **templates** (e.g. `log::info<Args...>`,
  `Renderer::forEachChild<F>`), **`constexpr`** functions and constants, and **`= default` / `= delete`**
  special members.

## Naming

| Entity | Style | Example |
|---|---|---|
| Types (class, struct, enum, alias, concept) | `PascalCase` | `TextFieldOptions`, `ButtonVariant` |
| Functions and methods | `camelCase` | `renderStack()`, `parseHex()` |
| Variables, parameters, constants | `camelCase` | `rowHeight`, `defaultSeed` |
| Class data members (incl. static) | `m_` + `camelCase` | `m_window`, `m_elements`, `m_instance` |
| Enumerators | `PascalCase` | `TextStyle::BodyLarge` |
| Namespaces | `lower_case` | `imlui::md`, `imlui::luaui` |
| Modules | lowercase, dotted | `imlui.widgets.text_field` |
| Files | `snake_case`, named after the module's last segment | `text_field.cppm` |

- **`m_` is the only prefix.** Don't use `k`, `g_`, `s_`, `p`, `sz`, or `_` suffixes. Constants are `camelCase`
  (`hugeExtent`, not `kHuge`); file-local state is `camelCase` in an anonymous namespace (`pendingTheme`,
  not `g_pending`).
- Classes with behavior keep their data **private** (so it carries `m_`). Plain aggregates and value types
  whose fields *are* the interface (`Color{r, g, b, a}`, `Theme`, `TextFieldOptions`) use public fields
  without a prefix.
- Mirrored C APIs may keep their original casing when it is already a single lowercase word
  (`lua::gettop`, `lua::pushvalue`). Compound names become `camelCase` (`lua::toStringAny`).

## Formatting

- `.clang-format`: 4-space indent, 120 columns, braces on the same line, `T* p` / `T& r`.
- Short accessors may stay on one line.
- Prefer `[[nodiscard]]` on queries, `explicit` on single-argument constructors, and `std::string_view` for
  read-only string parameters.
- Comments explain *why*. Public declarations get a one-line `///` summary when the name isn't enough.
