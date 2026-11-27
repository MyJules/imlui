// Shared helpers for the Material 3 widgets: theme access, disabled colors,
// label parsing, animation, text / icon drawing, elevation shadows and
// high-quality shapes (true pill radii, smooth arcs, pixel-snapped strokes).
//
// Disabled state: widgets detect ImGui::BeginDisabled() through the item
// flags and switch to Material's disabled colors (onSurface @ 12% container,
// @ 38% content) instead of ImGui's global alpha.
export module imlui.widgets.common;

import std;

import imgui;
import imlui.color;
import imlui.theme.dpi;
import imlui.theme.scheme;
import imlui.theme.typography;
import imlui.theme.icons;
import imlui.theme.theme;
import imlui.ui.frame_request;
import imlui.ui.item_size;
import imlui.ui.layout_box;

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

/// Name for a top-level window or a modal popup owned by the current ID scope: "label###id<hash>". ImGui names
/// top-level windows by their name alone, ignoring the ID stack, so the hash of `id` in the current scope (an
/// element renders inside its node's scope) keeps two instances apart. It doesn't depend on `label`, so the window
/// keeps its position, size and popup state across frames and title changes.
[[nodiscard]] std::string scopedWindowName(std::string_view label, std::string_view id);
/// scopedWindowName("", id) in a fixed buffer, for a window named every frame without a title: "###id<hash>" with no
/// allocation (a std::string of it is past the small-string buffer). An `id` longer than 40 characters is cut in the
/// name (the hash still covers all of it).
[[nodiscard]] std::array<char, 64> scopedWindowId(std::string_view id);

/// animate()'s settling distances. A fraction (an opacity, a 0..1 progress) needs a fine one to end without a visible
/// step; a length doesn't: with the fraction's, a tab indicator sliding 300px kept requesting frames about 20 frames
/// after it had come within a quarter pixel, where nothing visible moves any more.
inline constexpr float fractionEpsilon = 0.0015f;
inline constexpr float pixelEpsilon = 0.25f;
/// Smoothly moves a per-id stored value towards `target`. Requests another frame while it moves. A non-finite
/// target is ignored (the stored value stays); a non-finite stored value jumps to the target. The first call for an
/// id starts at `target`. Values are kept per ImGui context, in a hash map of imlui's (not the window's ImGuiStorage,
/// a sorted vector: a page showing thousands of widgets for the first time inserted into it in quadratic time); one
/// not asked for in animateKeepFrames frames is dropped (its widget starts at its target again, as if at rest).
/// The value settles on the target (and stops requesting frames) once within `epsilon` of it: the default suits a
/// 0..1 fraction; a value in pixels or dp passes pixelEpsilon.
float animate(ImGuiID id, float target, float speed = 14.0f, float epsilon = fractionEpsilon);
/// Frames an animate() value is kept without being asked for.
inline constexpr int animateKeepFrames = 600;
/// Hover/press state-layer opacity (Material: 8% hover, 10% pressed), animated.
float stateLayer(ImGuiID id, bool hovered, bool active);

/// Whether a popup is open over the current window now, or was at the end of the last frame (notePopupOver()).
/// ImGui's keyboard nav closes the top popup on Escape in NewFrame, before the window under it sees the key: a
/// window that closes on Escape (a Dialog, a drawer) checks this, so one press closes one popup. `flags` are
/// ImGui::IsPopupOpen("", flags)'s.
[[nodiscard]] bool popupOver(ImGuiPopupFlags flags);
/// Records whether a popup is open over the current window, for popupOver() on the next frame. Call it after the
/// window's contents, so a menu they opened this frame counts.
void notePopupOver(ImGuiPopupFlags flags);

/// Resolves a requested width against the available width of the layout box (resolveSize): > 0 fixed, -1 fill, < -1 fill minus
/// |requested|, 0 natural.
[[nodiscard]] float itemWidth(float requested, float natural);

/// Whether the rect [a, b] (with a pixel of slack for the anti-aliased fringe) overlaps the current clip rect of
/// `dl`, the rect the GPU clips its draw commands to. Geometry outside it is built and then thrown away, so the
/// drawing helpers below (shapes, text, icons, shadows) check it and draw nothing outside: a widget scrolled out of
/// view builds no vertices. An empty clip rect (an item rendered unseen) shows nothing.
[[nodiscard]] bool rectVisible(ImDrawList* dl, ImVec2 a, ImVec2 b);

