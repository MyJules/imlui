// Font library: the text fonts in the atlas, loaded by resource name, and the set of them in use.
//
// Every text font, the built-in Roboto faces included, is loaded through here: one atlas font per resource name
// and fallback list, created on first use and reused while the resource returns the same bytes. Fonts are
// size-independent (ImGui 1.92 rasterizes glyphs on demand), so one atlas font per face is enough. A theme
// declares its faces as a FontSet (Lua: `ui.theme { fonts = { ... } }`); useFontSet() makes it current.
module;

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module imlui.theme.font_library;

import imgui;
import imlui.log;
import imlui.resources;
import imlui.resources.name;

export namespace imlui {

/// The built-in faces of the type scale.
constexpr std::string_view builtinRegularFont = "imlui:/fonts/Roboto-Regular.ttf";
constexpr std::string_view builtinMediumFont = "imlui:/fonts/Roboto-Medium.ttf";

/// The fonts a theme declares. Every value is a full resource name ("app:/fonts/Inter-Regular.ttf").
struct FontSet {
    std::string regular;               ///< the type scale's Regular face; empty: Roboto Regular
    std::string medium;                ///< the type scale's Medium face; empty: Roboto Medium
    std::vector<std::string> fallback; ///< merged into every text font, in order, for the glyphs it lacks
    std::map<std::string, std::string, std::less<>> named; ///< extra faces, selected per element by name

    bool operator==(const FontSet&) const = default;
};

/// Forgets every font loaded so far (call right after clearing the atlas) and reads fonts from `resources` from now
/// on. The atlas doesn't copy font data, it points into the bytes Resources::read returned, so `resources` (its
/// mounts) must outlive the font atlas.
void resetFontLibrary(const Resources& resources);

/// Forgets every font and the resources, so nothing is loaded until the next resetFontLibrary(). Call before the
/// font atlas (the ImGui context) or the resources go away.
void clearFontLibrary();

/// Loads every font of `set`, with its fallbacks merged in, into the atlas, reusing fonts already loaded from the
/// same bytes; a resource that now returns other bytes (its file changed) is loaded again. Returns one message per
/// font or fallback that couldn't be loaded (missing, not a font). Safe inside a frame: it only adds fonts.
std::vector<std::string> loadFontSet(const FontSet& set);

/// Makes `set` the fonts in use (loading what's missing, failures logged; a face that fails falls back to the
/// built-in one) and removes from the atlas the fonts replaced by newer bytes. Call between frames.
void useFontSet(const FontSet& set);

/// The type scale's Regular / Medium face in use; nullptr before resetFontLibrary() (e.g. a bare ImGui context).
[[nodiscard]] ImFont* regularFace();
[[nodiscard]] ImFont* mediumFace();

/// The face in use named `name`: a FontSet::named key, or "regular" / "medium". nullptr when the set in use
/// doesn't declare it or it couldn't be loaded (and then also when `name` is empty).
[[nodiscard]] ImFont* namedFont(std::string_view name);

/// Whether the set in use declares `name` (true also when that font failed to load: the error was reported then).
[[nodiscard]] bool declaresFont(std::string_view name);

/// Atlas fonts the library holds (cached and replaced-but-not-yet-removed); for tests.
[[nodiscard]] std::size_t libraryFontCount();

/// The face that replaces the type scale's faces in the current scope (see FontFaceScope); nullptr: none.
[[nodiscard]] ImFont* faceOverride();
/// Sets the face override; returns the previous one, to restore.
ImFont* setFaceOverride(ImFont* face);

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// One loaded source: identifies the bytes a font was built from.
struct SourceBytes {
    const std::byte* data = nullptr;
    std::size_t size = 0;
    bool operator==(const SourceBytes&) const = default;
};

struct Entry {
    ImFont* font = nullptr;
    std::vector<SourceBytes> sources; ///< the face, then each fallback (empty span: missing)
};

const Resources* library = nullptr;
std::map<std::string, Entry, std::less<>> cache; ///< by cacheKey()
std::vector<ImFont*> replaced;                   ///< removed at the next useFontSet()
ImFont* defaultFont = nullptr;                   ///< ImGui's own, when the built-in Regular face is missing

ImFont* currentRegular = nullptr;
ImFont* currentMedium = nullptr;
std::map<std::string, ImFont*, std::less<>> currentNamed;
ImFont* currentOverride = nullptr;
std::set<std::string> loggedErrors; ///< useFontSet() logs each failure once

std::string cacheKey(std::string_view name, std::span<const std::string> fallback) {
    std::string key{name};
    for (const auto& f : fallback)
        key.append("\n").append(f);
    return key;
}

/// Whether `bytes` start like a TrueType/OpenType font or collection (stb_truetype asserts on anything else).
bool looksLikeFont(std::span<const std::byte> bytes) {
    if (bytes.size() < 12) return false;
    const auto is = [&](std::string_view tag) {
        return std::ranges::equal(bytes.first(4), tag,
                                  [](std::byte b, char c) { return b == static_cast<std::byte>(c); });
    };
    return is(std::string_view{"\0\1\0\0", 4}) || is("OTTO") || is("true") || is("ttcf");
}

/// Reads font `name`; empty span and a message in `error` when it is invalid, missing or not a font.
std::span<const std::byte> readFont(std::string_view name, std::string& error) {
    const auto canonical = normalizeName(name);
    if (!canonical) {
        error = "invalid font resource name '" + std::string{name} + "' (expected e.g. 'app:/fonts/Inter.ttf')";
        return {};
    }
    const auto bytes = library->read(*canonical);
    if (!bytes || bytes->empty()) {
        error = "font '" + *canonical + "' not found";
        return {};
    }
    if (!looksLikeFont(*bytes)) {
        error = "'" + *canonical + "' is not a TrueType/OpenType font";
        return {};
    }
    return *bytes;
}

ImFont* addFont(std::span<const std::byte> bytes, ImFont* mergeInto) {
    ImFontConfig cfg;
    // The bytes stay alive as long as the mount (see Mount::read), so the atlas needn't own a copy. ImGui only
    // reads them, but its API takes a non-const pointer.
    cfg.FontDataOwnedByAtlas = false;
    if (mergeInto) {
        cfg.MergeMode = true;
        cfg.DstFont = mergeInto;
    }
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(bytes.data()),
                                                      static_cast<int>(bytes.size()), 16.0f, &cfg);
}

