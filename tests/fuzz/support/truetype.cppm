// imlui.fuzz.truetype: reads a font with stb_truetype the way ImGui's loader does (imgui_draw.cpp,
// ImGui_ImplStbTrueType_*), for the `font` fuzz target. Its own copy of stb_truetype (ImGui compiles its copy static
// into imgui_draw.cpp), the same version, with STBTT_assert aborting like ImGui's IM_ASSERT does in imlui.
module;

#include <stdlib.h> // abort, for STBTT_assert

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x)                                                                                                \
    do {                                                                                                               \
        if (!(x)) abort();                                                                                             \
    } while (0)
#include "imstb_truetype.h"

export module imlui.fuzz.truetype;

import std;

export namespace imlui::fuzz {

/// Does with font `bytes` what ImGui does with a font checkFont() accepted, short of drawing glyph outlines (which
/// checkFont() doesn't check): FontSrcInit (stbtt_GetFontOffsetForIndex, stbtt_InitFont), FontBakedInit (the vertical
/// metrics), FontSrcContainsGlyph and FontBakedLoadGlyph's metrics (stbtt_FindGlyphIndex for every code point an
/// ImWchar holds, the glyph box and horizontal metrics). Aborts when stb_truetype fails to load the font, asserts, or
/// maps a code point to a glyph the font doesn't have: checkFont() promises none of that happens.
void readFontLikeImGui(std::span<const std::byte> bytes);

} // namespace imlui::fuzz

module :private;

namespace imlui::fuzz {

void readFontLikeImGui(std::span<const std::byte> bytes) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0) std::abort();
    stbtt_fontinfo font{};
    if (!stbtt_InitFont(&font, data, offset)) std::abort();
    const float scale = stbtt_ScaleForPixelHeight(&font, 16.0f);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);

    const auto loadGlyph = [&](int glyph) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0, advance = 0, lsb = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&font, glyph, scale, scale, 0, 0, &x0, &y0, &x1, &y1);
        stbtt_GetGlyphHMetrics(&font, glyph, &advance, &lsb);
    };
    // ImWchar is 16 bits (no IMGUI_USE_WCHAR32): these are all the code points ImGui asks a font for.
    std::vector<bool> loaded(static_cast<std::size_t>(std::max(font.numGlyphs, 0)));
    for (int codepoint = 0; codepoint <= 0xFFFF; ++codepoint) {
        const int glyph = stbtt_FindGlyphIndex(&font, codepoint);
        if (glyph < 0 || glyph >= font.numGlyphs) std::abort();
        if (glyph == 0 || loaded[static_cast<std::size_t>(glyph)]) continue;
        loaded[static_cast<std::size_t>(glyph)] = true;
        loadGlyph(glyph);
    }
    loadGlyph(0); // .notdef, for code points the font lacks
}

} // namespace imlui::fuzz
