// Font library: the text fonts in the atlas, loaded by resource name, and the set of them in use.
//
// Every text font, the built-in Roboto faces included, is loaded through here: one atlas font per resource name
// and fallback list, created on first use and reused while the resource returns the same bytes. Fonts are
// size-independent (ImGui 1.92 rasterizes glyphs on demand), so one atlas font per face is enough. A theme
// declares its faces as a FontSet (Lua: `ui.theme { fonts = { ... } }`); useFontSet() makes it current.
export module imlui.theme.font_library;

import std;

import imgui;
import imlui.log;
import imlui.resources;
import imlui.resources.name;
import imlui.theme.font_check;
import imlui.theme.system_fonts;

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
/// on. The atlas doesn't copy font data, it points into the bytes Resources::read returned; the library keeps their
/// ResourceData handles as long as it keeps the atlas font, so `resources` (its mounts) must outlive the font atlas.
void resetFontLibrary(const Resources& resources);

/// Forgets every font and the resources, so nothing is loaded until the next resetFontLibrary(), and lets go of the
/// fonts' bytes. Call when the font atlas is cleared or destroyed (it points into those bytes), before the resources
/// go away.
void clearFontLibrary();

/// Loads every font of `set`, with its fallbacks merged in, into the atlas, reusing fonts already loaded from the
/// same bytes; a resource that now returns other bytes (its file changed) is loaded again. Returns one message per
/// font or fallback that couldn't be loaded (missing, not a font, damaged: see checkFont()). Safe inside a frame: it
/// only adds fonts.
std::vector<std::string> loadFontSet(const FontSet& set);

/// Makes `set` the fonts in use (loading what's missing, failures logged; a face that fails falls back to the
/// built-in one) and removes from the atlas, with their bytes, the fonts replaced by newer bytes and the ones that
/// neither `set` nor the set used before it needs (nor a loadFontSet() since then). Call between frames.
void useFontSet(const FontSet& set);

/// The type scale's Regular / Medium face in use; nullptr before resetFontLibrary() (e.g. a bare ImGui context).
[[nodiscard]] ImFont* regularFace();
[[nodiscard]] ImFont* mediumFace();

/// The face in use named `name`: a FontSet::named key, or "regular" / "medium". nullptr when the set in use
/// doesn't declare it or it couldn't be loaded (and then also when `name` is empty).
[[nodiscard]] ImFont* namedFont(std::string_view name);

/// Whether the set in use declares `name` (true also when that font failed to load: the error was reported then).
[[nodiscard]] bool declaresFont(std::string_view name);

/// Atlas fonts the library holds (cached and replaced-but-not-yet-removed); for tests. Bounded by the fonts of the
/// last two sets used and of the loadFontSet() calls since the last useFontSet().
[[nodiscard]] std::size_t libraryFontCount();

/// How many times checkFont() ran since the last resetFontLibrary(); for tests. Each font's bytes are checked once
/// while they live: loading a set again, or the same file as a face and as a fallback, reuses the result.
[[nodiscard]] std::size_t fontCheckCount();

/// The face that replaces the type scale's faces in the current scope (see FontFaceScope); nullptr: none.
[[nodiscard]] ImFont* faceOverride();
/// Sets the face override; returns the previous one, to restore.
ImFont* setFaceOverride(ImFont* face);

} // namespace imlui

module :private;

namespace imlui {

// checkFont() checks the glyphs of code points up to font_check's lastCodepoint (0xFFFF), the most a 16-bit ImWchar
// can ask for. With IMGUI_USE_WCHAR32, ImGui would reach glyphs beyond it that were never checked: raise
// lastCodepoint to 0x10FFFF before changing this.
static_assert(sizeof(ImWchar) == 2 && lastCodepoint == 0xFFFF, "font_check's lastCodepoint assumes a 16-bit ImWchar");

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
    /// The bytes the atlas font points into (FontDataOwnedByAtlas = false): kept alive with it. While they are, a
    /// newer read of a changed file can't land at the same address, so `sources` tell the versions apart.
    std::vector<ResourceData> data;
    /// The useFontSet() call (`setsUsed`) that last needed it, or that a loadFontSet() since needed it for.
    std::uint64_t lastUse = 0;
};

const Resources* library = nullptr;
std::map<std::string, Entry, std::less<>> cache; ///< by cacheKey()
std::vector<Entry> replaced;                     ///< removed from the atlas (and their bytes released) at useFontSet()
ImFont* defaultFont = nullptr;                   ///< ImGui's own, when the built-in Regular face is missing
/// How many times useFontSet() ran since the last resetFontLibrary(): what Entry::lastUse counts in.
std::uint64_t setsUsed = 0;

