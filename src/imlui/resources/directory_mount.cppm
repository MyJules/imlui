// DirectoryMount: a folder on disk as a resource mount (dev builds, loose overrides next to the executable).
export module imlui.resources.directory_mount;

import std;

import imlui.log;
import imlui.resources.mount;
import imlui.resources.name;
export import imlui.resources.selection;

export namespace imlui {

/// The files under a folder. Reads are cached per file and served from the cache while the file's time and size
/// are unchanged; a changed file is read again into a new buffer. The ResourceData handles returned by read() share
/// the buffer, so a buffer lives on while a handle to it does (data that must stay put, such as font data handed to
/// ImGui, keeps its handle) and is freed with the last one. The cache itself keeps a buffer alive only while it is
/// among the files read most recently, up to retainedBytes in all (a larger file isn't kept); beyond that it only
/// finds a buffer that a handle still holds, so a file read once and dropped (an image, once decoded) doesn't stay
/// in memory. The folder's files are mutable, so modified() reports their times.
///
/// A changed file that can't be read at the moment (an editor still holds it open without sharing) is served from
/// its previous buffer while the cache still has it, and until a read succeeds, modified() reports that buffer's time
/// (nullopt when there is none): the file still counts as changed, so a hot reload that polls modified() tries again.
///
/// Paths are case-sensitive, like the embedded and .pak mounts, even on a file system that isn't (Windows, macOS):
/// a path whose spelling on disk differs in case, in any of its segments, isn't found (with a warning naming the
/// spelling on disk), so a misspelled name fails in development, not only once shipped. Each folder's entry names
/// are cached while its modification time is unchanged; a name missing from a listing read within 2 s of that time
/// lists the folder again (an entry added in the same tick of the folder's time leaves the time as it was).
///
/// A link to a folder (a symbolic link, a Windows junction or another reparse point) is listed and walked like a
/// folder, so a file reached through one is listed (and watched by hot reload) as well as read, unless the link leads
/// back to a folder the walk came through (the folder holding it or one above it, as resolved on disk): that would
/// make a walk endless (loop/loop/...). A file under such a link is still found by its name.
///
/// Like the other mounts, it never throws: a path that isn't valid UTF-8, a file too large to allocate or a folder
/// that can't be listed reads as missing. An entry whose name has no UTF-8 form (on Windows, an unpaired surrogate)
/// has no resource name and is left out; the rest of its folder is served.
class DirectoryMount final : public Mount {
public:
    /// The files under `root`; with a `selection`, only the files it selects (a dev build's source folder serves
    /// exactly what its manifest group embeds): read(), exists() and modified() don't find the others, and list()
    /// and listFiles() show only selected files and the folders that hold one. The selection's `files` and
    /// `unmatched` are kept sorted, so checking a file against them is a bisection, not a scan of the list.
    explicit DirectoryMount(std::filesystem::path root, std::optional<ResourceSelection> selection = std::nullopt);

    /// The most bytes of file data the cache keeps alive of its own (the files read most recently): 8 MiB, enough
    /// for an app's scripts, so a script that can't be read while an editor saves it is served from its last bytes.
    static constexpr std::size_t retainedBytes = std::size_t{8} << 20;

    [[nodiscard]] const std::filesystem::path& root() const;

    [[nodiscard]] std::optional<ResourceData> read(std::string_view path) const override;
    [[nodiscard]] bool exists(std::string_view path) const override;
    [[nodiscard]] std::vector<std::string> list(std::string_view path) const override;
    /// One walk of the folder, pruned where the selection can't match.
    [[nodiscard]] std::vector<std::string> listFiles(std::string_view path) const override;
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view path) const override;
    [[nodiscard]] std::string origin() const override;

private:
    using Buffer = std::shared_ptr<const std::vector<std::byte>>;

    struct Cached {
        ResourceTime time;
        std::uintmax_t size = 0;
        std::weak_ptr<const std::vector<std::byte>> bytes; ///< alive while `retained` or a handle holds it
        Buffer retained;                                   ///< the cache's own reference, while among the recent reads
        std::uint64_t lastRead = 0;                        ///< m_reads at the last read that served it
        bool stale = false; ///< the file changed but couldn't be read: `bytes` and `time` are the previous version
    };
    using Cache = std::map<std::string, Cached, std::less<>>;

