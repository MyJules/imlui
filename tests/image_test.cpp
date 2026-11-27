// Tests for images: decoding (imlui.image.decode), sizes and fill modes (imlui.image.fill_mode), the ImageCache
// (hits, misses, errors, reloading changed files, textures, the byte budget) and the Image element through the headless
// harness, with `app:/` on tests/data or on a temporary folder.
#include <gtest/gtest.h>

import std;

import imgui;
import imlui.image.decode;
import imlui.image.fill_mode;
import imlui.image.image_cache;
import imlui.lua.errors;
import imlui.resources;
import imlui.resources.directory_mount;
import imlui.theme.dpi;
import imlui.ui.background;
import imlui.ui.frame_request;
import imlui.ui.main_thread;
import imlui.ui.elements.image;
import imlui.test.headless_frame;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

/// A copy of the bytes of tests/data/`file`.
std::vector<std::byte> testFile(std::string_view file) {
    static DirectoryMount data{IMLUI_TEST_DATA_DIR};
    const auto bytes = data.read(file);
    EXPECT_TRUE(bytes) << file;
    return bytes ? std::vector<std::byte>{bytes->bytes().begin(), bytes->bytes().end()} : std::vector<std::byte>{};
}

std::string_view asText(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// RGBA of pixel (x, y).
std::vector<int> pixel(const DecodedImage& image, int x, int y) {
    const auto i = static_cast<std::size_t>((y * image.width + x) * 4);
    return {image.rgba[i], image.rgba[i + 1], image.rgba[i + 2], image.rgba[i + 3]};
}

void expectNear(ImVec2 actual, ImVec2 expected, std::string_view what = {}) {
    EXPECT_NEAR(actual.x, expected.x, 0.01f) << what;
    EXPECT_NEAR(actual.y, expected.y, 0.01f) << what;
}

bool hasError(std::string_view part) {
    for (const auto& e : luaui::errors())
        if (e.find(part) != std::string::npos) return true;
    return false;
}

std::size_t countErrors(std::string_view part) {
    std::size_t n = 0;
    for (const auto& e : luaui::errors())
        if (e.find(part) != std::string::npos) ++n;
    return n;
}

/// The start of a PNG declaring a `width` x `height` RGBA image: the signature, the IHDR chunk and an empty IDAT
/// chunk (stb_image's header scan reads up to the first IDAT). Enough for the size check, which reads the header
/// only; the decoder fails on it (no pixel data).
std::string pngHeader(std::uint32_t width, std::uint32_t height) {
    std::string png = "\x89PNG\r\n\x1a\n";
    const auto be32 = [&png](std::uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8)
            png += static_cast<char>((v >> shift) & 0xFF);
    };
    be32(13);
    png += "IHDR";
    be32(width);
    be32(height);
    png += std::string{"\x08\x06\x00\x00\x00", 5}; // 8 bits, RGBA, deflate, adaptive filters, not interlaced
    be32(0); // the CRC: stb_image doesn't check it
    be32(0);
    png += "IDAT";
    be32(0);
    return png;
}

} // namespace

// ------------------------------------------------------------------ decoding

TEST(ImageDecode, DecodesPngPixelsExactly) {
    const DecodedImage image = decodeImage(testFile("rgba_4x2.png"));
    ASSERT_TRUE(image.ok()) << image.error;
    EXPECT_EQ(image.width, 4);
    EXPECT_EQ(image.height, 2);
    ASSERT_EQ(image.rgba.size(), 4u * 2u * 4u);
    EXPECT_EQ(pixel(image, 0, 0), (std::vector{255, 0, 0, 255}));
    EXPECT_EQ(pixel(image, 2, 0), (std::vector{0, 0, 255, 255}));
    EXPECT_EQ(pixel(image, 1, 1), (std::vector{255, 0, 0, 128})) << "straight (not premultiplied) alpha";
    EXPECT_EQ(pixel(image, 2, 1)[3], 0);

    // The pixels are the decoder's buffer, not a copy of it, and moving the image keeps them in place.
    EXPECT_EQ(image.rgba.data(), image.pixels.get());
    DecodedImage moved = decodeImage(testFile("rgba_4x2.png"));
    const std::uint8_t* first = moved.rgba.data();
    const DecodedImage taken = std::move(moved);
    EXPECT_EQ(taken.rgba.data(), first);
    EXPECT_EQ(pixel(taken, 0, 0), (std::vector{255, 0, 0, 255}));
}

TEST(ImageDecode, DecodesJpegToOpaqueRgba) {
    const DecodedImage image = decodeImage(testFile("orange_8x8.jpg"));
    ASSERT_TRUE(image.ok()) << image.error;
    EXPECT_EQ(image.width, 8);
    EXPECT_EQ(image.height, 8);
    const auto p = pixel(image, 3, 5);
    EXPECT_NEAR(p[0], 200, 8);
    EXPECT_NEAR(p[1], 100, 8);
    EXPECT_NEAR(p[2], 50, 8);
    EXPECT_EQ(p[3], 255);
}

