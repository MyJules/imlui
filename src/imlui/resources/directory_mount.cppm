// DirectoryMount: a folder on disk as a resource mount (dev builds, loose overrides next to the executable).
export module imlui.resources.directory_mount;

import std;

import imlui.log;
import imlui.resources.mount;
import imlui.resources.name;
export import imlui.resources.selection;

export namespace imlui {

/// The files under a folder. Reads are cached per file and served from the cache while the file's time and size
/// are unchanged; a changed file is read again into a new buffer. The cache holds only each file's latest buffer;
/// the ResourceData handles returned by read() share it, so a superseded buffer lives on while a handle to it does
/// (data that must stay put, such as font data handed to ImGui, keeps its handle) and is freed with the last one.
/// The folder's files are mutable, so modified() reports their times.
///
/// A changed file that can't be read at the moment (an editor still holds it open without sharing) is served from
/// its previous buffer, and until a read succeeds, modified() reports that buffer's time (nullopt when there is
/// none): the file still counts as changed, so a hot reload that polls modified() tries again.
///
/// Paths are case-sensitive, like the embedded and .pak mounts, even on a file system that isn't (Windows, macOS):
/// a path whose spelling on disk differs in case, in any of its segments, isn't found (with a warning naming the
/// spelling on disk), so a misspelled name fails in development, not only once shipped. Each folder's entry names
/// are cached while its modification time is unchanged.
///
/// A link to a folder (a symbolic link, a Windows junction or another reparse point) isn't listed or walked, as the
/// manifest scan doesn't descend into one: a link to the folder itself or to a parent would make a walk endless. A
/// file under one is still found by its name.
///
/// Like the other mounts, it never throws: a path that isn't valid UTF-8, a file too large to allocate or a folder
/// that can't be listed reads as missing.
class DirectoryMount final : public Mount {
public:
    /// The files under `root`; with a `selection`, only the files it selects (a dev build's source folder serves
    /// exactly what its manifest group embeds): read(), exists() and modified() don't find the others, and list()
    /// and listFiles() show only selected files and the folders that hold one.
    explicit DirectoryMount(std::filesystem::path root, std::optional<ResourceSelection> selection = std::nullopt);

    [[nodiscard]] const std::filesystem::path& root() const;

    [[nodiscard]] std::optional<ResourceData> read(std::string_view path) const override;
    [[nodiscard]] bool exists(std::string_view path) const override;
    [[nodiscard]] std::vector<std::string> list(std::string_view path) const override;
    /// One walk of the folder, pruned where the selection can't match.
    [[nodiscard]] std::vector<std::string> listFiles(std::string_view path) const override;
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view path) const override;
    [[nodiscard]] std::string origin() const override;

private:
    struct Cached {
        ResourceTime time;
        std::uintmax_t size = 0;
        std::shared_ptr<const std::vector<std::byte>> bytes;
        bool stale = false; ///< the file changed but couldn't be read: `bytes` and `time` are the previous version
    };

    /// The entry names of one folder, as of its modification time.
    struct Folder {
        ResourceTime time;
        std::set<std::string, std::less<>> names;
    };

    // The public methods without their exception guard.
    [[nodiscard]] std::optional<ResourceData> readFile(std::string_view path) const;
    [[nodiscard]] bool existsIn(std::string_view path) const;
    [[nodiscard]] std::vector<std::string> listIn(std::string_view path) const;
    [[nodiscard]] std::vector<std::string> listFilesIn(std::string_view path) const;
    [[nodiscard]] std::optional<ResourceTime> modifiedTime(std::string_view path) const;

