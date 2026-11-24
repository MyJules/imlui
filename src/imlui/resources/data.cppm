// ResourceData: the bytes of one resource file as a mount returned them, and what keeps them alive.
module;

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

export module imlui.resources.data;

export namespace imlui {

/// The bytes of a resource file (Mount::read, Resources::read). A cheap, copyable handle.
///
/// Lifetime: the bytes stay valid as long as this handle, or any copy of it, exists **and** the mount that returned
/// it exists. Immutable mounts (embedded, .pak) serve their own memory (zero-copy; `owner()` is null) and keep it
/// for their lifetime. A directory mount reads a file into a buffer that the handles share (`owner()` holds it): once
/// the file changed and the last handle to the old bytes is gone, they are freed. So keep the handle, not just the
/// span, for as long as you use the bytes (the font library keeps one per atlas font, whose data it doesn't copy).
class ResourceData {
public:
    ResourceData() = default;
    /// `bytes` kept alive by `owner` (null: by the mount itself).
    explicit ResourceData(std::span<const std::byte> bytes, std::shared_ptr<const void> owner = {});

    [[nodiscard]] std::span<const std::byte> bytes() const;
    [[nodiscard]] const std::byte* data() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool empty() const;
    /// The bytes as text.
    [[nodiscard]] std::string_view text() const;
    /// What keeps the bytes alive besides the mount; null for memory the mount owns for its whole lifetime.
    [[nodiscard]] const std::shared_ptr<const void>& owner() const;

private:
    std::span<const std::byte> m_bytes;
    std::shared_ptr<const void> m_owner;
};

} // namespace imlui

module :private;

namespace imlui {

ResourceData::ResourceData(std::span<const std::byte> bytes, std::shared_ptr<const void> owner)
    : m_bytes{bytes}, m_owner{std::move(owner)} {}

std::span<const std::byte> ResourceData::bytes() const { return m_bytes; }
const std::byte* ResourceData::data() const { return m_bytes.data(); }
std::size_t ResourceData::size() const { return m_bytes.size(); }
bool ResourceData::empty() const { return m_bytes.empty(); }
std::string_view ResourceData::text() const { return {reinterpret_cast<const char*>(m_bytes.data()), m_bytes.size()}; }
const std::shared_ptr<const void>& ResourceData::owner() const { return m_owner; }

} // namespace imlui
