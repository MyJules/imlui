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

-- C++ types registered with Engine::registerType<T>() (absent when the library runs without an engine).
local typesOk, types = pcall(require, "imlui.types")
if not typesOk then types = nil end

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
--- For a type registered from C++ (ui.types()), the constructor also creates
--- the C++ instance (stored as `__native`), and the element's methods become
--- callable as `element:method(...)`.
function ui.element(name)
    local typed = types ~= nil and types.isType(name)
    return function(props)
        if type(props) ~= "table" then props = { props } end
        props.__type = name
        if typed then types.attach(props) end
        if props.id ~= nil then
            ui.checkId(props.id)
            ui.ids[props.id] = props
        end
        return props
    end
end

--- Shows a script error in the error overlay (logged once; the script keeps running).
function ui.reportError(msg)
    if native.reportError then native.reportError(msg) else native.log("error", msg) end
end

--- Reports an element id that can't be referenced by name in a UI script: free names are looked up in
--- `ui`, then in the Lua globals, and only then in the element ids (see ui.env), so an id like `io` or
--- `text` would silently resolve to the global or the ui member instead. Returns false for such an id.
function ui.checkId(id)
    if type(id) ~= "string" then return true end
    local what
    if rawget(ui, id) ~= nil then
        what = "ui member 'ui." .. id .. "'"
    elseif _G[id] ~= nil then
        what = "Lua global '" .. id .. "'"
    else
        return true
    end
    ui.reportError(("element id '%s' collides with %s; it can't be referenced by name — rename it"):format(id, what))
    return false
end

--- Names of the element types registered from C++ with Engine::registerType<T>(), sorted.
function ui.types()
    if types == nil then return {} end
    return types.names()
end

local BUILTIN = {
    -- windows
    "ApplicationWindow", "Window", "Dialog",
    -- layout
    "Column", "Item", "Row", "Grid", "ScrollView", "Card", "TabBar", "Tab", "Section", "Expander", "TreeNode",
    -- app bars, navigation & menus
    "TopAppBar", "NavigationItem", "NavigationBar", "NavigationRail", "ModalNavigationDrawer",
    "MenuBar", "Menu", "MenuItem",
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

--- RadioButtons sharing a `group` value act as one group: the C++ handler
--- unchecks the other members (through their bindings) when one is clicked.
--- ui.__radioGroups[group] is a weak-keyed set of the group's elements.
ui.__radioGroups = {}
local radioButton = ui.RadioButton
function ui.RadioButton(props)
    props = radioButton(props)
    local group = props.group
    if group ~= nil then
        local members = ui.__radioGroups[group]
        if not members then
            members = setmetatable({}, { __mode = "k" })
            ui.__radioGroups[group] = members
        end
        members[props] = true
        props.__radioGroup = members
    end
    return props
end

-------------------------------------------------------------------------------
-- State & bindings
-------------------------------------------------------------------------------

local watchers = setmetatable({}, { __mode = "k" }) -- proxy -> { key -> {fn...} }
local EMPTY_LIST = {}
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
            -- Iterate copies: a watcher may remove itself (or others) while running.
            local w = watchers[proxy]
            for _, fn in ipairs({ table.unpack(w[k] or EMPTY_LIST) }) do fn(v, old, k) end
            for _, fn in ipairs({ table.unpack(w["*"] or EMPTY_LIST) }) do fn(v, old, k) end
        end,
        __pairs = function() return next, data, nil end,
        __len = function() return #data end,
    })
end

-- Removes fn from list (by identity) if it is still there.
local function removeFrom(list, fn)
    for i = #list, 1, -1 do
        if list[i] == fn then
            table.remove(list, i)
            return
        end
    end
end

--- Calls fn(new, old, key) whenever state[key] changes ("*" = any key).
--- Returns a function that removes the watcher: local stop = ui.watch(...); stop()
function ui.watch(state, key, fn)
    local w = watchers[state]
    assert(w, "ui.watch: not a ui.state table")
    w[key] = w[key] or {}
    table.insert(w[key], fn)
    local list = w[key]
    return function() removeFrom(list, fn) end
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

