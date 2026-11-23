# Module refactor spec (temporary — delete after the refactor lands)

Target layout for applying `CODE_STYLE.md`. Module names and exported APIs below are **pinned** so work can
proceed in parallel. Keep every behavior identical unless a task says otherwise. Namespaces stay as today
(`imlui`, `imlui::md` for widgets, `imlui::luaui` for the Lua bridge, `imlui::log`, `imlui::platform`).

CMake collects all module files with `file(GLOB_RECURSE ... CONFIGURE_DEPENDS src/*.cppm)` (phase A sets this
up), so adding/removing module files needs no CMake edits.

## Phase A — foundation (`src/wrap`, `src/imlui/color`, `src/imlui/theme`, `src/imlui/lua`)

| Module | File | Exports |
|---|---|---|
| `lua` | `src/wrap/lua.cppm` | Lua C API wrappers (see renames below) |
| `lua.stack_guard` | `src/wrap/lua_stack_guard.cppm` | `class lua::StackGuard` (members `m_state`, `m_top`) |
| `imgui` | `src/wrap/imgui.cppm` | as today; `imgui::fill_width` -> `imgui::fillWidth`, `imgui::array_size` -> `imgui::arraySize` |
| `imlui.log` | `src/wrap/log.cppm` | as today (templates stay in the interface) |
| `imlui.platform.window` | `src/wrap/window.cppm` | `class platform::Window` with nested `struct Window::Config` (was `WindowConfig`) |
| `imlui.color` | `src/imlui/color/color.cppm` | `struct Color` (fields `r,g,b,a`; `withAlpha`, `vec4`, `u32`, `static from`), `mix`, `overlay`, `parseHex`, `toHex` |
| `imlui.color.oklch` | `src/imlui/color/oklch.cppm` | `struct Oklch`, `toOklch`, `fromOklch`, `toneToOkL` |
| `imlui.color.tonal_palette` | `src/imlui/color/tonal_palette.cppm` | `struct TonalPalette { float hue, chroma; Color tone(float) const; }` |
| `imlui.theme.dpi` | `src/imlui/theme/dpi.cppm` | `float dpiScale()`, `void setDpiScale(float)`, `float dp(float)` |
| `imlui.theme.scheme` | `src/imlui/theme/scheme.cppm` | `struct Scheme` (same fields), `struct RoleEntry`, `std::span<const RoleEntry> roles()` (was `kRoles`), `findRole` (both overloads), `generateScheme` |
| `imlui.theme.typography` | `src/imlui/theme/typography.cppm` | `enum class TextStyle`, `enum class Weight`, `struct TextStyleInfo`, `const TextStyleInfo& textStyleInfo(TextStyle)` (was `kTextStyles[i]`), `textStyleFromName`, `loadFonts(dir)` (text fonts only, no dpi arg — dpi lives in `imlui.theme.dpi`), `font`, `fontSize`, `fontPixels` (both overloads), `pushFont` |
| `imlui.theme.icons` | `src/imlui/theme/icons.cppm` | `loadIcons(dir)` (icon font + codepoints), `iconFont`, `iconText`, `hasIcon` |
| `imlui.theme.theme` | `src/imlui/theme/theme.cppm` | `struct Theme` with nested `struct Theme::Shape` and `struct Theme::StyleOverride` (field `shape` of type `Theme::Shape`, `style` vector of `StyleOverride`); `theme()`, `setTheme`, `applyPendingTheme`, `isStyleField`, `imguiColorIndex`, `resolveColor` |
| `imlui.theme.font_scope` | `src/imlui/theme/font_scope.cppm` | `class FontScope` (RAII push/pop of a `TextStyle` font; was `md::FontScope`, now `imlui::FontScope`) |
| `imlui.lua.errors` | `src/imlui/lua/errors.cppm` | `luaui::reportError`, `errors`, `clearErrors`, `tracebackHandler`, `safeCall` |
| `imlui.lua.value` | `src/imlui/lua/value.cppm` | `luaui::Value`, `pushValue`, `isBind` |
| `imlui.lua.node` | `src/imlui/lua/node.cppm` | `class luaui::Node` — same public methods as today; members `m_state`, `m_index` |
| `imlui.lua.theme_table` | `src/imlui/lua/theme_table.cppm` | `luaui::themeFromTable`, `luaui::pushScheme` |

