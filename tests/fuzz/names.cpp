// Fuzz target `names`: resource names (imlui.resources.name) and glob patterns (imlui.resources.glob), the strings a
// script or a manifest controls. The input is split at its first newline into `a` and `b`: a name or path, and a base
// or path. Results must be consistent: what normalizes is a fixed point, a parsed name round-trips, a resolved name is
// normalized, and globMatch agrees with a direct backtracking matcher on short inputs and with plain comparison on
// patterns without wildcards. Nothing may crash or hang.
import std;

import imlui.resources.glob;
import imlui.resources.name;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

void check(bool ok) {
    if (!ok) std::abort();
}

/// The glob rules as plain backtracking (globMatch before it became a table: exponential, so only for short inputs),
/// which globMatch must agree with. A pattern it recurses into starts at a segment start or right after a run of `*`.
bool reference(std::string_view pattern, std::string_view path) {
    std::size_t p = 0;
    std::size_t s = 0;
    while (p < pattern.size()) {
        if (pattern.compare(p, 2, "**") == 0) {
            std::size_t rest = p + 2;
            const bool segment = (p == 0 || pattern[p - 1] == '/') && rest < pattern.size() && pattern[rest] == '/';
            if (segment && reference(pattern.substr(rest + 1), path.substr(s))) return true;
            while (rest < pattern.size() && pattern[rest] == '*')
                ++rest;
            for (std::size_t i = s; i <= path.size(); ++i)
                if (reference(pattern.substr(rest), path.substr(i))) return true;
            return false;
        }
        if (pattern[p] == '*') {
            for (std::size_t i = s; i <= path.size(); ++i) {
                if (reference(pattern.substr(p + 1), path.substr(i))) return true;
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

void names(std::string_view a, std::string_view b) {
    if (const auto path = imlui::normalizePath(a)) {
        check(imlui::normalizePath(*path) == *path);
        check(!path->starts_with('/') && !path->ends_with('/') && path->find("//") == std::string::npos);
    }
    if (const auto name = imlui::parseName(a)) {
        const std::string full = name->full();
        check(imlui::normalizeName(full) == full);
        check(imlui::parseName(full) == *name);
        check(imlui::hasPrefix(full));
        const auto folder = imlui::folderOf(full);
        check(folder && folder->ends_with('/') && full.starts_with(*folder));
    } else {
        check(!imlui::normalizeName(a));
    }
    if (const auto resolved = imlui::resolveName(a, b)) check(imlui::normalizeName(*resolved) == *resolved);
}

void globs(std::string_view pattern, std::string_view path) {
    const bool matched = imlui::globMatch(pattern, path);
    if (pattern.find_first_of("*?") == std::string_view::npos) check(matched == (pattern == path));
    if (pattern.size() <= 12 && path.size() <= 12) check(matched == reference(pattern, path));
    if (imlui::globProblem(pattern).empty()) check(pattern.starts_with(imlui::globRoot(pattern)));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view input{reinterpret_cast<const char*>(data), size};
    const std::size_t newline = input.find('\n');
    const std::string_view a = input.substr(0, newline);
    const std::string_view b = newline == std::string_view::npos ? std::string_view{} : input.substr(newline + 1);
    names(a, b);
    names(b, a);
    globs(a, b);
    return 0;
}
