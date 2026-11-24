-- The library's own resources (imlui_add_resources(imlui resources/resources.lua NAME imlui) in src/CMakeLists.txt):
-- embedded into the imlui library as the module imlui.embedded.imlui, so an app runs without these files next to it.
-- Format: docs/architecture.md, "Resource manifests". Paths are relative to this file's folder (src/resources/).
return {
    -- The Lua side: imlui:/imlui.lua and whatever lands next to it (a manifest never matches itself).
    { prefix = "imlui", base = ".", include = "**" },
    -- The fonts and their codepoints, with their licenses.
    {
        prefix = "imlui",
        mount = "fonts",
        base = "../../external/fonts",
        files = {
            "Roboto-Regular.ttf", "Roboto-Medium.ttf", "LICENSE-Roboto.txt",
            "MaterialIconsRound-Regular.otf", "MaterialIconsRound-Regular.codepoints", "LICENSE-MaterialIcons.txt",
        },
    },
}