### `lua` wrapper renames (all callers must follow)

- `lua::type()` returns `enum class lua::Type : int { None = -1, Nil, Boolean, LightUserData, Number, String, Table, Function, UserData, Thread }`.
  The old `lua::TNIL`/`TTABLE`/... constants are removed; compare against `lua::Type::Table` etc. Functions that
  returned a type (`getfield`, `rawgeti`, `gettable`, `rawget`, `getglobal`, `Node::push`, `Node::pushRaw`)
  return `lua::Type` too.
- Constants: `lua::OK` -> `lua::ok`, `MULTRET` -> `multRet`, `REGISTRYINDEX` -> `registryIndex`, `NOREF` -> `noRef`, `REFNIL` -> `refNil`.
- `lua::type_name` -> `lua::typeName`, `lua::tostring_any` -> `lua::toStringAny`. All other function names are unchanged.

### Removed names

The old modules `imlui.theme` (single file), `imlui.luaui`, `imlui.platform` are deleted. Their content moves
as above. During phase A, the not-yet-split files (`widgets.cppm`, `renderer.cppm`, `engine.cppm`,
`imlui.cppm`) are only updated to import the new modules / use the renamed APIs, so the project keeps building.

## Notes from phase A (binding for later phases)

- `Oklch` fields are `l, c, h` (lowercase).
- Name `lua::State*` parameters `state`, not `L` (the clang-tidy naming check rejects `L`).
- No module re-exports another: import the module that defines a type you name (`imlui.theme.scheme` for
  `Scheme`, `imlui.theme.typography` for `TextStyle`, `imlui.color` for `Color`, ...).
- `loadIcons(dir)` must run after `loadFonts(dir)` (loadFonts clears the atlas).
- Check naming with `clang-tidy -p build/clang-debug <files>`. It works with modules here.

## Phase B — widgets (`src/imlui/widgets/`, namespace `imlui::md`)

All widget function names and signatures stay as they are today. Split `widgets.cppm` into:

| Module | Exports |
|---|---|
| `imlui.widgets.common` | shared helpers used by widget modules: `col`, `scheme()` (was `S()`), `shape()`, `itemDisabled`, `disabledContent`, `disabledContainer`, `labelEnd`, `visibleLabel` (was `visible`), `animate`, `stateLayer`, `itemWidth`, `measure`, `drawText`, `drawTextV`, `drawIcon`, `drawShadow`, constants `pi`, `hugeExtent` |
| `imlui.widgets.surface` | `pushSurface`, `popSurface`, `surfaceColor` |
| `imlui.widgets.text` | `text`, `icon`, `tooltip`, `divider` |
| `imlui.widgets.button` | `ButtonVariant`, `buttonVariant`, `button`, `IconButtonVariant`, `iconButtonVariant`, `iconButton` |
| `imlui.widgets.selection` | `checkbox`, `radio`, `switchToggle`, `ChipVariant`, `chipVariant`, `chip`, `segmented` |
| `imlui.widgets.slider` | `slider` |
| `imlui.widgets.progress` | `progress`, `circularProgress` |
| `imlui.widgets.field` | shared text-field-like plumbing: `FieldFrame`, `drawFieldDecor`, `drawFieldBackground`, `drawSupporting`, `scopeDisabled`, `inputResizeCallback` (namespace `imlui::md::field`) |
| `imlui.widgets.text_field` | `TextFieldOptions`, `TextFieldResult`, `textField` |
| `imlui.widgets.menu` | `pushMenuStyle`, `popMenuStyle`, `menuItem`, `menuDivider`, `beginAnchoredMenu`, `endAnchoredMenu` |
| `imlui.widgets.dropdown` | `DropdownOptions`, `dropdown` |
| `imlui.widgets.spin_box` | `spinBox` |
| `imlui.widgets.color_field` | `colorField` |
| `imlui.widgets.tabs` | `TabInfo`, `tabs` |
| `imlui.widgets.navigation` | `navigationItem` |
| `imlui.widgets.list` | `ListItemOptions`, `listItem`, `expander` |
| `imlui.widgets.card` | `CardVariant`, `cardVariant`, `beginCard`, `endCard` |
| `imlui.widgets.dialog` | `beginDialog`, `endDialog` |
| `imlui.widgets.snackbar` | `snackbar` |
| `imlui.widgets.chart` | `ChartOptions`, `chart` |
| `imlui.widgets.table` | `tableHeaderCell`, `tableCell` |
| `imlui.widgets` | umbrella: `export import` of all of the above (file `src/imlui/widgets/widgets.cppm`) |