--- Material 3 window size classes, smallest first.
ui.SIZE_CLASSES = { "compact", "medium", "expanded", "large", "extraLarge" }
local SIZE_INDEX = {}
for i, name in ipairs(ui.SIZE_CLASSES) do SIZE_INDEX[name] = i end

--- The value `p` defines for size class `class`: p[class] if set, else the nearest
--- smaller class that is set, else the smallest one that is set. `false` counts as
--- set (it means "nothing" for that class). Returns nil when no class is set.
function ui.pickSizeClass(p, class)
    local i = SIZE_INDEX[class] or 1
    for j = i, 1, -1 do
        local v = p[ui.SIZE_CLASSES[j]]
        if v ~= nil then return v end
    end
    for j = i + 1, #ui.SIZE_CLASSES do
        local v = p[ui.SIZE_CLASSES[j]]
        if v ~= nil then return v end
    end
    return nil
end

--- Responsive { compact = A, medium = B, expanded = C, large = D, extraLarge = E }
--- shows the element for the current window size class (see ui.sizeClass), falling
--- back to the nearest smaller class that is given. Inline, like Loader. The
--- elements are built once, so their state survives switching between classes.
function ui.Responsive(p)
    p.__type = "Responsive"
    p.__inline = true
    local box = {}
    p.__children = function(self)
        local v = ui.pickSizeClass(self, ui.sizeClass())
        if type(v) == "function" then v = v(self) end
        box[1] = type(v) == "table" and v or nil
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
--- Calls fn(time) at the start of every rendered frame. Frames are rendered on
--- demand: call ui.requestFrame() from fn to keep them coming while animating.
--- Returns a function that removes the callback.
function ui.onFrame(fn)
    table.insert(frameCallbacks, fn)
    return function() removeFrom(frameCallbacks, fn) end
end

--- Asks for a frame to be rendered (after `ms` milliseconds if given). The engine
--- only renders on input, animations, timers and these requests.
function ui.requestFrame(ms) native.requestFrame((ms or 0) / 1000) end

--- Called by the engine at the start of every frame. Returns the seconds until
--- the next timer is due (nil when there are no timers), so the engine wakes up for it.
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
    -- C++ elements that tick every frame, rendered or not (Timer).
    if types ~= nil and types.tick then types.tick(now) end
    -- Iterate a copy: a callback may remove itself while running.
    for _, fn in ipairs({ table.unpack(frameCallbacks) }) do fn(now) end
    local nextDue
    for _, t in pairs(timers) do
        if not nextDue or t.due < nextDue then nextDue = t.due end
    end
    return nextDue and math.max(0, nextDue - now) or nil
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
ui.deltaTime = native.deltaTime
ui.fps = native.fps
ui.isDark = native.isDark
ui.imguiVersion = native.imguiVersion
ui.quit = native.quit
ui.reload = native.reload
ui.setTitle = native.setTitle
ui.textSize = native.textSize

-- Scaling: sizes are in dp; dp -> pixels = monitor content scale * user zoom.
ui.zoom = native.zoom                       -- zoom() -> current (or pending) zoom factor
ui.setZoom = native.setZoom                 -- setZoom(z), clamped to 0.5..3, applied next frame
ui.dpiScale = native.dpiScale               -- dpiScale() -> effective dp -> pixel factor
ui.contentScale = native.contentScale       -- contentScale() -> monitor content scale
ui.setZoomShortcuts = native.setZoomShortcuts -- setZoomShortcuts(false) turns off Ctrl +/-/0/wheel

-- Window size (dp) and Material 3 window size classes.
ui.windowSize = native.windowSize           -- windowSize() -> w, h of the window in dp (pixels / dpiScale())
ui.sizeClass = native.sizeClass             -- sizeClass() -> "compact" | "medium" | "expanded" | "large" | "extraLarge"
ui.sizeClassFor = native.sizeClassFor       -- sizeClassFor(widthDp) -> name
ui.setWindowSize = native.setWindowSize     -- setWindowSize(w, h) in dp (times the content scale), after this frame

