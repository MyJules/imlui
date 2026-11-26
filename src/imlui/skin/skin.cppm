// Skins: what the controls look like. A Skin draws and measures every control the elements show; the theme picks the
// active one by name (Theme::design, Lua `theme { design = "material" }`).
//
// Skin itself is the Material skin ("material"): each virtual calls the control's md:: widget. Another look
// subclasses it and overrides only the controls it draws differently; everything else stays Material. Elements call
// `skin().button(...)` and `skin().buttonSize(...)` instead of md::, so a size always comes from the skin that draws.
//
// What goes through the skin: a call that decides a look (a control's shape, colors, metrics or its natural size:
// buttons, toggles, fields, menus, cards, dialogs, tooltips, dividers, ...). What stays a direct md:: call: drawing
// helpers that decide nothing (fillRoundRect, strokeRoundRect, fillCircle, strokeCircle, hLine, vLine, drawIcon, text
// and icon items, itemWidth, scopedWindowName, the constants), the vocabulary parsers (buttonVariant, chipVariant,
// ...), and the widgets' own stacks (pushSurface/popSurface/surfaceColor carry the color an element chose;
// cardDepth/surfaceDepth and their restores undo a render pass an error cut short). The md:: widgets keep calling
// each other inside (a snackbar's action is md::button).
//
// The active skin: skin() is resolved from theme().design when the theme generation moved, so a design switches
// with the theme, between frames (themeGeneration() moves only in applyPendingTheme()), never mid-frame. The theme
// keeps only the name: imlui.theme can't import this module (the widgets import the theme, and Skin calls the
// widgets), and a lookup here, keyed by the generation, needs no hook in applyPendingTheme() nor a call from every
// place that applies a theme (the engine, the tests). Main thread only, like the theme.
export module imlui.skin;

import std;

import imgui;
import imlui.color;
import imlui.log;
import imlui.theme.theme;
import imlui.widgets.button;
import imlui.widgets.card;
import imlui.widgets.chart;
import imlui.widgets.color_field;
import imlui.widgets.dialog;
import imlui.widgets.dropdown;
import imlui.widgets.list;
import imlui.widgets.menu;
import imlui.widgets.navigation;
import imlui.widgets.navigation_drawer;
import imlui.widgets.progress;
import imlui.widgets.selection;
import imlui.widgets.slider;
import imlui.widgets.snackbar;
import imlui.widgets.spin_box;
import imlui.widgets.table;
import imlui.widgets.tabs;
import imlui.widgets.text;
import imlui.widgets.text_field;

export namespace imlui {

/// Draws and measures the controls. The base class is the Material skin; a subclass overrides what it draws
/// differently (each draw function together with its size function, so layouts measure what is drawn). The
/// parameters are the md:: widgets' (see their docs); no defaults, so a call reads the same through any subclass.
class Skin {
public:
    virtual ~Skin() = default;

    /// The skin's name ("material"). Informational: the registry selects skins by the name they're registered under.
    [[nodiscard]] virtual std::string_view name() const;

    // Buttons
    virtual bool button(const char* label, md::ButtonVariant variant, ImVec2 size, std::string_view iconName) const;
    [[nodiscard]] virtual ImVec2 buttonSize(const char* label, md::ButtonVariant variant, ImVec2 size,
                                            std::string_view iconName) const;
    virtual bool iconButton(const char* id, std::string_view iconName, md::IconButtonVariant variant, bool toggle,
                            bool selected, std::optional<Color> tint) const;
    [[nodiscard]] virtual ImVec2 iconButtonSize() const;

    // Selection controls
    virtual bool checkbox(const char* label, bool* v) const;
    [[nodiscard]] virtual ImVec2 checkboxSize(const char* label) const;
    virtual bool radio(const char* label, bool selected) const;
    [[nodiscard]] virtual ImVec2 radioSize(const char* label) const;
    virtual bool switchToggle(const char* label, bool* v, bool withIcons) const;
    [[nodiscard]] virtual ImVec2 switchSize(const char* label) const;
    virtual bool chip(const char* label, bool selected, md::ChipVariant variant, std::string_view leadingIcon) const;
    [[nodiscard]] virtual ImVec2 chipSize(const char* label, bool selected, md::ChipVariant variant,
                                          std::string_view leadingIcon) const;
    virtual bool segmented(const char* id, const std::vector<std::string>& items, int* current, float width,
                           const std::vector<std::string>& icons) const;
    [[nodiscard]] virtual ImVec2 segmentedSize(const std::vector<std::string>& items, float width) const;

