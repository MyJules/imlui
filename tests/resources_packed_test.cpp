// Tests for the packed resource mounts: EmbeddedMount (stored and zstd-compressed files, lazy decompression,
// concurrent reads), .pak archives (written by the imlui_pack code, read by PakMount; corrupt and truncated archives
// rejected), the override order embedded < .pak < directory, the library's own embedded files, and a release-style
// setup that runs from embedded data only (defaultConfig without a source tree, the headless harness without lua/).
#include <gtest/gtest.h>

import imlui.resources;
import imlui.resources.mount;
import imlui.resources.directory_mount;
import imlui.resources.packed_mount;
import imlui.resources.embedded_mount;
import imlui.resources.pak_format;
import imlui.resources.pak_mount;
import imlui.app.config;
import imlui.embedded.imlui;
import imlui.embedded.imlui_fonts;
import imlui.lua.errors;
import imlui.tools.resource_encoding;
import imlui.tools.resource_spec;
import imlui.tools.pack;
import imlui.test.headless_frame;
import imlui.test.memory_mount;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

std::string text(std::optional<std::span<const std::byte>> bytes) {
    if (!bytes) return "<none>";
    return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

std::span<const std::byte> bytesOf(std::string_view s) { return std::as_bytes(std::span{s.data(), s.size()}); }

/// Text that zstd compresses well (well over the 64-byte minimum).
std::string repetitive(std::string_view seed, int times) {
    std::string out;
    for (int i = 0; i < times; ++i)
        out += std::string{seed} + std::to_string(i % 7) + "\n";
    return out;
}

/// An EmbeddedMount over a blob built like the generated modules build theirs: each file encoded by the tools.
class EmbeddedFixture {
public:
    explicit EmbeddedFixture(std::vector<std::pair<std::string, std::string>> files) {
        for (const auto& [path, content] : files) {
            const auto encoded = tools::encodeResource(path, bytesOf(content));
            m_paths.push_back(path);
            m_records.push_back({{}, m_blob.size(), encoded.stored.size(), encoded.size, encoded.compressed});
            m_blob.insert(m_blob.end(), encoded.stored.begin(), encoded.stored.end());
        }
        for (std::size_t i = 0; i < m_records.size(); ++i)
            m_records[i].path = m_paths[i];
        m_mount = std::make_shared<EmbeddedMount>("test", m_blob, m_records);
    }

    [[nodiscard]] const std::shared_ptr<EmbeddedMount>& mount() const { return m_mount; }
    [[nodiscard]] const std::vector<EmbeddedMount::Record>& records() const { return m_records; }
    [[nodiscard]] std::span<const std::byte> blob() const { return m_blob; }

private:
    std::vector<std::string> m_paths;
    std::vector<std::byte> m_blob;
    std::vector<EmbeddedMount::Record> m_records;
    std::shared_ptr<EmbeddedMount> m_mount;
};

/// The bytes of a .pak archive with `files`, through the imlui_pack code path.
std::vector<std::byte> makePak(const test::TempDir& dir, std::string_view mountPoint,
                               const std::vector<std::pair<std::string, std::string>>& files) {
    for (const auto& [path, content] : files)
        dir.write("pak_src/" + path, content);
    std::string error;
    auto collected = tools::collectFiles(dir.path() / "pak_src", {}, error);
    EXPECT_TRUE(collected) << error;
    auto pak = tools::packFiles(mountPoint, *collected, error);
    EXPECT_TRUE(pak) << error;
    return pak ? *pak : std::vector<std::byte>{};
}

/// The contents of a file (a path or a std::filesystem::path).
std::string readSource(const auto& file) {
    std::ifstream in{file, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

// ------------------------------------------------------------------ encoding

TEST(ResourceEncoding, CompressesOnlyWhenItPays) {
    const std::string big = repetitive("local x = require('something')", 50);
    EXPECT_TRUE(tools::encodeResource("a.lua", bytesOf(big)).compressed);
    EXPECT_FALSE(tools::encodeResource("a.lua", bytesOf("short")).compressed); // under the minimum size
    EXPECT_FALSE(tools::encodeResource("logo.png", bytesOf(big)).compressed);  // already-compressed format
    EXPECT_FALSE(tools::encodeResource("LOGO.JPG", bytesOf(big)).compressed);
    // Random-looking data doesn't shrink by 10 %, so it's stored.
    std::string noise;
    unsigned v = 12345;
    for (int i = 0; i < 4096; ++i) {
        v = v * 1103515245U + 12345U;
        noise += static_cast<char>((v >> 16) & 0xff);
    }
    const auto stored = tools::encodeResource("noise.bin", bytesOf(noise));
    EXPECT_FALSE(stored.compressed);
    EXPECT_EQ(stored.size, noise.size());
}

// ------------------------------------------------------------------ EmbeddedMount

TEST(EmbeddedMount, ReadsStoredAndCompressedFiles) {
    const std::string script = repetitive("return Column { Text { text = 'hello' } }", 40);
    EmbeddedFixture fx{{{"main.lua", script}, {"tiny.txt", "tiny"}, {"sub/data.json", repetitive("{\"a\": 1}", 30)}}};
    const auto& mount = *fx.mount();
    ASSERT_TRUE(fx.records()[0].compressed);
    ASSERT_FALSE(fx.records()[1].compressed);

    EXPECT_EQ(text(mount.read("main.lua")), script);
    EXPECT_EQ(text(mount.read("tiny.txt")), "tiny");
    EXPECT_EQ(text(mount.read("sub/data.json")), repetitive("{\"a\": 1}", 30));
    EXPECT_FALSE(mount.read("missing.lua"));

    // A stored file is served straight from the blob (zero-copy).
    const auto tiny = mount.read("tiny.txt");
    EXPECT_EQ(tiny->data(), fx.blob().data() + fx.records()[1].offset);

    EXPECT_TRUE(mount.exists(""));
    EXPECT_TRUE(mount.exists("sub"));
    EXPECT_TRUE(mount.exists("sub/data.json"));
    EXPECT_FALSE(mount.exists("su"));
    EXPECT_EQ(mount.list(""), (std::vector<std::string>{"main.lua", "sub/", "tiny.txt"}));
    EXPECT_EQ(mount.list("sub"), (std::vector<std::string>{"data.json"}));
    EXPECT_FALSE(mount.modified("main.lua"));
    EXPECT_EQ(mount.origin(), "embedded 'test'");
}

TEST(EmbeddedMount, DecompressesOnFirstReadAndCaches) {
    EmbeddedFixture fx{{{"a.lua", repetitive("aaaa", 100)}, {"b.lua", repetitive("bbbb", 100)}}};
    const auto& mount = *fx.mount();
    EXPECT_EQ(mount.decompressedCount(), 0U);
    const auto first = mount.read("a.lua");
    EXPECT_EQ(mount.decompressedCount(), 1U);
    const auto again = mount.read("a.lua");
    EXPECT_EQ(first->data(), again->data()); // the cached buffer, not a second decompression
    EXPECT_EQ(mount.decompressedCount(), 1U);
}

TEST(EmbeddedMount, ConcurrentReadsShareOneBuffer) {
    const std::string content = repetitive("concurrent", 500);
    EmbeddedFixture fx{{{"big.txt", content}}};
    std::vector<const std::byte*> seen(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < seen.size(); ++i)
        threads.emplace_back([&, i] {
            for (int n = 0; n < 50; ++n) {
                const auto bytes = fx.mount()->read("big.txt");
                if (!bytes || text(bytes) != content) return;
                seen[i] = bytes->data();
            }
        });
    for (auto& t : threads)
        t.join();
    for (const auto* p : seen)
        EXPECT_EQ(p, seen[0]);
    EXPECT_NE(seen[0], nullptr);
}

TEST(EmbeddedMount, RejectsBadPathsCorruptDataAndRecordsOutsideTheBlob) {
    EmbeddedFixture fx{{{"a/b.lua", repetitive("x = 1", 50)}}};
    EXPECT_FALSE(fx.mount()->read("a//b.lua"));
    EXPECT_FALSE(fx.mount()->read("a/./b.lua"));
    EXPECT_FALSE(fx.mount()->read("/a/b.lua"));

    // A compressed record over garbage, and one that runs past the end of the blob.
    std::vector<std::byte> garbage(100, std::byte{0x5a});
    const EmbeddedMount::Record records[] = {
        {"bad.lua", 0, 100, 400, true}, {"out.lua", 50, 100, 100, false}, {"../up.lua", 0, 10, 10, false}};
    const EmbeddedMount mount{"broken", garbage, records};
    EXPECT_FALSE(mount.read("bad.lua"));
    EXPECT_FALSE(mount.exists("out.lua"));
    EXPECT_EQ(mount.list(""), (std::vector<std::string>{"bad.lua"}));
}

// ------------------------------------------------------------------ .pak

TEST(PakMount, RoundTripsThroughTheWriter) {
    test::TempDir dir;
    const std::string script = repetitive("return Text { text = 'pak' }", 40);
    const auto bytes =
        makePak(dir, "app:/", {{"main.lua", script}, {"img/logo.png", repetitive("PNG", 40)}, {"small.txt", "small"}});
    {
        std::ofstream out{dir.path() / "data.pak", std::ios::binary};
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::string error;
    const auto pak = PakMount::open(dir.path() / "data.pak", &error);
    ASSERT_TRUE(pak) << error;
    EXPECT_EQ(pak->mountPoint(), "app:/");
    EXPECT_TRUE(pak->origin().starts_with("pak '")) << pak->origin();
    EXPECT_TRUE(pak->origin().ends_with("data.pak'")) << pak->origin();
    EXPECT_EQ(text(pak->read("main.lua")), script);
    EXPECT_EQ(text(pak->read("img/logo.png")), repetitive("PNG", 40));
    EXPECT_EQ(text(pak->read("small.txt")), "small");
    EXPECT_EQ(pak->list(""), (std::vector<std::string>{"img/", "main.lua", "small.txt"}));
    EXPECT_FALSE(pak->modified("main.lua"));

    // The compression rule is the embedded one: the script is compressed, the PNG and the small file aren't.
    for (const auto& e : pak->entries())
        EXPECT_EQ(e.compressed, e.path == "main.lua") << e.path;

    // Mounted through defaultConfig's helper, at the archive's own mount point.
    Resources res;
    EXPECT_EQ(mountPaks(res, dir.path()), 1);
    EXPECT_EQ(text(res.read("app:/main.lua")), script);
}

TEST(PakMount, RejectsCorruptAndTruncatedArchives) {
    test::TempDir dir;
    const auto good = makePak(dir, "imlui:/fonts/", {{"a.txt", repetitive("aaa", 60)}, {"b.txt", "bbb"}});
    std::string error;
    ASSERT_TRUE(PakMount::fromBytes(good, "good", &error)) << error;

    // Every truncation is rejected cleanly, with a reason.
    for (std::size_t size = 0; size < good.size(); ++size) {
        error.clear();
        EXPECT_FALSE(
            PakMount::fromBytes({good.begin(), good.begin() + static_cast<std::ptrdiff_t>(size)}, "cut", &error))
            << size;
        EXPECT_FALSE(error.empty()) << size;
    }

    auto bad = good;
    bad[0] = std::byte{'X'};
    EXPECT_FALSE(PakMount::fromBytes(bad, "magic", &error));
    EXPECT_NE(error.find("magic"), std::string::npos) << error;

    bad = good;
    bad[8] = std::byte{2}; // version
    EXPECT_FALSE(PakMount::fromBytes(bad, "version", &error));
    EXPECT_NE(error.find("version"), std::string::npos) << error;

    // A path that would leave the mount, and an entry pointing past the end.
    const std::string payload = "xyz";
    const PakFile escaping[] = {{"../x.txt", bytesOf(payload), 3, false}};
    EXPECT_FALSE(PakMount::fromBytes(writePak("app:/", escaping), "escape", &error));
    EXPECT_NE(error.find("invalid path"), std::string::npos) << error;
    const PakFile badPoint[] = {{"x.txt", bytesOf(payload), 3, false}};
    EXPECT_FALSE(PakMount::fromBytes(writePak("C:/x/", badPoint), "point", &error));

    // Compressed data that doesn't decompress: the archive opens, the file reads as missing.
    const std::string notZstd = repetitive("not zstd", 20);
    const PakFile corrupt[] = {{"c.txt", bytesOf(notZstd), 500, true}};
    const auto pak = PakMount::fromBytes(writePak("app:/", corrupt), "corrupt", &error);
    ASSERT_TRUE(pak) << error;
    EXPECT_TRUE(pak->exists("c.txt"));
    EXPECT_FALSE(pak->read("c.txt"));

    // defaultConfig's helper skips a bad archive and mounts the rest.
    dir.write("bad.pak", "not an archive");
    {
        std::ofstream out{dir.path() / "good.pak", std::ios::binary};
        out.write(reinterpret_cast<const char*>(good.data()), static_cast<std::streamsize>(good.size()));
    }
    Resources res;
    EXPECT_EQ(mountPaks(res, dir.path()), 1);
    EXPECT_EQ(text(res.read("imlui:/fonts/b.txt")), "bbb");
}

// ------------------------------------------------------------------ override order

TEST(PackedResources, EmbeddedThenPakThenDirectory) {
    test::TempDir dir;
    EmbeddedFixture embedded{{{"all.txt", "embedded"}, {"two.txt", "embedded"}, {"one.txt", "embedded"}}};
    const auto pak = PakMount::fromBytes(makePak(dir, "app:/", {{"all.txt", "pak"}, {"two.txt", "pak"}}), "p");
    ASSERT_TRUE(pak);
    dir.write("loose/all.txt", "directory");

    Resources res;
    res.mount("app:/", embedded.mount());
    res.mount(pak->mountPoint(), pak);
    res.mount("app:/", std::make_shared<DirectoryMount>(dir.path() / "loose"));
    EXPECT_EQ(text(res.read("app:/all.txt")), "directory");
    EXPECT_EQ(text(res.read("app:/two.txt")), "pak");
    EXPECT_EQ(text(res.read("app:/one.txt")), "embedded");
    EXPECT_EQ(res.list("app:/"), (std::vector<std::string>{"all.txt", "one.txt", "two.txt"}));
    EXPECT_TRUE(res.origin("app:/one.txt").starts_with("embedded 'test'"));
    EXPECT_TRUE(res.modified("app:/all.txt"));  // the directory serves it
    EXPECT_FALSE(res.modified("app:/two.txt")); // the archive is immutable
}

// ------------------------------------------------------------------ the library's own embedded files

TEST(EmbeddedLibrary, CarriesTheLuaSideAndTheFonts) {
    Resources res;
    embedded::imlui::mount(res);
    embedded::imlui_fonts::mount(res);
    EXPECT_EQ(text(res.read("imlui:/imlui.lua")), readSource(std::string{IMLUI_LUA_DIR} + "/imlui.lua"));
    for (const char* font : {"Roboto-Regular.ttf", "Roboto-Medium.ttf", "MaterialIconsRound-Regular.otf",
                             "MaterialIconsRound-Regular.codepoints"})
        EXPECT_EQ(text(res.read(std::string{"imlui:/fonts/"} + font)),
                  readSource(std::string{IMLUI_FONT_DIR} + "/" + font))
            << font;
    EXPECT_EQ(res.origin("imlui:/imlui.lua"), "embedded 'imlui' imlui.lua");
    // Text and fonts are worth compressing.
    for (const auto& e : embedded::imlui_fonts::files()->entries())
        if (e.path.ends_with(".ttf")) EXPECT_TRUE(e.compressed) << e.path;
}

// ------------------------------------------------------------------ release-style: no source tree

namespace {

/// The embedded app of the release-style tests (what imlui_add_resources would generate for an app).
void mountTestApp(Resources& resources) {
    resources.mount("app:/", std::make_shared<test::MemoryMount>(
                                 std::initializer_list<std::pair<const std::string, std::string>>{
                                     {"main.lua", "return Text { text = 'embedded app' }"}},
                                 "test app"));
}

} // namespace

TEST(ReleaseConfig, RunsFromEmbeddedDataAloneAndLooseFilesOverride) {
    test::TempDir dir;
    std::string exe = (dir.path() / "app.exe").string();
    char* argv[] = {exe.data()};

    // Nothing next to the executable: everything comes from the executable itself.
    {
        const EngineConfig cfg = defaultConfig(1, argv, nullptr, "scripts/main.lua", mountTestApp);
        EXPECT_EQ(cfg.entry, "app:/main.lua");
        EXPECT_EQ(text(cfg.resources.read("app:/main.lua")), "return Text { text = 'embedded app' }");
        EXPECT_TRUE(cfg.resources.origin("imlui:/imlui.lua").starts_with("embedded 'imlui'"));
        EXPECT_TRUE(cfg.resources.origin("imlui:/fonts/Roboto-Regular.ttf").starts_with("embedded 'imlui_fonts'"));
        EXPECT_EQ(cfg.resources.describe().size(), 3U); // imlui, imlui_fonts, the app: no folders, no archives
    }

    // A .pak next to it overrides embedded files; a folder overrides both.
    const auto pak = makePak(dir, "imlui:/", {{"imlui.lua", "-- from the pak"}, {"extra.lua", "-- pak only"}});
    {
        std::ofstream out{dir.path() / "override.pak", std::ios::binary};
        out.write(reinterpret_cast<const char*>(pak.data()), static_cast<std::streamsize>(pak.size()));
    }
    dir.write("lua/extra.lua", "-- loose");
    {
        const EngineConfig cfg = defaultConfig(1, argv, nullptr, "scripts/main.lua", mountTestApp);
        EXPECT_EQ(text(cfg.resources.read("imlui:/imlui.lua")), "-- from the pak");
        EXPECT_EQ(text(cfg.resources.read("imlui:/extra.lua")), "-- loose");
        EXPECT_EQ(text(cfg.resources.read("imlui:/fonts/Roboto-Regular.ttf")).size(),
                  readSource(std::string{IMLUI_FONT_DIR} + "/Roboto-Regular.ttf").size());
    }

    // A script on the command line replaces the embedded app.
    dir.write("other/main.lua", "return Text { text = 'other' }");
    std::string script = (dir.path() / "other" / "main.lua").string();
    char* withScript[] = {exe.data(), script.data()};
    const EngineConfig cfg = defaultConfig(2, withScript, nullptr, "scripts/main.lua", mountTestApp);
    EXPECT_EQ(text(cfg.resources.read("app:/main.lua")), "return Text { text = 'other' }");
}

TEST(ReleaseConfig, HeadlessUiRunsOnEmbeddedLuaAndFonts) {
    test::HeadlessFrame::Options options;
    options.embedded = true;
    options.builtins = true;
    test::HeadlessFrame h{options};
    EXPECT_TRUE(h.resources().origin("imlui:/imlui.lua").starts_with("embedded"));
    ASSERT_EQ(h.run(R"(
        local info = debug.getinfo(ui.element, "S")
        assert(info.source == "@imlui:/imlui.lua", info.source)
        root = Column { Text { text = "hello" }, Button { text = "ok", icon = "check" } }
    )"),
              "");
    h.renderFrames("root", 3);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST(PakMount, OpensAnArchiveBuiltByImluiAddPak) {
    std::string error;
    const auto pak = PakMount::open(IMLUI_TEST_PAK, &error);
    ASSERT_TRUE(pak) << error;
    EXPECT_EQ(pak->mountPoint(), "app:/counter/");
    EXPECT_EQ(text(pak->read("main.lua")), readSource(std::string{IMLUI_COUNTER_DIR} + "/main.lua"));
}