TEST(ImageDecode, ReportsUndecodableData) {
    const std::string text = "not an image";
    const DecodedImage garbage = decodeImage(std::as_bytes(std::span{text.data(), text.size()}));
    EXPECT_FALSE(garbage.ok());
    EXPECT_NE(garbage.error.find("not a PNG or JPEG"), std::string::npos) << garbage.error;
    EXPECT_TRUE(garbage.rgba.empty());

    EXPECT_EQ(decodeImage({}).error, "empty file");

    // A truncated PNG: the header is fine, the data isn't.
    const auto png = testFile("rgba_4x2.png");
    EXPECT_FALSE(decodeImage(std::span{png}.first(40)).ok());
}

// A flat 16384 x 16384 PNG is about 1 MiB and would decode to 1 GiB (and its texture take as much again, where an
// allocation failure aborts): an image over maxImageBytes is rejected from its header, before decoding.
TEST(ImageDecode, RejectsImagesTooLargeFromTheHeader) {
    static_assert(maxImageBytes == std::size_t{8192} * 8192 * 4);
    for (const auto [w, h] : {std::pair{16384u, 16384u}, std::pair{8192u, 8193u}, std::pair{16384u, 4097u}}) {
        const std::string png = pngHeader(w, h);
        const DecodedImage image = decodeImage(std::as_bytes(std::span{png.data(), png.size()}));
        EXPECT_FALSE(image.ok()) << w << " x " << h;
        EXPECT_EQ(image.error, std::format("too large ({} x {} pixels; at most 256 MiB of pixels are decoded)", w, h));
        EXPECT_TRUE(image.rgba.empty());
        EXPECT_EQ(image.width, 0);
    }
    // At the limit the decoder runs (and fails here: the file has no pixel data).
    const std::string atLimit = pngHeader(8192, 8192);
    const DecodedImage image = decodeImage(std::as_bytes(std::span{atLimit.data(), atLimit.size()}));
    EXPECT_FALSE(image.ok());
    EXPECT_TRUE(image.error.starts_with("not a PNG or JPEG image")) << image.error;
}

namespace {

/// The start of a JPEG declaring a `width` x `height` image with 3 components, after an APP0 segment: SOI, APP0, and a
/// frame header (SOF0 baseline, or SOF2 progressive). Enough for the size check; the decoder fails on it.
std::string jpegHeader(std::uint16_t width, std::uint16_t height, bool progressive) {
    std::string jpeg{"\xFF\xD8\xFF\xE0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00", 20};
    jpeg += progressive ? "\xFF\xC2" : "\xFF\xC0";
    jpeg += std::string{"\x00\x11\x08", 3}; // length 17, 8 bits
    for (const std::uint16_t v : {height, width}) {
        jpeg += static_cast<char>(v >> 8);
        jpeg += static_cast<char>(v & 0xFF);
    }
    jpeg += std::string{"\x03\x01\x11\x00\x02\x11\x00\x03\x11\x00", 10}; // 3 components, 1x1, table 0
    return jpeg;
}

} // namespace

// stb_image keeps every coefficient of a progressive JPEG until its last scan, about twice a baseline JPEG's peak
// memory, so a progressive JPEG gets half the pixels (maxProgressiveJpegBytes), told apart by its frame header.
TEST(ImageDecode, AllowsProgressiveJpegsHalfThePixels) {
    static_assert(maxProgressiveJpegBytes == maxImageBytes / 2);
    const std::string progressive = jpegHeader(6000, 6000, true); // 137 MiB of pixels
    DecodedImage image = decodeImage(std::as_bytes(std::span{progressive.data(), progressive.size()}));
    EXPECT_EQ(image.error, "too large (6000 x 6000 pixels; at most 128 MiB of pixels are decoded from a progressive "
                           "JPEG)");
    const std::string baseline = jpegHeader(6000, 6000, false);
    image = decodeImage(std::as_bytes(std::span{baseline.data(), baseline.size()}));
    EXPECT_TRUE(image.error.starts_with("not a PNG or JPEG image")) << "decoded (and failed: no data): " << image.error;
    const std::string small = jpegHeader(4000, 4000, true);
    image = decodeImage(std::as_bytes(std::span{small.data(), small.size()}));
    EXPECT_TRUE(image.error.starts_with("not a PNG or JPEG image")) << image.error;
}

// ------------------------------------------------------------------ sizes and fill modes

