--[[
  imlui — declarative, QML-like UI description in Lua, rendered by C++ with
  Dear ImGui and a Material 3 theme.

  A UI script runs in an environment where every element type is a global
  constructor, and returns the root element:

      local s = state { count = 0 }

      return ApplicationWindow {
          title = "Counter",
          Column {
              spacing = 12,
              Text { text = function() return "Clicked " .. s.count .. " times" end,
                     style = "headlineSmall" },
              Button { text = "Click me", onClicked = function() s.count = s.count + 1 end },
          },
      }

  Properties
    plain value      text = "Hello"
    binding          text = function(self) return ... end   -- re-evaluated every frame
    two-way binding  value = bind(s, "volume")              -- widget edits write s.volume

  Signals are on* handlers called as handler(self, ...):
    onClicked(self)  onToggled(self, checked)  onMoved(self, value)
    onTextChanged(self, text)  onAccepted(self, text)  onActivated(self, index, text)

  Indices (currentIndex, currentRow, ...) are 1-based, like everything in Lua.
]]

local native = require("imlui.native")

local ui = {}
ui.native = native
ui.ids = {}

-------------------------------------------------------------------------------
-- Elements
-------------------------------------------------------------------------------

--- Returns a constructor for element type `name`. Calling it with a table
--- turns that table into the element (no copy), so it keeps its identity
--- for the whole lifetime of the UI — that identity is what ImGui IDs and
--- per-element state hang off.
function ui.element(name)
    return function(props)
        if type(props) ~= "table" then props = { props } end
        props.__type = name
        if props.id ~= nil then ui.ids[props.id] = props end
        return props
    end
end

local BUILTIN = {
    -- windows
    "ApplicationWindow", "Window", "Dialog",
    -- layout
    "Column", "Item", "Row", "Grid", "ScrollView", "Card", "TabBar", "Tab", "Section", "Expander", "TreeNode",
    -- app bars, navigation & menus
    "TopAppBar", "NavigationItem", "MenuBar", "Menu", "MenuItem",
    -- text & decoration
    "Text", "Label", "Icon", "Link", "Separator", "Divider", "Spacer",
    -- controls
    "Button", "IconButton", "Checkbox", "Switch", "RadioButton", "RadioGroup", "Chip", "SegmentedButton", "ListItem",
    "Slider", "SpinBox", "TextField", "TextArea", "ComboBox", "ColorPicker",
    -- data & feedback
    "ProgressBar", "CircularProgress", "BusyIndicator", "Plot", "Table", "Canvas",
    -- non-visual / debugging
    "Timer", "ImGuiDemo",
}
for _, name in ipairs(BUILTIN) do ui[name] = ui.element(name) end

-------------------------------------------------------------------------------
-- State & bindings
-------------------------------------------------------------------------------

local watchers = setmetatable({}, { __mode = "k" }) -- proxy -> { key -> {fn...} }
local stores = setmetatable({}, { __mode = "k" })   -- proxy -> raw data

--- Observable state table. Reads/writes look like a plain table; writes
--- notify watchers registered with ui.watch().
function ui.state(init)
    local data = {}
    for k, v in pairs(init or {}) do data[k] = v end
    local proxy = {}
    stores[proxy] = data
    watchers[proxy] = {}
    return setmetatable(proxy, {
        __index = data,
        __newindex = function(_, k, v)
            local old = data[k]
            if old == v then return end
            data[k] = v
            local w = watchers[proxy]
            for _, fn in ipairs(w[k] or {}) do fn(v, old, k) end
            for _, fn in ipairs(w["*"] or {}) do fn(v, old, k) end
        end,
        __pairs = function() return next, data, nil end,
        __len = function() return #data end,
    })
end

--- Calls fn(new, old, key) whenever state[key] changes ("*" = any key).
function ui.watch(state, key, fn)
    local w = watchers[state]
    assert(w, "ui.watch: not a ui.state table")
    w[key] = w[key] or {}
    table.insert(w[key], fn)
    return fn
end

--- Raw (non-proxy) contents of a state table, e.g. for serialization.
function ui.raw(state) return stores[state] or state end

--- Two-way binding to t[k]. Also: ui.bind(getter, setter).
function ui.bind(t, k)
    if type(t) == "function" then return { __bind = true, get = t, set = k } end
    return { __bind = true, t = t, k = k }
