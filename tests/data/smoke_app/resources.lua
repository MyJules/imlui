-- The smoke app, embedded into imlui_smoke_host (imlui_add_resources in tests/CMakeLists.txt) as module
-- imlui.embedded.smoke_app, and packed into smoke_app.pak for the smoke.modes test. Two groups: the app's files at
-- app:/, and a test image from tests/data at app:/images/.
-- Format: docs/architecture.md, "Resource manifests".
--
-- It selects files every way a manifest can, so smoke.modes (tests/smoke_modes.cmake) sees each of them served alike
-- from the source folders (dev builds), the embedded copy and the archive: a pattern with a nested folder
-- (widgets/Label.lua), an exclude (scratch/draft.lua is on disk but not shipped), listed files (data/notes.txt next to
-- the unlisted data/unlisted.txt, and this manifest itself, which no pattern matches) and a file from another
-- folder. hidden.txt and data/unlisted.txt lie next to the selected files and must not be served in any mode.
return {
    {
        prefix = "app",
        include = "**/*.lua",
        exclude = "scratch/**",
        files = { "resources.lua", "data/notes.txt" },
    },
    { prefix = "app", mount = "images", base = "..", files = { "rgba_4x2.png" } },
}