    /// The file system path of `path`, or nullopt when `path` isn't normalized, isn't valid UTF-8 or is spelled in
    /// a different case on disk.
    [[nodiscard]] std::optional<std::filesystem::path> file(std::string_view path) const;
    /// False when a segment of `path` (normalized, not empty) isn't an entry of its folder in exactly this spelling;
    /// warns once per path when it is one in a different case. True when a folder can't be read (the file system
    /// decides).
    [[nodiscard]] bool spelledAsOnDisk(std::string_view path) const;
    /// True when file `path` is one the mount serves (every file without a selection).
    [[nodiscard]] bool selected(std::string_view path) const;
    /// True when folder `path` (`folder` on disk) holds a selected file at any depth (always without a selection).
    [[nodiscard]] bool holdsSelected(std::string_view path, const std::filesystem::path& folder) const;

    std::filesystem::path m_root;
    std::optional<ResourceSelection> m_selection;
    mutable std::mutex m_mutex;
    mutable std::map<std::string, Cached, std::less<>> m_cache; ///< the latest buffer of every file read
    mutable std::set<std::string, std::less<>> m_unreadable;    ///< files that exist but were never read successfully
    mutable std::mutex m_foldersMutex;
    mutable std::map<std::string, Folder, std::less<>> m_folders; ///< by path ("" is the root)
    mutable std::set<std::string, std::less<>> m_misspelled;      ///< paths already warned about
};

} // namespace imlui

module :private;

