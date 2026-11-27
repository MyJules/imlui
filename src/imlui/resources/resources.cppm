// Resources: the app's files (scripts, fonts, data) by name, from stacked mounts.
export module imlui.resources;

import std;

export import imlui.resources.data;
import imlui.resources.mount;
import imlui.resources.name;

export namespace imlui {

/// Maps resource names (`app:/main.lua`, `imlui:/fonts/Roboto-Regular.ttf`) to files in mounts. Each mount sits
/// at a mount point: the root of a prefix (`app:/`) or a folder in it (`imlui:/fonts/`). A name is looked up in
/// the mounts whose point contains it, the most recently mounted first, so a later mount overrides files of an
/// earlier one and falls back to it for the rest.
///
/// Mount before sharing; after that, every const method is safe from any thread. Copies share the mounts.
/// The bytes read() returns stay valid while their ResourceData handle (or a copy) lives and the mount that served
/// them does, so as long as any Resources holds it (see ResourceData).
class Resources {
public:
    /// Mounts `mount` at `point` (a full folder name such as `app:/` or `imlui:/fonts/`). Returns false, and
    /// mounts nothing, when `point` isn't a valid name or `mount` is null.
    bool mount(std::string_view point, std::shared_ptr<Mount> mount);

    /// The bytes of file `name`; nullopt when the name is invalid or no mount has the file.
    [[nodiscard]] std::optional<ResourceData> read(std::string_view name) const;
    /// A copy of read() as text.
    [[nodiscard]] std::optional<std::string> readText(std::string_view name) const;
    /// True when `name` is a file or a folder in some mount (mount points and their parents count as folders).
    [[nodiscard]] bool exists(std::string_view name) const;
    /// The entries of folder `name` across its mounts, sorted and without duplicates: file names, and folder names
    /// with a trailing `/`. Mount points inside the folder show up as folders.
    [[nodiscard]] std::vector<std::string> list(std::string_view name) const;
    /// The full names of all files under folder `name` (with or without a trailing `/`, like list()), at any depth,
    /// sorted.
    [[nodiscard]] std::vector<std::string> listRecursive(std::string_view name) const;
    /// When file `name` last changed, from the mount that serves it; nullopt when it's missing or that mount is
    /// immutable.
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view name) const;
    /// Where `name` comes from (`directory 'E:/app' main.lua`), for logs; "" when no mount has it.
    [[nodiscard]] std::string origin(std::string_view name) const;

    /// The mounted prefixes (`app`, `imlui`), in the order they were first mounted.
    [[nodiscard]] std::vector<std::string> prefixes() const;
    /// One line per mount, oldest first: `imlui:/fonts/ <- directory 'E:/x/fonts'`.
    [[nodiscard]] std::vector<std::string> describe() const;

private:
    struct Entry {
        std::string prefix;
        std::string at; ///< the mount point's path in the prefix; "" for the root
        std::shared_ptr<Mount> mount;
    };
    struct Hit {
        const Mount* mount;
        std::string path; ///< the name's path inside `mount`
    };

    /// The mounts whose point contains `name`, newest first, with the name's path in each.
    [[nodiscard]] std::vector<Hit> covering(const ResourceName& name) const;

    std::vector<Entry> m_mounts;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// `path` relative to `at` when `at` is `path` or one of its folders.
std::optional<std::string> relativeTo(std::string_view path, std::string_view at) {
    if (at.empty()) return std::string{path};
    if (path == at) return std::string{};
    if (path.size() > at.size() && path.starts_with(at) && path[at.size()] == '/')
        return std::string{path.substr(at.size() + 1)};
    return std::nullopt;
}

} // namespace

bool Resources::mount(std::string_view point, std::shared_ptr<Mount> mount) {
    auto parsed = parseName(point);
    if (!parsed || !mount) return false;
    m_mounts.push_back({std::move(parsed->prefix), std::move(parsed->path), std::move(mount)});
    return true;
}

std::vector<Resources::Hit> Resources::covering(const ResourceName& name) const {
    std::vector<Hit> out;
    for (auto it = m_mounts.rbegin(); it != m_mounts.rend(); ++it) {
        if (it->prefix != name.prefix) continue;
        if (auto path = relativeTo(name.path, it->at)) out.push_back({it->mount.get(), std::move(*path)});
    }
    return out;
}