TEST(ImageFill, SizesFromNaturalSizeScaleAndAspect) {
    const ImVec2 natural{200, 100};
    expectNear(imageSize(natural, 1.0f, 0, 0), {200, 100});
    expectNear(imageSize(natural, 1.5f, 0, 0), {300, 150}, "natural size x the DPI scale");
    expectNear(imageSize(natural, 1.5f, 100, 0), {100, 50}, "width only: the height keeps the aspect ratio");
    expectNear(imageSize(natural, 1.5f, 0, 30), {60, 30}, "height only");
    expectNear(imageSize(natural, 1.5f, 10, 20), {10, 20}, "both set: as given");
    expectNear(imageSize({0, 0}, 2.0f, 0, 0), {0, 0}, "no image yet");
    expectNear(imageSize({0, 0}, 2.0f, 40, 0), {40, 0});
}

TEST(ImageFill, PlacesQuadsPerFillMode) {
    const ImVec2 natural{200, 100}; // 2:1
    const ImVec2 pos{10, 20};
    const ImVec2 box{100, 100};

    auto quads = placeImage(pos, box, natural, 1.0f, FillMode::Stretch);
    ASSERT_EQ(quads.size(), 1u);
    expectNear(quads[0].min, {10, 20});
    expectNear(quads[0].max, {110, 120});
    expectNear(quads[0].uvMin, {0, 0});
    expectNear(quads[0].uvMax, {1, 1});

    quads = placeImage(pos, box, natural, 1.0f, FillMode::Fit); // 100 x 50, centered vertically
    ASSERT_EQ(quads.size(), 1u);
    expectNear(quads[0].min, {10, 45});
    expectNear(quads[0].max, {110, 95});
    expectNear(quads[0].uvMax, {1, 1});

    quads = placeImage(pos, box, natural, 1.0f, FillMode::Crop); // 200 x 100 scaled to cover: the middle half
    ASSERT_EQ(quads.size(), 1u);
    expectNear(quads[0].min, {10, 20});
    expectNear(quads[0].max, {110, 120});
    expectNear(quads[0].uvMin, {0.25f, 0});
    expectNear(quads[0].uvMax, {0.75f, 1});

    // Tiles of 40 x 20 px (natural 20 x 10 at scale 2) in 100 x 30: 3 columns (the last half) x 2 rows (the last
    // half).
    quads = placeImage(pos, {100, 30}, {20, 10}, 2.0f, FillMode::Tile);
    ASSERT_EQ(quads.size(), 6u);
    expectNear(quads[0].min, {10, 20});
    expectNear(quads[0].max, {50, 40});
    expectNear(quads[2].min, {90, 20});
    expectNear(quads[2].max, {110, 40});
    expectNear(quads[2].uvMax, {0.5f, 1});
    expectNear(quads[5].max, {110, 50});
    expectNear(quads[5].uvMax, {0.5f, 0.5f});

    EXPECT_TRUE(placeImage(pos, {0, 10}, natural, 1.0f, FillMode::Fit).empty()) << "nothing to draw into";
}

// ------------------------------------------------------------------ the cache

TEST(ImageCache, DecodesOncePerNameAndRegistersATexture) {
    test::HeadlessFrame h; // an ImGui context for the textures
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(IMLUI_TEST_DATA_DIR));
    ImageCache& cache = h.images();

    const auto& first = cache.get("app:/rgba_4x2.png");
    ASSERT_TRUE(first.ready()) << first.error();
    EXPECT_EQ(first.width(), 4);
    EXPECT_EQ(first.height(), 2);
    EXPECT_EQ(cache.decodeCount(), 1u);
    const ImTextureRef texture = first.texture();
    ASSERT_NE(texture._TexData, nullptr);
    EXPECT_EQ(texture._TexData->Status, ImTextureStatus_WantCreate) << "the backend uploads it";
    EXPECT_EQ(texture._TexData->Width, 4);

    const auto& again = cache.get("app:/rgba_4x2.png");
    EXPECT_EQ(&again, &first) << "a hit";
    EXPECT_EQ(cache.decodeCount(), 1u);

    EXPECT_TRUE(cache.get("app:/orange_8x8.jpg").ready());
    EXPECT_EQ(cache.decodeCount(), 2u) << "another name is a miss";
    EXPECT_EQ(cache.names(), (std::vector<std::string>{"app:/orange_8x8.jpg", "app:/rgba_4x2.png"}));

    cache.clear();
    EXPECT_FALSE(cache.contains("app:/rgba_4x2.png"));
    EXPECT_TRUE(cache.get("app:/rgba_4x2.png").ready());
    EXPECT_EQ(cache.decodeCount(), 3u);
}

