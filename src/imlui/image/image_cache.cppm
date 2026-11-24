// ImageCache: images decoded from resources once and kept as ImGui textures, keyed by resource name.
module;

#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module imlui.image.image_cache;

import imgui;
import imlui.image.decode;
import imlui.resources;
import imlui.resources.mount;

export namespace imlui {

/// Decodes images (PNG, JPEG) from Resources on first use and keeps each one as an ImGui texture, per resource
/// name. Textures go through ImGui's texture list (ImTextureData, registered as a user texture): the renderer
/// backend uploads a new one before the frame that first draws it and destroys it after it was released, so the
/// cache makes no graphics API calls and works in a headless ImGui context.
///
/// Main thread only. get() needs a current ImGui context; the textures belong to the context that was current
/// when the first one was created.
class ImageCache {
public:
    /// A cached image: a texture ready to draw, or the reason there is none.
    class Image {
    public:
        /// The full resource name.
        [[nodiscard]] const std::string& name() const;
        /// Size in pixels; 0 x 0 on error.
        [[nodiscard]] int width() const;
        [[nodiscard]] int height() const;
        /// Why there is no texture (`image 'app:/x.png' not found`); "" when the image is ready.
        [[nodiscard]] const std::string& error() const;
        [[nodiscard]] bool ready() const;
        /// The texture to draw (ImDrawList::AddImage); an empty ref on error.
        [[nodiscard]] ImTextureRef texture() const;

    private:
        friend class ImageCache;
        std::string m_name;
        int m_width = 0;
        int m_height = 0;
        std::string m_error;
        std::optional<ResourceTime> m_modified; ///< the file's time when it was decoded
        std::unique_ptr<ImTextureData> m_texture;
    };

    /// `resources` must outlive the cache.
    explicit ImageCache(const Resources& resources);
    ~ImageCache();
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;
    ImageCache(ImageCache&&) = delete;
    ImageCache& operator=(ImageCache&&) = delete;

    /// The image of resource `name` (a full, normalized name). Decoded on the first call for a name; after that
    /// the cached image, also when it failed (a missing or broken file is reported once, not every frame). The
    /// reference stays valid until refresh() or clear() drops the image.
    const Image& get(std::string_view name);
    /// True when `name` is cached (decoded, or failed).
    [[nodiscard]] bool contains(std::string_view name) const;
    /// The cached names, sorted.
    [[nodiscard]] std::vector<std::string> names() const;
    /// How many files get() has decoded (or tried to) since the cache was created.
    [[nodiscard]] std::size_t decodeCount() const;

    /// Drops every image whose file changed since it was decoded (Resources::modified differs, or the file
    /// appeared or vanished), so the next get() decodes it again. Returns how many were dropped. The engine calls
    /// it with its hot-reload file checks. Call between frames.
    std::size_t refresh();
    /// Drops every image. Call between frames.
    void clear();
    /// Deletes released textures that the backend has destroyed. Call once per frame, after rendering.
    void collect();

private:
    /// Hands `texture` to the backend for destruction, or deletes it when the backend never created it.
    void release(std::unique_ptr<ImTextureData> texture);
    /// Removes `texture` from the ImGui texture list, when its context is still the current one.
    void unregister(ImTextureData* texture) const;

    const Resources& m_resources;
    std::map<std::string, Image, std::less<>> m_images;
    std::vector<std::unique_ptr<ImTextureData>> m_released; ///< waiting for the backend to destroy them
    ImGuiContext* m_context = nullptr;                      ///< where the textures are registered
    std::size_t m_decodes = 0;
};

} // namespace imlui

module :private;