/// The atlas font for face `name` with `fallback` merged in, loading or reloading it as needed; nullptr when the
/// face itself can't be loaded, with the reason in `error`. Fallbacks that can't be read are left out (see
/// fallbackErrors()).
ImFont* fontFor(std::string_view name, std::span<const std::string> fallback, std::string& error) {
    if (!library) return nullptr;
    const auto face = readFont(name, error);
    if (face.empty()) return nullptr;
    std::vector<SourceBytes> sources{{face.data(), face.size()}};
    std::vector<std::span<const std::byte>> fallbackBytes;
    for (const auto& f : fallback) {
        std::string ignored; // reported once by fallbackErrors(), not once per font
        const auto bytes = readFont(f, ignored);
        fallbackBytes.push_back(bytes);
        sources.push_back({bytes.data(), bytes.size()});
    }

    const std::string key = cacheKey(name, fallback);
    auto it = cache.find(key);
    if (it != cache.end()) {
        if (it->second.sources == sources) return it->second.font;
        // Its file changed: build a new font. The old one may be pushed in this frame; it goes at useFontSet().
        replaced.push_back(it->second.font);
        cache.erase(it);
    }
    ImFont* font = addFont(face, nullptr);
    if (!font) {
        error = "font '" + std::string{name} + "' could not be loaded";
        return nullptr;
    }
    for (const auto& bytes : fallbackBytes)
        if (!bytes.empty()) addFont(bytes, font);
    cache.emplace(key, Entry{font, std::move(sources)});
    return font;
}

