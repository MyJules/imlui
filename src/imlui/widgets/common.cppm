// Shared helpers for the Material 3 widgets: theme access, disabled colors,
// label parsing, animation, text / icon drawing, elevation shadows and
// high-quality shapes (true pill radii, smooth arcs, pixel-snapped strokes).
//
// Disabled state: widgets detect ImGui::BeginDisabled() through the item
// flags and switch to Material's disabled colors (onSurface @ 12% container,
// @ 38% content) instead of ImGui's global alpha.
module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

export module imlui.widgets.common;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.theme.theme;
import imlui.ui.frame_request;
import imlui.ui.item_size;

export namespace imlui::md {

inline constexpr float pi = 3.14159265358979f;
/// "Unbounded" extent for text measuring and size constraints.
inline constexpr float hugeExtent = 3.4e38f;

/// Color scheme of the current theme.
[[nodiscard]] const Scheme& scheme();
/// Shape scale of the current theme.
[[nodiscard]] const Theme::Shape& shape();

/// Converts without ImGui's style alpha: disabled colors are explicit.
[[nodiscard]] ImU32 col(Color c);

/// True when the last submitted item is disabled.
[[nodiscard]] bool itemDisabled();
[[nodiscard]] Color disabledContent();
[[nodiscard]] Color disabledContainer();

/// End of the visible part of an ImGui label ("Text##id" -> "Text").
[[nodiscard]] const char* labelEnd(const char* label);
/// Visible part of an ImGui label ("Text##id" -> "Text").
[[nodiscard]] std::string_view visibleLabel(const char* label);

/// Smoothly moves a per-id stored value towards `target`. Requests another frame while it moves.
float animate(ImGuiID id, float target, float speed = 14.0f);
/// Hover/press state-layer opacity (Material: 8% hover, 10% pressed), animated.
float stateLayer(ImGuiID id, bool hovered, bool active);

/// Resolves a requested width against the available width (resolveSize): > 0 fixed, -1 fill, < -1 fill minus
/// |requested|, 0 natural.
[[nodiscard]] float itemWidth(float requested, float natural);

[[nodiscard]] ImVec2 measure(TextStyle style, std::string_view s);
void drawText(ImDrawList* dl, TextStyle style, ImVec2 pos, Color c, std::string_view s);
/// Draws `s` vertically centered on `cy`, starting at x. Returns its width.
float drawTextV(ImDrawList* dl, TextStyle style, float x, float cy, Color c, std::string_view s);
/// Draws a Material icon centered at `c`, `size` in dp.
void drawIcon(ImDrawList* dl, std::string_view name, ImVec2 c, float size, Color color);
/// Top y at which to draw `style` text so that its cap height is centered on `cy`, on a whole pixel.
[[nodiscard]] float textTop(TextStyle style, float cy);
/// Soft elevation shadow behind the rect [a, b]. `level` 0 = none.
void drawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float level);

// Shapes. Use these instead of ImDrawList::AddRect*/AddCircle* for Material shapes. Unlike ImGui's, they
// - use the true radius, clamped only to half the side it spans (so radius >= h/2 is an exact pill),
// - tessellate arcs by their length in pixels (about one segment per 2px), so curves stay smooth at any scale,
// - snap rects to whole pixels; strokes are whole pixels wide and lie inside the shape, so they stay crisp.
// Sizes are pixels (dp-scaled by the caller). `corners` takes ImDrawFlags_RoundCorners* flags.

/// Fills the rect [a, b] with rounded corners.
void fillRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, Color c,
                   ImDrawFlags corners = ImDrawFlags_RoundCornersAll);
/// Strokes the outline of the rect [a, b], inside it. `radius` is the outer radius.
void strokeRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, Color c, float thickness,
                     ImDrawFlags corners = ImDrawFlags_RoundCornersAll);
