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
-------------------------------------------------------------------------------
-- Element ids and their scopes
-------------------------------------------------------------------------------

-- Element ids live in scopes. App-level ids (ui.ids) are the ones declared outside every component instance and
-- delegate; they are the free names of UI scripts (see ui.env). A component instance (ui.component) and a Repeater
-- or ListView delegate each get a scope of their own, the `ids` table passed to the function that builds them: ids
-- declared while it runs register there, are private to it and aren't free names. Every ids table is weak-valued,
-- so an id never keeps its element alive: the tree (or whatever else references the element) does.
local function weakIds() return setmetatable({}, { __mode = "v" }) end
ui.ids = weakIds()

-- The scope stack, innermost last: { ids = table, what = function() -> description }. Empty: app level.
local scopes = {}

-- The elements registered under an id since the last ui.__tick. Ids are weak, so until then, while a script still
-- builds its tree (`Text { id = "title" }` as a statement, used by name in the root later), they're kept here.
local pinned = {}

-- A to-be-closed value that pops the innermost scope: `local _ <close> = POP_SCOPE` right after pushing one keeps
-- the stack right however the block is left, a component that raises included.
local POP_SCOPE = setmetatable({}, { __close = function() scopes[#scopes] = nil end })

-- Calls fn(...) with `scope` as the innermost scope and returns its results.
local function inScope(scope, fn, ...)
    scopes[#scopes + 1] = scope
    local _ <close> = POP_SCOPE
    return fn(...)
end

-- The scope each element was built in, for the elements built inside one (weak keys). The element's functions run
-- in it: its bindings and on* handlers called from C++ (through native.setScopeHooks: the C++ side passes those calls
-- through inScope), and the functions the Lua-side containers call (`model`, `condition`, a Loader's `item`, a
-- Responsive class: see resolve). Elements built by a handler of a component's element so register in the
-- component instance's `ids`. An element built at app level isn't here: its functions run in whatever scope is
-- active, which is app level whenever the engine calls them.
local scopeOf = setmetatable({}, { __mode = "k" })
if native.setScopeHooks then native.setScopeHooks(scopeOf, inScope) end

-- `fn` as a function that runs in the current scope when called later (a timer, a watcher), or `fn` itself at app
-- level.
local function inCurrentScope(fn)
    local scope = scopes[#scopes]
    if scope == nil then return fn end
    return function(...) return inScope(scope, fn, ...) end
end

-- Calls fn(...) at app level (an empty scope stack) and returns its results: a module's top level runs there,
-- whoever requires it first.
local function atAppLevel(fn, ...)
    local saved = scopes
    scopes = {}
    local _ <close> = setmetatable({}, { __close = function() scopes = saved end })
    return fn(...)
end

-- A new scope for one instance of `kind` built by `fn`; `what()` names it in duplicate-id reports.
local function newScope(kind, fn)
    return {
        ids = weakIds(),
        what = function()
            local info = type(fn) == "function" and debug.getinfo(fn, "S")
            if info then return ("%s (%s:%d)"):format(kind, info.short_src, info.linedefined) end
            return kind
        end,
    }
end

-- Registers element `el` under `id` in `scope` (nil: app level). The first element registered under an id keeps
-- it; another one is reported as a duplicate. The same element again is fine.
local function registerIn(scope, el, id)
    local ids = scope and scope.ids or ui.ids
    local existing = ids[id]
    if existing == el then return end
    if existing ~= nil then
        ui.reportError(("duplicate element id '%s' in %s; the first element keeps it"):format(
            tostring(id), scope and scope.what() or "the app scope"))
        return
    end
    if scope == nil then ui.checkId(id) end
    ids[id] = el
    pinned[#pinned + 1] = el
end

--- Registers `props.id` (if set) in the current scope: the innermost component instance or delegate being built,
--- or app level (ui.ids, checked with ui.checkId) outside of them. Every element constructor calls it, the Lua-side
--- containers (Repeater, ListView, If, Loader, Responsive) too.
function ui.registerId(props)
    local id = rawget(props, "id")
    if id ~= nil then registerIn(scopes[#scopes], props, id) end
end

--- Shows a script error in the error overlay (logged once; the script keeps running).
function ui.reportError(msg)
    if native.reportError then native.reportError(msg) else native.log("error", msg) end
end

--- Reports an app-level element id that can't be referenced by name in a UI script: free names are looked up in
--- `ui`, then in the Lua globals, and only then in the app-level ids (see ui.env), so an id like `io` or `text`
--- would silently resolve to the global or the ui member instead. Returns false for such an id. Ids in a component
--- or delegate scope aren't free names, so registering one doesn't check it.
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

-------------------------------------------------------------------------------
-- Parent links
-------------------------------------------------------------------------------

-- `element.parent` is the element that lays the element out: read-only, and weak (it never keeps the parent or the
-- element alive). It lives in one side table, `parents` [element] = parent (weak keys and values), not in the
-- element: the metatable that all elements of a type share reads it (its __index answers `parent` from the table,
-- anything else from the type's methods; see imlui.types `meta` and `parents`). Elements of a type that isn't
-- registered (or without the C++ types) share PLAIN_META. Writes to an element go straight to the table: a script
-- that assigns `parent` stores a raw field, which the C++ sync drops (and debug builds report).
local parents = types ~= nil and types.parents or setmetatable({}, { __mode = "kv" })
-- (__imlui_shared: holds no properties, so C++ property reads skip it, see luaui::Node::pushRaw.)
local PLAIN_META = { __imlui_shared = true, __index = function(t, k) if k == "parent" then return parents[t] end end }

local function isElement(v) return type(v) == "table" and type(rawget(v, "__type")) == "string" end

-- Gives element `el` the metatable its type shares, unless it has one: a registered type's (types.meta, without
-- creating the instance, which the first render or method call does), else PLAIN_META.
local function shareMeta(el)
    if getmetatable(el) ~= nil then return end
    setmetatable(el, types ~= nil and types.meta(rawget(el, "__type")) or PLAIN_META)
end

-- The parent of `el`: nil for the root, or for an element no container took yet.
local function parentOf(el) return parents[el] end

-- The element that lays out the children of `el`: `el` itself, or for an inline container (Repeater, If, Loader,
-- Responsive), which its parent splices its children into, that parent (QML's visual parent). An inline container
-- that is the root (or isn't in a tree yet) holds its children itself.
local function holder(el)
    if rawget(el, "__inline") then return parentOf(el) or el end
    return el
end

-- Element-valued props that are children too (the element renders them).
local CHILD_KEYS = { "otherwise", "menuBar", "header", "menu" }
local SIZE_CLASS_KEYS = { "compact", "medium", "expanded", "large", "extraLarge" }

-- Calls f(child, arg) for the children `el` holds now: positional, CHILD_KEYS, a Responsive's classes, the delegates a
-- Repeater or ListView built and the item a Loader or Responsive shows. Positional children are walked to rawlen, as
-- the renderer does (not ipairs, which stops at a nil hole: the children after one would render without a parent).
local function forEachChild(el, f, arg)
    for i = 1, rawlen(el) do
        local c = rawget(el, i)
        if isElement(c) then f(c, arg) end
    end
    for _, k in ipairs(CHILD_KEYS) do
        local c = rawget(el, k)
        if isElement(c) then f(c, arg) end
    end
    if rawget(el, "__type") == "Responsive" then
        for _, k in ipairs(SIZE_CLASS_KEYS) do
            local c = rawget(el, k)
            if isElement(c) then f(c, arg) end
        end
    end
    local list = rawget(el, "__list")
    if list then
        for _, c in ipairs(list) do
            if isElement(c) then f(c, arg) end
        end
    end
    local box = rawget(el, "__box")
    if box and isElement(box[1]) then f(box[1], arg) end
end

-- Makes `parent` the parent of `child` (nothing for a value that isn't an element). The children of an inline
-- container follow it: their parent is its new holder.
local function setParent(child, parent)
    if not isElement(child) or child == parent then return end
    shareMeta(child)
    if parents[child] == parent then return end
    parents[child] = parent
    if rawget(child, "__inline") then
        forEachChild(child, setParent, holder(child))
    end
end

-- The end of every constructor, once `__type` is set: the shared metatable (a `parent` prop is reported and
-- dropped), the children's parent links, the scope the element is built in (see scopeOf) and the id.
local function finishElement(props)
    if rawget(props, "parent") ~= nil then
        ui.reportError(("%s: 'parent' is read-only (the element's container sets it); the value is ignored"):format(
            tostring(rawget(props, "__type"))))
        rawset(props, "parent", nil)
    end
    shareMeta(props)
    forEachChild(props, setParent, holder(props))
    local scope = scopes[#scopes]
    if scope ~= nil then scopeOf[props] = scope end
    ui.registerId(props)
    return props
end

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
--- The constructor sets the parent links of the element's children (`child.parent`) and registers its id.
function ui.element(name)
    local typed = types ~= nil and types.isType(name)
    return function(props)
        if type(props) ~= "table" then props = { props } end
        props.__type = name
        if typed then types.attach(props) end
        return finishElement(props)
    end
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
    "Text", "Label", "Icon", "Image", "Link", "Separator", "Divider", "Spacer",
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
--- A watcher added inside a component instance or delegate runs in its id scope.
function ui.watch(state, key, fn)
    local w = watchers[state]
    assert(w, "ui.watch: not a ui.state table")
    fn = inCurrentScope(fn)
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

--- Reusable component: `fn(props, ids)` builds one instance's element tree. Locals created inside are per instance
--- (like a QML component's own properties), and so is `ids`: the ids declared while fn runs (nested elements
--- included, but not those of a nested component instance, which has its own) register there instead of at app
--- level, so two instances don't clash and the ids aren't free names. `MyComp { id = "x" }` registers the instance's
--- root element as `x` in the scope that creates the instance. The functions of the instance's elements (bindings,
--- handlers) and the timers and watchers it sets up run in its scope later too (see scopeOf), so the elements they
--- build register in `ids` as well.
function ui.component(fn)
    return function(props)
        props = props or {}
        local scope = newScope("component", fn)
        local root = inScope(scope, fn, props, scope.ids)
        local id = type(props) == "table" and rawget(props, "id") or nil
        if id ~= nil and type(root) == "table" then registerIn(scopes[#scopes], root, id) end
        return root
    end
end

-------------------------------------------------------------------------------
-- Dynamic children: Repeater, ListView, If, Loader
-------------------------------------------------------------------------------

local EMPTY = {}

-- The value of a container's prop `v`: v(self) for a function (in the scope `self` was built in, see scopeOf),
-- the bound value for a bind, else v.
local function resolve(self, v)
    if type(v) == "function" then
        local scope = scopeOf[self]
        if scope ~= nil then return inScope(scope, v, self) end
        return v(self)
    end
    if type(v) == "table" and rawget(v, "__bind") then -- raw, like the C++ side: an __index doesn't run
        if v.t then return v.t[v.k] end
        return v.get()
    end
    return v
end

-- Ticking C++ elements (Timer) tick until Lua collects their table. When a container drops children (a rebuilt
-- delegate, a Loader item that changed), the next ui.__tick runs a full garbage collection, so a dropped Timer
-- that nothing else references stops right away instead of firing until the next GC cycle. (One still
-- referenced, e.g. a page kept in a table or by an id, is not collected and keeps ticking.)
local function dropped() ui.__dropped = true end

-- Builds the delegate for `item` at index `i` in its own id scope, parented to the container's holder. A delegate
-- that returns nil shows nothing for the item: its node is `false`, so the child list has no hole (`#list` and the
-- renderer's rawlen would stop there) and a keyed entry isn't rebuilt every call.
local function buildDelegate(self, item, i, key)
    local scope = newScope(rawget(self, "__type") .. " delegate", self.delegate)
    local node = inScope(scope, self.delegate, item, i, scope.ids)
    if node == nil then node = false end
    local c = { item = item, key = key, node = node }
    setParent(node, holder(self))
    return c
end

-- Item `i` of a model table, and of a numeric model (no table: the item is the index).
local function rawItem(model, i) return model[i] end
local function numberItem(_, i) return i end

-- The key of `item` at index `i` (the container's `key` function, in the scope the container was built in). A nil
-- or NaN key can't index a table: the item itself is the key then.
local function keyOf(self, keyFn, item, i)
    local scope = scopeOf[self]
    local k
    if scope ~= nil then k = inScope(scope, keyFn, item, i) else k = keyFn(item, i) end
    if k == nil or k ~= k then return item end
    return k
end

-- Delegates by key: one is reused (also at another index) while an item with its key is in the model, so a model
-- that returns fresh item tables every call doesn't rebuild them. In model order, `cache[i]` is the entry shown at
-- index i; entries whose key went away are dropped.
local function keyedChildren(self, model, n, keyFn, cache, list)
    local at = model and rawItem or numberItem
    local oldN = #list
    local i = 1
    -- The common case first: the same keys at the same indices, no table to build.
    while i <= n do
        local c = cache[i]
        local item = at(model, i)
        if c == nil or c.key ~= keyOf(self, keyFn, item, i) then break end
        c.item = item
        i = i + 1
    end
    if i <= n then
        -- Index the entries not reused yet by key (a repeated key chains its entries in order), then take them.
        -- New delegates are built after the matching: `cache` still holds the entries being moved until then.
        local byKey = {}
        for j = oldN, i, -1 do
            local c = cache[j]
            if c.key ~= nil then
                c.nextSame = byKey[c.key]
                byKey[c.key] = c
            end
        end
        local reused = i - 1
        local taken = {}
        for j = i, n do
            local item = at(model, j)
            local k = keyOf(self, keyFn, item, j)
            local c = byKey[k]
            if c ~= nil then
                byKey[k] = c.nextSame
                c.item = item
                reused = reused + 1
            else
                c = { item = item, key = k } -- no node yet
            end
            taken[j - i + 1] = c
        end
        for j = i, n do
            local c = taken[j - i + 1]
            if c.node == nil then c = buildDelegate(self, c.item, j, c.key) end
            c.nextSame = nil
            cache[j] = c
            list[j] = c.node
        end
        if reused < oldN then dropped() end
    elseif n < oldN then
        dropped()
    end
    for j = n + 1, oldN do list[j] = nil; cache[j] = nil end
    return list
end

--- Builds (and caches) delegate instances for a model. Without a `key`, delegates are re-created only when the
--- item at an index changes (`~=`), so per-delegate state survives; with one, a delegate is kept while an item with
--- its key is in the model (see keyedChildren).
local function repeaterChildren(self)
    local model = resolve(self, self.model)
    local n
    if type(model) == "number" then
        -- Items 1..n, without a table of them.
        n = model == model and math.max(0, math.floor(math.min(model, math.maxinteger))) or 0
        model = nil
    else
        model = model or EMPTY
        n = #model
    end
    local cache = self.__cache
    if not cache then cache = {}; self.__cache = cache end
    local list = self.__list
    if not list then list = {}; self.__list = list end
    local keyFn = rawget(self, "key")
    if type(keyFn) == "function" then return keyedChildren(self, model, n, keyFn, cache, list) end
    for i = 1, n do
        local item
        if model then item = model[i] else item = i end
        local c = cache[i]
        if not c or c.item ~= item or c.key ~= nil then
            if c then dropped() end
            c = buildDelegate(self, item, i, nil)
            cache[i] = c
        end
        list[i] = c.node
    end
    if #list > n then dropped() end
    for i = n + 1, #list do list[i] = nil; cache[i] = nil end
    return list
end

--- Repeater { model = list | count | function, delegate = function(item, index, ids) ... end,
---            key = function(item, index) ... end }   -- optional: delegates are kept by key (see keyedChildren)
--- Its delegates are laid out by the parent (inside a Row they flow horizontally), which is also their `parent`.
--- Each delegate instance has an id scope of its own, `ids` (see ui.component).
function ui.Repeater(p)
    p.__type = "Repeater"
    p.__inline = true
    p.__children = repeaterChildren
    return finishElement(p)
end

--- ListView { model = ..., delegate = ..., height = 300, spacing = 4 } — a scrolling Repeater.
function ui.ListView(p)
    p.__type = "ListView"
    if p.delegate then p.__children = repeaterChildren end
    return finishElement(p)
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
    return finishElement(p)
end

-- Shows `item` in `box` (the one-element child list of a Loader or Responsive `self`): parents it, and marks the
-- one it replaces as dropped.
local function show(self, box, item)
    if box[1] == item then return end
    if box[1] ~= nil then dropped() end
    box[1] = item
    setParent(item, holder(self))
end

--- Loader { item = function() return pages[s.page] end } — shows one element.
function ui.Loader(p)
    p.__type = "Loader"
    p.__inline = true
    local box = {}
    p.__box = box
    p.__children = function(self)
        show(self, box, resolve(self, self.item))
        return box
    end
    return finishElement(p)
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
    p.__box = box
    p.__children = function(self)
        local v = ui.pickSizeClass(self, ui.sizeClass())
        if type(v) == "function" then v = resolve(self, v) end
        show(self, box, type(v) == "table" and v or nil)
        return box
    end
    return finishElement(p)
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
      fonts     = {                     -- resource names, full or relative to this script's folder; unset: Roboto
          regular  = "app:/fonts/Inter-Regular.ttf",   -- type-scale faces
          medium   = "fonts/Inter-Medium.ttf",
          fallback = { "app:/fonts/NotoSansJP.otf" },  -- merged into every text font for missing glyphs
          mono     = "app:/fonts/JetBrainsMono.ttf",   -- any other key: a named font, Text { font = "mono" }
      },
  }
  Returns the resulting color scheme { primary = "#...", ... }.
  Color props everywhere accept a role name ("primary"), role with alpha
  ("onSurface/0.6"), a hex string, or {r, g, b, a} in 0..1.
]]

-- The full name of the resource script holding the code that called the function at stack level `level` (1: the
-- caller of callingScript), or nil when that code isn't a resource script (code loaded from a string) or Lua
-- dropped its frame (the function was tail-called; then the second result is true). Scripts loaded as resources
-- don't need this: their `ui` and free names carry their own script (see chunkEnv below); it serves code that uses
-- the plain `ui` table.
local function callingScript(level)
    local fn = debug.getinfo(level + 1, "t")
    if fn and fn.istailcall then return nil, true end
    local caller = debug.getinfo(level + 2, "S")
    local source = caller and caller.source
    if source and source:match("^@%l[%w_%-]+:/") then return source:sub(2) end
    return nil
end

-- Applies theme `t`. Relative font names resolve against the folder of `script` (a full resource name).
local function applyTheme(t, script)
    ui.__theme = t
    return native.setTheme(t, script)
end

function ui.theme(t)
    return applyTheme(t, callingScript(1) or ui.__script or "app:/")
end

--- Current color scheme as { role = "#RRGGBB" }.
function ui.scheme() return native.scheme() end

--- Scheme a theme table *would* produce, without applying it.
function ui.schemeFor(t) return native.schemeFor(t) end

local function themeStateFor(init, script)
    local s = ui.state(init)
    local function apply() applyTheme(ui.raw(s), script) end
    ui.watch(s, "*", apply)
    apply()
    return s
end

--- A ui.state whose every change re-applies it as the theme. Bind switches,
--- color pickers, sliders... directly to it.
function ui.themeState(init)
    return themeStateFor(init, callingScript(1) or ui.__script or "app:/")
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

-- A timer set inside a component instance or delegate runs in its id scope. Frames are on demand: it asks for the
-- frame it's due in (ui.__tick asks for the following ones), also when it was set while the app sleeps (a posted
-- function) or during a frame, whose own request for the next due time was already made.
local function addTimer(fn, ms, rep)
    fn = inCurrentScope(fn)
    nextTimer = nextTimer + 1
    timers[nextTimer] = { fn = fn, interval = ms / 1000, due = native.time() + ms / 1000, rep = rep }
    native.requestFrame(ms / 1000)
    return nextTimer
end
local function describe(ms) return type(ms) == "number" and tostring(ms) or type(ms) end
--- `ms` must be a finite number >= 0 (nil is 0): a NaN or infinite delay would never fire, and a NaN due time would
--- keep the app rendering continuously.
function ui.setTimeout(fn, ms)
    if ms == nil then ms = 0 end
    if type(ms) ~= "number" or not (ms >= 0 and ms < math.huge) then
        error("setTimeout: the delay must be a finite number of milliseconds >= 0, got " .. describe(ms), 2)
    end
    return addTimer(fn, ms, false)
end
--- `ms` must be a positive, finite number: an interval of 0 (or nil) would run every frame and keep the app
--- rendering continuously.
function ui.setInterval(fn, ms)
    if type(ms) ~= "number" or not (ms > 0 and ms < math.huge) then
        error("setInterval: the interval must be a positive, finite number of milliseconds, got " .. describe(ms), 2)
    end
    return addTimer(fn, ms, true)
end
--- Stops a timer. An id that isn't one (nil, from a timer that was never set) is ignored.
function ui.clearTimer(id)
    if id ~= nil and id == id then timers[id] = nil end -- nil or NaN can't index a table
end

local frameCallbacks = {}
--- Calls fn(time) at the start of every rendered frame. Frames are rendered on
--- demand: call ui.requestFrame() from fn to keep them coming while animating.
--- Returns a function that removes the callback.
--- Inside a component instance or delegate, fn runs in its id scope (like the timers and watchers).
function ui.onFrame(fn)
    fn = inCurrentScope(fn)
    table.insert(frameCallbacks, fn)
    return function() removeFrom(frameCallbacks, fn) end
end

--- Asks for a frame to be rendered (after `ms` milliseconds if given). The engine
--- only renders on input, animations, timers and these requests.
function ui.requestFrame(ms) native.requestFrame((ms or 0) / 1000) end

-- Runs fn(...) in protected mode and reports an error (with traceback) instead of raising it, so one failing
-- timer or frame callback doesn't skip the others (or the ticking elements) for the rest of the frame.
local function protectedCall(fn, ...)
    local ok, err = xpcall(fn, debug.traceback, ...)
    if not ok then ui.reportError(tostring(err)) end
end

--- Called by the engine at the start of every frame. Returns the seconds until
--- the next timer is due (nil when there are no timers), so the engine wakes up for it.
function ui.__tick(now)
    -- The tree holds what it shows now; ids no longer keep their elements (see `pinned`).
    if pinned[1] ~= nil then pinned = {} end
    -- Collect first: callbacks may add or clear timers.
    local due = {}
    for id, t in pairs(timers) do
        if now >= t.due then due[#due + 1] = id end
    end
    -- In the order they came due (then the order they were set), not pairs' order: a timeout of 10 ms and one of 20 ms
    -- that both come due in one frame fire in that order.
    if #due > 1 then
        table.sort(due, function(a, b)
            local da, db = timers[a].due, timers[b].due
            if da ~= db then return da < db end
            return a < b
        end)
    end
    for _, id in ipairs(due) do
        local t = timers[id]
        if t then
            if t.rep then
                -- The next period starts where this one ended, not at the frame time (frames come late: the period
                -- would drift). A frame later than a whole period skips the missed ones: one call per frame.
                t.due = t.due + (math.floor((now - t.due) / t.interval) + 1) * t.interval
            else
                timers[id] = nil
            end
            protectedCall(t.fn)
        end
    end
    -- C++ elements that tick every frame, rendered or not (Timer). Dropped children are collected first, so a
    -- Timer that a container dropped and nothing references stops now (see `dropped` above).
    if types ~= nil and types.tick then
        if ui.__dropped then
            ui.__dropped = false
            if types.tickingCount() > 0 then collectgarbage() end
        end
        protectedCall(types.tick, now)
    end
    -- Iterate a copy: a callback may remove itself while running.
    for _, fn in ipairs({ table.unpack(frameCallbacks) }) do protectedCall(fn, now) end
    local nextDue
    for _, t in pairs(timers) do
        -- A NaN due time never comes (and math.max(0, NaN) is 0: a frame every iteration); the checks above keep
        -- it out, this keeps it from spinning the loop if one gets in anyway.
        if t.due == t.due and (not nextDue or t.due < nextDue) then nextDue = t.due end
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
-- singletons are globals), app-level element ids (QML style: `nameField.text`;
-- ids in a component or delegate scope are in its `ids` table instead), then any
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

-- `name` resolved against the folder of resource `script`. Errors at the caller of the ui.resource function calling
-- this. Stack: 1 = resolveIn, 2 = the ui.resource function, 3 = its caller.
local function resolveIn(script, name)
    if type(name) ~= "string" then error("resource name must be a string, got " .. type(name), 3) end
    local full, err = resource.resolve(script, name)
    if not full then error(err, 3) end
    return full
end

-- The script the plain ui.resource functions resolve `name` against: the one holding the code that called them, or
-- the main script when that code isn't a resource script (code loaded from a string). Stack: 1 = this, 2 = the
-- ui.resource function, 3 = its caller.
local function callerScript(name)
    local script, tail = callingScript(2)
    if tail and type(name) == "string" and not name:match("^%a[%w_%-]*:") then
        -- `return ui.resource.read("x")` in code that uses the plain `ui` (require("imlui")): Lua dropped the
        -- caller's frame, so its script is unknown. A resource script's own `ui` doesn't have this problem.
        error("ui.resource: a relative name in a tail call (`return ui.resource.f(name)`) can't be resolved in " ..
              "code that isn't a resource script; use a full name or wrap the call in parentheses", 3)
    end
    return script or ui.__script or "app:/"
end

-- The ui.resource functions, resolving relative names against scriptFor(name). They call it directly, so its stack
-- level 2 is the function and 3 its caller, and keep their results in locals: a tail call would hide the caller.
local function resourceApi(scriptFor)
    local api = {}

    --- The full, normalized name of `name`.
    function api.url(name)
        needResources()
        local full = resolveIn(scriptFor(name), name)
        return full
    end

    --- The contents of resource `name` as a string, or nil and a message when there is no such file.
    function api.read(name)
        needResources()
        local full = resolveIn(scriptFor(name), name)
        local text, err = resource.read(full)
        return text, err
    end

    --- True when resource `name` is a file or a folder.
    function api.exists(name)
        needResources()
        local full = resolveIn(scriptFor(name), name)
        local found = resource.exists(full)
        return found
    end

    return api
end

--- ui.resource: files from the app's resources. Names are full (`app:/data/items.json`, `imlui:/...`) or
--- relative to the folder of the calling script (`data/items.json`, `../shared/x.lua`).
ui.resource = resourceApi(callerScript)

--- Image { source = "logo.png" }: `source` is a resource name; a relative one is resolved (in C++, at every render,
--- so bindings work too) against the folder of the script that created the Image, stored as `__base`.
local image = ui.Image
local function imageIn(props, script)
    if type(props) ~= "table" then props = { props } end
    props.__base = script
    return image(props)
end
function ui.Image(props)
    return imageIn(props, callingScript(1) or ui.__script or "app:/")
end

-------------------------------------------------------------------------------
-- Script loading
-------------------------------------------------------------------------------

-- What a script loaded as a resource sees as `ui`: the ui table, with the functions that resolve relative names
-- (ui.resource.*, Image, theme, themeState) bound to `script`, so they work wherever its code runs, tail calls
-- included. Everything else reads and writes the ui table itself.
local function scriptUi(script)
    local own = {
        resource = resourceApi(function() return script end),
        Image = function(props) return imageIn(props, script) end,
        theme = function(t) return applyTheme(t, script) end,
        themeState = function(init) return themeStateFor(init, script) end,
    }
    return setmetatable({}, {
        __index = function(_, k)
            local v = own[k]
            if v ~= nil then return v end
            return ui[k]
        end,
        __newindex = ui,
    }), own
end

-- The environment of a UI script loaded as resource `script` (the entry script, components): ui.env, except that
-- `ui` and the free names of the bound functions (Image, theme, ...) come from scriptUi(script). Globals it assigns
-- go to ui.env, so they're shared between scripts as before.
local function chunkEnv(script)
    local scoped, own = scriptUi(script)
    return setmetatable({}, {
        __index = function(_, k)
            local v = rawget(ui.env, k) -- a global a script assigned wins, as in ui.env
            if k == "ui" and v == ui then return scoped end
            if v ~= nil then return v end
            v = own[k]
            if v ~= nil then return v end
            return ui.env[k]
        end,
        __newindex = ui.env,
    })
end

-- `require "MyComponent"` inside a UI script loads MyComponent.lua from the entry script's folder (a resource
-- folder such as `app:/`) in its own environment (chunkEnv), like QML files as components. Modules not found there
-- are looked up in `imlui:/` by the engine's searcher, then on package.path.
table.insert(package.searchers, 2, function(name)
    if not ui.__dir or resource == nil then return nil end
    local full, tried = resource.find(name, ui.__dir)
    if not full then return tried end
    local chunk, err = resource.load(full, chunkEnv(full))
    if not chunk then error(err, 0) end
    -- A module's top level is app level, even when a component instance requires it first.
    return function(...) return atAppLevel(chunk, ...) end, full
end)
ui.import = require

--- Runs the UI script `name` (a full resource name such as `app:/main.lua`) in its environment (chunkEnv) and
--- returns its result. Its folder is where `require` finds components.
function ui.__load(name)
    if resource == nil then error("ui.__load: no resource system", 0) end
    ui.__script = name
    ui.ids = weakIds() -- a load starts with no ids (a hot reload makes a new Lua state anyway)
    ui.__dir = name:match("^(.*/)") or name
    local chunk, err = resource.load(name, chunkEnv(name))
    if not chunk then error(err, 0) end
    return chunk()
end

return ui