TEST(ImageCache, CachesFailuresWithAClearMessage) {
    test::TempDir dir;
    dir.write("broken.png", "this is not a png");
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(dir.path()));
    ImageCache& cache = h.images();

    const auto& missing = cache.get("app:/missing.png");
    EXPECT_FALSE(missing.ready());
    EXPECT_EQ(missing.error(), "image 'app:/missing.png' not found");
    EXPECT_EQ(missing.texture()._TexData, nullptr);
    EXPECT_EQ(missing.width(), 0);

    const auto& broken = cache.get("app:/broken.png");
    EXPECT_FALSE(broken.ready());
    EXPECT_NE(broken.error().find("image 'app:/broken.png': not a PNG or JPEG image"), std::string::npos)
        << broken.error();

    dir.write("huge.png", pngHeader(16384, 16384));
    const auto& huge = cache.get("app:/huge.png");
    EXPECT_FALSE(huge.ready());
    EXPECT_EQ(huge.error(), "image 'app:/huge.png': too large (16384 x 16384 pixels; at most 256 MiB of pixels are "
                            "decoded)");
    EXPECT_EQ(cache.bytes(), 0u);

    cache.get("app:/missing.png");
    cache.get("app:/broken.png");
    cache.get("app:/huge.png");
    EXPECT_EQ(cache.decodeCount(), 3u) << "a failure is cached too";
    EXPECT_EQ(cache.refresh(), 0u) << "nothing changed";
}

TEST(ImageCache, RefreshDropsImagesWhoseFileChanged) {
    test::TempDir dir;
    dir.write("pic.png", asText(testFile("rgba_4x2.png")));
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(dir.path()));
    ImageCache& cache = h.images();

    EXPECT_EQ(cache.get("app:/pic.png").width(), 4);
    EXPECT_FALSE(cache.get("app:/later.png").ready());
    EXPECT_EQ(cache.refresh(), 0u);

    dir.write("pic.png", asText(testFile("orange_8x8.jpg"))); // the content decides the format, not the name
    dir.touch("pic.png", 10);
    dir.write("later.png", asText(testFile("rgba_4x2.png")));
    EXPECT_EQ(cache.refresh(), 2u) << "a changed file, and a missing one that appeared";
    EXPECT_FALSE(cache.contains("app:/pic.png"));

    const auto& reloaded = cache.get("app:/pic.png");
    ASSERT_TRUE(reloaded.ready()) << reloaded.error();
    EXPECT_EQ(reloaded.width(), 8);
    EXPECT_TRUE(cache.get("app:/later.png").ready());
    EXPECT_EQ(cache.decodeCount(), 4u);
}

TEST(ImageCache, KeepsUploadedTexturesUntilTheBackendDestroyedThem) {
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(IMLUI_TEST_DATA_DIR));
    ImageCache& cache = h.images();
    ImTextureData* texture = cache.get("app:/rgba_4x2.png").texture()._TexData;
    ASSERT_NE(texture, nullptr);
    // Pretend a backend uploaded it.
    texture->SetTexID(ImTextureID{42});
    texture->SetStatus(ImTextureStatus_OK);

    cache.clear();
    EXPECT_TRUE(texture->WantDestroyNextFrame) << "handed to the backend";
    cache.collect();
    EXPECT_EQ(texture->Status, ImTextureStatus_OK) << "still alive: not destroyed yet";

    // The backend destroys it.
    texture->SetTexID(imgui::invalidTextureId);
    texture->SetStatus(ImTextureStatus_Destroyed);
    cache.collect(); // deletes it (ASan would flag a leak or a use after free)
}

// The byte budget: after a frame, the images drawn least recently go until the cache fits, never one drawn in the
// current or the previous frame; an evicted image is decoded again when it's next drawn. (The harness collects after
// each frame, like the Engine; an image fetched between frames counts as drawn in the frame before.)
TEST(ImageCache, EvictsTheLeastRecentlyDrawnImagesOverItsBudget) {
    test::TempDir dir;
    for (const char* name : {"a.png", "b.png", "c.png"})
        dir.write(name, asText(testFile("rgba_4x2.png"))); // 4 x 2 x 4 = 32 bytes each
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(dir.path()));
    ImageCache& cache = h.images();
    EXPECT_EQ(cache.budget(), ImageCache::defaultBudget);
    EXPECT_EQ(ImageCache::defaultBudget, std::size_t{256} << 20);
    cache.setBudget(64);
    const auto frame = [&] { h.renderFrame("nothing"); };

    frame();
    EXPECT_TRUE(cache.get("app:/a.png").ready());
    frame();
    EXPECT_TRUE(cache.get("app:/b.png").ready());
    frame();
    EXPECT_TRUE(cache.get("app:/c.png").ready());
    EXPECT_EQ(cache.bytes(), 96u);
    frame(); // over budget: `a` (drawn longest ago) goes, `b` is enough to keep
    EXPECT_FALSE(cache.contains("app:/a.png"));
    EXPECT_TRUE(cache.contains("app:/b.png"));
    EXPECT_TRUE(cache.contains("app:/c.png"));
    EXPECT_EQ(cache.bytes(), 64u);
    EXPECT_EQ(cache.decodeCount(), 3u);

    // Drawn in this frame: kept over budget, until two frames later.
    cache.setBudget(1);
    cache.get("app:/b.png");
    cache.get("app:/c.png");
    frame();
    EXPECT_EQ(cache.names(), (std::vector<std::string>{"app:/b.png", "app:/c.png"})) << "drawn in the previous frame";
    frame();
    EXPECT_TRUE(cache.names().empty());
    EXPECT_EQ(cache.bytes(), 0u);

    // Evicted images decode again.
    EXPECT_TRUE(cache.get("app:/a.png").ready());
    EXPECT_EQ(cache.decodeCount(), 4u);
}