-------------------------------------------------------------------------------
-- Script environment & loading
-------------------------------------------------------------------------------

-- Lookup order for free names inside UI scripts: ui.*, Lua globals (C++
-- singletons are globals), element ids (QML style: `nameField.text`), then any
-- Capitalized name becomes an element constructor (so elements registered
-- from C++ just work; registered types get typed constructors).
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

-------------------------------------------------------------------------------
-- Resources
-------------------------------------------------------------------------------

-- Scripts and data are resources named `prefix:/path` (`app:/main.lua`, `imlui:/imlui.lua`), served by the
-- engine's mounts. The native library is absent when the library runs without an engine.
local resourceOk, resource = pcall(require, "imlui.resource")
if not resourceOk then resource = nil end

local function needResources()
    if resource == nil then error("ui.resource: no resource system (the library runs without an engine)", 3) end
end

-- `name` resolved against the folder of the script that called the ui.resource function calling this (its chunk
-- name is `@prefix:/...`), or of the main script when that code isn't a resource (code loaded from a string).
-- Errors at that caller on an invalid name. Stack: 1 = resolve, 2 = the ui.resource function, 3 = its caller.
local function resolve(name)
    if type(name) ~= "string" then error("resource name must be a string, got " .. type(name), 3) end
    local base = ui.__script or "app:/"
    local api = debug.getinfo(2, "t")
    if api and api.istailcall then
        -- `return ui.resource.read("x")`: Lua dropped the caller's frame, so its script is unknown.
        if not name:match("^%a[%w_%-]*:") then
            error("ui.resource: a relative name in a tail call (`return ui.resource.f(name)`) can't be resolved; " ..
                  "use a full name or wrap the call in parentheses", 3)
        end
    else
        local caller = debug.getinfo(3, "S")
        local source = caller and caller.source
        if source and source:match("^@%l[%w_%-]+:/") then base = source:sub(2) end
    end
    local full, err = resource.resolve(base, name)
    if not full then error(err, 3) end
    return full
end

--- ui.resource: files from the app's resources. Names are full (`app:/data/items.json`, `imlui:/...`) or
--- relative to the folder of the calling script (`data/items.json`, `../shared/x.lua`).
--- (The results are kept in locals below: a tail call would hide the caller from resolve().)
ui.resource = {}

--- The full, normalized name of `name`.
function ui.resource.url(name)
    needResources()
    local full = resolve(name)
    return full
end

--- The contents of resource `name` as a string, or nil and a message when there is no such file.
function ui.resource.read(name)
    needResources()
    local full = resolve(name)
    local text, err = resource.read(full)
    return text, err
end

--- True when resource `name` is a file or a folder.
function ui.resource.exists(name)
    needResources()
    local full = resolve(name)
    local found = resource.exists(full)
    return found
end

-------------------------------------------------------------------------------
-- Script loading
-------------------------------------------------------------------------------

-- `require "MyComponent"` inside a UI script loads MyComponent.lua from the script's folder (a resource
-- folder such as `app:/`) in the UI environment, like QML files as components. Modules not found there are
-- looked up in `imlui:/` by the engine's searcher, then on package.path.
table.insert(package.searchers, 2, function(name)
    if not ui.__dir or resource == nil then return nil end
    local full, tried = resource.find(name, ui.__dir)
    if not full then return tried end
    local chunk, err = resource.load(full, ui.env)
    if not chunk then error(err, 0) end
    return chunk, full
end)
ui.import = require

--- Runs the UI script `name` (a full resource name such as `app:/main.lua`) in the UI environment and returns
--- its result. Its folder is where `require` finds components.
function ui.__load(name)
    if resource == nil then error("ui.__load: no resource system", 0) end
    ui.__script = name
    ui.__dir = name:match("^(.*/)") or name
    local chunk, err = resource.load(name, ui.env)
    if not chunk then error(err, 0) end
    return chunk()
end

return ui
