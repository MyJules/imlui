// Image: a PNG or JPEG resource, as a typed element.
export module imlui.ui.elements.image;

import std;

import imgui;
import imlui.color;
import imlui.widgets;
import imlui.lua.errors;
import imlui.resources.name;
import imlui.image.fill_mode;
import imlui.image.image_cache;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.type_builder;

export namespace imlui {

/// A PNG or JPEG image from the app's resources, decoded once per resource name and drawn as a texture.
///
/// Props:   source = "" (positional; a resource name, relative ones resolved against the folder of the script
///          that created the Image), fillMode = "stretch" ("fit", "crop", "tile"), radius = 0 (dp), opacity = 1,
///          tint = unset (multiplies the pixels), smooth = true (false: nearest-neighbor sampling)
/// Read-only: status ("loading", "ready", "error", or "none" without a source), implicitWidth, implicitHeight
///          (the image's size in pixels, drawn as dp)
/// Raw:     width, height (dp; unset: the natural size times the DPI scale; one set: the other keeps the aspect
///          ratio), __base (the creating script, set by the Lua constructor)
/// Signals: onStatusChanged(self, status), onImplicitWidthChanged, onImplicitHeightChanged
///
/// A missing or undecodable file is reported once and drawn as a placeholder (a broken-image icon).
class Image final : public Element {
public:
    static void declare(TypeBuilder<Image>& type);

    void render(Frame& frame) override;

    [[nodiscard]] const std::string& source() const;
    void setSource(std::string source);
    [[nodiscard]] const std::string& fillMode() const;
    void setFillMode(std::string fillMode);
    [[nodiscard]] float radius() const; ///< dp
    void setRadius(float radius);
    [[nodiscard]] double opacity() const;
    void setOpacity(double opacity);
    [[nodiscard]] const std::optional<Color>& tint() const;
    void setTint(std::optional<Color> tint);
    [[nodiscard]] bool smooth() const;
    void setSmooth(bool smooth);

    [[nodiscard]] const std::string& status() const;
    [[nodiscard]] double implicitWidth() const;
    [[nodiscard]] double implicitHeight() const;

    /// The element's size in pixels at the last render.
    [[nodiscard]] ImVec2 lastSize() const;
    /// The full resource name drawn at the last render ("" when the source was empty or invalid).
    [[nodiscard]] const std::string& resolvedSource() const;

private:
    /// Sets the read-only properties and tells Lua about the ones that changed.
    void publish(std::string status, double implicitWidth, double implicitHeight);
    /// Reports `message` to the error overlay the first time this element meets it.
    void reportOnce(const std::string& message);

    std::string m_source;
    std::string m_fillMode{"stretch"};
    float m_radius = 0.0f;
    double m_opacity = 1.0;
    std::optional<Color> m_tint;
    bool m_smooth = true;
    std::string m_status{"loading"};
    double m_implicitWidth = 0.0;
    double m_implicitHeight = 0.0;
    ImVec2 m_lastSize{0.0f, 0.0f};
    std::string m_resolved;
    std::set<std::string> m_reported;
};

} // namespace imlui

module :private;

