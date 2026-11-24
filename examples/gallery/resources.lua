-- The gallery's files, embedded into imlui_app as its app (imlui_add_resources(imlui_app
-- examples/gallery/resources.lua NAME gallery) in CMakeLists.txt): app:/main.lua, its components and images.
-- Format: docs/architecture.md, "Resource manifests".
return {
    prefix = "app",
    include = { "**/*.lua", "**/*.png", "**/*.jpg" },
}
