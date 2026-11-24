# Getting started

imlui is a library. An app vendors this repository as a git submodule, adds it to its CMake build with
`add_subdirectory`, and links the `imlui` target: a few lines of C++ host (`main.cpp`) plus the app's Lua scripts,
embedded from a resource manifest (`src/resources/resources.lua`). The [imlui-gallery](#the-example-app-imlui-gallery)
repository is the complete example.

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

You don't install anything else. Dear ImGui 1.92.9b, Lua 5.4.8, GLFW 3.4, spdlog 1.15.3, stb_image 2.34 and zstd
1.5.7 are vendored in imlui's `external/` and built as plain static libraries. Roboto Regular/Medium and Material
Icons Round are in `external/fonts/`, and the library embeds them.

## Using imlui in your app

An app's repository looks like this (the names other than `vendor/imlui` are up to you):

```
my_app/
  CMakeLists.txt        project setup, add_subdirectory(vendor/imlui), add_subdirectory(src)
  CMakePresets.json     clang-debug, clang-release, clang-asan (copy imlui's)
  src/
    CMakeLists.txt      the app's targets and imlui_add_resources
    main.cpp            the host, and the app's own C++ modules (registered types)
    resources/
      resources.lua     the resource manifest: which files are embedded as app:/
      main.lua          the entry script, plus components, images, data
  vendor/imlui/         this repository, as a git submodule
```

imlui is laid out the same way: its targets are in `src/CMakeLists.txt` and its own resources in `src/resources/`.
Keeping the resources in their own folder means a manifest pattern like `**` can't pick up C++ sources, and the
module globs (`*.cppm`) don't see the scripts.

### 1. Add imlui as a submodule

```sh
git submodule add -b material-ui <imlui repository URL> vendor/imlui
```

A clone of the app then needs `git clone --recursive`, or `git submodule update --init` after a plain clone. To
move to a newer imlui: `git submodule update --remote vendor/imlui`, build, and commit the new submodule commit.

### 2. CMakeLists.txt and src/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Optional: IMLUI_SANITIZERS (the clang-asan preset) instruments the app's targets too, not only imlui's.
include(vendor/imlui/cmake/ImluiSanitizers.cmake)

add_subdirectory(vendor/imlui)
add_subdirectory(src)
```

```cmake
# src/CMakeLists.txt: paths are relative to src/
add_executable(my_app main.cpp)
target_sources(my_app PRIVATE FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES my_type.cppm) # if any
target_link_libraries(my_app PRIVATE imlui)
imlui_set_warnings(my_app)   # imlui's warning policy: strict flags, -Werror
imlui_set_hardening(my_app)  # -ftrapv in Debug
imlui_add_resources(my_app resources/resources.lua NAME my_app)  # generates module imlui.embedded.my_app
```

`add_subdirectory(vendor/imlui)` gives the parent project:

| What | Notes |
|---|---|
| target `imlui` | The library, with its C++20 modules (`import imlui;`, also `imgui` and the other wrappers) and its own embedded files (`src/resources/imlui.lua`, the fonts). Linking it is enough, also for modules that `import imgui;`. |
| `imlui_add_resources(<target> <manifest> [NAME <name>] [EMBED ON\|OFF])` | Embeds the files a manifest lists into `<target>` as module `imlui.embedded.<name>` ([Shipping an app](#shipping-an-app)); in dev builds it only records their folders ([Dev and ship builds](#dev-and-ship-builds)). `EMBED` overrides `IMLUI_EMBED_RESOURCES` for this call. |
| `imlui_add_pak(<name> <manifest> [OUTPUT <file>])` | A target that builds a `.pak` archive (default: `<name>.pak` in `CMAKE_RUNTIME_OUTPUT_DIRECTORY`). |
| `imlui_set_warnings(<target>)`, `imlui_set_hardening(<target>)` | imlui's warning policy and Debug hardening ([CODE_STYLE.md](../CODE_STYLE.md#warnings-and-compiler-checks)). |
| targets `imlui_rc`, `imlui_pack` | The build-time resource tools the functions above run; built on demand. |
| option `IMLUI_DEV_PATHS` (default `ON`) | Dev builds bake each embedded group's source folder in, for hot reload; `OFF` for a build you ship ([Dev and ship builds](#dev-and-ship-builds)). Applies to the app's `imlui_add_resources` too. |
| option `IMLUI_EMBED_RESOURCES` (default: the opposite of `IMLUI_DEV_PATHS`) | Whether `imlui_add_resources` embeds the files: `OFF` in dev builds, so editing a resource needs no build; `ON` for a build you ship. `OFF` needs `IMLUI_DEV_PATHS=ON`. |
| option `IMLUI_BUILD_TESTS` | Default `OFF` as a subproject (`ON` when imlui is built on its own). |
| cache variable `IMLUI_SANITIZERS` | e.g. `address,undefined` (see `cmake/ImluiSanitizers.cmake`). |

imlui doesn't set `CMAKE_RUNTIME_OUTPUT_DIRECTORY` or `CMAKE_EXPORT_COMPILE_COMMANDS` when it isn't the top-level
project, and has no `tidy` target then: those are the app's choice.

### 3. main.cpp

The host names no file paths. It passes the app's embedded packages to `defaultConfig`, registers its C++ types and
singletons (if any, see [Registering C++ types](extending.md)) and runs:

```cpp
// src/main.cpp: module-only translation unit (no #include)
import imlui;
import imlui.embedded.my_app;

int main(int argc, char** argv) {
    imlui::Engine engine{imlui::defaultConfig(argc, argv, imlui::embedded::my_app::packages())};
    // engine.registerType<my_app::Gauge>("Gauge");
    // engine.registerSingleton("backend", backend);
    return engine.run();
}
```

### 4. resources.lua and the scripts

```lua
-- src/resources/resources.lua: the app's files, embedded as app:/ (app:/main.lua is the entry script)
return {
    prefix = "app",
    base = ".", -- this folder; the manifest itself is never matched by a pattern
    include = { "**/*.lua", "**/*.png", "**/*.jpg" },
}
```

`src/resources/main.lua` returns the root element; see [Your first script](#your-first-script). A file added under
`src/resources/` is embedded on the next build; no CMake change. The manifest format (groups, mount folders, `exclude`,
`compress`) is in [Architecture](architecture.md#resource-manifests).

### 5. Build and run

```sh
cmake --preset clang-debug
cmake --build build/clang-debug
build/clang-debug/bin/my_app
```

The executable carries everything inside: the library's Lua and fonts, and the app's files. It runs on its own,
copied anywhere.

## The example app: imlui-gallery

The `imlui-gallery` repository (a separate repository, usually checked out next to this one as `../imlui-gallery`)
is the example app, laid out as above:

- `src/resources/main.lua`: the app shell: the theme, adaptive navigation (drawer, rail, modal drawer by window
  size class) and the page list.
- `src/resources/pages/*.lua`: one file per page, together showing every built-in element and a live theme editor;
  `layout.lua` and `theme_state.lua` are shared by the pages, `StatCard.lua` is a component and `landscape.png` an
  image, all listed in `src/resources/resources.lua`.
- `src/CMakeLists.txt`: the `imlui_gallery` target and its embedded resources (module `imlui.embedded.gallery`).
- `src/gauge.cppm` and `src/system_info.cppm`: a C++ element type (`Gauge`) and a singleton (`backend`), shown on
  the gallery's *C++ types* page. See [Registering C++ types](extending.md#the-example-app).
- `vendor/imlui`: this repository as a submodule.

```sh
git clone --recursive <imlui-gallery URL>
cd imlui-gallery
cmake --preset clang-debug
cmake --build build/clang-debug
build/clang-debug/bin/imlui_gallery
```

Set `GALLERY_PAGE=n` to open page `n` at startup, which helps for screenshots and tests. Its README covers updating
the submodule and pointing it at another imlui checkout.

The window starts hidden. It appears once the script has loaded, with the title and size taken from the root
`ApplicationWindow`.

## Command line

`my_app [script.lua]`: a script argument replaces the app's own `app:/` files. Its folder is mounted at `app:/`
and it becomes the entry script. A relative path is taken relative to the current directory, and when no such file
exists there, relative to the executable's folder. Any imlui app can so run another script, which is handy for
trying one out: `build/clang-debug/bin/imlui_gallery path/to/hello.lua`.

## Hot reload

While the app runs, the engine checks every 400 ms for changed `.lua` files: those in the app's folders (`app:/`,
including subfolders) and the library (`imlui:/`, `src/resources/`), and every file a script read through `ui.resource`.
When a file changes, it builds a **fresh Lua state** and runs the script again:

- Lua state (all `state{}` tables, element-local values, timers) starts over, and the theme resets to the default
  before the script runs.
- The window size is applied again only if the script changed `width`/`height`.
- If the new script fails to load, the **previous UI keeps running**. The error appears in an overlay at the
  bottom of the window, with **Dismiss** and **Reload** buttons.

You can also reload from Lua with `reload()`, as the gallery's *More → Reload script* menu does.

## Dev and ship builds

Two options decide where a build reads its files from; the second follows the first unless you set it:

```sh
cmake --preset clang-debug                                     # dev: source folders, nothing embedded
cmake --preset clang-release -B build/ship -DIMLUI_DEV_PATHS=OFF # ship: everything embedded, no source paths
```

| | `IMLUI_DEV_PATHS` | `IMLUI_EMBED_RESOURCES` | Files come from |
|---|---|---|---|
| Dev build (default, also the `clang-release` preset) | `ON` | `OFF` | the source folders: editing a Lua file, an image or a font hot-reloads and needs no build (`ninja: no work to do`) |
| Build you ship | `OFF` | `ON` | the executable alone, plus whatever `.pak` or folder you put next to it |
| Dev build that embeds (`-DIMLUI_EMBED_RESOURCES=ON`) | `ON` | `ON` | the source folders over the embedded copies; editing a file re-embeds on the next build. Run it with the environment variable `IMLUI_DEV_PATHS=0` to try it as it would ship |
| (error at configure) | `OFF` | `OFF` | nothing: the configure stops |

The presets don't set either option, so `clang-release` is an optimized dev build. Configure a build you ship in a
fresh build folder with `-DIMLUI_DEV_PATHS=OFF` (`IMLUI_EMBED_RESOURCES` then defaults to `ON`); in a folder that was
configured as a dev build, also pass `-DIMLUI_EMBED_RESOURCES=ON`, as the cached `OFF` stays. A dev build's
executable needs the source tree: moved elsewhere, it logs an error naming each missing source folder.
`imlui_add_resources(... EMBED ON)` embeds one manifest whatever the option says (a test that runs an executable
alone, say).

| `IMLUI_DEV_PATHS` | Effect |
|---|---|
| `ON` (default) | Every package remembers the folder its files come from (the `base` of its group in the resource manifest: the app's `src/resources/`, imlui's `src/resources/` and `external/fonts/`, ...), and `defaultConfig` mounts those folders (over the embedded copies, if any), so editing them hot-reloads without rebuilding. In an app that's the app's own `src/resources/`, and `vendor/imlui/src/resources/` and `vendor/imlui/external/fonts/` for the library. Setting the environment variable `IMLUI_DEV_PATHS=0` ignores them at runtime where the files are embedded. |
| `OFF` | No source paths are compiled in: the app runs from its embedded files. Use this for a build you move or ship. |

Either way, files next to the executable override the embedded ones: any `*.pak` archive, which mounts itself where
it was built for (see [Shipping an app](#shipping-an-app)), and, above those, a folder named after a prefix: `app/`
for `app:/` (the app's files) and `imlui/` for `imlui:/` (the library's Lua side; `imlui/fonts/` for the fonts).
The app's files are [resources](lua-api.md#resources); the entry script is always `app:/main.lua` (or the script
given on the command line).

## Shipping an app

The app lists its scripts and data in its resource manifest and embeds them with `imlui_add_resources`, as above.
Text and fonts are stored zstd-compressed and decompressed on first read. Configure the build you ship with
`-DIMLUI_DEV_PATHS=OFF`, so it holds no source paths and embeds every file (`IMLUI_EMBED_RESOURCES` defaults to `ON`
then); the executable then needs nothing next to it.

An `app/` folder next to the executable overrides single embedded `app:/` files. To update files without
rebuilding, ship a `.pak` next to the executable: `imlui_add_pak(patch patch/resources.lua)` in CMake (a manifest
like the one above), or `imlui_pack patch.pak app:/ <folder>` by hand. Archives override embedded files, and loose
folders override both (see [Architecture](architecture.md#embedding-and-archives)).

## Your first script

`src/resources/main.lua` of a new app, or a script anywhere, run with `my_app path/to/hello.lua`:

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

How it works:

- The script **returns the root element**, usually an `ApplicationWindow`.
- Every element type (`Column`, `Button`, ...) and every helper (`state`, `bind`, `theme`, `snackbar`, ...) is a
  global in the script. See [Lua API](lua-api.md).
- `function() ... end` property values are **bindings** that are evaluated again every frame. `bind(s, "name")`
  is a **two-way binding**: typing in the field writes `s.name`.
- To reuse a piece of UI, put it in its own file next to the script and `require` it. See the gallery's
  `src/resources/StatCard.lua` and [components](lua-api.md#components-and-require).

Next: [Elements](elements.md) for all props, and [Theming](theming.md) for colors and typography.

## Building imlui on its own

On its own, imlui builds the library, the resource tools and the tests (no app):

```sh
cmake --preset clang-debug            # or clang-release, clang-asan
cmake --build --preset clang-debug    # same as: cmake --build build/clang-debug
ctest --preset clang-debug
```

| Preset | Build dir | Build type |
|---|---|---|
| `clang-debug` | `build/clang-debug` | Debug |
| `clang-release` | `build/clang-release` | Release |
| `clang-asan` | `build/clang-asan` | Debug + AddressSanitizer + UBSan |

Every `src/**/*.cppm` file is picked up by a `CONFIGURE_DEPENDS` glob (in `src/CMakeLists.txt`), so adding or removing a
module needs no CMake edits. `compile_commands.json` is exported, which clangd, clice and clang-tidy use. To try a
library change in a real app, see [Development](development.md#trying-changes-in-an-app).
