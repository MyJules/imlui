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
/// A stored file is served straight from that memory (zero-copy). A compressed one is decompressed on its first
/// read and the result is cached until the mount is destroyed, so all bytes stay valid as long as the mount (the
/// ResourceData handles have no owner). A file that fails to decompress (corrupt data) is reported once and then
/// reads as missing; one too large to allocate reads as missing (the error is logged, nothing is thrown).
/// The files never change: modified() is always nullopt.
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
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view path) const override;

    /// The table, sorted by path.
    [[nodiscard]] const std::vector<Entry>& entries() const;
    /// How many compressed files have been decompressed so far (they're decompressed on first read).
    [[nodiscard]] std::size_t decompressedCount() const;

private:
    /// The index of file `path` in m_entries.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view path) const;

    /// The decompressed bytes of one compressed entry. `state` is written under `mutex` and read without it, so
    /// files are decompressed in parallel, each once.
    struct Decompressed {
        enum class State : std::uint8_t { Pending, Ready, Failed };
        std::mutex mutex;
        std::atomic<State> state = State::Pending;
        std::vector<std::byte> bytes; ///< set once, before `state` becomes Ready
    };

    std::vector<Entry> m_entries;                   ///< sorted by path, unique
    std::unique_ptr<Decompressed[]> m_decompressed; ///< by entry index
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

    using State = Decompressed::State;
    Decompressed& slot = m_decompressed[*index];
    if (slot.state.load(std::memory_order_acquire) == State::Pending) {
        std::scoped_lock lock{slot.mutex};
        if (slot.state.load(std::memory_order_relaxed) == State::Pending) {
            // Check the size the frame declares before allocating it.
            if (zstd::contentSize(entry.data) != entry.size) {
                log::error("resource '{}' ({}): corrupt compressed data", path, origin());
                slot.state.store(State::Failed, std::memory_order_release);
                return std::nullopt;
            }
            std::vector<std::byte> bytes;
            try {
                bytes.resize(static_cast<std::size_t>(entry.size));
            } catch (const std::bad_alloc&) {
                // Not thrown (read() is called from Lua's C frames), not remembered (it may fit later).
                log::error("resource '{}' ({}): out of memory decompressing {} bytes", path, origin(), entry.size);
                return std::nullopt;
            }
            if (!zstd::decompress(entry.data, bytes)) {
                log::error("resource '{}' ({}): corrupt compressed data", path, origin());
                slot.state.store(State::Failed, std::memory_order_release);
                return std::nullopt;
            }
            slot.bytes = std::move(bytes);
            slot.state.store(State::Ready, std::memory_order_release);
        }
    }
    if (slot.state.load(std::memory_order_acquire) != State::Ready) return std::nullopt;
    return ResourceData{slot.bytes}; // owned by the mount until it is destroyed
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

std::optional<ResourceTime> PackedMount::modified(std::string_view) const { return std::nullopt; }

const std::vector<PackedMount::Entry>& PackedMount::entries() const { return m_entries; }

std::size_t PackedMount::decompressedCount() const {
    std::size_t count = 0;
    for (std::size_t i = 0; i < m_entries.size(); ++i)
        if (m_decompressed[i].state.load(std::memory_order_acquire) == Decompressed::State::Ready) ++count;
    return count;
}

} // namespace imlui
