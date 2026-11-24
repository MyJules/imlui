-- The smoke app: run by imlui_smoke_host (tests/smoke_host/main.cpp) from its embedded data alone (the smoke CTest,
-- tests/smoke_run.cmake), and from its source folders, its embedded data and a .pak by smoke.modes
-- (tests/smoke_modes.cmake), which compares what each run prints. It touches what a shipped app needs: the
-- library's Lua and fonts, an icon, components (Tile.lua, widgets/Label.lua in a nested folder), resource reads, an
-- image from a second manifest group, and a C++ type the host registers (Probe).
local Tile = require "Tile"
local Label = require "widgets.Label"

local s = state { clicks = 0 }

theme { seed = "#006A6A", dark = true }

local source = ui.resource.read("Tile.lua")
assert(source and source:find("component", 1, true), "app:/Tile.lua isn't readable")

-- What the app sees of its resources, as `summary:` lines that smoke.modes compares across the runs: each name with
-- the size read (or "missing"), whether it exists, and whether it must. A file the manifest selects is served in
-- every mode, one it doesn't select in none, whatever lies on disk; names are case-sensitive in every mode.
local expected = {
    { "resources.lua", true },              -- the manifest, listed in its own `files`
    { "data/notes.txt", true },             -- listed in `files`, in a folder
    { "widgets/Label.lua", true },          -- matched by `include` in a nested folder
    { "images/rgba_4x2.png", true },        -- the second group, from another folder
    { "hidden.txt", false },                -- on disk, not selected
    { "data/unlisted.txt", false },         -- on disk next to a listed file, not selected
    { "scratch/draft.lua", false },         -- matched by `include`, removed by `exclude`
    { "scratch", false },                   -- a folder holding no selected file
    { "data/Notes.txt", false },            -- a listed file, spelled with another case
    { "images/smoke_app/main.lua", false }, -- the image group's folder holds the app, but selects only the image
}
for _, e in ipairs(expected) do
    local name, shipped = e[1], e[2]
    local text = ui.resource.read(name)
    local exists = ui.resource.exists(name)
    print(("summary: lua %s read=%s exists=%s"):format(name, text and #text or "missing", tostring(exists)))
    assert(exists == shipped, ("app:/%s: exists() is %s, expected %s"):format(name, tostring(exists), tostring(shipped)))
end
local manifest = ui.resource.read("resources.lua")
assert(manifest and manifest:find("prefix = \"app\"", 1, true), "app:/resources.lua isn't the manifest")

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
        Label(ui.resource.read("data/notes.txt")),
        Button { text = "Click", icon = "check", onClicked = function() s.clicks = s.clicks + 1 end },
        Image { source = "images/rgba_4x2.png", width = 64, height = 32 },
        Probe {},
    },
}
