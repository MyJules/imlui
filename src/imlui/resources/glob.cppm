// Glob patterns for resource manifests (`include`, `exclude`): `*` any characters but `/`, `?` one character but
// `/`, `**` any characters including `/` (as a whole segment, `**/` also matches no folder at all: `**/*.lua`
// matches `main.lua` and `ui/Card.lua`). Everything else matches itself. Patterns and paths are `/`-separated and
// relative. Shared by the resource tools (which select files with them at build time) and the library (a dev build's
// source folder mounts serve only what the manifest selects, ResourceSelection), so it is its own small library,
// imlui_glob (src/CMakeLists.txt), like imlui_lua.
export module imlui.resources.glob;

import std;

export namespace imlui {

/// True when `path` matches `pattern` as a whole.
[[nodiscard]] bool globMatch(std::string_view pattern, std::string_view path);

/// The folder a pattern's matches are all under: its leading segments without a wildcard (`ui/icons` for
/// `ui/icons/*.png`, "" for `*.lua` or `**`), so scanning can start there.
[[nodiscard]] std::string globRoot(std::string_view pattern);

/// "" when `pattern` is a valid relative pattern, otherwise why not: empty, absolute, a `\`, a `:`, or an empty,
/// `.` or `..` segment.
[[nodiscard]] std::string globProblem(std::string_view pattern);

} // namespace imlui

module :private;

namespace imlui {

namespace {

bool hasWildcard(std::string_view segment) { return segment.find_first_of("*?") != std::string_view::npos; }

} // namespace

bool globMatch(std::string_view pattern, std::string_view path) {
    // Dynamic programming over (pattern position, path position), from the ends: O(pattern x path) time, where
    // backtracking over the wildcards took exponential time on patterns like `**a**a**a**b`. at(p, s) is whether
    // pattern[p:] matches path[s:]; row p only needs the rows after it.
    // The table is reused between calls on a thread: a dev build's folder mount matches every file it lists or
    // reads against every pattern of its selection, and a fresh allocation per match was most of the cost.
    const std::size_t width = path.size() + 1;
    thread_local std::vector<char> match;
    match.assign((pattern.size() + 1) * width, 0);
    const auto at = [&](std::size_t p, std::size_t s) -> char& { return match[p * width + s]; };
    at(pattern.size(), path.size()) = 1;
    for (std::size_t p = pattern.size(); p-- > 0;) {
        if (pattern.compare(p, 2, "**") == 0) {
            // Any characters, `/` included. `**/` as a whole segment may also match no folder at all: skip it.
            std::size_t rest = p + 2;
            const bool segment = (p == 0 || pattern[p - 1] == '/') && rest < pattern.size() && pattern[rest] == '/';
            while (rest < pattern.size() && pattern[rest] == '*')
                ++rest;
            bool anyFrom = false; // whether pattern[rest:] matches path[i:] for some i >= s
            for (std::size_t s = width; s-- > 0;) {
                anyFrom = anyFrom || at(rest, s) != 0;
                at(p, s) = static_cast<char>(anyFrom || (segment && at(p + 3, s) != 0));
            }
        } else if (pattern[p] == '*') {
            // Any characters but `/`: none, or one more and still `*`.
            for (std::size_t s = width; s-- > 0;)
                at(p, s) =
                    static_cast<char>(at(p + 1, s) != 0 || (s < path.size() && path[s] != '/' && at(p, s + 1) != 0));
        } else {
            for (std::size_t s = 0; s < path.size(); ++s)
                at(p, s) = static_cast<char>((pattern[p] == '?' ? path[s] != '/' : pattern[p] == path[s]) &&
                                             at(p + 1, s + 1) != 0);
        }
    }
    return at(0, 0) != 0;
}

std::string globRoot(std::string_view pattern) {
    std::string root;
    std::size_t start = 0;
    for (std::size_t slash = pattern.find('/'); slash != std::string_view::npos;
         start = slash + 1, slash = pattern.find('/', start)) {
        const std::string_view segment = pattern.substr(start, slash - start);
        if (hasWildcard(segment)) break;
        if (!root.empty()) root += '/';
        root += segment;
    }
    return root;
}

std::string globProblem(std::string_view pattern) {
    if (pattern.empty()) return "an empty pattern";
    if (pattern.front() == '/') return "an absolute pattern";
    if (pattern.find('\\') != std::string_view::npos) return "a '\\' (patterns use '/')";
    if (pattern.find(':') != std::string_view::npos) return "a ':'";
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = pattern.find('/', start);
        const std::string_view segment = pattern.substr(start, slash - start);
        if (segment.empty() || segment == "." || segment == "..") return "an empty, '.' or '..' segment";
        if (slash == std::string_view::npos) return {};
        start = slash + 1;
    }
}

} // namespace imlui
