// DirectoryMount: a folder on disk as a resource mount (dev builds, loose overrides next to the executable).
export module imlui.resources.directory_mount;

import std;

import imlui.log;
import imlui.resources.mount;
import imlui.resources.name;

export namespace imlui {

/// The files under a folder. Reads are cached per file and served from the cache while the file's time and size
/// are unchanged; a changed file is read again into a new buffer. The cache holds only each file's latest buffer;
/// the ResourceData handles returned by read() share it, so a superseded buffer lives on while a handle to it does
/// (data that must stay put, such as font data handed to ImGui, keeps its handle) and is freed with the last one.
/// The folder's files are mutable, so modified() reports their times.
///
/// Paths are case-sensitive, like the embedded and .pak mounts, even on a file system that isn't (Windows, macOS):
/// a path whose spelling on disk differs in case, in any of its segments, isn't found (with a warning naming the
/// spelling on disk), so a misspelled name fails in development, not only once shipped. Each folder's entry names
/// are cached while its modification time is unchanged.
class DirectoryMount final : public Mount {
public:
    /// The files under `root`. Files whose paths are in `hidden` (normalized, relative to `root`) are left out:
    /// read() and modified() don't find them, exists() and list() don't show them (a package hides its manifest).
    explicit DirectoryMount(std::filesystem::path root, std::set<std::string, std::less<>> hidden = {});

    [[nodiscard]] const std::filesystem::path& root() const;

    [[nodiscard]] std::optional<ResourceData> read(std::string_view path) const override;
    [[nodiscard]] bool exists(std::string_view path) const override;
    [[nodiscard]] std::vector<std::string> list(std::string_view path) const override;
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view path) const override;
    [[nodiscard]] std::string origin() const override;

private:
    struct Cached {
        ResourceTime time;
        std::uintmax_t size = 0;
        std::shared_ptr<const std::vector<std::byte>> bytes;
    };

    /// The entry names of one folder, as of its modification time.
    struct Folder {
        ResourceTime time;
        std::set<std::string, std::less<>> names;
    };

    /// The file system path of `path`, or nullopt when `path` isn't normalized, is hidden or is spelled in a
    /// different case on disk.
    [[nodiscard]] std::optional<std::filesystem::path> file(std::string_view path) const;
    /// False when a segment of `path` (normalized, not empty) isn't an entry of its folder in exactly this spelling;
    /// warns once per path when it is one in a different case. True when a folder can't be read (the file system
    /// decides).
    [[nodiscard]] bool spelledAsOnDisk(std::string_view path) const;

    std::filesystem::path m_root;
    std::set<std::string, std::less<>> m_hidden;
    mutable std::mutex m_mutex;
    mutable std::map<std::string, Cached, std::less<>> m_cache; ///< the latest buffer of every file read
    mutable std::mutex m_foldersMutex;
    mutable std::map<std::string, Folder, std::less<>> m_folders; ///< by path ("" is the root)
    mutable std::set<std::string, std::less<>> m_misspelled;      ///< paths already warned about
};

} // namespace imlui

module :private;

namespace imlui {

namespace fs = std::filesystem;

namespace {

/// A UTF-8 string as a path (on Windows a plain std::string would be read in the ANSI code page).
fs::path pathFromUtf8(std::string_view s) { return fs::path{std::u8string{s.begin(), s.end()}}; }

std::string utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string{s.begin(), s.end()};
}

bool equalIgnoringAsciiCase(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::equal(a, b, {}, lower, lower);
}

} // namespace

DirectoryMount::DirectoryMount(fs::path root, std::set<std::string, std::less<>> hidden)
    : m_root{std::move(root)}, m_hidden{std::move(hidden)} {}

const fs::path& DirectoryMount::root() const { return m_root; }

std::optional<fs::path> DirectoryMount::file(std::string_view path) const {
    const auto normalized = normalizePath(path);
    if (!normalized || *normalized != path || m_hidden.contains(path)) return std::nullopt;
    if (path.empty()) return m_root;
    if (!spelledAsOnDisk(path)) return std::nullopt;
    return m_root / pathFromUtf8(path);
}

bool DirectoryMount::spelledAsOnDisk(std::string_view path) const {
    std::scoped_lock lock{m_foldersMutex};
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = path.find('/', start);
        const std::string_view folder = start == 0 ? std::string_view{} : path.substr(0, start - 1);
        const std::string_view segment = path.substr(start, slash - start);

        // The folder's entry names, listed again when its time changed (an entry was added, removed or renamed).
        std::error_code ec;
        const fs::path folderPath = folder.empty() ? m_root : m_root / pathFromUtf8(folder);
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
    const auto filePath = file(path);
    std::error_code ec;
    const bool regular = filePath && fs::is_regular_file(*filePath, ec);
    const ResourceTime time = regular ? fs::last_write_time(*filePath, ec) : ResourceTime{};
    const std::uintmax_t size = regular && !ec ? fs::file_size(*filePath, ec) : 0;

    std::scoped_lock lock{m_mutex};
    if (!regular || ec) {
        // A removed (or renamed) file: its last bytes go with their last handle.
        if (const auto it = m_cache.find(path); it != m_cache.end()) m_cache.erase(it);
        return std::nullopt;
    }
    if (auto it = m_cache.find(path); it != m_cache.end() && it->second.time == time && it->second.size == size)
        return ResourceData{*it->second.bytes, it->second.bytes};

    std::ifstream in{*filePath, std::ios::binary};
    if (!in) return std::nullopt;
    std::vector<std::byte> buffer(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    buffer.resize(static_cast<std::size_t>(in.gcount())); // the file may have shrunk since file_size()
    // The cache drops its reference to the previous buffer; handles still holding it keep it alive.
    auto bytes = std::make_shared<const std::vector<std::byte>>(std::move(buffer));
    m_cache.insert_or_assign(std::string{path}, Cached{time, size, bytes});
    return ResourceData{*bytes, bytes};
}

bool DirectoryMount::exists(std::string_view path) const {
    const auto filePath = file(path);
    std::error_code ec;
    return filePath && fs::exists(*filePath, ec);
}

std::vector<std::string> DirectoryMount::list(std::string_view path) const {
    std::vector<std::string> out;
    const auto filePath = file(path);
    std::error_code ec;
    if (!filePath || !fs::is_directory(*filePath, ec)) return out;
    for (auto it = fs::directory_iterator(*filePath, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        // Its own error code: one entry that can't be queried mustn't end the listing.
        std::error_code entryEc;
        std::string name = utf8(it->path().filename());
        if (it->is_directory(entryEc))
            name += '/';
        else if (!it->is_regular_file(entryEc) ||
                 m_hidden.contains(path.empty() ? name : std::string{path} + "/" + name))
            continue;
        out.push_back(std::move(name));
    }
    std::ranges::sort(out);
    return out;
}

std::optional<ResourceTime> DirectoryMount::modified(std::string_view path) const {
    const auto filePath = file(path);
    std::error_code ec;
    if (!filePath || !fs::is_regular_file(*filePath, ec)) return std::nullopt;
    const ResourceTime time = fs::last_write_time(*filePath, ec);
    if (ec) return std::nullopt;
    return time;
}

std::string DirectoryMount::origin() const { return "directory '" + utf8(m_root) + "'"; }

} // namespace imlui
