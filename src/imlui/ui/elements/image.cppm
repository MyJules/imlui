// Image: a PNG or JPEG resource, as a typed element.
export module imlui.ui.elements.image;

import std;

import imgui;
import lua;
import imlui.color;
import imlui.widgets;
import imlui.lua.errors;
import imlui.lua.node;
import imlui.resources.name;
import imlui.image.fill_mode;
import imlui.image.image_cache;
import imlui.types.element;
import imlui.types.frame;
import imlui.types.lua_convert;
import imlui.types.type_builder;
import imlui.ui.enum_names;

export namespace imlui {

/// A PNG or JPEG image from the app's resources, decoded once per resource name and drawn as a texture.
///
/// Props:   source = "" (positional; a resource name, relative ones resolved against the folder of the script
///          that created the Image), fillMode = "stretch" ("fit", "crop", "tile";
///          "preserveAspectFit" is fit, "preserveAspectCrop" crop), radius = 0 (dp), opacity = 1,
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
    /// Where the image is: its read-only `status`.
    enum class Status { Loading, Ready, Error, None };

    static void declare(TypeBuilder<Image>& type);

    void render(Frame& frame) override;
    /// Its `width` / `height` (a fill width of `maxWidth`), the image's own size (as dp) or the aspect ratio for
    /// what is unset, the placeholder's for a broken image. None for a fill height.
    [[nodiscard]] std::optional<ImVec2> intrinsicSize(const Frame& frame, float maxWidth) override;

    [[nodiscard]] const std::string& source() const;
    void setSource(std::string source);
    [[nodiscard]] FillMode fillMode() const;
    void setFillMode(FillMode fillMode);
    [[nodiscard]] float radius() const; ///< dp
    void setRadius(float radius);
    [[nodiscard]] double opacity() const;
    void setOpacity(double opacity);
    [[nodiscard]] const std::optional<Color>& tint() const;
    void setTint(std::optional<Color> tint);
    [[nodiscard]] bool smooth() const;
    void setSmooth(bool smooth);

    [[nodiscard]] Status status() const;
    [[nodiscard]] double implicitWidth() const;
    [[nodiscard]] double implicitHeight() const;

    /// The element's size in pixels at the last render.
    [[nodiscard]] ImVec2 lastSize() const;
    /// The full resource name drawn at the last render ("" when the source was empty or invalid).
    [[nodiscard]] const std::string& resolvedSource() const;

private:
    /// The source looked up: the image (null without a source or when it can't be drawn), why it can't be drawn
    /// (null when it can), and its natural size (image pixels; the placeholder's for a broken image). The strings
    /// are the element's own (the cached resolution and messages), valid until the next lookup.
    struct Lookup {
        const ImageCache::Image* image = nullptr;
        const std::string* error = nullptr;
        const std::string* resolved = nullptr; ///< the full resource name
        ImVec2 natural{0.0f, 0.0f};
    };
    /// Looks the source up: resolved (cached, see m_resolvedFrom), then in the image cache, every time (an image may
    /// be evicted between frames, or still be decoding).
    [[nodiscard]] Lookup lookup(const Frame& frame);
    /// The source resolved against `__base`: the full name, nullopt for an invalid source.
    [[nodiscard]] const std::optional<std::string>& resolvedName(const Frame& frame);
    /// "Image: <error>" for a broken image, made again only when its error changed.
    [[nodiscard]] const std::string& brokenMessage(const std::string& error);

    /// Sets the read-only properties and tells Lua about the ones that changed.
    void publish(Status status, double implicitWidth, double implicitHeight);
    /// Reports `message` to the error overlay the first time this element meets it.
    void reportOnce(const std::string& message);

    std::string m_source;
    FillMode m_fillMode = FillMode::Stretch;
    float m_radius = 0.0f;
    double m_opacity = 1.0;
    std::optional<Color> m_tint;
    bool m_smooth = true;
    Status m_status = Status::Loading;
    double m_implicitWidth = 0.0;
    double m_implicitHeight = 0.0;
    ImVec2 m_lastSize{0.0f, 0.0f};
    std::string m_resolved;
    std::set<std::string> m_reported;