namespace imlui {

namespace {

/// The size of the placeholder drawn for a broken image without a set size, in dp.
constexpr float placeholderSize = 48.0f;

} // namespace

void Image::declare(TypeBuilder<Image>& type) {
    type.property("source", &Image::source, &Image::setSource)
        .property("fillMode", &Image::fillMode, &Image::setFillMode)
        .property("radius", &Image::radius, &Image::setRadius)
        .property("opacity", &Image::opacity, &Image::setOpacity)
        .property("tint", &Image::tint, &Image::setTint)
        .property("smooth", &Image::smooth, &Image::setSmooth)
        .property("status", &Image::status)
        .property("implicitWidth", &Image::implicitWidth)
        .property("implicitHeight", &Image::implicitHeight)
        .defaultProperty("source");
}

void Image::render(Frame& frame) {
    // Resolve the source and look the image up.
    const ImageCache::Image* image = nullptr;
    std::string error;
    m_resolved.clear();
    if (!m_source.empty()) {
        const auto name = resolveName(frame.node().string("__base", "app:/"), m_source);
        if (!name)
            error = "Image: invalid source '" + m_source + "'";
        else if (ImageCache* images = frame.images(); !images)
            error = "Image: no image cache (the host didn't set one)";
        else {
            m_resolved = *name;
            image = &images->get(*name);
            if (!image->ready()) error = "Image: " + image->error();
        }
    }
    const auto mode = parseFillMode(m_fillMode);
    if (!mode) reportOnce("Image: unknown fillMode '" + m_fillMode + "' (stretch, fit, crop or tile)");
    if (!error.empty()) reportOnce(error);

    const bool ready = image && image->ready();
    const ImVec2 natural = ready                ? ImVec2{static_cast<float>(image->width()),
                                                         static_cast<float>(image->height())}
                           : !error.empty()     ? ImVec2{placeholderSize, placeholderSize}
                                                : ImVec2{0.0f, 0.0f};
    publish(ready ? "ready" : !error.empty() ? "error" : "none", ready ? image->width() : 0, ready ? image->height() : 0);

    // Lay out: one item of the computed size.
    // Unset (0) stays 0: imageSize() then derives the size from the image.
    const ImVec2 size = imageSize(natural, frame.dp(1.0f), frame.resolvedWidth(0.0f), frame.resolvedHeight(0.0f));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    m_lastSize = size;
    if (size.x <= 0.0f || size.y <= 0.0f) return;

    ImDrawList* dl = frame.drawList();
    const float radius = frame.dp(m_radius);
    if (!ready) {
        if (error.empty()) return;
        const auto& s = frame.scheme();
        md::fillRoundRect(dl, pos, {pos.x + size.x, pos.y + size.y}, radius, s.surfaceContainerHighest);
        const float icon = std::min(24.0f, std::min(size.x, size.y) / frame.dp(1.0f) * 0.6f);
        md::drawIcon(dl, "broken_image", {pos.x + size.x * 0.5f, pos.y + size.y * 0.5f}, icon, s.onSurfaceVariant);
        return;
    }

    Color color = m_tint.value_or(Color{1.0f, 1.0f, 1.0f, 1.0f});
    color.a *= static_cast<float>(std::clamp(m_opacity, 0.0, 1.0)) * ImGui::GetStyle().Alpha; // Alpha: disabled
    const ImU32 col = color.u32();
    const ImTextureRef texture = image->texture();
    const auto quads = placeImage(pos, size, natural, frame.dp(1.0f), mode.value_or(FillMode::Stretch));

    // Nearest-neighbor sampling through the backend's standard draw callbacks (absent without a backend).
    const ImGuiPlatformIO& io = ImGui::GetPlatformIO();
    const bool nearest = !m_smooth && io.DrawCallback_SetSamplerNearest && io.DrawCallback_SetSamplerLinear;
    if (nearest) dl->AddCallback(io.DrawCallback_SetSamplerNearest, nullptr);
    for (const ImageQuad& q : quads) {
        if (radius > 0.0f && quads.size() == 1)
            dl->AddImageRounded(texture, q.min, q.max, q.uvMin, q.uvMax, col, radius);
        else
            dl->AddImage(texture, q.min, q.max, q.uvMin, q.uvMax, col);
    }
    if (nearest) dl->AddCallback(io.DrawCallback_SetSamplerLinear, nullptr);
}

void Image::publish(std::string status, double implicitWidth, double implicitHeight) {
    const bool statusChanged = status != m_status;
    const bool widthChanged = implicitWidth != m_implicitWidth;
    const bool heightChanged = implicitHeight != m_implicitHeight;
    m_status = std::move(status);
    m_implicitWidth = implicitWidth;
    m_implicitHeight = implicitHeight;
    if (widthChanged) notify("implicitWidth");
    if (heightChanged) notify("implicitHeight");
    if (statusChanged) notify("status"); // last: a handler sees the new size
}

void Image::reportOnce(const std::string& message) {
    if (m_reported.insert(message).second) luaui::reportError(message);
}

const std::string& Image::source() const { return m_source; }
void Image::setSource(std::string source) { m_source = std::move(source); }
const std::string& Image::fillMode() const { return m_fillMode; }
void Image::setFillMode(std::string fillMode) { m_fillMode = std::move(fillMode); }
float Image::radius() const { return m_radius; }
void Image::setRadius(float radius) { m_radius = radius; }
double Image::opacity() const { return m_opacity; }
void Image::setOpacity(double opacity) { m_opacity = opacity; }
const std::optional<Color>& Image::tint() const { return m_tint; }
void Image::setTint(std::optional<Color> tint) { m_tint = tint; }
bool Image::smooth() const { return m_smooth; }
void Image::setSmooth(bool smooth) { m_smooth = smooth; }

const std::string& Image::status() const { return m_status; }
double Image::implicitWidth() const { return m_implicitWidth; }
double Image::implicitHeight() const { return m_implicitHeight; }

ImVec2 Image::lastSize() const { return m_lastSize; }
const std::string& Image::resolvedSource() const { return m_resolved; }

} // namespace imlui
