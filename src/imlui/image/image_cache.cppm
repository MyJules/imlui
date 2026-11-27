// ImageCache: images decoded from resources once and kept as ImGui textures, keyed by resource name.
export module imlui.image.image_cache;

import std;

import imgui;
import imlui.image.decode;
import imlui.resources;
import imlui.resources.mount;
import imlui.resources.name;
import imlui.ui.background;
import imlui.ui.frame_request;
import imlui.ui.main_thread;

export namespace imlui {

/// Decodes images (PNG, JPEG) from Resources on first use and keeps each one as an ImGui texture, per resource
/// name. Textures go through ImGui's texture list (ImTextureData, registered as a user texture): the renderer
/// backend uploads a new one before the frame that first draws it and destroys it after it was released, so the
/// cache makes no graphics API calls and works in a headless ImGui context. The texture takes the decoder's pixel
/// buffer as it is (no copy, no zero-filled buffer of its own), and collect() frees that buffer once the backend has
/// uploaded it (its status is OK), so an image costs its pixels once, on the GPU. (A backend that dropped its textures
/// mid-run would have nothing to create them again from; imlui's destroys them only when it shuts down.)
///
/// Decoding takes a frame's time and more for a large image (hundreds of ms for a photo), so with setAsync(true) it
/// runs on the background pool's decode lane (imlui.ui.background, BackgroundLane::Decodes: its own workers, newest
/// request first): get() returns an image that is loading() at once, the texture is made and registered on the main
/// thread by a later get() or collect() once the pixels are there, and a frame is requested for it (imlui::post, then
/// requestFrame(): one post for all the decodes that finish before the main thread takes their results). A decode whose
/// image was dropped before it started (refresh(), clear(), the cache destroyed) skips its work. Without it (the
/// default) get() decodes before it returns.
///
/// The decoded images are kept within a byte budget (setBudget(), 4 bytes per pixel; failures and loading images count
/// as 0): after each frame, collect() evicts the images drawn least recently until the cache fits, never one drawn in
/// the current or the previous frame (draw lists may still refer to it). An evicted image is decoded again by its next
/// get(). Failures (a missing file is cached so it is reported once) are kept up to maxFailures, then the ones asked
/// for least recently go the same way, so names made up at run time can't grow the cache without bound.
///
/// Names are normalized (imlui.resources.name): `app:/a/./b.png` and `app:/a/b.png` are one image.
///
/// Main thread only. get() and collect() need a current ImGui context (its frame count tells the frames apart); the
/// textures belong to the context that was current when the first one was created.
class ImageCache {
public:
    /// A cached image: a texture ready to draw, still loading, or the reason there is none.
    class Image {
    public:
        /// The full resource name (normalized).
        [[nodiscard]] const std::string& name() const;
        /// Size in pixels; 0 x 0 on error and while loading.
        [[nodiscard]] int width() const;
        [[nodiscard]] int height() const;
        /// Why there is no texture (`image 'app:/x.png' not found`); "" when the image is ready or loading.
        [[nodiscard]] const std::string& error() const;
        [[nodiscard]] bool ready() const;
        /// True while it is being decoded in the background (setAsync()): neither ready() nor an error() yet.
        [[nodiscard]] bool loading() const;
        /// The texture to draw (ImDrawList::AddImage); an empty ref on error and while loading.
        [[nodiscard]] ImTextureRef texture() const;

    private:
        friend class ImageCache;
        /// Frees the texture with the pixels it took from the decoder (stb_image's allocator, not ImGui's).
        struct DeleteTexture {
            void operator()(ImTextureData* texture) const;
        };
        using Texture = std::unique_ptr<ImTextureData, DeleteTexture>;

        std::string m_name;
        int m_width = 0;
        int m_height = 0;
        std::string m_error;
        std::optional<ResourceTime> m_modified; ///< the file's time when it was decoded (or the decode started)
        Texture m_texture;
        int m_lastUsed = 0;         ///< the ImGui frame of the last get()
        std::size_t m_bytes = 0;    ///< the pixels' size; 0 on error and while loading
        std::uint64_t m_ticket = 0; ///< the background decode it waits for (loading); 0 when none
        /// Shared with that decode: cleared when the image is dropped, so a decode that hasn't started skips its work.
        std::shared_ptr<std::atomic<bool>> m_wanted;
        bool m_stale = false; ///< its decode was dropped unrun (the pool shut down): refresh() drops it
    };

