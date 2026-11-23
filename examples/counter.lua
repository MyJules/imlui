-- Smallest useful imlui app. Run: imlui_app examples/counter.lua
local s = state { count = 0 }

theme { seed = "#006A6A", dark = false }

return ApplicationWindow {
    title = "Counter",
    width = 420, height = 260,

    Column {
        spacing = 16,
        Text { text = "Counter", style = "headlineMedium" },
        Text {
            text = function() return ("You clicked %d times"):format(s.count) end,
            style = "bodyLarge", color = "onSurfaceVariant",
        },
        Row {
            spacing = 8,
            Button { text = "Increment", onClicked = function() s.count = s.count + 1 end },
            Button { text = "Reset", variant = "outlined", enabled = function() return s.count > 0 end,
                     onClicked = function() s.count = 0 end },
        },
    },
}