void fillCircle(ImDrawList* dl, ImVec2 center, float radius, Color c);
/// Strokes a circle inside `radius` (the outer edge of the ring).
void strokeCircle(ImDrawList* dl, ImVec2 center, float radius, Color c, float thickness);
/// Strokes an open arc centered on `radius`, from angle `a0` to `a1` (radians, clockwise from +x).
void strokeArc(ImDrawList* dl, ImVec2 center, float radius, float a0, float a1, Color c, float thickness);
/// Horizontal line covering the pixel rows [y, y + thickness) from x0 to x1.
void hLine(ImDrawList* dl, float x0, float x1, float y, Color c, float thickness);
/// Vertical line covering the pixel columns [x, x + thickness) from y0 to y1.
void vLine(ImDrawList* dl, float x, float y0, float y1, Color c, float thickness);

} // namespace imlui::md

module :private;

namespace imlui::md {

const Scheme& scheme() { return theme().scheme; }
const Theme::Shape& shape() { return theme().shape; }

ImU32 col(Color c) { return ImGui::ColorConvertFloat4ToU32(c.vec4()); }

bool itemDisabled() { return (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0; }
Color disabledContent() { return scheme().onSurface.withAlpha(0.38f); }
Color disabledContainer() { return scheme().onSurface.withAlpha(0.12f); }

const char* labelEnd(const char* label) {
    std::string_view sv{label};
    auto pos = sv.find("##");
    return label + (pos == std::string_view::npos ? sv.size() : pos);
}

std::string_view visibleLabel(const char* label) {
    return {label, static_cast<std::size_t>(labelEnd(label) - label)};
}

float animate(ImGuiID id, float target, float speed) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    // The first frame after an idle wait has a long DeltaTime. Capping it keeps a
    // transition that starts then (hover, press) from jumping to its end.
    float dt = std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
    *v += (target - *v) * std::min(1.0f, dt * speed);
    if (std::fabs(*v - target) < 0.0015f)
        *v = target;
    else
        requestFrame();
    return *v;
}

float stateLayer(ImGuiID id, bool hovered, bool active) {
    return animate(id + 0x5157, active ? 0.10f : (hovered ? 0.08f : 0.0f), 22.0f);
}

float itemWidth(float requested, float natural) {
    return resolveSize(requested, ImGui::GetContentRegionAvail().x, natural);
}

ImVec2 measure(TextStyle style, std::string_view s) {
    return font(style)->CalcTextSizeA(fontPixels(style), hugeExtent, 0.0f, s.data(), s.data() + s.size());
}

void drawText(ImDrawList* dl, TextStyle style, ImVec2 pos, Color c, std::string_view s) {
    dl->AddText(font(style), fontPixels(style), pos, col(c), s.data(), s.data() + s.size());
}

float drawTextV(ImDrawList* dl, TextStyle style, float x, float cy, Color c, std::string_view s) {
    drawText(dl, style, {x, textTop(style, cy)}, c, s);
    return measure(style, s).x;
}

float textTop(TextStyle style, float cy) {
    const float size = fontPixels(style);
    // Centering the line box (ascent + descent) puts Roboto's capitals a little low, and ImGui truncates the text
    // position, which can add a pixel. Center the cap height ('H' spans cap top to baseline) and round instead.
    float center = size * 0.5f;
    if (ImFont* f = font(style)) {
        ImFontBaked* baked = f->GetFontBaked(size);
        const ImFontGlyph* g = baked ? baked->FindGlyphNoFallback('H') : nullptr;
        if (g && baked->Size > 0.0f) center = (g->Y0 + g->Y1) * 0.5f * (size / baked->Size);
    }
    return std::floor(cy - center + 0.5f);
}

void drawIcon(ImDrawList* dl, std::string_view name, ImVec2 c, float size, Color color) {
    ImFont* f = iconFont();
    if (!f || name.empty()) return;
    std::string t = iconText(name);
    if (t.empty()) return;
    float px = dp(size);
    ImVec2 sz = f->CalcTextSizeA(px, hugeExtent, 0.0f, t.c_str());
    dl->AddText(f, px, {std::floor(c.x - sz.x * 0.5f), std::floor(c.y - sz.y * 0.5f)}, col(color), t.c_str());
}

void drawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float level) {
    if (level <= 0) return;
    const bool dark = theme().dark;
    const float base = dark ? 0.22f : 0.10f;
    for (int i = 1; i <= 6; ++i) {
        float g = dp(static_cast<float>(i)) * (0.6f + 0.4f * level);
        float a0 = base * level / static_cast<float>(i + 1);
        fillRoundRect(dl, {a.x - g * 0.6f, a.y - g * 0.2f + dp(1) * level}, {b.x + g * 0.6f, b.y + g + dp(1) * level},
                      rounding + g, scheme().shadow.withAlpha(a0));
    }
}