    /// The default budget: 256 MiB of pixels.
    static constexpr std::size_t defaultBudget = std::size_t{256} << 20;
    /// The most failed images kept (each a name and its error, so it is reported once): beyond it collect() drops
    /// those asked for least recently, sparing the current and the previous frame's.
    static constexpr std::size_t maxFailures = 256;

    /// `resources` must outlive the cache. Background decodes read from a copy of it (which shares its mounts).
    explicit ImageCache(const Resources& resources);
    ~ImageCache();
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;
    ImageCache(ImageCache&&) = delete;
    ImageCache& operator=(ImageCache&&) = delete;

    /// The image of resource `name` (a full name; normalized here). Decoded on the first call for a name (in the
    /// background with setAsync(true): until then the image is loading()); after that the cached image, also when it
    /// failed (a missing or broken file is reported once, not every frame). Marks the image as drawn in the current
    /// frame. The reference stays valid until the end of the frame (collect() may evict the image after a later one),
    /// or until refresh() or clear() drops the image. Takes the results of finished background decodes first.
    const Image& get(std::string_view name);
    /// True when `name` is cached (decoded, loading, or failed).
    [[nodiscard]] bool contains(std::string_view name) const;
    /// The cached names, sorted.
    [[nodiscard]] std::vector<std::string> names() const;
    /// How many files get() has decoded (or started to, or tried to) since the cache was created, evicted images
    /// decoded again included.
    [[nodiscard]] std::size_t decodeCount() const;

    /// Whether get() decodes on the background pool (true) or before it returns (false, the default).
    [[nodiscard]] bool async() const;
    void setAsync(bool async);
    /// Background decodes not taken into the cache yet (running, or finished and waiting for get() or collect()).
    [[nodiscard]] std::size_t pendingCount() const;
    /// Waits up to `timeout` for the background decodes to finish, then takes their results (as get() does). For
    /// tests and tools without a frame loop. True when none is left.
    bool waitForPending(std::chrono::milliseconds timeout = std::chrono::seconds{10});

    /// The most bytes of pixels (4 per pixel) the cached images may hold once collect() has run; 0: no limit.
    /// Default: defaultBudget. ApplicationWindow's `imageCacheBudget` prop sets it from Lua.
    [[nodiscard]] std::size_t budget() const;
    void setBudget(std::size_t bytes);
    /// The bytes of pixels the cached images hold now.
    [[nodiscard]] std::size_t bytes() const;

    /// Drops every image whose file changed since it was decoded (Resources::modified differs, or the file
    /// appeared or vanished), so the next get() decodes it again; an image still loading too (its result is then
    /// ignored). Returns how many were dropped. Call between frames.
    std::size_t refresh();
    /// refresh() with the files' times read beforehand (off the main thread: the engine's hot-reload scan), by name;
    /// the images whose names aren't in `times` are left as they are (they weren't asked about).
    std::size_t refresh(const std::map<std::string, std::optional<ResourceTime>>& times);
    /// Drops every image (loading ones too). Call between frames.
    void clear();
    /// Takes the results of finished background decodes, deletes released textures that the backend has destroyed,
    /// then evicts the images drawn least recently until the cache is within its budget, and the failures asked for
    /// least recently beyond maxFailures, sparing those drawn in the current or the previous frame. Call once per
    /// frame, after rendering.
    void collect();

private:
    using Texture = Image::Texture;
    struct Inbox;
    class DecodeJob;

    /// Hands `texture` to the backend for destruction, or deletes it when the backend never created it.
    void release(Texture texture);
    /// Removes `texture` from the ImGui texture list, when its context is still the current one.
    void unregister(ImTextureData* texture) const;
    /// Makes `image` ready from `decoded` (a texture over its pixels, registered with the current context), or failed
    /// with the decoder's reason, or "not found" when the file wasn't `found`.
    void finish(Image& image, bool found, DecodedImage decoded);
    /// Moves the finished background decodes into their images (those still waiting for them).
    void takeResults();
    /// Frees the CPU pixels of the textures the backend has uploaded (m_uploading).
    void freeUploadedPixels();
    /// Sets the image's error, counting it in m_failures.
    void fail(Image& image, std::string error);
    /// True for an image counted in m_failures.
    [[nodiscard]] static bool failed(const Image& image);

