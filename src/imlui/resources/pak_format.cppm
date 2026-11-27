// The .pak archive format: writing an archive's bytes and parsing them back. Shared by PakMount (reading) and the
// imlui_pack tool (writing). The files' bytes are stored the same way as embedded data (as is or one zstd frame
// each); this module only lays them out.
//
// Layout, all integers little-endian:
//
//   header   "IMLUIPAK" (8 bytes), u32 version (1), u32 file count,
//            u32 mount point length, mount point (UTF-8, a full folder name such as `app:/` or `imlui:/fonts/`)
//   index    per file: u32 path length, path (UTF-8, normalized), u64 offset (from the start of the archive),
//            u64 stored size, u64 size once decompressed, u32 flags (bit 0: zstd-compressed)
//   blobs    each file's stored bytes, at its offset
export module imlui.resources.pak_format;

import std;

import imlui.resources.name;

export namespace imlui {

/// One file of an archive being written.
struct PakFile {
    std::string path;                  ///< normalized path in the mount
    std::span<const std::byte> stored; ///< the bytes as stored (compressed or not)
    std::uint64_t size = 0;            ///< the file's size once decompressed
    bool compressed = false;           ///< `stored` is one zstd frame
};

/// One file of a parsed archive.
struct PakIndexEntry {
    std::string path;
    std::uint64_t offset = 0; ///< from the start of the archive
    std::uint64_t storedSize = 0;
    std::uint64_t size = 0;
    bool compressed = false;
};

/// A parsed header and index.
struct PakIndex {
    std::string mountPoint; ///< the full folder name the archive mounts at (`app:/`)
    std::vector<PakIndexEntry> entries;
};

inline constexpr std::string_view pakMagic = "IMLUIPAK";
inline constexpr std::uint32_t pakVersion = 1;
/// Files larger than this (decompressed) are rejected as corrupt.
inline constexpr std::uint64_t pakMaxFileSize = std::uint64_t{1} << 31;
/// Archives larger than this aren't written or opened: an archive is read into memory whole.
inline constexpr std::uint64_t pakMaxArchiveSize = std::uint64_t{1} << 32;
/// Archives whose files add up to more than this (decompressed) are rejected as corrupt: a small archive can't make
/// a mount that reading every file would need terabytes for.
inline constexpr std::uint64_t pakMaxTotalSize = std::uint64_t{1} << 32;
/// Paths longer than this (bytes) are rejected: no real resource needs more, and the checks on paths (normalizing,
/// sorting, the file-and-folder check) get slower with their length.
inline constexpr std::size_t pakMaxPathLength = 4096;
/// Paths with more `/`-separated segments than this are rejected, for the same reason.
inline constexpr std::size_t pakMaxPathSegments = 256;

/// The bytes of an archive holding `files`, mounted at `mountPoint`. nullopt (with a reason in `error`, when given)
/// when the archive couldn't be read back: a string or the file count doesn't fit its u32 field, a file is larger
/// than pakMaxFileSize or a stored one's size isn't its stored size, the files add up to more than pakMaxTotalSize,
/// two files have the same path, a path is a file and a folder (`a` and `a/b`), a path is longer than
/// pakMaxPathLength or has more than pakMaxPathSegments segments, or the archive would be larger than
/// pakMaxArchiveSize. Whether the paths and the mount point are normalized isn't checked here (parsePak() does).
[[nodiscard]] std::optional<std::vector<std::byte>>
writePak(std::string_view mountPoint, std::span<const PakFile> files, std::string* error = nullptr);

/// Parses and checks the header and index of `archive`: the magic and version, a mount point that is a canonical
/// full folder name (`app:/`, `imlui:/fonts/`: not `app:/main.lua`), normalized paths without duplicates and none
/// that is both a file and a folder, none longer than pakMaxPathLength or deeper than pakMaxPathSegments, every file's
/// bytes inside the archive, past the index and overlapping no other
/// file's, and at most pakMaxTotalSize bytes in all once decompressed. nullopt (with a reason in `error`, when given)
/// for anything else.
[[nodiscard]] std::optional<PakIndex> parsePak(std::span<const std::byte> archive, std::string* error = nullptr);

} // namespace imlui

module :private;

namespace imlui {

namespace {

constexpr std::uint32_t flagCompressed = 1;

void putU32(std::vector<std::byte>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xffU));
}

void putU64(std::vector<std::byte>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xffU));
}

void putString(std::vector<std::byte>& out, std::string_view s) {
    putU32(out, static_cast<std::uint32_t>(s.size()));
    for (const char c : s)
        out.push_back(static_cast<std::byte>(c));
}

