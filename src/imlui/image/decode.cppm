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

/// The most bytes of RGBA pixels (4 per pixel) decodeImage() decodes an image to: 256 MiB, 8192 x 8192 pixels, the
/// image cache's default budget (ImageCache::defaultBudget). A small file can declare a huge image (a flat 16384 x
/// 16384 PNG is about 1 MiB and decodes to 1 GiB, and its texture takes as much again), so the size is checked from
/// the header before anything is decoded.
inline constexpr std::size_t maxImageBytes = std::size_t{256} << 20;

/// Decodes a PNG or JPEG file held in `bytes`. Any other format, a damaged file, one larger than 16384 pixels on a
/// side or one whose pixels would take more than maxImageBytes gives an image with `error` set and no pixels.
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
    // The header alone gives the size (a file whose header doesn't parse fails in the decoder, with its reason).
    if (int w = 0, h = 0; stbi::stbi_info_from_memory(data, size, &w, &h, &channels) && w > 0 && h > 0 &&
                          static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4 > maxImageBytes) {
        out.error =
            std::format("too large ({} x {} pixels; at most {} MiB of pixels are decoded)", w, h, maxImageBytes >> 20);
        return out;
    }
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
