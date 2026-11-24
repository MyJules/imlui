// Module wrapper around the zstd compressor (external/zstd), for the build-time resource tools only: the runtime
// links just the decompressor (src/wrap/zstd.cppm).
module;

#include <zstd.h>

export module zstd.compress;

import std;

export namespace zstd {

/// The highest standard compression level: resources are compressed once, at build time.
inline constexpr int maxLevel = 19;

/// `data` as one zstd frame that declares its decompressed size; nullopt if zstd fails.
[[nodiscard]] std::optional<std::vector<std::byte>> compress(std::span<const std::byte> data, int level = maxLevel);

} // namespace zstd

module :private;

namespace zstd {

std::optional<std::vector<std::byte>> compress(std::span<const std::byte> data, int level) {
    std::vector<std::byte> out(ZSTD_compressBound(data.size()));
    const std::size_t written = ZSTD_compress(out.data(), out.size(), data.data(), data.size(), level);
    if (ZSTD_isError(written) != 0) return std::nullopt;
    out.resize(written);
    return out;
}

} // namespace zstd
