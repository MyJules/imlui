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
  connect(element, "onClicked", fn) adds more listeners (see ui.connect).

  Indices (currentIndex, currentRow, ...) are 1-based, like everything in Lua.
]]

local native = require("imlui.native")

-- The whole debug library, for the DSL itself (debug.getinfo, getregistry, setupvalue). Scripts get it too only in a
-- dev build (IMLUI_DEV_PATHS=ON); a shipped one leaves them debug.traceback alone (also as require("debug")), so a
-- script can't reach the registry, upvalues or metatables the DSL and C++ keep from it. This local is out of their
-- reach: only the debug library could read it. `fullDebug` is absent from a stand-in `imlui.native` (tests): full.
local debug = debug
if native.fullDebug == false then
    local scriptDebug = { traceback = debug.traceback }
    _G.debug = scriptDebug
    package.loaded.debug = scriptDebug
end

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

-- The scope stack, innermost last: { ids = table, what = function() -> description }, or false for app level
-- (atAppLevel). Empty: app level too. `scopes[#scopes] or nil` is the current scope, nil at app level. Each async
-- function's coroutine has a stack of its own, swapped in while it runs (see step): a coroutine that awaits inside a
-- scope keeps it to itself, and the pops of its to-be-closed POP_SCOPEs (when it resumes, or is closed) apply to it.
-- A plain coroutine a script creates (coroutine.create, coroutine.wrap) has one too (see coResume).
local scopes = {}
-- [1] is the scope stack in use (`scopes`, swapped while an async function runs): C++ pushes an element's scope on it
-- around a call of the element's function, and pops it after, as inScope does (luaui::ElementScope).
local scopeStack = { scopes }

-- The elements registered under an id since the last ui.__tick. Ids are weak, so until then, while a script still
-- builds its tree (`Text { id = "title" }` as a statement, used by name in the root later), they're kept here.
local pinned = {}