namespace {

float snap(float v) { return std::floor(v + 0.5f); }
ImVec2 snap(ImVec2 v) { return {snap(v.x), snap(v.y)}; }

/// Stroke width in whole pixels, at least 1.
float strokePixels(float thickness) { return std::max(1.0f, snap(thickness)); }

/// Segments for an arc of `angle` radians at `radius` pixels: about one per 2px of arc length, at least 4 per
/// quarter turn.
int arcSegments(float radius, float angle) {
    const float byLength = radius * angle * 0.5f;
    const float minimum = 4.0f * angle / (pi * 0.5f);
    return std::max(1, static_cast<int>(std::ceil(std::max(byLength, minimum))));
}

bool samePoint(ImVec2 a, ImVec2 b) { return std::fabs(a.x - b.x) < 0.01f && std::fabs(a.y - b.y) < 0.01f; }

/// Appends a point to the path unless it repeats the last one. Repeated points (where a pill's end arcs meet)
/// have no direction, which breaks the anti-aliased fringe.
void pathPoint(ImDrawList* dl, ImVec2 p) {
    if (dl->_Path.Size > 0 && samePoint(dl->_Path.back(), p)) return;
    dl->_Path.push_back(p);
}

/// Quarter arc around `c` from angle `a0` (radians) clockwise; a single point when `r` is 0.
void pathQuarter(ImDrawList* dl, ImVec2 c, float r, float a0) {
    if (r <= 0.0f) {
        pathPoint(dl, c);
        return;
    }
    const int n = arcSegments(r, pi * 0.5f);
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + pi * 0.5f * static_cast<float>(i) / static_cast<float>(n);
        pathPoint(dl, {c.x + std::cos(a) * r, c.y + std::sin(a) * r});
    }
}

ImDrawFlags cornerFlags(ImDrawFlags corners) {
    return (corners & ImDrawFlags_RoundCornersMask_) == 0 ? ImDrawFlags_RoundCornersAll : corners;
}

/// Largest radius [a, b] allows for `corners`: half a side when both of its corners are rounded, the whole side
/// otherwise.
float maxRadius(ImVec2 a, ImVec2 b, ImDrawFlags corners) {
    const auto both = [&](int side) { return (corners & side) == side; };
    const float w = b.x - a.x, h = b.y - a.y;
    const float rw = w * (both(ImDrawFlags_RoundCornersTop) || both(ImDrawFlags_RoundCornersBottom) ? 0.5f : 1.0f);
    const float rh = h * (both(ImDrawFlags_RoundCornersLeft) || both(ImDrawFlags_RoundCornersRight) ? 0.5f : 1.0f);
    return std::max(0.0f, std::min(rw, rh));
}

void pathRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImDrawFlags corners) {
    if ((corners & ImDrawFlags_RoundCornersMask_) == ImDrawFlags_RoundCornersNone) r = 0.0f;
    r = std::clamp(r, 0.0f, maxRadius(a, b, corners));
    const auto at = [&](int corner) { return (corners & corner) != 0 ? r : 0.0f; };
    const float tl = at(ImDrawFlags_RoundCornersTopLeft), tr = at(ImDrawFlags_RoundCornersTopRight);
    const float br = at(ImDrawFlags_RoundCornersBottomRight), bl = at(ImDrawFlags_RoundCornersBottomLeft);
    pathQuarter(dl, {a.x + tl, a.y + tl}, tl, pi);
    pathQuarter(dl, {b.x - tr, a.y + tr}, tr, pi * 1.5f);
    pathQuarter(dl, {b.x - br, b.y - br}, br, 0.0f);
    pathQuarter(dl, {a.x + bl, b.y - bl}, bl, pi * 0.5f);
    if (dl->_Path.Size > 1 && samePoint(dl->_Path.back(), dl->_Path[0])) dl->_Path.pop_back();
}