    /// What one folder holds for holdsSelected(), as of its modification time: whether a selected file is among its
    /// own entries, and its subfolders (links included) otherwise. A file added deeper down changes only its own
    /// folder's time, so the answer for a folder is its own entry and its subfolders' answers, each checked against its
    /// time.
    struct Holding {
        ResourceTime time;
        ResourceTime listedAt; ///< when the entries were read (the file system's clock)
        bool selectedFile = false;
        std::vector<std::string> subfolders; ///< entry names; empty when `selectedFile`
        std::vector<std::string> links;      ///< the subfolders that are links to folders, sorted
    };

    /// The entry names of one folder, as of its modification time.
    struct Folder {
        ResourceTime time;
        ResourceTime listedAt; ///< when the names were read (the file system's clock)
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
    /// warns once per path when it is one in a different case. Also false when a folder can't be timed or listed: a
    /// spelling that can't be checked isn't accepted.
    [[nodiscard]] bool spelledAsOnDisk(std::string_view path) const;
    /// True when file `path` is one the mount serves (every file without a selection).
    [[nodiscard]] bool selected(std::string_view path) const;
    /// True when folder `path` (`folder` on disk) holds a selected file at any depth (always without a selection).
    /// Each folder's answer for its own entries is kept while its time is unchanged (Holding), so a listing that asks
    /// for every subfolder, or a hot-reload poll asking again, times the folders under it instead of listing them and
    /// matching every file against the selection's patterns.
    [[nodiscard]] bool holdsSelected(std::string_view path, const std::filesystem::path& folder) const;
    /// True when `link`, a link to a folder found in folder `path`, may be walked: it leads to none of the folders
    /// from the root down to `path` (each resolved on disk, through the links on the way). A walk that follows only
    /// such links ends: each link it follows adds a folder that the links below must avoid, and there are finitely
    /// many. False when the link or a folder on the way can't be resolved.
    [[nodiscard]] bool walkable(std::string_view path, const std::filesystem::path& link) const;

    // The read cache, with m_mutex held.
    /// Marks `entry` (holding `bytes`) as read now and keeps `bytes` among the retained buffers, then releases the
    /// least recently read ones until they fit in retainedBytes.
    void retain(Cache::iterator entry, const Buffer& bytes) const;
    /// Removes `entry`, releasing its retained buffer.
    void forget(Cache::iterator entry) const;
    /// Removes the entries whose buffer is gone (nothing holds it any more).
    void prune() const;

