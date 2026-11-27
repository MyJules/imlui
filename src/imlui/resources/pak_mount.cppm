// PakMount: a .pak archive (see imlui.resources.pak_format) as a resource mount, e.g. `data.pak` next to the
// executable. The archive is read into memory once, when it's opened; stored files are served from that buffer,
// compressed ones are decompressed when read (see PackedMount).
export module imlui.resources.pak_mount;

import std;

import imlui.resources.packed_mount;
import imlui.resources.pak_format;

export namespace imlui {

/// The files of one .pak archive, held in memory. The archive names its own mount point (mountPoint()).
class PakMount final : public PackedMount {
public:
    /// Reads and checks the archive at `file`. nullptr, with the reason in `error` when given, when it can't be
    /// read or isn't a valid archive. Its header is checked before the file is read whole, so a large file that
    /// isn't an archive costs nothing, and one larger than pakMaxArchiveSize or too large to allocate fails cleanly.
    /// Never throws.
    [[nodiscard]] static std::shared_ptr<PakMount> open(const std::filesystem::path& file,
                                                        std::string* error = nullptr);
    /// The archive in `bytes`; `name` is for origin(). Never throws.
    [[nodiscard]] static std::shared_ptr<PakMount> fromBytes(std::vector<std::byte> bytes, std::string name,
                                                             std::string* error = nullptr);

    /// The full folder name the archive asks to be mounted at (`app:/`, `imlui:/fonts/`).
    [[nodiscard]] const std::string& mountPoint() const;
    [[nodiscard]] std::string origin() const override;

private:
    PakMount(std::vector<std::byte> bytes, std::vector<Entry> entries, std::string mountPoint, std::string name);

    std::vector<std::byte> m_bytes; ///< the whole archive; the entries point into it
    std::string m_mountPoint;
    std::string m_name;
};

} // namespace imlui

module :private;

namespace imlui {

namespace fs = std::filesystem;

PakMount::PakMount(std::vector<std::byte> bytes, std::vector<Entry> entries, std::string mountPoint, std::string name)
    : PackedMount{std::move(entries)}, m_bytes{std::move(bytes)}, m_mountPoint{std::move(mountPoint)},
      m_name{std::move(name)} {}

std::shared_ptr<PakMount> PakMount::fromBytes(std::vector<std::byte> bytes, std::string name, std::string* error) {
    try {
        auto index = parsePak(bytes, error);
        if (!index) return nullptr;
        // The entries point into the vector's heap buffer, which moving the vector into the mount doesn't move.
        const std::span<const std::byte> archive{bytes};
        std::vector<Entry> entries;
        entries.reserve(index->entries.size());
        for (auto& e : index->entries)
            entries.push_back(
                {std::move(e.path),
                 archive.subspan(static_cast<std::size_t>(e.offset), static_cast<std::size_t>(e.storedSize)), e.size,
                 e.compressed});
        return std::shared_ptr<PakMount>{
            new PakMount{std::move(bytes), std::move(entries), std::move(index->mountPoint), std::move(name)}};
    } catch (const std::exception& e) { // out of memory for the index of a huge archive
        if (error) *error = e.what();
        return nullptr;
    }
}

std::shared_ptr<PakMount> PakMount::open(const fs::path& file, std::string* error) {
    std::string name;
    const auto failed = [&](std::string_view reason) -> std::shared_ptr<PakMount> {
        if (error) *error = "'" + name + "': " + std::string{reason};
        return nullptr;
    };
    try {
        const std::u8string u8 = file.generic_u8string();
        name.assign(u8.begin(), u8.end());
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(file, ec);
        std::ifstream in{file, std::ios::binary};
        if (ec || !in) return failed("can't read it");
        // The magic first: whatever else lies next to the executable under a .pak name isn't read whole.
        std::array<char, pakMagic.size()> magic{};
        if (!in.read(magic.data(), static_cast<std::streamsize>(magic.size())) ||
            std::string_view{magic.data(), magic.size()} != pakMagic)
            return failed("not a .pak archive (bad magic)");
        if (size > pakMaxArchiveSize || size > std::numeric_limits<std::size_t>::max())
            return failed("too large for a .pak archive (" + std::to_string(size) + " bytes)");
        std::vector<std::byte> bytes;
        try {
            bytes.resize(static_cast<std::size_t>(size));
        } catch (const std::bad_alloc&) {
            return failed("out of memory reading " + std::to_string(size) + " bytes");
        }
        in.seekg(0);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        bytes.resize(static_cast<std::size_t>(in.gcount()));
        std::string reason;
        auto pak = fromBytes(std::move(bytes), name, &reason);
        if (!pak) return failed(reason);
        return pak;
    } catch (const std::exception& e) {
        return failed(e.what());
    }
}

const std::string& PakMount::mountPoint() const { return m_mountPoint; }

std::string PakMount::origin() const { return "pak '" + m_name + "'"; }

} // namespace imlui