void pathCircle(ImDrawList* dl, ImVec2 c, float r) {
    const int n = std::max(16, arcSegments(r, 2.0f * pi));
    for (int i = 0; i < n; ++i) {
        const float a = 2.0f * pi * static_cast<float>(i) / static_cast<float>(n);
        dl->_Path.push_back({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
    }
}

} // namespace

void fillRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, Color c, ImDrawFlags corners) {
    if (c.a <= 0.0f) return;
    corners = cornerFlags(corners);
    // A radius meant as a pill stays one when snapping changes the size by a pixel.
    const bool pill = radius >= maxRadius(a, b, corners) - 0.01f;
    const ImVec2 sa = snap(a), sb = snap(b);
    if (sb.x <= sa.x || sb.y <= sa.y) return;
    pathRoundRect(dl, sa, sb, pill ? hugeExtent : radius, corners);
    dl->PathFillConvex(col(c));
}

void strokeRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, Color c, float thickness, ImDrawFlags corners) {
    if (c.a <= 0.0f) return;
    corners = cornerFlags(corners);
    const bool pill = radius >= maxRadius(a, b, corners) - 0.01f;
    const float t = strokePixels(thickness), inset = t * 0.5f;
    // The center line runs `inset` inside the snapped rect: on pixel centers for odd widths, on pixel edges for
    // even ones. Either way the stroke covers whole pixels inside [a, b].
    const ImVec2 sa = snap(a), sb = snap(b);
    const ImVec2 pa{sa.x + inset, sa.y + inset}, pb{sb.x - inset, sb.y - inset};
    if (pb.x <= pa.x || pb.y <= pa.y) {
        fillRoundRect(dl, sa, sb, radius, c, corners);
        return;
    }
    pathRoundRect(dl, pa, pb, pill ? hugeExtent : radius - inset, corners);
    dl->PathStroke(col(c), t, ImDrawFlags_Closed);
}

void fillCircle(ImDrawList* dl, ImVec2 center, float radius, Color c) {
    if (c.a <= 0.0f || radius <= 0.0f) return;
    pathCircle(dl, center, radius);
    dl->PathFillConvex(col(c));
}

void strokeCircle(ImDrawList* dl, ImVec2 center, float radius, Color c, float thickness) {
    if (c.a <= 0.0f || radius <= 0.0f) return;
    const float t = strokePixels(thickness);
    if (radius <= t) {
        fillCircle(dl, center, radius, c);
        return;
    }
    pathCircle(dl, center, radius - t * 0.5f);
    dl->PathStroke(col(c), t, ImDrawFlags_Closed);
}

void strokeArc(ImDrawList* dl, ImVec2 center, float radius, float a0, float a1, Color c, float thickness) {
    if (c.a <= 0.0f || radius <= 0.0f || a1 <= a0) return;
    dl->PathArcTo(center, radius, a0, a1, arcSegments(radius, a1 - a0));
    dl->PathStroke(col(c), thickness);
}

void hLine(ImDrawList* dl, float x0, float x1, float y, Color c, float thickness) {
    if (c.a <= 0.0f) return;
    const float sy = snap(y), sx0 = snap(x0), sx1 = snap(x1);
    if (sx1 > sx0) dl->AddRectFilled({sx0, sy}, {sx1, sy + strokePixels(thickness)}, col(c));
}

void vLine(ImDrawList* dl, float x, float y0, float y1, Color c, float thickness) {
    if (c.a <= 0.0f) return;
    const float sx = snap(x), sy0 = snap(y0), sy1 = snap(y1);
    if (sy1 > sy0) dl->AddRectFilled({sx, sy0}, {sx + strokePixels(thickness), sy1}, col(c));
}

} // namespace imlui::md
