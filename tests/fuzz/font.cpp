// Fuzz target `font`: font data from untrusted bytes (an app's font resource). checkFont() decides; a font it accepts
// goes to stb_truetype, read the way ImGui reads it (imlui.fuzz.truetype), which must not crash, assert or read out
// of bounds: stb_truetype trusts the data, and checkFont() is what stands between it and a damaged font.
import std;

import imlui.theme.font_check;
import imlui.fuzz.truetype;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // A copy of exactly `size` bytes, so ASan sees any read past the end (the fuzzer's buffer may be larger).
    const auto* first = reinterpret_cast<const std::byte*>(data);
    const std::vector<std::byte> bytes{first, first + size};
    if (const auto problem = imlui::checkFont(bytes)) {
        if (problem->empty()) std::abort();
        return 0;
    }
    imlui::fuzz::readFontLikeImGui(bytes);
    return 0;
}
