// Mount: one source of resource files (a directory, data embedded in the executable, a .pak archive).
export module imlui.resources.mount;

import std;

export import imlui.resources.data;

export namespace imlui {

/// Modification time of a resource, for hot reload.
using ResourceTime = std::filesystem::file_time_type;

/// A source of files that Resources mounts under a name (`app:/`, `imlui:/fonts/`). Paths are relative to the
/// mount's root, `/`-separated and normalized (see normalizePath); "" is the root. Implementations reject paths
/// that aren't normalized. Every method is safe to call from any thread.
class Mount {
public:
    Mount() = default;
    virtual ~Mount() = default;
    Mount(const Mount&) = delete;
    Mount& operator=(const Mount&) = delete;
    Mount(Mount&&) = delete;
    Mount& operator=(Mount&&) = delete;

    /// The bytes of file `path`, or nullopt when there is no such file. They stay valid while the returned handle
    /// (or a copy) and the mount live, also after a later read of a changed file returned newer bytes (see
    /// ResourceData).
    [[nodiscard]] virtual std::optional<ResourceData> read(std::string_view path) const = 0;
    /// True when `path` is a file or a folder ("" always is).
    [[nodiscard]] virtual bool exists(std::string_view path) const = 0;
    /// The entries of folder `path`, sorted: file names, and folder names with a trailing `/`. Empty when `path`
    /// isn't a folder.
    [[nodiscard]] virtual std::vector<std::string> list(std::string_view path) const = 0;
    /// The files under folder `path` at any depth, relative to it, sorted. Empty when `path` isn't a folder. The
    /// default walks list(); a mount that can do better in one pass (DirectoryMount) overrides it.
    [[nodiscard]] virtual std::vector<std::string> listFiles(std::string_view path) const;
    /// When file `path` last changed. nullopt for a missing file and for immutable mounts (embedded, .pak),
    /// whose files never change while the program runs.
    [[nodiscard]] virtual std::optional<ResourceTime> modified(std::string_view path) const = 0;
    /// Where the files come from, for logs and errors: `directory 'E:/app'`, `embedded 'imlui'`, `pak 'data.pak'`.
    [[nodiscard]] virtual std::string origin() const = 0;
};

} // namespace imlui

module :private;

namespace imlui {

std::vector<std::string> Mount::listFiles(std::string_view path) const {
    std::vector<std::string> out;
    std::vector<std::string> pending{""}; // folders still to list, relative to `path`, each with its trailing `/`
    while (!pending.empty()) {
        const std::string current = std::move(pending.back());
        pending.pop_back();
        std::string folder{path};
        if (!current.empty()) folder += (folder.empty() ? "" : "/") + current.substr(0, current.size() - 1);
        for (const std::string& entry : list(folder))
            (entry.ends_with('/') ? pending : out).push_back(current + entry);
    }
    std::ranges::sort(out);
    return out;
}

} // namespace imlui
