# imlui documentation

imlui is a declarative, QML-like UI toolkit. You describe the whole UI in **Lua**, including the window
title and size, layout, state and theme. A **C++20-modules** backend renders that description every frame
with **Dear ImGui 1.92** and a **Material 3** look: a seed-generated color scheme, the M3 type scale, Roboto,
Material Icons, state layers and disabled colors.

```lua
local s = state { count = 0 }
theme { seed = "#006A6A", dark = false }

return ApplicationWindow {
    title = "Counter", width = 420, height = 260,
    Column {
        spacing = 16,
        Text { text = function() return "Clicked " .. s.count .. " times" end, style = "headlineSmall" },
        Button { text = "Increment", onClicked = function() s.count = s.count + 1 end },
    },
}
```

Save the script while the app is running and it reloads immediately.

## Pages

| Page | What it covers |
|---|---|
| [Getting started](getting-started.md) | Prerequisites, using imlui in your app (git submodule, `add_subdirectory`, `main.cpp`, `resources.lua`), the imlui-gallery example, hot reload, `IMLUI_DEV_PATHS`, shipping, a first script, building imlui on its own |
| [Architecture](architecture.md) | Layers, the frame loop, property resolution, signals, inline containers, hot reload, the module map, third-party wrappers |
| [Lua API](lua-api.md) | The script environment: `state`, `watch`, `bind`, `component`, `Repeater`/`ListView`/`If`/`Loader`, theme functions, timers, snackbar, logging, `native` |
| [Elements](elements.md) | Every built-in element: props, defaults, signals, two-way props, common props, the Canvas painter |
| [Registering C++ types](extending.md) | Extending imlui from C++, QML style: element types (`Element`, `TypeBuilder`, `Frame`), properties, signals, methods, singletons, `LuaConvert`, lifetime, threading, ticking elements, the built-ins as examples, the example in imlui-gallery |
| [Theming](theming.md) | Material 3 scheme generation, color roles, the `theme{}` table, color specs, typography, icons, state layers |
| [Development](development.md) | Code style, module conventions, adding elements and native functions, logging, clang-tidy, clice, tests, scripted UI tests |
| [Status / changelog](changelog.md) | What exists today and what is in progress |

The C++ code style lives in [`../CODE_STYLE.md`](../CODE_STYLE.md).
