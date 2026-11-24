// PakMount: a .pak archive (see imlui.resources.pak_format) as a resource mount, e.g. `data.pak` next to the
// executable. The archive is read into memory once, when it's opened; stored files are served from that buffer,
// compressed ones are decompressed on first read (see PackedMount).
export module imlui.resources.pak_mount;

import std;

import imlui.resources.packed_mount;
import imlui.resources.pak_format;

export namespace imlui {

/// The files of one .pak archive, held in memory. The archive names its own mount point (mountPoint()).
class PakMount final : public PackedMount {
public:
    /// Reads and checks the archive at `file`. nullptr, with the reason in `error` when given, when it can't be
    /// read or isn't a valid archive.
    [[nodiscard]] static std::shared_ptr<PakMount> open(const std::filesystem::path& file,
                                                        std::string* error = nullptr);
    /// The archive in `bytes`; `name` is for origin().
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
    auto index = parsePak(bytes, error);
    if (!index) return nullptr;
    // The entries point into the vector's heap buffer, which moving the vector into the mount doesn't move.
    const std::span<const std::byte> archive{bytes};
    std::vector<Entry> entries;
    entries.reserve(index->entries.size());
    for (auto& e : index->entries)
        entries.push_back({std::move(e.path),
                           archive.subspan(static_cast<std::size_t>(e.offset), static_cast<std::size_t>(e.storedSize)),
                           e.size, e.compressed});
    return std::shared_ptr<PakMount>{
        new PakMount{std::move(bytes), std::move(entries), std::move(index->mountPoint), std::move(name)}};
}

std::shared_ptr<PakMount> PakMount::open(const fs::path& file, std::string* error) {
    const std::u8string u8 = file.generic_u8string();
    std::string name{u8.begin(), u8.end()};
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    std::ifstream in{file, std::ios::binary};
    if (ec || !in) {
        if (error) *error = "can't read '" + name + "'";
        return nullptr;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    std::string reason;
    auto pak = fromBytes(std::move(bytes), name, &reason);
    if (!pak && error) *error = "'" + name + "': " + reason;
    return pak;
}

const std::string& PakMount::mountPoint() const { return m_mountPoint; }

std::string PakMount::origin() const { return "pak '" + m_name + "'"; }

} // namespace imlui
