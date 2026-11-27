// The skin registry: skin() is the active skin, the one registered under theme().design; registerSkin() adds one.
// "material" (MaterialSkin) and "fluent" (FluentSkin) are built in.
//
// The active skin: skin() is resolved from theme().design when the theme generation moved, so a design switches
// with the theme, between frames (themeGeneration() moves only in applyPendingTheme()), never mid-frame. The theme
// keeps only the name: imlui.theme can't import the skins (the widgets import the theme, and Skin calls the
// widgets), and a lookup here, keyed by the generation, needs no hook in applyPendingTheme() nor a call from every
// place that applies a theme (the engine, the tests). Main thread only, like the theme.
export module imlui.skin.registry;

import std;

import imlui.log;
import imlui.skin.base;
import imlui.skin.fluent.fluent_skin;
import imlui.skin.material.material_skin;
import imlui.theme.fluent_colors;
import imlui.theme.theme;

export namespace imlui {

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

/// The registered skin names, sorted ("material" and "fluent" among them).
[[nodiscard]] std::vector<std::string> skinNames();

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// The design every theme starts with, always registered.
constexpr std::string_view materialDesign = material::designName;

/// Last resort when a factory gives no skin: the base class is Material.
const Skin materialFallback{};

struct Registry {
    std::map<std::string, SkinFactory, std::less<>> factories;
    std::uint64_t version = 0; ///< moves on every registerSkin(): the active skin is made again from its new factory
};

Registry& registry() {
    static Registry r = [] {
        Registry init;
        init.factories.emplace(std::string{materialDesign}, [] { return std::make_unique<MaterialSkin>(); });
        init.factories.emplace(std::string{fluent::designName}, [] { return std::make_unique<FluentSkin>(); });
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

} // namespace imlui