    using Images = std::map<std::string, Image, std::less<>>;
    /// Drops cached image `it`, releasing its texture. Returns the next one.
    Images::iterator drop(Images::iterator it);
    /// Evicts the images drawn least recently, before the previous frame, until the cache fits its budget, and the
    /// failures beyond maxFailures.
    void evict();

    const Resources& m_resources;
    Images m_images;
    std::vector<Texture> m_released; ///< waiting for the backend to destroy them
    /// Textures waiting for the backend's upload, by image name: their CPU pixels are freed once it is done.
    std::vector<std::pair<std::string, ImTextureData*>> m_uploading;
    ImGuiContext* m_context = nullptr; ///< where the textures are registered
    std::shared_ptr<Inbox> m_inbox;    ///< shared with the background decodes, which may outlive the cache
    std::uint64_t m_nextTicket = 1;
    std::size_t m_decodes = 0;
    std::size_t m_budget = defaultBudget;
    std::size_t m_bytes = 0;    ///< the sum of the cached images' m_bytes
    std::size_t m_failures = 0; ///< the cached images that failed (failed())
    bool m_async = false;
};

} // namespace imlui

module :private;

namespace imlui {

/// Where background decodes leave their results for the main thread. Shared by the cache and every decode in flight,
/// so a decode that finishes after the cache is gone has somewhere to put its result (freed unused with the inbox).
struct ImageCache::Inbox {
    struct Result {
        std::string name;
        std::uint64_t ticket = 0;
        bool found = false;     ///< the file was read
        bool cancelled = false; ///< dropped unrun (a shutdown of the pool) or stopped
        DecodedImage decoded;
    };
    std::mutex mutex;
    std::condition_variable finished; ///< `inFlight` went down
    std::vector<Result> results;
    std::size_t inFlight = 0;       ///< decodes queued or running
    std::atomic<bool> ready{false}; ///< `results` isn't empty: get() takes the lock only then
};

/// One background decode. It hands in its result exactly once: when it ran, or when it is destroyed without having run
/// (a shutdown drops queued work), so the cache never waits for a decode that will never come.
class ImageCache::DecodeJob {
public:
    DecodeJob(std::shared_ptr<Inbox> inbox, Resources resources, std::string name, std::uint64_t ticket,
              std::shared_ptr<const std::atomic<bool>> wanted)
        : m_inbox{std::move(inbox)}, m_resources{std::move(resources)}, m_name{std::move(name)}, m_ticket{ticket},
          m_wanted{std::move(wanted)} {}
    DecodeJob(const DecodeJob&) = delete;
    DecodeJob& operator=(const DecodeJob&) = delete;
    DecodeJob(DecodeJob&&) = delete;
    DecodeJob& operator=(DecodeJob&&) = delete;

    ~DecodeJob() {
        if (m_delivered) return;
        try {
            deliver(false, {}, true);
        } catch (...) { // NOLINT(bugprone-empty-catch): out of memory in a destructor; nothing else to do
        }
    }

    void run(const std::stop_token& stop) {
        // Stopped, or the image was dropped meanwhile (its result would be ignored): nothing to read or decode.
        if (stop.stop_requested() || !m_wanted->load(std::memory_order_relaxed)) {
            deliver(false, {}, true);
            return;
        }
        // Off the main thread: the read (a directory mount's file, a compressed file's decompression) and the decode.
        const auto bytes = m_resources.read(m_name);
        DecodedImage decoded = bytes ? decodeImage(bytes->bytes()) : DecodedImage{};
        deliver(bytes.has_value(), std::move(decoded), false);
    }

private:
    void deliver(bool found, DecodedImage decoded, bool cancelled) {
        m_delivered = true;
        bool first = false;
        {
            std::scoped_lock lock{m_inbox->mutex};
            first = m_inbox->results.empty();
            m_inbox->results.push_back({m_name, m_ticket, found, cancelled, std::move(decoded)});
            m_inbox->ready.store(true, std::memory_order_release);
        }
        // Wakes the loop (post() does) and asks for the frame that shows the image; requestFrame() is main-thread
        // only, so it runs there. Nothing of the cache is captured: it may be gone by then. Only the result that found
        // the inbox empty posts: the frame it asks for takes the ones after it too, and the main thread empties the
        // inbox (under the lock) before it takes them, so a result after that posts again. After the result is in the
        // inbox (the frame it asks for finds it), before the decode counts as finished: waitForPending() returns only
        // once the post is made.
        if (first) post([] { requestFrame(); });
        {
            std::scoped_lock lock{m_inbox->mutex};
            --m_inbox->inFlight;
        }
        m_inbox->finished.notify_all();
    }