    /// The resolution of `source` against the creating script's folder (`__base`), kept while neither changes:
    /// resolving allocates, and an Image looks its source up on every frame (twice when a layout asks its size).
    bool m_resolvedValid = false;
    std::string m_resolvedFrom;        ///< the source it was resolved from
    std::string m_resolvedBase;        ///< the base it was resolved against
    std::optional<std::string> m_name; ///< the full resource name; nullopt for an invalid source
    std::string m_invalidMessage;      ///< the error for an invalid source
    std::string m_brokenFrom;          ///< the image error m_brokenMessage was made from
    std::string m_brokenMessage;
};

} // namespace imlui

template <>
struct imlui::EnumNames<imlui::Image::Status> {
    static constexpr std::string_view name = "ImageStatus";
    static constexpr std::string_view doc = "Where an Image is: loading, ready, error or none (no source).";
    static constexpr std::pair<imlui::Image::Status, std::string_view> values[] = {
        {imlui::Image::Status::Loading, "loading"},
        {imlui::Image::Status::Ready, "ready"},
        {imlui::Image::Status::Error, "error"},
        {imlui::Image::Status::None, "none"},
    };
};

module :private;

namespace imlui {

namespace {

/// The size of the placeholder drawn for a broken image without a set size, in dp.
constexpr float placeholderSize = 48.0f;
/// The base a source is resolved against when the element has no `__base` (made outside a script).
constexpr std::string_view defaultBase = "app:/";

const std::string noImageCache = "Image: no image cache (the host didn't set one)";

} // namespace

void Image::declare(TypeBuilder<Image>& type) {
    type.doc("A PNG or JPEG image from the app's resources, decoded once per resource name and drawn as a "
             "texture. A missing or undecodable file is reported once and drawn as a broken-image placeholder.\n"
             "`Image { \"img/logo.png\" }` sets `source`. Natural size: the image's pixels as dp (times the DPI "
             "scale); with only `width` or `height` set, the other follows the aspect ratio.")
        .property("source", &Image::source, &Image::setSource)
        .doc("A resource name: `\"app:/img/logo.png\"`, or relative to the folder of the script that created the "
             "`Image`. The positional value. `\"\"`: nothing is drawn and no space is taken.")
        .property("fillMode", &Image::fillMode, &Image::setFillMode)
        .doc("How the image fills its size when the aspect ratios differ: `\"stretch\"` (default), `\"fit\"` (inside, "
             "centered), `\"crop\"` (covers it, centered and cut off) or `\"tile\"` (repeated at its natural size). "
             "QML's `\"preserveAspectFit\"` and `\"preserveAspectCrop\"` are `\"fit\"` and `\"crop\"`.")
        .property("radius", &Image::radius, &Image::setRadius)
        .doc("The corner radius (dp); not applied to `\"tile\"`. Half the size with `\"crop\"`: a circle.")
        .property("opacity", &Image::opacity, &Image::setOpacity)
        .doc("0 to 1.")
        .property("tint", &Image::tint, &Image::setTint)
        .doc("Multiplies the pixels (`\"primary\"`, `\"#FFFFFF80\"`); its alpha multiplies the opacity.")
        .property("smooth", &Image::smooth, &Image::setSmooth)
        .doc("`false` samples the nearest pixel (pixel art, scaled-up icons) instead of filtering.")
        .property("status", &Image::status)
        .doc("Read-only: `\"loading\"` (before the first render), `\"ready\"`, `\"error\"` (missing or undecodable "
             "file) or `\"none\"` (no source).")
        .property("implicitWidth", &Image::implicitWidth)
        .doc("Read-only: the image's width in pixels; 0 until it's ready.")
        .property("implicitHeight", &Image::implicitHeight)
        .doc("Read-only: the image's height in pixels; 0 until it's ready.")
        .defaultProperty("source");
}

const std::optional<std::string>& Image::resolvedName(const Frame& frame) {
    // `__base` compared in place: nothing is allocated while the source and the base stay the same. A base that
    // isn't a string or nil (the default) is converted like Node::string() does, and not kept.
    const luaui::Node& node = frame.node();
    lua::State* state = node.state();
    const lua::Type t = node.push("__base");
    const bool keep = t == lua::Type::String || t == lua::Type::Nil;
    const std::string_view base = t == lua::Type::String ? lua::tostring(state, -1) : defaultBase;
    const bool same = keep && m_resolvedValid && m_resolvedFrom == m_source && m_resolvedBase == base;
    if (!same && keep) m_resolvedBase.assign(base);
    lua::pop(state);
    if (same) return m_name;
    if (!keep) m_resolvedBase = node.string("__base", defaultBase);
    m_resolvedValid = keep;
    m_resolvedFrom = m_source;
    m_name = resolveName(m_resolvedBase, m_source);
    if (!m_name) m_invalidMessage = "Image: invalid source '" + m_source + "'";
    return m_name;
}

const std::string& Image::brokenMessage(const std::string& error) {
    if (m_brokenMessage.empty() || m_brokenFrom != error) {
        m_brokenFrom = error;
        m_brokenMessage = "Image: " + error;
    }
    return m_brokenMessage;
}

Image::Lookup Image::lookup(const Frame& frame) {
    Lookup l;
    if (!m_source.empty()) {
        const std::optional<std::string>& name = resolvedName(frame);
        if (!name)
            l.error = &m_invalidMessage;
        else if (ImageCache* images = frame.images(); !images)
            l.error = &noImageCache;
        else {
            l.resolved = &*name;
            l.image = &images->get(*name);
            // Still decoding in the background (ImageCache::setAsync): no error, no size yet; the finished decode
            // requests the frame that shows it.
            if (!l.image->ready() && !l.image->loading()) l.error = &brokenMessage(l.image->error());
        }
    }
    if (l.image && l.image->ready())
        l.natural = {static_cast<float>(l.image->width()), static_cast<float>(l.image->height())};
    else if (l.error)
        l.natural = {placeholderSize, placeholderSize};
    return l;
}

std::optional<ImVec2> Image::intrinsicSize(const Frame& frame, float maxWidth) {
    const float height = frame.size("height");
    if (height < 0) return std::nullopt; // fills a height known only where it renders
    // render()'s size: decoding the image now is what render() would do on this frame anyway.
    return imageSize(lookup(frame).natural, frame.dp(1.0f), frame.widthIn(maxWidth), height);
}

void Image::render(Frame& frame) {
    // Resolve the source and look the image up.
    const Lookup found = lookup(frame);
    const ImageCache::Image* image = found.image;
    const bool broken = found.error != nullptr;
    if (!found.resolved)
        m_resolved.clear();
    else if (m_resolved != *found.resolved)
        m_resolved = *found.resolved;
    if (broken) reportOnce(*found.error);

    const bool ready = image && image->ready();
    const ImVec2 natural = found.natural;
    const bool loading = image && image->loading();
    const Status status = ready     ? Status::Ready
                          : broken  ? Status::Error
                          : loading ? Status::Loading
                                    : Status::None;
    publish(status, ready ? image->width() : 0, ready ? image->height() : 0);

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
        if (!broken) return;
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
    const auto quads = placeImage(pos, size, natural, frame.dp(1.0f), m_fillMode);

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

void Image::publish(Status status, double implicitWidth, double implicitHeight) {
    const bool statusChanged = status != m_status;
    const bool widthChanged = implicitWidth != m_implicitWidth;
    const bool heightChanged = implicitHeight != m_implicitHeight;
    m_status = status;
    m_implicitWidth = implicitWidth;
    m_implicitHeight = implicitHeight;
    if (widthChanged) notify("implicitWidth");
    if (heightChanged) notify("implicitHeight");
    if (statusChanged) notify("status"); // last: a handler sees the new size
}

void Image::reportOnce(const std::string& message) {
    // A lookup on the frames after the first: no copy of the message is made.
    if (m_reported.contains(message)) return;
    m_reported.insert(message);
    luaui::reportError(message);
}

const std::string& Image::source() const { return m_source; }
void Image::setSource(std::string source) { m_source = std::move(source); }
FillMode Image::fillMode() const { return m_fillMode; }
void Image::setFillMode(FillMode fillMode) { m_fillMode = fillMode; }
float Image::radius() const { return m_radius; }
void Image::setRadius(float radius) { m_radius = radius; }
double Image::opacity() const { return m_opacity; }
void Image::setOpacity(double opacity) { m_opacity = opacity; }
const std::optional<Color>& Image::tint() const { return m_tint; }
void Image::setTint(std::optional<Color> tint) { m_tint = tint; }
bool Image::smooth() const { return m_smooth; }
void Image::setSmooth(bool smooth) { m_smooth = smooth; }

Image::Status Image::status() const { return m_status; }
double Image::implicitWidth() const { return m_implicitWidth; }
double Image::implicitHeight() const { return m_implicitHeight; }

ImVec2 Image::lastSize() const { return m_lastSize; }
const std::string& Image::resolvedSource() const { return m_resolved; }

} // namespace imlui