    // Ranges
    virtual bool slider(const char* id, double* v, double vmin, double vmax, double step, float width,
                        const char* valueFormat) const;
    [[nodiscard]] virtual ImVec2 sliderSize(float width) const;
    virtual void progress(float fraction, float width) const;
    [[nodiscard]] virtual ImVec2 progressSize(float width) const;
    virtual void circularProgress(float fraction, float diameter, float thickness) const;
    [[nodiscard]] virtual ImVec2 circularProgressSize(float diameter) const;

    // Inputs
    virtual md::TextFieldResult textField(const char* id, std::string& value, const md::TextFieldOptions& o) const;
    [[nodiscard]] virtual ImVec2 textFieldSize(const md::TextFieldOptions& o) const;
    virtual bool spinBox(const char* id, double* v, double vmin, double vmax, double step, int decimals,
                         std::string_view label, float width, bool outlined) const;
    [[nodiscard]] virtual ImVec2 spinBoxSize(std::string_view label, float width, bool outlined) const;
    virtual bool dropdown(const char* id, const std::vector<std::string>& items, int* current,
                          const md::DropdownOptions& o, const std::vector<std::string>& icons) const;
    [[nodiscard]] virtual ImVec2 dropdownSize(const md::DropdownOptions& o) const;
    virtual bool colorField(const char* id, Color* c, std::string_view label, bool alpha, float width) const;
    [[nodiscard]] virtual ImVec2 colorFieldSize(Color c, std::string_view label, float width) const;

    // Lists and menus
    virtual bool listItem(const char* id, const md::ListItemOptions& o) const;
    [[nodiscard]] virtual ImVec2 listItemSize(const md::ListItemOptions& o) const;
    virtual bool expander(const char* id, std::string_view title, std::string_view supporting, bool* open,
                          float width) const;
    [[nodiscard]] virtual ImVec2 expanderSize(std::string_view supporting, float width) const;
    virtual bool menuItem(const char* id, std::string_view label, std::string_view leadingIcon,
                          std::string_view trailing, bool selected, bool closeOnClick, bool reserveIcon) const;
    /// A menu-bar menu or a submenu (md::beginMenu); endMenu() after it returned true.
    virtual bool beginMenu(const char* label, float minWidth) const;
    virtual void endMenu() const;
    /// A dropdown-style menu under [a, b] (md::beginAnchoredMenu); endAnchoredMenu() after it returned true.
    virtual bool beginAnchoredMenu(const char* popupId, ImVec2 a, ImVec2 b, float minWidth) const;
    virtual void endAnchoredMenu() const;

    // Navigation
    virtual bool tabs(const char* id, const std::vector<md::TabInfo>& items, int* current, bool secondary,
                      float width) const;
    virtual bool navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                                std::string_view badge, float width) const;
    [[nodiscard]] virtual ImVec2 navigationItemSize(float width) const;
    virtual bool navigationBarItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                                   std::string_view badge, float width) const;
    [[nodiscard]] virtual ImVec2 navigationBarItemSize(float width) const;
    virtual bool navigationRailItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                                    std::string_view badge, float width) const;
    [[nodiscard]] virtual ImVec2 navigationRailItemSize(float width) const;
    virtual bool beginModalDrawer(const char* id, bool* open, float width) const;
    virtual void endModalDrawer() const;

    // Containers and messages
    virtual bool beginCard(const char* id, md::CardVariant variant, float width, float padding, float height,
                           std::optional<Color> color, float rounding) const;
    virtual void endCard() const;
    [[nodiscard]] virtual ImVec2 cardSize(float width, float padding, float height, float contentHeight) const;
    virtual bool beginDialog(const char* title, bool* open, float width, std::string_view heroIcon) const;
    virtual void endDialog() const;
    virtual bool snackbar(std::string_view message, std::string_view action, float t, bool closeIcon,
                          bool* closed) const;
    virtual void tooltip(std::string_view s) const;

    // Dividers, tables and charts
    virtual void divider(float insetStart, float insetEnd, float width) const;
    [[nodiscard]] virtual ImVec2 dividerSize(float width) const;
    virtual void tableHeaderCell(std::string_view text, float rowH) const;
    virtual void tableCell(std::string_view text, float rowH, std::optional<Color> color) const;
    virtual void chart(const char* id, const std::vector<float>& values, const md::ChartOptions& o) const;
    [[nodiscard]] virtual ImVec2 chartSize(const md::ChartOptions& o) const;
};

