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

/// The most bytes of RGBA pixels decodeImage() decodes a progressive JPEG to: 128 MiB, about 5792 x 5792 pixels. stb_image
/// keeps every DCT coefficient of a progressive JPEG until its last scan, 2 bytes per sample (6 per pixel at full
/// chroma resolution), next to its per-component planes and the output: about twice a baseline JPEG's peak memory,
/// so half the pixels give about the same peak.
inline constexpr std::size_t maxProgressiveJpegBytes = maxImageBytes / 2;

/// Decodes a PNG or JPEG file held in `bytes`. Any other format, a damaged file, one larger than 16384 pixels on a
/// side or one whose pixels would take more than maxImageBytes (maxProgressiveJpegBytes for a progressive JPEG) gives an
/// image with `error` set and no pixels.
[[nodiscard]] DecodedImage decodeImage(std::span<const std::byte> bytes);

} // namespace imlui

module :private;

namespace imlui {

void DecodedImage::FreePixels::operator()(std::uint8_t* buffer) const { stbi::stbi_image_free(buffer); }

namespace {

/// Whether `bytes` is a progressive JPEG: its frame header (the first SOFn marker, which stb_image reads before any
/// scan) is SOF2. Walks the marker segments before it by their lengths, so only the headers are read.
bool progressiveJpeg(std::span<const std::byte> bytes) {
    const auto at = [&](std::size_t i) { return std::to_integer<unsigned>(bytes[i]); };
    if (bytes.size() < 4 || at(0) != 0xFF || at(1) != 0xD8) return false; // not a JPEG (SOI)
    std::size_t i = 2;
    while (i < bytes.size()) {
        if (at(i) != 0xFF) return false; // not a marker: stb_image rejects it too
        while (i < bytes.size() && at(i) == 0xFF) // fill bytes
            ++i;
        if (i >= bytes.size()) return false;
        const unsigned marker = at(i++);
        if (marker == 0xC2) return true;
        // Another frame header (C0-CF but DHT C4, JPG C8, DAC CC); or the scan or the end before any.
        if ((marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) ||
            marker == 0xD9 || marker == 0xDA)
            return false;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue; // no length
        if (i + 2 > bytes.size()) return false;
        const std::size_t length = (at(i) << 8) | at(i + 1);
        if (length < 2) return false;
        i += length;
    }
    return false;
}

} // namespace

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
    if (int w = 0, h = 0; stbi::stbi_info_from_memory(data, size, &w, &h, &channels) && w > 0 && h > 0) {
        const bool progressive = progressiveJpeg(bytes);
        const std::size_t limit = progressive ? maxProgressiveJpegBytes : maxImageBytes;
        if (static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4 > limit) {
            out.error = std::format("too large ({} x {} pixels; at most {} MiB of pixels are decoded{})", w, h,
                                    limit >> 20, progressive ? " from a progressive JPEG" : "");
            return out;
        }
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
