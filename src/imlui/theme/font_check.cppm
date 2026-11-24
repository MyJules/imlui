// checkFont(): a structural check of TrueType/OpenType font data before it goes to ImGui.
//
// ImGui parses fonts with stb_truetype, which trusts the data: offsets are read without bounds checks, and data it
// doesn't expect trips IM_ASSERT_USER_ERROR or STBTT_assert, which abort the program. checkFont() walks everything
// stb_truetype reads when a font is added (stbtt_GetFontOffsetForIndex, stbtt_InitFont) and when a glyph is looked
// up (the character map, glyph locations, metrics, the CFF indexes), and rejects data that would make it read out of
// bounds or assert. It doesn't decode TrueType glyph outlines. CFF outlines are different: stb_truetype runs a glyph's
// charstring program to get its box (stbtt_GetGlyphBox, which ImGui calls for every glyph it loads), and a broken
// program trips an STBTT_assert there. Once the structure is known to be sound, checkFont() runs every charstring
// through its own copy of stb_truetype, whose STBTT_assert records the failure instead of aborting.
module;

#include <errno.h> // errno: where STBTT_assert records a failure

// stb_truetype, a static copy of the one ImGui compiles into imgui_draw.cpp (same file, so the same interpreter).
// STBTT_assert sets errno to -1, a value no library function sets, for checkCharstrings() to see: nothing else can
// be declared in this fragment, and an exception can't be used (stb_truetype's functions are extern "C", which the
// MSVC exception model assumes never throw). stb_truetype's buffer reads are bounds-checked and clamp where they
// assert, so it carries on safely; getting a glyph's box calls nothing else that sets errno.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x) ((x) ? (void)0 : (void)(errno = -1))
#include "imstb_truetype.h"

export module imlui.theme.font_check;

import std;

export namespace imlui {

/// Why `bytes` can't be handed to ImGui as a font (font 0 of a collection): `not a TrueType/OpenType font` when the
/// signature is wrong, else what is broken (`table 'glyf' lies outside the file`). nullopt when the font is fine.
[[nodiscard]] std::optional<std::string> checkFont(std::span<const std::byte> bytes);

/// The message checkFont() returns for data that doesn't start like a font at all.
constexpr std::string_view notAFont = "not a TrueType/OpenType font";

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// Big-endian reads with bounds checks.
class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : m_bytes{bytes} {}

    [[nodiscard]] std::size_t size() const { return m_bytes.size(); }
    [[nodiscard]] const unsigned char* data() const { return reinterpret_cast<const unsigned char*>(m_bytes.data()); }
    /// True when [offset, offset + length) lies inside the data.
    [[nodiscard]] bool has(std::size_t offset, std::size_t length) const {
        return offset <= m_bytes.size() && length <= m_bytes.size() - offset;
    }
    // Callers check has() first.
    [[nodiscard]] std::uint32_t u8(std::size_t at) const { return std::to_integer<std::uint32_t>(m_bytes[at]); }
    [[nodiscard]] std::uint32_t u16(std::size_t at) const { return (u8(at) << 8) | u8(at + 1); }
    [[nodiscard]] std::uint32_t u32(std::size_t at) const { return (u16(at) << 16) | u16(at + 2); }
    /// `count` bytes at `at` as an unsigned number (CFF offsets: 1 to 4 bytes).
    [[nodiscard]] std::uint32_t un(std::size_t at, std::uint32_t count) const {
        std::uint32_t v = 0;
        for (std::uint32_t i = 0; i < count; ++i)
            v = (v << 8) | u8(at + i);
        return v;
    }
    [[nodiscard]] bool tagIs(std::size_t at, std::string_view tag) const {
        for (std::size_t i = 0; i < 4; ++i)
            if (u8(at + i) != static_cast<unsigned char>(tag[i])) return false;
        return true;
    }

private:
    std::span<const std::byte> m_bytes;
};

struct Table {
    std::size_t offset = 0;
    std::size_t length = 0;
};

/// A slice of the data: [begin, end).
struct Range {
    std::size_t begin = 0;
    std::size_t end = 0;
};

using Problem = std::optional<std::string>;

/// The first table `tag` in the directory at `font` (the directory is checked already), like stb_truetype's lookup.
std::optional<Table> findTable(const Reader& r, std::size_t font, std::string_view tag) {
    const std::uint32_t count = r.u16(font + 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t record = font + 12 + 16 * std::size_t{i};
        if (r.tagIs(record, tag)) return Table{r.u32(record + 8), r.u32(record + 12)};
    }
    return std::nullopt;
}

