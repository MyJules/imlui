-- A component of the smoke app, loaded with require "Tile" through the resource searcher.
return function(title, value)
    return Card {
        variant = "outlined",
        Column {
            spacing = 4,
            Text { text = title, style = "labelLarge", color = "onSurfaceVariant" },
            Text { text = value, style = "headlineSmall" },
        },
    }
end