ImFont* currentRegular = nullptr;
ImFont* currentMedium = nullptr;
std::map<std::string, ImFont*, std::less<>> currentNamed;
ImFont* currentOverride = nullptr;
std::set<std::string> loggedErrors; ///< useFontSet() logs each failure once

/// checkFont()'s verdict on bytes that are still alive. Keyed by their address and size: while they live, no other
/// bytes can be at that address.
struct CheckedFont {
    std::weak_ptr<const void> owner; ///< the bytes' owner; once it expired, the address may hold other bytes
    bool owned = false;              ///< false: memory of an immutable mount, valid for as long as it is mounted
    std::optional<std::string> problem;
};
std::map<std::pair<const std::byte*, std::size_t>, CheckedFont> checked;
std::size_t checks = 0;

/// checkFont(bytes), once per bytes while they live (a font is checked again when its file changed).
std::optional<std::string> checkOnce(const ResourceData& bytes) {
    std::erase_if(checked, [](const auto& e) { return e.second.owned && e.second.owner.expired(); });
    const std::pair key{bytes.data(), bytes.size()};
    if (const auto it = checked.find(key); it != checked.end()) return it->second.problem;
    ++checks;
    auto problem = checkFont(bytes.bytes());
    checked.emplace(key, CheckedFont{bytes.owner(), bytes.owner() != nullptr, problem});
    return problem;
}

std::string cacheKey(std::string_view name, std::span<const std::string> fallback) {
    std::string key{name};
    for (const auto& f : fallback)
        key.append("\n").append(f);
    return key;
}

/// Reads font `name`; nullopt and a message in `error` when it is invalid, missing, not a font or damaged.
std::optional<ResourceData> readFont(std::string_view name, std::string& error) {
    const auto canonical = normalizeName(name);
    if (!canonical) {
        error = "invalid font resource name '" + std::string{name} + "' (expected e.g. 'app:/fonts/Inter.ttf')";
        return std::nullopt;
    }
    // `system:/fonts/...`: a font installed with the OS (imlui.theme.system_fonts), not an app resource.
    auto bytes = isSystemFont(*canonical) ? readSystemFont(*canonical) : library->read(*canonical);
    if (!bytes || bytes->empty()) {
        error = "font '" + *canonical + "' not found";
        return std::nullopt;
    }
    // stb_truetype trusts the data and aborts (or reads out of bounds) on a damaged font.
    if (const auto problem = checkOnce(*bytes)) {
        error = *problem == notAFont ? "'" + *canonical + "' is not a TrueType/OpenType font"
                                     : "font '" + *canonical + "' is damaged: " + *problem;
        return std::nullopt;
    }
    return bytes;
}

ImFont* addFont(const ResourceData& bytes, ImFont* mergeInto) {
    ImFontConfig cfg;
    // The cache entry keeps the bytes' handle as long as the atlas font, so the atlas needn't own a copy. ImGui
    // only reads them, but its API takes a non-const pointer.
    cfg.FontDataOwnedByAtlas = false;
    if (mergeInto) {
        cfg.MergeMode = true;
        cfg.DstFont = mergeInto;
    }
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(bytes.data()),
                                                      static_cast<int>(bytes.size()), 16.0f, &cfg);
}

/// A font set's fallbacks, read once for all of its faces (each face merges every fallback in).
struct Fallbacks {
    std::span<const std::string> names;
    std::vector<ResourceData> bytes; ///< one per name; empty when it can't be read
    std::vector<std::string> errors; ///< "fallback: <why>" for each that can't be read
};

Fallbacks readFallbacks(const FontSet& set) {
    Fallbacks f;
    f.names = set.fallback;
    if (!library) return f; // fontFor() loads nothing then
    for (const auto& name : set.fallback) {
        std::string error;
        auto bytes = readFont(name, error);
        if (!bytes) f.errors.push_back("fallback: " + error);
        f.bytes.push_back(bytes ? std::move(*bytes) : ResourceData{});
    }
    return f;
}

/// The atlas font for face `name` with `fallback` merged in, loading or reloading it as needed; nullptr when the
/// face itself can't be loaded, with the reason in `error`. Fallbacks that can't be read are left out (reported
/// once, in Fallbacks::errors).
ImFont* fontFor(std::string_view name, const Fallbacks& fallback, std::string& error) {
    if (!library) return nullptr;
    auto face = readFont(name, error);
    if (!face) return nullptr;
    Entry entry;
    entry.sources.push_back({face->data(), face->size()});
    entry.data.push_back(std::move(*face));
    for (const ResourceData& bytes : fallback.bytes) {
        entry.sources.push_back({bytes.data(), bytes.size()});
        entry.data.push_back(bytes);
    }

    const std::string key = cacheKey(name, fallback.names);
    entry.lastUse = setsUsed;
    auto it = cache.find(key);
    if (it != cache.end()) {
        if (it->second.sources == entry.sources) {
            it->second.lastUse = setsUsed;
            return it->second.font;
        }
        // Its file changed: build a new font. The old one may be pushed in this frame; it goes at useFontSet().
        replaced.push_back(std::move(it->second));
        cache.erase(it);
    }
    entry.font = addFont(entry.data.front(), nullptr);
    if (!entry.font) {
        error = "font '" + std::string{name} + "' could not be loaded";
        return nullptr;
    }
    for (std::size_t i = 1; i < entry.data.size(); ++i)
        if (!entry.data[i].empty()) addFont(entry.data[i], entry.font);
    ImFont* font = entry.font;
    cache.emplace(key, std::move(entry));
    return font;
}