// ------------------------------------------------------------------ CFF

/// Checks the CFF INDEX at `at` inside `cff` and sets `end` past it; the ranges of its first elements go to
/// `firstElements`. stb_truetype asserts on an offset size other than 1-4 and reads the offsets without bounds
/// checks.
Problem readIndex(const Reader& r, Range cff, std::size_t at, std::string_view what, std::size_t& end,
                  std::span<Range> firstElements = {}) {
    const std::string broken = "CFF " + std::string{what} + " is broken";
    if (at < cff.begin || at + 2 > cff.end) return broken;
    const std::uint32_t count = r.u16(at);
    if (count == 0) {
        end = at + 2;
        return std::nullopt;
    }
    if (at + 3 > cff.end) return broken;
    const std::uint32_t offSize = r.u8(at + 2);
    if (offSize < 1 || offSize > 4) return broken;
    const std::size_t offsets = at + 3;
    const std::size_t offsetsSize = (std::size_t{count} + 1) * offSize;
    if (offsets + offsetsSize > cff.end) return broken;
    const std::size_t data = offsets + offsetsSize - 1; // offsets count from 1
    std::uint32_t previous = 0;
    for (std::uint32_t i = 0; i <= count; ++i) {
        const std::uint32_t offset = r.un(offsets + std::size_t{i} * offSize, offSize);
        if (offset < 1 || offset < previous || data + offset > cff.end) return broken;
        if (i > 0 && i - 1 < firstElements.size()) firstElements[i - 1] = {data + previous, data + offset};
        previous = offset;
    }
    end = data + previous;
    return std::nullopt;
}

/// The integer operands of the operators stb_truetype reads from a CFF DICT (it asserts on anything but integers
/// there, and on the bytes 31 and 255 anywhere among the operands).
struct DictValues {
    std::uint32_t charStrings = 0;              ///< 17
    std::uint32_t charStringType = 2;           ///< 12 6
    std::uint32_t fdArray = 0;                  ///< 12 36
    std::uint32_t fdSelect = 0;                 ///< 12 37
    std::uint32_t subrs = 0;                    ///< 19 (in a Private DICT)
    std::array<std::uint32_t, 2> privateDict{}; ///< 18: size, offset
};

Problem readDict(const Reader& r, Range dict, std::string_view what, DictValues& out) {
    const std::string broken = "CFF " + std::string{what} + " is broken";
    std::array<std::uint32_t, 2> ints{};
    std::size_t intCount = 0;
    bool onlyInts = true;
    std::set<std::uint32_t> seen; ///< operators read so far
    std::size_t at = dict.begin;
    while (at < dict.end) {
        const std::uint32_t b0 = r.u8(at);
        if (b0 >= 28) { // an operand
            if (b0 == 31 || b0 == 255) return broken;
            std::uint32_t value = 0;
            std::size_t length = 1;
            if (b0 == 30) { // a real number: nibbles up to an 0xF
                onlyInts = false;
                std::size_t i = at + 1;
                for (;; ++i) {
                    if (i >= dict.end) return broken;
                    const std::uint32_t b = r.u8(i);
                    if ((b & 0x0F) == 0x0F || (b >> 4) == 0x0F) break;
                }
                length = i + 1 - at;
            } else if (b0 == 28) {
                length = 3;
            } else if (b0 == 29) {
                length = 5;
            } else if (b0 >= 247) {
                length = 2;
            }
            if (at + length > dict.end) return broken;
            if (b0 == 28)
                value = r.u16(at + 1);
            else if (b0 == 29)
                value = r.u32(at + 1);
            else if (b0 >= 32 && b0 <= 246)
                value = b0 - 139; // negative values wrap like stb_truetype's
            else if (b0 >= 247 && b0 <= 250)
                value = (b0 - 247) * 256 + r.u8(at + 1) + 108;
            else if (b0 >= 251 && b0 <= 254)
                value = 0u - ((b0 - 251) * 256 + r.u8(at + 1) + 108);
            if (intCount < ints.size()) ints[intCount] = value;
            ++intCount;
            at += length;
            continue;
        }
        std::uint32_t op = b0;
        if (b0 == 12) {
            if (at + 1 >= dict.end) return broken;
            op = 0x100 | r.u8(at + 1);
            ++at;
        }
        ++at;
        const bool read =
            op == 17 || op == 18 || op == 19 || op == (0x100 | 6) || op == (0x100 | 36) || op == (0x100 | 37);
        if (read && (!onlyInts || intCount == 0)) return broken;
        // stb_truetype uses an operator's first occurrence.
        const auto first = [&](std::uint32_t& field) {
            if (!seen.contains(op)) field = ints[0];
        };
        if (op == 17) first(out.charStrings);
        if (op == 19) first(out.subrs);
        if (op == (0x100 | 6)) first(out.charStringType);
        if (op == (0x100 | 36)) first(out.fdArray);
        if (op == (0x100 | 37)) first(out.fdSelect);
        if (op == 18) {
            if (intCount < 2) return broken;
            if (!seen.contains(op)) out.privateDict = ints;
        }
        seen.insert(op);
        ints = {};
        intCount = 0;
        onlyInts = true;
    }
    return std::nullopt;
}

