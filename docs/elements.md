# Elements

Every built-in element, grouped like the root README. Defaults come from the element handlers (`renderer.cppm`,
moving to `imlui.ui.elements.*`). Sizes are in **dp**. Colors are [color specs](theming.md#color-specs). Any prop
can be a plain value, a binding function or a `bind(...)` ([property values](lua-api.md#property-values)).
**Two-way** props are written back when the user changes them.

Signal handlers get `self` first: `onToggled(self, checked)`.

## Common props

These apply to every element.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | — | Makes the element reachable by name from the script (`nameField.text`). |
| `visible` | bool | `true` | `false` skips the element entirely. It takes no space, and for an inline container all of its children are skipped. |
| `enabled` | bool | — | `false` disables the element and everything inside it. Widgets switch to Material disabled colors. |
| `tooltip` | string | — | Plain tooltip (inverse surface colors, `bodySmall`) shown on hover, also while disabled. |
| `width` | number | element default | `> 0`: fixed width in dp. `-1`: fill the available width. `< -1`: fill minus that amount. `0`: element default (see each element). |
| `height` | number | element default | Same rules, for elements that read it. |
| `fillWidth` | bool | `false` | Shorthand for `width = -1`. |

The first positional string works as `text`: `Button { "OK" }` is `Button { text = "OK" }`. Negative sizes are
currently applied in pixels, not dp.

Elements that hold children stack them (`Column`, `Card`, `ScrollView`, `Tab`, `Dialog`, `Window`,
`ApplicationWindow`, ...). They also accept:

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `spacing` | number | theme `spacing` | Vertical gap between visible children (dp). |
| `align` | `"start"` \| `"center"` \| `"end"` | `"start"` | Horizontal position of each child. |

---

## Windows

### ApplicationWindow

The root element. It fills the native window, with no decoration, and the engine reads its native window props.
It's non-visual. Its children are stacked.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `title` | string | `"imlui"` | Native window title, updated every frame. |
| `width`, `height` | number | — | Native window size, applied at load if both are `> 0`. On hot reload it's applied again only if it changed. |
| `padding` | number | `24` | Content padding. |
| `color` | color | `surface` | Background. |
| `scrollable` | bool | `true` | `false` hides the scrollbar. |
| `menuBar` | `MenuBar` | — | Menu bar at the top. |

### Window

A floating ImGui window with Material surface colors. It's non-visual. Its children are stacked.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `title` | string | `"Window"` | Title bar text. |
| `open` | bool, **two-way** | `true` | If the prop is present, the window gets a close button that sets `open = false`. |
| `x`, `y` | number | `60`, `60` | Initial position. |
| `width`, `height` | number | `0` | Initial size. |
| `autoResize` | bool | `true` if no size is given | Fits the window to its content. |
| `titleBar`, `resizable`, `movable` | bool | `true` | Window chrome. |
| `menuBar` | `MenuBar` | — | Menu bar. |

Signals: `onClosed(self)`.

### Dialog

A Material modal dialog: centered, with a scrim behind it, `extraLarge` corners and a `headlineSmall` title.
`Esc` closes it. It's non-visual. Its children are stacked.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `open` | bool, **two-way** | `false` | Shows the dialog. It's set to `false` when the dialog is dismissed. |
| `title` | string | `""` | Headline. |
| `icon` | string | `""` | Hero icon above the title, which is then centered. |
| `width` | number | 280–560 | Fixed width. |

Signals: `onClosed(self)`.

---

## Layout

### Column / Item

Vertical stack. `Item` is an alias.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `spacing`, `align` | | | See [common props](#common-props). |
| `padding` | number | `0` | Inset at the top, bottom and left. |
| `width` | number | `0` (auto) | A fixed width that children can fill (`fillWidth`) and `align` within. |

### Row

Horizontal layout. Sizes are measured on the previous frame, so the layout settles in one frame.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `spacing` | number | theme `spacing` | Horizontal gap. |
| `align` | `"center"` \| `"top"` \| `"bottom"` | `"center"` | Vertical alignment of the children. |
| `justify` | `"start"` \| `"center"` \| `"end"` \| `"spaceBetween"` | `"start"` | Horizontal distribution. Ignored while the row has fill spacers. |
| `width` | number | available width | Width used for `justify` and fills. |

`Spacer { fill = true }` children share the remaining width: `Row { Spacer { fill = true }, Button {...} }`
right-aligns the button.

### Grid

Table-based grid. Children fill the cells left to right.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `columns` | number | `2` | Column count. Columns stretch equally. |
| `spacing` | number | `8` | Cell gap. |
| `border` | bool | `false` | Cell borders. |
| `width` | number | fill | |

### ScrollView

A scrolling child region. Its children are stacked.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `width`, `height` | number | `0` (fill remaining) | Size. |
| `padding` | number | `0` | Inner padding. |
| `color` | color | transparent | Background. It's also the surface color for the children. |
| `rounding` | number | theme `shape.medium` | Corner radius. |
| `border` | bool | `false` | Border. |

### ListView

A `ScrollView` whose children come from `model`/`delegate`. It takes every `ScrollView` prop. See
[ListView](lua-api.md#listview).

### Card

Material card. Its children are stacked.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `variant` | `"elevated"` \| `"filled"` \| `"outlined"` | `"elevated"` | `elevated`: `surfaceContainerLow` with a shadow. `filled`: `surfaceContainerHighest`. `outlined`: `surface` with an `outlineVariant` border. |
| `width` | number | fill | |
| `height` | number | auto | Fixed height. If it isn't set, the card fits its content. |
| `padding` | number | `16` | Inner padding. |
| `color` | color | per variant | Background. |
| `rounding` | number | theme `shape.medium` | Corner radius. |

### TabBar / Tab

Material tabs. A `TabBar` collects its visible `Tab` children as headers and renders only the selected tab's
content, stacked.

| `TabBar` prop | Type | Default | Meaning |
|---|---|---|---|
| `currentIndex` | number, **two-way** | `1` | Selected tab, 1-based. |
| `variant` | `"primary"` \| `"secondary"` | `"primary"` | Primary tabs with icons stack the icon over the label (64dp). Secondary tabs use a full-width indicator. |
| `width` | number | fill | |

Signals: `onCurrentChanged(self, index)`.

| `Tab` prop | Type | Default | Meaning |
|---|---|---|---|
| `title` | string | `"Tab"` | Header label. |
| `icon` | string | `""` | Header icon. |
| `visible` | bool | `true` | Hides the tab and its header. |

A `Tab` outside a `TabBar` renders its content inline.

### Expander / Section

A collapsible header (48dp, or 64dp with supporting text). The children are indented 16dp when expanded.
`Section` is an alias.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `title` | string | `text` | Header text (`titleMedium`). |
| `supportingText` | string | `""` | Second line. |
| `expanded` | bool, **two-way** | `true` | Open state. |
| `width` | number | fill | |

Signals: `onToggled(self, expanded)`.

### TreeNode

An ImGui tree node (`bodyLarge`) whose children render inside it.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | `""` | Label. |
| `expanded` | bool | `false` | Initially open. Not written back. |

### Repeater, If, Loader

Inline containers that produce children dynamically. See [dynamic children](lua-api.md#dynamic-children).

---

## App bars & navigation

### TopAppBar

Material top app bar. Its children are **actions**, right-aligned and vertically centered in the 64dp bar.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `title` | string | `""` | `titleLarge` for `small` and `center`, `headlineSmall` for `medium`, `headlineMedium` for `large`. |
| `variant` | `"small"` \| `"center"` \| `"medium"` \| `"large"` | `"small"` | Height 64, 64, 112 or 152dp. `center` centers the title. `medium` and `large` put the title below the bar row. |
| `navigationIcon` | string | `""` | Leading icon button. |
| `color` | color | current surface | Background. |
| `width` | number | fill | |

Signals: `onNavigation(self)` when the navigation icon is clicked.

### NavigationItem

A navigation drawer item: a 56dp pill with an icon, a label and an optional badge.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | `""` | Label. |
| `icon` | string | `""` | Leading icon. |
| `selected` | bool | `false` | Selected look (`secondaryContainer`). |
| `badge` | string | `""` | Trailing badge text. |
| `width` | number | fill | |

Signals: `onClicked(self)`.

### MenuBar, Menu, MenuItem

Use a `MenuBar` in `ApplicationWindow.menuBar` or `Window.menuBar`, with `Menu` children. You can also give
**any `Button` or `IconButton`** a `menu = Menu { ... }`. It then opens as an anchored dropdown (min width 112dp)
when the button is clicked.

| `Menu` prop | Type | Default | Meaning |
|---|---|---|---|
| `title` | string | `""` | Menu title (in a `MenuBar`). |

Children: `MenuItem`, `Divider`, `Repeater`, nested `Menu`, ...

| `MenuItem` prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | `""` | Label (`bodyLarge`). |
| `icon` | string | `""` | Leading icon. |
| `shortcut` | string | `""` | Trailing text. Display only. |
| `checked` | bool, **two-way** | — | If present, the item is checkable: it shows a check, toggles on click, and the menu stays open. |

Signals: `onTriggered(self)`.

---

## Text

### Text / Label

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | positional | Content. |
| `style` | string | `"bodyLarge"` | [Text style](theming.md#typography). |
| `color` | color | `onSurface` | Text color. |
| `wrap` | bool | `false` | Wraps at the available width. |
| `align` | `"start"` \| `"center"` \| `"end"` | `"start"` | Horizontal alignment. Ignored when `wrap` is on. |

### Icon

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | `icon`, or positional | Material icon name. |
| `size` | number | `24` | Size in dp. |
| `color` | color | `onSurfaceVariant` | Tint. |

### Link

A clickable text link (`bodyLarge`, `primary` color) that opens a URL.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | positional | Label. |
| `url` | string | `text` | URL to open. |

### Divider / Separator

A 1dp `outlineVariant` line across the available width.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `inset`, `insetEnd` | number | `0` | Start and end insets. |
| `text` | string | — | **`Separator` only:** a labeled separator (`titleSmall`, `onSurfaceVariant`). |

### Spacer

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `width`, `height` | number | `0` | Fixed empty space. |
| `fill` | bool | `false` | In a `Row`, takes a share of the remaining width. |

---

## Controls

### Button

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | positional | Label (`labelLarge`). |
| `variant` | `"filled"` \| `"tonal"` \| `"outlined"` \| `"text"` \| `"elevated"` \| `"fab"` | `"filled"` | Material emphasis. `fab` is a `primaryContainer` FAB: with empty `text` it's a 56dp square, otherwise an extended FAB. |
| `icon` | string | `""` | Leading icon (18dp, or 24dp on a FAB). |
| `width` | number | natural, min 48dp | |
| `height` | number | `40` (FAB `56`) | |
| `menu` | `Menu` | — | Dropdown opened on click. |

Signals: `onClicked(self)`.

### IconButton

A 40dp icon container with a 48dp touch target.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `icon` | string | positional | Icon name. |
| `variant` | `"standard"` \| `"filled"` \| `"tonal"` \| `"outlined"` | `"standard"` | |
| `checked` | bool, **two-way** | — | If present, the button is a toggle with selected and unselected looks. |
| `checkedIcon` | string | `icon` | Icon while checked. |
| `color` | color | per variant | Icon tint. |
| `menu` | `Menu` | — | Dropdown opened on click. |

Signals: `onClicked(self)`.

### Checkbox

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | `""` | Label. |
| `checked` | bool, **two-way** | `false` | |

Signals: `onToggled(self, checked)`.

### Switch

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | `""` | Label. |
| `checked` | bool, **two-way** | `false` | |
| `icons` | bool | `true` | Shows a check icon in the thumb when on. |

Signals: `onToggled(self, checked)`.

### RadioButton

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | `""` | Label. |
| `checked` | bool, **two-way** | `false` | Set to `true` when clicked. It's never set back to `false`, so group the buttons yourself or use `RadioGroup`. |

Signals: `onClicked(self)`.

### RadioGroup

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `model` | list of strings | `{}` | Option labels. |
| `currentIndex` | number, **two-way** | `1` | Selected option, 1-based. |
| `horizontal` | bool | `false` | Lays the options out in a row. |

Signals: `onActivated(self, index, text)`.

### Chip

A 32dp chip.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | positional | Label. |
| `variant` | `"filter"` \| `"assist"` \| `"suggestion"` \| `"input"` | `"filter"` | A `filter` chip toggles and shows a check while selected. The other variants only report clicks. |
| `checked` | bool, **two-way** for `filter` | `false` | Selected state. |
| `checkable` | bool | `true` | `false` stops a filter chip from toggling itself. |
| `icon` | string | `""` | Leading icon. |

Signals: `onClicked(self, checked)`, where `checked` is `not` the previous state.

### SegmentedButton

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `model` | list of strings | `{}` | Segment labels. |
| `icons` | list of strings | `{}` | Per-segment icons. The selected segment shows a check instead. |
| `currentIndex` | number, **two-way** | `1` | Selected segment, 1-based. |
| `width` | number | natural | |

Signals: `onActivated(self, index, text)`.

### ListItem

A one-line (56dp) or two-line (72dp) list item.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | positional | Headline (`bodyLarge`). |
| `supportingText` | string | `""` | Second line. Makes the item two-line. |
| `icon` | string | `""` | Leading icon. |
| `trailingText` | string | `""` | Trailing label (`labelSmall`). |
| `trailingIcon` | string | `""` | Trailing icon. |
| `selected` | bool | `false` | Selected look. |
| `width` | number | fill | |

Signals: `onClicked(self)`.

---

## Inputs

### Slider

A 44dp-tall Material slider. A value label appears above the handle while you drag.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `value` | number, **two-way** | `0` | |
| `from`, `to` | number | `0`, `1` | Range. |
| `stepSize` | number | `0` | `0` means continuous. Tick marks are drawn when there are at most 40 steps. |
| `format` | printf string | `"%.0f"` if `stepSize >= 1`, else `"%.2f"` | Value label. |
| `width` | number | fill | |

Signals: `onMoved(self, value)` while dragging, `onReleased(self, value)` when the handle is released.

### SpinBox

A numeric field (56dp) with decrement and increment icon buttons.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `value` | number, **two-way** | `0` | Clamped to the range. |
| `from`, `to` | number | `-1e9`, `1e9` | Range. |
| `stepSize` | number | `1` | Button step. |
| `decimals` | number | `0` | Shown decimals. |
| `label` | string | `""` | Floating label. |
| `variant` | `"filled"` \| `"outlined"` | `"filled"` | |
| `width` | number | `200` | |

Signals: `onValueModified(self, value)`.

### TextField / TextArea

A Material text field (56dp) with a floating label. `TextArea` is the multi-line version.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `text` | string, **two-way** | `""` | Content. |
| `label` | string | `""` | Floating label. |
| `placeholderText` | string | `""` | Hint, shown while focused (or always if there's no label). Single-line only. |
| `variant` | `"filled"` \| `"outlined"` | `"filled"` | |
| `leadingIcon`, `trailingIcon` | string | `""` | Icons. The trailing icon is clickable. |
| `supportingText` | string | `""` | Helper text below. Error-colored while `error` is set. |
| `error` | bool | `false` | Error colors. |
| `password` | bool | `false` | Masks input and adds a visibility toggle, unless `trailingIcon` is set. `echoMode = "password"` does the same. |
| `readOnly` | bool | `false` | |
| `width` | number | fill | |
| `height` | number | `120` | **`TextArea` only.** |

Signals: `onTextChanged(self, text)`, `onAccepted(self, text)` (Enter, single-line only),
`onTrailingClicked(self)`.

### ComboBox

A Material exposed dropdown menu: text-field look, with the menu anchored below.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `model` | list of strings | `{}` | Options. |
| `icons` | list of strings | `{}` | Per-option menu icons. |
| `currentIndex` | number, **two-way** | `1` | Selected option, 1-based. |
| `label` | string | `""` | Floating label. |
| `leadingIcon` | string | `""` | |
| `supportingText` | string | `""` | |
| `variant` | `"filled"` \| `"outlined"` | `"filled"` | |
| `error` | bool | `false` | |
| `width` | number | `240` | |

Signals: `onActivated(self, index, text)`.

### ColorPicker

A swatch with the hex value. Clicking it opens a picker popup with a hue bar and a hex field.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `color` | color, **two-way** | white | Written back as `"#RRGGBB"` (`"#RRGGBBAA"` with alpha). |
| `label` | string | `""` | |
| `alpha` | bool | `false` | Adds an alpha bar. |
| `width` | number | natural | |

Signals: `onColorChanged(self, hex)`.

---

## Feedback & data

### ProgressBar

A Material 3 (2024) linear indicator with a gap and a stop indicator.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `value` | number | `0` | 0..1. |
| `indeterminate` | bool | `false` | Animated. |
| `width` | number | `240` | |

### CircularProgress

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `value` | number | `0` | 0..1. |
| `indeterminate` | bool | `false` | Spinning. |
| `size` | number | `48` | Diameter. |
| `thickness` | number | `4` | Stroke width. |

### BusyIndicator

An indeterminate circular indicator.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `running` | bool | `true` | When it's `false`, the space is still reserved. |
| `size`, `thickness` | number | `48`, `4` | |

### Plot

A line or bar chart with a grid, a gradient area and a hover readout.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `values` | list of numbers | `{}` | Data. |
| `kind` | `"lines"` \| `"bars"` \| `"histogram"` | `"lines"` | `histogram` is the same as `bars`. |
| `fill` | bool | `true` | Gradient area under lines. |
| `title` | string | `label` | Chart title. |
| `color` | color | `primary` | Series color. |
| `min`, `max` | number | auto | Setting either one fixes the range (defaults 0 and 1). Auto range includes 0 for bars. |
| `width` | number | fill | |
| `height` | number | `160` | |

### Table

A Material data table: a 56dp header, rows with selection, inside an outlined card.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `columns` | list of strings | — | **Required.** Header labels. Sets the column count. |
| `rows` | list of lists | `{}` | Cell values, converted with `tostring`. |
| `currentRow` | number, **two-way** | `0` | Selected row, 1-based. `0` means none. |
| `rowHeight` | number | `52` | |
| `outlined` | bool | `true` | Wraps the table in an outlined card. |
| `width` | number | fill | |

Signals: `onRowClicked(self, row)`.

### Canvas

Custom drawing from Lua. The canvas is an invisible button, and `onPaint` draws on it every frame, clipped to
its rectangle.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `width` | number | fill | |
| `height` | number | `200` | |
| `onPaint` | `function(self, p)` | — | Draws with the painter `p`. |

Signals: `onClicked(self, x, y)`, with the position in dp relative to the canvas.

**Painter fields:**

| Field | Meaning |
|---|---|
| `p.width`, `p.height` | Canvas size (dp). |
| `p.mouseX`, `p.mouseY` | Mouse position relative to the canvas (dp). |
| `p.hovered`, `p.pressed` | Mouse state over the canvas. |
| `p.time` | Seconds since start. |

**Painter methods.** Coordinates are in dp from the canvas's top-left corner. Colors are color specs.

| Method | Defaults |
|---|---|
| `p:line(x1, y1, x2, y2, color, thickness)` | `color = "onSurface"`, `thickness = 1` |
| `p:rect(x, y, w, h, color, rounding, outline, thickness)` | `color = "primary"`, `rounding = 0`, `outline = false` (filled), `thickness = 1` |
| `p:circle(cx, cy, r, color, outline, thickness)` | `color = "primary"`, `outline = false`, `thickness = 1` |
| `p:text(x, y, text, color, style)` | `color = "onSurface"`, `style = "bodyMedium"` |
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

## Other

### Timer

A non-visual timer element. It only ticks while it's rendered: a `Timer` on a page that isn't shown, or with
`visible = false`, doesn't run. For timers that run regardless, use [`setInterval`](lua-api.md#timers-and-frames).

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `interval` | number | `1000` | Period in ms. |
| `running` | bool, **two-way** | `true` | Set to `false` after a one-shot fires. |
| `repeat` | bool | `true` | `false` fires once. |
| `triggeredOnStart` | bool | `false` | Also fires when it starts running. |

Signals: `onTriggered(self)`.

### ImGuiDemo

Shows the Dear ImGui demo window. It's non-visual.

| Prop | Type | Default | Meaning |
|---|---|---|---|
| `open` | bool, **two-way** | `true` | Closing the window sets it to `false`. |

### Snackbar (internal)

The app-wide overlay behind [`snackbar()`](lua-api.md#snackbar). It's created by the prelude, and you don't
normally use it directly. Its props are `text`, `actionText`, `onAction`, `shownAt`, `duration` (ms) and
`closable`.

---

## Two-way props at a glance

| Element | Two-way props |
|---|---|
| `Window`, `Dialog`, `ImGuiDemo` | `open` |
| `TabBar`, `RadioGroup`, `SegmentedButton`, `ComboBox` | `currentIndex` |
| `Expander` / `Section` | `expanded` |
| `Checkbox`, `Switch`, `RadioButton`, `Chip` (filter), `IconButton` (toggle), `MenuItem` (checkable) | `checked` |
| `Slider`, `SpinBox` | `value` |
| `TextField`, `TextArea` | `text` |
| `ColorPicker` | `color` |
| `Table` | `currentRow` |
| `Timer` | `running` |

## Custom elements from C++

```cpp
engine.renderer().registerElement("Badge", [](imlui::Renderer& r, const imlui::luaui::Node& n) {
    imlui::md::text(n.text(), imlui::TextStyle::LabelSmall, n.color("color"));
}, /*visual=*/true);
```

Any Capitalized name in a script is a constructor, so `Badge { text = "3" }` works immediately. See
[Development](development.md#adding-an-element).
