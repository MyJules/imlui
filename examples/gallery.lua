-- imlui gallery: every built-in element, plus a live theme editor.
-- Edit this file while the app is running — it hot-reloads on save.

local StatCard = require "StatCard"   -- component from StatCard.lua

-------------------------------------------------------------------------------
-- Theme: fully decided here, on the Lua side
-------------------------------------------------------------------------------
local presets = { "material", "imgui-dark", "imgui-light", "imgui-classic" }
local seeds = {
    { "Purple", "#6750A4" }, { "Teal", "#006A6A" }, { "Blue", "#0061A4" },
    { "Green", "#386A20" }, { "Orange", "#8B5000" }, { "Rose", "#9C4146" },
}

local look = state {
    seed = "#6750A4", dark = true, preset = 1,
    roundness = 1.0, spacing = 8, fontScale = 1.0, neon = false,
}

local function applyTheme()
    local r = look.roundness
    theme {
        preset = presets[look.preset],
        seed = look.seed,
        dark = look.dark,
        spacing = look.spacing,
        fontScale = look.fontScale,
        shape = { extraSmall = 4 * r, small = 8 * r, medium = 12 * r, large = 16 * r, extraLarge = 28 * r },
        -- Any color role can be overridden from Lua:
        colors = look.neon and { primary = "#00E5FF", onPrimary = "#00363D", secondaryContainer = "#4A0072",
                                 onSecondaryContainer = "#F8D8FF" } or nil,
    }
end
watch(look, "*", applyTheme)
applyTheme()

-------------------------------------------------------------------------------
-- Building blocks
-------------------------------------------------------------------------------
-- The top app bar already shows the page title; pages open with a short intro.
local function pageHeader(_, sub)
    return Text { text = sub, style = "bodyLarge", color = "onSurfaceVariant", wrap = true }
end

-- A tight vertical list of 40dp controls (switches, checkboxes...).
local function controls(...)
    return Column { spacing = 0, ... }
end