/// Makes a skin; registered by name with registerSkin().
using SkinFactory = std::function<std::unique_ptr<Skin>()>;

/// The active skin: the one registered under theme().design (Material when no skin has that name). Called per control
/// per frame: a generation compare and a pointer read, except on the first call after the theme changed, which
/// resolves the name (and makes the skin when the name or its registration changed).
[[nodiscard]] const Skin& skin();

/// Registers `factory` under `name`, replacing a skin of that name ("material" included). It takes effect the next
/// time the theme changes (a theme{} call or a reload), never in the middle of a frame. A factory that is empty or
/// returns null draws Material.
void registerSkin(std::string name, SkinFactory factory);

/// Whether a skin is registered under `name` (what `theme { design = ... }` accepts).
[[nodiscard]] bool hasSkin(std::string_view name);

/// The registered skin names, sorted ("material" among them).
[[nodiscard]] std::vector<std::string> skinNames();

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// The design every theme starts with, always registered.
constexpr std::string_view materialDesign = "material";

/// Last resort when a factory gives no skin: the base class is Material.
const Skin materialFallback{};

struct Registry {
    std::map<std::string, SkinFactory, std::less<>> factories;
    std::uint64_t version = 0; ///< moves on every registerSkin(): the active skin is made again from its new factory
};

Registry& registry() {
    static Registry r = [] {
        Registry init;
        init.factories.emplace(std::string{materialDesign}, [] { return std::make_unique<Skin>(); });
        return init;
    }();
    return r;
}

/// The skin in effect and what it was resolved from.
struct Active {
    std::unique_ptr<Skin> owned; ///< null: materialFallback
    const Skin* skin = &materialFallback;
    std::string design;        ///< the registry name it was made from ("" before the first resolve)
    std::uint64_t version = 0; ///< Registry::version it was made at
    std::uint64_t generation = std::numeric_limits<std::uint64_t>::max(); ///< themeGeneration() it is current for
};

Active active;

void resolve() {
    Registry& reg = registry();
    std::string_view design = theme().design;
    auto it = reg.factories.find(design);
    if (it == reg.factories.end()) {
        log::warn("theme design '{}' is not a registered skin: using '{}'", design, materialDesign);
        design = materialDesign;
        it = reg.factories.find(design);
    }
    if (design == active.design && reg.version == active.version) return; // same skin: keep its instance
    std::unique_ptr<Skin> made;
    if (it != reg.factories.end() && it->second) made = it->second();
    if (!made) log::warn("the skin factory of '{}' made no skin: using Material", design);
    active.owned = std::move(made);
    active.skin = active.owned ? active.owned.get() : &materialFallback;
    active.design = std::string{design};
    active.version = reg.version;
}

} // namespace

const Skin& skin() {
    const std::uint64_t generation = themeGeneration();
    if (active.generation != generation) {
        active.generation = generation;
        resolve();
    }
    return *active.skin;
}

void registerSkin(std::string name, SkinFactory factory) {
    Registry& reg = registry();
    reg.factories.insert_or_assign(std::move(name), std::move(factory));
    ++reg.version;
}

bool hasSkin(std::string_view name) { return registry().factories.contains(name); }

std::vector<std::string> skinNames() {
    std::vector<std::string> names;
    for (const auto& entry : registry().factories)
        names.push_back(entry.first);
    return names;
}