/// Messages for the fallbacks of `set` that can't be read.
std::vector<std::string> fallbackErrors(const FontSet& set) {
    std::vector<std::string> errors;
    std::string error;
    for (const auto& f : set.fallback)
        if (readFont(f, error).empty()) errors.push_back("fallback: " + error);
    return errors;
}

/// Loads the face of `role` ("regular", "medium" or a named font) from `name`, adding a message to `errors` on
/// failure.
ImFont* loadFace(std::string_view role, std::string_view name, const FontSet& set, std::vector<std::string>& errors) {
    std::string error;
    ImFont* font = fontFor(name, set.fallback, error);
    if (!font) errors.push_back(std::string{role} + ": " + error);
    return font;
}

/// The type scale face `role` declared as `name` (empty: the built-in `builtin`), falling back to the built-in
/// face. Failures of the built-in face are logged, not added to `errors`: they are the library's, not the script's.
ImFont* typeScaleFace(std::string_view role, const std::string& name, std::string_view builtin, const FontSet& set,
                      std::vector<std::string>& errors) {
    if (!name.empty())
        if (ImFont* f = loadFace(role, name, set, errors)) return f;
    std::string error;
    if (ImFont* f = fontFor(builtin, set.fallback, error)) return f;
    log::warn("{}", error);
    return nullptr;
}

} // namespace

void resetFontLibrary(const Resources& resources) {
    clearFontLibrary();
    library = &resources;
}

void clearFontLibrary() {
    library = nullptr;
    cache.clear();
    replaced.clear();
    defaultFont = nullptr;
    currentRegular = currentMedium = currentOverride = nullptr;
    currentNamed.clear();
    loggedErrors.clear();
}

std::vector<std::string> loadFontSet(const FontSet& set) {
    std::vector<std::string> errors = fallbackErrors(set);
    typeScaleFace("regular", set.regular, builtinRegularFont, set, errors);
    typeScaleFace("medium", set.medium, builtinMediumFont, set, errors);
    for (const auto& [name, resource] : set.named)
        loadFace(name, resource, set, errors);
    return errors;
}

void useFontSet(const FontSet& set) {
    if (!library) return;
    std::vector<std::string> errors = fallbackErrors(set);
    currentRegular = typeScaleFace("regular", set.regular, builtinRegularFont, set, errors);
    currentMedium = typeScaleFace("medium", set.medium, builtinMediumFont, set, errors);
    if (!currentRegular) {
        if (!defaultFont) defaultFont = ImGui::GetIO().Fonts->AddFontDefault();
        currentRegular = defaultFont;
    }
    if (!currentMedium) currentMedium = currentRegular;
    currentNamed.clear();
    for (const auto& [name, resource] : set.named)
        currentNamed.emplace(name, loadFace(name, resource, set, errors));
    for (auto& e : errors)
        if (loggedErrors.insert(e).second) log::warn("theme.fonts.{}", e);
    ImGui::GetIO().FontDefault = currentRegular;

    // Between frames nothing is pushed, and the faces in use were just looked up again, so the fonts replaced
    // by newer bytes are unused now.
    for (ImFont* f : replaced)
        ImGui::GetIO().Fonts->RemoveFont(f);
    replaced.clear();
}

ImFont* regularFace() { return currentRegular; }
ImFont* mediumFace() { return currentMedium; }

ImFont* namedFont(std::string_view name) {
    if (const auto it = currentNamed.find(name); it != currentNamed.end()) return it->second;
    if (name == "regular") return currentRegular;
    if (name == "medium") return currentMedium;
    return nullptr;
}

bool declaresFont(std::string_view name) {
    return currentNamed.contains(name) || name == "regular" || name == "medium";
}

std::size_t libraryFontCount() { return cache.size() + replaced.size(); }

ImFont* faceOverride() { return currentOverride; }
ImFont* setFaceOverride(ImFont* face) { return std::exchange(currentOverride, face); }

} // namespace imlui
