// Fuzz target `image`: PNG or JPEG bytes from untrusted data (an app's image resource), decoded by decodeImage()
// (stb_image). Nothing may crash; a decoded image has exactly width x height RGBA pixels.
import std;

import imlui.image.decode;
import stb_image;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

/// Images with more pixels aren't decoded: up to maxImageBytes (8192 x 8192) is allowed, which is only slow, not
/// wrong, and would trip the fuzzer's memory limit.
constexpr long long maxPixels = 4096LL * 4096;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) return 0;
    int width = 0, height = 0, channels = 0;
    if (stbi::stbi_info_from_memory(data, static_cast<int>(size), &width, &height, &channels) &&
        static_cast<long long>(width) * height > maxPixels)
        return 0;

    const auto* first = reinterpret_cast<const std::byte*>(data);
    const std::vector<std::byte> bytes{first, first + size};
    const auto image = imlui::decodeImage(bytes);
    if (image.ok()) {
        if (image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4)
            std::abort();
    } else if (image.error.empty() || !image.rgba.empty()) {
        std::abort();
    }
    return 0;
}
