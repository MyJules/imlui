# Examples

Each example is one self-contained folder holding all of its code. Its entry script is `main.lua`; components sit
next to it and are loaded with `require` (which searches the script's folder).

| Example | Shows | Run |
|---|---|---|
| [`gallery/`](gallery/) | Every built-in element, adaptive navigation and a live theme editor. `StatCard.lua` is a reusable component (`require "StatCard"`). `landscape.png` is the image the Canvas page shows. | `imlui_app` (its embedded app) or `imlui_app examples/gallery/main.lua` |
| [`counter/`](counter/) | The smallest useful app: state, a binding and a button. | `imlui_app examples/counter/main.lua` |
| [`custom_types/`](custom_types/) | Registering C++ types: a `Gauge` element (`gauge.cppm`) and a `backend` singleton fed by a worker thread (`system_info.cppm`). See [Registering C++ types](../docs/extending.md#the-example-app). | target `imlui_custom_types`, then `imlui_custom_types` |

```sh
cmake --build build/clang-debug                       # builds imlui_app and the example executables
build/clang-debug/bin/imlui_app                       # gallery
build/clang-debug/bin/imlui_app examples/counter/main.lua
build/clang-debug/bin/imlui_custom_types
```

Lua-only examples run on the generic `imlui_app` host. They need no build step: `imlui_app` copies every example
folder without a `CMakeLists.txt` next to its binary. A relative script path is tried relative to the current
directory first (run from the repository root to edit the source copy with hot reload), then relative to the
binary's folder (the copy). The gallery is also embedded into `imlui_app` as its app (`app:/main.lua`), so
`imlui_app` with no argument runs on its own; in dev builds `examples/gallery/` is mounted over the embedded copy,
so editing `main.lua`, `StatCard.lua` or `landscape.png` hot-reloads.

Examples with C++ code have their own `CMakeLists.txt` and executable, added by `examples/CMakeLists.txt` when
`IMLUI_BUILD_EXAMPLES` is `ON` (the default). They embed their scripts with `imlui_add_resources`, so the executable
needs nothing next to it (`custom_types/` embeds its `main.lua`). To add one, create a folder like `custom_types/`
and add an `add_subdirectory` line to [`CMakeLists.txt`](CMakeLists.txt).