std::string_view Skin::name() const { return materialDesign; }

bool Skin::button(const char* label, md::ButtonVariant variant, ImVec2 size, std::string_view iconName) const {
    return md::button(label, variant, size, iconName);
}
ImVec2 Skin::buttonSize(const char* label, md::ButtonVariant variant, ImVec2 size, std::string_view iconName) const {
    return md::buttonSize(label, variant, size, iconName);
}
bool Skin::iconButton(const char* id, std::string_view iconName, md::IconButtonVariant variant, bool toggle,
                      bool selected, std::optional<Color> tint) const {
    return md::iconButton(id, iconName, variant, toggle, selected, tint);
}
ImVec2 Skin::iconButtonSize() const { return md::iconButtonSize(); }

bool Skin::checkbox(const char* label, bool* v) const { return md::checkbox(label, v); }
ImVec2 Skin::checkboxSize(const char* label) const { return md::checkboxSize(label); }
bool Skin::radio(const char* label, bool selected) const { return md::radio(label, selected); }
ImVec2 Skin::radioSize(const char* label) const { return md::radioSize(label); }
bool Skin::switchToggle(const char* label, bool* v, bool withIcons) const {
    return md::switchToggle(label, v, withIcons);
}
ImVec2 Skin::switchSize(const char* label) const { return md::switchSize(label); }
bool Skin::chip(const char* label, bool selected, md::ChipVariant variant, std::string_view leadingIcon) const {
    return md::chip(label, selected, variant, leadingIcon);
}
ImVec2 Skin::chipSize(const char* label, bool selected, md::ChipVariant variant, std::string_view leadingIcon) const {
    return md::chipSize(label, selected, variant, leadingIcon);
}
bool Skin::segmented(const char* id, const std::vector<std::string>& items, int* current, float width,
                     const std::vector<std::string>& icons) const {
    return md::segmented(id, items, current, width, icons);
}
ImVec2 Skin::segmentedSize(const std::vector<std::string>& items, float width) const {
    return md::segmentedSize(items, width);
}

bool Skin::slider(const char* id, double* v, double vmin, double vmax, double step, float width,
                  const char* valueFormat) const {
    return md::slider(id, v, vmin, vmax, step, width, valueFormat);
}
ImVec2 Skin::sliderSize(float width) const { return md::sliderSize(width); }
void Skin::progress(float fraction, float width) const { md::progress(fraction, width); }
ImVec2 Skin::progressSize(float width) const { return md::progressSize(width); }
void Skin::circularProgress(float fraction, float diameter, float thickness) const {
    md::circularProgress(fraction, diameter, thickness);
}
ImVec2 Skin::circularProgressSize(float diameter) const { return md::circularProgressSize(diameter); }

md::TextFieldResult Skin::textField(const char* id, std::string& value, const md::TextFieldOptions& o) const {
    return md::textField(id, value, o);
}
ImVec2 Skin::textFieldSize(const md::TextFieldOptions& o) const { return md::textFieldSize(o); }
bool Skin::spinBox(const char* id, double* v, double vmin, double vmax, double step, int decimals,
                   std::string_view label, float width, bool outlined) const {
    return md::spinBox(id, v, vmin, vmax, step, decimals, label, width, outlined);
}
ImVec2 Skin::spinBoxSize(std::string_view label, float width, bool outlined) const {
    return md::spinBoxSize(label, width, outlined);
}
bool Skin::dropdown(const char* id, const std::vector<std::string>& items, int* current, const md::DropdownOptions& o,
                    const std::vector<std::string>& icons) const {
    return md::dropdown(id, items, current, o, icons);
}
ImVec2 Skin::dropdownSize(const md::DropdownOptions& o) const { return md::dropdownSize(o); }
bool Skin::colorField(const char* id, Color* c, std::string_view label, bool alpha, float width) const {
    return md::colorField(id, c, label, alpha, width);
}
ImVec2 Skin::colorFieldSize(Color c, std::string_view label, float width) const {
    return md::colorFieldSize(c, label, width);
}

