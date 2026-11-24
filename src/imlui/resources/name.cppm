// Resource names: `prefix:/path/to/file.lua`. Parsing, normalization and resolving relative names.
//
// A full name is a prefix (the namespace: `app`, `imlui`, a library's own), `:/`, and a path of `/`-separated
// segments. Normalizing drops empty and `.` segments and resolves `..`; a `..` that would leave the prefix's root,
// a backslash, a `:` inside the path, a NUL or a malformed prefix makes the name invalid. Prefixes are lowercase
// and at least two characters long, so an absolute OS path such as `C:/x` or `/etc/x` is never a resource name.
module;

#include <optional>
#include <string>
#include <string_view>

export module imlui.resources.name;

export namespace imlui {

/// A valid, normalized resource name. `path` has no leading or trailing slash; "" is the prefix's root.
struct ResourceName {
    std::string prefix;
    std::string path;

    /// The canonical full name: `prefix:/path` (`prefix:/` for the root).
    [[nodiscard]] std::string full() const;
    friend bool operator==(const ResourceName&, const ResourceName&) = default;
};

/// Parses and normalizes a full name (`app:/a/./b.lua` -> {"app", "a/b.lua"}); nullopt when it isn't valid.
[[nodiscard]] std::optional<ResourceName> parseName(std::string_view name);

/// The canonical form of a full name, or nullopt when it isn't valid.
[[nodiscard]] std::optional<std::string> normalizeName(std::string_view name);

/// Normalizes a path inside a prefix (`a//./b/../c` -> `a/c`); nullopt when it would leave the root, starts with
/// `/` or holds an unsafe character.
[[nodiscard]] std::optional<std::string> normalizePath(std::string_view path);

/// True when `name` starts with a prefix (`app:`), whether or not the rest is valid.
[[nodiscard]] bool hasPrefix(std::string_view name);

/// Resolves `name` against `base`: a full name is only normalized; a relative one is taken relative to the folder
/// of `base` (a full file name, or a folder name ending in `/`). nullopt when either is invalid or the result
/// would leave the prefix's root.
[[nodiscard]] std::optional<std::string> resolveName(std::string_view base, std::string_view name);

/// The folder of a full name with a trailing slash: `app:/a/b.lua` -> `app:/a/`, `app:/a/` -> `app:/a/`.
/// nullopt when `name` isn't valid.
[[nodiscard]] std::optional<std::string> folderOf(std::string_view name);

} // namespace imlui

module :private;

namespace imlui {

namespace {

bool isPrefixChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }

/// Length of the prefix before `:` when `name` starts with one (letters, digits, `_`, `-`), else 0.
std::size_t prefixLength(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos || colon == 0) return 0;
    for (std::size_t i = 0; i < colon; ++i) {
        const char c = name[i];
        const bool letterOrDigit = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!letterOrDigit && c != '_' && c != '-') return 0;
    }
    return colon;
}

bool isValidPrefix(std::string_view prefix) {
    if (prefix.size() < 2 || !(prefix[0] >= 'a' && prefix[0] <= 'z')) return false;
    for (const char c : prefix)
        if (!isPrefixChar(c)) return false;
    return true;
}

} // namespace

std::string ResourceName::full() const { return prefix + ":/" + path; }

std::optional<std::string> normalizePath(std::string_view path) {
    if (!path.empty() && path.front() == '/') return std::nullopt;
    std::string out;
    std::size_t pos = 0;
    while (pos <= path.size()) {
        std::size_t end = path.find('/', pos);
        if (end == std::string_view::npos) end = path.size();
        const std::string_view segment = path.substr(pos, end - pos);
        pos = end + 1;
        if (segment.empty() || segment == ".") continue;
        if (segment == "..") {
            if (out.empty()) return std::nullopt; // would leave the root
            const std::size_t slash = out.rfind('/');
            out.resize(slash == std::string::npos ? 0 : slash);
            continue;
        }
        for (const char c : segment)
            if (c == '\\' || c == ':' || c == '\0') return std::nullopt;
        if (!out.empty()) out += '/';
        out += segment;
    }
    return out;
}

bool hasPrefix(std::string_view name) { return prefixLength(name) > 0; }

std::optional<ResourceName> parseName(std::string_view name) {
    const std::size_t length = prefixLength(name);
    if (length == 0) return std::nullopt;
    const std::string_view prefix = name.substr(0, length);
    if (!isValidPrefix(prefix)) return std::nullopt;
    std::string_view rest = name.substr(length + 1);
    if (rest.empty() || rest.front() != '/') return std::nullopt;
    while (!rest.empty() && rest.front() == '/')
        rest.remove_prefix(1); // `app://a` is `app:/a`
    std::optional<std::string> path = normalizePath(rest);
    if (!path) return std::nullopt;
    return ResourceName{std::string{prefix}, std::move(*path)};
}

std::optional<std::string> normalizeName(std::string_view name) {
    auto parsed = parseName(name);
    if (!parsed) return std::nullopt;
    return parsed->full();
}

std::optional<std::string> folderOf(std::string_view name) {
    auto parsed = parseName(name);
    if (!parsed) return std::nullopt;
    // A name ending in `/` is a folder already; otherwise drop the last segment.
    if (!name.ends_with('/')) {
        const std::size_t slash = parsed->path.rfind('/');
        parsed->path.resize(slash == std::string::npos ? 0 : slash);
    }
    std::string out = parsed->full();
    if (!out.ends_with('/')) out += '/';
    return out;
}

std::optional<std::string> resolveName(std::string_view base, std::string_view name) {
    if (hasPrefix(name)) return normalizeName(name);
    if (name.empty() || name.front() == '/') return std::nullopt; // absolute OS path, or nothing
    const auto folder = folderOf(base);
    if (!folder) return std::nullopt;
    return normalizeName(*folder + std::string{name});
}

} // namespace imlui
