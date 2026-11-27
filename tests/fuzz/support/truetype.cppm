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

import imlui.theme.font_check;

export namespace imlui::fuzz {

/// Does with font `bytes` what ImGui does with a font checkFont() accepted: FontSrcInit (stbtt_GetFontOffsetForIndex,
/// stbtt_InitFont, the scale), FontBakedInit (the vertical metrics), FontSrcContainsGlyph and FontBakedLoadGlyph
/// (stbtt_FindGlyphIndex for every code point an ImWchar holds, up to imlui::lastCodepoint; for a glyph other than 0,
/// its box and horizontal metrics, and its bitmap: stbtt_MakeGlyphBitmapSubpixelPrefilter with 2x horizontal
/// oversampling, at a size from 4 to 64px picked by the input's length). Those glyphs are the ones checkFont() checks,
/// and ImGui reaches no other: glyph 0 (a code point the font lacks) it never loads. Aborts when stb_truetype fails to
/// load the font, asserts, or maps a code point to a glyph the font doesn't have, or when ImGui would refuse a glyph's
/// bitmap size: checkFont() promises none of that happens. To keep each run short, it decodes the outline of every
/// glyph found that ImGui would rasterize (stbtt_GetGlyphShape, up to 8192 glyphs) but rasterizes only up to 64 of
/// them, spread over the font.
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
    // ImGui's ScaleFactor (FontSrcInit) times the font size.
    const float scale = stbtt_ScaleForPixelHeight(&font, 1.0f) * static_cast<float>(4 + bytes.size() % 61);
    if (!std::isfinite(scale)) std::abort();
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);

    const auto glyphs = static_cast<std::size_t>(std::max(font.numGlyphs, 0));
    const auto loadGlyph = [&](int glyph) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0, advance = 0, lsb = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&font, glyph, scale, scale, 0, 0, &x0, &y0, &x1, &y1);
        stbtt_GetGlyphHMetrics(&font, glyph, &advance, &lsb);
    };
    // All the code points ImGui asks a font for. It loads nothing for glyph 0 (FontBakedLoadGlyph returns false), so
    // the glyphs found are all it ever reaches (glyph 0 included, as a part of a composite one).
    std::vector<bool> loaded(glyphs);
    std::vector<int> found;
    for (int codepoint = 0; codepoint <= imlui::lastCodepoint; ++codepoint) {
        const int glyph = stbtt_FindGlyphIndex(&font, codepoint);
        if (glyph < 0 || glyph >= font.numGlyphs) std::abort();
        if (glyph == 0 || loaded[static_cast<std::size_t>(glyph)]) continue;
        loaded[static_cast<std::size_t>(glyph)] = true;
        found.push_back(glyph);
        loadGlyph(glyph);
    }

    // The outline of every glyph found that ImGui rasterizes, as stbtt_MakeGlyphBitmap* decodes it. Not those whose
    // box is 0 wide or high at 0: ImGui never rasterizes them, and checkFont() lets their outline make stb_truetype
    // read one vertex past its array (placeholders, a lone point off the curve, as in Calibri).
    for (std::size_t i = 0; i < std::min<std::size_t>(found.size(), 8192); ++i) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBox(&font, found[i], &x0, &y0, &x1, &y1);
        if ((x0 == 0 && x1 == 0) || (y0 == 0 && y1 == 0)) continue;
        stbtt_vertex* vertices = nullptr;
        static_cast<void>(stbtt_GetGlyphShape(&font, found[i], &vertices));
        stbtt_FreeShape(&font, vertices);
    }

    // FontBakedLoadGlyph's bitmap, for a spread of the glyphs found (the offset varies with the input).
    constexpr int oversampleH = 2, oversampleV = 1;
    constexpr std::size_t maxRasterized = 64;
    std::vector<unsigned char> pixels;
    const std::size_t step = std::max<std::size_t>(1, found.size() / maxRasterized);
    for (std::size_t i = bytes.size() % step; i < found.size(); i += step) {
        const int glyph = found[i];
        const float scaleX = scale * oversampleH, scaleY = scale * oversampleV;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&font, glyph, scaleX, scaleY, 0, 0, &x0, &y0, &x1, &y1);
        if (x0 == x1 || y0 == y1) continue; // ImGui's is_visible
        const int w = x1 - x0 + oversampleH - 1, h = y1 - y0 + oversampleV - 1;
        // ImGui asserts on a size <= 0 (an inverted box) and on one its atlas can't hold. At most 64px, a glyph
        // checkFont() accepts is at most 8 font heights: 512 x 2 pixels.
        if (w <= 0 || h <= 0 || w > 2 * 512 + 4 || h > 512 + 4) std::abort();
        pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        float subX = 0, subY = 0;
        stbtt_MakeGlyphBitmapSubpixelPrefilter(&font, pixels.data(), w, h, w, scaleX, scaleY, 0, 0, oversampleH,
                                               oversampleV, &subX, &subY, glyph);
    }
}

} // namespace imlui::fuzz
