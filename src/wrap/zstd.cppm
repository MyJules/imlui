// Module wrapper around the zstd decompressor (external/zstd). The runtime only decompresses: files are compressed
// at build time by the resource tools (tools/), which wrap the compressor themselves (module zstd.compress).
module;

#include <zstd.h>

export module zstd;

import std;

export namespace zstd {

/// The decompressed size a zstd frame declares in its header; nullopt when `frame` isn't a zstd frame or doesn't
/// declare its size.
[[nodiscard]] std::optional<std::uint64_t> contentSize(std::span<const std::byte> frame);

/// Decompresses `frame` into `out`, which must be exactly the decompressed size. False when the data is corrupt or
/// doesn't decompress to exactly `out.size()` bytes.
[[nodiscard]] bool decompress(std::span<const std::byte> frame, std::span<std::byte> out);

} // namespace zstd

module :private;

namespace zstd {

std::optional<std::uint64_t> contentSize(std::span<const std::byte> frame) {
    const unsigned long long size = ZSTD_getFrameContentSize(frame.data(), frame.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN) return std::nullopt;
    return static_cast<std::uint64_t>(size);
}

bool decompress(std::span<const std::byte> frame, std::span<std::byte> out) {
    const std::size_t written = ZSTD_decompress(out.data(), out.size(), frame.data(), frame.size());
    return ZSTD_isError(written) == 0 && written == out.size();
}

} // namespace zstd