[[nodiscard]] ImVec2 measure(TextStyle style, std::string_view s);
void drawText(ImDrawList* dl, TextStyle style, ImVec2 pos, Color c, std::string_view s);
/// Draws `s` vertically centered on `cy`, starting at x.
void drawTextV(ImDrawList* dl, TextStyle style, float x, float cy, Color c, std::string_view s);
/// The largest icon drawIcon() draws, in pixels: a larger size is drawn at this one (ImGui rasterizes each size
/// into its font atlas, and a glyph larger than the atlas aborts).
inline constexpr float maxIconPixels = 1024.0f;
/// Draws a Material icon centered at `c`, `size` in dp. Nothing is drawn below a pixel (a size of 0, negative or
/// NaN, which ImGui asserts on); above maxIconPixels the icon is drawn at that size.
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
/// Fills the quad from the segment a-b down to the horizontal line y = `bottom` (a chart's area under its line),
/// with a vertical gradient: `top` at a and b, `bottomColor` at the bottom line.
void fillAreaBelow(ImDrawList* dl, ImVec2 a, ImVec2 b, float bottom, ImU32 top, ImU32 bottomColor);

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

std::string scopedWindowName(std::string_view label, std::string_view id) {
    return std::format("{}###{}{:08X}", label, id, ImGui::GetID(id.data(), id.data() + id.size()));
}

std::array<char, 64> scopedWindowId(std::string_view id) {
    std::array<char, 64> name{};
    const int shown = static_cast<int>(std::min<std::size_t>(id.size(), 40));
    const ImGuiID hash = ImGui::GetID(id.data(), id.data() + id.size());
    std::snprintf(name.data(), name.size(), "###%.*s%08X", shown, id.data(), hash);
    return name;
}

bool popupOver(ImGuiPopupFlags flags) {
    return ImGui::IsPopupOpen("", flags) || ImGui::GetStateStorage()->GetBool(ImGui::GetID("##popupOver"));
}

void notePopupOver(ImGuiPopupFlags flags) {
    ImGui::GetStateStorage()->SetBool(ImGui::GetID("##popupOver"), ImGui::IsPopupOpen("", flags));
}

namespace {

/// animate()'s values: by id, with the frame each was last asked for, for the current ImGui context.
struct Animated {
    float value;
    int used;
};
struct AnimatedValues {
    ImGuiContext* context = nullptr;
    int swept = 0;
    std::unordered_map<ImGuiID, Animated> byId;
};
AnimatedValues animated; // main thread only, like all ImGui code

/// The value of `id`, created at `initial` on its first use; drops the ones unused for animateKeepFrames frames,
/// looking for them every 64 frames.
float& animatedValue(ImGuiID id, float initial) {
    if (animated.context != ImGui::GetCurrentContext()) {
        animated.byId.clear();
        animated.context = ImGui::GetCurrentContext();
        animated.swept = 0;
    }
    const int frame = ImGui::GetFrameCount();
    if (frame - animated.swept >= 64 || frame < animated.swept) {
        animated.swept = frame;
        std::erase_if(animated.byId, [&](const auto& e) { return frame - e.second.used >= animateKeepFrames; });
    }
    Animated& a = animated.byId.try_emplace(id, Animated{initial, frame}).first->second;
    a.used = frame;
    return a.value;
}

} // namespace

float animate(ImGuiID id, float target, float speed, float epsilon) {
    float* v = &animatedValue(id, target);
    // A NaN target (a Slider value of 0/0) would never converge and request frames forever.
    if (!std::isfinite(target)) return std::isfinite(*v) ? *v : 0.0f;
    if (!std::isfinite(*v)) *v = target;
    // The first frame after an idle wait has a long DeltaTime. Capping it keeps a
    // transition that starts then (hover, press) from jumping to its end.
    float dt = std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
    *v += (target - *v) * std::min(1.0f, dt * speed);
    if (std::fabs(*v - target) < epsilon)
        *v = target;
    else
        requestFrame();
    return *v;
}

