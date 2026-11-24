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

/// The bytes of an archive holding `files`, mounted at `mountPoint`.
[[nodiscard]] std::vector<std::byte> writePak(std::string_view mountPoint, std::span<const PakFile> files);

/// Parses and checks the header and index of `archive`: the magic and version, a valid mount point, normalized
/// paths, and every file inside the archive. nullopt (with a reason in `error`, when given) for anything else.
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

std::optional<PakIndex> failed(std::string* error, std::string reason) {
    if (error) *error = std::move(reason);
    return std::nullopt;
}

} // namespace

std::vector<std::byte> writePak(std::string_view mountPoint, std::span<const PakFile> files) {
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
    const auto version = in.number(4);
    if (!in.ok()) return failed(error, "truncated header");
    if (version != pakVersion) return failed(error, "unsupported .pak version " + std::to_string(version));
    const auto count = in.number(4);
    PakIndex index;
    index.mountPoint = in.string(static_cast<std::size_t>(in.number(4)));
    if (!in.ok()) return failed(error, "truncated header");
    if (!normalizeName(index.mountPoint)) return failed(error, "invalid mount point '" + index.mountPoint + "'");

    for (std::uint64_t i = 0; i < count; ++i) {
        PakIndexEntry e;
        e.path = in.string(static_cast<std::size_t>(in.number(4)));
        e.offset = in.number(8);
        e.storedSize = in.number(8);
        e.size = in.number(8);
        const auto flags = in.number(4);
        if (!in.ok()) return failed(error, "truncated index");
        e.compressed = (flags & flagCompressed) != 0;
        const auto normalized = normalizePath(e.path);
        if (!normalized || *normalized != e.path || e.path.empty())
            return failed(error, "invalid path '" + e.path + "' in the index");
        if (e.offset > archive.size() || e.storedSize > archive.size() - e.offset)
            return failed(error, "'" + e.path + "' lies outside the archive (truncated?)");
        if (e.size > pakMaxFileSize || (!e.compressed && e.size != e.storedSize))
            return failed(error, "'" + e.path + "' has an invalid size");
        index.entries.push_back(std::move(e));
    }
    return index;
}

} // namespace imlui