/// The Private DICT a font DICT points to, and the local subroutines INDEX in it (stbtt__get_subrs).
Problem checkPrivate(const Reader& r, Range cff, const DictValues& fontDict) {
    const auto [size, offset] = fontDict.privateDict;
    if (size == 0 || offset == 0) return std::nullopt;
    if (offset > cff.end - cff.begin || size > cff.end - cff.begin - offset) return "CFF Private DICT is broken";
    DictValues priv;
    const std::size_t begin = cff.begin + offset;
    if (auto p = readDict(r, {begin, begin + size}, "Private DICT", priv)) return p;
    if (priv.subrs == 0) return std::nullopt;
    if (priv.subrs > cff.end - begin) return "CFF local subroutines are broken";
    std::size_t end = 0;
    return readIndex(r, cff, begin + priv.subrs, "local subroutines", end);
}

/// The FDSelect of a CID font: every glyph's FD must exist (stb_truetype asserts on an index past the FDArray).
Problem checkFdSelect(const Reader& r, Range cff, std::size_t at, std::size_t glyphs, std::uint32_t fdCount) {
    const std::string broken = "CFF FDSelect is broken";
    if (at >= cff.end) return broken;
    const std::uint32_t format = r.u8(at);
    if (format == 0) {
        if (at + 1 + glyphs > cff.end) return broken;
        for (std::size_t i = 0; i < glyphs; ++i)
            if (r.u8(at + 1 + i) >= fdCount) return broken;
        return std::nullopt;
    }
    if (format != 3 || at + 5 > cff.end) return broken;
    const std::size_t ranges = r.u16(at + 1);
    if (at + 5 + 3 * ranges + 2 > cff.end) return broken;
    for (std::size_t i = 0; i < ranges; ++i)
        if (r.u8(at + 5 + 3 * i) >= fdCount) return broken;
    return std::nullopt;
}

/// The CFF table, as stbtt_InitFont and the glyph lookups read it. Offsets in it count from its start.
Problem checkCff(const Reader& r, Table table, std::size_t glyphs) {
    const Range cff{table.offset, table.offset + table.length};
    if (table.length < 4) return "CFF table is broken";
    const std::size_t headerSize = r.u8(cff.begin + 2);
    std::size_t at = cff.begin + headerSize;
    std::array<Range, 1> topDicts{};
    if (auto p = readIndex(r, cff, at, "Name INDEX", at)) return p;
    if (auto p = readIndex(r, cff, at, "Top DICT INDEX", at, topDicts)) return p;
    if (topDicts[0].end == 0) return "CFF table has no Top DICT";
    if (auto p = readIndex(r, cff, at, "String INDEX", at)) return p;
    if (auto p = readIndex(r, cff, at, "global subroutines", at)) return p;

    DictValues top;
    if (auto p = readDict(r, topDicts[0], "Top DICT", top)) return p;
    if (top.charStringType != 2)
        return "CFF charstring type " + std::to_string(top.charStringType) + " isn't supported";
    if (top.charStrings == 0) return "CFF table has no CharStrings";
    const std::size_t length = table.length;
    if (top.charStrings > length) return "CFF CharStrings are broken";
    std::size_t end = 0;
    const std::size_t charStrings = cff.begin + top.charStrings;
    if (auto p = readIndex(r, cff, charStrings, "CharStrings", end)) return p;
    // A glyph's charstring is looked up by index without a check against the count.
    if (r.u16(charStrings) < glyphs) return "CFF CharStrings are truncated";
    if (auto p = checkPrivate(r, cff, top)) return p;
    if (top.fdArray != 0) { // a CID font: one font DICT per FD, chosen by FDSelect
        if (top.fdSelect == 0 || top.fdArray > length) return "CFF FDArray is broken";
        const std::size_t fdArray = cff.begin + top.fdArray;
        if (auto p = readIndex(r, cff, fdArray, "FDArray", end)) return p;
        // Every font DICT and its Private DICT (read when a glyph of that FD is drawn).
        const std::uint32_t count = r.u16(fdArray);
        const std::uint32_t offSize = count ? r.u8(fdArray + 2) : 1;
        const std::size_t data = fdArray + 3 + (std::size_t{count} + 1) * offSize - 1;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t first = fdArray + 3 + std::size_t{i} * offSize;
            DictValues fd;
            const Range dict{data + r.un(first, offSize), data + r.un(first + offSize, offSize)};
            if (auto p = readDict(r, dict, "font DICT", fd)) return p;
            if (auto p = checkPrivate(r, cff, fd)) return p;
        }
        if (top.fdSelect > length) return "CFF FDSelect is broken";
        if (auto p = checkFdSelect(r, cff, cff.begin + top.fdSelect, glyphs, count)) return p;
    }
    return std::nullopt;
}

