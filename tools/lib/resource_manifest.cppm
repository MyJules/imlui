// Resource manifests: a Lua file (by convention `resources.lua`) that says which files a resource package holds and
// where they're mounted. imlui_rc (imlui_add_resources) and imlui_pack (imlui_add_pak) read it. It returns one group
// or a list of groups:
//
//     return {
//         prefix = "app",                     -- required: `app`, `imlui`, a library's own
//         mount = "ui",                       -- optional folder under the prefix: app:/ui/
//         base = ".",                         -- the folder paths are relative to, relative to the manifest
//         files = { "main.lua" },             -- explicit files
//         include = { "**/*.lua", "*.png" },  -- glob patterns (imlui.resources.glob), relative to base
//         exclude = { "tests/**" },           -- patterns removed from what `include` matched
//         compress = { ["big.bin"] = false }, -- per file: override the size-based compression rule
//     }
//
// The manifest is evaluated in a sandbox: a fresh Lua state with only the base library, without dofile, loadfile,
// load and collectgarbage (no io, os, require, string, ...), and text chunks only. The manifest file itself is never
// matched by a pattern (list it in `files` to embed it). It may run at most manifestMaxInstructions Lua instructions
// and use at most manifestMaxMemory bytes, so a runaway manifest fails the build instead of hanging it.
export module imlui.tools.resource_manifest;

import std;

import lua;
import imlui.resources.glob;
import imlui.tools.source_files;

export namespace imlui::tools {

/// One group of a manifest: files mounted at one mount point.
struct ResourceGroup {
    std::string mountPoint;        ///< `app:/`, `imlui:/fonts/`
    std::filesystem::path base;    ///< absolute: the folder the paths are relative to (the dev source folder)
    std::vector<SourceFile> files; ///< sorted by path
    // The selection as the manifest wrote it (`files`, `include`, `exclude`), for a dev build's source folder mount
    // to serve the same files (ResourceSelection).
    std::vector<std::string> listed;
    std::vector<std::string> include;
    std::vector<std::string> exclude;
};

/// The fields of one group as the manifest wrote them, before they're checked against the file system.
struct ManifestGroupFields {
    std::string prefix;
    std::string mount;
    std::string base = ".";
    std::vector<std::string> files;
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::map<std::string, bool> compress;
};

/// A parsed manifest.
struct ResourceManifest {
    std::filesystem::path file; ///< the manifest, absolute
    std::vector<ResourceGroup> groups;
    /// Every folder scanned for `include` patterns: adding or removing a file there changes what matches.
    std::vector<std::filesystem::path> scannedFolders;
};

/// Reads and evaluates manifest `file`; nullopt, with the reason in `error`, when it can't be read, fails to run,
/// returns something that isn't a valid manifest, or names a file that doesn't exist or isn't under its base.
[[nodiscard]] std::optional<ResourceManifest> loadManifest(const std::filesystem::path& file, std::string& error);

/// Like loadManifest(), with the manifest's Lua source given: `file` names it (error messages, the folder `base`
/// is relative to, the file patterns never match).
[[nodiscard]] std::optional<ResourceManifest> parseManifest(std::string_view source, const std::filesystem::path& file,
                                                            std::string& error);

/// The first half of parseManifest(): runs the manifest's Lua source in the sandbox and reads the groups it returns,
/// without looking at the file system. `file` names it in error messages. nullopt, with the reason in `error`, when
/// it fails to run or returns something that isn't one group table or a list of them.
[[nodiscard]] std::optional<std::vector<ManifestGroupFields>>
evaluateManifest(std::string_view source, const std::filesystem::path& file, std::string& error);

/// The second half of parseManifest(): checks `groups` (from evaluateManifest()) and selects their files, relative
/// to manifest `file`.
[[nodiscard]] std::optional<ResourceManifest> resolveManifest(std::span<const ManifestGroupFields> groups,
                                                              const std::filesystem::path& file, std::string& error);

/// The most Lua instructions a manifest may run.
inline constexpr int manifestMaxInstructions = 10'000'000;
/// The most memory (bytes) a manifest's Lua state may use.
inline constexpr std::size_t manifestMaxMemory = std::size_t{64} << 20;

/// What a build of the manifest depends on: the manifest, every file of every group and every scanned folder.
[[nodiscard]] std::vector<std::filesystem::path> manifestDependencies(const ResourceManifest& manifest);

/// Every file of the manifest as `<mount point><path> <source file>`, one per line: it changes exactly when a file
/// is added, removed or moved.
[[nodiscard]] std::string manifestListing(const ResourceManifest& manifest);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

namespace {

using GroupFields = ManifestGroupFields;

using StatePtr = std::unique_ptr<lua::State, void (*)(lua::State*)>;

/// Whether `s` is well-formed UTF-8 (no overlong forms, surrogates or code points past U+10FFFF). The strings become
/// paths, and converting one that isn't throws on Windows.
bool validUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(s[at]); };
        const unsigned lead = byte(i);
        std::size_t length = 0;
        unsigned low = 0x80, high = 0xBF; // the allowed range of the second byte
        if (lead < 0x80) {
            length = 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            if (lead == 0xE0) low = 0xA0;  // overlong
            if (lead == 0xED) high = 0x9F; // surrogates
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            if (lead == 0xF0) low = 0x90;  // overlong
            if (lead == 0xF4) high = 0x8F; // past U+10FFFF
        } else {
            return false;
        }
        if (length > s.size() - i) return false;
        for (std::size_t k = 1; k < length; ++k) {
            const unsigned b = byte(i + k);
            if (k == 1 ? (b < low || b > high) : (b < 0x80 || b > 0xBF)) return false;
        }
        i += length;
    }
    return true;
}