/// Loads the face of `role` ("regular", "medium" or a named font) from `name`, adding a message to `errors` on
/// failure.
ImFont* loadFace(std::string_view role, std::string_view name, const Fallbacks& fallback,
                 std::vector<std::string>& errors) {
    std::string error;
    ImFont* font = fontFor(name, fallback, error);
    if (!font) errors.push_back(std::string{role} + ": " + error);
    return font;
}

/// The type scale face `role` declared as `name` (empty: the built-in `builtin`), falling back to the built-in
/// face. Failures of the built-in face are logged, not added to `errors`: they are the library's, not the script's.
ImFont* typeScaleFace(std::string_view role, const std::string& name, std::string_view builtin,
                      const Fallbacks& fallback, std::vector<std::string>& errors) {
    if (!name.empty())
        if (ImFont* f = loadFace(role, name, fallback, errors)) return f;
    std::string error;
    if (ImFont* f = fontFor(builtin, fallback, error)) return f;
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
    setsUsed = 0;
    currentRegular = currentMedium = currentOverride = nullptr;
    currentNamed.clear();
    loggedErrors.clear();
    checked.clear(); // the next resources' mounts may reuse the memory
    checks = 0;
}

std::vector<std::string> loadFontSet(const FontSet& set) {
    Fallbacks fallback = readFallbacks(set);
    std::vector<std::string> errors = std::move(fallback.errors);
    typeScaleFace("regular", set.regular, builtinRegularFont, fallback, errors);
    typeScaleFace("medium", set.medium, builtinMediumFont, fallback, errors);
    for (const auto& [name, resource] : set.named)
        loadFace(name, resource, fallback, errors);
    return errors;
}

void useFontSet(const FontSet& set) {
    if (!library) return;
    ++setsUsed; // every font `set` needs is marked with this call below (fontFor())
    Fallbacks fallback = readFallbacks(set);
    std::vector<std::string> errors = std::move(fallback.errors);
    currentRegular = typeScaleFace("regular", set.regular, builtinRegularFont, fallback, errors);
    currentMedium = typeScaleFace("medium", set.medium, builtinMediumFont, fallback, errors);
    if (!currentRegular) {
        if (!defaultFont) defaultFont = ImGui::GetIO().Fonts->AddFontDefault();
        currentRegular = defaultFont;
    }
    if (!currentMedium) currentMedium = currentRegular;
    currentNamed.clear();
    for (const auto& [name, resource] : set.named)
        currentNamed.emplace(name, loadFace(name, resource, fallback, errors));
    for (auto& e : errors)
        if (loggedErrors.insert(e).second) log::warn("theme.fonts.{}", e);
    ImGui::GetIO().FontDefault = currentRegular;

    // Fonts no set needs any more: each theme with other faces or fallbacks added atlas fonts (and kept their
    // bytes) for good, so cycling through themes grew the atlas and the memory until the next resetFontLibrary().
    // The fonts of the set used before this one stay, so switching back and forth between two themes (light and
    // dark, a theme picker) reuses them instead of reading, checking and rasterizing them again; so do the fonts a
    // loadFontSet() needed since that set was made current, which a theme still pending (set but not yet applied)
    // will use. An entry not needed by this call nor the one before it goes.
    for (auto it = cache.begin(); it != cache.end();) {
        if (it->second.lastUse + 1 < setsUsed) {
            replaced.push_back(std::move(it->second));
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
    // Between frames nothing is pushed (the face override is a FontFaceScope's, inside a frame), no widget keeps a
    // font across frames, and the faces in use were just looked up again, so the fonts replaced by newer bytes and
    // the ones no set needs are unused now.
    for (const Entry& e : replaced)
        ImGui::GetIO().Fonts->RemoveFont(e.font);
    replaced.clear(); // and with them the last handles to their bytes, unless another font shares them
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

std::size_t fontCheckCount() { return checks; }

ImFont* faceOverride() { return currentOverride; }
ImFont* setFaceOverride(ImFont* face) { return std::exchange(currentOverride, face); }

} // namespace imlui
