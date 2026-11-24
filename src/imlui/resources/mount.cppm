// Mount: one source of resource files (a directory, data embedded in the executable, a .pak archive).
module;

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module imlui.resources.mount;

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

    /// The bytes of file `path`, or nullopt when there is no such file. The span stays valid as long as the mount
    /// lives, even after a later read of a changed file returned newer bytes.
    [[nodiscard]] virtual std::optional<std::span<const std::byte>> read(std::string_view path) const = 0;
    /// True when `path` is a file or a folder ("" always is).
    [[nodiscard]] virtual bool exists(std::string_view path) const = 0;
    /// The entries of folder `path`, sorted: file names, and folder names with a trailing `/`. Empty when `path`
    /// isn't a folder.
    [[nodiscard]] virtual std::vector<std::string> list(std::string_view path) const = 0;
    /// When file `path` last changed. nullopt for a missing file and for immutable mounts (embedded, .pak),
    /// whose files never change while the program runs.
    [[nodiscard]] virtual std::optional<ResourceTime> modified(std::string_view path) const = 0;
    /// Where the files come from, for logs and errors: `directory 'E:/app'`, `embedded 'imlui'`, `pak 'data.pak'`.
    [[nodiscard]] virtual std::string origin() const = 0;
};

} // namespace imlui