// ------------------------------------------------------------------ TrueType tables

/// Format 4 (segment mapping to delta values), as stbtt_FindGlyphIndex reads it: the binary search over the segments
/// trusts the header's searchRange / entrySelector / rangeShift, and a segment with an idRangeOffset reads the glyph
/// from glyphIdArray at an offset nothing checks. Every glyph a segment can map to must be below `glyphs`.
Problem checkCmap4(const Reader& r, std::size_t at, std::size_t length, std::size_t glyphs) {
    const std::string broken = "the character map is broken";
    const std::size_t segments = r.u16(at + 6) >> 1;
    if (segments == 0) return broken; // the search reads segment 0 anyway
    // stb_truetype starts at segment rangeShift (in segments) when the code point lies past it, then steps forward
    // by searchRange / 2, / 4, ... entrySelector times: the furthest segment it can reach must exist. (Exactly
    // segments - 1 for the values the spec prescribes.)
    std::size_t furthest = r.u16(at + 12) >> 1;
    const std::size_t searchRange = r.u16(at + 8) >> 1;
    const std::size_t steps = std::min<std::size_t>(r.u16(at + 10), 16); // further steps add 0
    for (std::size_t k = 1; k <= steps; ++k)
        furthest += searchRange >> k;
    if (furthest >= segments) return broken;

    // The glyphIdArray entries with a glyph index past the font's, counted per byte offset and parity, so each
    // segment's run of entries is checked at once: bad(q0, q1) = entries at q0, q0 + 2, ..., q1.
    std::vector<std::uint32_t> badBefore(length + 2, 0); // badBefore[q + 2] = badBefore[q] + (entry at q is bad)
    for (std::size_t q = 0; q + 2 <= length; ++q)
        badBefore[q + 2] = badBefore[q] + (r.u16(at + q) >= glyphs ? 1u : 0u);
    const std::size_t ends = 14, starts = 16 + 2 * segments, deltas = 16 + 4 * segments,
                      rangeOffsets = 16 + 6 * segments;
    for (std::size_t i = 0; i < segments; ++i) {
        const std::size_t first = r.u16(at + starts + 2 * i), last = r.u16(at + ends + 2 * i);
        if (first > last) continue; // no code point matches it
        const std::size_t span = last - first;
        const std::size_t rangeOffset = r.u16(at + rangeOffsets + 2 * i);
        if (rangeOffset == 0) {
            // glyph = (code point + idDelta) mod 65536: a contiguous run that may wrap past 0xFFFF.
            const std::size_t low = (first + r.u16(at + deltas + 2 * i)) & 0xFFFF;
            const std::size_t high = low + span > 0xFFFF ? 0xFFFF : low + span;
            if (high >= glyphs) return "the character map maps to glyphs the font doesn't have";
            continue;
        }
        // The entries for first..last start rangeOffset bytes past this segment's idRangeOffset.
        const std::size_t q0 = rangeOffsets + 2 * i + rangeOffset, q1 = q0 + 2 * span;
        if (q1 + 2 > length) return broken;
        if (badBefore[q1 + 2] != badBefore[q0]) return "the character map maps to glyphs the font doesn't have";
    }
    return std::nullopt;
}

