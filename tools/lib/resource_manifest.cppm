// Resource manifests: a Lua file (by convention `resources.lua`) that says which files a resource package holds and
// where they're mounted. imlui_rc (imlui_add_resources) and imlui_pack (imlui_add_pak) read it. It returns one group
// or a list of groups:
//
//     return {
//         prefix = "app",                     -- required: `app`, `imlui`, a library's own
//         mount = "ui",                       -- optional folder under the prefix: app:/ui/
//         base = ".",                         -- the folder paths are relative to, relative to the manifest
//         files = { "main.lua" },             -- explicit files
//         include = { "**/*.lua", "*.png" },  -- glob patterns (imlui.tools.glob), relative to base
//         exclude = { "tests/**" },           -- patterns removed from what `include` matched
//         compress = { ["big.bin"] = false }, -- per file: override the size-based compression rule
//     }
//
// The manifest is evaluated in a sandbox: a fresh Lua state with only the base library, without dofile, loadfile,
// load and collectgarbage (no io, os, require, string, ...), and text chunks only. The manifest file itself is never
// matched by a pattern (list it in `files` to embed it).
export module imlui.tools.resource_manifest;

import std;

import lua;
import imlui.tools.glob;
import imlui.tools.source_files;

export namespace imlui::tools {

/// One group of a manifest: files mounted at one mount point.
struct ResourceGroup {
    std::string mountPoint;        ///< `app:/`, `imlui:/fonts/`
    std::filesystem::path base;    ///< absolute: the folder the paths are relative to (the dev source folder)
    std::vector<SourceFile> files; ///< sorted by path
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

/// The fields of one group, as the manifest wrote them.
struct GroupFields {
    std::string prefix;
    std::string mount;
    std::string base = ".";
    std::vector<std::string> files;
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::map<std::string, bool> compress;
};

using StatePtr = std::unique_ptr<lua::State, void (*)(lua::State*)>;

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
    return true;
}

/// Runs the manifest in a sandbox and reads the groups it returns.
std::optional<std::vector<GroupFields>> evaluate(std::string_view source, const fs::path& file, std::string& error) {
    const StatePtr owner{lua::newstate(), lua::close};
    lua::State* state = owner.get();
    if (!state) {
        error = "can't create a Lua state";
        return std::nullopt;
    }
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
            if (it->is_directory(ec)) {
                scanned.push_back(it->path());
                continue;
            }
            if (!it->is_regular_file(ec) || it->path() == self) continue;
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
    return group;
}

} // namespace

std::optional<ResourceManifest> parseManifest(std::string_view source, const fs::path& file, std::string& error) {
    std::error_code ec;
    ResourceManifest manifest;
    manifest.file = fs::absolute(file, ec).lexically_normal();
    const auto groups = evaluate(source, manifest.file, error);
    if (!groups) return std::nullopt;
    for (std::size_t i = 0; i < groups->size(); ++i) {
        const std::string where =
            utf8(manifest.file) + ": " + (groups->size() > 1 ? "group " + std::to_string(i + 1) + ": " : std::string{});
        auto group = resolveGroup((*groups)[i], manifest.file, where, manifest.scannedFolders, error);
        if (!group) return std::nullopt;
        manifest.groups.push_back(std::move(*group));
    }
    std::ranges::sort(manifest.scannedFolders);
    const auto [first, last] = std::ranges::unique(manifest.scannedFolders);
    manifest.scannedFolders.erase(first, last);
    return manifest;
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
