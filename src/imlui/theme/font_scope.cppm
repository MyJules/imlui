// FontScope: RAII push/pop of a text style's font.
export module imlui.theme.font_scope;

import imgui;
import imlui.theme.typography;

export namespace imlui {

/// Pushes the font of `style` for the lifetime of the scope.
class FontScope {
public:
    explicit FontScope(TextStyle style);
    ~FontScope();
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};

} // namespace imlui

module :private;

namespace imlui {

FontScope::FontScope(TextStyle style) { pushFont(style); }
FontScope::~FontScope() { ImGui::PopFont(); }

} // namespace imlui
