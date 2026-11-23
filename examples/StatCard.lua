-- A reusable component, the imlui equivalent of a QML file.
-- Used from other scripts with:  local StatCard = require "StatCard"
--
-- Props: title, value (string or binding), caption, color (role/hex), width, variant.
return component(function(props)
    return Card {
        variant = props.variant or "filled",
        width = props.width or 220,
        spacing = 4,
        Text { text = props.title, style = "labelLarge", color = "onSurfaceVariant" },
        Text { text = props.value, style = "headlineMedium", color = props.color or "primary" },
        Text {
            text = props.caption or "",
            style = "bodySmall",
            color = "onSurfaceVariant",
            visible = props.caption ~= nil,
        },
    }
end)