end

--- Reusable component: a function from props to an element tree.
--- Locals created inside are per instance (like a QML component's own properties).
function ui.component(fn)
    return function(props) return fn(props or {}) end
end

-------------------------------------------------------------------------------
-- Dynamic children: Repeater, ListView, If, Loader
-------------------------------------------------------------------------------

local EMPTY = {}

local function resolve(self, v)
    if type(v) == "function" then return v(self) end
    if type(v) == "table" and v.__bind then
        if v.t then return v.t[v.k] end
        return v.get()
    end
    return v
end

--- Builds (and caches) delegate instances for a model. Delegates are only
--- re-created when the item at an index changes, so per-delegate state survives.
local function repeaterChildren(self)
    local model = resolve(self, self.model)
    if type(model) == "number" then
        local n = model
        model = {}
        for i = 1, n do model[i] = i end
    end
    model = model or EMPTY
    local cache = self.__cache
    if not cache then cache = {}; self.__cache = cache end
    local list = self.__list
    if not list then list = {}; self.__list = list end
    local n = #model
    for i = 1, n do
        local item = model[i]
        local c = cache[i]
        if not c or c.item ~= item then
            c = { item = item, node = self.delegate(item, i) }
            cache[i] = c
        end
        list[i] = c.node
    end
    for i = n + 1, #list do list[i] = nil; cache[i] = nil end
    return list
end

--- Repeater { model = list | count | function, delegate = function(item, index) ... end }
--- Its delegates are laid out by the parent (inside a Row they flow horizontally).
function ui.Repeater(p)
    p.__type = "Repeater"
    p.__inline = true
    p.__children = repeaterChildren
    return p
end

--- ListView { model = ..., delegate = ..., height = 300, spacing = 4 } — a scrolling Repeater.
function ui.ListView(p)
    p.__type = "ListView"
    if p.delegate then p.__children = repeaterChildren end
    return p
end

--- If { condition = function() ... end, ElementA, ElementB, otherwise = ElementC }
function ui.If(p)
    p.__type = "If"
    p.__inline = true
    local others = p.otherwise and { p.otherwise } or EMPTY
    p.__children = function(self)
        if resolve(self, self.condition) then return self end
        return others
    end
    return p
end

--- Loader { item = function() return pages[s.page] end } — shows one element.
function ui.Loader(p)
    p.__type = "Loader"
    p.__inline = true
    local box = {}
    p.__children = function(self)
        box[1] = resolve(self, self.item)
        return box
    end
    return p
end

-------------------------------------------------------------------------------
-- Theme (decided entirely on the Lua side)
-------------------------------------------------------------------------------

--[[
  ui.theme {
      preset    = "material",   -- or "imgui-dark" | "imgui-light" | "imgui-classic"
      seed      = "#6750A4",    -- Material 3 scheme is generated from this color
      dark      = true,
      spacing   = 8,            -- base spacing unit (dp)
      fontScale = 1.0,
      shape     = { extraSmall = 4, small = 8, medium = 12, large = 16, extraLarge = 28 },
      colors    = { primary = "#FF5722", surface = "#101010" },   -- override any role
      imgui     = { WindowBg = "#000000" },                      -- override raw ImGuiCol_*
      style     = { FrameRounding = 6, ItemSpacing = { 10, 6 } }, -- override raw ImGuiStyle fields
  }
  Returns the resulting color scheme { primary = "#...", ... }.
  Color props everywhere accept a role name ("primary"), role with alpha
  ("onSurface/0.6"), a hex string, or {r, g, b, a} in 0..1.
]]
function ui.theme(t)
    ui.__theme = t
    return native.setTheme(t)
end

--- Current color scheme as { role = "#RRGGBB" }.
function ui.scheme() return native.scheme() end

--- Scheme a theme table *would* produce, without applying it.
function ui.schemeFor(t) return native.schemeFor(t) end

--- A ui.state whose every change re-applies it as the theme. Bind switches,
--- color pickers, sliders... directly to it.
function ui.themeState(init)
    local s = ui.state(init)
    local function apply() ui.theme(ui.raw(s)) end
    ui.watch(s, "*", apply)
    apply()
    return s
end

ui.color = {
    mix = native.mix,        -- mix(a, b, t)
    tone = native.tone,      -- tone(color, 0..100)  (Material tone of the color's hue)
    alpha = native.alpha,    -- alpha(color, a)
    resolve = native.color,  -- resolve("primary") -> "#RRGGBB"
}

-------------------------------------------------------------------------------
-- Timers & misc
-------------------------------------------------------------------------------

local timers, nextTimer = {}, 0

local function addTimer(fn, ms, rep)
    nextTimer = nextTimer + 1
    timers[nextTimer] = { fn = fn, interval = (ms or 0) / 1000, due = native.time() + (ms or 0) / 1000, rep = rep }
    return nextTimer
end
function ui.setTimeout(fn, ms) return addTimer(fn, ms, false) end
function ui.setInterval(fn, ms) return addTimer(fn, ms, true) end
function ui.clearTimer(id) timers[id] = nil end

local frameCallbacks = {}
--- Calls fn(time) at the start of every frame.
function ui.onFrame(fn) table.insert(frameCallbacks, fn) end

--- Called by the engine at the start of every frame.
function ui.__tick(now)
    -- Collect first: callbacks may add or clear timers.
    local due = {}
    for id, t in pairs(timers) do
        if now >= t.due then due[#due + 1] = id end
    end
    for _, id in ipairs(due) do
        local t = timers[id]
        if t then
            if t.rep then t.due = now + t.interval else timers[id] = nil end
            t.fn()
        end
    end
    for _, fn in ipairs(frameCallbacks) do fn(now) end
end

-- Snackbar: one app-wide instance, rendered by the engine above everything.
local snack = ui.element("Snackbar") { shownAt = -1 }
ui.__overlays = { snack }

--- Shows a brief message at the bottom of the window.
---   ui.snackbar("Item removed", { action = "Undo", onAction = fn, duration = 4000, closable = false })
function ui.snackbar(text, opts)
    opts = opts or {}
    snack.text = text
    snack.actionText = opts.action or ""
    snack.onAction = opts.onAction and function() opts.onAction() end or nil
    snack.duration = opts.duration or 4000
    snack.closable = opts.closable or false
    snack.shownAt = native.time()
end

-- Logging goes through spdlog on the C++ side.
local function logAt(level)
    return function(fmt, ...)
        local msg = select("#", ...) > 0 and string.format(fmt, ...) or tostring(fmt)
        native.log(level, msg)
    end
end
ui.log = { trace = logAt("trace"), debug = logAt("debug"), info = logAt("info"),
           warn = logAt("warn"), error = logAt("error") }

--- print() in UI scripts logs at info level.
function ui.print(...)
    local parts = {}
    for i = 1, select("#", ...) do parts[i] = tostring((select(i, ...))) end
    native.log("info", table.concat(parts, "\t"))
end

ui.time = native.time
ui.fps = native.fps
ui.quit = native.quit
ui.reload = native.reload
ui.setTitle = native.setTitle
ui.textSize = native.textSize

-------------------------------------------------------------------------------
-- Script environment & loading
-------------------------------------------------------------------------------

-- Lookup order for free names inside UI scripts: ui.*, Lua globals, element
-- ids (QML style: `nameField.text`), then any Capitalized name becomes an
-- element constructor (so elements registered from C++ just work).
ui.env = setmetatable({ ui = ui }, {
    __index = function(_, k)
        local v = ui[k]
        if v ~= nil then return v end
        v = _G[k]
        if v ~= nil then return v end
        v = ui.ids[k]
        if v ~= nil then return v end
        if type(k) == "string" and k:match("^%u") then
            local ctor = ui.element(k)
            ui[k] = ctor
            return ctor
        end
        return nil
    end,
})

-- `require "MyComponent"` inside a UI script loads MyComponent.lua from the
-- script's directory in the UI environment (like QML files as components).
table.insert(package.searchers, 2, function(name)
    if not ui.__dir then return nil end
    local path = package.searchpath(name, ui.__dir .. "/?.lua")
    if not path then return "\n\tno UI component '" .. ui.__dir .. "/" .. name .. ".lua'" end
    return function()
        local chunk = assert(loadfile(path, "t", ui.env))
        return chunk(name)
    end, path
end)
ui.import = require

function ui.__load(path)
    ui.__dir = path:match("^(.*)[/\\]") or "."
    local chunk, err = loadfile(path, "t", ui.env)
    if not chunk then error(err, 0) end
    return chunk()
end

return ui