bool readString(lua::State* state, std::string& out) {
    if (lua::type(state, -1) != lua::Type::String) return false;
    out = lua::tostring(state, -1);
    return true;
}

/// A string or a list of strings at the top of the stack.
bool readStrings(lua::State* state, std::vector<std::string>& out) {
    if (lua::type(state, -1) == lua::Type::String) {
        out.emplace_back(lua::tostring(state, -1));
        return true;
    }
    if (lua::type(state, -1) != lua::Type::Table) return false;
    const int t = lua::absindex(state, -1);
    const lua::Integer n = lua::rawlen(state, t);
    lua::Integer entries = 0;
    lua::pushnil(state);
    while (lua::next(state, t) != 0) {
        ++entries;
        lua::pop(state);
    }
    if (entries != n) return false; // keys other than 1..n
    for (lua::Integer i = 1; i <= n; ++i) {
        const bool isString = lua::rawgeti(state, t, i) == lua::Type::String;
        if (isString) out.emplace_back(lua::tostring(state, -1));
        lua::pop(state);
        if (!isString) return false;
    }
    return true;
}

/// Reads the group table at `t`. Only raw accesses: a metatable can't run code while C++ reads the result.
bool readGroup(lua::State* state, int t, const std::string& where, GroupFields& group, std::string& error) {
    bool hasPrefix = false;
    lua::pushnil(state);
    while (lua::next(state, t) != 0) {
        if (lua::type(state, -2) != lua::Type::String) {
            error = where + "fields are named (`prefix = ...`), found a " + lua::typeName(state, -2) + " key";
            return false;
        }
        const std::string key{lua::tostring(state, -2)};
        bool ok = true;
        if (key == "prefix")
            ok = hasPrefix = readString(state, group.prefix);
        else if (key == "mount")
            ok = readString(state, group.mount);
        else if (key == "base")
            ok = readString(state, group.base);
        else if (key == "files")
            ok = readStrings(state, group.files);
        else if (key == "include")
            ok = readStrings(state, group.include);
        else if (key == "exclude")
            ok = readStrings(state, group.exclude);
        else if (key == "compress") {
            ok = lua::type(state, -1) == lua::Type::Table;
            const int c = lua::absindex(state, -1);
            lua::pushnil(state);
            while (ok && lua::next(state, c) != 0) {
                ok = lua::type(state, -2) == lua::Type::String && lua::type(state, -1) == lua::Type::Boolean;
                if (ok) group.compress[std::string{lua::tostring(state, -2)}] = lua::toboolean(state, -1);
                lua::pop(state);
            }
            if (!ok) {
                error = where + "'compress' must map file paths to true or false";
                return false;
            }
        } else {
            error = where + "unknown field '" + key +
                    "' (fields: prefix, mount, base, files, include, exclude, "
                    "compress)";
            return false;
        }
        if (!ok) {
            const bool list = key == "files" || key == "include" || key == "exclude";
            error = where + "'" + key + "' must be a string" + (list ? " or a list of strings" : "") + ", not a " +
                    lua::typeName(state, -1);
            return false;
        }
        lua::pop(state);
    }
    if (!hasPrefix) {
        error = where + "'prefix' is required (`prefix = \"app\"`)";
        return false;
    }
    // Every string names a path or a mount point.
    const auto invalid = [&](std::string_view field) {
        error = where + "'" + std::string{field} + "' isn't valid UTF-8";
        return false;
    };
    for (const auto& [field, value] :
         {std::pair{"prefix", &group.prefix}, std::pair{"mount", &group.mount}, std::pair{"base", &group.base}})
        if (!validUtf8(*value)) return invalid(field);
    for (const auto& [field, list] :
         {std::pair{"files", &group.files}, std::pair{"include", &group.include}, std::pair{"exclude", &group.exclude}})
        for (const auto& value : *list)
            if (!validUtf8(value)) return invalid(field);
    for (const auto& [path, compress] : group.compress)
        if (!validUtf8(path)) return invalid("compress");
    return true;
}

