# Getting started

## Prerequisites

imlui is developed on Windows with clang. Other compilers aren't tested, and CMake warns if the compiler isn't
Clang.

| Tool | Notes |
|---|---|
| **LLVM / clang** | `clang` and `clang++` must be on `PATH` (the presets set `CMAKE_C_COMPILER=clang`, `CMAKE_CXX_COMPILER=clang++`). It needs to be recent enough for C++20 modules with CMake's module scanning. |
| **CMake ≥ 3.28** | Needed for `FILE_SET CXX_MODULES`. |
| **Ninja** | The presets use the Ninja generator, which CMake requires for C++ modules. |
| **Visual Studio or Build Tools, with a Windows SDK** | On Windows, clang uses the MSVC STL and the Windows SDK libraries. If clang can't find them, configure from a *Developer PowerShell for VS*. |
| OpenGL | Provided by the system driver (`find_package(OpenGL)`). |

You don't install anything else. Dear ImGui 1.92.9b, Lua 5.4.8, GLFW 3.4 and spdlog 1.15.3 are vendored in
`external/` and built as plain static libraries. Roboto Regular/Medium and Material Icons Round are in
`external/fonts/`.

## Configure and build

```sh
cmake --preset clang-debug            # or clang-release
cmake --build --preset clang-debug    # same as: cmake --build build/clang-debug
```

| Preset | Build dir | Build type |
|---|---|---|
| `clang-debug` | `build/clang-debug` | Debug |
| `clang-release` | `build/clang-release` | Release |

The app is `build/<preset>/bin/imlui_app` (`.exe` on Windows). After each build, `lua/`, the fonts and the
Lua-only examples (`examples/gallery/`, `examples/counter/`) are copied next to the executable, so the `bin/` folder
runs on its own. Examples with C++ code have their own executables (see [examples/README.md](../examples/README.md)).

Every `src/**/*.cppm` file is picked up by a `CONFIGURE_DEPENDS` glob, so adding or removing a module needs no
CMake edits. `compile_commands.json` is always exported, which clangd, clice and clang-tidy use.

## Run

```sh
build/clang-debug/bin/imlui_app                              # the gallery (examples/gallery/main.lua)
build/clang-debug/bin/imlui_app examples/counter/main.lua    # any script
```

A relative script path is taken relative to the current directory. If no such file exists there, it is looked up
under the source tree (dev builds) or next to the executable, so `imlui_app examples/counter/main.lua` works from
any directory.

```sh
build/clang-debug/bin/imlui_custom_types                     # an example with C++ types (docs/extending.md)
```

The gallery shows every element and has a live theme editor on its *Theme* page. Set `GALLERY_PAGE=n` to open
page `n` at startup, which helps for screenshots and tests.

The window starts hidden. It appears once the script has loaded, with the title and size taken from the root
`ApplicationWindow`.

## Hot reload

While the app runs, the engine checks every 400 ms for changed `.lua` files. It scans the Lua path (`lua/`) and the
script's own directory, including subfolders. When a file changes, it builds a **fresh Lua state** and runs the
script again:

- Lua state (all `state{}` tables, element-local values, timers) starts over, and the theme resets to the default
  before the script runs.
- The window size is applied again only if the script changed `width`/`height`.
- If the new script fails to load, the **previous UI keeps running**. The error appears in an overlay at the
  bottom of the window, with **Dismiss** and **Reload** buttons.

You can also reload from Lua with `reload()`, as the gallery's *More → Reload script* menu does.

## Dev paths (`IMLUI_DEV_PATHS`)

```sh
cmake --preset clang-debug -DIMLUI_DEV_PATHS=OFF
```

| `IMLUI_DEV_PATHS` | Effect |
|---|---|
| `ON` (default) | The app is compiled with `IMLUI_SOURCE_DIR` set to the source tree. `lua/imlui.lua` and the default script (`examples/gallery/main.lua`) are read from the source tree, and so are relative script paths such as `examples/counter/main.lua`, so editing them hot-reloads without rebuilding. |
| `OFF` | Everything is read from the copies next to the executable. Use this for a build you move or ship. |

Fonts are always loaded from `fonts/` next to the executable.

## Your first script

Create `hello.lua` anywhere:

```lua
-- hello.lua
local s = state { name = "", clicks = 0 }

theme { seed = "#0061A4", dark = false }

return ApplicationWindow {
    title = "Hello", width = 480, height = 320,

    Column {
        spacing = 16,
        Text { text = "Hello, imlui", style = "headlineMedium" },
        TextField { label = "Your name", leadingIcon = "person", text = bind(s, "name") },
        Row {
            spacing = 8,
            Button {
                text = "Greet", icon = "waving_hand",
                enabled = function() return s.name ~= "" end,
                onClicked = function()
                    s.clicks = s.clicks + 1
                    snackbar("Hi, " .. s.name .. "!")
                end,
            },
            Text { text = function() return ("%d greetings"):format(s.clicks) end, color = "onSurfaceVariant" },
        },
    },
}
```

```sh
build/clang-debug/bin/imlui_app path/to/hello.lua
```

How it works:

- The script **returns the root element**, usually an `ApplicationWindow`.
- Every element type (`Column`, `Button`, ...) and every helper (`state`, `bind`, `theme`, `snackbar`, ...) is a
  global in the script. See [Lua API](lua-api.md).
- `function() ... end` property values are **bindings** that are evaluated again every frame. `bind(s, "name")`
  is a **two-way binding**: typing in the field writes `s.name`.
- To reuse a piece of UI, put it in its own file next to the script and `require` it. See
  `examples/gallery/StatCard.lua` and [components](lua-api.md#components-and-require).

Next: [Elements](elements.md) for all props, and [Theming](theming.md) for colors and typography.