    std::shared_ptr<Inbox> m_inbox;
    Resources m_resources;
    std::string m_name;
    std::uint64_t m_ticket;
    std::shared_ptr<const std::atomic<bool>> m_wanted;
    bool m_delivered = false;
};

namespace {

/// The current ImGui frame; 0 without a context.
int currentFrame() { return ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0; }

} // namespace

void ImageCache::Image::DeleteTexture::operator()(ImTextureData* texture) const {
    // The pixels came from stb_image (decodeImage); ImTextureData's destructor would free them with IM_FREE.
    DecodedImage::FreePixels{}(texture->Pixels);
    texture->Pixels = nullptr;
    delete texture;
}

const std::string& ImageCache::Image::name() const { return m_name; }
int ImageCache::Image::width() const { return m_width; }
int ImageCache::Image::height() const { return m_height; }
const std::string& ImageCache::Image::error() const { return m_error; }
bool ImageCache::Image::ready() const { return m_texture != nullptr; }
bool ImageCache::Image::loading() const { return m_ticket != 0; }
ImTextureRef ImageCache::Image::texture() const { return m_texture ? m_texture->GetTexRef() : ImTextureRef{}; }

ImageCache::ImageCache(const Resources& resources) : m_resources{resources}, m_inbox{std::make_shared<Inbox>()} {}

ImageCache::~ImageCache() {
    // The engine destroys its ImGui context (after the backend destroyed every registered texture) before the
    // cache, so there is nothing to unregister there. A context that is still current (tests) is cleaned up.
    // Decodes still in flight keep the inbox; their results are freed with it. Those that haven't started skip.
    for (auto& [name, image] : m_images) {
        if (image.m_wanted) image.m_wanted->store(false, std::memory_order_relaxed);
        if (image.m_texture) unregister(image.m_texture.get());
    }
    for (auto& texture : m_released)
        unregister(texture.get());
}

void ImageCache::finish(Image& image, bool found, DecodedImage decoded) {
    if (!found) {
        fail(image, "image '" + image.m_name + "' not found");
        return;
    }
    if (!decoded.ok() || !decoded.pixels) {
        fail(image, "image '" + image.m_name + "': " + (decoded.error.empty() ? "no pixels" : decoded.error));
        return;
    }
    // The texture takes the decoder's buffer as its pixels: what ImTextureData::Create() sets up, without allocating
    // (IM_ALLOC, unchecked but for an assert) and zero-filling a buffer of its own to copy them into. DeleteTexture
    // frees the buffer the decoder's way.
    Texture texture{new ImTextureData};
    texture->Format = ImTextureFormat_RGBA32;
    texture->Status = ImTextureStatus_WantCreate;
    texture->Width = decoded.width;
    texture->Height = decoded.height;
    texture->BytesPerPixel = 4;
    texture->Pixels = decoded.pixels.release();
    texture->UpdateRect.x = texture->UpdateRect.y = static_cast<unsigned short>(~0U);
    texture->UseColors = true;
    // Registered textures with status WantCreate are uploaded by the backend before the frame is drawn.
    ImGui::RegisterUserTexture(texture.get());
    m_context = ImGui::GetCurrentContext();
    image.m_width = decoded.width;
    image.m_height = decoded.height;
    image.m_bytes = decoded.rgba.size();
    m_uploading.emplace_back(image.m_name, texture.get());
    image.m_texture = std::move(texture);
    m_bytes += image.m_bytes;
}

bool ImageCache::failed(const Image& image) { return !image.m_error.empty() && !image.loading(); }

void ImageCache::fail(Image& image, std::string error) {
    const bool counted = failed(image);
    image.m_error = std::move(error);
    if (!counted && failed(image)) ++m_failures;
}

void ImageCache::freeUploadedPixels() {
    std::erase_if(m_uploading, [this](const std::pair<std::string, ImTextureData*>& entry) {
        const auto it = m_images.find(entry.first);
        // Dropped meanwhile (its texture went with it), or replaced by a later decode with a texture of its own.
        if (it == m_images.end() || it->second.m_texture.get() != entry.second) return true;
        ImTextureData* texture = entry.second;
        if (texture->Status != ImTextureStatus_OK) return false; // not uploaded yet
        // On the GPU now: the backend reads Pixels only to create or update a texture, and nothing updates these.
        DecodedImage::FreePixels{}(texture->Pixels);
        texture->Pixels = nullptr;
        return true;
    });
}

void ImageCache::takeResults() {
    if (!m_inbox->ready.load(std::memory_order_acquire)) return;
    std::vector<Inbox::Result> results;
    {
        std::scoped_lock lock{m_inbox->mutex};
        results.swap(m_inbox->results);
        m_inbox->ready.store(false, std::memory_order_relaxed);
    }
    for (auto& r : results) {
        const auto it = m_images.find(r.name);
        // Dropped meanwhile (refresh(), clear(); eviction spares loading images), or asked for again since.
        if (it == m_images.end() || it->second.m_ticket != r.ticket) continue;
        Image& image = it->second;
        image.m_ticket = 0;
        image.m_wanted.reset();
        if (r.cancelled) {
            // Not this image's fault: kept as an error for now (the reference get() gave may still be in use this
            // frame), and refresh() drops it so it is decoded again.
            fail(image, "image '" + image.m_name + "': decoding was cancelled");
            image.m_stale = true;
            continue;
        }
        finish(image, r.found, std::move(r.decoded));
    }
}

const ImageCache::Image& ImageCache::get(std::string_view name) {
    takeResults();
    const int frame = currentFrame();
    // The keys are canonical names (or invalid names as given), and an Image element asks with the canonical name it
    // was given back, so a name is looked up as it is first: normalizing it costs a few strings, every frame. A
    // name that isn't canonical can't equal a key (its canonical form is the only valid name that does).
    if (const auto it = m_images.find(name); it != m_images.end()) {
        it->second.m_lastUsed = frame;
        return it->second;
    }
    const auto normalized = normalizeName(name);
    const std::string_view key = normalized ? std::string_view{*normalized} : name; // invalid: not found, as given
    if (const auto it = m_images.find(key); it != m_images.end()) {
        it->second.m_lastUsed = frame;
        return it->second;
    }

    Image& image = m_images.emplace(std::string{key}, Image{}).first->second;
    image.m_lastUsed = frame;
    image.m_name = std::string{key};
    image.m_modified = m_resources.modified(key);
    ++m_decodes;
    if (m_async) {
        image.m_ticket = m_nextTicket++;
        {
            std::scoped_lock lock{m_inbox->mutex};
            ++m_inbox->inFlight;
        }
        // Shared, as std::function needs a copyable callable; it hands in its result when it runs or, when the pool
        // drops it unrun, when it is destroyed.
        image.m_wanted = std::make_shared<std::atomic<bool>>(true);
        auto job = std::make_shared<DecodeJob>(m_inbox, m_resources, image.m_name, image.m_ticket, image.m_wanted);
        runInBackground([job = std::move(job)](std::stop_token stop) { job->run(stop); }, BackgroundLane::Decodes);
        return image;
    }
    const auto bytes = m_resources.read(key);
    finish(image, bytes.has_value(), bytes ? decodeImage(bytes->bytes()) : DecodedImage{});
    return image;
}

bool ImageCache::contains(std::string_view name) const {
    if (m_images.contains(name)) return true; // canonical already (see get())
    const auto normalized = normalizeName(name);
    return m_images.find(normalized ? std::string_view{*normalized} : name) != m_images.end();
}

std::vector<std::string> ImageCache::names() const {
    std::vector<std::string> out;
    out.reserve(m_images.size());
    for (const auto& [name, image] : m_images)
        out.push_back(name);
    return out;
}

std::size_t ImageCache::decodeCount() const { return m_decodes; }

bool ImageCache::async() const { return m_async; }
void ImageCache::setAsync(bool async) { m_async = async; }

std::size_t ImageCache::pendingCount() const {
    std::scoped_lock lock{m_inbox->mutex};
    return m_inbox->inFlight + m_inbox->results.size();
}

bool ImageCache::waitForPending(std::chrono::milliseconds timeout) {
    {
        std::unique_lock lock{m_inbox->mutex};
        m_inbox->finished.wait_for(lock, timeout, [&] { return m_inbox->inFlight == 0; });
    }
    takeResults();
    return pendingCount() == 0;
}

std::size_t ImageCache::budget() const { return m_budget; }
void ImageCache::setBudget(std::size_t bytes) { m_budget = bytes; }
std::size_t ImageCache::bytes() const { return m_bytes; }

ImageCache::Images::iterator ImageCache::drop(Images::iterator it) {
    Image& image = it->second;
    m_bytes -= image.m_bytes;
    if (failed(image)) --m_failures;
    if (image.m_wanted) image.m_wanted->store(false, std::memory_order_relaxed); // a decode not started yet skips
    release(std::move(image.m_texture));
    return m_images.erase(it);
}

std::size_t ImageCache::refresh() {
    std::size_t dropped = 0;
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (!it->second.m_stale && m_resources.modified(it->first) == it->second.m_modified) {
            ++it;
            continue;
        }
        it = drop(it);
        ++dropped;
    }
    return dropped;
}

