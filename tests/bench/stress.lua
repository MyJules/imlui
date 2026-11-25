-- The stress scenarios of imlui_bench (tests/bench/main.cpp): lots of elements at once, ported from the gallery app's
-- stress page. Runs in the UI script environment (ui.env) of a test::HeadlessFrame. Defines the global `stress`:
--
--   stress.scenarios           the scenario names, in order
--   stress.build(name, count)  the root element: a page-like ScrollView holding the scenario, with a Timer that bumps
--                              stress.s.tick every frame while stress.s.animate is true (the harness's frames are
--                              1/60 s apart, longer than its 16 ms interval)
--   stress.s                   state { tick, animate }: every item reads `tick` through a binding

local s = state { tick = 0, animate = false }

-- A value that moves while animating; each item reads it, so every binding is evaluated every frame.
local function phase(i) return ((s.tick + i * 7) % 100) / 100 end

-- A Grid of cards, each with text, a button, a switch, a slider and a progress bar.
local function tiles(count)
    return Grid {
        columns = 6, spacing = 12,
        Repeater {
            model = function() return count end,
            delegate = function(i)
                local item = state { on = i % 3 == 0, level = (i % 10) / 10 }
                return Card {
                    variant = "outlined", padding = 12, spacing = 6,
                    Text { text = "Tile " .. i, style = "titleSmall" },
                    Button { text = "Open", variant = "tonal", fillWidth = true },
                    Switch { checked = bind(item, "on") },
                    Slider { value = bind(item, "level") },
                    ProgressBar { value = function() return phase(i) end, fillWidth = true },
                }
            end,
        },
    }
end

-- Rows of mixed controls, each bound to its own state.
local function controls(count)
    return Column {
        spacing = 4,
        Repeater {
            model = function() return count end,
            delegate = function(i)
                local item = state { done = i % 4 == 0, picked = false, amount = i % 100, on = false }
                return Row {
                    spacing = 12,
                    Checkbox { checked = bind(item, "done") },
                    Text { text = function() return ("Row %d · %.2f"):format(i, phase(i)) end, width = 140 },
                    Chip { "Tag " .. (i % 9), checked = bind(item, "picked") },
                    Slider { value = bind(item, "amount"), from = 0, to = 100, stepSize = 1 },
                    IconButton { icon = "star", checked = bind(item, "on") },
                    Switch { checked = bind(item, "on"), icons = false },
                    CircularProgress { value = function() return phase(i) end, size = 24 },
                }
            end,
        },
    }
end

-- A ListView with ten items per count unit.
local function list(count)
    return ListView {
        height = 560, spacing = 0, border = true,
        model = function() return count * 10 end,
        delegate = function(i)
            return ListItem {
                "Item " .. i, supportingText = "Second line of item " .. i,
                icon = "description", trailingText = tostring(i % 60) .. " min",
            }
        end,
    }
end

-- A Table with a hundred rows per count unit. Only the rows in view are drawn.
local function tableView(count)
    local rows = {}
    for i = 1, count * 100 do
        rows[i] = { i, "Name " .. i, ("%.2f"):format((i * 37 % 1000) / 10), (i % 2 == 0) and "yes" or "no" }
    end
    return Table {
        columns = { "#", "Name", "Value", "Even" },
        rowHeight = 40,
        rows = function() return rows end,
    }
end

-- Cards with stacks nested eight levels deep.
local function nestedStack(i, depth)
    if depth == 0 then
        return Text { text = function() return ("Leaf %d · %.2f"):format(i, phase(i)) end }
    end
    if depth % 2 == 0 then
        return Column { spacing = 2, nestedStack(i, depth - 1) }
    end
    return Row { spacing = 2, nestedStack(i, depth - 1) }
end
local function nested(count)
    return Grid {
        columns = 5, spacing = 8,
        Repeater {
            model = function() return count end,
            delegate = function(i) return Card { variant = "filled", padding = 8, nestedStack(i, 8) } end,
        },
    }
end

-- Plots: one per ten count units, each with a hundred points that shift while animating.
local function plots(count)
    return Grid {
        columns = 4, spacing = 12,
        Repeater {
            model = function() return math.max(1, count // 10) end,
            delegate = function(i)
                return Card {
                    variant = "outlined", padding = 8,
                    Plot {
                        title = "Series " .. i, height = 120, kind = (i % 2 == 0) and "bars" or "lines",
                        values = function()
                            local v = {}
                            for k = 1, 100 do v[k] = math.sin((k + s.tick + i * 13) / 8) * 10 + 10 end
                            return v
                        end,
                    },
                }
            end,
        },
    }
end

local builders = {
    tiles = tiles, controls = controls, listview = list, table = tableView, nested = nested, plots = plots,
}

stress = {
    s = s,
    scenarios = { "tiles", "controls", "listview", "table", "nested", "plots" },
    build = function(name, count)
        local builder = builders[name]
        if not builder then error("unknown scenario '" .. tostring(name) .. "'", 2) end
        return ScrollView {
            padding = 24,
            Column {
                spacing = 24,
                Timer { interval = 16, running = function() return s.animate end,
                        onTriggered = function() s.tick = s.tick + 1 end },
                Text { text = function() return ("%s, count %d, tick %d"):format(name, count, s.tick) end,
                       style = "titleLarge" },
                builder(count),
            },
        }
    end,
}
