# Elements

The reference for every built-in element: props with their defaults, signals with their arguments, two-way
props, positional strings and non-visual elements. The Lua side (`state`, `bind`, components, dynamic children,
timers, snackbar) is in the [Lua API](lua-api.md).

Every built-in element is a typed C++ class (`imlui::Element`), one module per element in
`src/imlui/ui/elements/<name>.cppm`, registered under its Lua name and aliases by `registerBuiltinTypes()`
(`src/imlui/ui/elements/builtin.cppm`). Your own elements are written the same way: see
[Registering C++ types](extending.md). [`ui.types()`](lua-api.md#types---name-) lists every registered type name,
built-in and your own.

- [Reading this page](#reading-this-page)
- [Common props](#common-props): `id`, `visible`, `enabled`, `tooltip`, sizes, `menu`, stacking
- [Values that come back from C++](#values-that-come-back-from-c)
- [Positional strings](#positional-strings)
- [Windows and overlays](#windows-and-overlays): [ApplicationWindow](#applicationwindow), [Window](#window),
  [Dialog](#dialog), [Snackbar](#snackbar), [ImGuiDemo](#imguidemo)
- [Layout](#layout): [Column / Item](#column--item), [Row](#row), [Grid](#grid), [ScrollView](#scrollview),
  [ListView](#listview), [Card](#card), [TabBar / Tab](#tabbar--tab), [Section / Expander](#section--expander),
  [TreeNode](#treenode)
- [Inline containers](#inline-containers): Repeater, If, Loader, Responsive
- [App bars, navigation and menus](#app-bars-navigation-and-menus): [TopAppBar](#topappbar),
  [NavigationItem](#navigationitem), [NavigationBar](#navigationbar), [NavigationRail](#navigationrail),
  [ModalNavigationDrawer](#modalnavigationdrawer), [MenuBar, Menu, MenuItem](#menubar-menu-menuitem)
- [Text and decoration](#text-and-decoration): [Text / Label](#text--label), [Icon](#icon), [Image](#image),
  [Link](#link), [Divider / Separator](#divider--separator), [Spacer](#spacer)
- [Buttons and selection controls](#buttons-and-selection-controls): [Button](#button),
  [IconButton](#iconbutton), [Checkbox](#checkbox), [Switch](#switch), [RadioButton](#radiobutton),
  [RadioGroup](#radiogroup), [Chip](#chip), [SegmentedButton](#segmentedbutton), [ListItem](#listitem)
- [Value inputs](#value-inputs): [Slider](#slider), [SpinBox](#spinbox), [TextField / TextArea](#textfield--textarea),
  [ComboBox](#combobox), [ColorPicker](#colorpicker)
- [Progress, data and drawing](#progress-data-and-drawing): [ProgressBar](#progressbar),
  [CircularProgress](#circularprogress), [BusyIndicator](#busyindicator), [Plot](#plot), [Table](#table),
  [Canvas](#canvas)
- [Non-visual](#non-visual): [Timer](#timer)
- [At a glance](#at-a-glance): two-way props, signals, non-visual elements
- [Custom elements](#custom-elements)

## Reading this page

Each element has a short description, a props table, then **Signals** and **Two-way** lines.

| Column | Meaning |
|---|---|
| Prop | The key in the element table. |
| Type | `string`, `number`, `integer`, `bool`, `color` (a [color spec](theming.md#color-specs)), `icon` (a [Material icon name](theming.md#icons)), `style` (a [text style](theming.md#typography)), `list`, `element` (another element as the value), or `size` (see [sizes](#sizes)). |
| Default | The value used when the prop is unset or is `nil`. `—` means unset: the element picks (it's described in Notes). |
| Notes | What it does. **two-way** marks a prop the element writes back. |

- Sizes and positions are in **dp**.
- Any prop can be a plain value, a binding function or a `bind(...)`
  ([property values](lua-api.md#property-values)). A binding that returns `nil` resets the prop to its default.
- Props marked *raw* aren't C++ properties: the element reads them straight from its table while rendering
  (sizes, element-valued props, a few presence checks). They take the same three forms.
- Signal handlers get `self` first: `onToggled = function(self, checked) ... end`. A handler runs after the prop
  it reports on has been written back.
- Indices (`currentIndex`, `currentRow`, ...) are 1-based.
- **Non-visual** elements take no layout space and aren't a layout item: put them anywhere among the children,
  for example a `Dialog` or a `Timer` inside a `Row`.

## Common props

These are handled by the renderer (`Renderer::render`) for every element.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `id` | string | — | Makes the element reachable by name: `nameField.text` at app level, `ids.nameField` inside a [component or delegate](lua-api.md#element-ids), which has its own scope. Ids are weak (they don't keep the element alive); a duplicate in a scope is reported, and so is an app-level id that collides with a `ui` member or a Lua global, since the name would resolve to that instead. Every constructor registers it (`ui.registerId`), the Lua-side `Repeater`, `ListView`, `If`, `Loader` and `Responsive` included: `ListView { id = "list" }` is `list`. |
| `parent` | element | — | *Read-only, Lua side.* The element that lays this one out (inline containers are skipped), `nil` for the root: `self.parent` in a binding or handler. A weak reference, kept in a side table; assigning it has no effect (the next sync drops the field; debug builds report it). See [`self.parent`](lua-api.md#selfparent). |
| `visible` | bool | `true` | `false` skips the element entirely: it isn't rendered and takes no space. On an inline container it hides all of its children. (A `Grid` keeps a hidden child's empty cell.) |
| `enabled` | bool | `true` | `false` disables the element and everything inside it, with the Material disabled colors. |
| `tooltip` | string | — | A plain tooltip (inverse surface colors, `bodySmall`) on hover, also while disabled. |

### Sizes

Props of type `size` (mostly `width` and `height`, read *raw*) share one set of rules, applied by one function
(`resolveSize`, `src/imlui/ui/item_size.cppm`) for every element and Material widget, so a fill is exactly the
available width (or height) everywhere:

| Value | Meaning |
|---|---|
| `> 0` | A fixed size in dp. |
| `-1` | Fill the available space (any value from `-1` up to `0`). |
| `< -1` | Fill minus `|v|` dp (`width = -16` leaves 16dp). |
| `0` (or unset) | Automatic: the element's own default, given in its table: its natural size, or `0 (fill)` for the elements that fill by default (`Card`, `TextField`, `Slider`, `Grid`, `ScrollView`, `TopAppBar`, ...). |
| `fillWidth = true` | Counts as `width = -1`. |

The available width is the space the parent gives: the width of a `Column` with `width` (less its `padding`), a
`Grid` cell, the content of a `Card` or `ScrollView`, else the window's. A fill is at least 1px. Heights fill the
space left below the element in its window or scroll region.

Only the elements whose table lists `width` / `height` read them. `Window`'s `width`/`height` and
`ModalNavigationDrawer`'s `width` are plain dp numbers instead (no fill values, no `fillWidth`).

### `menu`

`Button` and `IconButton` take `menu = Menu { ... }` (*raw*): clicking the button opens the menu's children as an
anchored Material dropdown (at least 112dp wide) under the button. See [Menu](#menubar-menu-menuitem).

### Stacking containers

Elements that stack their children top to bottom (`ApplicationWindow`, `Window`, `Dialog`, `Column`,
`ScrollView`, `ListView`, `Card`, `Tab`, `ModalNavigationDrawer`) also take:

| Prop | Type | Default | Notes |
|---|---|---|---|
| `spacing` | number | ImGui item spacing | Vertical gap between visible children, in dp. Declared by `Column`, `ScrollView` and `Card`; *raw* on the others. |
| `align` | `"start"` \| `"center"` \| `"end"` | `"start"` | Horizontal position of each child in the available width, by its width (see below). A child that fills is as wide as the stack, so `align` doesn't move it; a fill-minus child (`width = -40`) is the stack's width less 40dp, and `align` places it in the stack (centered: 20dp on each side). |

Non-visual children are rendered in place and don't count for the spacing.

How `align` knows a child's width before drawing it: every built-in element whose size follows from its props, the
font and the theme knows it up front (its *intrinsic size*: `Text`, `Icon`, `Button`, `Checkbox`, `Chip`,
`TextField`, `ProgressBar`, `Image`, ... see [what still lags](architecture.md#layout-notes)), and so do `Column`,
`Row`, `Card`, `Section`, `TopAppBar` and `NavigationBar` when all their children do, so they're in place on every
frame, the first included. Any other child (a custom element without an intrinsic size, a stack holding one, a
`ScrollView`, `Grid`, `Table`, `TabBar`, `TreeNode`) is placed by its width on the last frame; when that changed,
another frame follows right away, so the layout settles by itself even when nothing else is redrawing (a `Timer`
changing a label, a script setting a prop). On a child's very first frame, when neither is known, it is laid out but
not shown, and appears in place on the next frame. The same goes for `Row` (see there: only where a wrong width
would shift its siblings), `TopAppBar` actions and `NavigationRail` destinations. A size, once measured, is kept while the child is hidden (another tab, an
`If` branch, a collapsed section, for at least a minute), so a child that comes back is in place on its first frame.
A fill or fill-minus child needs no measured width: it is as wide as the stack, less its `-N`.

## Values that come back from C++

Whole numbers reach Lua as **integers**: `math.type(v) == "integer"`, and `tostring(v)` is `"5"`, not `"5.0"`.
`LuaConvert` (`src/imlui/types/lua_convert.cppm`) pushes a floating-point value that is a whole number as a Lua
integer, and integer-typed props as integers. That covers every value the element hands to Lua: two-way
write-backs, `on<Prop>Changed` values and signal arguments. So `TabBar.currentIndex`, a `Slider` value with an
integral `stepSize`, `onActivated(self, index, text)`'s `index` and `Canvas.onClicked`'s whole `x`, `y` are
integers. Fractional values stay floats. The Canvas painter's fields (`p.width`, `p.mouseX`, ...) are pushed as
plain numbers (floats).

Every write-back also emits **`on<Prop>Changed(self, value)`** if the element table has that handler (for example
`onCheckedChanged`, `onCurrentIndexChanged`, `onOpenChanged`), besides the element's own signals. For
`TextField.text` and `ColorPicker.color` the generated name is the declared signal (`onTextChanged`,
`onColorChanged`): it runs once per edit.

A two-way prop is written back the way it was given ([property values](lua-api.md#property-values)): a plain value
is replaced in the element table, a `bind(...)` writes its source, and a function binding is left alone (it owns
the value, so the element's change only lasts until the next frame's sync).

## Positional strings

These elements take a positional string as their `text` (their default property): `Button { "OK" }` is
`Button { text = "OK" }`, and `Button "OK"` works too. An explicit `text` wins.

| Element | The positional string is |
|---|---|
| `Text` / `Label` | the text |
| `Icon` | the glyph name (after `name` and `icon`) |
| `Image` | the `source` (`Image { "logo.png" }`) |
| `Link` | the label, and the URL unless `url` is set |
| `Separator` | the label |
| `Section` / `Expander` | the title, unless `title` is set |
| `TreeNode` | the label |
| `NavigationItem` | the label |
| `MenuItem` | the label |
| `Button` | the label |
| `IconButton` | the icon name, unless `icon` is set |
| `Checkbox`, `Switch`, `RadioButton` | the label |
| `Chip` | the label |
| `ListItem` | the headline |

Other elements ignore a positional string. Tables in the array part are always children.

---

## Windows and overlays

### ApplicationWindow

The root element. It fills the native window, with no decoration, and stacks its children. Non-visual.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `title` | string | — | Native window title, applied every frame while the prop exists (so it can be a binding). Without it, the last `ui.setTitle()` sticks, else `"imlui"`. |
| `width`, `height` | number | — | *Raw, read by the engine.* Native window size in dp (times the monitor content scale), applied at load when both are `> 0`. On hot reload it's applied again only if it changed. |
| `padding` | number | `24` | Content padding. |
| `color` | color | — | Background. Unset: the theme's window background (`surface`). |
| `scrollable` | bool | `true` | `false` hides the scrollbar. |
| `menuBar` | element | — | *Raw.* A [`MenuBar`](#menubar-menu-menuitem) at the top. |
| `spacing`, `align` | | | *Raw.* See [stacking containers](#stacking-containers). |

### Window

A floating ImGui window with Material surface colors (`surfaceContainerLow`, an `outlineVariant` border). It
stacks its children. Non-visual. Each `Window` element is its own ImGui window, which keeps its position and size
when the title changes.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `title` | string | `"Window"` | Title bar text. |
| `open` | bool | `true` | **two-way.** `false` hides the window. When the prop is present the window has a close button, which writes `open = false`. |
| `x`, `y` | number | `60`, `60` | Position, applied when the window first appears. |
| `width`, `height` | number | `0` | Size, applied when the window first appears (if either is `> 0`). With `autoResize`, the minimum size. |
| `autoResize` | bool | — | Fits the window to its content, every frame: it grows and shrinks back with it. Unset: `true` when neither `width` nor `height` is set. Inside, a fill (`width = -1`, a `TextField`, a `Divider`, ...) is `width` wide less the padding, or 240dp when `width` is unset, never the window's current width (which follows the content); text wraps where the window would reach the viewport's right edge. |
| `titleBar`, `resizable`, `movable` | bool | `true` | Window chrome. |
| `menuBar` | element | — | *Raw.* A [`MenuBar`](#menubar-menu-menuitem). |
| `spacing`, `align` | | | *Raw.* See [stacking containers](#stacking-containers). |

**Signals:** `onClosed(self)` after the close button wrote `open = false`.
**Two-way:** `open`.

### Dialog

A Material basic dialog: modal, centered over a scrim, `extraLarge` corners, a `headlineSmall` title. Escape
closes it. It stacks its children. Non-visual. A dialog may open another one from its content: the nested dialog
shows above it, and Escape closes the top-most dialog (or a menu opened from it) first. Sibling dialogs (not
nested in one another) show one at a time: one whose `open` turns true while another is shown waits, still
`open`, and shows the frame after the other closes.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `open` | bool | `false` | **two-way.** Shows the dialog. Closing it (Escape) writes `false`. |
| `title` | string | `""` | Headline. |
| `icon` | icon | `""` | Hero icon above the title, which is then centered. |
| `width` | size | — | *Raw.* A fixed width. Unset: 280–560dp, fitting the content: the dialog grows with its widest child and shrinks back with it, and wrapping text wraps at 560dp. A fill inside (`width = -1`, a `TextField`, a `Divider`, ...) is as wide as the minimum (232dp, inside the 24dp padding) or the title, whichever is wider, never the dialog's current width; `align` places the children in the widest of them. |
| `spacing`, `align` | | | *Raw.* See [stacking containers](#stacking-containers). |

**Signals:** `onClosed(self)` when the dialog closes.
**Two-way:** `open`.

```lua
local s = state { confirm = false }
Button { "Delete", onClicked = function() s.confirm = true end }
Dialog { title = "Delete file?", icon = "delete", open = bind(s, "confirm"),
    Text { "This can't be undone.", wrap = true },
    Row { Spacer { fill = true },
          Button { "Cancel", variant = "text", onClicked = function() s.confirm = false end },
          Button { "Delete", variant = "text", onClicked = function() remove(); s.confirm = false end } },
}
```

### Snackbar

The Material snackbar. There is one app-wide instance, created by the prelude and rendered by the engine as an
overlay above everything; you show it with [`ui.snackbar(text, opts)`](lua-api.md#snackbar) and don't normally
create one. Non-visual.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Message. |
| `actionText` | string | `""` | Action button label; empty for none. |
| `shownAt` | number | `-1` | Time it was shown (`ui.time()`, seconds); `< 0` hides it. Written back as `-1` once it has faded out; the action and the close icon move it back to start the fade. |
| `duration` | number | `4000` | Visible time in ms, after a 0.18s slide-in and before a 0.15s fade. |
| `closable` | bool | `false` | Shows a close icon. |

The bar is as wide as its content (the message, the action, the close icon and 16dp padding on each side), 344–672dp,
and at most the viewport less 16dp on each side; the action and the close icon sit at its right edge. A message too
long for the widest bar wraps in what the bar leaves it (word by word, and at `\n`); the bar grows from 48dp by 20dp a
line (68dp for two, as Material 3 specifies) and keeps the action and the close icon on the right, vertically
centered. Its width doesn't change with the wrapping.

**Signals:** `onAction(self)` when the action is clicked (the snackbar then fades out).
**Two-way:** `shownAt`.

### ImGuiDemo

Dear ImGui's demo window, for debugging. Non-visual.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `open` | bool | `true` | **two-way.** Closing the window writes `false`. |

**Two-way:** `open`.

---

## Layout

### Column / Item

A vertical stack. `Item` is an alias.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `spacing` | number | ImGui item spacing | Gap between visible children (dp). |
| `align` | `"start"` \| `"center"` \| `"end"` | `"start"` | Horizontal position of each child, by its width ([how it knows it](#stacking-containers)). |
| `padding` | number | `0` | Inset on every side: the children start `padding` in and fill up to `padding` from the column's right edge. The column's size (what a `Row` places it by) includes the padding on both sides. |
| `width` | size | `0` (auto) | *Raw.* A fixed or fill width: the column's own, padding included (like a `Card`'s), so `Column { padding = 16, width = -1 }` fills the available width and its children get that less 16dp on each side. The children's available width is the width less the padding: they fill it (`fillWidth`, fill-minus widths, the elements that fill by default), `align` within it and wrap text at it (`Text { wrap = true }`, raw `ImGui::TextWrapped`), also inside a `Row`. Unset, a column that holds a child that fills is as wide as the available width, so it fills itself: a `Row` gives it a share. |

### Row

Children side by side, as a flex row. Children that **fill** share the width the others leave, in equal parts:
a fill or fill-minus `width` (`fillWidth = true`, `width = -16`), an element that fills by default (`0 (fill)` in
its table: `TextField`, `Card`, `Slider`, `Grid`, `ScrollView`, ...), wrapping or aligned text (`Text { wrap = true }`,
`Text { align = "center" }`), `Spacer { fill = true }`, and, by their content, a `Column` (without a `width`) that
holds a fill and a `Row` that has fills or a `justify`. Each fill is laid out in its share: it fills it, a fill-minus
width is its share minus `|v|`, wrapping text wraps at it, aligned text aligns in it. The other children keep their
own width (their intrinsic size, else their width on the previous frame, settling within a frame) and are bounded by
the children after them, so a fill nested inside one stops there. `align` centers (or bottom-aligns) each child
against the tallest, by the same sizes, on whole pixels.

On a child's very first frame its size may not be known yet (no intrinsic size, never rendered). When the row needs
its **width** (the row has fills, or `justify` isn't `"start"`), a guess would shift its siblings far, so the row is
laid out unseen for that one frame and shown in place on the next. When the row only needs its **height** (the
default `align = "center"`, or `"bottom"`), the child is shown at the top on that frame, the others are aligned among
the known heights, and the next frame (requested) aligns them all: a shift of a few pixels for one frame, much less
noticeable than a row that blinks in.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `spacing` | number | ImGui item spacing | Horizontal gap (dp). |
| `align` | `"center"` \| `"top"` \| `"bottom"` | `"center"` | Vertical alignment of the children. |
| `justify` | `"start"` \| `"center"` \| `"end"` \| `"spaceBetween"` | `"start"` | Horizontal distribution. Ignored while the row has fills. |
| `width` | size | `0` (available width) | *Raw.* The row's width: the children share it, and `justify` distributes in it. |

`Row { Spacer { fill = true }, Button { "OK" } }` right-aligns the button; `Row { TextField {}, Button { "Send" } }`
gives the field what the button leaves; `Row { Button { "A", fillWidth = true }, Button { "B", fillWidth = true } }`
gives each button half. Non-visual children take no place in the row.

### Grid

A table-based grid. The children fill the cells left to right, `columns` per row, and the columns stretch
equally. A hidden child (`visible = false`) keeps its empty cell, so the others don't move when it toggles;
non-visual children take no cell.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `columns` | integer | `2` | Column count, 1 to 511 (ImGui's table limit). A larger value is reported as a script error and capped. |
| `spacing` | number | `8` | Gap between cells (dp). |
| `border` | bool | `false` | Cell borders. |
| `width` | size | `0` (fill) | *Raw.* |

### ScrollView

A scrolling child region that stacks its children.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `width`, `height` | size | `0` (fill the remaining space) | *Raw.* |
| `padding` | number | `0` | Inner padding (dp). |
| `color` | color | — | Background, also the surface color the children draw on. Unset: transparent. |
| `rounding` | number | theme `shape.medium` | Corner radius (dp). |
| `border` | bool | `false` | Border. |
| `spacing`, `align` | | | See [stacking containers](#stacking-containers). |

A ScrollView **keeps its scroll position when it moves**. When the same element is rendered somewhere else (under
another parent, for example after a [`Responsive`](lua-api.md#responsive--compact-medium-expanded-large-extralarge-)
or size-class layout switch), its ImGui window is a new one; the element then restores the last horizontal and
vertical scroll offset into it, retrying for a few frames until it sticks (a new window learns its content size
on its first frame, and a content now too short for the offset clamps it). If one element is rendered twice in a
frame, only the first place is tracked.

### ListView

A `ScrollView` whose children come from `model` / `delegate` (built on the Lua side, cached per index like a
`Repeater`). Without `delegate`, its own array children are used. It takes every `ScrollView` prop. See
[ListView](lua-api.md#listview).

| Prop | Type | Default | Notes |
|---|---|---|---|
| `model` | list, number, function or `bind` | — | Items; a number `n` means `1..n`. |
| `delegate` | `function(item, index, ids) -> element` | — | Builds one item's element; `ids` is its own [id scope](lua-api.md#element-ids). |
| `key` | `function(item, index) -> key` | — | *Optional.* Keeps delegates by key instead of by item: see [Repeater](lua-api.md#repeater--model-delegate-). |

```lua
ListView { height = 240, spacing = 4, model = function() return todo.items end,
    delegate = function(item) return ListItem { item.title, supportingText = item.note } end }
```

### Card

A Material card that stacks its children.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `variant` | `"elevated"` \| `"filled"` \| `"outlined"` | `"elevated"` | `elevated`: `surfaceContainerLow` with a shadow. `filled`: `surfaceContainerHighest`. `outlined`: `surface` with an `outlineVariant` border. |
| `padding` | number | `16` | Inner padding (dp). |
| `color` | color | — | Container color. Unset: the variant's. |
| `rounding` | number | — | Corner radius (dp). Unset: the card shape (`shape.medium`). |
| `width` | size | `0` (fill) | *Raw.* |
| `height` | size | `0` (fit the content) | *Raw.* A fixed height. |
| `spacing`, `align` | | | See [stacking containers](#stacking-containers). |

### TabBar / Tab

Material tabs. A `TabBar` collects its visible children as tab headers and renders only the selected one's
content, stacked with that child's `spacing` / `align`. Its children are normally `Tab`s; another element used
as a tab gives its header through *raw* `title` (default `"Tab"`) and `icon` props. Non-visual children (`Timer`,
`Dialog`, `Window`, `ModalNavigationDrawer`, ...) are not tabs and don't count for `currentIndex`: they render
whichever tab is selected, so a `Dialog` declared in a `TabBar` opens from any tab.

| `TabBar` prop | Type | Default | Notes |
|---|---|---|---|
| `currentIndex` | integer | `1` | **two-way.** The selected tab among the visible ones. Out-of-range values show the nearest tab. |
| `variant` | `"primary"` \| `"secondary"` | `"primary"` | Primary tabs with icons stack the icon over the label (64dp). Secondary tabs use a full-width indicator. |
| `width` | size | `0` (fill) | *Raw.* |

**Signals:** `onCurrentChanged(self, index)` when the user picks a tab.
**Two-way:** `currentIndex`.

| `Tab` prop | Type | Default | Notes |
|---|---|---|---|
| `title` | string | `"Tab"` | Header label. |
| `icon` | icon | `""` | Header icon. |
| `spacing`, `align` | | | *Raw.* See [stacking containers](#stacking-containers). |

`visible = false` on a `Tab` hides the tab and its header. A `Tab` outside a `TabBar` renders its content inline.

```lua
TabBar { currentIndex = bind(s, "tab"),
    Tab { title = "Music", icon = "music_note", Text { "Songs" } },
    Tab { title = "Videos", icon = "movie", Text { "Clips" } },
}
```

### Section / Expander

A collapsible header (48dp, or 64dp with supporting text). The children are indented 16dp while expanded.
`Expander` is an alias.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `title` | string | — | Header text (`titleMedium`). Unset: `text`. |
| `text` | string | `""` | Positional. The title when `title` isn't set. |
| `supportingText` | string | `""` | Second line. |
| `expanded` | bool | `true` | **two-way.** Open state. |
| `width` | size | `0` (fill) | *Raw.* |

**Signals:** `onToggled(self, expanded)`.
**Two-way:** `expanded`.

### TreeNode

An ImGui tree node (`bodyLarge`) whose children render inside it (not stacked: no `spacing` / `align`).

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label. |
| `expanded` | bool | `false` | Open when the node first appears. ImGui keeps the open state afterwards; it isn't written back. |

---

## Inline containers

`Repeater`, `If`, `Loader` and `Responsive` produce children on the Lua side (`__children`), and they're
**inline**: their parent lays the children out as its own, so a `Repeater` in a `Row` flows horizontally. On the
C++ side they're one class (`InlineContainer`) with no props of its own; it only renders when one of them is the
root. `visible = false` hides all of their children.

| Element | Props (Lua side) |
|---|---|
| `Repeater` | `model` (list, number, function or `bind`), `delegate = function(item, index, ids)`, optional `key = function(item, index)`. See [Repeater](lua-api.md#repeater--model-delegate-). |
| `If` | `condition`, array children, `otherwise` (one element). See [If](lua-api.md#if--condition-children-otherwise--element-). |
| `Loader` | `item` (an element, or a function returning one). See [Loader](lua-api.md#loader--item-). |
| `Responsive` | `compact`, `medium`, `expanded`, `large`, `extraLarge`: the element (or a function returning one, or `false`) for each window size class. See [Responsive](lua-api.md#responsive--compact-medium-expanded-large-extralarge-). |

---

## App bars, navigation and menus

### TopAppBar

The Material top app bar. Its children are **actions**, right-aligned in the 64dp top row and each vertically
centered by its height (its intrinsic size, else last frame's; see [Stacking containers](#stacking-containers)).

| Prop | Type | Default | Notes |
|---|---|---|---|
| `title` | string | `""` | `titleLarge` for `small` and `center`, `headlineSmall` for `medium`, `headlineMedium` for `large`. |
| `variant` | `"small"` \| `"center"` \| `"medium"` \| `"large"` | `"small"` | Height 64, 64, 112 or 152dp. `center` centers the title; `medium` and `large` put it below the top row. |
| `navigationIcon` | icon | `""` | A leading icon button; empty for none. |
| `color` | color | — | Container color. Unset: the current surface. |
| `width` | size | `0` (available width) | *Raw.* |

**Signals:** `onNavigation(self)` when the navigation icon is clicked.

```lua
TopAppBar { title = "Inbox", navigationIcon = "menu", onNavigation = function() nav.drawer = true end,
    IconButton { "search" },
    IconButton { "more_vert", menu = Menu { MenuItem { "Settings" }, MenuItem { "About" } } },
}
```

### NavigationItem

A navigation destination. It takes the look of the navigation container it's rendered in:

- **by default** (a `ScrollView` used as a standard drawer, a `ModalNavigationDrawer`, a rail's `header`,
  anywhere else): a drawer item, a 56dp pill with a leading icon, a `labelLarge` label and an optional trailing
  badge text;
- **in a `NavigationBar`**: a bar destination, an 80dp tall cell with a 24dp icon in a 64x32dp pill indicator and a
  `labelMedium` label below;
- **in a `NavigationRail`**: a rail destination, 56dp tall, with a 56x32dp indicator and the label below.

In a bar or rail the indicator is `secondaryContainer` when selected, the icon `onSecondaryContainer` (selected) or
`onSurfaceVariant`, the label `onSurface` (selected) or `onSurfaceVariant`, and the hover and press layer sits on
the indicator.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label. |
| `icon` | icon | `""` | Icon. |
| `selected` | bool | `false` | Selected look. |
| `badge` | string | `""` | Drawer: trailing text. Bar and rail: a small `error` badge on the icon with this text, or a 6dp dot for `"."` (or `"•"`). |
| `width` | size | `0` (fill) | *Raw.* Drawer items only; in a bar or rail the container sets the width. |

**Signals:** `onClicked(self)`.

How it works: while a `NavigationBar` or `NavigationRail` renders its destinations it opens a `NavigationScope`
(`src/imlui/ui/navigation_scope.cppm`: the style and the destination width), and `NavigationItem` reads the
innermost one. The scope covers everything the container renders, including `Repeater` delegates, and the outer
scope comes back afterwards. A rail's `header` and a `ModalNavigationDrawer`'s sheet open a default (drawer) scope.

### NavigationBar

The Material 3 bottom navigation bar, for compact windows: 80dp tall, as wide as the available width. Its visible
children are `NavigationItem`s (directly or from a `Repeater`) and share the width equally.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `color` | color | — | Container color. Unset: `surfaceContainer`. |
| `width` | size | `0` (available width) | *Raw.* |

```lua
NavigationBar {
    Repeater { model = tabs, delegate = function(t, i)
        return NavigationItem { t.name, icon = t.icon, selected = function() return s.tab == i end,
                                onClicked = function() s.tab = i end }
    end },
}
```

### NavigationRail

The Material 3 navigation rail, for medium windows: 80dp wide and as tall as the available height (at least
80dp). An optional `header` element sits **fixed at the top**, centered, and the destinations stack below it
(28dp under the header, or 12dp from the top without one).

When the destinations don't fit, they **scroll** vertically (mouse wheel) and a thin themed scrollbar appears in
the rail's right gutter, **only then**; the destinations keep their place and width, and a 12dp bottom margin
scrolls with them. While they fit there's no scrollbar, and `align` places the group in the height left below the
header.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `header` | element | — | *Raw.* Rendered centered at the top and not scrolled, e.g. a menu `IconButton` or a FAB (`Button { variant = "fab" }`). It's in drawer style, not a rail destination. |
| `color` | color | — | Container color. Unset: `surface`. |
| `spacing` | number | `12` | Gap between destinations (dp). |
| `align` | `"top"` \| `"center"` \| `"bottom"` | `"top"` | Vertical position of the destinations below the header, while they fit. |
| `height` | size | `0` (available height) | *Raw.* |

```lua
NavigationRail {
    header = IconButton { "menu", onClicked = function() nav.drawer = true end },
    Repeater { model = pages, delegate = destination },
}
```

### ModalNavigationDrawer

A modal navigation drawer: a scrim (`scrim` at 32%) over the whole window and a `surfaceContainerLow` sheet that
slides in from the left, with `large` rounded corners on its right edge. It draws above the app content. Its
children are stacked in a scrolling region with 12dp padding, in drawer style, so `NavigationItem`s render as
drawer items. Clicking the scrim or pressing Escape closes it (Escape is left to a menu or popup opened from the
drawer first). Non-visual: put it anywhere, for example as a child of the `ApplicationWindow`.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `open` | bool | `false` | **two-way** (see below). Shown while true. |
| `width` | number | `360` | Sheet width in dp, at most 80% of the window. |
| `spacing`, `align` | | | *Raw.* See [stacking containers](#stacking-containers). |

**Signals:** `onClosed(self)` when the scrim or Escape closes it, after `open` was updated.
**Two-way:** `open`, in three forms:

- **A plain value** (`open = false`, opened with `drawer.open = true` through its `id`): closing writes `false`
  back to the element.
- **`bind(...)`**: closing writes `false` to the bound state.
- **A function binding** (`open = function() return nav.drawer end`): the function owns the value, so the drawer
  can't write it. Closing hides the sheet locally and fires `onClosed`; the drawer then stays hidden until the
  function has returned `false` (the handler, or anything else, updated the state it reads) and then `true` again.

```lua
local nav = state { drawer = false, page = 1 }
TopAppBar { title = "Inbox", navigationIcon = "menu", onNavigation = function() nav.drawer = true end }
ModalNavigationDrawer { open = bind(nav, "drawer"), spacing = 0,
    Repeater { model = pages, delegate = function(p, i)
        return NavigationItem { p.name, icon = p.icon, selected = function() return nav.page == i end,
                                onClicked = function() nav.page = i; nav.drawer = false end }
    end },
}
```

Close it from a destination's `onClicked` as above.

### MenuBar, Menu, MenuItem

A `MenuBar` goes in `ApplicationWindow.menuBar` or `Window.menuBar` and holds `Menu`s. A `Menu` in a menu bar is
a title that opens its children as a Material menu; a `Menu` inside a menu is a submenu; and a `Menu` given as a
`Button`'s or `IconButton`'s [`menu`](#menu) opens as an anchored dropdown. Menu children: `MenuItem`, `Divider`,
`Repeater`, nested `Menu`, ...

`MenuBar` has no props of its own.

A menu is as wide as its widest row (at least 112dp; a row is at least 160dp): every row, and a `Divider`, spans that
width, and the menu shrinks back when its rows get shorter.

| `Menu` prop | Type | Default | Notes |
|---|---|---|---|
| `title` | string | `""` | Title in a menu bar, or the submenu's label. |

| `MenuItem` prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label (`bodyLarge`). |
| `icon` | icon | `""` | Leading icon. Having the prop at all reserves the icon slot, so labels line up. |
| `shortcut` | string | `""` | Trailing text, e.g. `"Ctrl+S"`. Display only. |
| `checked` | bool | `false` | **two-way.** If the prop is present at all (a value, a binding or a `bind`), the item is checkable: it shows a check in the icon slot while checked (instead of `icon`), toggles on click and keeps the menu open. Other items close the menu. |

**Signals:** `onTriggered(self)` when the item is chosen (after a checkable item toggled).
**Two-way:** `checked` (checkable items).

```lua
ApplicationWindow { menuBar = MenuBar {
    Menu { title = "File",
        MenuItem { "Open", icon = "folder_open", shortcut = "Ctrl+O", onTriggered = open },
        Divider {},
        MenuItem { "Word wrap", checked = bind(settings, "wrap") },
    },
}, ... }
```

---

## Text and decoration

### Text / Label

`Label` is an alias.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Content. |
| `style` | style | `"bodyLarge"` | Type-scale role. Unknown names draw as `bodyLarge`. |
| `color` | color | — | Text color. Unset: the theme's text color (`onSurface`). |
| `wrap` | bool | `false` | Wraps at the available width (a fixed `Column`'s, a `Grid` cell's, ...). In a `Row`, wrapping text takes a share of the free width and wraps at it. |
| `align` | `"start"` \| `"center"` \| `"end"` | `"start"` | Position of the line in the available width. Ignored while `wrap` is on. Aligned text fills: it is as wide as the available width (its offset and its line), and takes a share of a `Row`. |
| `font` | string | `""` | A [font the theme declares](theming.md#fonts) (`"mono"`, or `"regular"` / `"medium"`), drawn at the style's size. `""` or an undeclared name: the style's own face. |

### Icon

A Material icon glyph.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `name` | icon | — | The glyph. |
| `icon` | icon | — | The glyph, when `name` isn't set. |
| `text` | string | `""` | Positional. The glyph, when neither `name` nor `icon` is set. |
| `size` | number | `24` | Size (dp). |
| `color` | color | — | Tint. Unset: `onSurfaceVariant`. |

### Image

A PNG or JPEG image from the app's [resources](lua-api.md#resources), decoded once per resource name (stb_image)
and drawn as a texture. It's drawn at its natural size times the DPI scale (one image pixel per dp) unless
`width`/`height` are set, so it stays sharp only up to its pixel size. When the file changes in a directory mount
(dev builds), the image is decoded again within half a second, without a script reload.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `source` | string | `""` | Positional. A resource name: `app:/img/logo.png`, or relative (`logo.png`, `../img/x.jpg`) to the folder of the script that created the `Image` (a relative name from a binding too). Empty: nothing is drawn and no space is taken. |
| `width`, `height` | size | `0` | *Raw.* Unset: the natural size times the DPI scale. Only one set: the other follows the image's aspect ratio. Fill values work. |
| `fillMode` | `"stretch"` \| `"fit"` \| `"crop"` \| `"tile"` | `"stretch"` | How the image fills its size when the aspect ratios differ: stretched; scaled to fit inside, centered (`preserveAspectFit` too); scaled to cover, centered and cut off (`preserveAspectCrop` too); repeated at its natural size from the top left. An unknown name is reported and stretches. |
| `radius` | number | `0` | Corner radius (dp). With `fit`, the corners of the drawn image; not applied to `tile`. `radius` = half the size and `crop` make a circle. |
| `opacity` | number | `1` | 0 to 1. |
| `tint` | color | — | Multiplies the pixels (`"primary"`, `"#FFFFFF80"`); its alpha multiplies the opacity. |
| `smooth` | bool | `true` | `false` samples the nearest pixel (pixel art, scaled-up icons) instead of filtering. |
| `status` | string | — | *Read-only.* `"ready"`, `"error"` (missing or undecodable file), `"none"` (no source), or `"loading"` before the first render. Decoding happens on the first render that needs it. |
| `implicitWidth`, `implicitHeight` | number | `0` | *Read-only.* The image's size in pixels (dp at its natural size); 0 until it's ready. |

A missing or undecodable file is reported once in the error overlay (`Image: image 'app:/logo.png' not found`) and
drawn as a placeholder: a `surfaceContainerHighest` box with a broken-image icon, 48dp unless sized.

**Signals:** `onStatusChanged(self, status)`, `onImplicitWidthChanged(self, w)`, `onImplicitHeightChanged(self, h)`

```lua
Image { source = "img/avatar.jpg", width = 96, height = 96, fillMode = "crop", radius = 48 }
Image { source = "app:/banner.png", fillWidth = true, height = 160, fillMode = "crop", radius = 12,
        onStatusChanged = function(self, status) if status == "error" then print("no banner") end end }
```

### Link

A clickable text link (`bodyLarge`) that opens a URL in the browser.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label. |
| `url` | string | — | URL to open. Unset: `text`. |

### Divider / Separator

A 1dp `outlineVariant` line across the available width. `Separator` extends `Divider` with an optional label.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `inset`, `insetEnd` | number | `0`, `0` | Start and end insets (dp), on the plain rule. |
| `width` | size | `0` (fill) | *Raw.* The plain rule's width. |
| `text` | string | `""` | **`Separator` only.** Positional. With text, a labeled separator: the text in `titleSmall` `onSurfaceVariant` followed by a rule (the insets don't apply). |

### Spacer

Empty space.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `width`, `height` | size | `0` (1px) | *Raw.* Fixed space, or fill / fill minus like every size. |
| `fill` | bool | `false` | Fills the available width when `width` is unset: in a `Row`, a share of the remaining width. |

---

## Buttons and selection controls

### Button

A Material button.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label (`labelLarge`). |
| `variant` | `"filled"` \| `"tonal"` \| `"outlined"` \| `"text"` \| `"elevated"` \| `"fab"` | `"filled"` | Material emphasis. `fab` is a `primaryContainer` FAB: with empty `text` a 56dp square, otherwise an extended FAB. |
| `icon` | icon | `""` | Leading icon (18dp, or 24dp on a FAB). |
| `font` | string | `""` | The label's [font](theming.md#fonts), as on `Text`. |
| `width` | size | `0` (natural, at least 48dp) | *Raw.* |
| `height` | size | `0` (40dp, FAB 56dp) | *Raw.* |
| `menu` | element | — | *Raw.* A [`Menu`](#menu) opened on click. |

**Signals:** `onClicked(self)`.

### IconButton

A 40dp icon container with a 48dp touch target.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. The icon name when `icon` isn't set: `IconButton { "menu" }`. |
| `icon` | icon | — | Icon name. Unset: `text`. |
| `checkedIcon` | icon | — | Icon while checked. Unset: the normal icon. |
| `variant` | `"standard"` \| `"filled"` \| `"tonal"` \| `"outlined"` | `"standard"` | |
| `checked` | bool | `false` | **two-way.** If the prop is present at all, the button is a toggle with selected and unselected looks, and a click flips it. |
| `color` | color | — | Icon tint. Unset: the variant's. |
| `menu` | element | — | *Raw.* A [`Menu`](#menu) opened on click. |

**Signals:** `onClicked(self)` (after a toggle flipped `checked`).
**Two-way:** `checked` (toggles).

### Checkbox

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label. |
| `checked` | bool | `false` | **two-way.** |

**Signals:** `onToggled(self, checked)`.
**Two-way:** `checked`.

### Switch

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label. |
| `checked` | bool | `false` | **two-way.** |
| `icons` | bool | `true` | The check / close icons in the handle. |

**Signals:** `onToggled(self, checked)`.
**Two-way:** `checked`.

### RadioButton

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label. |
| `checked` | bool | `false` | **two-way.** Set to `true` when clicked. Without a `group` it's never set back to `false`. |
| `group` | any value | — | *Lua side, read when the element is created.* RadioButtons with the same `group` act as one group: clicking one writes `checked = false` to the others (through their `bind`s; a function binding is left alone). |

**Signals:** `onClicked(self)` (after `checked` and the group were updated).
**Two-way:** `checked`.

```lua
-- The group keeps plain `checked` values exclusive:
RadioButton { "Small", group = "size" }
RadioButton { "Medium", group = "size", checked = true }

-- Or derive `checked` from state (no group needed):
local s = state { size = "m" }
RadioButton { "Small", checked = function() return s.size == "s" end, onClicked = function() s.size = "s" end }
RadioButton { "Medium", checked = function() return s.size == "m" end, onClicked = function() s.size = "m" end }
```

### RadioGroup

A list of radio buttons from a model.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `model` | list of strings | `{}` | Option labels. |
| `currentIndex` | number | `1` | **two-way.** The selected option. |
| `horizontal` | bool | `false` | Lays the options out in a row (16dp apart). |

**Signals:** `onActivated(self, index, text)` when another option is picked.
**Two-way:** `currentIndex`.

### Chip

A 32dp Material chip.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Label. |
| `variant` | `"filter"` \| `"assist"` \| `"suggestion"` \| `"input"` | `"filter"` | A `filter` chip toggles and shows a check while selected. The other variants only report clicks. |
| `checked` | bool | `false` | **two-way** for a checkable `filter` chip. Selected state. |
| `checkable` | bool | `true` | `false` stops a filter chip from toggling itself (it still reports clicks). |
| `icon` | icon | `""` | Leading icon. |

**Signals:** `onClicked(self, checked)`, where `checked` is `not` the state before the click (for every variant).
**Two-way:** `checked` (checkable filter chips).

```lua
Row { spacing = 8,
    Repeater { model = { "lua", "cpp" }, delegate = function(tag)
        return Chip { tag, checked = bind(sel.tags, tag) }
    end },
}
```

### SegmentedButton

| Prop | Type | Default | Notes |
|---|---|---|---|
| `model` | list of strings | `{}` | Segment labels. |
| `icons` | list of strings | `{}` | Per-segment icons, shown on unselected segments. The selected segment shows a check instead. |
| `currentIndex` | number | `1` | **two-way.** The selected segment. |
| `width` | size | `0` (natural) | *Raw.* |

**Signals:** `onActivated(self, index, text)`.
**Two-way:** `currentIndex`.

### ListItem

A one-line (56dp) or two-line (72dp) list item.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | Positional. Headline (`bodyLarge`). |
| `supportingText` | string | `""` | Second line; makes the item two-line. |
| `icon` | icon | `""` | Leading icon. |
| `trailingText` | string | `""` | Trailing label (`labelSmall`). |
| `trailingIcon` | icon | `""` | Trailing icon. |
| `selected` | bool | `false` | Selected look. |
| `width` | size | `0` (fill) | *Raw.* |

**Signals:** `onClicked(self)`.

---

## Value inputs

### Slider

A 44dp-tall Material slider. A value label appears above the handle while you drag.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `value` | number | `0` | **two-way.** Kept as a double end to end and written back only when the user moves the slider, so a bound `0.1` stays `0.1`. A stepped value is `from + k * stepSize` without binary noise (three steps of `0.1` are `0.3`); with an integral `stepSize` it is an integer. |
| `from`, `to` | number | `0`, `1` | Range. |
| `stepSize` | number | `0` | `0` means continuous. Tick marks are drawn when there are at most 40 steps. |
| `format` | printf string | — | Value label. Unset: `"%.0f"` if `stepSize >= 1`, else `"%.2f"`. Exactly one `%f`, `%e` or `%g` conversion (flags, width and precision allowed), plus any `%%`. Anything else is reported once and replaced by `"%.2f"`. |
| `width` | size | `0` (fill) | *Raw.* |

**Signals:** `onMoved(self, value)` while the value changes, `onReleased(self, value)` when the handle is
released after the drag or click changed the value (a click on the current value emits neither).
**Two-way:** `value`.

### SpinBox

A numeric field (56dp) with decrement and increment icon buttons.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `value` | number | `0` | **two-way.** Clamped to the range. |
| `from`, `to` | number | `-1e9`, `1e9` | Range. With `from > to` the range is empty and the buttons don't clamp. |
| `stepSize` | number | `1` | Button step. |
| `decimals` | integer | `0` | Shown decimals, clamped to 0..15. |
| `label` | string | `""` | Floating label. |
| `variant` | `"filled"` \| `"outlined"` | `"filled"` | |
| `width` | size | `0` (200dp) | *Raw.* |

**Signals:** `onValueModified(self, value)`.
**Two-way:** `value`.

### TextField / TextArea

A Material text field (56dp) with a floating label. `TextArea` extends `TextField`: the same props and signals,
multi-line. Clicking anywhere in the container (but the trailing icon) focuses the input.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `text` | string | `""` | **two-way.** Content. |
| `label` | string | `""` | Floating label. |
| `placeholderText` | string | `""` | Hint, shown while focused (or always if there's no label). Single-line only. |
| `variant` | `"filled"` \| `"outlined"` | `"filled"` | |
| `leadingIcon`, `trailingIcon` | icon | `""` | Icons. The trailing icon is clickable. |
| `supportingText` | string | `""` | Helper text below. Error-colored while `error` is set. |
| `error` | bool | `false` | Error colors. |
| `password` | bool | `false` | Masks the input and adds a visibility toggle, unless `trailingIcon` is set. |
| `echoMode` | string | `""` | `"password"` does the same as `password = true`. |
| `readOnly` | bool | `false` | |
| `width` | size | `0` (fill) | *Raw.* |
| `height` | size | `0` (`TextArea`: 120dp) | *Raw.* **`TextArea` only;** a single-line field is always 56dp. |

**Signals:** `onTextChanged(self, text)` on every edit, `onAccepted(self, text)` on Enter (single-line only),
`onTrailingClicked(self)`.
**Two-way:** `text`.

```lua
TextField { id = "nameField", label = "Name", leadingIcon = "person", text = bind(form, "name"),
            onAccepted = function(_, text) submit(text) end }
TextArea { label = "Notes", height = 160, text = bind(form, "notes") }
```

### ComboBox

A Material exposed dropdown menu: a text-field look, with the menu anchored below.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `model` | list of strings | `{}` | Options. |
| `icons` | list of strings | `{}` | Per-option menu icons. |
| `currentIndex` | integer | `1` | **two-way.** The selected option. |
| `label` | string | `""` | Floating label. |
| `leadingIcon` | icon | `""` | |
| `supportingText` | string | `""` | |
| `variant` | `"filled"` \| `"outlined"` | `"filled"` | |
| `error` | bool | `false` | |
| `width` | size | `0` (240dp) | *Raw.* |

**Signals:** `onActivated(self, index, text)`.
**Two-way:** `currentIndex`.

### ColorPicker

A swatch with the hex value. Clicking it opens a picker popup with a hue bar and a hex field.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `color` | color | — | **two-way.** Any color spec; written back as `"#RRGGBB"` (`"#RRGGBBAA"` with alpha). Unset shows white. |
| `label` | string | `""` | |
| `alpha` | bool | `false` | Adds an alpha bar. Without it the color stays opaque, also when the hex field gets `#RGBA` or `#RRGGBBAA`. |
| `width` | size | `0` (natural) | *Raw.* |

**Signals:** `onColorChanged(self, hex)`.
**Two-way:** `color`.

---

## Progress, data and drawing

### ProgressBar

A Material 3 (2024) linear indicator, with a gap and a stop indicator.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `value` | number | `0` | 0..1. Clamped; NaN (`done / total` with `total == 0`) shows 0. |
| `indeterminate` | bool | `false` | Animated. |
| `width` | size | `0` (240dp) | *Raw.* |

### CircularProgress

| Prop | Type | Default | Notes |
|---|---|---|---|
| `value` | number | `0` | 0..1. Clamped; NaN shows 0. |
| `indeterminate` | bool | `false` | Spinning. |
| `size` | size | `0` (48dp) | *Raw.* Diameter. |
| `thickness` | number | `4` | Stroke width (dp). |

### BusyIndicator

An indeterminate circular indicator.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `running` | bool | `true` | While `false`, the space is still reserved. |
| `size` | size | `0` (48dp) | *Raw.* Diameter. |
| `thickness` | number | `4` | Stroke width (dp). |

### Plot

A line or bar chart with a grid, a gradient area and a hover readout.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `values` | list of numbers | `{}` | Data. Non-finite values (NaN, ±inf) are gaps: no point, bar or readout, the line breaks around them, and the automatic range ignores them. |
| `kind` | `"lines"` \| `"bars"` \| `"histogram"` | `"lines"` | `histogram` is the same as `bars`. |
| `fill` | bool | `true` | Gradient area under lines. |
| `title` | string | — | Chart title. Unset: `label`. |
| `label` | string | `""` | Chart title when `title` isn't set. |
| `color` | color | — | Series color. Unset: `primary`. |
| `min`, `max` | number | — | Setting either one fixes the range (the other defaults to 0 or 1). Unset: an automatic range, which includes 0 for bars. |
| `width` | size | `0` (fill) | *Raw.* |
| `height` | size | `0` (160dp) | *Raw.* |

### Table

A Material data table: a 56dp header and selectable rows, inside an outlined card.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `columns` | list of strings | `{}` | Header labels; sets the column count. Nothing is drawn without columns. At most 511 are shown (ImGui's table limit); more are reported as a script error. |
| `rows` | list of lists | — | *Raw.* Cell values, converted to strings (`nil` is empty). Only the rows in view are drawn and read (`ImGuiListClipper`; every row is `rowHeight` tall), so a long table costs what its visible rows do; the table still takes the height of all of them. A row that isn't a list draws empty and is reported as a script error when it comes into view. |
| `currentRow` | integer | `0` | **two-way.** The selected row; `0` means none. |
| `rowHeight` | number | `52` | Row height (dp). |
| `outlined` | bool | `true` | Wraps the table in an outlined card. |
| `width` | size | `0` (fill) | *Raw.* With or without the card. |

**Signals:** `onRowClicked(self, row)`, after the rows are drawn: the handler may change `rows`.
**Two-way:** `currentRow`.

```lua
Table { columns = { "Name", "Size" }, rows = function() return files end, currentRow = bind(s, "file"),
        onRowClicked = function(_, row) open(files[row]) end }
```

### Canvas

Custom drawing from Lua. The canvas is an invisible button, and `onPaint` draws on it on every rendered frame,
clipped to its rectangle. Frames are [rendered on demand](architecture.md#frame-loop), so a canvas whose drawing
changes over time (it reads `p.time`) needs `animated = true`.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `width` | size | `0` (fill) | *Raw.* |
| `height` | size | `0` (200dp) | *Raw.* `-1` fills the remaining height. |
| `animated` | bool | `false` | Requests a frame every frame while the canvas is rendered, so time-based drawing keeps moving. |

**Signals:** `onPaint(self, p)` draws with the painter `p`; `onClicked(self, x, y)`, with the position in dp
relative to the canvas.

**Painter fields** (dp, plain numbers):

| Field | Meaning |
|---|---|
| `p.width`, `p.height` | Canvas size. |
| `p.mouseX`, `p.mouseY` | Mouse position relative to the canvas. |
| `p.hovered`, `p.pressed` | Mouse state over the canvas. |
| `p.time` | Seconds since start. |

**Painter methods** (`src/imlui/lua/painter.cppm`). Coordinates are in dp from the canvas's top-left corner;
colors are color spec strings or `{r, g, b, a}` tables (anything else uses the default). Rectangles and circles
are drawn with the crisp Material shape helpers: true corner radii, smooth arcs and pixel-snapped strokes.
Horizontal and vertical lines are pixel-snapped bands a whole number of pixels thick; diagonal lines and polylines
use ImGui's anti-aliased lines.

| Method | Defaults |
|---|---|
| `p:line(x1, y1, x2, y2, color, thickness)` | `color = "onSurface"`, `thickness = 1` |
| `p:rect(x, y, w, h, color, rounding, outline, thickness)` | `color = "primary"`, `rounding = 0`, `outline = false` (filled), `thickness = 1` |
| `p:circle(cx, cy, r, color, outline, thickness)` | `color = "primary"`, `outline = false` (filled), `thickness = 1` |
| `p:text(x, y, text, color, style, font)` | top-left at `(x, y)`, `color = "onSurface"`, `style = "bodyMedium"`, `font` = a [named font](theming.md#fonts) (unset: the style's face) |
| `p:icon(cx, cy, name, size, color)` | centered on `(cx, cy)`, `size = 24`, `color = "onSurfaceVariant"` |
| `p:polyline({x1, y1, x2, y2, ...}, color, thickness, closed)` | `color = "primary"`, `thickness = 1`, `closed = false` |

Calling the painter outside `onPaint` raises an error.

```lua
Canvas { height = 200, onPaint = function(_, p)
    p:rect(0, 0, p.width, p.height, "surfaceContainer", 12)
    p:polyline({ 0, p.height, p.width / 2, 20, p.width, p.height }, "primary", 3)
    if p.hovered then p:circle(p.mouseX, p.mouseY, 16, "primary/0.2") end
    p:text(16, 12, "Hello", "onSurface", "titleMedium")
end }
```

---

## Non-visual

`ApplicationWindow`, `Window`, `Dialog`, `Snackbar`, `ImGuiDemo` and `ModalNavigationDrawer` are non-visual too;
they're described with their families above.

### Timer

A timer element. It **ticks even when it isn't rendered**: a `Timer` on a page that isn't shown, inside a
`visible = false` parent or in a `Loader` that shows something else still fires. It's a ticking element
(`Element::setTicking`): its `tick()` runs at the start of every frame, rendered or not, with its props synced
first, so it also sees `running` turn true through a binding. It ticks from the moment the element is created
until it's collected (or the script reloads). Use `running = false` to stop it. A `Timer` that a container drops (a
`Repeater` / `ListView` delegate rebuilt or removed because the model changed, a `Loader` or `Responsive` item
replaced) stops on the next frame when nothing else references it: the next `ui.__tick` collects garbage first. One
still referenced (a page kept in a table, an `id`) keeps ticking. See
[ticking elements](extending.md#ticking-elements). The engine wakes up for the next
trigger, so it keeps its rate without input. Each period starts where the last one ended, not at the frame that
fired it, so it doesn't drift; a frame that comes more than a period late fires once and skips the missed ones.

| Prop | Type | Default | Notes |
|---|---|---|---|
| `interval` | number | `1000` | Period in ms, > 0. Another value (0, negative, NaN, infinite) is reported as a script error, and the timer doesn't fire until it gets a valid one. |
| `running` | bool | `true` | **two-way.** Starting it (again) restarts the period. A one-shot writes `false` back when it fires. Props sync once per frame, so `running = false` then `true` before the next frame is no change: use `restart()`. Under a function binding (`running = function() ... end`), which the timer can't write, a one-shot stays stopped until the function has returned `false` and then `true` again. |
| `repeat` | bool | `true` | `false` fires once. |
| `triggeredOnStart` | bool | `false` | Also fires when it starts running. |

**Methods:** `timer:restart()` starts the period over now (and fires `triggeredOnStart`), running or not; a
stopped timer gets `running = true`.
**Signals:** `onTriggered(self)`; `onRunningChanged(self, false)` when a one-shot stops itself.
**Two-way:** `running`.

```lua
local fb = state { seconds = 0 }
Timer { interval = 1000, onTriggered = function() fb.seconds = fb.seconds + 1 end }
Timer { interval = 3000, repeat = false, running = bind(s, "toast"), onTriggered = hideToast }
```

For timers without an element, see [`setInterval` / `setTimeout`](lua-api.md#timers-and-frames).

---

## At a glance

### Two-way props

| Element | Two-way props |
|---|---|
| `Window`, `Dialog`, `ImGuiDemo`, `ModalNavigationDrawer` | `open` |
| `TabBar`, `RadioGroup`, `SegmentedButton`, `ComboBox` | `currentIndex` |
| `Section` / `Expander` | `expanded` |
| `Checkbox`, `Switch`, `RadioButton`, `Chip` (checkable filter), `IconButton` (toggle), `MenuItem` (checkable) | `checked` |
| `Slider`, `SpinBox` | `value` |
| `TextField`, `TextArea` | `text` |
| `ColorPicker` | `color` |
| `Table` | `currentRow` |
| `Timer` | `running` |
| `Snackbar` (internal) | `shownAt` |

Each also emits `on<Prop>Changed(self, value)` when written back ([above](#values-that-come-back-from-c)).

### Signals

| Element | Signals |
|---|---|
| `Window`, `Dialog`, `ModalNavigationDrawer` | `onClosed(self)` |
| `Snackbar` | `onAction(self)` |
| `TabBar` | `onCurrentChanged(self, index)` |
| `Section` / `Expander` | `onToggled(self, expanded)` |
| `TopAppBar` | `onNavigation(self)` |
| `NavigationItem`, `Button`, `IconButton`, `RadioButton`, `ListItem` | `onClicked(self)` |
| `MenuItem`, `Timer` | `onTriggered(self)` |
| `Checkbox`, `Switch` | `onToggled(self, checked)` |
| `Chip` | `onClicked(self, checked)` |
| `RadioGroup`, `SegmentedButton`, `ComboBox` | `onActivated(self, index, text)` |
| `Slider` | `onMoved(self, value)`, `onReleased(self, value)` |
| `SpinBox` | `onValueModified(self, value)` |
| `TextField`, `TextArea` | `onTextChanged(self, text)`, `onAccepted(self, text)`, `onTrailingClicked(self)` |
| `ColorPicker` | `onColorChanged(self, hex)` |
| `Table` | `onRowClicked(self, row)` |
| `Canvas` | `onPaint(self, p)`, `onClicked(self, x, y)` |
| `Image` | `onStatusChanged(self, status)` |

### Non-visual elements

`ApplicationWindow`, `Window`, `Dialog`, `Snackbar`, `ImGuiDemo`, `ModalNavigationDrawer`, `Timer`.

## Custom elements

Every element above is a C++ class derived from `imlui::Element`, with a static `declare(TypeBuilder<T>&)` that
lists its properties, signals, default property and so on. Register your own the same way and use it like a
built-in (registering under a built-in's name replaces it):

```cpp
engine.registerType<Gauge>("Gauge");
```

```lua
Gauge { id = "cpu", value = bind(s, "cpu"), label = "CPU", onPeak = function(self, v) ... end }
Button { "Reset", onClicked = function() cpu:reset() end }
```

`ui.types()` lists the registered names. See [Registering C++ types](extending.md) for the whole API, and the
element modules in `src/imlui/ui/elements/` for real examples.
