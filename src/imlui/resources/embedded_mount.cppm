// EmbeddedMount: files compiled into the executable. The modules that imlui_add_resources (cmake/ImluiResources.cmake)
// generates hold the data (`#embed`) and a static table of records, and build their mount with this class.
module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module imlui.resources.embedded_mount;

import imlui.resources.packed_mount;

export namespace imlui {

/// The files of one embedded block: a blob of bytes in the executable and a table that places each file in it.
/// Compressed files are decompressed on first read and cached for the mount's lifetime (see PackedMount).
class EmbeddedMount final : public PackedMount {
public:
    /// One file of the table, as the generated module writes it.
    struct Record {
        std::string_view path;        ///< normalized path in the mount
        std::uint64_t offset = 0;     ///< where the stored bytes start in the blob
        std::uint64_t storedSize = 0; ///< how many bytes are stored
        std::uint64_t size = 0;       ///< the file's size once decompressed
        bool compressed = false;      ///< the stored bytes are one zstd frame
    };

    /// `name` is for origin(). `blob` must outlive the mount (static data). Records that don't fit in the blob are
    /// dropped.
    EmbeddedMount(std::string name, std::span<const std::byte> blob, std::span<const Record> records);

    [[nodiscard]] std::string origin() const override;

private:
    std::string m_name;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

std::vector<PackedMount::Entry> entriesOf(std::span<const std::byte> blob,
                                          std::span<const EmbeddedMount::Record> records) {
    std::vector<PackedMount::Entry> out;
    out.reserve(records.size());
    for (const auto& r : records) {
        if (r.offset > blob.size() || r.storedSize > blob.size() - r.offset) continue;
        out.push_back({std::string{r.path},
                       blob.subspan(static_cast<std::size_t>(r.offset), static_cast<std::size_t>(r.storedSize)), r.size,
                       r.compressed});
    }
    return out;
}

} // namespace

EmbeddedMount::EmbeddedMount(std::string name, std::span<const std::byte> blob, std::span<const Record> records)
    : PackedMount{entriesOf(blob, records)}, m_name{std::move(name)} {}

std::string EmbeddedMount::origin() const { return "embedded '" + m_name + "'"; }

} // namespace imlui
