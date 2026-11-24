-- A reusable component, the imlui equivalent of a QML file.
-- Used from other scripts with:  local StatCard = require "StatCard"
--
-- Props: title, value (string or binding), caption, icon, color (role/hex), width, variant.
return component(function(props)
    return Card {
        variant = props.variant or "filled",
        color = props.background,
        width = props.width or 0,
        padding = 20,
        Row {
            spacing = 12,
            Icon { name = props.icon or "insights", color = props.color or "primary", visible = props.icon ~= nil },
            Text { text = props.title, style = "titleSmall", color = "onSurfaceVariant" },
        },
        Spacer { height = 4 },
        Text { text = props.value, style = "headlineMedium", color = props.color or "primary" },
        Text {
            text = props.caption or "",
            style = "bodySmall",
            color = "onSurfaceVariant",
            visible = props.caption ~= nil,
        },
    }
end)