    std::filesystem::path m_root;
    std::optional<ResourceSelection> m_selection; ///< its `files` and `unmatched` sorted and unique
    mutable std::mutex m_mutex;
    mutable Cache m_cache;                                   ///< the latest buffer of the files read, while it lives
    mutable std::uint64_t m_reads = 0;                       ///< the clock of Cached::lastRead
    mutable std::size_t m_retained = 0;                      ///< the bytes of the Cached::retained buffers
    mutable std::set<std::string, std::less<>> m_unreadable; ///< files that exist but were never read successfully
    /// Guards m_folders and m_misspelled; not held while a folder is listed or timed (another thread's lookups go on).
    mutable std::mutex m_foldersMutex;
    mutable std::map<std::string, Folder, std::less<>> m_folders; ///< by path ("" is the root)
    mutable std::set<std::string, std::less<>> m_misspelled;      ///< paths already warned about
    mutable std::mutex m_holdingMutex;
    mutable std::map<std::string, Holding, std::less<>> m_holding; ///< by path ("" is the root), for holdsSelected()
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

/// utf8(), or nullopt when `p` has no UTF-8 form (on Windows, a name with an unpaired surrogate): such an entry has
/// no resource name, so listings skip it rather than fail as a whole.
std::optional<std::string> utf8Name(const fs::path& p) {
    try {
        return utf8(p);
    } catch (const std::system_error&) {
        return std::nullopt;
    }
}

/// The coarsest file system time resolution there is (FAT's 2 s): a folder listing read this long after the
/// folder's time holds every entry with that time.
constexpr auto timeTick = std::chrono::seconds{2};

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

/// True when `entry` (a folder, links followed) is a link to one: a symbolic link, a junction or another reparse
/// point, or an entry that can't be queried (which walkable() can't resolve either, so it isn't walked).
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
    : m_root{std::move(root)}, m_selection{std::move(selection)} {
    if (!m_selection) return;
    for (std::vector<std::string>* paths : {&m_selection->files, &m_selection->unmatched}) {
        std::ranges::sort(*paths);
        const auto [first, last] = std::ranges::unique(*paths);
        paths->erase(first, last);
    }
}

const fs::path& DirectoryMount::root() const { return m_root; }

std::optional<fs::path> DirectoryMount::file(std::string_view path) const {
    const auto normalized = normalizePath(path);
    if (!normalized || *normalized != path) return std::nullopt;
    if (path.empty()) return m_root;
    const auto relative = pathFromUtf8(path);
    if (!relative || !spelledAsOnDisk(path)) return std::nullopt;
    return m_root / *relative;
}

bool DirectoryMount::selected(std::string_view path) const {
    // ResourceSelection::contains(), with `files` and `unmatched` sorted (see the constructor).
    if (!m_selection) return true;
    if (std::ranges::binary_search(m_selection->files, path, std::less<>{})) return true;
    return m_selection->matchesPatterns(path) &&
           !std::ranges::binary_search(m_selection->unmatched, path, std::less<>{});
}

bool DirectoryMount::holdsSelected(std::string_view path, const fs::path& folder) const {
    if (!m_selection) return true;
    if (!m_selection->mayContainUnder(path)) return false;
    std::scoped_lock lock{m_holdingMutex};
    // Depth first over the folders the selection can reach, each one's own entries from m_holding while its time is
    // unchanged (as in spelledAsOnDisk(), a listing read within a tick of that time may miss an entry added in the same
    // tick, so it is read again).
    std::vector<std::pair<std::string, fs::path>> pending;
    pending.emplace_back(path, folder);
    while (!pending.empty()) {
        auto [current, onDisk] = std::move(pending.back());
        pending.pop_back();
        std::error_code ec;
        const ResourceTime time = fs::last_write_time(onDisk, ec);
        if (ec) {
            // Gone, or can't be checked: holds nothing (as the walk this replaces found nothing there).
            if (const auto it = m_holding.find(current); it != m_holding.end()) m_holding.erase(it);
            continue;
        }
        auto it = m_holding.find(current);
        if (it == m_holding.end() || it->second.time != time || it->second.listedAt - it->second.time < timeTick) {
            Holding listed{time, fs::file_time_type::clock::now(), false, {}, {}};
            for (auto entry = fs::directory_iterator(onDisk, ec); !ec && entry != fs::directory_iterator();
                 entry.increment(ec)) {
                std::error_code entryEc; // one entry that can't be queried mustn't end the listing
                const auto name = utf8Name(entry->path().filename());
                if (!name) continue;
                if (entry->is_directory(entryEc)) {
                    listed.subfolders.push_back(*name);
                    if (linkedFolder(*entry)) listed.links.push_back(*name);
                } else if (entry->is_regular_file(entryEc) && selected(joined(current, *name))) {
                    listed.selectedFile = true;
                    listed.subfolders.clear(); // not needed: the folder holds one
                    listed.links.clear();
                    break;
                }
            }
            if (ec && !listed.selectedFile) continue; // can't be listed: not cached, holds nothing for now
            std::ranges::sort(listed.links);
            it = m_holding.insert_or_assign(current, std::move(listed)).first;
        }
        if (it->second.selectedFile) return true;
        const Holding& holding = it->second;
        for (const std::string& sub : holding.subfolders) {
            std::string child = joined(current, sub);
            if (!m_selection->mayContainUnder(child)) continue;
            const auto subPath = pathFromUtf8(sub);
            if (!subPath) continue;
            // A link back to a folder on the way down isn't walked (see walkable()).
            if (std::ranges::binary_search(holding.links, sub) && !walkable(current, onDisk / *subPath)) continue;
            pending.emplace_back(std::move(child), onDisk / *subPath);
        }
    }
    return false;
}

bool DirectoryMount::walkable(std::string_view path, const fs::path& link) const {
    std::error_code ec;
    const fs::path target = fs::canonical(link, ec);
    if (ec) return false;
    // The folders from the root down to `path`, each resolved on its own: one reached through a link resolves to
    // where the link leads.
    fs::path folder = m_root;
    std::size_t start = 0;
    while (true) {
        const fs::path resolved = fs::canonical(folder, ec);
        if (ec || resolved == target) return false;
        if (start >= path.size()) return true;
        const std::size_t slash = path.find('/', start);
        const auto segment = pathFromUtf8(path.substr(start, slash - start));
        if (!segment) return false;
        folder /= *segment;
        start = slash == std::string_view::npos ? path.size() : slash + 1;
    }
}

bool DirectoryMount::spelledAsOnDisk(std::string_view path) const {
    // m_foldersMutex is held only to look listings up and store them: the folder's time and its listing are read
    // without it, so a slow disk (or a hot-reload scan on a worker) doesn't hold up another thread's lookups.
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
        if (ec) return false; // missing, or can't be checked: not found (a check that can't run doesn't pass)

        // Not there in this spelling: false, with a hint (once per path) when only ASCII case differs. Strict even
        // when it differs beyond ASCII case, which the file system may also ignore. With m_foldersMutex held.
        const auto missing = [&](const std::set<std::string, std::less<>>& names) -> std::optional<std::string> {
            const auto other =
                std::ranges::find_if(names, [&](const std::string& n) { return equalIgnoringAsciiCase(n, segment); });
            if (other == names.end() || !m_misspelled.emplace(path).second) return std::nullopt;
            return folder.empty() ? *other : std::string{folder} + "/" + *other;
        };
        const auto warn = [&](const std::optional<std::string>& onDisk) {
            if (onDisk)
                log::warn("resource '{}' ({}): not found, the file on disk is spelled '{}' (names are case-sensitive)",
                          path, origin(), *onDisk);
        };

        // The kept listing, while the folder's time is unchanged. An entry added within the same tick of the folder's
        // time (coarse on some file systems) doesn't change it, so a miss in a listing that was read within a tick of
        // that time lists the folder again. A listing read later is complete: anything added since has a later time.
        enum class Kept { Found, Missing, Relist } kept = Kept::Relist;
        std::optional<std::string> hint;
        {
            std::scoped_lock lock{m_foldersMutex};
            if (const auto it = m_folders.find(folder); it != m_folders.end() && it->second.time == time) {
                if (it->second.names.contains(segment)) {
                    kept = Kept::Found;
                } else if (it->second.listedAt - it->second.time >= timeTick) {
                    kept = Kept::Missing;
                    hint = missing(it->second.names);
                }
            }
        }
        if (kept == Kept::Missing) {
            warn(hint);
            return false;
        }
        if (kept == Kept::Relist) {
            Folder listed{time, fs::file_time_type::clock::now(), {}};
            for (auto entry = fs::directory_iterator(folderPath, ec); !ec && entry != fs::directory_iterator();
                 entry.increment(ec))
                if (auto name = utf8Name(entry->path().filename())) listed.names.insert(std::move(*name));
            if (ec) return false;
            const bool found = listed.names.contains(segment);
            {
                std::scoped_lock lock{m_foldersMutex};
                if (!found) hint = missing(listed.names);
                // Another thread may have listed the folder meanwhile: the listing read last is kept.
                const auto it = m_folders.find(folder);
                if (it == m_folders.end())
                    m_folders.emplace(std::string{folder}, std::move(listed));
                else if (it->second.listedAt <= listed.listedAt)
                    it->second = std::move(listed);
            }
            if (!found) {
                warn(hint);
                return false;
            }
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
        if (cached != m_cache.end()) forget(cached);
        if (const auto it = m_unreadable.find(path); it != m_unreadable.end()) m_unreadable.erase(it);
        return std::nullopt;
    }
    // The previous buffer, when something (the cache or a handle) still holds it.
    const Buffer previous = cached != m_cache.end() ? cached->second.bytes.lock() : nullptr;
    if (previous && cached->second.time == time && cached->second.size == size) {
        cached->second.stale = false;
        retain(cached, previous);
        return ResourceData{*previous, previous};
    }

    // The file is there but doesn't open (an editor saving it without sharing) or doesn't fit in memory: serve the
    // previous version, marked stale so modified() keeps reporting its time and the next poll tries again. Without
    // one (never read, or released since), it is missing until it can be read.
    const auto unreadable = [&]() -> std::optional<ResourceData> {
        if (!previous) {
            if (cached != m_cache.end()) forget(cached);
            m_unreadable.emplace(path);
            return std::nullopt;
        }
        cached->second.stale = true;
        retain(cached, previous);
        return ResourceData{*previous, previous};
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
    if (cached != m_cache.end()) forget(cached);
    prune(); // so the cache doesn't grow with every file ever read
    Cached fresh;
    fresh.time = time;
    fresh.size = size;
    fresh.bytes = bytes;
    const auto entry = m_cache.emplace(std::string{path}, std::move(fresh)).first;
    retain(entry, bytes);
    if (const auto it = m_unreadable.find(path); it != m_unreadable.end()) m_unreadable.erase(it);
    return ResourceData{*bytes, bytes};
}

void DirectoryMount::retain(Cache::iterator entry, const Buffer& bytes) const {
    Cached& c = entry->second;
    c.lastRead = ++m_reads;
    if (c.retained || bytes->size() > retainedBytes) return;
    c.retained = bytes;
    m_retained += bytes->size();
    while (m_retained > retainedBytes) {
        // The least recently read retained buffer (never `entry`'s: it was read last). A scan, only when the retained
        // files outgrow the budget, over the files still alive.
        auto oldest = m_cache.end();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->second.retained && (oldest == m_cache.end() || it->second.lastRead < oldest->second.lastRead))
                oldest = it;
        m_retained -= oldest->second.retained->size();
        oldest->second.retained.reset();
    }
}

void DirectoryMount::forget(Cache::iterator entry) const {
    if (entry->second.retained) m_retained -= entry->second.retained->size();
    m_cache.erase(entry);
}

void DirectoryMount::prune() const {
    // A stale entry whose buffer is gone has nothing to serve either: its file is read again (or is unreadable).
    std::erase_if(m_cache, [](const Cache::value_type& e) { return e.second.bytes.expired(); });
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
        auto converted = utf8Name(it->path().filename());
        if (!converted) continue;
        std::string name = std::move(*converted);
        const std::string child = joined(path, name);
        if (it->is_directory(entryEc)) {
            if ((linkedFolder(*it) && !walkable(path, it->path())) || !holdsSelected(child, it->path())) continue;
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
    // Depth first over (the folder's name below `path`, its path on disk). Not a recursive_directory_iterator, which
    // follows every link or none: a link is followed when walkable().
    std::vector<std::pair<std::string, fs::path>> pending;
    pending.emplace_back(std::string{}, *folder);
    while (!pending.empty()) {
        auto [relative, onDisk] = std::move(pending.back());
        pending.pop_back();
        const std::string inMountFolder = joined(path, relative);
        ec.clear(); // a folder that can't be listed holds nothing; the walk goes on
        for (auto it = fs::directory_iterator(onDisk, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            std::error_code entryEc; // one entry that can't be queried mustn't end the walk
            const auto name = utf8Name(it->path().filename());
            if (!name) continue; // no resource name, so none for anything under it either
            std::string entry = joined(relative, *name);
            const std::string inMount = joined(path, entry);
            if (it->is_directory(entryEc)) {
                if (m_selection && !m_selection->mayContainUnder(inMount)) continue;
                if (linkedFolder(*it) && !walkable(inMountFolder, it->path())) continue;
                pending.emplace_back(std::move(entry), it->path());
            } else if (it->is_regular_file(entryEc) && selected(inMount)) {
                out.push_back(std::move(entry));
            }
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
