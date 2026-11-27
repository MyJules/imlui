# Theming

The theme is plain data, decided in Lua. `theme{...}` builds an `imlui::Theme`, which holds the design (the skin
that draws the controls), a Material 3 color scheme generated from one seed color, a shape scale, spacing, font scale
and the text fonts. It can also override
any color role, any raw `ImGuiCol_*` color and many `ImGuiStyle` fields. The engine applies it **between frames**. Scripts start
from the default theme on every (re)load: `design = "material"`, seed `#6750A4`, dark, `preset = "material"`.

## The theme table

```lua
theme {
    design    = "material",   -- the skin that draws the controls: "material" or "fluent"; see Designs
    tokens    = { controlHeight = 32, controlRadius = 4 }, -- a token-driven design's metrics; see Tokens
    preset    = "material",   -- or "imgui-dark" | "imgui-light" | "imgui-classic"
    seed      = "#6750A4",    -- any color spec; the scheme is generated from it
    dark      = true,
    spacing   = 8,            -- base spacing unit (dp)
    fontScale = 1.0,
    shape     = { extraSmall = 4, small = 8, medium = 12, large = 16, extraLarge = 28 },
    colors    = { primary = "#FF5722", surface = "surfaceContainerLow" }, -- override any color role
    imgui     = { WindowBg = "#101010" },                      -- override any ImGuiCol_*
    style     = { FrameRounding = 6, ItemSpacing = { 10, 6 } }, -- override ImGuiStyle fields
    fonts     = { regular = "app:/fonts/Inter-Regular.ttf", mono = "app:/fonts/JetBrainsMono.ttf" }, -- see Fonts
}
```