-- A to-be-closed value that pops the innermost scope: `local _ <close> = POP_SCOPE` right after pushing one keeps
-- the stack right however the block is left, a component that raises included.
local POP_SCOPE = setmetatable({}, { __close = function() scopes[#scopes] = nil end })

-- Calls fn(...) with `scope` as the innermost scope (false: app level) and returns its results.
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
if native.setScopeHooks then native.setScopeHooks(scopeOf, inScope, scopeStack) end

-- `fn` as a function that runs in the current scope when called later (a timer, a watcher, a future's handler): at
-- app level too, when set up there, whatever scope is active when it is called (a component's handler that changes a
-- state, or settles a future).
local function inCurrentScope(fn)
    local scope = scopes[#scopes] or false
    return function(...) return inScope(scope, fn, ...) end
end

-- Calls fn(...) at app level and returns its results: a module's top level runs there, whoever requires it first.
local function atAppLevel(fn, ...) return inScope(false, fn, ...) end

-- Callbacks that something long-lived holds (a signal listener, a watcher, a frame callback, a timer) are kept in
-- records. A record is { fn } for a callback set up at app level. One set up in a component instance or delegate
-- scope belongs to the scope: its record is weak-valued, { [2] = scope }, and its function (which runs in the scope,
-- so holds it) is ownedFns[scope][record], an ephemeron: once nothing else holds the scope (its elements are gone),
-- the callback goes with it, and it doesn't pin the elements it refers to. A delegate that a Repeater or ListView
-- rebuilds (or trims) so leaves no callback behind. Removing a callback empties its record, so a call that took the
-- record already skips it.
local ownedFns = setmetatable({}, { __mode = "k" })
local WEAK_VALUES = { __mode = "v" }

-- The function of a record; nil once it was removed or its scope was collected.
local function listenerOf(rec)
    local fn = rec[1]
    if fn ~= nil then return fn end
    local scope = rec[2]
    local fns = scope ~= nil and ownedFns[scope] or nil
    return fns and fns[rec]
end

-- A record for callback `fn`, set up now: in the current scope, it runs there and belongs to it. At app level it is
-- `fn` itself, or with `appLevel`, `fn` run at app level (whatever scope is active when it is called).
local function newRecord(fn, appLevel)
    local scope = scopes[#scopes] or nil
    if scope == nil then
        if appLevel then return { function(...) return atAppLevel(fn, ...) end } end
        return { fn }
    end
    local rec = setmetatable({ [2] = scope }, WEAK_VALUES)
    local fns = ownedFns[scope]
    if fns == nil then
        fns = {}
        ownedFns[scope] = fns
    end
    fns[rec] = function(...) return inScope(scope, fn, ...) end
    return rec
end

-- Empties `rec`: its callback was removed. (The scope's table would otherwise keep it until the scope goes.)
local function dropRecord(rec)
    rec[1] = nil
    local owner = rec[2]
    if owner ~= nil then
        rec[2] = nil
        local fns = ownedFns[owner]
        if fns ~= nil then fns[rec] = nil end
    end
end

-- Removes the empty records (of collected scopes) from `list`, keeping the order of the others. Returns the length.
local function pruneRecords(list)
    local n, j = #list, 0
    for i = 1, n do
        local rec = list[i]
        if listenerOf(rec) ~= nil then
            j = j + 1
            list[j] = rec
        end
    end
    for i = j + 1, n do list[i] = nil end
    return j
end

-- Appends `rec` to `list`. Each time the list has doubled since it was last pruned, the empty records go first: a
-- list whose callbacks never run (a watched key nobody writes) doesn't grow with the scopes that came and went.
local function addRecord(list, rec)
    local n = #list
    if n >= (list.pruneAt or 8) then
        n = pruneRecords(list)
        list.pruneAt = math.max(8, 2 * n)
    end
    list[n + 1] = rec
end

-- Copies the functions of the records in `list` into `into` (from index 1), in order: the callbacks there are now.
-- Prunes `list` when a record is empty. Returns how many there are.
local function liveFns(list, into)
    local n, m = #list, 0
    for i = 1, n do
        local fn = listenerOf(list[i])
        if fn ~= nil then
            m = m + 1
            into[m] = fn
        end
    end
    if m < n then pruneRecords(list) end
    return m
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
    if id ~= nil then registerIn(scopes[#scopes] or nil, props, id) end
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

-- Makes `parent` the parent of element `child`, or of any value (nothing for one that isn't an element) with
-- setParent. The children of an inline container follow it: their parent is its new holder.
local setParent
local function linkChild(child, parent)
    if child == parent then return end
    shareMeta(child)
    if parents[child] == parent then return end
    parents[child] = parent
    if rawget(child, "__inline") then
        forEachChild(child, setParent, holder(child))
    end
end
setParent = function(child, parent)
    if isElement(child) then linkChild(child, parent) end
end

-- The keys that make finishElement do more than link positional children, the scope and the id: a `parent` prop to
-- drop, element-valued children, the children a container built, an inline container's holder. ui.element's
-- constructors take their shortcut only without any of them.
local NOT_PLAIN_KEYS = { "parent", "__list", "__box", "__inline" }
for _, k in ipairs(CHILD_KEYS) do NOT_PLAIN_KEYS[#NOT_PLAIN_KEYS + 1] = k end

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
    local scope = scopes[#scopes] or nil
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
    -- Most elements are built from a fresh table: no metatable (so its fields read raw, with no call), at most
    -- positional children, no `parent` prop. For those of a registered type the constructor does what
    -- finishElement does with less work: attach sets `__type` (the table resized once for it and `__native`) and the
    -- type's metatable, and only the positional children are walked. Same result.
    local plainPath = typed and name ~= "Responsive"
    return function(props)
        if type(props) ~= "table" then props = { props } end
        if plainPath and getmetatable(props) == nil then
            local plain = true
            for i = 1, #NOT_PLAIN_KEYS do
                if props[NOT_PLAIN_KEYS[i]] ~= nil then
                    plain = false
                    break
                end
            end
            if plain then
                local n, id = #props, props.id
                types.attach(props, name)
                for i = 1, n do
                    local c = props[i] -- raw: a hole reads the type's methods, which have no integer keys
                    if isElement(c) then linkChild(c, props) end
                end
                local scope = scopes[#scopes] or nil
                if scope ~= nil then scopeOf[props] = scope end
                if id ~= nil then registerIn(scope, props, id) end
                return props
            end
        end
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
--- ui.__radioGroups[group] is a weak-keyed set of the group's elements. Each member holds its set (props.__radioGroup)
--- and the table is weak-valued, so a group goes once its last member is collected.
ui.__radioGroups = setmetatable({}, { __mode = "v" })
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

local watchers = setmetatable({}, { __mode = "k" }) -- proxy -> { key -> {record...} } (see newRecord)
local stores = setmetatable({}, { __mode = "k" })   -- proxy -> raw data

-- Calls each watcher of `list` with (v, old, k), iterating a snapshot of their functions: a watcher may remove itself
-- (or others) or add one while it runs, and the call goes to the watchers there were when the write happened. The
-- copy is a fresh table, not a reused one: a watcher that writes another state (or this one) runs this again before
-- it returns. Most writes have no watcher for the key, or none for "*", so nothing is copied for those.
local function notify(list, v, old, k)
    if list == nil or list[1] == nil then return end
    local copy = {}
    for i = 1, liveFns(list, copy) do copy[i](v, old, k) end
end

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
            notify(w[k], v, old, k)
            notify(w["*"], v, old, k)
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
--- A watcher added inside a component instance or delegate runs in its id scope and is dropped with it (when its
--- elements are collected).
function ui.watch(state, key, fn)
    local w = watchers[state]
    assert(w, "ui.watch: not a ui.state table")
    local list = w[key]
    if list == nil then
        list = {}
        w[key] = list
    end
    local rec = newRecord(fn, true)
    addRecord(list, rec)
    return function()
        dropRecord(rec)
        removeFrom(list, rec)
    end
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
        if id ~= nil and type(root) == "table" then registerIn(scopes[#scopes] or nil, root, id) end
        return root
    end
end

-------------------------------------------------------------------------------
-- Signals: several listeners
-------------------------------------------------------------------------------

-- connections[target] = { [signal] = { record... } }, in connect order. Weak keys: a connection never keeps its
-- target (an element table, a singleton global) alive, also when its function refers to the target. Each connection
-- is a record (see newRecord): one made in a component instance or delegate scope goes with the scope.
-- Disconnecting empties the record, so a dispatch that took it already skips it.
local connections = setmetatable({}, { __mode = "k" })
-- The same for C++ list models (imlui.types.list_model), with strong keys: a model's Lua value is found again by the
-- model only while Lua holds it, so its connections hold it until they are disconnected.
local modelConnections = {}

-- The functions of a C++ list model's value (see modelFns below), or nil.
local function modelFnsOf(v)
    if type(v) ~= "userdata" then return nil end
    local mt = getmetatable(v)
    return type(mt) == "table" and rawget(mt, "__imlui_model") or nil
end

--- Calls fn with the arguments of `signal` whenever `target` emits it: a C++ element (typed or built-in) as
--- fn(self, ...), a singleton as fn(...), like its `on*` handler. The signal is one the type declares or a property's
--- `on<Name>Changed`; for a C++ list model, one of its notifications as fn(...) (onRowsInserted(first, count), ...).
--- After the handler, in connect order; each listener's error is reported, and the others still
--- run. A listener connected during a dispatch runs from the next one on; one disconnected during it doesn't run.
--- Returns a function that disconnects it (calling it again does nothing). Made inside a component instance or
--- delegate, the listener runs in its scope and is dropped with it (when its elements are collected).
function ui.connect(target, signal, fn)
    if type(signal) ~= "string" then error("connect: signal name expected, got " .. type(signal), 2) end
    if type(fn) ~= "function" then error("connect: function expected, got " .. type(fn), 2) end
    local mfns = modelFnsOf(target)
    local ok, why
    if mfns ~= nil then
        ok, why = mfns.listen(target, signal)
    else
        if types == nil then error("connect: no C++ types (the library runs without an engine)", 2) end
        ok, why = types.listen(target, signal)
    end
    if not ok then error("connect: " .. why, 2) end
    local rec = newRecord(fn, false)
    local all = mfns ~= nil and modelConnections or connections
    local signals = all[target]
    if signals == nil then
        signals = {}
        all[target] = signals
    end
    local list = signals[signal]
    if list == nil then
        list = {}
        signals[signal] = list
    end
    addRecord(list, rec)
    return function()
        dropRecord(rec)
        removeFrom(list, rec)
        -- A model's last connection gone: its value no longer needs holding.
        if mfns ~= nil and #list == 0 and signals[signal] == list then
            signals[signal] = nil
            if next(signals) == nil and modelConnections[target] == signals then modelConnections[target] = nil end
        end
    end
end

-- dispatch(target, signal, ...): ScriptObject::emit and notify call it after the handler, once the object has had a
-- listener (imlui.lua.listeners), and a list model after each notification. Calls the listeners connected when it
-- starts. Returns whether one ran without an error.
local function dispatch(target, signal, ...)
    local signals = connections[target] or modelConnections[target]
    local list = signals and signals[signal]
    local n = list and #list or 0
    if n == 0 then return false end
    local taken = n == 1 and list or table.move(list, 1, n, 1, {}) -- one listener: its record is read before it runs
    local ran = false
    for i = 1, n do
        local fn = listenerOf(taken[i])
        if fn ~= nil then
            local ok, err = xpcall(fn, debug.traceback, ...)
            if ok then ran = true else ui.reportError(tostring(err)) end
        end
    end
    return ran
end
if types ~= nil then types.setDispatch(dispatch) end

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

-- Item `i` of a model table (or a C++ list model), and of a numeric model (no table: the item is the index).
local function rawItem(model, i) return model[i] end
local function numberItem(_, i) return i end

-- A C++ list model (imlui.types.list_model) is a userdata whose metatable's `__imlui_model` holds its functions:
-- count(m), revision(m), hasKeys(m), key(m, i). `#m` and `m[i]` work as on a list; m[i] converts row i once and
-- caches the item by the row's identity, so an unchanged row reads as the same item table wherever it moved (and a
-- changed one as a new table). The containers use that: with a model, a delegate is kept only while its item is the
-- same, the model's keys keep delegates without a `key` function, and nothing is compared while the model's revision
-- stands still. modelFns(v) returns those functions, or nil for any other model.
local modelFns = modelFnsOf

-- The key function of a container: its `key` (a function) or, without one, the keys of a list model that has them
-- (`fns` its modelFns; the second result is true then). nil: delegates are kept by index. `memo` (the container's
-- state) keeps the model's key function, made once per model.
local function keyFunction(self, model, fns, memo)
    local keyFn = rawget(self, "key")
    if type(keyFn) == "function" then return keyFn end
    if fns ~= nil and fns.hasKeys(model) then
        if memo.__keysOf ~= model then
            local key = fns.key
            memo.__keysOf, memo.__modelKey = model, function(_, i) return key(model, i) end
        end
        return memo.__modelKey, true
    end
    return nil
end

-- The key a container caches the delegate of an item without a key under: a nil or NaN key can't index a table.
local NO_KEY = {}

-- The key of `item` at index `i` (the container's `key` function, in the scope the container was built in). A nil
-- or NaN key can't index a table: the item itself is the key then, or NO_KEY when the item can't either (NaN, or nil
-- in a model with holes).
local function keyOf(self, keyFn, item, i)
    local scope = scopeOf[self]
    local k
    if scope ~= nil then k = inScope(scope, keyFn, item, i) else k = keyFn(item, i) end
    if k == nil or k ~= k then k = item end
    if k == nil or k ~= k then return NO_KEY end
    return k
end

-- Delegates by key: one is reused (also at another index) while an item with its key is in the model, so a model
-- that returns fresh item tables every call doesn't rebuild them. In model order, `cache[i]` is the entry shown at
-- index i; entries whose key went away are dropped.
-- `fresh`: the model is a C++ list model, whose items are new tables exactly when their rows changed, so an entry is
-- kept only while its item is the same too.
local function keyedChildren(self, model, n, keyFn, cache, list, fresh)
    local at = model and rawItem or numberItem
    local oldN = #list
    local i = 1
    -- The common case first: the same keys at the same indices, no table to build.
    while i <= n do
        local c = cache[i]
        local item = at(model, i)
        if c == nil or c.key ~= keyOf(self, keyFn, item, i) or (fresh and c.item ~= item) then break end
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
                if fresh and c.item ~= item then c = nil end -- its row changed: a new delegate
            end
            if c ~= nil then
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
    local n, fns
    if type(model) == "number" then
        -- Items 1..n, without a table of them.
        n = model == model and math.max(0, math.floor(math.min(model, math.maxinteger))) or 0
        model = nil
    else
        model = model or EMPTY
        fns = modelFns(model)
        n = #model
    end
    local cache = self.__cache
    if not cache then cache = {}; self.__cache = cache end
    local list = self.__list
    if not list then list = {}; self.__list = list end
    local rawKey = rawget(self, "key")
    if type(rawKey) ~= "function" then rawKey = false end
    -- A list model at the revision, length and `key` of the last call: every delegate is what it was. (The revision
    -- is read before any delegate is built: one that changes the model is seen on the next call.)
    local rev = fns and fns.revision(model)
    if fns ~= nil and self.__model == model and self.__revision == rev and self.__n == n and self.__key == rawKey then
        return list
    end
    -- Remembered once the list is complete: after a delegate that raised, the next call finishes it.
    self.__model = nil
    local keyFn = keyFunction(self, model, fns, self)
    if keyFn ~= nil then
        self.__counted = nil
        keyedChildren(self, model, n, keyFn, cache, list, fns ~= nil)
    else
        -- A count that didn't change: the items are 1..n again, and every cached delegate was built for its index.
        if model == nil and self.__counted == n then return list end
        self.__counted = nil
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
        if model == nil then self.__counted = n end
    end
    if fns ~= nil then self.__model, self.__revision, self.__n, self.__key = model, rev, n, rawKey end
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

-- A ListView with a delegate is virtualized: its C++ element (imlui.ui.elements.list_view) asks for the delegates of
-- the indices it shows, and only those are built. Each render calls __count(self, cacheSize), which resolves the
-- model, then __range(self, first, last) for the delegates of indices first..last. Built delegates are cached in
-- self.__view:
--   model, n      the model the last __count resolved (nil for a numeric model: the items are the indices), its length
--   fns, revision a list model's functions (modelFns) and its revision at that __count; nil for any other model
--   keyFn, rawKey the key function __range uses (keyFunction: `key`, or a list model's keys), and `key` itself
--   pass          counts the __count calls; an entry's `pass` is the last one that showed it
--   cache         [index] = entry, without a `key` function
--   keyed         [key] = entry, with one; more entries with the same key are chained through `nextSame`
--   built, shown  how many entries there are; how many indices the current pass showed
--   seen, seenN   with a key and a Lua model: the items whose keys the last pass counted (seen[i]) and how many (nil:
--                 none to compare with); seenKeyFn is the key function they were counted with, seenNumeric whether
--                 the model was a number
--   atIndex       with a list model: [index] = the keyed entry shown at the index (see listRange)
--   counts, out   scratch tables of listCount and listRange
-- An entry is { item, key, node, pass } (see buildDelegate). An entry the model no longer has is dropped as soon as
-- that is known cheaply: an index past the end or whose item changed, and with a key, a key gone from a model of at
-- most LIST_KEEP items (counted again only when an item changed: a key is a function of its item and index). Beyond
-- that the cache keeps at most max(cacheSize, 4 x shown) entries (the C++ element's `cacheSize`, LIST_KEEP by
-- default), dropping the ones shown longest ago. With a list model, an entry is also dropped when its item isn't the
-- row's any more (the row changed), and a pass at an unchanged revision drops nothing.
local LIST_KEEP = 256

-- One entry of a ListView's cache is gone (the caller unlinked it).
local function dropEntry(st)
    st.built = st.built - 1
    st.atIndex = nil
    dropped()
end

-- Drops the keyed entries that `keep(entry, arg)` rejects, keeping the order of each key's chain.
local function filterKeyed(st, keep, arg)
    for k, head in pairs(st.keyed) do
        local newHead, tail = nil, nil
        local c = head
        while c ~= nil do
            local nextC = c.nextSame
            c.nextSame = nil
            if keep(c, arg) then
                if tail then tail.nextSame = c else newHead = c end
                tail = c
            else
                dropEntry(st)
            end
            c = nextC
        end
        st.keyed[k] = newHead -- a key being traversed: assigning it (nil included) is allowed
    end
end

-- Keeps about the `keep` entries shown most recently.
local function trimView(st, keep)
    local all = {}
    for _, c in pairs(st.cache) do all[#all + 1] = c end
    for _, head in pairs(st.keyed) do
        local c = head
        while c ~= nil do all[#all + 1] = c; c = c.nextSame end
    end
    if #all <= keep then return end
    table.sort(all, function(a, b) return a.pass > b.pass end)
    local cut = all[keep + 1].pass -- what was shown no later than this goes (ties too)
    for i, c in pairs(st.cache) do
        if c.pass <= cut then st.cache[i] = nil; dropEntry(st) end
    end
    filterKeyed(st, function(c, last) return c.pass > last end, cut)
end

-- filterKeyed's `keep` for a count of the model's keys (counts[key] = how many items have it): keeps that many entries
-- of each key.
local function keepCounted(c, counts)
    local left = counts[c.key]
    if not left or left == 0 then return false end
    counts[c.key] = left - 1
    return true
end
local function keepNone() return false end

-- Whether the model has the items the last count saw (st.seen), counted with the same key function: then every item
-- has the key it had, and counting would drop nothing. Records the items for the next call otherwise. A numeric
-- model's items are its indices.
local function sameItems(st, model, n, keyFn)
    local same = st.seenN == n and st.seenKeyFn == keyFn and st.seenNumeric == (model == nil)
    if model ~= nil then
        local seen = st.seen
        if seen == nil then
            seen = {}
            st.seen = seen
        end
        for i = 1, n do
            local item = model[i]
            if seen[i] ~= item then -- a NaN item never matches: counted every call (right, if slow)
                same = false
                seen[i] = item
            end
        end
        for i = n + 1, st.seenN or 0 do seen[i] = nil end
    end
    st.seenN, st.seenKeyFn, st.seenNumeric = n, keyFn, model == nil
    return same
end

-- __count(self, cacheSize): resolves the model for this render's __range calls, drops the entries it no longer has
-- (keeping at most max(cacheSize, 4 x shown)), and returns its length and, for a C++ list model, the model (the C++
-- element follows its rows' heights through it).
local function listCount(self, cacheSize)
    local model = resolve(self, self.model)
    local n, fns
    if type(model) == "number" then
        n = model == model and math.max(0, math.floor(math.min(model, math.maxinteger))) or 0
        model = nil
    else
        model = model or EMPTY
        fns = modelFns(model)
        n = #model
    end
    local st = rawget(self, "__view")
    if not st then
        st = { cache = {}, keyed = {}, built = 0, pass = 0, shown = 0, n = 0 }
        rawset(self, "__view", st)
    end
    local rawKey = rawget(self, "key")
    if type(rawKey) ~= "function" then rawKey = false end
    local keyFn, modelKeys = keyFunction(self, model, fns, st)
    local rev = fns and fns.revision(model)
    local stable = fns ~= nil and st.model == model and st.revision == rev and st.n == n and st.rawKey == rawKey
    local counted = false
    if stable then
        -- A list model that didn't change since the last pass: every entry still is what it was, and at its index.
    elseif keyFn ~= nil then
        for i in pairs(st.cache) do st.cache[i] = nil; dropEntry(st) end -- `key` was set since
        -- A small model: count its keys, and drop the entries past each key's count. A Lua model with the items it
        -- had at the last count needs no count (a plain table model's keys would be asked for every frame). A list
        -- model changed (it isn't stable), and its own keys need no item: its rows aren't converted for this.
        if n <= LIST_KEEP and st.built > 0 then
            counted = fns == nil
            if not counted or not sameItems(st, model, n, keyFn) then
                local counts = st.counts or {}
                st.counts = nil -- taken while in use: a key function that renders this list again gets its own
                for i = 1, n do
                    local k = modelKeys and fns.key(model, i) or nil
                    if k == nil then
                        local item
                        if model then item = model[i] else item = i end
                        k = keyOf(self, keyFn, item, i)
                    end
                    counts[k] = (counts[k] or 0) + 1
                end
                filterKeyed(st, keepCounted, counts)
                for k in pairs(counts) do counts[k] = nil end
                st.counts = counts
            end
        end
    else
        if next(st.keyed) ~= nil then filterKeyed(st, keepNone) end -- `key` was removed
        if model ~= nil then
            for i, c in pairs(st.cache) do
                if i > n or c.item ~= model[i] then st.cache[i] = nil; dropEntry(st) end
            end
        elseif n < st.n then
            for i in pairs(st.cache) do
                if i > n then st.cache[i] = nil; dropEntry(st) end
            end
        end
    end
    if math.type(cacheSize) ~= "integer" or cacheSize < 0 then cacheSize = LIST_KEEP end
    local limit = math.max(cacheSize, 4 * st.shown)
    if st.built > limit then trimView(st, limit * 3 // 4) end
    -- The items seen are what the next sameItems compares with only when this pass counted them: a pass without a
    -- count (a bigger model, no entries, another kind of model) may build entries for other keys.
    if not counted then st.seenN = nil end
    st.model, st.n, st.fns, st.keyFn, st.revision, st.rawKey = model, n, fns, keyFn, rev, rawKey
    st.stable = stable
    local atIndex = st.atIndex
    if not stable and atIndex ~= nil then
        for i in pairs(atIndex) do atIndex[i] = nil end
    end
    st.pass = st.pass + 1
    st.shown = 0
    if fns ~= nil then return n, model end
    return n
end

-- __range(self, first, last): the delegates for model indices first..last (1-based), built when they aren't cached;
-- false for a delegate that returned nil. Called after __count, in the same render. The list is the view's, reused by
-- its next __range call (the C++ element reads it before that).
local function listRange(self, first, last)
    local st = rawget(self, "__view")
    if not st then return {} end
    local out = st.out or {}
    st.out = nil -- taken while in use: a delegate whose builder runs __range of this list again gets its own
    local model, pass, keyFn = st.model, st.pass, st.keyFn
    if last > st.n then last = st.n end
    -- A list model may have changed since __count (a delegate that edits it): its rows past the end are gone, and an
    -- entry whose row changed (another item) is rebuilt.
    local fresh = st.fns ~= nil
    -- A stable pass (see listCount) whose model is still at that revision: the entry shown at an index last time is
    -- the one for it now, found without reading the row or its key (atIndex: [index] = entry, keyed entries only;
    -- emptied by any change).
    local stable = false
    if fresh then
        local now = #model
        if last > now then last = now end
        stable = st.stable and st.fns.revision(model) == st.revision
    end
    -- (Only a list model's pass can be stable: atIndex is kept for those only.)
    local atIndex = st.atIndex
    if atIndex == nil and keyFn ~= nil and fresh then
        atIndex = {}
        st.atIndex = atIndex
    end
    for i = first, last do
        local c
        if stable then
            if keyFn ~= nil then c = atIndex[i] else c = st.cache[i] end
            if c ~= nil and c.pass == pass then c = nil end
        end
        if c ~= nil then
            -- kept as it is
        elseif keyFn ~= nil then
            local item
            if model then item = model[i] else item = i end
            -- The first entry with the item's key that this pass hasn't shown yet (repeated keys get one each).
            local k = keyOf(self, keyFn, item, i)
            local tail
            c = st.keyed[k]
            while c ~= nil and c.pass == pass do tail = c; c = c.nextSame end
            if c == nil then
                c = buildDelegate(self, item, i, k)
                st.built = st.built + 1
                if tail then tail.nextSame = c else st.keyed[k] = c end
            elseif fresh and c.item ~= item then
                -- Its row changed: a new delegate takes the entry's place in the key's chain.
                local nc = buildDelegate(self, item, i, k)
                nc.nextSame, c.nextSame = c.nextSame, nil
                if tail then tail.nextSame = nc else st.keyed[k] = nc end
                dropped()
                c = nc
            else
                c.item = item
            end
            if atIndex ~= nil then atIndex[i] = c end
        else
            local item
            if model then item = model[i] else item = i end
            c = st.cache[i]
            if c ~= nil and fresh and c.item ~= item then
                st.cache[i] = nil
                dropEntry(st)
                c = nil
            end
            if c == nil then
                c = buildDelegate(self, item, i, nil)
                st.built = st.built + 1
                st.cache[i] = c
            end
        end
        c.pass = pass
        out[i - first + 1] = c.node
    end
    local count = last >= first and last - first + 1 or 0
    for j = #out, count + 1, -1 do out[j] = nil end -- what the last call returned past this one's
    st.shown = st.shown + count
    st.out = out
    return out
end

--- ListView { model = ..., delegate = ..., height = 300, spacing = 4 } — a scrolling Repeater that builds only the
--- delegates in view (see listCount). Without a delegate it shows its own children.
function ui.ListView(p)
    p.__type = "ListView"
    -- Read-only, written back by the C++ element when it changes: 0 until then.
    if rawget(p, "firstVisibleIndex") == nil then rawset(p, "firstVisibleIndex", 0) end
    -- The C++ instance now, like ui.element's: its methods (positionViewAtIndex) work before the first render.
    if types ~= nil then types.attach(p) end
    if p.delegate then
        p.__count = listCount
        p.__range = listRange
    end
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
      design    = "material",   -- the skin that draws the controls: "material", "fluent", or one the app registered (registerSkin)
      tokens    = { controlHeight = 32, controlRadius = 4 },  -- override the design's metrics (Fluent draws with them)
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

-- A timer set inside a component instance or delegate runs in its id scope, and stops when the scope is collected
-- (its function is a record of the scope, see newRecord). Frames are on demand: it asks for the frame it's due in
-- (ui.__tick asks for the following ones), also when it was set while the app sleeps (a posted function) or during a
-- frame, whose own request for the next due time was already made.
local function addTimer(fn, ms, rep)
    nextTimer = nextTimer + 1
    timers[nextTimer] = { rec = newRecord(fn, true), interval = ms / 1000, due = native.time() + ms / 1000, rep = rep }
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
    if id == nil or id ~= id then return end -- nil or NaN can't index a table
    local t = timers[id]
    if t ~= nil then
        timers[id] = nil
        dropRecord(t.rec)
    end
end

local frameCallbacks = {} -- records (see newRecord)
--- Calls fn(time) at the start of every rendered frame. Frames are rendered on
--- demand: call ui.requestFrame() from fn to keep them coming while animating.
--- Returns a function that removes the callback.
--- Inside a component instance or delegate, fn runs in its id scope (like the timers and watchers) and is dropped
--- with it (when its elements are collected).
function ui.onFrame(fn)
    local rec = newRecord(fn, true)
    addRecord(frameCallbacks, rec)
    return function()
        dropRecord(rec)
        removeFrom(frameCallbacks, rec)
    end
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

-------------------------------------------------------------------------------
-- Async: futures, async/await
-------------------------------------------------------------------------------

-- A future is a value that arrives later. Its table holds `state` ("pending", "done", "failed", "cancelled"), `value`
-- (the first value, once done), `error` (once failed; "cancelled" once cancelled) and `progress` (the last progress
-- reported while pending, or nil), which bindings read like any field. Internal fields: __values (table.pack of every
-- value), __handlers (functions called with the future when it settles), __onProgress (functions called with each
-- progress), __consumers (how many handlers take its outcome: next/catch, await, all, a C++ future), __cancel (what
-- cancel() also stops: a C++ job, a timer, an async function), __waiting (the future a next/catch future waits on,
-- released when it is cancelled), __handled (something takes its outcome: an awaiter, next/catch, all, a C++
-- handler), __resolvable (made by future()), __core (the C++ future it was pushed from or read into: a userdata C++
-- reads back), __cxx (pushed from C++: cancel() cancels the C++ side first), __trace (a Lua error's traceback, for
-- the report).
--
-- Handlers run synchronously when a future settles: an awaiting coroutine resumes inside the resolve(), the timer or
-- the posted C++ function that settled it, inside a frame or outside one. Settling requests a frame.
--
-- Cancellation flows down and, when nothing else waits, up: a cancelled future cancels the futures next/catch made
-- from it (their handlers don't run: cancelling isn't an error), and an await on it raises "cancelled". Cancelling a
-- future that waits on others (next/catch, an async function, all) releases them: each one nothing else consumes is
-- cancelled too (release).
local FUTURE_METHODS = {}
local FUTURE = { __index = FUTURE_METHODS, __name = "future" }
-- C++ tells a Lua future (a method argument of type imlui::Future<T>) by this metatable.
debug.getregistry()["imlui.future"] = FUTURE

local function isFuture(v) return getmetatable(v) == FUTURE end
local function newFuture() return setmetatable({ state = "pending" }, FUTURE) end

-- Rejections that nothing handled when they happened. ui.__tick reports those still unhandled at the next frame, so
-- code that attaches a handler right after (in the same posted function or handler) isn't reported.
local unhandled = {}

local function display(err)
    local ok, s = pcall(tostring, err)
    return ok and s or "(error object)"
end

local function reportUnhandled()
    local list = unhandled
    unhandled = {}
    for _, f in ipairs(list) do
        if not f.__handled then ui.reportError("unhandled future rejection: " .. (f.__trace or display(f.error))) end
    end
end

-- Settles `f` as `state` with the values (done) or the error (failed, cancelled) in `...`, once. Returns whether it did.
local function settle(f, state, ...)
    if f.state ~= "pending" then return false end
    f.state = state
    if state == "done" then
        f.__values = table.pack(...)
        f.value = (...)
    else
        f.error = (...)
        if state == "failed" and not f.__handled then unhandled[#unhandled + 1] = f end
    end
    f.__cancel = nil
    f.__waiting = nil
    f.__onProgress = nil
    local handlers = f.__handlers
    f.__handlers = nil
    native.requestFrame(0)
    if handlers then
        for _, fn in ipairs(handlers) do protectedCall(fn, f) end
    end
    return true
end

-- Calls fn(f) once `f` settled: now when it is. Marks the outcome handled and counts a consumer.
local function onSettle(f, fn)
    f.__handled = true
    f.__consumers = (f.__consumers or 0) + 1
    if f.state ~= "pending" then return fn(f) end
    local handlers = f.__handlers
    if handlers == nil then
        handlers = {}
        f.__handlers = handlers
    end
    handlers[#handlers + 1] = fn
end

-- A consumer of pending `f` no longer needs it: cancels `f` once no consumer is left.
local function release(f)
    if f.state ~= "pending" then return end
    local n = (f.__consumers or 1) - 1
    f.__consumers = n
    if n <= 0 then FUTURE_METHODS.cancel(f) end
end

-- Reports `value` as the progress of pending `f`: sets f.progress, calls the onProgress handlers. Returns whether it
-- did.
local function reportProgress(f, value)
    if f.state ~= "pending" then return false end
    f.progress = value
    native.requestFrame(0)
    local list = f.__onProgress
    if list then
        for i = 1, #list do
            if f.state ~= "pending" then break end -- a handler settled it
            protectedCall(list[i], value)
        end
    end
    return true
end

-- Settles `g` the way `f` settles (and waits on it: cancelling `g` releases `f`).
local function follow(g, f)
    if g.state == "pending" then g.__waiting = f end
    onSettle(f, function()
        if f.state == "done" then
            settle(g, "done", table.unpack(f.__values, 1, f.__values.n))
        else
            g.__trace = f.__trace
            settle(g, f.state, f.error)
        end
    end)
end

-- The traceback of the last error a handler raised (xpcall's message handler keeps the error value itself).
local lastTrace
local function keepTrace(err)
    lastTrace = type(err) == "string" and debug.traceback(err, 2) or nil
    return err
end

-- Settles `g` with the outcome of xpcall(handler, keepTrace, ...): its results, the future it returned (followed), or
-- its error.
local function settleWith(g, ok, ...)
    if not ok then
        g.__trace = lastTrace
        settle(g, "failed", (...))
    elseif select("#", ...) == 1 and isFuture((...)) then
        follow(g, (...))
    else
        settle(g, "done", ...)
    end
end

local function checkFuture(f, method)
    if not isFuture(f) then error(("%s: call it on a future (f:%s(...)), got %s"):format(method, method, type(f)), 3) end
end
local function checkHandler(fn, method, what)
    if fn ~= nil and type(fn) ~= "function" then
        error(("%s: %s must be a function or nil, got %s"):format(method, what, type(fn)), 3)
    end
end

--- f:next(onValue, onError) -> future. When `f` is done, onValue(values...); when it failed, onError(error). The new
--- future settles with what the handler returns (a future it returns is followed), or fails with what it raises. A
--- missing handler passes the outcome on. When `f` is cancelled, so is the new future (no handler runs). Cancelling
--- the new future releases what it waits on (`f`, or the future a handler returned): cancelled unless something else
--- consumes it. Handlers run in the scope they were set in.
function FUTURE_METHODS:next(onValue, onError)
    checkFuture(self, "next")
    checkHandler(onValue, "next", "onValue")
    checkHandler(onError, "next", "onError")
    if onValue then onValue = inCurrentScope(onValue) end
    if onError then onError = inCurrentScope(onError) end
    local g = newFuture()
    g.__waiting = self
    onSettle(self, function(f)
        if f.state == "done" then
            if onValue then
                settleWith(g, xpcall(onValue, keepTrace, table.unpack(f.__values, 1, f.__values.n)))
            else
                follow(g, f)
            end
        elseif f.state == "cancelled" then
            settle(g, "cancelled", "cancelled")
        elseif onError then
            settleWith(g, xpcall(onError, keepTrace, f.error))
        else
            follow(g, f)
        end
    end)
    return g
end

--- f:catch(onError) -> future: f:next(nil, onError).
function FUTURE_METHODS:catch(onError)
    checkFuture(self, "catch")
    checkHandler(onError, "catch", "onError")
    return self:next(nil, onError)
end

--- f:cancel() -> boolean. Settles a pending future as cancelled (error "cancelled"), which cancels the futures next()
--- made from it, and stops what it waits on: the C++ work behind it, a sleep's timer, an async function (and the
--- future it awaits, released), all()'s inputs (released), next()'s source (released). False when `f` already settled.
function FUTURE_METHODS:cancel()
    checkFuture(self, "cancel")
    if self.state ~= "pending" then return false end
    local stop, waiting = self.__cancel, self.__waiting
    if rawget(self, "__cxx") then
        -- A C++ future settles first, running its C++ handlers (Future::onSettled, whenSettled, notifyWhenSettled)
        -- before the Lua future's awaiters and next() handlers, as when C++ settles it. It doesn't tell Lua (the
        -- canceller cancels without the link), so settling here is the only settle, unless a C++ handler cancelled
        -- it from Lua meanwhile: then this one does nothing.
        if stop then protectedCall(stop) end
        settle(self, "cancelled", "cancelled")
    else
        settle(self, "cancelled", "cancelled")
        if stop then protectedCall(stop) end
    end
    if waiting then protectedCall(release, waiting) end
    return true
end

--- f:onProgress(fn) -> f. Calls fn(progress) at each progress `f` reports from now until it settles (a C++ job's,
--- coalesced to at most one per loop iteration, or f:report()). The current one is f.progress. fn runs in the scope
--- onProgress was called in.
function FUTURE_METHODS:onProgress(fn)
    checkFuture(self, "onProgress")
    if type(fn) ~= "function" then error("onProgress: a function expected, got " .. type(fn), 2) end
    if self.state == "pending" then
        local list = self.__onProgress
        if list == nil then
            list = {}
            self.__onProgress = list
        end
        list[#list + 1] = inCurrentScope(fn)
    end
    return self
end

--- f:report(progress) -> boolean: reports the progress (a number, by convention 0..1) of a future made by future().
--- False when it already settled.
function FUTURE_METHODS:report(value)
    checkFuture(self, "report")
    if not rawget(self, "__resolvable") then error("report: only a future made by future() takes progress", 2) end
    if type(value) ~= "number" then error("report: a number expected, got " .. type(value), 2) end
    return reportProgress(self, value)
end

--- f:resolve(values...) / f:reject(error) -> boolean: settle a future made by future(). False when it already settled.
function FUTURE_METHODS:resolve(...)
    checkFuture(self, "resolve")
    if not rawget(self, "__resolvable") then error("resolve: only a future made by future() can be resolved", 2) end
    return settle(self, "done", ...)
end
function FUTURE_METHODS:reject(err)
    checkFuture(self, "reject")
    if not rawget(self, "__resolvable") then error("reject: only a future made by future() can be rejected", 2) end
    return settle(self, "failed", err)
end

--- A pending future that the script settles with f:resolve(...) or f:reject(err).
function ui.future()
    local f = newFuture()
    f.__resolvable = true
    return f
end

-- The coroutine library's own functions: scripts get coroutine.create, resume, wrap and close that give each
-- coroutine a scope stack of its own (see coResume); the async functions' coroutines keep theirs in their task.
local rawCreate, rawResume, rawClose, rawStatus = coroutine.create, coroutine.resume, coroutine.close, coroutine.status

-- Async functions. tasks[coroutine] = { co, future, scopes = its scope stack, waiting = the future it awaits,
-- cancelled }. The stack starts with the scope the async function was called in (false: app level).
local tasks = setmetatable({}, { __mode = "k" })

-- Closes the coroutine of `task` (its pending to-be-closed variables, POP_SCOPEs included) with its scope stack in
-- place, and puts the current one back.
local function closeTask(task)
    local saved = scopes
    scopes = task.scopes
    scopeStack[1] = scopes
    rawClose(task.co) -- returns an error a __close raised; never raises
    scopes = saved
    scopeStack[1] = scopes
end

-- After a resume of `task`, with its scope stack still in place (`saved` is the resumer's): puts `saved` back and
-- settles the task's future when the body returned or raised. A body that yielded without await (coroutine.yield)
-- fails: nothing would ever resume it.
local function finish(task, saved, ok, ...)
    scopes = saved
    scopeStack[1] = scopes
    local co = task.co
    if rawStatus(co) == "dead" then
        tasks[co] = nil
        if ok then
            settle(task.future, "done", ...)
        else
            local err = (...)
            if type(err) == "string" then task.future.__trace = debug.traceback(co, err) end
            closeTask(task) -- closes its pending to-be-closed variables
            settle(task.future, "failed", err)
        end
    elseif task.waiting == nil and not task.cancelled then
        tasks[co] = nil
        closeTask(task)
        settle(task.future, "failed", "async: the function yielded with coroutine.yield; use await")
    end
end

-- Resumes `task` with `...`, with its own scope stack in place (the scope it was started in, and those it entered).
local function step(task, ...)
    task.waiting = nil
    local saved = scopes
    scopes = task.scopes
    scopeStack[1] = scopes
    return finish(task, saved, rawResume(task.co, ...)) -- resume never raises: `saved` is always put back
end

--- async(fn) -> function. Calling the result runs fn(...) in a new coroutine, which may `await`, and returns a future
--- of fn's results right away (when fn first awaits). An error fn raises fails the future. Cancelling the future
--- stops the coroutine where it waits (its to-be-closed variables are closed) and releases the future it awaits.
--- The coroutine runs in the scope async's result was called in, like a timer.
function ui.async(fn)
    if type(fn) ~= "function" then error("async: a function expected, got " .. type(fn), 2) end
    return function(...)
        local f = newFuture()
        local task = { co = rawCreate(fn), future = f, scopes = { scopes[#scopes] or false } }
        tasks[task.co] = task
        f.__cancel = function()
            task.cancelled = true
            tasks[task.co] = nil
            local waiting = task.waiting
            task.waiting = nil
            if waiting then release(waiting) end
            if rawStatus(task.co) == "suspended" then closeTask(task) end
        end
        step(task, ...)
        return f
    end
end

--- await(f) -> values... Inside an async function: waits until future `f` settles (the coroutine yields; the UI keeps
--- running), then returns its values, or raises its error (catchable with pcall; "cancelled" for a cancelled one). A
--- value that isn't a future is returned as is. Raises outside an async function.
function ui.await(f)
    local task = tasks[coroutine.running()]
    if task == nil then error("await: not inside an async function (wrap the caller in async(fn))", 2) end
    if not isFuture(f) then return f end
    if f.state == "pending" then
        task.waiting = f
        onSettle(f, function()
            if not task.cancelled then step(task) end
        end)
        coroutine.yield()
    else
        f.__handled = true
    end
    if f.state == "done" then return table.unpack(f.__values, 1, f.__values.n) end
    error(f.error, 0)
end

-- Plain coroutines. coScopes[co] is the scope stack of a coroutine a script created, swapped in while it runs, as an
-- async function's is: it starts with the scope the coroutine was created in (false: app level), and a coroutine that
-- yields inside a scope (a component's builder, a handler) keeps the scope to itself instead of leaving it on the
-- resumer's stack, where the code after the resume would run in it and the coroutine's pop, once it resumes, would
-- pop the resumer's scope. A coroutine without one (an async function's, the main thread) runs on the resumer's.
local coScopes = setmetatable({}, { __mode = "k" })

-- Puts the resumer's stack `saved` back and returns the results of the resume or close.
local function coRestore(saved, ...)
    scopes = saved
    scopeStack[1] = scopes
    return ...
end

local function coCreate(fn)
    if type(fn) ~= "function" then error("bad argument #1 to 'create' (function expected)", 2) end
    local co = rawCreate(fn)
    coScopes[co] = { scopes[#scopes] or false }
    return co
end

-- coroutine.resume with the coroutine's own scope stack in place. Neither resume nor this raises once `co` is a
-- coroutine, so the resumer's stack is always put back.
local function coResume(co, ...)
    if type(co) ~= "thread" then error("bad argument #1 to 'resume' (coroutine expected)", 2) end
    local own = coScopes[co]
    if own == nil or own == scopes then return rawResume(co, ...) end
    local saved = scopes
    scopes = own
    scopeStack[1] = scopes
    return coRestore(saved, rawResume(co, ...))
end

-- coroutine.close with the coroutine's own stack in place, for the pops of its pending POP_SCOPEs. A coroutine that
-- is running or resuming another one can't be closed: close raises, with the stack left as it is.
local function coClose(co)
    if type(co) ~= "thread" then error("bad argument #1 to 'close' (coroutine expected)", 2) end
    local own = coScopes[co]
    local status = rawStatus(co)
    if own == nil or own == scopes or (status ~= "suspended" and status ~= "dead") then return rawClose(co) end
    local saved = scopes
    scopes = own
    scopeStack[1] = scopes
    return coRestore(saved, rawClose(co))
end

-- The results of a coroutine.wrap function's resume: its values, or its error raised in the caller, like Lua's own
-- wrap: a coroutine that raised is closed first (an error its __close raises replaces the error) and a string error
-- gets the caller's position. Tail-called, so level 2 is the wrap function's caller.
local function wrapResults(co, ok, ...)
    if ok then return ... end
    local err = ...
    if rawStatus(co) == "dead" then
        local closed, closeErr = coClose(co)
        if not closed then err = closeErr end
    end
    error(err, type(err) == "string" and 2 or 0)
end

local function coWrap(fn)
    if type(fn) ~= "function" then error("bad argument #1 to 'wrap' (function expected)", 2) end
    local co = coCreate(fn)
    return function(...) return wrapResults(co, coResume(co, ...)) end
end

coroutine.create, coroutine.resume, coroutine.close, coroutine.wrap = coCreate, coResume, coClose, coWrap

--- sleep(ms) -> future, done after `ms` milliseconds (a timer, like setTimeout). Cancelling it clears the timer.
function ui.sleep(ms)
    if ms == nil then ms = 0 end
    if type(ms) ~= "number" or not (ms >= 0 and ms < math.huge) then
        error("sleep: the delay must be a finite number of milliseconds >= 0, got " .. describe(ms), 2)
    end
    local f = newFuture()
    local id = addTimer(function() settle(f, "done") end, ms, false)
    f.__cancel = function() ui.clearTimer(id) end
    return f
end

--- all({f1, f2, ...}) -> future of the list of their values (the first value of each), in order. Fails or is cancelled
--- like the first input that fails or is cancelled. Values that aren't futures are taken as they are. Cancelling it
--- releases the inputs (each one nothing else consumes is cancelled).
function ui.all(list)
    if type(list) ~= "table" then error("all: a list of futures expected, got " .. type(list), 2) end
    local n = list.n or #list
    local f = newFuture()
    local results, left = { n = n }, n
    for i = 1, n do
        local item = list[i]
        if isFuture(item) then
            onSettle(item, function()
                if item.state == "done" then
                    results[i] = item.value
                    left = left - 1
                    if left == 0 then settle(f, "done", results) end
                else
                    f.__trace = item.__trace
                    settle(f, item.state, item.error)
                end
            end)
        else
            results[i] = item
            left = left - 1
        end
    end
    if f.state == "pending" then
        if left == 0 then
            settle(f, "done", results)
        else
            f.__cancel = function()
                for i = 1, n do
                    if isFuture(list[i]) then release(list[i]) end
                end
            end
        end
    end
    return f
end

-- Settles pending `g` the way settled `f` did.
local function settleLike(g, f)
    if f.state == "done" then
        settle(g, "done", table.unpack(f.__values, 1, f.__values.n))
    else
        g.__trace = f.__trace
        settle(g, f.state, f.error)
    end
end

--- timeout(f, ms) -> future that settles like `f` (with its values, and its progress), unless `f` is still pending
--- after `ms` milliseconds: then it fails with "timed out after <ms> ms" and releases `f` (cancelled unless something
--- else consumes it). Cancelling it clears the timer and releases `f`. `ms` is checked like sleep's.
function ui.timeout(f, ms)
    if not isFuture(f) then error("timeout: a future expected, got " .. type(f), 2) end
    if type(ms) ~= "number" or not (ms >= 0 and ms < math.huge) then
        error("timeout: the delay must be a finite number of milliseconds >= 0, got " .. describe(ms), 2)
    end
    local g = newFuture()
    local id
    g.__waiting = f
    onSettle(f, function()
        if id then ui.clearTimer(id) end
        if g.state == "pending" then settleLike(g, f) end
    end)
    if g.state == "pending" then
        if f.progress ~= nil then g.progress = f.progress end
        local list = f.__onProgress
        if list == nil then
            list = {}
            f.__onProgress = list
        end
        list[#list + 1] = function(value) reportProgress(g, value) end
        id = addTimer(function()
            if settle(g, "failed", ("timed out after %g ms"):format(ms)) then release(f) end
        end, ms, false)
        g.__cancel = function() ui.clearTimer(id) end
    end
    return g
end

--- race({f1, f2, ...}) -> future that settles like the first of them to settle, whichever way (done, failed or
--- cancelled); an input settled already, or a value that isn't a future, wins at once (the first in order). Then the
--- others are released (each one nothing else consumes is cancelled), as they are when the race is cancelled. An
--- empty list gives a cancelled future.
function ui.race(list)
    if type(list) ~= "table" then error("race: a list of futures expected, got " .. type(list), 2) end
    local n = list.n or #list
    local g = newFuture()
    local inputs = {}
    for i = 1, n do
        if isFuture(list[i]) then inputs[#inputs + 1] = list[i] end
    end
    local function releaseAll(except)
        for _, f in ipairs(inputs) do
            if f ~= except then release(f) end
        end
    end
    local armed = false
    for _, f in ipairs(inputs) do
        onSettle(f, function()
            if armed and g.state == "pending" then
                settleLike(g, f)
                releaseAll(f)
            end
        end)
    end
    armed = true
    for i = 1, n do
        local item = list[i]
        if not isFuture(item) then
            settle(g, "done", item)
            releaseAll(nil)
            break
        elseif item.state ~= "pending" then
            settleLike(g, item)
            releaseAll(item)
            break
        end
    end
    if g.state == "pending" then
        if n == 0 then
            settle(g, "cancelled", "cancelled")
        else
            g.__cancel = function() releaseAll(nil) end
        end
    end
    return g
end

-- C++ futures (imlui::Future<T>, module imlui.types.future) push through this: a new future and its link, which C++
-- keeps (a LuaFunction) and calls on the main thread to settle it: link("done", value), link("failed", message,
-- handled), link("cancelled"), link("progress", value); link() returns the future. `canceller` (a C function) cancels
-- the C++ side when the script cancels; `core` (a userdata) lets C++ read the future back as itself. A settlement
-- passed after it applies at once.
function ui.__futureLink(canceller, core, ...)
    local f = newFuture()
    f.__cancel = canceller
    f.__core = core
    f.__cxx = true
    local function link(kind, ...)
        if kind == nil then return f end
        if kind == "progress" then
            reportProgress(f, (...))
        elseif kind == "failed" then
            local err, handled = ...
            if handled then f.__handled = true end
            settle(f, "failed", err)
        elseif kind == "cancelled" then
            settle(f, "cancelled", "cancelled")
        else
            settle(f, "done", ...)
        end
    end
    if select("#", ...) > 0 then link(...) end
    return f, link
end

-- A Lua future read into C++ (a method argument of type imlui::Future<T>) that wasn't pushed from C++: the C++ future
-- follows it through `receiver` (a C function): receiver("done", values...), receiver("failed", message),
-- receiver("cancelled"), receiver("progress", value). Returns the link C++ keeps: link() returns the future,
-- link("cancelled") cancels it. `core` is set as f.__core unless it has one.
function ui.__futureAdopt(f, core, receiver)
    if f.__core == nil then f.__core = core end
    if f.state == "pending" then
        local list = f.__onProgress
        if list == nil then
            list = {}
            f.__onProgress = list
        end
        list[#list + 1] = function(value) receiver("progress", value) end
        if f.progress ~= nil then receiver("progress", f.progress) end
    end
    onSettle(f, function()
        if f.state == "done" then
            receiver("done", table.unpack(f.__values, 1, f.__values.n))
        elseif f.state == "failed" then
            receiver("failed", display(f.error))
        else
            receiver("cancelled")
        end
    end)
    return function(kind)
        if kind == nil then return f end
        if kind == "cancelled" then FUTURE_METHODS.cancel(f) end
    end
end

-- Scratch lists of ui.__tick, reused from frame to frame instead of a new table each (a frame with nothing due makes
-- no garbage). Taken while in use (the local is nil then): a callback that runs ui.__tick again (a test, a script
-- driving frames by hand) gets fresh ones and doesn't clobber the outer call's. Emptied after use, so they hold no
-- callback alive and the next use starts at length 0.
local dueScratch, frameScratch = {}, {}

-- The order timers fire in when several come due in one frame: by due time, then by the order they were set.
local function dueFirst(a, b)
    local da, db = timers[a].due, timers[b].due
    if da ~= db then return da < db end
    return a < b
end

--- Called by the engine at the start of every frame. Returns the seconds until
--- the next timer is due (nil when there are no timers), so the engine wakes up for it.
function ui.__tick(now)
    -- The tree holds what it shows now; ids no longer keep their elements (see `pinned`).
    if pinned[1] ~= nil then pinned = {} end
    if unhandled[1] ~= nil then reportUnhandled() end
    -- Collect first: callbacks may add or clear timers.
    local due = dueScratch or {}
    dueScratch = nil
    local ndue = 0
    for id, t in pairs(timers) do
        if listenerOf(t.rec) == nil then
            timers[id] = nil -- set in a scope that was collected since (clearing a field during pairs is allowed)
        elseif now >= t.due then
            ndue = ndue + 1
            due[ndue] = id
        end
    end
    -- In the order they came due (then the order they were set), not pairs' order: a timeout of 10 ms and one of 20 ms
    -- that both come due in one frame fire in that order.
    if ndue > 1 then table.sort(due, dueFirst) end
    for i = 1, ndue do
        local id = due[i]
        due[i] = nil -- emptied as it goes: the list is empty again however the loop ends
        local t = timers[id]
        local fn = t and listenerOf(t.rec)
        if fn == nil then
            if t then timers[id] = nil end
        else
            if t.rep then
                -- The next period starts where this one ended, not at the frame time (frames come late: the period
                -- would drift). A frame later than a whole period skips the missed ones: one call per frame.
                t.due = t.due + (math.floor((now - t.due) / t.interval) + 1) * t.interval
            else
                timers[id] = nil
                dropRecord(t.rec)
            end
            protectedCall(fn)
        end
    end
    dueScratch = due
    -- C++ elements that tick every frame, rendered or not (Timer). Dropped children are collected first, so a
    -- Timer that a container dropped and nothing references stops now (see `dropped` above).
    if types ~= nil and types.tick then
        if ui.__dropped then
            ui.__dropped = false
            if types.tickingCount() > 0 then collectgarbage() end
        end
        protectedCall(types.tick, now)
    end
    -- Iterate a snapshot: a callback may remove itself (or others), or add one, while running; the frame calls the
    -- ones there were when it started (their functions: one removed meanwhile still runs this frame). No limit on how
    -- many there are (a loop, not table.unpack).
    if frameCallbacks[1] ~= nil then
        local snapshot = frameScratch or {}
        frameScratch = nil
        for i = 1, liveFns(frameCallbacks, snapshot) do
            local fn = snapshot[i]
            snapshot[i] = nil
            protectedCall(fn, now)
        end
        frameScratch = snapshot
    end
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

-- Logging goes through spdlog on the C++ side. A message at a level that's off isn't built: no string.format, and
-- no __tostring of the arguments (a log.debug in a binding costs a call and a check when debug is off).
local logEnabled = native.logEnabled or function() return true end -- (a stub native without it logs everything)
local function logAt(level)
    return function(fmt, ...)
        if not logEnabled(level) then return end
        local msg = select("#", ...) > 0 and string.format(fmt, ...) or tostring(fmt)
        native.log(level, msg)
    end
end
ui.log = { trace = logAt("trace"), debug = logAt("debug"), info = logAt("info"),
           warn = logAt("warn"), error = logAt("error"),
           --- enabled(level) -> whether log[level] logs, for a message that costs something to gather.
           enabled = logEnabled }

--- print() in UI scripts logs at info level.
function ui.print(...)
    if not logEnabled("info") then return end
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

-- Culling: containers skip the children out of view (only their cached size is laid out). On by default.
ui.setCulling = native.setCulling           -- setCulling(false) renders every child, for debugging and comparison
ui.culling = native.culling                 -- culling() -> whether it's on

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
--
-- Those constructors are cached in a table of their own, read after the ids, never in `ui`: `ui` is read first, so a
-- constructor cached there would shadow an app id of the same name for good. An id read before its element registers
-- (a binding `Details ~= nil` that runs before a Loader builds `Card { id = "Details" }`), or after a weak id was
-- collected, would stay a constructor (`Details.text` then fails), and ui.checkId would report the id as colliding
-- with "ui member 'ui.Details'". Cached here, the id wins as soon as it exists.
local implicitCtors = {}

-- The implicit constructor for free name `k`, past the ids: one for any Capitalized name, else nil.
local function implicitCtor(k)
    local v = implicitCtors[k]
    if v ~= nil then return v end
    if type(k) == "string" and k:match("^%u") then
        v = ui.element(k)
        implicitCtors[k] = v
        return v
    end
    return nil
end

-- The caches of the chunk environments (see chunkEnv), weak: a global a script assigns (a new key of ui.env) is
-- removed from them, so it takes precedence there as it does in ui.env.
local envCaches = setmetatable({}, { __mode = "k" })

-- Its own fields are the globals UI scripts assigned (`ui` is served by __index, so assigning it is a new key too).
ui.env = setmetatable({}, {
    __index = function(_, k)
        if k == "ui" then return ui end
        local v = ui[k]
        if v ~= nil then return v end
        v = _G[k]
        if v ~= nil then return v end
        v = ui.ids[k]
        if v ~= nil then return v end
        return implicitCtor(k)
    end,
    __newindex = function(env, k, v)
        rawset(env, k, v)
        for cache in pairs(envCaches) do rawset(cache, k, nil) end
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
-- included. Everything else reads and writes the ui table itself. Reads are a chain of tables (the empty proxy, then
-- `own`, then ui): no function runs for them. Returns the proxy and `own` (read it raw: its __index is ui).
local function scriptUi(script)
    local own = setmetatable({
        resource = resourceApi(function() return script end),
        Image = function(props) return imageIn(props, script) end,
        theme = function(t) return applyTheme(t, script) end,
        themeState = function(init) return themeStateFor(init, script) end,
    }, { __index = ui })
    return setmetatable({}, { __index = own, __newindex = ui }), own
end

-- The environment of a UI script loaded as resource `script` (the entry script, components): ui.env, except that
-- `ui` and the free names of the bound functions (Image, theme, ...) come from scriptUi(script). Globals it assigns
-- go to ui.env, so they're shared between scripts as before.
--
-- Free names are looked up every frame (bindings), so the lookup is short: the environment stays empty (every
-- assignment reaches ui.env) and reads go to a cache table, then to one function that does the whole lookup in
-- order: a global a script assigned (raw in ui.env), `ui` and the bound functions (scriptUi), ui, the Lua globals,
-- the app-level ids, an implicit constructor. The cache holds only what can't change behind it: `ui` and the bound
-- functions, which only a global of the same name could shadow, and assigning one removes them from every cache
-- (ui.env's __newindex). Everything else is looked up again each time, so ids, ui members and globals that change
-- are seen as before.
local function chunkEnv(script)
    local scoped, own = scriptUi(script)
    local env = ui.env
    local cache = setmetatable({}, {
        __index = function(cache, k)
            local v = rawget(env, k) -- a global a script assigned wins, as in ui.env
            if v ~= nil then
                if k == "ui" and v == ui then return scoped end
                return v
            end
            if k == "ui" then v = scoped else v = rawget(own, k) end
            if v ~= nil then
                rawset(cache, k, v)
                return v
            end
            v = ui[k]
            if v ~= nil then return v end
            v = _G[k]
            if v ~= nil then return v end
            v = ui.ids[k]
            if v ~= nil then return v end
            return implicitCtor(k)
        end,
    })
    envCaches[cache] = true
    return setmetatable({}, { __index = cache, __newindex = env })
end

-- `require "MyComponent"` inside a UI script loads MyComponent.lua from the entry script's folder (a resource
-- folder such as `app:/`) in its own environment (chunkEnv), like QML files as components. Modules not found there
-- are looked up in `imlui:/` by the engine's searcher, and nowhere else: registerResources removed Lua's file and C
-- searchers (no package.path, no DLLs). loadModule finds and loads it with one read (a folder named
-- `MyComponent.lua` doesn't count: `MyComponent/init.lua` is next); the environment, which depends on the name it
-- found, is set after.
table.insert(package.searchers, 2, function(name)
    if not ui.__dir or resource == nil then return nil end
    local chunk, full = resource.loadModule(name, ui.__dir)
    if chunk == nil then return full end -- the names it tried
    if chunk == false then error(full, 0) end -- the load error
    debug.setupvalue(chunk, 1, chunkEnv(full)) -- a main chunk's only upvalue is _ENV
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