Bug fix in this phase: anchored menus (`beginAnchoredMenu`, also the dropdown and color-field popups) must stay
inside the main viewport — shift left when they would overflow the right edge (align the menu's right edge with
the anchor's right edge), and open upward when there is no room below. Use last frame's popup size (already
stored for the shadow).

## Phase C — UI & app (`src/imlui/lua`, `src/imlui/ui`, `src/imlui/app`)

| Module | File | Exports |
|---|---|---|
| `imlui.lua.painter` | `src/imlui/lua/painter.cppm` | `luaui::registerPainter(L)`, `luaui::beginPaint(ImDrawList*, ImVec2 origin)`, `luaui::endPaint()` |
| `imlui.lua.native` | `src/imlui/lua/native.cppm` | `struct luaui::NativeHooks { std::function<void()> quit, reload; std::function<void(const std::string&)> setTitle; }`, `luaui::registerNative(lua::State*, NativeHooks*)` — the `imlui.native` Lua library (no dependency on `Engine`) |
| `imlui.ui.props` | `src/imlui/ui/props.cppm` | `sizeProp`, `labelId` (was `label`), `alignFactor` |
| `imlui.ui.renderer` | `src/imlui/ui/renderer.cppm` | `class Renderer` — `registerElement`, `render`, `forEachChild` (template, stays in interface), `renderChildren`, `renderStack`, `isVisual`, `attachMenu`, `windowTitle`, `setWindowTitle`; members `m_elements`, `m_title`. The constructor no longer registers built-ins. |
| `imlui.ui.elements.windows` | `src/imlui/ui/elements/windows.cppm` | `registerWindowElements(Renderer&)` — ApplicationWindow, Window, Dialog |
| `imlui.ui.elements.layout` | `.../layout.cppm` | `registerLayoutElements` — Column, Item, Row, Grid, ScrollView, ListView, Card, TabBar, Tab, Section, Expander, TreeNode, Repeater, If, Loader |
| `imlui.ui.elements.navigation` | `.../navigation.cppm` | `registerNavigationElements` — TopAppBar, NavigationItem, MenuBar, Menu, MenuItem |
| `imlui.ui.elements.content` | `.../content.cppm` | `registerContentElements` — Text, Label, Icon, Link, Separator, Divider, Spacer |
| `imlui.ui.elements.controls` | `.../controls.cppm` | `registerControlElements` — Button, IconButton, Checkbox, Switch, RadioButton, RadioGroup, Chip, SegmentedButton, ListItem |
| `imlui.ui.elements.inputs` | `.../inputs.cppm` | `registerInputElements` — Slider, SpinBox, TextField, TextArea, ComboBox, ColorPicker |
| `imlui.ui.elements.data` | `.../data.cppm` | `registerDataElements` — ProgressBar, CircularProgress, BusyIndicator, Plot, Table, Canvas |
| `imlui.ui.elements.system` | `.../system.cppm` | `registerSystemElements` — Snackbar, Timer, ImGuiDemo |
| `imlui.ui.elements` | `.../elements.cppm` | `registerBuiltinElements(Renderer&)` calling all of the above |
| `imlui.app.config` | `src/imlui/app/config.cppm` | `struct EngineConfig` (same fields; `window` is `platform::Window::Config`), `defaultConfig(argc, argv, sourceDir)` |
| `imlui.app.engine` | `src/imlui/app/engine.cppm` | `class Engine` (same public API: ctor(EngineConfig), `renderer()`, `requestQuit`, `requestReload`, `window()`, `run()`); all members `m_` |
| `imlui` | `src/imlui/imlui.cppm` | umbrella: export import of the app-facing modules |

Phase C codes against the **phase B widget names** above; until phase B lands, `import imlui.widgets;` (the old
single module, same name) provides the same `md::` functions, so phase C can build on its own.

## Phase D — DPI scaling (after B and C are merged)

Specified separately.
