// Glob patterns for resource manifests (`include`, `exclude`): `*` any characters but `/`, `?` one character but
// `/`, `**` any characters including `/` (as a whole segment, `**/` also matches no folder at all: `**/*.lua`
// matches `main.lua` and `ui/Card.lua`). Everything else matches itself. Patterns and paths are `/`-separated and
// relative.
module;

#include <cstddef>
#include <string>
#include <string_view>

export module imlui.tools.glob;

export namespace imlui::tools {

/// True when `path` matches `pattern` as a whole.
[[nodiscard]] bool globMatch(std::string_view pattern, std::string_view path);

/// The folder a pattern's matches are all under: its leading segments without a wildcard (`ui/icons` for
/// `ui/icons/*.png`, "" for `*.lua` or `**`), so scanning can start there.
[[nodiscard]] std::string globRoot(std::string_view pattern);

/// "" when `pattern` is a valid relative pattern, otherwise why not: empty, absolute, a `\`, a `:`, or an empty,
/// `.` or `..` segment.
[[nodiscard]] std::string globProblem(std::string_view pattern);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace {

bool hasWildcard(std::string_view segment) { return segment.find_first_of("*?") != std::string_view::npos; }

} // namespace

bool globMatch(std::string_view pattern, std::string_view path) {
    std::size_t p = 0;
    std::size_t s = 0;
    while (p < pattern.size()) {
        if (pattern.compare(p, 2, "**") == 0) {
            std::size_t rest = p + 2;
            // `**/` may also match no folder at all.
            const bool segment = (p == 0 || pattern[p - 1] == '/') && rest < pattern.size() && pattern[rest] == '/';
            if (segment && globMatch(pattern.substr(rest + 1), path.substr(s))) return true;
            while (rest < pattern.size() && pattern[rest] == '*')
                ++rest;
            for (std::size_t i = s; i <= path.size(); ++i)
                if (globMatch(pattern.substr(rest), path.substr(i))) return true;
            return false;
        }
        if (pattern[p] == '*') {
            for (std::size_t i = s; i <= path.size(); ++i) {
                if (globMatch(pattern.substr(p + 1), path.substr(i))) return true;
                if (i < path.size() && path[i] == '/') break;
            }
            return false;
        }
        if (s >= path.size()) return false;
        if (pattern[p] == '?' ? path[s] == '/' : pattern[p] != path[s]) return false;
        ++p;
        ++s;
    }
    return s == path.size();
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

} // namespace imlui::tools