--- A titled card grouping one demo.
local function section(title, description, content)
    local card = Card { padding = 24, spacing = 16,
        Column {
            spacing = 2,
            Text { text = title, style = "titleMedium" },
            Text { text = description or "", style = "bodyMedium", color = "onSurfaceVariant", wrap = true,
                   visible = description ~= nil },
        },
    }
    for _, c in ipairs(content) do card[#card + 1] = c end
    return card
end

local function grid(...)
    return Grid { columns = 2, spacing = 24, ... }
end

-------------------------------------------------------------------------------
-- Pages
-------------------------------------------------------------------------------

-- Buttons ----------------------------------------------------------------------
local btn = state { n = 0, enabled = true, fav = false, bell = true, bookmark = false }
local function count() btn.n = btn.n + 1 end
local en = bind(btn, "enabled")

local buttonsPage = Column {
    spacing = 20,
    pageHeader("Buttons", "Common buttons, icon buttons and floating action buttons in every Material 3 variant."),
    Row {
        spacing = 16,
        Switch { text = "Enabled", checked = bind(btn, "enabled") },
        Text { text = function() return ("Clicked %d times"):format(btn.n) end, style = "bodyLarge", color = "onSurfaceVariant" },
    },
    grid(
        section("Common buttons", "Five emphasis levels: filled, tonal, elevated, outlined, text.", {
            Row { spacing = 8,
                Button { text = "Filled", enabled = en, onClicked = count },
                Button { text = "Tonal", variant = "tonal", enabled = en, onClicked = count },
                Button { text = "Elevated", variant = "elevated", enabled = en, onClicked = count },
            },
            Row { spacing = 8,
                Button { text = "Outlined", variant = "outlined", enabled = en, onClicked = count },
                Button { text = "Text", variant = "text", enabled = en, onClicked = count },
            },
        }),
        section("With icons", "A leading icon reinforces the action.", {
            Row { spacing = 8,
                Button { text = "Create", icon = "add", enabled = en, onClicked = count },
                Button { text = "Share", icon = "share", variant = "tonal", enabled = en, onClicked = count },
            },
            Row { spacing = 8,
                Button { text = "Download", icon = "download", variant = "outlined", enabled = en, onClicked = count },
                Button { text = "Learn more", icon = "open_in_new", variant = "text", enabled = en, onClicked = count },
            },
        }),
        section("Icon buttons", "Standard, filled, tonal and outlined. These ones toggle.", {
            Row { spacing = 4,
                IconButton { icon = "favorite_border", checkedIcon = "favorite", checked = bind(btn, "fav"),
                             enabled = en, tooltip = "Favorite" },
                IconButton { icon = "notifications_none", checkedIcon = "notifications", variant = "filled",
                             checked = bind(btn, "bell"), enabled = en, tooltip = "Notifications" },
                IconButton { icon = "bookmark_border", checkedIcon = "bookmark", variant = "tonal",
                             checked = bind(btn, "bookmark"), enabled = en, tooltip = "Bookmark" },
                IconButton { icon = "settings", variant = "outlined", enabled = en, tooltip = "Settings", onClicked = count },
            },
        }),
        section("Floating action buttons", "For the primary action of a screen.", {
            Row { spacing = 16,
                Button { variant = "fab", icon = "edit", text = "", enabled = en, onClicked = count, tooltip = "Compose" },
                Button { variant = "fab", icon = "add", text = "New item", enabled = en, onClicked = count },
            },
        })
    ),
}

-- Selection --------------------------------------------------------------------
local sel = state { wifi = true, bt = false, airplane = false, notify = true, sound = false, size = 2, view = 1,
                    tags = { lua = true, cpp = true, imgui = false, qml = false } }

local selectionPage = Column {
    spacing = 20,
    pageHeader("Selection", "Switches, checkboxes, radio buttons, chips and segmented buttons."),
    grid(
        section("Switches", "Toggle a single setting on or off.", {
            controls(
                Switch { text = "Wi-Fi", checked = bind(sel, "wifi") },
                Switch { text = "Bluetooth", checked = bind(sel, "bt") },
                Switch { text = "Airplane mode (disabled)", checked = bind(sel, "airplane"), enabled = false }
            ),
        }),
        section("Checkboxes", "Select one or more options.", {
            controls(
                Checkbox { text = "Send notifications", checked = bind(sel, "notify") },
                Checkbox { text = "Play sounds", checked = bind(sel, "sound") },
                Checkbox { text = "Disabled option", checked = true, enabled = false }
            ),
        }),
        section("Radio buttons", "Pick exactly one.", {
            RadioGroup { model = { "Small", "Medium", "Large" }, currentIndex = bind(sel, "size") },
        }),
        section("Chips", "Filter chips toggle; assist chips trigger an action.", {
            Row { spacing = 8,
                Repeater {
                    model = { "lua", "cpp", "imgui", "qml" },
                    delegate = function(tag) return Chip { text = tag, checked = bind(sel.tags, tag) } end,
                },
            },
            Row { spacing = 8,
                Chip { text = "Add to calendar", variant = "assist", icon = "event",
                       onClicked = function() snackbar("Added to calendar") end },
                Chip { text = "Directions", variant = "assist", icon = "directions",
                       onClicked = function() snackbar("Opening directions") end },
            },
        }),
        section("Segmented buttons", "Switch between closely related views.", {
            SegmentedButton { model = { "Day", "Week", "Month" }, currentIndex = bind(sel, "view"), fillWidth = true },
            Text {
                text = function()
                    local on = {}
                    for _, k in ipairs({ "lua", "cpp", "imgui", "qml" }) do if sel.tags[k] then on[#on + 1] = k end end
                    return ("size = %d · view = %d · tags = {%s}"):format(sel.size, sel.view, table.concat(on, ", "))
                end,
                style = "bodySmall", color = "onSurfaceVariant",
            },
        })
    ),
}

-- Inputs -----------------------------------------------------------------------
local form = state { email = "", password = "", bio = "", search = "", volume = 0.4, stars = 3,
                     qty = 1, country = 1, size = 2, color = "#6750A4" }

local function emailInvalid() return form.email ~= "" and not form.email:match("^[^@%s]+@[^@%s]+%.[^@%s]+$") end

local inputsPage = Column {
    spacing = 20,
    pageHeader("Inputs", "Text fields, dropdowns, sliders and spin boxes, all bound two-way to a state table."),
    grid(
        section("Filled text fields", nil, {
            -- No binding: the text is stored on the element itself, reachable by id.
            TextField { id = "nameField", label = "Name", text = "", leadingIcon = "person", fillWidth = true },
            TextField { label = "Email", placeholderText = "you@example.com", text = bind(form, "email"),
                        leadingIcon = "mail", fillWidth = true,
                        error = emailInvalid,
                        supportingText = function() return emailInvalid() and "Enter a valid email address" or "We'll never share it" end },
            Text { text = function() return "Hello, " .. (nameField.text ~= "" and nameField.text or "stranger") .. "!" end,
                   style = "titleMedium", color = "primary" },
        }),
        section("Outlined text fields", nil, {
            TextField { variant = "outlined", label = "Search", text = bind(form, "search"), leadingIcon = "search",
                        trailingIcon = function() return form.search ~= "" and "close" or "" end,
                        onTrailingClicked = function() form.search = "" end, fillWidth = true },
            TextField { variant = "outlined", label = "Password", password = true, text = bind(form, "password"),
                        leadingIcon = "lock", fillWidth = true,
                        supportingText = function() return #form.password .. " characters" end },
            TextArea { variant = "outlined", label = "Bio", text = bind(form, "bio"), fillWidth = true, height = 110 },
        }),
        section("Dropdowns", "Exposed dropdown menus, filled and outlined.", {
            ComboBox { label = "Country", leadingIcon = "public", model = { "Ukraine", "Poland", "Germany", "Japan", "Canada" },
                       currentIndex = bind(form, "country"), fillWidth = true },
            ComboBox { label = "Size", variant = "outlined", model = { "Small", "Medium", "Large", "Extra large" },
                       currentIndex = bind(form, "size"), fillWidth = true },
        }),
        section("Sliders & spin boxes", nil, {
            Row { spacing = 8, Icon { name = "volume_down" }, Slider { value = bind(form, "volume"), width = 300 }, Icon { name = "volume_up" } },
            Slider { value = bind(form, "stars"), from = 1, to = 5, stepSize = 1, fillWidth = true, format = "%.0f stars" },
            Row { spacing = 16,
                SpinBox { label = "Quantity", value = bind(form, "qty"), from = 1, to = 99, width = 180 },
                SpinBox { label = "Outlined", variant = "outlined", value = bind(form, "qty"), from = 1, to = 99, width = 180 },
            },
        }),
        section("Color", "A swatch that opens a picker.", {
            ColorPicker { label = "Accent color", color = bind(form, "color"), fillWidth = true },
        })
    ),
}

-- Feedback ---------------------------------------------------------------------
local fb = state { progress = 0.35, dialog = false, confirm = false, seconds = 0, busy = true, result = "" }

local feedbackPage = Column {
    spacing = 20,
    pageHeader("Feedback", "Progress indicators, dialogs, snackbars and tooltips."),
    grid(
        section("Linear progress", "Determinate (bound to the slider) and indeterminate.", {
            ProgressBar { value = bind(fb, "progress"), fillWidth = true },
            Slider { value = bind(fb, "progress"), fillWidth = true, format = "%.2f" },
            ProgressBar { indeterminate = true, fillWidth = true },
        }),
        section("Circular progress", nil, {
            Row { spacing = 24,
                CircularProgress { value = bind(fb, "progress") },
                CircularProgress { indeterminate = true },
                BusyIndicator { running = bind(fb, "busy"), size = 32, thickness = 3 },
                Switch { text = "Loading", checked = bind(fb, "busy") },
            },
        }),
        section("Dialogs", "Modal decisions with a scrim behind them.", {
            Row { spacing = 8,
                Button { text = "Basic dialog", variant = "tonal", onClicked = function() fb.dialog = true end },
                Button { text = "With icon", variant = "outlined", icon = "delete", onClicked = function() fb.confirm = true end },
            },
            Text { text = bind(fb, "result"), style = "bodyMedium", color = "onSurfaceVariant" },
        }),
        section("Snackbars & tooltips", "Brief messages about app processes.", {
            Row { spacing = 8,
                Button { text = "Show snackbar", variant = "tonal",
                         onClicked = function() snackbar("Message archived", { action = "Undo",
                             onAction = function() fb.result = "Undo clicked" end }) end },
                Button { text = "Hover me", variant = "outlined", tooltip = "Plain tooltips use inverse surface colors" },
            },
            Timer { interval = 1000, onTriggered = function() fb.seconds = fb.seconds + 1 end },
            Text { text = function() return ("A Timer element has fired %d times"):format(fb.seconds) end,
                   style = "bodySmall", color = "onSurfaceVariant" },
        })
    ),
    Dialog {
        title = "Reset settings?",
        open = bind(fb, "dialog"),
        width = 360,
        Text { text = "This will reset your app preferences back to their default settings.",
               style = "bodyMedium", color = "onSurfaceVariant", wrap = true },
        Spacer { height = 8 },
        Row { Spacer { fill = true },
            Button { text = "Cancel", variant = "text", onClicked = function() fb.dialog = false; fb.result = "Cancelled" end },
            Button { text = "Accept", variant = "text", onClicked = function() fb.dialog = false; fb.result = "Settings reset" end },
        },
    },
    Dialog {
        title = "Permanently delete?",
        icon = "delete",
        open = bind(fb, "confirm"),
        width = 360,
        Text { text = "Deleting the selected messages will also remove them from synced devices.",
               style = "bodyMedium", color = "onSurfaceVariant", wrap = true },
        Spacer { height = 8 },
        Row { Spacer { fill = true },
            Button { text = "Cancel", variant = "text", onClicked = function() fb.confirm = false end },
            Button { text = "Delete", variant = "text", onClicked = function()
                fb.confirm = false
                fb.result = "Messages deleted"
                snackbar("3 messages deleted", { action = "Undo" })
            end },
        },
    },
}

-- Navigation -------------------------------------------------------------------
local navDemo = state { tab = 1, tab2 = 1, adv = false, sort = "Name" }

local navigationPage = Column {
    spacing = 20,
    pageHeader("Navigation", "Tabs, expanders and menus."),
    Card { padding = 0,
        TabBar {
            currentIndex = bind(navDemo, "tab"),
            Tab { title = "Flights", icon = "flight", Column { padding = 24,
                Text { text = "Find cheap flights to anywhere.", style = "bodyLarge" } } },
            Tab { title = "Trips", icon = "luggage", Column { padding = 24,
                Text { text = "Your upcoming trips show up here.", style = "bodyLarge" } } },
            Tab { title = "Explore", icon = "explore", Column { padding = 24,
                Text { text = "Discover new destinations.", style = "bodyLarge" } } },
        },
    },
    grid(
        section("Secondary tabs", "For sub-sections within a page.", {
            TabBar {
                variant = "secondary",
                currentIndex = bind(navDemo, "tab2"),
                Tab { title = "Overview", Text { text = "Overview content", color = "onSurfaceVariant" } },
                Tab { title = "Specifications", Text { text = "Specifications content", color = "onSurfaceVariant" } },
            },
        }),
        section("Menus", "Attach a menu to any button or icon button.", {
            Row { spacing = 8,
                Button { text = function() return "Sort: " .. navDemo.sort end, icon = "sort", variant = "outlined",
                    menu = Menu {
                        Repeater { model = { "Name", "Date modified", "Size" }, delegate = function(k)
                            return MenuItem { text = k,
                                              icon = function() return navDemo.sort == k and "check" or "" end,
                                              onTriggered = function() navDemo.sort = k end }
                        end },
                    } },
                IconButton { icon = "more_vert", tooltip = "More",
                    menu = Menu {
                        MenuItem { text = "Edit", icon = "edit", shortcut = "Ctrl+E" },
                        MenuItem { text = "Duplicate", icon = "content_copy", shortcut = "Ctrl+D" },
                        Divider {},
                        MenuItem { text = "Delete", icon = "delete", onTriggered = function() snackbar("Deleted") end },
                    } },
            },
        }),
        section("Expanders", nil, {
            Expander { title = "Advanced settings", supportingText = "Network, storage, developer options",
                       expanded = bind(navDemo, "adv"),
                controls(Switch { text = "Developer mode" }, Switch { text = "Verbose logging" }),
            },
            Expander { title = "About", expanded = false,
                Text { text = "imlui renders Lua-described UI with Dear ImGui.", style = "bodyMedium", color = "onSurfaceVariant" },
            },
        })
    ),
}

-- Data -------------------------------------------------------------------------
local todo = state { items = { "Write the Lua DSL", "Theme everything", "Ship it" }, newItem = "", selected = 0 }
local samples, bars = {}, { 4, 7, 5, 9, 6, 11, 8 }
for i = 1, 90 do samples[i] = 0 end
setInterval(function()
    table.remove(samples, 1)
    local t = time()
    samples[#samples + 1] = math.sin(t * 1.7) * 0.6 + math.sin(t * 4.3) * 0.25 + (math.random() - 0.5) * 0.12
end, 50)

local function addTodo()
    if todo.newItem ~= "" then
        table.insert(todo.items, todo.newItem)
        todo.newItem = ""
    end
end

local dataPage = Column {
    spacing = 20,
    pageHeader("Data", "Lists from models, charts, tables and components loaded from other files."),
    Grid { columns = 3, spacing = 24,
        StatCard { title = "Items", icon = "checklist", value = function() return tostring(#todo.items) end, caption = "in the list below" },
        StatCard { title = "Frame rate", icon = "speed", value = function() return ("%.0f fps"):format(fps()) end,
                   color = "tertiary", caption = "rendered by Dear ImGui" },
        StatCard { title = "Signal", icon = "show_chart", value = function() return ("%+.2f"):format(samples[#samples]) end,
                   color = "secondary", caption = "live, from a Lua timer" },
    },
    grid(
        section("To-do list", "A ListView driven by a model table.", {
            Row { spacing = 8,
                TextField { label = "New item", text = bind(todo, "newItem"), width = 260, onAccepted = addTodo },
                Button { text = "Add", icon = "add", onClicked = addTodo, enabled = function() return todo.newItem ~= "" end },
            },
            ListView {
                height = 240,
                model = function() return todo.items end,
                delegate = function(item, index)
                    return Row {
                        spacing = 4,
                        ListItem { text = item, supportingText = "Item #" .. index, icon = "task_alt", width = -56,
                                   selected = function() return todo.selected == index end,
                                   onClicked = function() todo.selected = index end },
                        IconButton { icon = "delete_outline", tooltip = "Remove", onClicked = function()
                            local removed = table.remove(todo.items, index)
                            todo.selected = 0
                            snackbar("Removed \"" .. removed .. "\"", { action = "Undo", onAction = function()
                                table.insert(todo.items, index, removed)
                            end })
                        end },
                    }
                end,
            },
        }),
        section("Charts", "Live line chart with gradient fill, and bars.", {
            Plot { title = "Live signal", values = function() return samples end, height = 150 },
            Plot { title = "Weekly sessions", kind = "bars", values = bars, height = 130, color = "tertiary" },
        })
    ),
    section("Data table", "Click a row to select it.", {
        Table {
            columns = { "Element", "Kind", "Two-way props" },
            rows = {
                { "TextField", "input", "text" }, { "Slider", "input", "value" },
                { "Switch", "selection", "checked" }, { "ComboBox", "selection", "currentIndex" },
                { "Dialog", "window", "open" },
            },
        },
    }),
}

-- Canvas -----------------------------------------------------------------------
local paint = state { dots = {} }

local canvasPage = Column {
    spacing = 20,
    pageHeader("Canvas", "Custom drawing from Lua through a painter object. Click to drop stars."),
    Card { padding = 0, variant = "filled",
        Canvas {
            height = 360,
            onClicked = function(_, x, y) table.insert(paint.dots, { x, y }) end,
            onPaint = function(_, p)
                local pts = {}
                for i = 0, 80 do
                    local x = i / 80 * p.width
                    local y = p.height / 2 + math.sin(p.time * 2 + i * 0.2) * p.height * 0.22
                    pts[#pts + 1] = x
                    pts[#pts + 1] = y
                end
                p:polyline(pts, "primary", 3)
                for i = 0, 14 do
                    local x = (i + 0.5) / 15 * p.width
                    p:circle(x, p.height / 2 + math.cos(p.time * 3 + i) * 50, 7, i % 2 == 0 and "tertiary" or "secondary")
                end
                for _, d in ipairs(paint.dots) do p:icon(d[1], d[2], "star", 28, "error") end
                if p.hovered then p:circle(p.mouseX, p.mouseY, 22, "primary/0.18") end
                p:text(20, 16, "Drawn from Lua", "onSurface", "titleMedium")
                p:text(20, 40, #paint.dots .. " stars placed", "onSurfaceVariant", "bodySmall")
            end,
        },
    },
    Button { text = "Clear", icon = "clear_all", variant = "outlined", onClicked = function() paint.dots = {} end },
}

-- Theme ------------------------------------------------------------------------
local roles = { "primary", "onPrimary", "primaryContainer", "onPrimaryContainer", "secondary", "secondaryContainer",
                "tertiary", "tertiaryContainer", "error", "errorContainer", "surface", "surfaceContainerLow",
                "surfaceContainer", "surfaceContainerHighest", "outline", "outlineVariant" }

local themePage = Column {
    spacing = 20,
    pageHeader("Theme", "The theme is pure data built in Lua: a Material 3 scheme generated from a seed color, "
        .. "plus any role, ImGui color or style override you like."),
    grid(
        section("Seed color", "Everything is derived from this one color.", {
            Row { spacing = 8,
                Repeater { model = seeds, delegate = function(s)
                    return Chip { text = s[1], variant = "suggestion",
                                  checked = function() return look.seed:upper() == s[2] end,
                                  onClicked = function() look.seed = s[2] end }
                end },
            },
            ColorPicker { label = "Custom seed", color = bind(look, "seed"), fillWidth = true },
        }),
        section("Mode & preset", nil, {
            controls(
                Switch { text = "Dark mode", checked = bind(look, "dark") },
                Switch { text = "Neon role overrides", checked = bind(look, "neon") }
            ),
            ComboBox { label = "Preset", model = presets, currentIndex = bind(look, "preset"), fillWidth = true },
        }),
        section("Shape & density", nil, {
            Text { text = function() return ("Roundness ×%.2f"):format(look.roundness) end, style = "labelLarge", color = "onSurfaceVariant" },
            Slider { value = bind(look, "roundness"), from = 0, to = 2, fillWidth = true },
            Text { text = function() return ("Spacing %d dp"):format(look.spacing) end, style = "labelLarge", color = "onSurfaceVariant" },
            Slider { value = bind(look, "spacing"), from = 4, to = 14, stepSize = 1, fillWidth = true },
            Text { text = function() return ("Font scale ×%.2f"):format(look.fontScale) end, style = "labelLarge", color = "onSurfaceVariant" },
            Slider { value = bind(look, "fontScale"), from = 0.8, to = 1.4, fillWidth = true },
        }),
        section("Scheme", "Generated color roles.", {
            Grid { columns = 2, spacing = 8,
                Repeater { model = roles, delegate = function(role)
                    return Row { spacing = 12,
                        Canvas { width = 32, height = 32, onPaint = function(_, p)
                            p:rect(0, 0, p.width, p.height, role, 8)
                            p:rect(0, 0, p.width, p.height, "outlineVariant", 8, true)
                        end },
                        Text { text = role, style = "bodySmall" },
                    }
                end },
            },
        })
    ),
}

-------------------------------------------------------------------------------
-- Shell: navigation drawer + top app bar + page loader
-------------------------------------------------------------------------------
local pages = {
    { "Buttons", "smart_button", buttonsPage },
    { "Selection", "toggle_on", selectionPage },
    { "Inputs", "edit_note", inputsPage },
    { "Feedback", "notifications_active", feedbackPage },
    { "Navigation", "tab", navigationPage },
    { "Data", "table_chart", dataPage },
    { "Canvas", "brush", canvasPage },
    { "Theme", "palette", themePage },
}
-- GALLERY_PAGE=n opens page n at startup (handy for screenshots / testing).
local nav = state { page = tonumber(os.getenv("GALLERY_PAGE") or "") or 1, demo = false }

return ApplicationWindow {
    title = function() return "imlui gallery — " .. pages[nav.page][1] end,
    width = 1280, height = 860,
    padding = 0,
    scrollable = false,

    Row {
        align = "top", spacing = 0,

        -- Navigation drawer
        ScrollView {
            width = 296, padding = 12, spacing = 0, color = "surfaceContainerLow", rounding = 0,
            Column { padding = 16, spacing = 2,
                Text { text = "imlui", style = "headlineSmall", color = "primary" },
                Text { text = "Lua + Dear ImGui + Material 3", style = "bodySmall", color = "onSurfaceVariant" },
                Spacer { height = 4 },
            },
            Repeater {
                model = pages,
                delegate = function(p, i)
                    return NavigationItem { icon = p[2], text = p[1],
                                            selected = function() return nav.page == i end,
                                            onClicked = function() nav.page = i end }
                end,
            },
            Spacer { height = 12 },
            Divider { inset = 16, insetEnd = 16 },
            Spacer { height = 12 },
            NavigationItem { icon = "code", text = "ImGui demo window",
                             selected = function() return nav.demo end,
                             onClicked = function() nav.demo = not nav.demo end },
        },

        -- Content
        Column {
            spacing = 0,
            TopAppBar {
                title = function() return pages[nav.page][1] end,
                IconButton { icon = function() return look.dark and "light_mode" or "dark_mode" end,
                             tooltip = "Toggle dark mode", onClicked = function() look.dark = not look.dark end },
                IconButton { icon = "palette", tooltip = "Seed color",
                    menu = Menu {
                        Repeater { model = seeds, delegate = function(s)
                            return MenuItem { text = s[1],
                                              icon = function() return look.seed:upper() == s[2] and "check" or "" end,
                                              onTriggered = function() look.seed = s[2] end }
                        end },
                    } },
                IconButton { icon = "more_vert", tooltip = "More",
                    menu = Menu {
                        MenuItem { text = "Reload script", icon = "refresh", shortcut = "on save", onTriggered = reload },
                        MenuItem { text = "ImGui demo window", checked = bind(nav, "demo") },
                        Divider {},
                        MenuItem { text = "Quit", icon = "logout", onTriggered = quit },
                    } },
            },
            ScrollView {
                padding = 24, rounding = 0,
                Loader { item = function() return pages[nav.page][3] end },
            },
        },
    },

    ImGuiDemo { open = bind(nav, "demo"), visible = function() return nav.demo end },
}