// A failure costs no bytes: it stays cached, so a missing file is reported once, whatever the budget.
TEST(ImageCache, KeepsFailuresAndAnUnlimitedBudget) {
    test::TempDir dir;
    for (const char* name : {"a.png", "b.png"})
        dir.write(name, asText(testFile("rgba_4x2.png")));
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(dir.path()));
    ImageCache& cache = h.images();
    const auto frame = [&] { h.renderFrame("nothing"); };

    cache.setBudget(1);
    EXPECT_FALSE(cache.get("app:/missing.png").ready());
    EXPECT_EQ(cache.bytes(), 0u);
    for (int i = 0; i < 3; ++i)
        frame();
    EXPECT_TRUE(cache.contains("app:/missing.png"));
    cache.get("app:/missing.png");
    EXPECT_EQ(cache.decodeCount(), 1u);

    // 0: no limit.
    cache.setBudget(0);
    cache.get("app:/a.png");
    cache.get("app:/b.png");
    for (int i = 0; i < 3; ++i)
        frame();
    EXPECT_EQ(cache.bytes(), 64u);
    EXPECT_EQ(cache.names().size(), 3u);
}

// Failures are kept up to maxFailures: beyond that, those asked for least recently go (never one asked for in the
// current or the previous frame), so made-up names can't grow the cache without bound.
TEST(ImageCache, KeepsAtMostMaxFailures) {
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(IMLUI_TEST_DATA_DIR));
    ImageCache& cache = h.images();
    const auto frame = [&] { h.renderFrame("nothing"); };
    const std::size_t extra = 10;
    for (std::size_t i = 0; i < ImageCache::maxFailures + extra; ++i)
        EXPECT_FALSE(cache.get(std::format("app:/missing/{:04}.png", i)).ready());
    EXPECT_TRUE(cache.get("app:/rgba_4x2.png").ready());
    frame();
    EXPECT_EQ(cache.names().size(), ImageCache::maxFailures + extra + 1) << "asked for in the previous frame";
    // Asked for again now: these stay; the others, asked for longest ago, go first.
    for (std::size_t i = ImageCache::maxFailures; i < ImageCache::maxFailures + extra; ++i)
        cache.get(std::format("app:/missing/{:04}.png", i));
    frame();
    frame();
    EXPECT_EQ(cache.names().size(), ImageCache::maxFailures + 1);
    EXPECT_FALSE(cache.contains("app:/missing/0000.png"));
    EXPECT_FALSE(cache.contains(std::format("app:/missing/{:04}.png", extra - 1)));
    EXPECT_TRUE(cache.contains(std::format("app:/missing/{:04}.png", extra)));
    EXPECT_TRUE(cache.contains(std::format("app:/missing/{:04}.png", ImageCache::maxFailures + extra - 1)));
    EXPECT_TRUE(cache.contains("app:/rgba_4x2.png")) << "images aren't failures";
}

// Equivalent names are one image: get() and contains() normalize them.
TEST(ImageCache, NormalizesNames) {
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(IMLUI_TEST_DATA_DIR));
    ImageCache& cache = h.images();
    const auto& image = cache.get("app:/./rgba_4x2.png");
    ASSERT_TRUE(image.ready()) << image.error();
    EXPECT_EQ(image.name(), "app:/rgba_4x2.png");
    EXPECT_EQ(&cache.get("app://x/../rgba_4x2.png"), &image);
    EXPECT_EQ(&cache.get("app:/rgba_4x2.png"), &image);
    EXPECT_TRUE(cache.contains("app:/x/../rgba_4x2.png"));
    EXPECT_EQ(cache.decodeCount(), 1u);
    EXPECT_EQ(cache.names(), (std::vector<std::string>{"app:/rgba_4x2.png"}));
    EXPECT_EQ(cache.get("not a name").error(), "image 'not a name' not found");
}

