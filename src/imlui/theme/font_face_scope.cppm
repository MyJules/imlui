// FontFaceScope: RAII override of the type scale's faces with a named font (an element's `font` prop).
module;

#include <functional>
#include <set>
#include <string>
#include <string_view>

export module imlui.theme.font_face_scope;

import imgui;
import imlui.log;
import imlui.theme.font_library;

export namespace imlui {

/// For the lifetime of the scope, every text style draws in the face named `name` (see namedFont()), at the style's
/// own size. An empty name changes nothing. A name the theme doesn't declare changes nothing either and is logged
/// once; a declared font that failed to load was reported when the theme was set.
class FontFaceScope {
public:
    explicit FontFaceScope(std::string_view name);
    ~FontFaceScope();
    FontFaceScope(const FontFaceScope&) = delete;
    FontFaceScope& operator=(const FontFaceScope&) = delete;

private:
    ImFont* m_previous = nullptr;
    bool m_active = false;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {
std::set<std::string, std::less<>> warnedFonts; ///< unknown names already logged
} // namespace

FontFaceScope::FontFaceScope(std::string_view name) {
    if (name.empty()) return;
    ImFont* face = namedFont(name);
    if (!face) {
        if (!declaresFont(name) && warnedFonts.emplace(name).second)
            log::warn("unknown font '{}': declare it in ui.theme {{ fonts = {{ ... }} }}", name);
        return;
    }
    m_previous = setFaceOverride(face);
    m_active = true;
}

FontFaceScope::~FontFaceScope() {
    if (m_active) setFaceOverride(m_previous);
}

} // namespace imlui
