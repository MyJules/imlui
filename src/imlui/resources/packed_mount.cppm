// PackedMount: an immutable mount over a fixed table of files whose bytes are stored, each one either as is or
// zstd-compressed, in memory that outlives the mount. The shared base of EmbeddedMount and PakMount.
export module imlui.resources.packed_mount;

import std;

import zstd;
import imlui.log;
import imlui.resources.mount;
import imlui.resources.name;

export namespace imlui {

/// Files packed into one block of memory (data compiled into the executable, a .pak archive read into memory).
/// A stored file is served straight from that memory (zero-copy; the ResourceData has no owner, the bytes live as long
/// as the mount). A compressed one is decompressed when it is read into a buffer that the handles share (their
/// owner()), like a DirectoryMount's: the buffer lives while a handle holds it, and the mount itself keeps the buffers
/// of the files read most recently, up to retainedBytes in all, so a file read again soon isn't decompressed again.
/// Beyond that a buffer is freed with its last handle and decompressed again by the next read: a small archive that
/// declares a few files of 2 GiB each doesn't pin their bytes for as long as it is mounted. A file that fails to
/// decompress (corrupt data) is reported once and then reads as missing; one too large to allocate reads as missing
/// (the error is logged, nothing is thrown). The files never change: modified() is always nullopt.
class PackedMount : public Mount {
public:
    /// One file of the table.
    struct Entry {
        std::string path;                ///< normalized path in the mount (`fonts/Roboto-Regular.ttf`)
        std::span<const std::byte> data; ///< the bytes as stored (compressed or not)
        std::uint64_t size = 0;          ///< the file's size once decompressed
        bool compressed = false;         ///< `data` is one zstd frame
    };

    /// Takes the table. Entries whose path isn't normalized are dropped; of two entries with the same path, the
    /// later one is kept. The memory behind every `data` must outlive the mount.
    explicit PackedMount(std::vector<Entry> entries);

    [[nodiscard]] std::optional<ResourceData> read(std::string_view path) const override;
    [[nodiscard]] bool exists(std::string_view path) const override;
    [[nodiscard]] std::vector<std::string> list(std::string_view path) const override;
    /// The table's range under `path`, found by bisection: no walk.
    [[nodiscard]] std::vector<std::string> listFiles(std::string_view path) const override;
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view path) const override;
    /// False: the files never change.
    [[nodiscard]] bool mayChange() const override;

    /// The most bytes of decompressed files the mount keeps alive of its own (those read most recently): 16 MiB,
    /// enough for an app's scripts and a few fonts. A larger file is kept only by its handles.
    static constexpr std::size_t retainedBytes = std::size_t{16} << 20;

    /// The table, sorted by path.
    [[nodiscard]] const std::vector<Entry>& entries() const;
    /// How many times a compressed file has been decompressed: on its first read, and again on a read after its
    /// bytes were freed (no handle held them and the mount no longer kept them).
    [[nodiscard]] std::size_t decompressedCount() const;
    /// The bytes of decompressed files the mount keeps alive of its own now (at most retainedBytes).
    [[nodiscard]] std::size_t retainedSize() const;

private:
    /// The index of file `path` in m_entries.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view path) const;

    using Buffer = std::shared_ptr<const std::vector<std::byte>>;

    /// The decompressed bytes of one compressed entry. `failed` and `bytes` are guarded by `mutex`, so files are
    /// decompressed in parallel, each by one reader at a time (the others wait for its buffer).
    struct Decompressed {
        std::mutex mutex;
        bool failed = false;                               ///< corrupt: reads as missing from now on
        std::weak_ptr<const std::vector<std::byte>> bytes; ///< alive while `retained` or a handle holds it
        // Guarded by m_retainMutex (an eviction resets another file's `retained`, holding only that lock).
        Buffer retained;            ///< the mount's own reference, while among the files read most recently
        std::uint64_t lastRead = 0; ///< m_reads at the last read; its key in m_recent while `retained`
    };

    /// Marks entry `index` (holding `bytes`) as read now and keeps `bytes` among the retained buffers, then releases
    /// the least recently read ones until they fit in retainedBytes.
    void retain(std::size_t index, const Buffer& bytes) const;

    std::vector<Entry> m_entries;                   ///< sorted by path, unique
    std::unique_ptr<Decompressed[]> m_decompressed; ///< by entry index
    mutable std::atomic<std::size_t> m_decompressions = 0;
    mutable std::mutex m_retainMutex;   ///< guards the fields below and Decompressed::retained / lastRead
    mutable std::uint64_t m_reads = 0;  ///< the clock of Decompressed::lastRead
    mutable std::size_t m_retained = 0; ///< the bytes of the Decompressed::retained buffers
    mutable std::map<std::uint64_t, std::size_t> m_recent; ///< retained entries by lastRead, oldest first
};

} // namespace imlui

module :private;

