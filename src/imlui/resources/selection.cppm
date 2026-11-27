// ResourceSelection: which files of a folder belong to a resource package, as a manifest group selects them
// (`files`, `include`, `exclude`). A dev build mounts a package's source folder with its selection, so the folder
// serves exactly what the embedded copy or a .pak would hold, and a file added later that matches a pattern is served
// at once (hot reload), as the next build would embed it.
export module imlui.resources.selection;

import std;

import imlui.resources.glob;

export namespace imlui {

/// A manifest group's selection, with paths relative to the group's `base` folder (`/`-separated, normalized).
struct ResourceSelection {
    std::vector<std::string> files = {};   ///< always selected (`files = {...}`)
    std::vector<std::string> include = {}; ///< glob patterns (imlui.resources.glob)
    std::vector<std::string> exclude = {}; ///< patterns removed from what `include` matches
    /// Paths no pattern matches: the manifest itself, when it lies in the folder (still selected when in `files`).
    std::vector<std::string> unmatched = {};

    /// True when file `path` is selected: listed in `files`, or matched by `include` and neither `exclude` nor
    /// `unmatched`. Searches `files` and `unmatched` from end to end; a mount asking for every file of a folder keeps
    /// them sorted and uses matchesPatterns() (DirectoryMount).
    [[nodiscard]] bool contains(std::string_view path) const;
    /// True when `path` is matched by `include` and not by `exclude` (`files` and `unmatched` aren't looked at).
    [[nodiscard]] bool matchesPatterns(std::string_view path) const;
    /// False when no selected file can lie under folder `folder` ("" is the root): no listed file is in it and no
    /// `include` pattern can match there. True doesn't mean one does.
    [[nodiscard]] bool mayContainUnder(std::string_view folder) const;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// The `/`-separated segments of `path`.
std::vector<std::string_view> segments(std::string_view path) {
    std::vector<std::string_view> out;
    for (std::size_t start = 0;;) {
        const std::size_t slash = path.find('/', start);
        out.push_back(path.substr(start, slash - start));
        if (slash == std::string_view::npos) return out;
        start = slash + 1;
    }
}

/// False when no path under `folder` can match `pattern`: one of the folder's segments doesn't match the pattern's
/// segment at its place, or the pattern has no folder segment left there (its last segment is the file name). A
/// segment with `**` can match any number of folders, so from there on anything may match.
bool mayMatchUnder(std::string_view pattern, const std::vector<std::string_view>& folder) {
    const std::vector<std::string_view> parts = segments(pattern);
    for (std::size_t i = 0; i < folder.size(); ++i) {
        if (i < parts.size() && parts[i].find("**") != std::string_view::npos) return true;
        if (i + 1 >= parts.size() || !globMatch(parts[i], folder[i])) return false;
    }
    return true;
}

} // namespace

bool ResourceSelection::contains(std::string_view path) const {
    if (std::ranges::find(files, path) != files.end()) return true;
    return matchesPatterns(path) && std::ranges::find(unmatched, path) == unmatched.end();
}

bool ResourceSelection::matchesPatterns(std::string_view path) const {
    const auto matches = [&](const std::string& pattern) { return globMatch(pattern, path); };
    return std::ranges::any_of(include, matches) && !std::ranges::any_of(exclude, matches);
}

bool ResourceSelection::mayContainUnder(std::string_view folder) const {
    if (folder.empty()) return !files.empty() || !include.empty();
    const auto inFolder = [&](const std::string& f) {
        return f.size() > folder.size() && f.starts_with(folder) && f[folder.size()] == '/';
    };
    if (std::ranges::any_of(files, inFolder)) return true;
    const std::vector<std::string_view> parts = segments(folder);
    return std::ranges::any_of(include, [&](const std::string& pattern) { return mayMatchUnder(pattern, parts); });
}

} // namespace imlui