/// Reads little-endian fields from the archive, failing (and staying failed) at its end.
class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : m_bytes{bytes} {}

    [[nodiscard]] bool ok() const { return m_ok; }
    /// How many bytes have been read.
    [[nodiscard]] std::size_t position() const { return m_pos; }

    std::uint64_t number(int width) {
        if (!take(static_cast<std::size_t>(width))) return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < width; ++i)
            v |= std::to_integer<std::uint64_t>(m_bytes[m_pos - static_cast<std::size_t>(width - i)]) << (8 * i);
        return v;
    }

    std::string string(std::size_t length) {
        if (!take(length)) return {};
        const auto* first = reinterpret_cast<const char*>(m_bytes.data() + (m_pos - length));
        return std::string{first, length};
    }

private:
    bool take(std::size_t n) {
        if (!m_ok || n > m_bytes.size() - m_pos) {
            m_ok = false;
            return false;
        }
        m_pos += n;
        return true;
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_pos = 0;
    bool m_ok = true;
};

/// Sets `error` (when given); returns nullopt, for an optional of any type.
std::nullopt_t failed(std::string* error, std::string reason) {
    if (error) *error = std::move(reason);
    return std::nullopt;
}

constexpr std::uint64_t u32Max = std::numeric_limits<std::uint32_t>::max();

/// The first path of `paths` (a sorted, unique set) that is also a folder of another one (`a` with `a/b`), or nullopt.
/// One lookup per path: the paths under folder `p` are those from `p/` up, sorted together right there, so the first
/// path at or after `p/` starts with it when any does (`a-b` sorts between `a` and `a/b`, but not after `a/`). Looking
/// up every prefix of every path instead costs O(length²) per path, minutes for a 1 MiB `a/a/a/...` from a hostile
/// archive (now also capped by pakMaxPathLength).
template <class Paths>
std::optional<std::string_view> fileAndFolder(const Paths& paths) {
    std::string folder;
    for (const std::string_view path : paths) {
        folder.assign(path);
        folder += '/';
        if (const auto it = paths.lower_bound(folder); it != paths.end() && std::string_view{*it}.starts_with(folder))
            return path;
    }
    return std::nullopt;
}

/// `path` for an error message: its start when it is long.
std::string shown(std::string_view path) {
    return path.size() > 64 ? std::string{path.substr(0, 64)} + "..." : std::string{path};
}

/// Why `path` is too long or too deep for an archive, or "" when it isn't.
std::string pathLimitProblem(std::string_view path) {
    if (path.size() > pakMaxPathLength)
        return "is too long (" + std::to_string(path.size()) + " bytes, at most " + std::to_string(pakMaxPathLength) +
               ")";
    if (static_cast<std::size_t>(std::ranges::count(path, '/')) >= pakMaxPathSegments)
        return "has too many segments (at most " + std::to_string(pakMaxPathSegments) + ")";
    return {};
}

} // namespace

std::optional<std::vector<std::byte>> writePak(std::string_view mountPoint, std::span<const PakFile> files,
                                               std::string* error) {
    // Everything the reader would reject or the fields would truncate fails here, not in the shipped archive.
    if (mountPoint.size() > u32Max) return failed(error, "the mount point is too long");
    if (files.size() > u32Max) return failed(error, "too many files (" + std::to_string(files.size()) + ")");
    std::set<std::string_view> paths;
    for (const auto& f : files) {
        if (const std::string problem = pathLimitProblem(f.path); !problem.empty())
            return failed(error, "'" + shown(f.path) + "' " + problem);
        if (f.size > pakMaxFileSize)
            return failed(error, "'" + f.path + "' is too large (" + std::to_string(f.size) + " bytes, at most " +
                                     std::to_string(pakMaxFileSize) + ")");
        if (!f.compressed && f.size != f.stored.size()) return failed(error, "'" + f.path + "' has an invalid size");
        if (!paths.insert(f.path).second) return failed(error, "'" + f.path + "' is listed twice");
    }
    if (const auto both = fileAndFolder(paths))
        return failed(error, "'" + std::string{*both} + "' is both a file and a folder");
    std::uint64_t declared = 0;
    for (const auto& f : files)
        declared += f.size; // each at most pakMaxFileSize (2^31), so no overflow below 2^32 files
    if (declared > pakMaxTotalSize)
        return failed(error, "the files are too large together (" + std::to_string(declared) + " bytes, at most " +
                                 std::to_string(pakMaxTotalSize) + ")");

    std::uint64_t total = pakMagic.size() + 4 + 4 + 4 + mountPoint.size();
    for (const auto& f : files)
        total += 4 + f.path.size() + 8 + 8 + 8 + 4 + f.stored.size();
    if (total > pakMaxArchiveSize)
        return failed(error, "the archive would be too large (" + std::to_string(total) + " bytes, at most " +
                                 std::to_string(pakMaxArchiveSize) + ")");

    std::vector<std::byte> out;
    for (const char c : pakMagic)
        out.push_back(static_cast<std::byte>(c));
    putU32(out, pakVersion);
    putU32(out, static_cast<std::uint32_t>(files.size()));
    putString(out, mountPoint);

    std::size_t indexSize = 0;
    for (const auto& f : files)
        indexSize += 4 + f.path.size() + 8 + 8 + 8 + 4;
    std::uint64_t offset = out.size() + indexSize;
    for (const auto& f : files) {
        putString(out, f.path);
        putU64(out, offset);
        putU64(out, f.stored.size());
        putU64(out, f.size);
        putU32(out, f.compressed ? flagCompressed : 0);
        offset += f.stored.size();
    }
    for (const auto& f : files)
        out.insert(out.end(), f.stored.begin(), f.stored.end());
    return out;
}