/// The manifest's Lua allocator: at most manifestMaxMemory bytes in all (Lua then raises "not enough memory").
struct MemoryBudget {
    std::size_t used = 0;
};

void* budgetAlloc(void* ud, void* ptr, std::size_t oldSize, std::size_t newSize) {
    auto& budget = *static_cast<MemoryBudget*>(ud);
    if (!ptr) oldSize = 0; // a new block: oldSize is the object's type
    if (newSize == 0) {
        std::free(ptr);
        budget.used -= oldSize;
        return nullptr;
    }
    if (newSize > oldSize && newSize - oldSize > manifestMaxMemory - budget.used) return nullptr;
    void* block = std::realloc(ptr, newSize);
    if (block) budget.used = budget.used - oldSize + newSize;
    return block;
}

/// Runs once the manifest has run manifestMaxInstructions instructions.
void tooLong(lua::State* state, lua::Debug*) {
    // A literal: lua::error longjmps, so nothing here may need destroying.
    static_assert(manifestMaxInstructions == 10'000'000);
    lua::error(state, "the manifest runs too long (more than 10000000 Lua instructions: an endless loop?)");
}

} // namespace

std::optional<std::vector<GroupFields>> evaluateManifest(std::string_view source, const fs::path& file,
                                                         std::string& error) {
    MemoryBudget budget;
    const StatePtr owner{lua::newstate(budgetAlloc, &budget), lua::close};
    lua::State* state = owner.get();
    if (!state) {
        error = "can't create a Lua state";
        return std::nullopt;
    }
    lua::sethook(state, tooLong, lua::maskCount, manifestMaxInstructions);
    lua::openbase(state);
    for (const char* unsafe : {"dofile", "loadfile", "load", "collectgarbage"}) {
        lua::pushnil(state);
        lua::setglobal(state, unsafe);
    }
    const std::string chunkName = "@" + utf8(file);
    // "t": text only, so a manifest can't smuggle in bytecode.
    if (lua::loadbufferx(state, source, chunkName.c_str(), "t") != lua::ok || lua::pcall(state, 0, 1, 0) != lua::ok) {
        error =
            lua::type(state, -1) == lua::Type::String ? std::string{lua::tostring(state, -1)} : "the manifest failed";
        return std::nullopt;
    }
    const std::string where = utf8(file) + ": ";
    if (lua::type(state, -1) != lua::Type::Table) {
        error = where + "must return a group table or a list of them, not a " + lua::typeName(state, -1);
        return std::nullopt;
    }
    const int t = lua::absindex(state, -1);
    std::vector<GroupFields> groups;
    const lua::Integer n = lua::rawlen(state, t);
    if (n == 0) {
        if (!readGroup(state, t, where, groups.emplace_back(), error)) return std::nullopt;
        return groups;
    }
    lua::pushnil(state);
    while (lua::next(state, t) != 0) {
        if (lua::type(state, -2) != lua::Type::Number || !lua::isinteger(state, -2) || lua::tointeger(state, -2) < 1 ||
            lua::tointeger(state, -2) > n) {
            error = where + "a list of groups can't have other fields (a group and a list mixed?)";
            return std::nullopt;
        }
        lua::pop(state);
    }
    for (lua::Integer i = 1; i <= n; ++i) {
        const std::string groupWhere = where + "group " + std::to_string(i) + ": ";
        if (lua::rawgeti(state, t, i) != lua::Type::Table) {
            error = groupWhere + "must be a table, not a " + lua::typeName(state, -1);
            return std::nullopt;
        }
        if (!readGroup(state, lua::absindex(state, -1), groupWhere, groups.emplace_back(), error)) return std::nullopt;
        lua::pop(state);
    }
    return groups;
}