// The texture takes the decoder's pixel buffer: the pixels are the decoded ones, set up as ImTextureData::Create()
// would (and freed the decoder's way when the texture goes: ASan would flag a mismatch or a leak).
TEST(ImageCache, TexturesTakeTheDecodedPixels) {
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(IMLUI_TEST_DATA_DIR));
    ImageCache& cache = h.images();
    ImTextureData* texture = cache.get("app:/rgba_4x2.png").texture()._TexData;
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->Format, ImTextureFormat_RGBA32);
    EXPECT_EQ(texture->BytesPerPixel, 4);
    EXPECT_EQ(texture->GetSizeInBytes(), 4 * 2 * 4);
    EXPECT_TRUE(texture->UseColors);
    const DecodedImage decoded = decodeImage(testFile("rgba_4x2.png"));
    ASSERT_TRUE(decoded.ok());
    EXPECT_TRUE(std::ranges::equal(std::span{texture->Pixels, decoded.rgba.size()}, decoded.rgba));
    cache.clear();
}

// With setAsync(true), get() returns a loading image at once and the decode runs on the background pool; its result is
// taken by a later get() or collect() (or waitForPending()), and a frame is asked for through the main-thread queue.
TEST(ImageCache, DecodesInTheBackground) {
    test::TempDir dir;
    dir.write("pic.png", asText(testFile("rgba_4x2.png")));
    dir.write("other.png", asText(testFile("orange_8x8.jpg")));
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(dir.path()));
    ImageCache& cache = h.images();
    EXPECT_FALSE(cache.async());
    cache.setAsync(true);
    clearPosted();
    clearFrameRequests();

    const auto& pic = cache.get("app:/pic.png");
    EXPECT_TRUE(pic.loading());
    EXPECT_FALSE(pic.ready());
    EXPECT_EQ(pic.error(), "");
    EXPECT_EQ(pic.texture()._TexData, nullptr);
    EXPECT_EQ(cache.bytes(), 0u);
    const auto& missing = cache.get("app:/missing.png");
    EXPECT_TRUE(missing.loading());
    EXPECT_EQ(cache.decodeCount(), 2u);

    ASSERT_TRUE(cache.waitForPending());
    EXPECT_EQ(cache.pendingCount(), 0u);
    EXPECT_EQ(&cache.get("app:/pic.png"), &pic) << "the same entry, now ready";
    EXPECT_FALSE(pic.loading());
    ASSERT_TRUE(pic.ready()) << pic.error();
    EXPECT_EQ(pic.width(), 4);
    EXPECT_EQ(pic.texture()._TexData->Status, ImTextureStatus_WantCreate);
    EXPECT_EQ(cache.bytes(), 32u);
    EXPECT_EQ(missing.error(), "image 'app:/missing.png' not found");
    EXPECT_EQ(cache.decodeCount(), 2u);
    // Each finished decode asked for a frame, on the main thread.
    EXPECT_EQ(postedCount(), 2u);
    runPosted();
    EXPECT_EQ(secondsUntilFrame(), 0.0);
    clearFrameRequests();

    // A file that changes while it decodes: refresh() drops the image, and the late result is ignored.
    const auto& other = cache.get("app:/other.png");
    EXPECT_TRUE(other.loading());
    dir.write("other.png", asText(testFile("rgba_4x2.png")));
    dir.touch("other.png", 10);
    EXPECT_EQ(cache.refresh(), 1u);
    EXPECT_FALSE(cache.contains("app:/other.png"));
    ASSERT_TRUE(cache.waitForPending());
    EXPECT_FALSE(cache.contains("app:/other.png")) << "the dropped decode's result went nowhere";
    const auto& again = cache.get("app:/other.png");
    ASSERT_TRUE(cache.waitForPending());
    ASSERT_TRUE(again.ready()) << again.error();
    EXPECT_EQ(again.width(), 4);

    // collect() (after each frame) takes finished results too.
    cache.clear();
    const auto& late = cache.get("app:/pic.png");
    for (int i = 0; i < 1000 && backgroundTaskCount() > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    EXPECT_TRUE(late.loading());
    h.renderFrame("nothing");
    EXPECT_TRUE(late.ready()) << late.error();
    clearPosted();
    clearFrameRequests();
}

// A cache destroyed while its decodes run: they finish into the shared inbox and are freed with it (ASan would flag a
// use after free or a leak).
TEST(ImageCache, OutlivedByItsBackgroundDecodes) {
    test::HeadlessFrame h;
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(IMLUI_TEST_DATA_DIR));
    {
        ImageCache cache{h.resources()};
        cache.setAsync(true);
        for (const char* name : {"app:/rgba_4x2.png", "app:/orange_8x8.jpg", "app:/missing.png"})
            EXPECT_TRUE(cache.get(name).loading());
    }
    for (int i = 0; i < 200 && backgroundTaskCount() > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    clearPosted();
}

// ------------------------------------------------------------------ the Image element

namespace {

class ImageElement : public ::testing::Test {
protected:
    ImageElement() { m_h.resources().mount("app:/", std::make_shared<DirectoryMount>(IMLUI_TEST_DATA_DIR)); }

