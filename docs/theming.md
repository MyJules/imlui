# Theming

The theme is plain data, decided in Lua. `theme{...}` builds an `imlui::Theme`, which holds a Material 3 color
scheme generated from one seed color, a shape scale, spacing and font scale. It can also override any color role,
any raw `ImGuiCol_*` color and many `ImGuiStyle` fields. The engine applies it **between frames**. Scripts start
from the default theme on every (re)load: seed `#6750A4`, dark, `preset = "material"`.

## The theme table

```lua
theme {
    preset    = "material",   -- or "imgui-dark" | "imgui-light" | "imgui-classic"
    seed      = "#6750A4",    -- any color spec; the scheme is generated from it
    dark      = true,
    spacing   = 8,            -- base spacing unit (dp)
    fontScale = 1.0,
    shape     = { extraSmall = 4, small = 8, medium = 12, large = 16, extraLarge = 28 },
    colors    = { primary = "#FF5722", surface = "surfaceContainerLow" }, -- override any color role
    imgui     = { WindowBg = "#101010" },                      -- override any ImGuiCol_*
    style     = { FrameRounding = 6, ItemSpacing = { 10, 6 } } -- override ImGuiStyle fields
}
```

| Field | Type | Default | Meaning |
|---|---|---|---|
| `preset` | string | `"material"` | `"material"` maps the scheme onto `ImGuiStyle`. `"imgui-dark"`, `"imgui-light"` and `"imgui-classic"` use ImGui's stock styles instead. Material widgets always draw with the scheme. |
| `seed` | color | `"#6750A4"` | Seed color: hex, `{r,g,b,a}`, or a role (`"primary"`, `"tertiary/1"`) of the theme currently in effect. An invalid value reports an error and keeps the default. |
| `dark` | bool | `true` | Dark or light scheme. |
| `spacing` | number | `8` | Base unit for ImGui paddings and spacings. It's also the default gap in `Column` and `Row`. |
| `fontScale` | number | `1.0` | Multiplies every text size. Glyphs are rasterized at the final size, so text stays sharp. |
| `shape` | table | see above | Material shape scale: corner radii in dp. |
| `colors` | `{ role = color }` | — | Overrides generated roles. Values are [color specs](#color-specs); role names refer to the scheme generated from this table's `seed`/`dark` (before any overrides). An unknown role or invalid color reports an error. |
| `imgui` | `{ ImGuiColName = color }` | — | Raw ImGui color overrides by name (`"WindowBg"`, `"Text"`, ...), applied after the preset. Values are [color specs](#color-specs), with roles from this table's generated scheme. An unknown name or invalid color reports an error. |
| `style` | `{ Field = n \| {x, y} }` | — | Raw `ImGuiStyle` overrides, applied after the preset. An unknown field reports an error. |

`style` accepts these fields: `Alpha`, `DisabledAlpha`, `WindowPadding`, `WindowRounding`, `WindowBorderSize`,
`WindowMinSize`, `WindowTitleAlign`, `ChildRounding`, `ChildBorderSize`, `PopupRounding`, `PopupBorderSize`,
`FramePadding`, `FrameRounding`, `FrameBorderSize`, `ItemSpacing`, `ItemInnerSpacing`, `CellPadding`,
`IndentSpacing`, `ScrollbarSize`, `ScrollbarRounding`, `GrabMinSize`, `GrabRounding`, `TabRounding`,
`TabBorderSize`, `TabBarBorderSize`, `TabBarOverlineSize`, `ButtonTextAlign`, `SelectableTextAlign`,
`SeparatorTextBorderSize`, `SeparatorTextAlign`, `SeparatorTextPadding`. A number sets a scalar field, and
`{x, y}` sets a vector field.

After the preset and the overrides, every style size is scaled by the DPI factor
(`ImGuiStyle::ScaleAllSizes`).

`theme()` returns the generated scheme. Related functions: `scheme()`, `schemeFor(t)`, `themeState{}` and
`color.*`. See [Lua API](lua-api.md#theme).

## Scheme generation

`generateScheme(seed, dark)` (`imlui.theme.scheme`) works in **OKLCH**. The seed is converted to OKLCH, and six
tonal palettes, each a hue and a chroma, are derived from it:

| Palette | Hue | Chroma |
|---|---|---|
| primary | seed hue | seed chroma clamped to 0.12–0.22 |
| secondary | seed hue | 0.045 |
| tertiary | seed hue + 60° | 0.07 |
| neutral | seed hue | 0.008 |
| neutral variant | seed hue | 0.018 |
| error | 25° | 0.19 |

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

With the `material` preset, ImGui's own colors are mapped from the roles: `WindowBg` = `surface`,
`Text` = `onSurface`, `PopupBg` = `surfaceContainer`, `FrameBg` = `surfaceContainerHighest`,
`Border` and `Separator` = `outlineVariant`, `TextLink` = `primary`, and so on. That way raw ImGui widgets, such
as those in `ImGuiDemo`, match the theme.

## Color specs

Element color props (`color = ...`), Canvas painter colors and `color.*` functions accept:

| Spec | Example |
|---|---|
| role name | `"primary"` |
| role with alpha | `"onSurface/0.6"` |
| hex: `#RGB`, `#RGBA`, `#RRGGBB` or `#RRGGBBAA` (`#` optional) | `"#FF5722"`, `"#00000080"` |
| table `{r, g, b[, a]}` in 0..1 | `{ 1, 0.34, 0.13, 1 }` |

Roles are resolved against the scheme **currently in effect**, so `color = "primary"` follows theme changes. The
theme table itself (`seed`, `colors`, `imgui`) accepts the same specs, but there `colors` and `imgui` roles mean the
scheme generated from that table's seed, so `colors = { surface = "surfaceContainerLow" }` uses the new scheme's
`surfaceContainerLow`. `seed` roles come from the theme in effect before the call.

## Typography

The Material 3 type scale, with Roboto **Regular** and **Medium**. Use these names for `Text { style = ... }`,
`textSize(text, style)` and `p:text(..., style)`.

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

Final pixel size = size × `fontScale` × DPI scale. ImGui 1.92 rasterizes glyphs on demand at each size, so one
font file per weight covers everything. If a Roboto file is missing, a warning is logged and ImGui's default font
is used.

## Icons

Icons come from **Material Icons Round** (`external/fonts/MaterialIconsRound-Regular.otf`). Names are the ones
listed in `MaterialIconsRound-Regular.codepoints`, about 2,200 of them, for example `"add"`, `"home"`,
`"delete_outline"` or `"favorite_border"`. You can browse them at <https://fonts.google.com/icons> (Round style).
An unknown name draws nothing.

Icon props: `icon` on `Button`, `IconButton`, `Chip`, `ListItem`, `NavigationItem`, `MenuItem`, `Tab`, `Dialog`;
`name` on `Icon`; `leadingIcon`/`trailingIcon` on `TextField`/`TextArea` (`leadingIcon` also on `ComboBox`);
`checkedIcon` on `IconButton`; `navigationIcon` on `TopAppBar`; `trailingIcon` on `ListItem`; `icons` lists on
`SegmentedButton` and `ComboBox`; `p:icon` in Canvas.

## States: hover, press, disabled

- **State layers.** Interactive widgets draw an animated overlay in their content color: **8%** on hover and
  **10%** while pressed. It fades in and out per widget ID.
- **Disabled.** `enabled = false` wraps the element in `ImGui::BeginDisabled()`. Widgets detect this and use the
  Material disabled colors instead of ImGui's global alpha: **containers at 12%** and **content at 38%** of
  `onSurface`. Elevation shadows are dropped. With the `material` preset, ImGui's own `DisabledAlpha` is also set
  to 0.38 for raw ImGui widgets.
- **Surfaces.** Cards, dialogs, colored `ScrollView`s and windows push their background as the "current surface".
  Widgets that need a cutout (for example, the `TopAppBar` default background) read it.
- **Focus and selection.** Text fields switch to `primary` (or `error`) for the indicator, label and caret.
  Selected list, navigation and segmented items use `secondaryContainer` / `onSecondaryContainer`.
