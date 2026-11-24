-- Fixture for the `manifest` fuzz target (tests/fuzz/manifest.cpp): a manifest is resolved against this folder.
local Tile = require "components.Tile"
return ui.Column { Tile { text = "one" }, Tile { text = "two" } }
