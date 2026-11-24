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

You don't install anything else. Dear ImGui 1.92.9b, Lua 5.4.8, GLFW 3.4, spdlog 1.15.3 and zstd 1.5.7 are vendored
in `external/` and built as plain static libraries. Roboto Regular/Medium and Material Icons Round are in
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

The app is `build/<preset>/bin/imlui_app` (`.exe` on Windows). It carries its files inside: the library embeds
`lua/imlui.lua` and the fonts, and `imlui_app` embeds the gallery, its default app. The executable runs on its own,
copied anywhere. The Lua-only examples (`examples/gallery/`, `examples/counter/`) are also copied next to it after
each build, so `imlui_app examples/counter/main.lua` works in a release build too. Examples with C++ code have their
own executables, which embed their scripts the same way (see [examples/README.md](../examples/README.md)).

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

While the app runs, the engine checks every 400 ms for changed `.lua` files: those in the script's folder
(`app:/`, including subfolders) and the library (`imlui:/`, `lua/`), and every file a script read through
`ui.resource`. When a file changes, it builds a **fresh Lua state** and runs the script again:

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
| `ON` (default) | The app is compiled with `IMLUI_SOURCE_DIR` set to the source tree. `lua/` and the default script's folder (`examples/gallery/`) are mounted from the source tree over the embedded copies, and relative script paths such as `examples/counter/main.lua` are found there, so editing them hot-reloads without rebuilding. Setting the environment variable `IMLUI_DEV_PATHS=0` ignores the source tree at runtime. |
| `OFF` | The app runs from its embedded files. Use this for a build you move or ship. |

Either way, files next to the executable override the embedded ones: a `lua/` folder (`imlui:/`), a `fonts/` folder
(`imlui:/fonts/`), the default script's folder (`examples/gallery/` for `imlui_app`), and any `*.pak` archive, which
mounts itself where it was built for (see [Shipping an app](#shipping-an-app)). The app's files are
[resources](lua-api.md#resources): the script's folder is `app:/`, `lua/` is `imlui:/`.

## Shipping an app

An app with its own host embeds its scripts and data with `imlui_add_resources`, like `examples/custom_types`:

```cmake
imlui_add_resources(my_app NAME my_app PREFIX app BASE ${CMAKE_CURRENT_SOURCE_DIR} FILES main.lua
                    DIRECTORY components assets)
```

```cpp
import imlui;
import imlui.embedded.my_app;

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, nullptr, "app/main.lua", imlui::embedded::my_app::mount)};
    return engine.run();
}
```

The executable then needs nothing next to it. Text and fonts are stored zstd-compressed and decompressed on first
read. The default script is `app/main.lua` relative to the executable, so an `app/` folder next to it overrides
single embedded `app:/` files. To update files without rebuilding, ship a `.pak` next to the executable:
`imlui_add_pak(patch PREFIX app BASE ... FILES ...)` in CMake, or `imlui_pack patch.pak app:/ <folder>` by hand.
Archives override embedded files, and loose folders override both (see
[Architecture](architecture.md#embedding-and-archives)).

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
