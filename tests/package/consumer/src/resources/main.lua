-- The consumer app (tests/package): checks that the library's files (embedded or from its source folder), the app's
-- and the .pak built by imlui_add_pak are all mounted, then shows a window. An assert fails the run (exit code 1).
assert(ui.resource.exists("imlui:/imlui.lua"), "imlui:/imlui.lua is missing")
assert(ui.resource.exists("imlui:/fonts/Roboto-Regular.ttf"), "imlui:/fonts/ is missing")
local greeting = assert(ui.resource.read("app:/pak/greeting.lua"))
assert(greeting:find("consumer", 1, true), "app:/pak/greeting.lua has the wrong contents")

return ApplicationWindow {
    title = "imlui consumer", width = 320, height = 200,
    Column { Text { text = "Consumer", style = "headlineSmall" }, Button { text = "OK", icon = "check" } },
}
