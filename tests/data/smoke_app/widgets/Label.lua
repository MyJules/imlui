-- A component in a nested folder of the smoke app, loaded with require "widgets.Label".
return function(text)
    return Text { text = text, style = "labelMedium" }
end