std::optional<PakIndex> parsePak(std::span<const std::byte> archive, std::string* error) {
    Reader in{archive};
    if (in.string(pakMagic.size()) != pakMagic || !in.ok()) return failed(error, "not a .pak archive (bad magic)");
    if (archive.size() > pakMaxArchiveSize) return failed(error, "too large for a .pak archive");
    const auto version = in.number(4);
    if (!in.ok()) return failed(error, "truncated header");
    if (version != pakVersion) return failed(error, "unsupported .pak version " + std::to_string(version));
    const auto count = in.number(4);
    PakIndex index;
    index.mountPoint = in.string(static_cast<std::size_t>(in.number(4)));
    if (!in.ok()) return failed(error, "truncated header");
    // A folder, spelled canonically with its trailing slash: Resources mounts any name as a folder, so a file name
    // (`app:/main.lua`) would silently become one.
    if (const auto folder = normalizeName(index.mountPoint);
        !folder || (folder->ends_with('/') ? *folder : *folder + "/") != index.mountPoint)
        return failed(error, "invalid mount point '" + index.mountPoint +
                                 "' (a full folder name ending in '/', such as 'app:/' or 'imlui:/fonts/')");
    std::set<std::string, std::less<>> seen;
    std::uint64_t declared = 0;

    for (std::uint64_t i = 0; i < count; ++i) {
        PakIndexEntry e;
        e.path = in.string(static_cast<std::size_t>(in.number(4)));
        e.offset = in.number(8);
        e.storedSize = in.number(8);
        e.size = in.number(8);
        const auto flags = in.number(4);
        if (!in.ok()) return failed(error, "truncated index");
        e.compressed = (flags & flagCompressed) != 0;
        // Before anything else looks at the path: the checks below are linear in its length, but not all of them
        // are once a path is compared with many others.
        if (const std::string problem = pathLimitProblem(e.path); !problem.empty())
            return failed(error, "path '" + shown(e.path) + "' in the index " + problem);
        const auto normalized = normalizePath(e.path);
        if (!normalized || *normalized != e.path || e.path.empty())
            return failed(error, "invalid path '" + e.path + "' in the index");
        if (e.offset > archive.size() || e.storedSize > archive.size() - e.offset)
            return failed(error, "'" + e.path + "' lies outside the archive (truncated?)");
        if (e.size > pakMaxFileSize || (!e.compressed && e.size != e.storedSize))
            return failed(error, "'" + e.path + "' has an invalid size");
        if (!seen.insert(e.path).second) return failed(error, "'" + e.path + "' is in the index twice");
        declared += e.size;
        if (declared > pakMaxTotalSize)
            return failed(error, "the files are too large together (more than " + std::to_string(pakMaxTotalSize) +
                                     " bytes)");
        index.entries.push_back(std::move(e));
    }
    if (const auto both = fileAndFolder(seen))
        return failed(error, "'" + std::string{*both} + "' is both a file and a folder");

    // Every file's bytes lie past the index, and no two files share any: a file's bytes are its own.
    std::vector<const PakIndexEntry*> blobs;
    for (const auto& e : index.entries)
        if (e.storedSize > 0) blobs.push_back(&e);
    std::ranges::sort(blobs, {}, &PakIndexEntry::offset);
    std::uint64_t end = in.position();
    const PakIndexEntry* previous = nullptr;
    for (const PakIndexEntry* e : blobs) {
        if (e->offset < end)
            return failed(error, "'" + e->path + "' overlaps " +
                                     (previous ? "'" + previous->path + "'" : std::string{"the index"}));
        end = e->offset + e->storedSize;
        previous = e;
    }
    return index;
}

} // namespace imlui