float stateLayer(ImGuiID id, bool hovered, bool active) {
    return animate(id + 0x5157, active ? 0.10f : (hovered ? 0.08f : 0.0f), 22.0f);
}

float itemWidth(float requested, float natural) {
    return resolveSize(requested, layout::availableWidth(), natural);
}

bool rectVisible(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    // A draw list nobody pushed a clip rect on clips nothing.
    if (dl->_ClipRectStack.Size == 0) return true;
    const ImVec4& clip = dl->_ClipRectStack.back();
    if (!(clip.z > clip.x && clip.w > clip.y)) return false;
    return a.x - 1.0f < clip.z && b.x + 1.0f > clip.x && a.y - 1.0f < clip.w && b.y + 1.0f > clip.y;
}

ImVec2 measure(TextStyle style, std::string_view s) {
    return font(style)->CalcTextSizeA(fontPixels(style), hugeExtent, 0.0f, s.data(), s.data() + s.size());
}

void drawText(ImDrawList* dl, TextStyle style, ImVec2 pos, Color c, std::string_view s) {
    // ImGui skips the lines above and below the clip rect cheaply, but walks the glyphs of a line right of it.
    if (!rectVisible(dl, pos, {hugeExtent, hugeExtent})) return;
    dl->AddText(font(style), fontPixels(style), pos, col(c), s.data(), s.data() + s.size());
}

void drawTextV(ImDrawList* dl, TextStyle style, float x, float cy, Color c, std::string_view s) {
    // A line is at most a font size tall around its center: skip textTop()'s glyph lookup out of view.
    const float size = fontPixels(style);
    if (!rectVisible(dl, {x, cy - size}, {hugeExtent, cy + size})) return;
    drawText(dl, style, {x, textTop(style, cy)}, c, s);
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
    const float px = std::min(dp(size), maxIconPixels);
    if (!(px >= 1.0f)) return; // also NaN
    // The glyph lies within its size around `c`; out of view, skip the name lookup and the measuring.
    if (!rectVisible(dl, {c.x - px, c.y - px}, {c.x + px, c.y + px})) return;
    std::string t = iconText(name);
    if (t.empty()) return;
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

/// Appends `n` + 1 points of an arc around `c` at radius `r`: angles a0 + i * step for i in 0..n, from the cosine
/// and sine of `a0` and of `step` (one sincos per arc, not per point). Each point rotates the last by `step`, in double:
/// the rounding a float rotation gathers over a few hundred points stays far below a pixel's 1/1000.
template <class Add>
void arcPoints(ImVec2 c, float r, double cos0, double sin0, double step, int n, Add add) {
    const double cs = std::cos(step), sn = std::sin(step);
    double x = cos0, y = sin0;
    const auto cx = static_cast<double>(c.x), cy = static_cast<double>(c.y), rr = static_cast<double>(r);
    for (int i = 0; i <= n; ++i) {
        add(ImVec2{static_cast<float>(cx + x * rr), static_cast<float>(cy + y * rr)});
        const double nx = x * cs - y * sn;
        y = x * sn + y * cs;
        x = nx;
    }
}

/// Quarter arc around `c` starting at `quadrant` quarter turns (0: +x, 1: +y, 2: -x, 3: -y) clockwise; a single
/// point when `r` is 0. A quarter turn's start has an exact cosine and sine.
void pathQuarter(ImDrawList* dl, ImVec2 c, float r, int quadrant) {
    if (r <= 0.0f) {
        pathPoint(dl, c);
        return;
    }
    constexpr double cosines[] = {1.0, 0.0, -1.0, 0.0};
    const auto q = static_cast<std::size_t>(quadrant & 3);
    const int n = arcSegments(r, pi * 0.5f);
    arcPoints(c, r, cosines[q], cosines[(q + 3) & 3], std::numbers::pi * 0.5 / static_cast<double>(n), n,
              [&](ImVec2 p) { pathPoint(dl, p); });
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
    pathQuarter(dl, {a.x + tl, a.y + tl}, tl, 2);
    pathQuarter(dl, {b.x - tr, a.y + tr}, tr, 3);
    pathQuarter(dl, {b.x - br, b.y - br}, br, 0);
    pathQuarter(dl, {a.x + bl, b.y - bl}, bl, 1);
    if (dl->_Path.Size > 1 && samePoint(dl->_Path.back(), dl->_Path[0])) dl->_Path.pop_back();
}

void pathCircle(ImDrawList* dl, ImVec2 c, float r) {
    const int n = std::max(16, arcSegments(r, 2.0f * pi));
    // n points: the one after the last would repeat the first.
    arcPoints(c, r, 1.0, 0.0, 2.0 * std::numbers::pi / static_cast<double>(n), n - 1,
              [&](ImVec2 p) { dl->_Path.push_back(p); });
}

} // namespace

void fillRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, Color c, ImDrawFlags corners) {
    if (c.a <= 0.0f) return;
    corners = cornerFlags(corners);
    // A radius meant as a pill stays one when snapping changes the size by a pixel.
    const bool pill = radius >= maxRadius(a, b, corners) - 0.01f;
    const ImVec2 sa = snap(a), sb = snap(b);
    if (sb.x <= sa.x || sb.y <= sa.y || !rectVisible(dl, sa, sb)) return;
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
    if (!rectVisible(dl, sa, sb)) return;
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
    if (!rectVisible(dl, {center.x - radius, center.y - radius}, {center.x + radius, center.y + radius})) return;
    pathCircle(dl, center, radius);
    dl->PathFillConvex(col(c));
}

void strokeCircle(ImDrawList* dl, ImVec2 center, float radius, Color c, float thickness) {
    if (c.a <= 0.0f || radius <= 0.0f) return;
    if (!rectVisible(dl, {center.x - radius, center.y - radius}, {center.x + radius, center.y + radius})) return;
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
    // The stroke is centered on the arc.
    const float outer = radius + thickness * 0.5f;
    if (!rectVisible(dl, {center.x - outer, center.y - outer}, {center.x + outer, center.y + outer})) return;
    dl->PathArcTo(center, radius, a0, a1, arcSegments(radius, a1 - a0));
    dl->PathStroke(col(c), thickness);
}

void hLine(ImDrawList* dl, float x0, float x1, float y, Color c, float thickness) {
    if (c.a <= 0.0f) return;
    const float sy = snap(y), sx0 = snap(x0), sx1 = snap(x1);
    const ImVec2 a{sx0, sy}, b{sx1, sy + strokePixels(thickness)};
    if (sx1 > sx0 && rectVisible(dl, a, b)) dl->AddRectFilled(a, b, col(c));
}

void vLine(ImDrawList* dl, float x, float y0, float y1, Color c, float thickness) {
    if (c.a <= 0.0f) return;
    const float sx = snap(x), sy0 = snap(y0), sy1 = snap(y1);
    const ImVec2 a{sx, sy0}, b{sx + strokePixels(thickness), sy1};
    if (sy1 > sy0 && rectVisible(dl, a, b)) dl->AddRectFilled(a, b, col(c));
}

void fillAreaBelow(ImDrawList* dl, ImVec2 a, ImVec2 b, float bottom, ImU32 top, ImU32 bottomColor) {
    // ImDrawIdx is 16 or 32 bits (imconfig). The draw list's own index type, rather than a fixed unsigned short: with
    // 32-bit indices a draw list past 65535 vertices numbers them on (no VtxOffset), and a 16-bit cast wrapped the
    // quad's indices onto vertices drawn earlier. With 16-bit indices PrimReserve() starts a new VtxOffset when the
    // vertices would pass 65535, so _VtxCurrentIdx + 3 always fits.
    using DrawIdx = std::remove_pointer_t<decltype(ImDrawList::_IdxWritePtr)>;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(6, 4);
    const unsigned base = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(a, uv, top);
    dl->PrimWriteVtx(b, uv, top);
    dl->PrimWriteVtx({b.x, bottom}, uv, bottomColor);
    dl->PrimWriteVtx({a.x, bottom}, uv, bottomColor);
    for (const unsigned corner : {0u, 1u, 2u, 0u, 2u, 3u})
        dl->PrimWriteIdx(static_cast<DrawIdx>(base + corner));
}

} // namespace imlui::md
