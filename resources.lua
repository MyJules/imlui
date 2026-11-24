-- The library's own resources (imlui_add_resources(imlui resources.lua NAME imlui) in CMakeLists.txt): embedded
-- into the imlui library as the module imlui.embedded.imlui, so an app runs without these files next to it.
-- Format: docs/architecture.md, "Resource manifests".
return {
    -- The Lua side: imlui:/imlui.lua and whatever lands next to it.
    { prefix = "imlui", base = "lua", include = "**" },
    -- The fonts and their codepoints, with their licenses.
    {
        prefix = "imlui",
        mount = "fonts",
        base = "external/fonts",
        files = {
            "Roboto-Regular.ttf", "Roboto-Medium.ttf", "LICENSE-Roboto.txt",
            "MaterialIconsRound-Regular.otf", "MaterialIconsRound-Regular.codepoints", "LICENSE-MaterialIcons.txt",
        },
    },
}
