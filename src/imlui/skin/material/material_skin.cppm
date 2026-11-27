// MaterialSkin: the Material 3 look, registered as "material" (`theme { design = "material" }`), the default design.
//
// The Skin base class already draws Material (each virtual calls the control's md:: widget under
// src/imlui/widgets/), since every other skin falls back to it for the controls it doesn't override. So MaterialSkin
// adds nothing: it is the named, final peer of FluentSkin (src/imlui/skin/fluent/), so each built-in design has its
// own module under src/imlui/skin/ and the registry makes each from its own class.
export module imlui.skin.material.material_skin;

import std;

import imlui.skin.base;

export namespace imlui {

namespace material {
/// The registry name of the Material design (Theme::design's default).
inline constexpr std::string_view designName = "material";
} // namespace material

/// The Material 3 skin: the Skin base class's draws, under its own name.
class MaterialSkin final : public Skin {
public:
    [[nodiscard]] std::string_view name() const override;
};

} // namespace imlui

module :private;

namespace imlui {

std::string_view MaterialSkin::name() const { return material::designName; }

} // namespace imlui