/// The character map stb_truetype picks (the last Unicode one) and its subtable. Every glyph index it can return
/// must be below `glyphs`: the metrics (hmtx) and CFF lookups use it without a check.
Problem checkCmap(const Reader& r, Table cmap, std::size_t glyphs) {
    if (cmap.length < 4) return "table 'cmap' is broken";
    const std::uint32_t count = r.u16(cmap.offset + 2);
    if (4 + 8 * std::size_t{count} > cmap.length) return "table 'cmap' is broken";
    std::optional<std::size_t> subtable;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t record = cmap.offset + 4 + 8 * std::size_t{i};
        const std::uint32_t platform = r.u16(record), encoding = r.u16(record + 2);
        if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) subtable = r.u32(record + 4);
    }
    if (!subtable) return "the font has no Unicode character map";
    const std::size_t at = cmap.offset + *subtable;
    if (*subtable > cmap.length || cmap.length - *subtable < 8) return "the character map is broken";
    const std::size_t room = cmap.length - *subtable;
    const std::uint32_t format = r.u16(at);
    std::size_t length = 0;
    std::size_t needed = 0;
    switch (format) {
    case 0: length = r.u16(at + 2), needed = 6 + 256; break;
    case 4: length = r.u16(at + 2), needed = 16 + 4 * std::size_t{r.u16(at + 6)}; break;
    case 6: length = r.u16(at + 2), needed = 10 + 2 * std::size_t{r.u16(at + 8)}; break;
    case 12:
    case 13:
        if (room < 16) return "the character map is broken";
        length = r.u32(at + 4), needed = 16 + 12 * std::size_t{r.u32(at + 12)};
        break;
    default: return "character map format " + std::to_string(format) + " isn't supported";
    }
    if (needed > length || length > room) return "the character map is broken";

    const std::string missingGlyphs = "the character map maps to glyphs the font doesn't have";
    switch (format) {
    case 0: // one byte per code point below length - 6
        for (std::size_t i = 6; i < length; ++i)
            if (r.u8(at + i) >= glyphs) return missingGlyphs;
        break;
    case 4: return checkCmap4(r, at, length, glyphs);
    case 6:
        for (std::size_t i = 0; i < r.u16(at + 8); ++i)
            if (r.u16(at + 10 + 2 * i) >= glyphs) return missingGlyphs;
        break;
    default: // 12 and 13: groups of (first code point, last code point, glyph)
        for (std::size_t i = 0; i < r.u32(at + 12); ++i) {
            const std::size_t group = at + 16 + 12 * i;
            const std::uint64_t first = r.u32(group), last = r.u32(group + 4), glyph = r.u32(group + 8);
            if (first > last) continue; // no code point matches it
            // Format 12 maps the run to consecutive glyphs, 13 all of it to one.
            const std::uint64_t highest = format == 12 ? glyph + (last - first) : glyph;
            if (highest >= glyphs) return missingGlyphs;
        }
        break;
    }
    return std::nullopt;
}

/// Every glyph's CFF charstring, run by stb_truetype as stbtt_GetGlyphBox runs it. Only for a font whose structure
/// checkSfnt() found sound: stb_truetype reads the CFF table through a buffer it thinks is 512 MiB long, so only
/// checkCff()'s checks keep it inside the data.
Problem checkCharstrings(const Reader& r, std::size_t font) {
    if (r.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return "the font is too large";
    const int saved = errno;
    errno = 0;
    stbtt_fontinfo info{};
    const bool read = stbtt_InitFont(&info, r.data(), static_cast<int>(font)) != 0 && errno == 0;
    int glyph = 0;
    for (; read && glyph < info.numGlyphs && errno == 0; ++glyph) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBox(&info, glyph, &x0, &y0, &x1, &y1);
    }
    const bool asserted = errno != 0;
    errno = saved;
    if (!read) return "the CFF table is broken";
    if (asserted) return "the CFF charstring of glyph " + std::to_string(glyph - 1) + " is broken";
    return std::nullopt;
}