std::optional<ResourceData> Resources::read(std::string_view name) const {
    const auto parsed = parseName(name);
    if (!parsed) return std::nullopt;
    for (const Hit& hit : covering(*parsed))
        if (auto bytes = hit.mount->read(hit.path)) return bytes;
    return std::nullopt;
}

std::optional<std::string> Resources::readText(std::string_view name) const {
    const auto bytes = read(name);
    if (!bytes) return std::nullopt;
    return std::string{bytes->text()};
}

bool Resources::exists(std::string_view name) const {
    const auto parsed = parseName(name);
    if (!parsed) return false;
    for (const Hit& hit : covering(*parsed))
        if (hit.mount->exists(hit.path)) return true;
    // A folder that holds a mount point (`imlui:/` when only `imlui:/fonts/` is mounted).
    for (const Entry& e : m_mounts)
        if (e.prefix == parsed->prefix && relativeTo(e.at, parsed->path)) return true;
    return false;
}

std::vector<std::string> Resources::list(std::string_view name) const {
    std::vector<std::string> out;
    const auto parsed = parseName(name);
    if (!parsed) return out;
    for (const Hit& hit : covering(*parsed)) {
        auto entries = hit.mount->list(hit.path);
        out.insert(out.end(), std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end()));
    }
    for (const Entry& e : m_mounts) {
        if (e.prefix != parsed->prefix) continue;
        const auto below = relativeTo(e.at, parsed->path);
        if (!below || below->empty()) continue;
        out.push_back(below->substr(0, below->find('/')) + "/");
    }
    std::ranges::sort(out);
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

std::vector<std::string> Resources::listRecursive(std::string_view name) const {
    std::vector<std::string> out;
    // `name` is the folder itself, like in list(): `imlui:/fonts` is `imlui:/fonts/`, not `imlui:/` (folderOf()).
    const auto parsed = parseName(name);
    if (!parsed) return out;
    std::string folder = parsed->full();
    if (!folder.ends_with('/')) folder += '/';
    // One walk per mount: the mounts covering the folder from its path in them, the mounts inside it from their root.
    for (const Entry& e : m_mounts) {
        if (e.prefix != parsed->prefix) continue;
        std::string at = folder;
        auto path = relativeTo(parsed->path, e.at);
        if (!path) {
            const auto below = relativeTo(e.at, parsed->path);
            if (!below) continue;
            at += *below + "/";
            path = std::string{};
        }
        for (const std::string& file : e.mount->listFiles(*path))
            out.push_back(at + file);
    }
    std::ranges::sort(out);
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

std::optional<ResourceTime> Resources::modified(std::string_view name) const {
    const auto parsed = parseName(name);
    if (!parsed) return std::nullopt;
    // The mount that serves the file decides. modified() is nullopt for a missing file, so a time is that mount's
    // answer at once; only a nullopt needs exists() to tell a missing file (try the next mount) from one an immutable
    // mount serves (no time). A directory mount, the one with times, is asked once per check instead of twice (each
    // ask checks the spelling of every folder on the way and matches the selection's patterns).
    for (const Hit& hit : covering(*parsed)) {
        if (auto time = hit.mount->modified(hit.path)) return time;
        if (hit.mount->exists(hit.path)) return std::nullopt;
    }
    return std::nullopt;
}

std::string Resources::origin(std::string_view name) const {
    const auto parsed = parseName(name);
    if (!parsed) return {};
    for (const Hit& hit : covering(*parsed))
        if (hit.mount->exists(hit.path)) return hit.mount->origin() + (hit.path.empty() ? "" : " " + hit.path);
    return {};
}

std::vector<std::string> Resources::prefixes() const {
    std::vector<std::string> out;
    for (const Entry& e : m_mounts)
        if (std::ranges::find(out, e.prefix) == out.end()) out.push_back(e.prefix);
    return out;
}

std::vector<std::string> Resources::describe() const {
    std::vector<std::string> out;
    for (const Entry& e : m_mounts) {
        std::string point = ResourceName{e.prefix, e.at}.full();
        if (!point.ends_with('/')) point += '/';
        out.push_back(point + " <- " + e.mount->origin());
    }
    return out;
}

} // namespace imlui