    /// Runs `code` (which sets `root`) and renders two frames.
    void show(std::string_view code) {
        ASSERT_EQ(m_h.run(code), "");
        m_h.renderFrames("root", 2);
    }
    Image* image(std::string_view name = "img") { return m_h.instance<Image>(name); }

    test::HeadlessFrame m_h{{.builtins = true}};
};

} // namespace

// ApplicationWindow's `imageCacheBudget` sets the cache's budget while the prop exists.
TEST_F(ImageElement, ApplicationWindowSetsTheCacheBudget) {
    show(R"(root = ApplicationWindow { Image { id = "img", source = "rgba_4x2.png" } })");
    EXPECT_EQ(m_h.images().budget(), ImageCache::defaultBudget) << "without the prop";
    show(R"(s = state { budget = 1000 }
            root = ApplicationWindow { imageCacheBudget = bind(s, "budget"),
                                       Image { id = "img2", source = "rgba_4x2.png" } })");
    EXPECT_EQ(m_h.images().budget(), 1000u);
    ASSERT_EQ(m_h.run("s.budget = 0"), "");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(m_h.images().budget(), 0u);
    ASSERT_EQ(m_h.run("s.budget = -5"), "");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(m_h.images().budget(), 0u) << "a negative budget reads as 0";
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ImageElement, DrawsAtItsNaturalSizeTimesTheScale) {
    show(R"(
        s = state { status = nil, calls = 0 }
        root = Column { Image { id = "img", source = "rgba_4x2.png",
                                onStatusChanged = function(self, st) s.status = st; s.calls = s.calls + 1 end } })");
    Image* img = image();
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->status(), Image::Status::Ready);
    EXPECT_EQ(img->resolvedSource(), "app:/rgba_4x2.png");
    expectNear(img->lastSize(), {4 * dpiScale(), 2 * dpiScale()});
    EXPECT_EQ(img->implicitWidth(), 4.0);
    EXPECT_EQ(img->implicitHeight(), 2.0);
    EXPECT_EQ(m_h.run(R"(
        assert(s.status == "ready", tostring(s.status))
        assert(s.calls == 1, s.calls)
        assert(img.status == "ready" and img.implicitWidth == 4 and img.implicitHeight == 2))"),
              "");
    EXPECT_TRUE(luaui::errors().empty());
    EXPECT_EQ(m_h.images().decodeCount(), 1u);
}