namespace {

bool isPrefix(std::string_view prefix) {
    if (prefix.size() < 2 || prefix.front() < 'a' || prefix.front() > 'z') return false;
    return std::ranges::all_of(
        prefix, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}

bool hasDotDot(std::string_view path) {
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = path.find('/', start);
        if (path.substr(start, slash - start) == "..") return true;
        if (slash == std::string_view::npos) return false;
        start = slash + 1;
    }
}

/// Folders to scan for `patterns`: their roots, without those inside another root.
std::vector<std::string> scanRoots(const std::vector<std::string>& patterns) {
    std::vector<std::string> roots;
    for (const auto& pattern : patterns)
        roots.push_back(globRoot(pattern));
    std::ranges::sort(roots);
    std::vector<std::string> out;
    for (const auto& root : roots) {
        const bool inside = std::ranges::any_of(
            out, [&](const std::string& kept) { return kept.empty() || root == kept || root.starts_with(kept + "/"); });
        if (!inside) out.push_back(root);
    }
    return out;
}

bool matchesAny(const std::vector<std::string>& patterns, std::string_view path) {
    return std::ranges::any_of(patterns, [&](const std::string& pattern) { return globMatch(pattern, path); });
}

bool equalIgnoringAsciiCase(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::equal(a, b, {}, lower, lower);
}

/// How existing file `path` (relative to `base`) is spelled on disk when that differs from `path`, else "". On a file
/// system that ignores case (Windows, macOS) `Main.lua` finds `main.lua`, but the resource would be named `Main.lua`,
/// which no mount finds (resource names are case-sensitive).
std::string spellingOnDisk(const fs::path& base, std::string_view path) {
    fs::path folder = base;
    std::string onDisk;
    bool differs = false;
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = path.find('/', start);
        const std::string segment{path.substr(start, slash - start)};
        std::string found;
        std::error_code ec;
        for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            const std::string name = utf8(it->path().filename());
            if (name == segment) {
                found = name;
                break;
            }
            if (found.empty() && equalIgnoringAsciiCase(name, segment)) found = name;
        }
        if (found.empty()) found = segment; // can't tell (unlisted, or a difference beyond ASCII case)
        differs = differs || found != segment;
        onDisk += (onDisk.empty() ? "" : "/") + found;
        if (slash == std::string_view::npos) break;
        folder /= pathFromUtf8(found);
        start = slash + 1;
    }
    return differs ? onDisk : std::string{};
}