bool Skin::listItem(const char* id, const md::ListItemOptions& o) const { return md::listItem(id, o); }
ImVec2 Skin::listItemSize(const md::ListItemOptions& o) const { return md::listItemSize(o); }
bool Skin::expander(const char* id, std::string_view title, std::string_view supporting, bool* open,
                    float width) const {
    return md::expander(id, title, supporting, open, width);
}
ImVec2 Skin::expanderSize(std::string_view supporting, float width) const {
    return md::expanderSize(supporting, width);
}
bool Skin::menuItem(const char* id, std::string_view label, std::string_view leadingIcon, std::string_view trailing,
                    bool selected, bool closeOnClick, bool reserveIcon) const {
    return md::menuItem(id, label, leadingIcon, trailing, selected, closeOnClick, reserveIcon);
}
bool Skin::beginMenu(const char* label, float minWidth) const { return md::beginMenu(label, minWidth); }
void Skin::endMenu() const { md::endMenu(); }
bool Skin::beginAnchoredMenu(const char* popupId, ImVec2 a, ImVec2 b, float minWidth) const {
    return md::beginAnchoredMenu(popupId, a, b, minWidth);
}
void Skin::endAnchoredMenu() const { md::endAnchoredMenu(); }

bool Skin::tabs(const char* id, const std::vector<md::TabInfo>& items, int* current, bool secondary,
                float width) const {
    return md::tabs(id, items, current, secondary, width);
}
bool Skin::navigationItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                          std::string_view badge, float width) const {
    return md::navigationItem(id, iconName, label, selected, badge, width);
}
ImVec2 Skin::navigationItemSize(float width) const { return md::navigationItemSize(width); }
bool Skin::navigationBarItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                             std::string_view badge, float width) const {
    return md::navigationBarItem(id, iconName, label, selected, badge, width);
}
ImVec2 Skin::navigationBarItemSize(float width) const { return md::navigationBarItemSize(width); }
bool Skin::navigationRailItem(const char* id, std::string_view iconName, std::string_view label, bool selected,
                              std::string_view badge, float width) const {
    return md::navigationRailItem(id, iconName, label, selected, badge, width);
}
ImVec2 Skin::navigationRailItemSize(float width) const { return md::navigationRailItemSize(width); }
bool Skin::beginModalDrawer(const char* id, bool* open, float width) const {
    return md::beginModalDrawer(id, open, width);
}
void Skin::endModalDrawer() const { md::endModalDrawer(); }

bool Skin::beginCard(const char* id, md::CardVariant variant, float width, float padding, float height,
                     std::optional<Color> color, float rounding) const {
    return md::beginCard(id, variant, width, padding, height, color, rounding);
}
void Skin::endCard() const { md::endCard(); }
ImVec2 Skin::cardSize(float width, float padding, float height, float contentHeight) const {
    return md::cardSize(width, padding, height, contentHeight);
}
bool Skin::beginDialog(const char* title, bool* open, float width, std::string_view heroIcon) const {
    return md::beginDialog(title, open, width, heroIcon);
}
void Skin::endDialog() const { md::endDialog(); }
bool Skin::snackbar(std::string_view message, std::string_view action, float t, bool closeIcon, bool* closed) const {
    return md::snackbar(message, action, t, closeIcon, closed);
}
void Skin::tooltip(std::string_view s) const { md::tooltip(s); }

void Skin::divider(float insetStart, float insetEnd, float width) const { md::divider(insetStart, insetEnd, width); }
ImVec2 Skin::dividerSize(float width) const { return md::dividerSize(width); }
void Skin::tableHeaderCell(std::string_view text, float rowH) const { md::tableHeaderCell(text, rowH); }
void Skin::tableCell(std::string_view text, float rowH, std::optional<Color> color) const {
    md::tableCell(text, rowH, color);
}
void Skin::chart(const char* id, const std::vector<float>& values, const md::ChartOptions& o) const {
    md::chart(id, values, o);
}
ImVec2 Skin::chartSize(const md::ChartOptions& o) const { return md::chartSize(o); }

} // namespace imlui