/// One font of the data: the sfnt at `font`.
Problem checkSfnt(const Reader& r, std::size_t font) {
    if (!r.has(font, 12)) return "the font header is truncated";
    const std::uint32_t count = r.u16(font + 4);
    if (count == 0) return "the font has no tables";
    if (!r.has(font + 12, 16 * std::size_t{count})) return "the table directory is truncated";
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t record = font + 12 + 16 * std::size_t{i};
        if (!r.has(r.u32(record + 8), r.u32(record + 12))) {
            std::string tag;
            for (std::size_t j = 0; j < 4; ++j)
                tag += static_cast<char>(r.u8(record + j));
            return "table '" + tag + "' lies outside the file";
        }
    }

    // stbtt_InitFont's required tables, and the sizes of the fields it and the metrics lookups read.
    const auto cmap = findTable(r, font, "cmap");
    const auto head = findTable(r, font, "head");
    const auto hhea = findTable(r, font, "hhea");
    const auto hmtx = findTable(r, font, "hmtx");
    const auto maxp = findTable(r, font, "maxp");
    for (const auto& [table, tag] : {std::pair{&cmap, "cmap"}, std::pair{&head, "head"}, std::pair{&hhea, "hhea"},
                                     std::pair{&hmtx, "hmtx"}, std::pair{&maxp, "maxp"}})
        if (!*table) return "required table '" + std::string{tag} + "' is missing";
    if (head->length < 54) return "table 'head' is broken";
    if (hhea->length < 36) return "table 'hhea' is broken";
    if (maxp->length < 6) return "table 'maxp' is broken";
    // stb_truetype's table lookups return 0 for "missing", so a table at offset 0 would count as missing there.
    for (const auto* t : {&*cmap, &*head, &*hhea, &*hmtx, &*maxp})
        if (t->offset == 0) return "the table directory is broken";
    const std::size_t glyphs = r.u16(maxp->offset + 4);
    // Glyph 0 (.notdef) is looked up for every character the font lacks.
    if (glyphs == 0) return "the font has no glyphs";
    const std::size_t longMetrics = r.u16(hhea->offset + 34);
    if (longMetrics == 0 && glyphs > 0) return "table 'hhea' is broken";
    const std::size_t shortMetrics = glyphs > longMetrics ? glyphs - longMetrics : 0;
    if (4 * longMetrics + 2 * shortMetrics > hmtx->length) return "table 'hmtx' is truncated";
    if (auto p = checkCmap(r, *cmap, glyphs)) return p;

    if (const auto glyf = findTable(r, font, "glyf"); glyf && glyf->offset != 0) {
        const auto loca = findTable(r, font, "loca");
        if (!loca || loca->offset == 0) return "required table 'loca' is missing";
        const std::uint32_t format = r.u16(head->offset + 50);
        if (format > 1) return "table 'head' is broken";
        const std::size_t entry = format == 0 ? 2 : 4;
        if ((glyphs + 1) * entry > loca->length) return "table 'loca' is truncated";
        std::size_t previous = 0;
        for (std::size_t i = 0; i <= glyphs; ++i) {
            const std::size_t at = loca->offset + i * entry;
            const std::size_t offset = format == 0 ? 2 * std::size_t{r.u16(at)} : r.u32(at);
            if (offset < previous || offset > glyf->length) return "table 'loca' is broken";
            if (offset != previous && offset - previous < 10) return "table 'glyf' is broken"; // a glyph header
            previous = offset;
        }
        return std::nullopt;
    }
    const auto cff = findTable(r, font, "CFF ");
    if (!cff || cff->offset == 0) return "the font has neither TrueType ('glyf') nor CFF outlines";
    if (auto p = checkCff(r, *cff, glyphs)) return p;
    return checkCharstrings(r, font);
}

} // namespace

std::optional<std::string> checkFont(std::span<const std::byte> bytes) {
    const Reader r{bytes};
    if (r.size() < 12) return std::string{notAFont};
    if (r.tagIs(0, std::string_view{"\0\1\0\0", 4}) || r.tagIs(0, "OTTO") || r.tagIs(0, "true")) return checkSfnt(r, 0);
    if (!r.tagIs(0, "ttcf")) return std::string{notAFont};
    // A collection: ImGui loads its first font.
    const std::uint32_t version = r.u32(4);
    if (version != 0x00010000 && version != 0x00020000) return "the font collection header is broken";
    if (r.u32(8) == 0 || r.u32(8) > 0x7FFFFFFF || !r.has(12, 4)) return "the font collection is empty";
    return checkSfnt(r, r.u32(12));
}

} // namespace imlui