/// Resolves one group's fields against the file system.
std::optional<ResourceGroup> resolveGroup(const GroupFields& fields, const fs::path& manifest, const std::string& where,
                                          std::vector<fs::path>& scanned, std::string& error) {
    if (!isPrefix(fields.prefix)) {
        error = where + "prefix '" + fields.prefix +
                "' must be lowercase letters, digits, '_' or '-', at least 2 "
                "characters, starting with a letter";
        return std::nullopt;
    }
    ResourceGroup group;
    group.mountPoint = fields.prefix + ":/";
    std::string mount = fields.mount;
    while (mount.starts_with('/'))
        mount.erase(0, 1);
    while (mount.ends_with('/'))
        mount.pop_back();
    if (!mount.empty()) {
        if (const std::string problem = globProblem(mount);
            !problem.empty() || mount.find_first_of("*?") != std::string::npos) {
            error = where + "mount '" + fields.mount + "' isn't a plain folder" +
                    (problem.empty() ? std::string{} : " (" + problem + ")");
            return std::nullopt;
        }
        group.mountPoint += mount + "/";
    }

    std::error_code ec;
    const fs::path baseInput = pathFromUtf8(fields.base);
    group.base = fs::weakly_canonical(manifest.parent_path() / baseInput, ec);
    if (ec || !fs::is_directory(group.base, ec)) {
        error = where + "base folder '" + fields.base + "' doesn't exist (" + utf8(manifest.parent_path() / baseInput) +
                ")";
        return std::nullopt;
    }
    if (fields.files.empty() && fields.include.empty()) {
        error = where + "selects no files: give 'files' and/or 'include'";
        return std::nullopt;
    }

    std::map<std::string, SourceFile> selected;
    for (const auto& path : fields.files) {
        const std::string problem = globProblem(path);
        if (hasDotDot(path)) {
            error = where + "'" + path + "' is outside the base folder";
            return std::nullopt;
        }
        if (!problem.empty()) {
            error = where + "file '" + path + "' isn't a relative path (" + problem + ")";
            return std::nullopt;
        }
        const fs::path file = fs::weakly_canonical(group.base / pathFromUtf8(path), ec);
        if (ec || !fs::is_regular_file(file, ec)) {
            error = where + "file '" + path + "' not found in " + utf8(group.base);
            return std::nullopt;
        }
        // A link may still lead out of the base folder.
        if (leavesBase(file.lexically_relative(group.base))) {
            error = where + "'" + path + "' is outside the base folder";
            return std::nullopt;
        }
        if (const std::string spelled = spellingOnDisk(group.base, path); !spelled.empty()) {
            error = where + "file '" + path + "' is spelled '" + spelled +
                    "' on disk (resource names are case-sensitive: use the same spelling)";
            return std::nullopt;
        }
        selected[path] = {path, file, std::nullopt};
    }

    for (const auto* patterns : {&fields.include, &fields.exclude})
        for (const auto& pattern : *patterns)
            if (const std::string problem = globProblem(pattern); !problem.empty()) {
                error = where + "pattern '" + pattern + "' has " + problem;
                return std::nullopt;
            }
    const fs::path self = fs::weakly_canonical(manifest, ec);
    for (const auto& root : scanRoots(fields.include)) {
        const fs::path folder = root.empty() ? group.base : group.base / pathFromUtf8(root);
        if (!fs::is_directory(folder, ec)) continue;
        scanned.push_back(folder);
        for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            // Its own error code: an entry that can't be queried (a link to nothing) isn't a file, and the walk goes
            // on.
            std::error_code entryEc;
            if (it->is_directory(entryEc)) {
                scanned.push_back(it->path());
                continue;
            }
            if (!it->is_regular_file(entryEc) || it->path() == self) continue;
            const std::string path = utf8(it->path().lexically_relative(group.base));
            if (!matchesAny(fields.include, path) || matchesAny(fields.exclude, path) || selected.contains(path))
                continue;
            // A matched link may lead out of the base folder, like a `files` entry.
            std::error_code linkEc;
            const fs::path target = fs::weakly_canonical(it->path(), linkEc);
            if (linkEc || leavesBase(target.lexically_relative(group.base))) {
                error = where + "'" + path + "' is outside the base folder";
                return std::nullopt;
            }
            selected[path] = {path, it->path(), std::nullopt};
        }
        if (ec) {
            error = where + "can't list " + utf8(folder) + ": " + ec.message();
            return std::nullopt;
        }
    }

    for (const auto& [path, compress] : fields.compress) {
        const auto it = selected.find(path);
        if (it == selected.end()) {
            error = where + "compress: '" + path + "' isn't one of the group's files";
            return std::nullopt;
        }
        it->second.compress = compress;
    }
    for (auto& [path, file] : selected) {
        if (path.find_first_of(":\\") != std::string::npos) {
            error = where + "'" + path + "': resource paths can't contain ':' or '\\'";
            return std::nullopt;
        }
        group.files.push_back(std::move(file));
    }
    group.listed = fields.files;
    group.include = fields.include;
    group.exclude = fields.exclude;
    return group;
}

