# C++ code style

These rules apply to all C++ in `src/`, `tools/` and `tests/`, and to apps built on imlui (they use
`imlui_set_warnings` too). Vendored code in `external/` keeps its own style.
`.clang-format` and `.clang-tidy` (naming checks) at the repo root enforce most of them.

## Language and modules

- C++20, **modules only**. Headers are included only in the global module fragment
  (`module;` ... `export module x;`) of a module, and only third-party headers.
- The standard library is **`import std;`**, never `#include <string>` and the like: the first import after the module
  declaration (`cmake/ImluiStd.cmake` builds the module). `import std` exports no macros and nothing in the global
  namespace, so a file that needs a C macro (`EOF`, `SIZE_MAX`) or a non-standard C function (`_putenv_s`,
  `setenv`) includes the C header for it (`<stdio.h>`, `<stdlib.h>`) in its global module fragment and says why.
- An app's `main.cpp`, `tests/smoke_host/main.cpp` and every other non-module file use `import` only. They never `#include`.
  **Exception:** test files in `tests/` include `<gtest/gtest.h>`, because GoogleTest's `TEST`/`EXPECT_*` macros
  can't be exported from a module. They include nothing else and import everything they test (`import std;` after
  the include: clang can merge a header's declarations into an imported `std`, not the other way round).
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
export module imlui.example;  // a global module fragment (`module;` + #include) only for third-party headers

import std;

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

## Warnings and compiler checks

**Zero warnings.** Our targets (`imlui`, the tools, `imlui_tests`, `imlui_smoke_host`, and an app's own targets) build with `-Werror` and the
`COMPILE_WARNING_AS_ERROR` property, so any warning fails the build, in every configuration. Release can surface
warnings Debug doesn't, so build both. The flags live in `cmake/ImluiWarnings.cmake` (`imlui_set_warnings`):

| Flag | Why |
|---|---|
| `-Wall -Wextra -Wpedantic` | The baseline, plus standard conformance (no compiler extensions). |
| `-Werror` | Warnings don't pile up. |
| `-Wshadow` | A local that hides another name is a classic source of wrong-variable bugs. Rename it. |
| `-Wconversion -Wsign-conversion` | Narrowing and signed/unsigned mixups are silent value changes. Write `static_cast` where the conversion is intended, or use the right type (`std::size_t` for indices). |
| `-Wdouble-promotion` | `float` to `double` is usually accidental. `lua::pushnumber(static_cast<double>(f))` says it isn't. |
| `-Wold-style-cast` | C casts can do any of `static_cast`/`const_cast`/`reinterpret_cast` without saying which. |
| `-Wcast-align` | Casts that increase alignment are UB on access. |
| `-Wnon-virtual-dtor -Woverloaded-virtual` | Polymorphic classes need a virtual destructor, and overloads mustn't hide base virtuals. |
| `-Wnull-dereference` | Paths the compiler can prove dereference null. |
| `-Wformat=2` | printf-style formats must be literals that match their arguments. |
| `-Wimplicit-fallthrough` | A `switch` case that falls through needs `[[fallthrough]]`. |
| `-Wunused -Wextra-semi` | Dead variables and stray `;`. Unused parameters are unnamed or `[[maybe_unused]]`, never silenced globally. |
| `-Wmissing-declarations` | A non-exported function belongs in an anonymous namespace (see above); otherwise it's exported. |

Rules:

- **Fix the code, don't silence the warning.** There's no `-Wno-*` for our targets.
- **Suppressions only at the narrowest scope**, with a comment that says why the code is right and the warning
  can't be avoided:

  ```cpp
  // The format is the Slider's `format` prop: a printf format chosen by the script, ...
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wformat-nonliteral"
  std::snprintf(buf, sizeof buf, valueFormat, static_cast<double>(*v));
  #pragma clang diagnostic pop
  ```

- **Third-party headers are SYSTEM.** Every target in `external/CMakeLists.txt` has the `SYSTEM` property, so
  its include directories reach us as `-isystem` and our flags don't fire inside vendored headers or their
  macros. Vendored code keeps its own flags.
- **Debug hardening** (`imlui_set_hardening`): our targets build with `-ftrapv` in Debug, so signed overflow
  traps. Debug uses `/MDd`, which turns on the MSVC STL's checked iterators (`_ITERATOR_DEBUG_LEVEL=2`).
  Vendored Lua doesn't get `-ftrapv`, because it relies on wrapping integer arithmetic.
- **Run the sanitizer preset before merging larger changes** (`cmake --preset clang-asan`, then build and
  `ctest --preset clang-asan`, and start the app once). It builds everything, vendored libraries included, with
  AddressSanitizer and UndefinedBehaviorSanitizer. See [Development](docs/development.md#building-and-testing).
