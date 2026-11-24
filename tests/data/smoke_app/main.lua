-- The smoke app: run by imlui_smoke_host (tests/smoke_host/main.cpp) from its embedded data alone (the smoke CTest,
-- tests/smoke_run.cmake). It touches what a shipped app needs: the library's Lua and fonts, an icon, a component
-- (Tile.lua), a resource read, an image from a second manifest group, and a C++ type the host registers (Probe).
local Tile = require "Tile"

local s = state { clicks = 0 }

theme { seed = "#006A6A", dark = true }

local source = ui.resource.read("Tile.lua")
assert(source and source:find("component", 1, true), "app:/Tile.lua isn't readable")

return ApplicationWindow {
    title = "imlui smoke test", width = 480, height = 360,
    Column {
        spacing = 12,
        Text { text = "Smoke test", style = "headlineSmall" },
        Row {
            spacing = 8,
            Tile("Clicks", function() return tostring(s.clicks) end),
            Tile("Frame rate", function() return ("%.0f fps"):format(fps()) end),
        },
        Button { text = "Click", icon = "check", onClicked = function() s.clicks = s.clicks + 1 end },
        Image { source = "images/rgba_4x2.png", width = 64, height = 32 },
        Probe {},
    },
}