/// `file` absolute and normalized, as the manifest's chunk name and the base of its folders.
fs::path manifestPath(const fs::path& file) {
    std::error_code ec;
    return fs::absolute(file, ec).lexically_normal();
}

} // namespace

std::optional<ResourceManifest> resolveManifest(std::span<const GroupFields> groups, const fs::path& file,
                                                std::string& error) {
    ResourceManifest manifest;
    manifest.file = manifestPath(file);
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const std::string where =
            utf8(manifest.file) + ": " + (groups.size() > 1 ? "group " + std::to_string(i + 1) + ": " : std::string{});
        auto group = resolveGroup(groups[i], manifest.file, where, manifest.scannedFolders, error);
        if (!group) return std::nullopt;
        manifest.groups.push_back(std::move(*group));
    }
    std::ranges::sort(manifest.scannedFolders);
    const auto [first, last] = std::ranges::unique(manifest.scannedFolders);
    manifest.scannedFolders.erase(first, last);
    return manifest;
}

std::optional<ResourceManifest> parseManifest(std::string_view source, const fs::path& file, std::string& error) {
    const fs::path absolute = manifestPath(file);
    const auto groups = evaluateManifest(source, absolute, error);
    if (!groups) return std::nullopt;
    return resolveManifest(*groups, absolute, error);
}

std::optional<ResourceManifest> loadManifest(const fs::path& file, std::string& error) {
    const auto bytes = readFile(file);
    if (!bytes) {
        error = "can't read manifest '" + utf8(file) + "'";
        return std::nullopt;
    }
    const std::string source{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    return parseManifest(source, file, error);
}

std::vector<fs::path> manifestDependencies(const ResourceManifest& manifest) {
    std::vector<fs::path> out{manifest.file};
    for (const auto& group : manifest.groups)
        for (const auto& file : group.files)
            out.push_back(file.file);
    out.insert(out.end(), manifest.scannedFolders.begin(), manifest.scannedFolders.end());
    return out;
}

std::string manifestListing(const ResourceManifest& manifest) {
    std::string out;
    for (const auto& group : manifest.groups)
        for (const auto& file : group.files)
            out += group.mountPoint + file.path + " " + utf8(file.file) + '\n';
    return out;
}

} // namespace imlui::tools
