-- imlui gallery: every built-in element, plus a live theme editor.
-- Edit this file while the app is running — it hot-reloads on save.

local StatCard = require "StatCard"   -- component from StatCard.lua

-------------------------------------------------------------------------------
-- Theme: fully decided here, on the Lua side
-------------------------------------------------------------------------------
local presets = { "material", "imgui-dark", "imgui-light", "imgui-classic" }
local seeds = {
    { "Purple", "#6750A4" }, { "Teal", "#006A6A" }, { "Blue", "#0061A4" },
    { "Green", "#386A20" }, { "Orange", "#8B5000" }, { "Red", "#B3261E" },
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
-- Pages
-------------------------------------------------------------------------------
local function heading(text, sub)
    return Column {
        spacing = 4,
        Text { text = text, style = "headlineMedium" },
        Text { text = sub, style = "bodyMedium", color = "onSurfaceVariant", wrap = true },
        Spacer { height = 8 },
    }
end

-- Buttons ----------------------------------------------------------------------
local clicks = state { n = 0, enabled = true }

local buttonsPage = Column {
    spacing = 16,
    heading("Buttons", "Material 3 button variants, drawn with ImGui primitives."),
    Row {
        spacing = 8,
        Button { text = "Filled", onClicked = function() clicks.n = clicks.n + 1 end,
                 enabled = bind(clicks, "enabled") },
        Button { text = "Tonal", variant = "tonal", enabled = bind(clicks, "enabled"),
                 onClicked = function() clicks.n = clicks.n + 1 end },
        Button { text = "Outlined", variant = "outlined", enabled = bind(clicks, "enabled"),
                 onClicked = function() clicks.n = clicks.n + 1 end },
        Button { text = "Elevated", variant = "elevated", enabled = bind(clicks, "enabled"),
                 onClicked = function() clicks.n = clicks.n + 1 end },
        Button { text = "Text", variant = "text", enabled = bind(clicks, "enabled"),
                 onClicked = function() clicks.n = clicks.n + 1 end },
    },
    Row {
        spacing = 16,
        Button { text = "+", variant = "fab", tooltip = "Floating action button",
                 onClicked = function() clicks.n = clicks.n + 10 end },
        Text { text = function() return ("Clicked %d times"):format(clicks.n) end, style = "titleMedium" },
    },
    Switch { text = "Buttons enabled", checked = bind(clicks, "enabled") },
    Button { text = "Full-width button", fillWidth = true, variant = "tonal",
             onClicked = function() clicks.n = 0 end, tooltip = "Resets the counter" },
}

-- Selection --------------------------------------------------------------------
local sel = state { wifi = true, bt = false, notify = true, size = 2, view = 1,
                    tags = { lua = true, cpp = true, imgui = false, qml = false } }

local selectionPage = Column {
    spacing = 16,
    heading("Selection", "Checkboxes, switches, radio buttons, chips and segmented buttons."),
    Card {
        variant = "outlined", spacing = 4,
        Switch { text = "Wi-Fi", checked = bind(sel, "wifi") },
        Switch { text = "Bluetooth", checked = bind(sel, "bt") },
        Checkbox { text = "Send notifications", checked = bind(sel, "notify") },
    },
    Text { text = "Size", style = "titleSmall" },
    RadioGroup { model = { "Small", "Medium", "Large" }, currentIndex = bind(sel, "size"), horizontal = true },
    Text { text = "Filter chips", style = "titleSmall" },
    Row {
        spacing = 8,
        Repeater {
            model = { "lua", "cpp", "imgui", "qml" },
            delegate = function(tag)
                return Chip { text = tag, checked = bind(sel.tags, tag) }
            end,
        },
    },
    Text { text = "Segmented button", style = "titleSmall" },
    SegmentedButton { model = { "Day", "Week", "Month" }, currentIndex = bind(sel, "view"), width = 420 },
    Text {
        text = function()
            local on = {}
            for _, k in ipairs({ "lua", "cpp", "imgui", "qml" }) do
                if sel.tags[k] then on[#on + 1] = k end
            end
            return ("size=%d  view=%d  tags={%s}"):format(sel.size, sel.view, table.concat(on, ", "))
        end,
        style = "bodyMedium", color = "onSurfaceVariant",
    },
}

-- Inputs -----------------------------------------------------------------------
local form = state { email = "", password = "", bio = "", volume = 0.4, stars = 3,
                     qty = 1, country = 1, submitted = "" }

local inputsPage = Column {
    spacing = 16,
    heading("Inputs", "Text fields, sliders, spin boxes and drop-downs. All bound two-way to a state table."),
    Row {
        spacing = 12,
        -- No binding: edits are stored on the element itself, reachable by its id.
        TextField { id = "nameField", label = "Name", text = "", width = 260 },
        TextField { label = "Email", placeholderText = "you@example.com", text = bind(form, "email"), width = 260 },
    },
    TextField { label = "Password", password = true, text = bind(form, "password"), width = 532,
                onAccepted = function(_, t) form.submitted = "password of length " .. #t end },
    TextArea { label = "Bio", text = bind(form, "bio"), width = 532, height = 110 },
    Text {
        -- QML-style id reference: nameField is the TextField above.
        text = function() return "Hello, " .. (nameField.text ~= "" and nameField.text or "stranger") .. "!" end,
        style = "titleMedium", color = "primary",
    },
    Text { text = "Volume", style = "titleSmall" },
    Slider { value = bind(form, "volume"), from = 0, to = 1, width = 532 },
    Text { text = "Rating (stepped)", style = "titleSmall" },
    Slider { value = bind(form, "stars"), from = 1, to = 5, stepSize = 1, width = 532, format = "%.0f stars" },
    Row {
        spacing = 24,
        Column { Text { text = "Quantity", style = "titleSmall" },
                 SpinBox { value = bind(form, "qty"), from = 1, to = 99 } },
        ComboBox { label = "Country", model = { "Ukraine", "Poland", "Germany", "Japan", "Canada" },
                   currentIndex = bind(form, "country") },
    },
    Text { text = function() return form.submitted end, visible = function() return form.submitted ~= "" end,
           style = "bodySmall", color = "tertiary" },
}

-- Feedback ---------------------------------------------------------------------
local fb = state { progress = 0.35, dialog = false, seconds = 0, busy = true, result = "" }

local feedbackPage = Column {
    spacing = 16,
    heading("Feedback", "Progress, loading indicators, dialogs, tooltips and timers."),
    Text { text = function() return ("Progress %d%%"):format(math.floor(fb.progress * 100 + 0.5)) end, style = "titleSmall" },
    ProgressBar { value = bind(fb, "progress"), width = 480 },
    Slider { value = bind(fb, "progress"), from = 0, to = 1, width = 480 },
    Text { text = "Indeterminate", style = "titleSmall" },
    ProgressBar { indeterminate = true, width = 480 },
    Row {
        spacing = 16,
        BusyIndicator { running = bind(fb, "busy") },
        Switch { text = "Loading", checked = bind(fb, "busy") },
    },
    Timer { interval = 1000, onTriggered = function() fb.seconds = fb.seconds + 1 end },
    Text { text = function() return ("Timer ticks: %d"):format(fb.seconds) end, style = "bodyLarge" },
    Row {
        spacing = 8,
        Button { text = "Open dialog", variant = "tonal", onClicked = function() fb.dialog = true end },
        Button { text = "Hover me", variant = "outlined", tooltip = "Tooltips use inverseSurface colors" },
        Text { text = bind(fb, "result"), style = "bodyMedium", color = "onSurfaceVariant" },
    },
    Dialog {
        title = "Discard draft?",
        open = bind(fb, "dialog"),
        width = 360,
        Text { text = "This removes the draft from this device. You can't undo it.",
               style = "bodyMedium", color = "onSurfaceVariant", wrap = true },
        Spacer { height = 12 },
        Row {
            spacing = 8,
            Button { text = "Cancel", variant = "text",
                     onClicked = function() fb.dialog = false; fb.result = "Kept the draft" end },
            Button { text = "Discard", variant = "text",
                     onClicked = function() fb.dialog = false; fb.result = "Draft discarded" end },
        },
    },
}

-- Data -------------------------------------------------------------------------
local todo = state { items = { "Write the Lua DSL", "Theme everything", "Ship it" }, newItem = "", selected = 0 }
local samples = {}
for i = 1, 120 do samples[i] = 0 end
setInterval(function()
    table.remove(samples, 1)
    local t = time()
    samples[#samples + 1] = math.sin(t * 2.0) * 0.6 + math.sin(t * 5.3) * 0.25 + (math.random() - 0.5) * 0.15
end, 33)

local function addTodo()
    if todo.newItem ~= "" then
        table.insert(todo.items, todo.newItem)
        todo.newItem = ""
    end
end

local dataPage = Column {
    spacing = 16,
    heading("Data", "Lists from models, tables, plots and components loaded from other files."),
    Row {
        spacing = 12,
        StatCard { title = "Items", value = function() return tostring(#todo.items) end, caption = "in the list below" },
        StatCard { title = "FPS", value = function() return ("%.0f"):format(fps()) end, color = "tertiary",
                   caption = "rendered by ImGui" },
        StatCard { title = "Signal", value = function() return ("%+.2f"):format(samples[#samples]) end,
                   color = "secondary", variant = "elevated" },
    },
    Row {
        spacing = 8,
        TextField { label = "New item", text = bind(todo, "newItem"), width = 320, onAccepted = addTodo },
        Button { text = "Add", onClicked = addTodo, enabled = function() return todo.newItem ~= "" end },
    },
    ListView {
        height = 220, border = true,
        model = function() return todo.items end,
        delegate = function(item, index)
            return Row {
                ListItem { text = item, supportingText = "Item #" .. index, width = 420,
                           selected = function() return todo.selected == index end,
                           onClicked = function() todo.selected = index end },
                Button { text = "Remove", variant = "text",
                         onClicked = function() table.remove(todo.items, index); todo.selected = 0 end },
            }
        end,
    },
    Plot { values = function() return samples end, height = 120, min = -1.2, max = 1.2, overlay = "live signal" },
    Table {
        columns = { "Element", "Kind", "Two-way props" },
        rows = {
            { "TextField", "input", "text" }, { "Slider", "input", "value" },
            { "Switch", "selection", "checked" }, { "ComboBox", "selection", "currentIndex" },
            { "Dialog", "window", "open" },
        },
    },
}

-- Canvas -----------------------------------------------------------------------
local paint = state { dots = {} }

local canvasPage = Column {
    spacing = 16,
    heading("Canvas", "Custom drawing from Lua through a painter object. Click to drop dots."),
    Canvas {
        height = 320,
        onClicked = function(_, x, y) table.insert(paint.dots, { x, y }) end,
        onPaint = function(_, p)
            p:rect(0, 0, p.width, p.height, "surfaceContainerHighest", 16)
            local pts = {}
            for i = 0, 60 do
                local x = i / 60 * p.width
                local y = p.height / 2 + math.sin(p.time * 2 + i * 0.25) * p.height * 0.25
                pts[#pts + 1] = x
                pts[#pts + 1] = y
            end
            p:polyline(pts, "primary", 3)
            for i = 0, 12 do
                local x = (i + 0.5) / 13 * p.width
                p:circle(x, p.height / 2 + math.cos(p.time * 3 + i) * 40, 6, i % 2 == 0 and "tertiary" or "secondary")
            end
            for _, d in ipairs(paint.dots) do p:circle(d[1], d[2], 8, "error") end
            if p.hovered then p:circle(p.mouseX, p.mouseY, 18, "primary/0.25") end
            p:text(16, 12, "Drawn from Lua", "onSurface", "titleMedium")
        end,
    },
    Button { text = "Clear dots", variant = "outlined", onClicked = function() paint.dots = {} end },
}

-- Theme ------------------------------------------------------------------------
local roles = { "primary", "onPrimary", "primaryContainer", "secondary", "secondaryContainer", "tertiary",
                "tertiaryContainer", "error", "surface", "surfaceContainer", "surfaceContainerHighest", "outline" }

local themePage = Column {
    spacing = 16,
    heading("Theme", "The theme is pure data built in Lua: a Material 3 scheme generated from a seed color, "
        .. "plus any role, ImGui color or style override you like."),
    Row {
        spacing = 8,
        Repeater {
            model = seeds,
            delegate = function(s)
                return Chip { text = s[1], checkable = false,
                              checked = function() return look.seed:upper() == s[2] end,
                              onClicked = function() look.seed = s[2] end }
            end,
        },
    },
    ColorPicker { label = "Seed color", color = bind(look, "seed") },
    Row {
        spacing = 24,
        Switch { text = "Dark mode", checked = bind(look, "dark") },
        Switch { text = "Neon override", checked = bind(look, "neon") },
    },
    ComboBox { label = "Preset", model = presets, currentIndex = bind(look, "preset") },
    Text { text = "Roundness", style = "titleSmall" },
    Slider { value = bind(look, "roundness"), from = 0, to = 2, width = 420 },
    Text { text = "Spacing", style = "titleSmall" },
    Slider { value = bind(look, "spacing"), from = 4, to = 14, stepSize = 1, width = 420 },
    Text { text = "Font scale", style = "titleSmall" },
    Slider { value = bind(look, "fontScale"), from = 0.8, to = 1.4, width = 420 },
    Separator { text = "Scheme" },
    Grid {
        columns = 4,
        Repeater {
            model = roles,
            delegate = function(role)
                return Row {
                    spacing = 8,
                    Canvas { width = 28, height = 28, onPaint = function(_, p)
                        p:rect(0, 0, p.width, p.height, role, 8)
                        p:rect(0, 0, p.width, p.height, "outlineVariant", 8, true)
                    end },
                    Text { text = role, style = "bodySmall" },
                }
            end,
        },
    },
}

-------------------------------------------------------------------------------
-- Shell: navigation rail + page loader
-------------------------------------------------------------------------------
local pages = {
    { "Buttons", "Variants & FAB", buttonsPage },
    { "Selection", "Toggles & chips", selectionPage },
    { "Inputs", "Fields & sliders", inputsPage },
    { "Feedback", "Progress & dialogs", feedbackPage },
    { "Data", "Lists, tables, plots", dataPage },
    { "Canvas", "Custom painting", canvasPage },
    { "Theme", "Live theme editor", themePage },
}
-- GALLERY_PAGE=n opens page n at startup (handy for screenshots / testing).
local nav = state { page = tonumber(os.getenv("GALLERY_PAGE") or "") or 1, demo = false }

return ApplicationWindow {
    title = function() return "imlui gallery — " .. pages[nav.page][1] end,
    width = 1200, height = 820,
    padding = 0,

    menuBar = MenuBar {
        Menu {
            title = "File",
            MenuItem { text = "Reload script", shortcut = "auto on save", onTriggered = reload },
            Separator {},
            MenuItem { text = "Quit", onTriggered = quit },
        },
        Menu {
            title = "View",
            MenuItem { text = "Dark mode", checked = bind(look, "dark") },
            MenuItem { text = "ImGui demo window", checked = bind(nav, "demo") },
        },
    },

    Row {
        align = "top",
        ScrollView {
            width = 260, padding = 12,
            Text { text = "imlui", style = "titleLarge", color = "primary" },
            Text { text = "Lua + ImGui + Material 3", style = "bodySmall", color = "onSurfaceVariant" },
            Spacer { height = 8 },
            Repeater {
                model = pages,
                delegate = function(p, i)
                    return ListItem { text = p[1], supportingText = p[2],
                                      selected = function() return nav.page == i end,
                                      onClicked = function() nav.page = i end }
                end,
            },
        },
        ScrollView {
            padding = 28,
            Loader { item = function() return pages[nav.page][3] end },
        },
    },

    ImGuiDemo { open = bind(nav, "demo"), visible = function() return nav.demo end },
}