| Field | Type | Default | Meaning |
|---|---|---|---|
| `design` | string | `"material"` | The skin that draws and measures the controls: `"material"`, `"fluent"`, or a skin the app registered in C++. An unknown name (or a value that isn't a string) reports an error (`theme.design: unknown design 'x' (registered: 'fluent', 'material')`) and keeps `"material"`. See [Designs](#designs). |
| `tokens` | `{ token = value }` | the design's | Overrides of the design's metrics: control height, radii, strokes, focus and underline styles, padding, density. Fluent draws with them; Material ignores them. An unknown token, or a value of the wrong kind or out of range, reports an error and keeps the default. See [Tokens](#tokens). |
| `preset` | string | `"material"` | `"material"` maps the scheme onto `ImGuiStyle` (under design `"fluent"`: the Fluent colors and the tokens, see [The Fluent design](#the-fluent-design)). `"imgui-dark"`, `"imgui-light"` and `"imgui-classic"` use ImGui's stock styles instead. The skins always draw with the scheme. Another name (or a value that isn't a string) reports an error (`theme.preset: unknown preset 'x' (one of: 'material', 'imgui-dark', 'imgui-light', 'imgui-classic')`) and keeps `"material"`. |
| `seed` | color | `"#6750A4"` | Seed color: hex, `{r,g,b,a}`, or a role (`"primary"`, `"tertiary/1"`) of the theme currently in effect. An invalid value reports an error and keeps the default. |
| `dark` | bool | `true` | Dark or light scheme. |
| `spacing` | number | `8` | Base unit for ImGui paddings and spacings. It's also the default gap in `Column` and `Row`. 0 to 10000. |
| `fontScale` | number | `1.0` | Multiplies every text size. Glyphs are rasterized at the final size, so text stays sharp. 0.25 to 4; text is at most 512 pixels tall whatever the scale, zoom and monitor (see [Typography](#typography)). |
| `shape` | table | see above | Material shape scale: corner radii in dp, each 0 to 10000. |
| `colors` | `{ role = color }` | — | Overrides generated roles. Values are [color specs](#color-specs); role names refer to the scheme generated from this table's `seed`/`dark` (before any overrides). An unknown role or invalid color reports an error. |
| `imgui` | `{ ImGuiColName = color }` | — | Raw ImGui color overrides by name (`"WindowBg"`, `"Text"`, ...), applied after the preset. Values are [color specs](#color-specs), with roles from this table's generated scheme. An unknown name or invalid color reports an error. |
| `style` | `{ Field = n \| {x, y} }` | — | Raw `ImGuiStyle` overrides, applied after the preset. An unknown field or a value out of range (below) reports an error. |
| `fonts` | `{ key = resource name }` | Roboto | The type scale's faces, named fonts and glyph fallbacks. See [Fonts](#fonts). |

**The window's title bar follows the theme.** On Windows the engine colors the native title bar with the theme's
`WindowBg` and `Text` colors (whatever the preset or overrides make them): Windows 11 takes both exactly; Windows 10
(20H1 and later) switches the title bar to dark mode when `WindowBg` is dark. It's set before the window first
shows and follows every theme change. Other platforms keep their own title bar.

`style` accepts these fields: `Alpha`, `DisabledAlpha`, `WindowPadding`, `WindowRounding`, `WindowBorderSize`,
`WindowMinSize`, `WindowTitleAlign`, `ChildRounding`, `ChildBorderSize`, `PopupRounding`, `PopupBorderSize`,
`FramePadding`, `FrameRounding`, `FrameBorderSize`, `ItemSpacing`, `ItemInnerSpacing`, `CellPadding`,
`IndentSpacing`, `ScrollbarSize`, `ScrollbarRounding`, `GrabMinSize`, `GrabRounding`, `TabRounding`,
`TabBorderSize`, `TabBarBorderSize`, `TabBarOverlineSize`, `ButtonTextAlign`, `SelectableTextAlign`,
`SeparatorTextBorderSize`, `SeparatorTextAlign`, `SeparatorTextPadding`. A number sets a scalar field, and
`{x, y}` sets a vector field (a number sets both of its components; a table needs both).

Values are checked, since ImGui asserts on some of them: every component must be a finite number of at most 10000
in magnitude, `Alpha` and `DisabledAlpha` must be in 0..1, and both components of `WindowMinSize` at least 1. A style
value, `spacing`, `fontScale` or a radius out of its range, or one that isn't a number, reports an error
(`theme.style.Alpha: must be in 0..1`) and is ignored: the field keeps its default. A `Theme` built in C++ is held to the same limits by `setTheme`, which
clamps `fontScale`, `spacing`, the radii and every color (`seed`, the scheme's roles, `imguiColors`: to 0..1, NaN as 0) and drops style overrides it can't apply (`styleValueProblem`). Sizes are
scaled by the DPI scale and zoom like the defaults (ImGui truncates them to whole pixels); `WindowMinSize` stays at
least 1 pixel at any zoom, and `ItemSpacing.x` is applied as at least 0 (ImGui's menus store it as an unsigned
width; a negative `ItemSpacing.y` is kept).

After the preset and the overrides, every style size is scaled by the DPI factor
(`ImGuiStyle::ScaleAllSizes`, which truncates to whole pixels). Line widths are the exception: a border or line that
is set (`WindowBorderSize`, `ChildBorderSize`, `PopupBorderSize`, `FrameBorderSize`, `TabBorderSize`,
`TabBarBorderSize`, `TabBarOverlineSize`, `SeparatorTextBorderSize`, and ImGui's `SeparatorSize`, `ImageBorderSize`,
`DragDropTargetBorderSize`, `TreeLinesSize`, `InputTextCursorSize`) becomes `max(1, round(width × scale))`, as the
skins round their strokes, so a 1dp border is 1px at 50% or 90% zoom (truncated, it was 0 and ImGui drew none) and
2px at 150%, like a skin's; a width of 0 stays 0.

`theme()` returns the generated scheme. Related functions: `scheme()`, `schemeFor(t)`, `themeState{}` and
`color.*`. See [Lua API](lua-api.md#theme).

## Designs

`design` picks the **skin**: the C++ object that draws every control (buttons, toggles, fields, menus, cards,
dialogs, tooltips, ...), reports its natural size, and gives the chrome some elements draw themselves (window and bar
backgrounds, table rules). imlui has two, `"material"` (Material 3, the default) and `"fluent"` (Fluent 2, the
Windows 11 look). An app adds its own in C++ with `registerSkin(name, factory)` (see
[Writing a skin](extending.md#skins)), and a script picks it by name:

```lua
theme { design = "fluent", dark = false, seed = "#0067C0" }
```

A skin decides how controls look and how big they are; the rest of the theme stays what it is under every design:
the color scheme and its roles (a skin draws with them), `shape`, `spacing`, `fontScale`, the fonts and the ImGui
`preset`. The design switches with the theme, at the start of the next frame, and layouts follow the new sizes on
that frame. `themeState` works too (`themeState { design = "material" }`).

### The Fluent design

`design = "fluent"` draws with the [tokens](#tokens) (Fluent's defaults, plus the theme's `tokens`) and with the
[Fluent colors](#fluent-colors), derived from the same scheme roles Material uses: `primary` is the accent, so
`seed` and `colors = { ... }` restyle it too. Control labels are in Fluent's body size (`bodyMedium`, 14dp).

**Fonts and icons.** On Windows, the Fluent design draws text with **Segoe UI Variable** (Regular) and **Segoe UI
Semibold** (the Medium weight: titles, labels) and icons with **Segoe Fluent Icons** (Windows 10's Segoe MDL2 Assets
where it is missing), read from the system's font folder: they come with Windows and can't be bundled. A face the
theme sets in `fonts` wins over Segoe. An icon name with a Segoe counterpart (`home`, `settings`, `search`, `add`,
`close`, `check`, `expand_more`, `delete`, `edit`, about 70 of the common ones) draws Segoe's glyph; every other name
stays Material Icons. Where the Segoe files are missing (another OS, an older Windows), Fluent draws with the theme's
fonts (Roboto by default) and Material Icons. Material always uses the theme's fonts and Material Icons.

Every control is Fluent-drawn:

| What | Fluent look |
|---|---|
| `Button` | 32dp high (`controlHeight`), `paddingX` on each side, 4dp corners (`controlRadius`). `filled` and `tonal` are Fluent's **accent** button (an accent outline with `tokens = { buttonFill = "outline" }`), `outlined` and `elevated` the **standard** button (a subtle neutral fill, a 1dp stroke with a darker bottom edge), `text` the **subtle** button (transparent at rest), `fab` an accent button 16dp taller with the overlay radius (square without a label). Hover and press change the fill (no state layer); disabled uses Fluent's disabled fills. Keyboard focus (Tab, arrows; Space or Enter clicks) shows Fluent's focus rectangle: a `focusStrokeWidth` outer stroke `focusOffset` outside the button and a thin inner one. A mouse click shows none. |
| `IconButton` | A `controlHeight` square (32dp; Material's is a 48dp touch target) with the control radius and an `iconSize` icon. `standard` is a **subtle** button (a checked toggle keeps the subtle fill with an accent icon), `filled` an **accent** one (an unchecked toggle: standard, like an unchecked ToggleButton), `tonal` and `outlined` **standard** ones (a checked toggle: accent). `color` tints the icon unless disabled. |
| `Checkbox` | WinUI's CheckBox: a 20dp box with the control radius, unchecked an alt fill (`controlAlt*`) with a 1dp `controlStrongStroke` outline, checked an accent fill (hover, pressed, disabled fills) with a check on accent; the check fades in. The label follows after `gap`, in body text. `controlHeight` high (at least the box); the whole item is the hit area and the focus rectangle's frame. |
| `RadioButton`, `RadioGroup` | WinUI's RadioButton: a 20dp circle, unchecked like the checkbox, checked an accent circle with a dot on accent of 12dp (14dp on hover, 10dp pressed; it grows in). Sizes as the checkbox. |
| `Switch` | WinUI's ToggleSwitch: a 40 x 20dp pill, off an alt fill with a `controlStrongStroke` outline and a `textSecondary` knob, on an accent fill with a knob on accent. The knob (12dp, 14dp on hover, stretched to 17dp while pressed) slides between the ends. `icons` is ignored (Fluent's knob has no icon). The label after `gap`; `controlHeight` high. |
| `Chip` | No chip in WinUI: drawn as its closest control, a **ToggleButton** (Fluent 2's interaction tag looks the same). `filter`, `suggestion` and `input`: unselected a standard button, selected an accent one (following `buttonFill`); `assist`: always standard. A selected filter chip shows no check (the fill shows the selection); a leading `icon` stays. `controlHeight` high, `paddingX` + icon + `gap` + label + `paddingX` wide. |
| `SegmentedButton` | The Community Toolkit's Segmented (WinUI's SelectorBar look): a standard control's frame (an alt fill, a 1dp stroke, the control radius) holding equal segments 2dp inside it; the segments are subtle, the selected one a raised standard control with a 16 x 3dp accent pill at its bottom (the selection fades between segments). No check on the selected segment; `icons` show on every segment. `controlHeight` high; each segment `paddingX` + room for an icon + `gap` + the widest label + `paddingX`; `width` sizes the whole. |
| `Slider` | WinUI's Slider: a 4dp track (`controlStrongStroke`) with the value part in accent, a 20dp thumb (the opaque solid control fill, a 1dp stroke) carrying an accent dot of 12dp (14dp on hover, 10dp while dragging), 1dp tick marks under the track (at most 40 steps), and the value in a flyout-colored tooltip above the thumb while dragging. `controlHeight` high (at least the thumb), 200dp natural width (unset: fills). Keyboard focus shows the focus rectangle; while focused, Left/Right step the value (by `stepSize`, or a hundredth of the range when continuous), PageDown/PageUp by a tenth of the range, Home/End go to `from`/`to` (WinUI's keys; Up/Down and Tab move the focus on); a keyboard activation leaves the value alone (only the mouse drags). |
| `ProgressBar` | WinUI's ProgressBar: a 3dp accent bar with round ends over a 1dp `controlStrongStroke` track, 3dp high, 240dp natural width; indeterminate, two accent bars sweep across. |
| `CircularProgress`, `BusyIndicator` | WinUI's ProgressRing: an accent arc `thickness` dp wide from the top, clockwise, with no track (the ring's background is transparent); 32dp across unless `size` sets it. Indeterminate, a spinning arc that grows and shrinks. |
| `TextField`, `TextArea` | Fluent's text box. The `label` is a header above the box (body text, `gap` below it), the box `controlHeight` high (a `TextArea`'s `height`, 120dp by default, is the header and the box together), `supportingText` a description 4dp below it in the secondary text color (`error`: the scheme's `error` role). The box: the control fill (hover: the hover fill; while editing: `controlInputActive`), a `strokeWidth` border whose bottom edge is `controlStrongStroke`, `controlRadius` corners; while editing an accent underline `inputUnderlineWidth` thick at the bottom (`tokens = { inputUnderline = false }`: an accent border instead, and a `controlBottomStroke` bottom edge); `error` makes the bottom edge (and the underline) the error color. Text in body text, `paddingX` in, the placeholder in the secondary text color (shown whenever the text is empty, as the label is outside). `leadingIcon` at `iconSize` inside the left padding; `trailingIcon` (or a password's reveal toggle) a subtle inner button 30dp wide, 4dp inside the box's right edge, not a tab stop. Natural width 280dp, as Material's. `variant` doesn't change the look. Arrow-key focus without editing shows the focus rectangle; editing shows the underline. |
| `SpinBox` | Fluent's number box with inline spin buttons: the text box (header, box, underline as above) with two subtle Fluent buttons inside its right edge, up (increment) then down (decrement), each 30dp wide and `controlHeight` - 8dp high, disabled at the range's end. Natural width 200dp. |
| `ComboBox` | Fluent's combo box: a standard control (the standard button's fills and stroke, pressed while open), the header and description as for the text box, the selected item in body text after `paddingX` (and the `leadingIcon`), a chevron at the right. It opens a flyout 4dp below (the `flyout` fill, a `flyoutStroke` border, `overlayRadius` corners and a shadow, 2dp padding, at least the control's width, scrolling past 8 rows; the menus' flyout surface): rows `controlHeight` high with a subtle highlight inset 4dp x 2dp (`controlRadius`), icons from `icons`, and on the selected row a subtle fill and a 3 x 16dp accent pill at its left. Keyboard: the control and the rows are navigable and show the focus rectangle. Natural width 240dp. |
| `ColorPicker` | A Fluent drop-down button: a standard control with a swatch (`controlHeight` - 12dp square, half the control radius, a `controlStrongStroke` border), the hex value and a chevron, the `label` as a header. It opens a flyout (as the combo box's, 16dp padding) with ImGui's color picker (240dp, in the Fluent ImGui style) and a Fluent text box for the hex value. |
| `Card` | WinUI's card: `elevated` (default) is the translucent `card` fill with a 1dp `cardStroke` (no shadow: Fluent marks elevation with the stroke), `filled` the `layer` fill without a stroke, `outlined` no fill of its own (the surface it is on) with a `divider` stroke. The overlay radius (`overlayRadius`, 8dp) unless `rounding` is set; `color` replaces the fill. The sizes are Material's (320dp natural width, the content plus `padding` on both sides): the tokens change the look, not the size. The fill it pushes as the surface is opaque (the card fill painted over what it is on). |
| `Dialog` | WinUI's ContentDialog: the `smoke` dim behind it, the `layer` fill, a `flyoutStroke` border of the stroke width, the overlay radius, 24dp padding, 320-548dp wide (or `width`), the title in `titleLarge` 12dp above the content. An `icon` is centered above the title in the accent. Its buttons are its `Button` children, so Fluent buttons. Opening, closing (Escape closes the top-most popup only) and the shrink-to-fit content are Material's. |
| Snackbar (`ui.snackbar`) | WinUI's informational InfoBar, floating at the bottom: the opaque `flyout` fill with a `flyoutStroke` border, the control radius, an accent `info` icon, the message in body text, the action a standard Fluent button, the close icon a `standard` Fluent `IconButton` (subtle, a `controlHeight` square). 48dp high, 20dp more per wrapped line; the width, the wrapping, the slide-in and the fade are Material's. |
| `tooltip` | WinUI's ToolTip: the `flyout` fill with a `flyoutStroke` border, the control radius, 8dp x 6dp padding, `bodySmall` (Fluent's caption) in the primary text color, wrapping at 320dp. |
| `Divider` | A 1dp `divider`-colored line (Material's size and insets). |
| `Table` | `divider`-colored rules, the selected row in the `subtleHover` fill with primary text, `paddingX` cell padding, a header as high as a control (`controlHeight`); header cells in `titleSmall` (Fluent's body strong) in the secondary text color, body cells in body text; an `outlined` table sits in an outlined Fluent card. |
| `Plot` | Material's chart (axis, sizes, gaps, hover) with the series in the accent (or `color`), divider-colored gridlines over a `controlStrongStroke` base line, axis labels in the secondary text color, bars with the control radius at the top, and a flyout for the hover readout. |
| `TopAppBar` | WinUI's title/command bar: a 48dp top row (`small`, `center`); `medium` and `large` are 104dp and 128dp with the title below the row. The current surface's color, the title in the primary text color. |
| `Separator`, `Link` | A separator's label in the secondary text color; a link in the accent text color (WinUI's HyperlinkButton). |
| `ApplicationWindow` | Fluent's solid background (unless `imgui = { WindowBg = ... }` sets it). |
| `Window` | A layer over the background, a 1dp divider-colored border, title bars of the background color. |
| `ScrollView` | The overlay radius (8dp) when it has no `rounding`. |
| `ListItem` | WinUI's list row: `controlHeight` + 8dp high (40dp; + 24dp, 56dp, with `supportingText`; the extras scale with `density`), 320dp natural width. A subtle item: transparent at rest, the subtle hover and press fills on a highlight 4dp inside the row's sides and 2dp inside its top and bottom, `controlRadius` corners. `selected`: the subtle fill one step on (`subtleHover`, hovered `subtlePressed`) and the **selection indicator**, a 3dp x 16dp `accent` pill at the highlight's left edge (10dp while pressed). Body text (14dp), the supporting line and `trailingText` in the caption style (12dp) and `textSecondary`, `iconSize` icons. Keyboard focus shows the focus rectangle. |
| `Section` (`Expander`) | WinUI's Expander header: a card (`card` fill, `cardStroke` border, `controlRadius`), `controlHeight` + 16dp high (48dp; + 32dp, 64dp, with `supportingText`; scaled by `density`), 320dp natural width; body title, caption supporting text; the chevron on the right in a `controlHeight` square that takes the subtle hover and press fills. |
| `Menu`, `MenuItem`, a `menu` prop | WinUI's MenuFlyout: the `flyout` color, a `flyoutStroke` border (`strokeWidth`), `overlayRadius` corners, 2dp above and below the rows, a shadow under a dropdown (a `menu` prop's). Rows `compactHeight` + 8dp high (32dp, the 8dp scaled by `density`), at least 160dp wide, with the subtle fills on a highlight inside the row (4dp, 2dp); `paddingX`, the icon slot (`iconSize` + `gap`), the body label, the shortcut in the caption style and `textSecondary`, right-aligned. A dropdown shows 8 rows before it scrolls. Opening, choosing, a checkable item keeping the menu open: as under Material. |
| `TabBar` | `controlHeight` + 8dp high (40dp) for both variants; the tabs share the width (natural: each as wide as the widest tab's icon and label plus `paddingX` on both sides). `primary` is WinUI's **SelectorBar**: subtle tabs (hover and press fills inside the tab), the icon beside the label (never above it), `textSecondary` on unselected tabs, and the selection indicator (a 16dp x 3dp `accent` pill) under the selected one, sliding to the new tab. `secondary` is WinUI's **TabView**: the selected tab is a `layer` tab with `overlayRadius` top corners standing on a `divider` line along the bar's bottom that stops under it; the others are subtle. |
| `NavigationItem` | WinUI's NavigationViewItem: a subtle item with the selection indicator at its left edge (the `ListItem`'s fills). As a drawer item (outside a bar or rail, or in a `ModalNavigationDrawer`): `controlHeight` + 8dp high (40dp), 280dp natural width, the icon (`iconSize` + 4dp, 20dp) and the body label; `badge` is an InfoBadge at the right (an `accent` pill with `onAccent` text, a dot for `"."`). In a `NavigationRail`: `controlHeight` + 24dp high (56dp), the icon over a caption label. In a `NavigationBar`: 80dp high (the bar's; no `density`), the icon over a caption label, selected in `accentText` with the indicator under the label. A bar or rail `badge` is an InfoBadge on the icon. |
| `NavigationBar`, `NavigationRail` | The bar on `layer`, the rail on `background` (NavigationView's pane over Mica), unless they have a `color`. The bar is as high as its destinations (80dp). |
| `Image` (broken) | A missing or undecodable image's placeholder: the standard control fill with its stroke and a broken-image icon in the secondary text color. |
| `ModalNavigationDrawer` | WinUI's NavigationView pane in overlay mode: `smoke` over the window, the pane in `flyout` with a `flyoutStroke` right edge, `overlayRadius` right corners and a shadow, sliding in from the left; its content 4dp from the pane's edges (scaled by `density`). Escape and a click on the smoke close it, as under Material. |
| ImGui's own style | With the `"material"` preset: `FrameRounding`, `TabRounding` and `GrabRounding` = `controlRadius`; `WindowRounding`, `ChildRounding` and `PopupRounding` = `overlayRadius`; the border sizes = `strokeWidth`; `FramePadding` from `paddingX` and `controlHeight` (an ImGui frame is as high as a Fluent control); `ItemInnerSpacing.x` = `gap`; the colors from the Fluent colors (`WindowBg` = background, `PopupBg` = flyout, `FrameBg` = control fill, `Header*` = subtle fills, `Separator` and table borders = divider, `TextLink` = accent text, `NavCursor` = focus stroke, ...). So raw ImGui (scrollbars, popups, `InputText`, `TreeNode`, menu bars, `ImGuiDemo`) matches. `style` and `imgui` overrides still apply after it; the `imgui-*` presets are untouched. |

### Tokens

Tokens are a flat design's metrics. Each design has defaults; `tokens = { ... }` overrides some of them by name
(lengths in dp, scaled by the DPI factor and zoom like everything else):

```lua
theme { design = "fluent", tokens = { controlHeight = 40, controlRadius = 8, density = 0.9 } }
```

| Token | Type | Range | Fluent | Material | Meaning |
|---|---|---|---|---|---|
| `controlHeight` | number | 8..200 | 32 | 40 | A standard control's height: buttons, text fields, combo boxes. |
| `compactHeight` | number | 8..200 | 24 | 32 | A compact control's height: menu rows, small buttons. |
| `controlRadius` | number | 0..100 | 4 | 20 | The corner radius of controls. |
| `overlayRadius` | number | 0..100 | 8 | 28 | The corner radius of overlays and containers: cards, flyouts, dialogs. |
| `strokeWidth` | number | 0..8 | 1 | 1 | A control's border width; 0: none. |
| `focusStrokeWidth` | number | 0..8 | 2 | 2 | The keyboard focus rectangle's outer stroke; 0: none. |
| `focusOffset` | number | 0..16 | 3 | 2 | How far the focus rectangle lies outside the control. |
| `inputUnderline` | boolean | | `true` | `true` | Inputs carry an accent underline at their bottom edge when focused. |
| `inputUnderlineWidth` | number | 0..8 | 2 | 2 | The focused underline's width (1dp at rest). |
| `buttonFill` | `"filled"` or `"outline"` | | `"filled"` | `"filled"` | How accent buttons (`filled`, `tonal`) are drawn: an accent fill, or an accent outline with accent text. |
| `paddingX` | number | 0..100 | 12 | 24 | The horizontal padding inside controls. |
| `gap` | number | 0..100 | 8 | 8 | The gap between a control's parts: icon and label, label and glyph. |
| `iconSize` | number | 8..64 | 16 | 18 | The size of icons inside controls. |
| `density` | number | 0.5..2 | 1 | 1 | Multiplies heights, paddings and gaps (not radii or strokes): below 1 compact, above 1 roomy. |

Under Fluent every token takes effect. Under Material they are **informational**: the Material widgets keep
Material 3's own metrics, and `skin().tokens()` reports them (the Material column) whatever the theme overrides.
A wrong token reports an error and is ignored, the token keeping its default: `theme.tokens: unknown token 'x'
(known: controlHeight, ...)`, `theme.tokens.controlHeight: expected a number in 8..200`,
`theme.tokens.inputUnderline: expected a boolean`, `theme.tokens.buttonFill: expected "filled" or "outline"`. A
`Theme` built in C++ is held to the same rules by `setTheme`, which drops an override with a problem
(`tokenProblem`).

### Fluent colors

The Fluent skin paints with Fluent 2's brushes, derived from the scheme (`imlui::fluent::colorsFor(scheme, dark)`,
module `imlui.theme.fluent_colors`). Fluent's neutral brushes are white or black at an opacity; here **white** and
**black** are roles, so they carry the scheme's tint and follow `colors = { ... }`: in a dark scheme white is
`onSurface` and black `surfaceContainerLowest`, in a light one the other way round. The opacities are Fluent's
(rounded here). Translucent colors are painted over whatever is below.

| Fluent color | Dark | Light | Used for |
|---|---|---|---|
| `accent` / `accentHover` / `accentPressed` | `primary` / at 90% / at 80% | the same | accent buttons, checked toggles, sliders, the focused underline, the selection indicator, InfoBadges |
| `accentDisabled` | white 16% | black 22% | a disabled accent fill |
| `onAccent` | `onPrimary` | `onPrimary` | text and icons on accent (pressed: at 70%) |
| `onAccentDisabled` | white 53% | white | text on a disabled accent fill |
| `accentStroke` / `accentBottomStroke` | white 8% / black 14% | white 8% / black 40% | an accent control's border and its darker bottom edge |
| `accentText` | `primary` | `primary` | hyperlinks, an outline accent button, a selected navigation bar item |
| `control` / `controlHover` / `controlPressed` / `controlDisabled` | white 6% / 8% / 3% / 4% | white 70% / 50% / 30% / 30% | standard controls |
| `controlInputActive` | black 70% | white | a text input while editing |
| `controlStroke` / `controlBottomStroke` | white 7% / white 9% | black 6% / black 16% | a standard control's border and bottom edge |
| `controlStrongStroke` / `controlStrongStrokeDisabled` | white 54% / 16% | black 45% / 22% | unchecked checkbox and radio outlines, slider tracks, scrollbar thumbs, a text input's bottom edge |
| `controlAlt` / `controlAltHover` / `controlAltPressed` | black 10% / white 4% / white 7% | black 2% / 6% / 9% | an unchecked checkbox's or radio's fill |
| `subtle` / `subtleHover` / `subtlePressed` | transparent / white 6% / white 4% | transparent / black 4% / black 2% | text buttons, list rows, menu items, tabs, navigation items (selected: `subtleHover`, hovered `subtlePressed`) |
| `text` / `textSecondary` / `textTertiary` / `textDisabled` | white / 79% / 54% / 36% | black 90% / 61% / 45% / 36% | text |
| `background` | `surfaceContainer` | `surfaceContainerLow` | the window background (Mica's solid fallback), `NavigationRail` |
| `layer` | `background` 60% toward `surfaceContainerHigh` | `background` 50% toward white | content layers, `Window`, `Dialog`, a `filled` card, `NavigationBar`, the selected `secondary` tab |
| `card` / `cardStroke` | white 5% / black 10% | white 70% / black 6% | cards, expander headers |
| `flyout` / `flyoutStroke` | `surfaceContainerHigh` / black 20% | `background` 60% toward white / black 6% | menus, popups, tooltips, combo box and color flyouts, the modal navigation drawer, the snackbar, a chart's readout; `flyoutStroke` also a dialog's border |
| `smoke` | black 30% | black 30% | the dim behind a modal (`Dialog`) and behind the modal navigation drawer |
| `divider` | white 8% | black 8% | dividers, table rules, chart gridlines, an `outlined` card's stroke |
| `focusOuter` / `focusInner` | `text` / black 70% | `text` / white 70% | the keyboard focus rectangle |

Some brushes the controls need are derived from these, by `imlui.skin.fluent.common`: Fluent's opaque
**ControlSolidFillColorDefault** (`solidControlFill`, a slider's thumb) is the background 18% toward the text in a
dark scheme (about `#454545`) and `controlInputActive` (white) in a light one; a toggle's accent fill
(`accentToggleFill`) is `accent` / `accentHover` / `accentPressed` / `accentDisabled` whatever `buttonFill` says, with
`onAccent` (disabled: `onAccentDisabled`) for its check, dot or knob; an unchecked toggle's fill (`altFill`) is
`controlAlt` / `controlAltHover` / `controlAltPressed` (disabled: transparent) inside a `controlStrongStroke` outline
(`strongStroke`; disabled: `controlStrongStrokeDisabled`).

Fluent's critical color (input validation: an `error` text field, combo box or description) isn't one of these: it is
the scheme's `error` role.

## Scheme generation

`generateScheme(seed, dark)` (`imlui.theme.scheme`) works in **OKLCH**. The seed is converted to OKLCH, and six
tonal palettes, each a hue and a chroma, are derived from it (the last few schemes generated are cached, so making
`Theme`s, which generate their default scheme, costs little):

| Palette | Hue | Chroma |
|---|---|---|
| primary | seed hue | seed chroma clamped to 0.12–0.22 |
| secondary | seed hue | 0.045 |
| tertiary | seed hue + 60° | 0.07 |
| neutral | seed hue | 0.008 |
| neutral variant | seed hue | 0.018 |
| error | 25° | 0.19 |

A grey seed (OKLCH chroma below `1e-4`, where its hue is only rounding noise) has no hue to give: every palette but
error gets chroma 0, so the scheme is grey apart from the error roles.

`TonalPalette::tone(t)` maps the Material tone `t` (CIE L\*, 0 = black, 100 = white) to OKLab lightness. It is
exact for greys. The color is then converted back to sRGB, with chroma reduced until it fits the gamut.

### Tones used

| Role | Palette | Light | Dark |
|---|---|---|---|
| `primary` / `secondary` / `tertiary` / `error` | own | 40 | 80 |
| `onPrimary` / `onSecondary` / `onTertiary` / `onError` | own | 100 | 20 |
| `*Container` | own | 90 | 30 |
| `on*Container` | own | 10 | 90 |
| `surface` | neutral | 98 | 6 |
| `onSurface` | neutral | 10 | 90 |
| `surfaceVariant` | neutral variant | 90 | 30 |
| `onSurfaceVariant` | neutral variant | 30 | 80 |
| `surfaceDim` | neutral | 87 | 6 |
| `surfaceBright` | neutral | 98 | 24 |
| `surfaceContainerLowest` | neutral | 100 | 4 |
| `surfaceContainerLow` | neutral | 96 | 10 |
| `surfaceContainer` | neutral | 94 | 12 |
| `surfaceContainerHigh` | neutral | 92 | 17 |
| `surfaceContainerHighest` | neutral | 90 | 22 |
| `outline` | neutral variant | 50 | 60 |
| `outlineVariant` | neutral variant | 80 | 30 |
| `inverseSurface` | neutral | 20 | 90 |
| `inverseOnSurface` | neutral | 95 | 20 |
| `inversePrimary` | primary | 80 | 40 |
| `scrim`, `shadow` | — | black | black |

## Color roles

All 34 roles, usable anywhere a color is accepted and in `theme{ colors = ... }`:

`primary`, `onPrimary`, `primaryContainer`, `onPrimaryContainer`,
`secondary`, `onSecondary`, `secondaryContainer`, `onSecondaryContainer`,
`tertiary`, `onTertiary`, `tertiaryContainer`, `onTertiaryContainer`,
`error`, `onError`, `errorContainer`, `onErrorContainer`,
`surface`, `onSurface`, `surfaceVariant`, `onSurfaceVariant`,
`surfaceDim`, `surfaceBright`,
`surfaceContainerLowest`, `surfaceContainerLow`, `surfaceContainer`, `surfaceContainerHigh`, `surfaceContainerHighest`,
`outline`, `outlineVariant`,
`inverseSurface`, `inverseOnSurface`, `inversePrimary`,
`scrim`, `shadow`.

With the `material` preset (and design `"material"`), ImGui's own colors are mapped from the roles: `WindowBg` = `surface`,
`Text` = `onSurface`, `PopupBg` = `surfaceContainer`, `FrameBg` = `surfaceContainerHighest`,
`Border` and `Separator` = `outlineVariant`, `TextLink` = `primary`, and so on. That way raw ImGui widgets, such
as those in `ImGuiDemo`, match the theme. Under design `"fluent"` they map from the
[Fluent colors](#fluent-colors) instead.

## Color specs

Element color props (`color = ...`), Canvas painter colors and `color.*` functions accept:

| Spec | Example |
|---|---|
| role name | `"primary"` |
| role with alpha (a number in 0..1; anything else, such as `"onSurface/O.6"` or `"primary/2"`, is an invalid color) | `"onSurface/0.6"` |
| hex: `#RGB`, `#RGBA`, `#RRGGBB` or `#RRGGBBAA` (`#` optional) | `"#FF5722"`, `"#00000080"` |
| table `{r, g, b[, a]}` in 0..1 (clamped to it; NaN reads as 0) | `{ 1, 0.34, 0.13, 1 }` |

Roles are resolved against the scheme **currently in effect**, so `color = "primary"` follows theme changes. The
theme table itself (`seed`, `colors`, `imgui`) accepts the same specs, but there `colors` and `imgui` roles mean the
scheme generated from that table's seed, so `colors = { surface = "surfaceContainerLow" }` uses the new scheme's
`surfaceContainerLow`. `seed` roles come from the theme in effect before the call.

## Typography

The Material 3 type scale, with Roboto **Regular** and **Medium**. Use these names for `Text { style = ... }`,
`textSize(text, style)` and `p:text(..., style)`. The weights' faces can be replaced, and other fonts added, from
the theme (see [Fonts](#fonts)).

| Style | Size (dp) | Weight |
|---|---|---|
| `displayLarge` | 57 | Regular |
| `displayMedium` | 45 | Regular |
| `displaySmall` | 36 | Regular |
| `headlineLarge` | 32 | Regular |
| `headlineMedium` | 28 | Regular |
| `headlineSmall` | 24 | Regular |
| `titleLarge` | 22 | Regular |
| `titleMedium` | 16 | Medium |
| `titleSmall` | 14 | Medium |
| `bodyLarge` | 16 | Regular |
| `bodyMedium` | 14 | Regular |
| `bodySmall` | 12 | Regular |
| `labelLarge` | 14 | Medium |
| `labelMedium` | 12 | Medium |
| `labelSmall` | 11 | Medium |

Final pixel size = size × `fontScale` × DPI scale, at most 512 pixels (`maxTextPixels`, ImGui's own limit for a
pushed font: text measured or drawn with an explicit size keeps to it too). The limit keeps the largest glyph inside
ImGui's 8192-pixel font atlas: a font's glyphs may be at most 8 font heights wide or tall (`checkFont` rejects a font
with a larger one, see [Fonts](#fonts)), so one is at most 4096 pixels, where a glyph larger than the atlas would
abort the program ("Out of texture memory"). Very large text with many distinct glyphs in one frame can still fill
the atlas; ImGui frees sizes no longer drawn. ImGui 1.92 rasterizes glyphs on demand at each size, so one
font file per weight covers everything. If a Roboto file is missing, a warning is logged and ImGui's default font
is used. A theme can replace both faces with the app's own typeface (see [Fonts](#fonts)).

## Fonts

`theme { fonts = { ... } }` declares the app's text fonts by [resource name](lua-api.md#resources) (TrueType or
OpenType files): a full name such as `"app:/fonts/Inter-Regular.ttf"`, or a name relative to the folder of the script
that declares the theme, like `ui.resource.url` and `Image { source }` (`"fonts/Inter-Regular.ttf"` in
`app:/main.lua` is `app:/fonts/Inter-Regular.ttf`; in `app:/pages/settings.lua` it is
`app:/pages/fonts/Inter-Regular.ttf`). A `themeState` resolves against the script that created it, every time it
re-applies.

```lua
theme {
    fonts = {
        regular  = "app:/fonts/Inter-Regular.ttf",  -- the Regular face of the type scale
        medium   = "app:/fonts/Inter-Medium.ttf",   -- the Medium face (titles, labels, buttons)
        fallback = { "app:/fonts/NotoSansJP-Regular.otf" }, -- or one name as a string
        mono     = "app:/fonts/JetBrainsMono-Regular.ttf",  -- any other key: a named font
    },
}
```

| Key | Meaning |
|---|---|
| `regular`, `medium` | Replace Roboto Regular / Medium for every text style of that weight, app-wide. Unset: Roboto. |
| `fallback` | Fonts merged, in order, into every text font (the type-scale faces, built-in or not, and the named fonts), for the glyphs it lacks: other scripts, symbols. The icon font is separate and unaffected. |
| any other key | A **named font**, drawn by elements with `font = "<key>"` (`Text`, `Label`, `Button`), by `p:text(..., style, font)` in a Canvas and measured by `textSize(text, style, font)`, at the size of their text style. `"regular"` and `"medium"` name the type-scale faces. |

- **Loading.** `theme{}` loads the fonts at once (so problems are reported while the script runs) and switches to
  them with the rest of the theme at the start of the next frame. Fonts are size-independent in ImGui 1.92, so one
  file per face covers every style, `fontScale` and DPI. The font data is read through Resources without a copy,
  so fonts embedded in the executable work the same as files.
- **Errors.** A missing file, a file that isn't a font, a damaged font, a name that leaves its prefix's root
  (`../../x.ttf`) or a value that isn't a string is reported in the error overlay
  (`theme.fonts.regular: font 'app:/fonts/Inter.ttf' not found`,
  `theme.fonts.mono: font 'app:/fonts/Mono.ttf' is damaged: table 'glyf' lies outside the file`). The type-scale
  face then stays Roboto, a named font draws in its style's face, and a missing fallback is left out. Fonts are
  checked before they reach ImGui (a truncated or garbled file would otherwise abort the program inside its font
  parser); see `imlui.theme.font_check` in [Architecture](architecture.md#resources).
- **System fonts.** A font installed with the OS is named `system:/fonts/<file>` (`"system:/fonts/segoeui.ttf"`,
  `"system:/fonts/consola.ttf"`): a file in the OS's font folder (on Windows the one the shell reports,
  `C:\Windows\Fonts`), spelled as on disk. There is none on other systems, where such a name is reported as not
  found. The Fluent design uses this for Segoe UI Variable (see [The Fluent design](#the-fluent-design)).
- **Unknown names.** An element whose `font` names no declared font draws in its style's face; the name is logged
  once.
- **Reuse and hot reload.** Each fallback file is read once per set applied, for all its faces. Each face (with its
  fallbacks) becomes one atlas font, reused while its file is
  unchanged: re-applying a theme, `themeState` changes and script reloads don't add fonts. The fonts of the set in
  use and of the one before it are kept (switching back and forth between two themes reuses both); a font neither
  needs leaves the atlas, with its bytes, when another theme is applied, so going through many themes doesn't grow
  the atlas. The declared font files
  are watched in dev builds: editing one reloads the script and replaces its atlas font (the old one is removed,
  and with it the old file contents: the library holds a font's bytes exactly as long as its atlas font).

## Icons

Icons come from **Material Icons Round** (`external/fonts/MaterialIconsRound-Regular.otf`). Names are the ones
listed in `MaterialIconsRound-Regular.codepoints`, about 2,200 of them, for example `"add"`, `"home"`,
`"delete_outline"` or `"favorite_border"`. You can browse them at <https://fonts.google.com/icons> (Round style).
An unknown name draws nothing. Under the Fluent design on Windows, the names with a Segoe Fluent Icons counterpart
draw that glyph instead (see [The Fluent design](#the-fluent-design)).

Icon props: `icon` on `Button`, `IconButton`, `Chip`, `ListItem`, `NavigationItem`, `MenuItem`, `Tab`, `Dialog`;
`name` on `Icon`; `leadingIcon`/`trailingIcon` on `TextField`/`TextArea` (`leadingIcon` also on `ComboBox`);
`checkedIcon` on `IconButton`; `navigationIcon` on `TopAppBar`; `trailingIcon` on `ListItem`; `icons` lists on
`SegmentedButton` and `ComboBox`; `p:icon` in Canvas.

## States: hover, press, disabled

These are Material's. The Fluent skin changes fills instead of drawing state layers, uses Fluent's disabled colors,
and shows a focus rectangle for keyboard focus (see [The Fluent design](#the-fluent-design)).

- **State layers.** Interactive widgets draw an animated overlay in their content color: **8%** on hover and
  **10%** while pressed. It fades in and out per widget ID.
- **Disabled.** `enabled = false` wraps the element in `ImGui::BeginDisabled()`. Widgets detect this and use the
  Material disabled colors instead of ImGui's global alpha: **containers at 12%** and **content at 38%** of
  `onSurface`. Elevation shadows are dropped. With the `material` preset, ImGui's own `DisabledAlpha` is also set
  to 0.38 for raw ImGui widgets.
- **Surfaces.** Cards, dialogs, colored `ScrollView`s and windows push their background as the "current surface".
  Widgets that need a cutout (for example, the `TopAppBar` default background) read it.
- **Keyboard focus.** Every control is reachable with Tab and the arrow keys, and Space or Enter activates it (a
  focused `Slider` steps with Left/Right, PageUp/PageDown and Home/End). The focused control shows Material's focus
  indicator, a 3dp ring in `secondary` 2dp outside it, while the focus came from the keyboard (not after a click);
  text fields and menu items show ImGui's nav rectangle in the same color.
- **Focus and selection.** Text fields switch to `primary` (or `error`) for the indicator, label and caret.
  Selected list, navigation and segmented items use `secondaryContainer` / `onSecondaryContainer`.