// With background decoding (the Engine's setting), the image is "loading" until its decode lands: no error, no
// placeholder, no size; then "ready" at its natural size, with one onStatusChanged.
TEST_F(ImageElement, IsLoadingWhileItDecodesInTheBackground) {
    m_h.images().setAsync(true);
    clearPosted();
    ASSERT_EQ(m_h.run(R"(
        s = state { seen = {} }
        root = Column { Image { id = "img", source = "rgba_4x2.png",
                                onStatusChanged = function(self, st) s.seen[#s.seen + 1] = st end } })"),
              "");
    m_h.renderFrames("root", 1);
    Image* img = image();
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->status(), Image::Status::Loading);
    EXPECT_EQ(img->implicitWidth(), 0.0);
    expectNear(img->lastSize(), {0.0f, 0.0f}, "no placeholder while loading");
    EXPECT_TRUE(luaui::errors().empty()) << "loading is not an error";

    ASSERT_TRUE(m_h.images().waitForPending());
    runPosted();
    m_h.renderFrames("root", 2);
    EXPECT_EQ(img->status(), Image::Status::Ready);
    expectNear(img->lastSize(), {4 * dpiScale(), 2 * dpiScale()});
    EXPECT_EQ(m_h.run(R"(assert(#s.seen == 1 and s.seen[1] == "ready", table.concat(s.seen, ",")))"), "");
    EXPECT_TRUE(luaui::errors().empty());
    clearPosted();
}

TEST_F(ImageElement, KeepsTheAspectRatioWhenOneSizeIsSet) {
    show(R"(root = Column {
        Image { id = "wide", source = "rgba_4x2.png", width = 40 },
        Image { id = "tall", source = "app:/rgba_4x2.png", height = 30 },
        Image { id = "both", source = "rgba_4x2.png", width = 10, height = 10, fillMode = "fit" },
    })");
    const float d = dpiScale();
    expectNear(image("wide")->lastSize(), {40 * d, 20 * d});
    expectNear(image("tall")->lastSize(), {60 * d, 30 * d});
    expectNear(image("both")->lastSize(), {10 * d, 10 * d});
    EXPECT_EQ(m_h.images().decodeCount(), 1u) << "one decode per resource name, however many elements";
}

TEST_F(ImageElement, ReportsAMissingFileOnceAndDrawsAPlaceholder) {
    show(R"(root = Column { Image { id = "img", source = "nope.png" }, Image { id = "img2", source = "nope.png" } })");
    Image* img = image();
    EXPECT_EQ(img->status(), Image::Status::Error);
    EXPECT_EQ(img->implicitWidth(), 0.0);
    expectNear(img->lastSize(), {48 * dpiScale(), 48 * dpiScale()}, "the placeholder's size");
    EXPECT_EQ(countErrors("image 'app:/nope.png' not found"), 1u);
    m_h.renderFrames("root", 3);
    EXPECT_EQ(countErrors("image 'app:/nope.png' not found"), 1u);
    EXPECT_EQ(m_h.images().decodeCount(), 1u);
}

// fillMode takes the FillMode names and QML's two aliases; Lua reads back what it wrote.
TEST_F(ImageElement, TakesTheFillModeNames) {
    show(R"(root = Column { Image { id = "img", source = "rgba_4x2.png", fillMode = "preserveAspectFit" } })");
    EXPECT_EQ(image()->fillMode(), FillMode::Fit);
    const std::pair<std::string_view, FillMode> names[] = {{"stretch", FillMode::Stretch},
                                                           {"fit", FillMode::Fit},
                                                           {"crop", FillMode::Crop},
                                                           {"tile", FillMode::Tile},
                                                           {"preserveAspectCrop", FillMode::Crop}};
    for (const auto& [name, mode] : names) {
        ASSERT_EQ(m_h.run(std::format(R"(img.fillMode = "{0}"; assert(img.fillMode == "{0}"))", name)), "");
        m_h.renderFrames("root", 1);
        EXPECT_EQ(image()->fillMode(), mode) << name;
    }
    EXPECT_TRUE(luaui::errors().empty());
}

// An unknown fillMode is reported with the names, and the image keeps its fill mode (here the default, stretch).
TEST_F(ImageElement, ReportsAnUnknownFillModeAndStretches) {
    show(R"(root = Image { id = "img", source = "rgba_4x2.png", fillMode = "cover" })");
    EXPECT_EQ(image()->status(), Image::Status::Ready);
    EXPECT_EQ(image()->fillMode(), FillMode::Stretch);
    EXPECT_TRUE(hasError(
        R"(Image.fillMode: FillMode ('stretch'|'fit'|'crop'|'tile'|'preserveAspectFit'|'preserveAspectCrop') expected, got string "cover")"));
}

// status and the onStatusChanged argument are the ImageStatus names, as strings.
TEST_F(ImageElement, StatusReachesLuaAsAString) {
    show(R"(
        seen = {}
        root = Column { Image { id = "img", source = "rgba_4x2.png",
                                onStatusChanged = function(self, st) seen[#seen + 1] = st end } })");
    ASSERT_EQ(m_h.run(R"(img.source = "nope.png")"), "");
    m_h.renderFrames("root", 2);
    ASSERT_EQ(m_h.run(R"(img.source = "")"), "");
    m_h.renderFrames("root", 2);
    EXPECT_EQ(image()->status(), Image::Status::None);
    EXPECT_EQ(m_h.run(R"(
        assert(img.status == "none", tostring(img.status))
        local all = table.concat(seen, ",")
        assert(all == "ready,error,none", all))"),
              "");
}

TEST_F(ImageElement, WithoutSourceTakesNoSpace) {
    show(R"(root = Column { Image { id = "img" } })");
    EXPECT_EQ(image()->status(), Image::Status::None);
    expectNear(image()->lastSize(), {0, 0});
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(ImageElement, FollowsABoundSource) {
    show(R"(
        s = state { src = "rgba_4x2.png" }
        root = Image { id = "img", source = bind(s, "src") })");
    EXPECT_EQ(image()->implicitWidth(), 4.0);
    ASSERT_EQ(m_h.run(R"(s.src = "orange_8x8.jpg")"), "");
    m_h.renderFrames("root", 1);
    EXPECT_EQ(image()->implicitWidth(), 8.0);
    EXPECT_EQ(image()->resolvedSource(), "app:/orange_8x8.jpg");
}

TEST(ImageElementSources, ResolvesRelativeNamesAgainstTheCreatingScript) {
    test::TempDir dir;
    dir.write("pages/page.lua", R"(return Column { Image { id = "img", source = "pic.png" } })");
    dir.write("pages/pic.png", asText(testFile("rgba_4x2.png")));
    test::HeadlessFrame h{{.builtins = true}};
    h.resources().mount("app:/", std::make_shared<DirectoryMount>(dir.path()));
    ASSERT_EQ(h.run(R"(
        root = ui.__load("app:/pages/page.lua")
        assert(img.__base == "app:/pages/page.lua", img.__base)
        local direct = Image { source = "x.png" }
        assert(direct.__base == "app:/pages/page.lua", "code that isn't a resource uses the main script")
    )"),
              "");
    h.renderFrames("root", 2);
    Image* img = h.instance<Image>("img");
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->resolvedSource(), "app:/pages/pic.png");
    EXPECT_EQ(img->status(), Image::Status::Ready);
}
