-- The smoke app (tests/data/smoke_app) as an archive mounted at app:/smoke/ (imlui_add_pak in tests/CMakeLists.txt),
-- read back by resources_packed_test.
return {
    prefix = "app",
    mount = "smoke",
    base = "smoke_app",
    include = "**/*.lua",
    exclude = "resources.lua", -- the app's own manifest: only the manifest being evaluated never matches itself
}
