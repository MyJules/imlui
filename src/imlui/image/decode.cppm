// decodeImage(): PNG and JPEG bytes to RGBA pixels, with stb_image.
export module imlui.image.decode;

import std;

import stb_image;

export namespace imlui {

/// A decoded image: `width` x `height` RGBA pixels, 8 bits per channel, rows top to bottom, straight alpha.
/// Move-only: it owns the decoder's buffer, so the pixels aren't copied out of it.
struct DecodedImage {
    /// Frees the decoder's buffer.
    struct FreePixels {
        void operator()(std::uint8_t* buffer) const;
    };

    int width = 0;
    int height = 0;
    std::span<const std::uint8_t> rgba; ///< `width * height * 4` bytes in `pixels`; empty on error
    std::string error;                  ///< why decoding failed; "" on success
    std::unique_ptr<std::uint8_t[], FreePixels> pixels;

    [[nodiscard]] bool ok() const;
};

/// Decodes a PNG or JPEG file held in `bytes`. Any other format, a damaged file or one larger than 16384 pixels
/// on a side gives an image with `error` set and no pixels.
[[nodiscard]] DecodedImage decodeImage(std::span<const std::byte> bytes);

} // namespace imlui

module :private;

namespace imlui {

void DecodedImage::FreePixels::operator()(std::uint8_t* buffer) const { stbi::stbi_image_free(buffer); }

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
    // Kept, not copied: an image is held once here and once in its texture while it is uploaded.
    out.pixels.reset(pixels);
    out.rgba = {pixels, static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4};
    return out;
}

} // namespace imlui