namespace imlui {

PackedMount::PackedMount(std::vector<Entry> entries) {
    std::erase_if(entries, [](const Entry& e) {
        const auto normalized = normalizePath(e.path);
        return !normalized || *normalized != e.path || e.path.empty();
    });
    // Stable, so of equal paths the later entry ends up last; keep that one.
    std::ranges::stable_sort(entries, {}, &Entry::path);
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (i + 1 == entries.size() || entries[i].path != entries[i + 1].path)
            m_entries.push_back(std::move(entries[i]));
    m_decompressed = std::make_unique<Decompressed[]>(m_entries.size());
}

std::optional<std::size_t> PackedMount::find(std::string_view path) const {
    const auto it =
        std::ranges::lower_bound(m_entries, path, {}, [](const Entry& e) -> std::string_view { return e.path; });
    if (it == m_entries.end() || it->path != path) return std::nullopt;
    return static_cast<std::size_t>(it - m_entries.begin());
}

std::optional<ResourceData> PackedMount::read(std::string_view path) const {
    const auto index = find(path);
    if (!index) return std::nullopt;
    const Entry& entry = m_entries[*index];
    if (!entry.compressed) return ResourceData{entry.data};

    Decompressed& slot = m_decompressed[*index];
    Buffer bytes;
    {
        std::scoped_lock lock{slot.mutex};
        if (slot.failed) return std::nullopt;
        bytes = slot.bytes.lock();
        if (!bytes) {
            // Check the size the frame declares before allocating it.
            if (zstd::contentSize(entry.data) != entry.size) {
                log::error("resource '{}' ({}): corrupt compressed data", path, origin());
                slot.failed = true;
                return std::nullopt;
            }
            std::vector<std::byte> buffer;
            try {
                buffer.resize(static_cast<std::size_t>(entry.size));
                if (!zstd::decompress(entry.data, buffer)) {
                    log::error("resource '{}' ({}): corrupt compressed data", path, origin());
                    slot.failed = true;
                    return std::nullopt;
                }
                bytes = std::make_shared<const std::vector<std::byte>>(std::move(buffer));
            } catch (const std::bad_alloc&) {
                // Not thrown (a failed read is "not found"), not remembered (it may fit later).
                log::error("resource '{}' ({}): out of memory decompressing {} bytes", path, origin(), entry.size);
                return std::nullopt;
            }
            slot.bytes = bytes;
            ++m_decompressions;
        }
    }
    try {
        retain(*index, bytes);
    } catch (const std::bad_alloc&) { // NOLINT(bugprone-empty-catch): not kept by the mount, still served
    }
    return ResourceData{*bytes, bytes}; // alive while a handle holds it (or the mount keeps it, retain())
}

void PackedMount::retain(std::size_t index, const Buffer& bytes) const {
    std::scoped_lock lock{m_retainMutex};
    Decompressed& slot = m_decompressed[index];
    if (!slot.retained && bytes->size() > retainedBytes) return; // kept by its handles only
    // The one step that can throw (bad_alloc) comes first, so a failure leaves everything as it was.
    const std::uint64_t now = m_reads + 1;
    m_recent.emplace(now, index);
    m_reads = now;
    if (slot.retained) m_recent.erase(slot.lastRead);
    slot.lastRead = now;
    if (!slot.retained) {
        slot.retained = bytes;
        m_retained += bytes->size();
    }
    // Release the least recently read (never this one: it was read last, and fits on its own).
    while (m_retained > retainedBytes) {
        const auto oldest = m_recent.begin();
        Decompressed& old = m_decompressed[oldest->second];
        m_retained -= old.retained->size();
        old.retained.reset(); // freed here unless a handle still holds it
        m_recent.erase(oldest);
    }
}

bool PackedMount::exists(std::string_view path) const {
    if (path.empty() || find(path)) return true;
    const std::string folder = std::string{path} + "/";
    const auto it =
        std::ranges::lower_bound(m_entries, folder, {}, [](const Entry& e) -> std::string_view { return e.path; });
    return it != m_entries.end() && it->path.starts_with(folder);
}

std::vector<std::string> PackedMount::list(std::string_view path) const {
    std::vector<std::string> out;
    const std::string folder = path.empty() ? std::string{} : std::string{path} + "/";
    auto it =
        std::ranges::lower_bound(m_entries, folder, {}, [](const Entry& e) -> std::string_view { return e.path; });
    for (; it != m_entries.end() && it->path.starts_with(folder); ++it) {
        const std::string_view rest = std::string_view{it->path}.substr(folder.size());
        const std::size_t slash = rest.find('/');
        std::string entry{slash == std::string_view::npos ? rest : rest.substr(0, slash + 1)};
        if (out.empty() || out.back() != entry) out.push_back(std::move(entry));
    }
    // Paths sort `a-b` before `a/`, so a folder's entries may come out of order; sort and dedupe.
    std::ranges::sort(out);
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

std::vector<std::string> PackedMount::listFiles(std::string_view path) const {
    std::vector<std::string> out;
    const std::string folder = path.empty() ? std::string{} : std::string{path} + "/";
    // The files under the folder are the entries that start with `folder/`: one range of the sorted table, already in
    // order once the common prefix is cut.
    auto it =
        std::ranges::lower_bound(m_entries, folder, {}, [](const Entry& e) -> std::string_view { return e.path; });
    for (; it != m_entries.end() && it->path.starts_with(folder); ++it)
        out.push_back(it->path.substr(folder.size()));
    return out;
}

std::optional<ResourceTime> PackedMount::modified(std::string_view) const { return std::nullopt; }

bool PackedMount::mayChange() const { return false; }

const std::vector<PackedMount::Entry>& PackedMount::entries() const { return m_entries; }

std::size_t PackedMount::decompressedCount() const { return m_decompressions.load(); }

std::size_t PackedMount::retainedSize() const {
    std::scoped_lock lock{m_retainMutex};
    return m_retained;
}

} // namespace imlui
