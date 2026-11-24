// decodeImage(): PNG and JPEG bytes to RGBA pixels, with stb_image.
module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

export module imlui.image.decode;

import stb_image;

export namespace imlui {

/// A decoded image: `width` x `height` RGBA pixels, 8 bits per channel, rows top to bottom, straight alpha.
struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::string error; ///< why decoding failed; "" on success

    [[nodiscard]] bool ok() const;
};

/// Decodes a PNG or JPEG file held in `bytes`. Any other format, a damaged file or one larger than 16384 pixels
/// on a side gives an image with `error` set and no pixels.
[[nodiscard]] DecodedImage decodeImage(std::span<const std::byte> bytes);

} // namespace imlui

module :private;

namespace imlui {

bool DecodedImage::ok() const { return error.empty() && width > 0 && height > 0; }

DecodedImage decodeImage(std::span<const std::byte> bytes) {
    DecodedImage out;
    if (bytes.empty()) {
        out.error = "empty file";
        return out;
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        out.error = "file too large";
        return out;
    }
    const auto* data = reinterpret_cast<const stbi::stbi_uc*>(bytes.data());
    const int size = static_cast<int>(bytes.size());
    int channels = 0;
    stbi::stbi_uc* pixels = stbi::stbi_load_from_memory(data, size, &out.width, &out.height, &channels, 4);
    if (!pixels) {
        const char* reason = stbi::stbi_failure_reason();
        out.error = std::string{"not a PNG or JPEG image ("} + (reason ? reason : "unknown error") + ")";
        out.width = out.height = 0;
        return out;
    }
    const std::size_t count = static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4;
    out.rgba.resize(count);
    std::memcpy(out.rgba.data(), pixels, count);
    stbi::stbi_image_free(pixels);
    return out;
}

} // namespace imlui
