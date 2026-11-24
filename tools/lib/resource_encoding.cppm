// How a resource file is stored, in embedded data and in .pak archives alike: compressed with zstd when that
// saves enough, as is otherwise.
module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module imlui.tools.resource_encoding;

import zstd.compress;

export namespace imlui::tools {

/// A file's bytes as they're stored.
struct EncodedResource {
    std::vector<std::byte> stored;
    std::uint64_t size = 0;  ///< the original size
    bool compressed = false; ///< `stored` is one zstd frame
};

/// Files smaller than this are stored as is: the frame overhead eats the gain.
inline constexpr std::size_t minCompressSize = 64;

/// True when a file named `path` is worth trying to compress: not an already-compressed format (PNG, JPEG, zstd,
/// zip, ...).
[[nodiscard]] bool compressible(std::string_view path);

/// Encodes file `path` with contents `data`: compressed when it's compressible(), at least minCompressSize bytes
/// and compression saves at least 10 %; stored as is otherwise.
[[nodiscard]] EncodedResource encodeResource(std::string_view path, std::span<const std::byte> data);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

bool compressible(std::string_view path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos || path.find('/', dot) != std::string_view::npos) return true;
    std::string ext{path.substr(dot + 1)};
    std::ranges::transform(ext, ext.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    constexpr std::string_view packed[] = {"png", "jpg", "jpeg", "gif",   "webp", "zst", "gz",  "zip",
                                           "7z",  "xz",  "bz2",  "woff2", "pak",  "mp3", "ogg", "mp4"};
    return std::ranges::find(packed, std::string_view{ext}) == std::end(packed);
}

EncodedResource encodeResource(std::string_view path, std::span<const std::byte> data) {
    EncodedResource out;
    out.size = data.size();
    if (data.size() >= minCompressSize && compressible(path)) {
        auto packed = zstd::compress(data);
        // Keep it only when it saves at least 10 %.
        if (packed && packed->size() * 10 <= data.size() * 9) {
            out.stored = std::move(*packed);
            out.compressed = true;
            return out;
        }
    }
    out.stored.assign(data.begin(), data.end());
    return out;
}

} // namespace imlui::tools