namespace imlui {

const std::string& ImageCache::Image::name() const { return m_name; }
int ImageCache::Image::width() const { return m_width; }
int ImageCache::Image::height() const { return m_height; }
const std::string& ImageCache::Image::error() const { return m_error; }
bool ImageCache::Image::ready() const { return m_texture != nullptr; }
ImTextureRef ImageCache::Image::texture() const { return m_texture ? m_texture->GetTexRef() : ImTextureRef{}; }

ImageCache::ImageCache(const Resources& resources) : m_resources{resources} {}

ImageCache::~ImageCache() {
    // The engine destroys its ImGui context (after the backend destroyed every registered texture) before the
    // cache, so there is nothing to unregister there. A context that is still current (tests) is cleaned up.
    for (auto& [name, image] : m_images)
        if (image.m_texture) unregister(image.m_texture.get());
    for (auto& texture : m_released)
        unregister(texture.get());
}

const ImageCache::Image& ImageCache::get(std::string_view name) {
    if (const auto it = m_images.find(name); it != m_images.end()) return it->second;

    Image image;
    image.m_name = std::string{name};
    image.m_modified = m_resources.modified(name);
    ++m_decodes;
    const auto bytes = m_resources.read(name);
    const DecodedImage decoded = bytes ? decodeImage(bytes->bytes()) : DecodedImage{}; // copies the pixels
    if (!bytes) {
        image.m_error = "image '" + image.m_name + "' not found";
    } else if (!decoded.ok()) {
        image.m_error = "image '" + image.m_name + "': " + decoded.error;
    } else {
        image.m_width = decoded.width;
        image.m_height = decoded.height;
        auto texture = std::make_unique<ImTextureData>();
        texture->Create(ImTextureFormat_RGBA32, decoded.width, decoded.height);
        std::memcpy(texture->GetPixels(), decoded.rgba.data(), decoded.rgba.size());
        texture->UseColors = true;
        // Registered textures with status WantCreate are uploaded by the backend before the frame is drawn.
        ImGui::RegisterUserTexture(texture.get());
        m_context = ImGui::GetCurrentContext();
        image.m_texture = std::move(texture);
    }
    std::string key = image.m_name;
    return m_images.emplace(std::move(key), std::move(image)).first->second;
}

bool ImageCache::contains(std::string_view name) const { return m_images.find(name) != m_images.end(); }

std::vector<std::string> ImageCache::names() const {
    std::vector<std::string> out;
    out.reserve(m_images.size());
    for (const auto& [name, image] : m_images)
        out.push_back(name);
    return out;
}

std::size_t ImageCache::decodeCount() const { return m_decodes; }

std::size_t ImageCache::refresh() {
    std::size_t dropped = 0;
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (m_resources.modified(it->first) == it->second.m_modified) {
            ++it;
            continue;
        }
        release(std::move(it->second.m_texture));
        it = m_images.erase(it);
        ++dropped;
    }
    return dropped;
}

void ImageCache::clear() {
    for (auto& [name, image] : m_images)
        release(std::move(image.m_texture));
    m_images.clear();
}

void ImageCache::collect() {
    std::erase_if(m_released, [this](const std::unique_ptr<ImTextureData>& texture) {
        if (texture->Status != ImTextureStatus_Destroyed) return false;
        unregister(texture.get());
        return true;
    });
}

void ImageCache::release(std::unique_ptr<ImTextureData> texture) {
    if (!texture) return;
    // Never uploaded (no backend, or released before a frame drew it): nothing on the GPU to free. Released only
    // between frames, so no draw list still to be drawn refers to it.
    if (texture->TexID == imgui::invalidTextureId && texture->Status == ImTextureStatus_WantCreate) {
        unregister(texture.get());
        return;
    }
    // The next NewFrame() turns this into WantDestroy; the backend destroys it after that frame is drawn, and
    // collect() deletes it.
    texture->WantDestroyNextFrame = true;
    m_released.push_back(std::move(texture));
}

void ImageCache::unregister(ImTextureData* texture) const {
    if (m_context && ImGui::GetCurrentContext() == m_context) ImGui::UnregisterUserTexture(texture);
}

} // namespace imlui
