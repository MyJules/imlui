// How an image fills its element: the element's size from the image's natural size, and the quads (screen rect and
// texture coordinates) that draw it for each fill mode.
export module imlui.image.fill_mode;

import std;

import imgui;

export namespace imlui {

/// How an image is scaled into its element's rect (QML's Image.fillMode).
enum class FillMode {
    Stretch, ///< scaled to the rect; the aspect ratio may change
    Fit,     ///< scaled uniformly to fit inside the rect, centered (letterboxed)
    Crop,    ///< scaled uniformly to cover the rect, centered; the overflow is cut off
    Tile,    ///< repeated at its natural size from the top-left corner; the last row and column are cut off
};

/// One textured quad: a screen rect and the part of the texture drawn into it.
struct ImageQuad {
    ImVec2 min;
    ImVec2 max;
    ImVec2 uvMin{0.0f, 0.0f};
    ImVec2 uvMax{1.0f, 1.0f};
};

/// "stretch", "fit" (also "preserveAspectFit"), "crop" ("preserveAspectCrop") or "tile"; nullopt otherwise.
[[nodiscard]] std::optional<FillMode> parseFillMode(std::string_view name);

/// The size in pixels of an element showing an image of `natural` size (image pixels, drawn as dp) at `scale`
/// pixels per dp, with a requested `width`/`height` in pixels (0: unset). Neither set: the natural size times the
/// scale. One set: the other follows the image's aspect ratio.
[[nodiscard]] ImVec2 imageSize(ImVec2 natural, float scale, float width, float height);

/// The quads that draw an image of `natural` size (image pixels) into the rect at `pos` of `size` pixels.
/// `scale` (pixels per dp) is the tile size factor of FillMode::Tile.
[[nodiscard]] std::vector<ImageQuad> placeImage(ImVec2 pos, ImVec2 size, ImVec2 natural, float scale, FillMode mode);

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// More tiles than this draw the image stretched instead (a tiny image in a huge rect).
constexpr int maxTiles = 4096;

float snap(float v) { return std::floor(v + 0.5f); }

} // namespace

std::optional<FillMode> parseFillMode(std::string_view name) {
    if (name == "stretch") return FillMode::Stretch;
    if (name == "fit" || name == "preserveAspectFit") return FillMode::Fit;
    if (name == "crop" || name == "preserveAspectCrop") return FillMode::Crop;
    if (name == "tile") return FillMode::Tile;
    return std::nullopt;
}

ImVec2 imageSize(ImVec2 natural, float scale, float width, float height) {
    const bool known = natural.x > 0.0f && natural.y > 0.0f;
    if (width > 0.0f && height > 0.0f) return {width, height};
    if (!known) return {std::max(width, 0.0f), std::max(height, 0.0f)};
    if (width > 0.0f) return {width, width * natural.y / natural.x};
    if (height > 0.0f) return {height * natural.x / natural.y, height};
    return {natural.x * scale, natural.y * scale};
}

std::vector<ImageQuad> placeImage(ImVec2 pos, ImVec2 size, ImVec2 natural, float scale, FillMode mode) {
    const ImVec2 end{pos.x + size.x, pos.y + size.y};
    if (size.x <= 0.0f || size.y <= 0.0f || natural.x <= 0.0f || natural.y <= 0.0f) return {};
    switch (mode) {
    case FillMode::Stretch:
        break;
    case FillMode::Fit: {
        const float s = std::min(size.x / natural.x, size.y / natural.y);
        const ImVec2 drawn{natural.x * s, natural.y * s};
        const ImVec2 min{snap(pos.x + (size.x - drawn.x) * 0.5f), snap(pos.y + (size.y - drawn.y) * 0.5f)};
        return {{min, {min.x + snap(drawn.x), min.y + snap(drawn.y)}}};
    }
    case FillMode::Crop: {
        const float s = std::max(size.x / natural.x, size.y / natural.y);
        const float fx = size.x / (natural.x * s); // the visible fraction of the texture
        const float fy = size.y / (natural.y * s);
        return {{pos, end, {(1.0f - fx) * 0.5f, (1.0f - fy) * 0.5f}, {(1.0f + fx) * 0.5f, (1.0f + fy) * 0.5f}}};
    }
    case FillMode::Tile: {
        const ImVec2 tile{natural.x * scale, natural.y * scale};
        if (tile.x < 1.0f || tile.y < 1.0f) break;
        const float columns = std::ceil(size.x / tile.x);
        const float rows = std::ceil(size.y / tile.y);
        if (columns * rows > static_cast<float>(maxTiles)) break;
        std::vector<ImageQuad> quads;
        for (float y = 0.0f; y < size.y; y += tile.y) {
            const float h = std::min(tile.y, size.y - y);
            for (float x = 0.0f; x < size.x; x += tile.x) {
                const float w = std::min(tile.x, size.x - x);
                quads.push_back({{pos.x + x, pos.y + y}, {pos.x + x + w, pos.y + y + h}, {0.0f, 0.0f},
                                 {w / tile.x, h / tile.y}});
            }
        }
        return quads;
    }
    }
    return {{pos, end}};
}

} // namespace imlui