std::size_t ImageCache::refresh(const std::map<std::string, std::optional<ResourceTime>>& times) {
    std::size_t dropped = 0;
    for (auto it = m_images.begin(); it != m_images.end();) {
        const auto time = times.find(it->first);
        if (!it->second.m_stale && (time == times.end() || time->second == it->second.m_modified)) {
            ++it;
            continue;
        }
        it = drop(it);
        ++dropped;
    }
    return dropped;
}

void ImageCache::clear() {
    for (auto it = m_images.begin(); it != m_images.end();)
        it = drop(it);
    m_bytes = 0;
    m_failures = 0;
}

void ImageCache::collect() {
    takeResults();
    freeUploadedPixels();
    std::erase_if(m_released, [this](const Texture& texture) {
        if (texture->Status != ImTextureStatus_Destroyed) return false;
        unregister(texture.get());
        return true;
    });
    evict();
    // A released texture is destroyed by the backend only in a frame that comes after the release (NewFrame marks it,
    // the backend destroys it while drawing): ask for one, or it would wait for the next input.
    if (!m_released.empty()) requestFrame();
}

void ImageCache::evict() {
    // Drawn before the previous frame: no draw list still to be drawn refers to it. Least recently drawn first; among
    // equals, the map's (name) order.
    const int keepFrom = currentFrame() - 1;
    const auto leastRecent = [&](auto&& pick) {
        std::vector<Images::iterator> out;
        for (auto it = m_images.begin(); it != m_images.end(); ++it)
            if (pick(it->second) && it->second.m_lastUsed < keepFrom) out.push_back(it);
        std::ranges::stable_sort(out, {}, [](Images::iterator it) { return it->second.m_lastUsed; });
        return out;
    };

    if (m_budget != 0 && m_bytes > m_budget)
        for (const auto& it : leastRecent([](const Image& image) { return image.m_bytes > 0; })) {
            if (m_bytes <= m_budget) break;
            drop(it);
        }

    // Failures cost no pixels, but each is a name and a message: a script making up names (a missing frame of an
    // animation, a user's input) would grow the cache for ever.
    // m_failures is kept up to date as images fail and go, so the walk happens only when there are too many.
    if (m_failures > maxFailures)
        for (const auto& it : leastRecent([](const Image& image) { return failed(image); })) {
            if (m_failures <= maxFailures) break;
            drop(it); // counts it out of m_failures
        }
}

void ImageCache::release(Texture texture) {
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