namespace imlui {

namespace fs = std::filesystem;

namespace {

/// A UTF-8 string as a path (on Windows a plain std::string would be read in the ANSI code page); nullopt when it
/// isn't valid UTF-8 (the conversion throws on Windows).
std::optional<fs::path> pathFromUtf8(std::string_view s) {
    try {
        return fs::path{std::u8string{s.begin(), s.end()}};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string{s.begin(), s.end()};
}

bool equalIgnoringAsciiCase(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::equal(a, b, {}, lower, lower);
}

std::string joined(std::string_view folder, std::string_view name) {
    std::string out{folder};
    if (!out.empty()) out += '/';
    out += name;
    return out;
}

/// True when `entry` is a link to a folder: a symbolic link, a junction or another reparse point (or an entry that
/// can't be queried, so that it isn't walked either).
bool linkedFolder(const fs::directory_entry& entry) {
    std::error_code ec;
    return entry.symlink_status(ec).type() != fs::file_type::directory;
}

/// Runs `f`; on an exception (out of memory, a file name the file system can't convert), logs it and returns
/// `fallback`. A mount reports a failure as "not found" (Lua's resource functions return nil and a message).
template <class R, class F>
R guarded(const DirectoryMount& mount, std::string_view what, std::string_view path, R fallback, F&& f) {
    try {
        return std::forward<F>(f)();
    } catch (const std::exception& e) {
        try {
            log::error("resource '{}' ({}): {} failed: {}", path, mount.origin(), what, e.what());
        } catch (const std::exception&) { // NOLINT(bugprone-empty-catch): nothing left to report it with
        }
        return fallback;
    }
}

} // namespace

DirectoryMount::DirectoryMount(fs::path root, std::optional<ResourceSelection> selection)
    : m_root{std::move(root)}, m_selection{std::move(selection)} {}

const fs::path& DirectoryMount::root() const { return m_root; }

std::optional<fs::path> DirectoryMount::file(std::string_view path) const {
    const auto normalized = normalizePath(path);
    if (!normalized || *normalized != path) return std::nullopt;
    if (path.empty()) return m_root;
    const auto relative = pathFromUtf8(path);
    if (!relative || !spelledAsOnDisk(path)) return std::nullopt;
    return m_root / *relative;
}

bool DirectoryMount::selected(std::string_view path) const { return !m_selection || m_selection->contains(path); }

bool DirectoryMount::holdsSelected(std::string_view path, const fs::path& folder) const {
    if (!m_selection) return true;
    if (!m_selection->mayContainUnder(path)) return false;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        std::error_code entryEc; // one entry that can't be queried mustn't end the search
        if (it->is_directory(entryEc)) {
            if (linkedFolder(*it)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(entryEc) &&
            m_selection->contains(joined(path, utf8(it->path().lexically_relative(folder)))))
            return true;
    }
    return false;
}

bool DirectoryMount::spelledAsOnDisk(std::string_view path) const {
    std::scoped_lock lock{m_foldersMutex};
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = path.find('/', start);
        const std::string_view folder = start == 0 ? std::string_view{} : path.substr(0, start - 1);
        const std::string_view segment = path.substr(start, slash - start);

        // The folder's entry names, listed again when its time changed (an entry was added, removed or renamed).
        // file() converted `path`, so its leading segments convert too.
        std::error_code ec;
        const fs::path folderPath = folder.empty() ? m_root : m_root / pathFromUtf8(folder).value_or(fs::path{});
        const ResourceTime time = fs::last_write_time(folderPath, ec);
        if (ec) return true; // missing: the lookup fails by itself
        auto it = m_folders.find(folder);
        if (it == m_folders.end() || it->second.time != time) {
            Folder listed{time, {}};
            for (auto entry = fs::directory_iterator(folderPath, ec); !ec && entry != fs::directory_iterator();
                 entry.increment(ec))
                listed.names.insert(utf8(entry->path().filename()));
            if (ec) return true;
            it = m_folders.insert_or_assign(std::string{folder}, std::move(listed)).first;
        }
        const auto& names = it->second.names;
        if (!names.contains(segment)) {
            const auto other =
                std::ranges::find_if(names, [&](const std::string& n) { return equalIgnoringAsciiCase(n, segment); });
            // Not there in this spelling. Strict even when it differs beyond ASCII case, which the file system may
            // also ignore; a hint when only ASCII case differs.
            if (other != names.end() && m_misspelled.emplace(path).second)
                log::warn("resource '{}' ({}): not found, the file on disk is spelled '{}' (names are case-sensitive)",
                          path, origin(), folder.empty() ? *other : std::string{folder} + "/" + *other);
            return false;
        }
        if (slash == std::string_view::npos) return true;
        start = slash + 1;
    }
}

std::optional<ResourceData> DirectoryMount::read(std::string_view path) const {
    return guarded(*this, "read", path, std::optional<ResourceData>{}, [&] { return readFile(path); });
}

std::optional<ResourceData> DirectoryMount::readFile(std::string_view path) const {
    const auto filePath = selected(path) ? file(path) : std::nullopt;
    std::error_code ec;
    const bool regular = filePath && fs::is_regular_file(*filePath, ec);
    const ResourceTime time = regular ? fs::last_write_time(*filePath, ec) : ResourceTime{};
    const std::uintmax_t size = regular && !ec ? fs::file_size(*filePath, ec) : 0;

    std::scoped_lock lock{m_mutex};
    const auto cached = m_cache.find(path);
    if (!regular || ec) {
        // A removed (or renamed) file: its last bytes go with their last handle.
        if (cached != m_cache.end()) m_cache.erase(cached);
        if (const auto it = m_unreadable.find(path); it != m_unreadable.end()) m_unreadable.erase(it);
        return std::nullopt;
    }
    if (cached != m_cache.end() && cached->second.time == time && cached->second.size == size) {
        cached->second.stale = false;
        return ResourceData{*cached->second.bytes, cached->second.bytes};
    }

    // The file is there but doesn't open (an editor saving it without sharing) or doesn't fit in memory: serve the
    // previous version, marked stale so modified() keeps reporting its time and the next poll tries again.
    const auto unreadable = [&]() -> std::optional<ResourceData> {
        if (cached == m_cache.end()) {
            m_unreadable.emplace(path);
            return std::nullopt;
        }
        cached->second.stale = true;
        return ResourceData{*cached->second.bytes, cached->second.bytes};
    };
    std::ifstream in{*filePath, std::ios::binary};
    if (!in) return unreadable();
    std::vector<std::byte> buffer;
    try {
        buffer.resize(static_cast<std::size_t>(size));
    } catch (const std::bad_alloc&) {
        log::error("resource '{}' ({}): out of memory reading {} bytes", path, origin(), size);
        return unreadable();
    }
    in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    if (in.bad()) return unreadable();
    buffer.resize(static_cast<std::size_t>(in.gcount())); // the file may have shrunk since file_size()
    // The cache drops its reference to the previous buffer; handles still holding it keep it alive.
    auto bytes = std::make_shared<const std::vector<std::byte>>(std::move(buffer));
    m_cache.insert_or_assign(std::string{path}, Cached{time, size, bytes, false});
    if (const auto it = m_unreadable.find(path); it != m_unreadable.end()) m_unreadable.erase(it);
    return ResourceData{*bytes, bytes};
}

bool DirectoryMount::exists(std::string_view path) const {
    return guarded(*this, "exists", path, false, [&] { return existsIn(path); });
}

bool DirectoryMount::existsIn(std::string_view path) const {
    const auto filePath = file(path);
    if (!filePath) return false;
    if (path.empty()) return true;
    std::error_code ec;
    if (fs::is_directory(*filePath, ec)) return holdsSelected(path, *filePath);
    return selected(path) && fs::exists(*filePath, ec);
}

std::vector<std::string> DirectoryMount::list(std::string_view path) const {
    return guarded(*this, "list", path, std::vector<std::string>{}, [&] { return listIn(path); });
}

std::vector<std::string> DirectoryMount::listIn(std::string_view path) const {
    std::vector<std::string> out;
    const auto filePath = file(path);
    std::error_code ec;
    if (!filePath || !fs::is_directory(*filePath, ec)) return out;
    for (auto it = fs::directory_iterator(*filePath, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        // Its own error code: one entry that can't be queried mustn't end the listing.
        std::error_code entryEc;
        std::string name = utf8(it->path().filename());
        const std::string child = joined(path, name);
        if (it->is_directory(entryEc)) {
            if (linkedFolder(*it) || !holdsSelected(child, it->path())) continue;
            name += '/';
        } else if (!it->is_regular_file(entryEc) || !selected(child)) {
            continue;
        }
        out.push_back(std::move(name));
    }
    std::ranges::sort(out);
    return out;
}

std::vector<std::string> DirectoryMount::listFiles(std::string_view path) const {
    return guarded(*this, "listFiles", path, std::vector<std::string>{}, [&] { return listFilesIn(path); });
}

std::vector<std::string> DirectoryMount::listFilesIn(std::string_view path) const {
    std::vector<std::string> out;
    const auto folder = file(path);
    std::error_code ec;
    if (!folder || !fs::is_directory(*folder, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(*folder, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        std::error_code entryEc; // one entry that can't be queried mustn't end the walk
        const std::string relative = utf8(it->path().lexically_relative(*folder));
        const std::string inMount = joined(path, relative);
        if (it->is_directory(entryEc)) {
            if (linkedFolder(*it) || (m_selection && !m_selection->mayContainUnder(inMount)))
                it.disable_recursion_pending();
        } else if (it->is_regular_file(entryEc) && selected(inMount)) {
            out.push_back(relative);
        }
    }
    std::ranges::sort(out);
    return out;
}

std::optional<ResourceTime> DirectoryMount::modified(std::string_view path) const {
    return guarded(*this, "modified", path, std::optional<ResourceTime>{}, [&] { return modifiedTime(path); });
}

std::optional<ResourceTime> DirectoryMount::modifiedTime(std::string_view path) const {
    const auto filePath = selected(path) ? file(path) : std::nullopt;
    std::error_code ec;
    if (!filePath || !fs::is_regular_file(*filePath, ec)) return std::nullopt;
    const ResourceTime time = fs::last_write_time(*filePath, ec);
    if (ec) return std::nullopt;
    // A change read() couldn't read yet: report the version it serves, so the file still looks changed.
    std::scoped_lock lock{m_mutex};
    if (m_unreadable.contains(path)) return std::nullopt;
    if (const auto it = m_cache.find(path); it != m_cache.end() && it->second.stale) return it->second.time;
    return time;
}

std::string DirectoryMount::origin() const { return "directory '" + utf8(m_root) + "'"; }

} // namespace imlui
