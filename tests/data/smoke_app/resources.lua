-- The smoke app, embedded into imlui_smoke_host (imlui_add_resources in tests/CMakeLists.txt) as module
-- imlui.embedded.smoke_app. Two groups: the scripts at app:/, and a test image from tests/data at app:/images/.
-- Format: docs/architecture.md, "Resource manifests".
return {
    { prefix = "app", include = "**/*.lua" },
    { prefix = "app", mount = "images", base = "..", files = { "rgba_4x2.png" } },
}
