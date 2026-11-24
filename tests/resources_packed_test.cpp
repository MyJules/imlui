// Tests for the packed resource mounts: EmbeddedMount (stored and zstd-compressed files, lazy decompression,
// concurrent reads), .pak archives (written by the imlui_pack code, read by PakMount; corrupt and truncated archives
// rejected), the override order embedded < .pak < directory, the library's own embedded files and packages, and
// defaultConfig: release-style (embedded data only), archives and prefix folders next to the executable, dev source
// folders on top, packages without files (IMLUI_EMBED_RESOURCES=OFF) mounting their source folder, a script argument
// replacing the app; and the headless harness without src/resources/.
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

import imlui.resources;
import imlui.resources.mount;
import imlui.resources.directory_mount;
import imlui.resources.packed_mount;
import imlui.resources.embedded_mount;
import imlui.resources.pak_format;
import imlui.resources.pak_mount;
import imlui.resources.package;
import imlui.app.config;
import imlui.embedded.imlui;
import imlui.lua.errors;
import imlui.tools.resource_encoding;
import imlui.tools.source_files;
import imlui.tools.pack;
import imlui.test.headless_frame;
import imlui.test.memory_mount;
import imlui.test.scoped_env;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

std::string text(std::optional<std::span<const std::byte>> bytes) {
    if (!bytes) return "<none>";
    return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

std::span<const std::byte> bytesOf(std::string_view s) { return std::as_bytes(std::span{s.data(), s.size()}); }

/// Whether the library's own files are embedded (IMLUI_EMBED_RESOURCES=ON); dev builds mount their source folders.
#ifdef IMLUI_TEST_EMBED_RESOURCES
constexpr bool libraryEmbedded = true;
#else
constexpr bool libraryEmbedded = false;
#endif

/// How the origin of a library file starts: `embedded 'imlui (imlui:/fonts/)'` or `directory '`.
std::string libraryOrigin(std::string_view mountPoint) {
    return libraryEmbedded ? "embedded 'imlui (" + std::string{mountPoint} + ")'" : std::string{"directory '"};
}

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
    EXPECT_EQ(text(res.read("imlui:/imlui.lua")), readSource(std::string{IMLUI_RESOURCES_DIR} + "/imlui.lua"));
    for (const char* font : {"Roboto-Regular.ttf", "Roboto-Medium.ttf", "MaterialIconsRound-Regular.otf",
                             "MaterialIconsRound-Regular.codepoints"})
        EXPECT_EQ(text(res.read(std::string{"imlui:/fonts/"} + font)),
                  readSource(std::string{IMLUI_FONT_DIR} + "/" + font))
            << font;
    EXPECT_TRUE(res.origin("imlui:/imlui.lua").starts_with(libraryOrigin("imlui:/")));
    const auto packages = embedded::imlui::packages();
    ASSERT_EQ(packages.size(), 2U);
    if (!libraryEmbedded) { // the source folders, mounted by mount()
        EXPECT_EQ(packages[1].files, nullptr);
        return;
    }
    // Text and fonts are worth compressing.
    const auto* fonts = dynamic_cast<const PackedMount*>(packages[1].files.get());
    ASSERT_NE(fonts, nullptr);
    for (const auto& e : fonts->entries())
        if (e.path.ends_with(".ttf")) EXPECT_TRUE(e.compressed) << e.path;
}

// ------------------------------------------------------------------ defaultConfig: packages, overrides, dev folders

namespace {

/// An embedded app package, as imlui_add_resources generates it; `sourceDir` is "" like in a release build.
ResourcePackage testApp(std::string sourceDir = {}) {
    return {"app:/",
            std::make_shared<test::MemoryMount>(std::initializer_list<std::pair<const std::string, std::string>>{
                                                    {"main.lua", "return Text { text = 'embedded app' }"},
                                                    {"data.txt", "embedded data"}},
                                                "test app"),
            std::move(sourceDir)};
}

void writeFile(const std::filesystem::path& file, std::span<const std::byte> bytes) {
    std::ofstream out{file, std::ios::binary};
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST(EmbeddedLibrary, PackagesNameTheirMountPointAndSourceFolder) {
    const std::vector<ResourcePackage> packages = embedded::imlui::packages();
    ASSERT_EQ(packages.size(), 2U); // src/resources/resources.lua: src/resources/ and the fonts
    const ResourcePackage& lua = packages[0];
    EXPECT_EQ(lua.mountPoint, "imlui:/");
    EXPECT_EQ(lua.files, embedded::imlui::packages()[0].files); // one mount per group, shared by every caller
    // The source folder is baked in only with IMLUI_DEV_PATHS=ON.
    if (!lua.sourceDir.empty())
        EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path{lua.sourceDir}, IMLUI_RESOURCES_DIR));
    const ResourcePackage& fonts = packages[1];
    EXPECT_EQ(fonts.mountPoint, "imlui:/fonts/");
    if (!fonts.sourceDir.empty())
        EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path{fonts.sourceDir}, IMLUI_FONT_DIR));
}

TEST(DefaultConfig, ReleaseRunsFromEmbeddedDataAlone) {
    const test::ScopedEnv noDev{"IMLUI_DEV_PATHS", "0"}; // ignore the library's own source folders (dev builds)
    test::TempDir dir;
    std::string exe = (dir.path() / "app.exe").string();
    char* argv[] = {exe.data()};

    const EngineConfig cfg = defaultConfig(1, argv, {testApp()});
    EXPECT_EQ(cfg.entry, "app:/main.lua");
    EXPECT_EQ(text(cfg.resources.read("app:/main.lua")), "return Text { text = 'embedded app' }");
    // Not embedded (dev builds), the library's source folders are its only copy, mounted despite IMLUI_DEV_PATHS=0.
    EXPECT_TRUE(cfg.resources.origin("imlui:/imlui.lua").starts_with(libraryOrigin("imlui:/")));
    EXPECT_TRUE(cfg.resources.origin("imlui:/fonts/Roboto-Regular.ttf").starts_with(libraryOrigin("imlui:/fonts/")));
    EXPECT_EQ(cfg.resources.describe().size(), 3U); // imlui:/, imlui:/fonts/, the app: no folders, no archives

    // No app package: the library alone, and the entry is still app:/main.lua (reported missing by the Engine).
    const EngineConfig bare = defaultConfig(1, argv);
    EXPECT_EQ(bare.entry, "app:/main.lua");
    EXPECT_EQ(bare.resources.describe().size(), 2U);
}

TEST(DefaultConfig, CombinesThePackagesOfSeveralModules) {
    const test::ScopedEnv noDev{"IMLUI_DEV_PATHS", "0"};
    test::TempDir dir;
    std::string exe = (dir.path() / "app.exe").string();
    char* argv[] = {exe.data()};
    const std::vector<ResourcePackage> app{testApp()};
    const std::vector<ResourcePackage> plugin{
        {"plugin:/charts/",
         std::make_shared<test::MemoryMount>(
             std::initializer_list<std::pair<const std::string, std::string>>{{"chart.lua", "-- chart"}}, "plugin"),
         ""}};

    const EngineConfig cfg = defaultConfig(1, argv, {app, plugin});
    EXPECT_EQ(text(cfg.resources.read("app:/main.lua")), "return Text { text = 'embedded app' }");
    EXPECT_EQ(text(cfg.resources.read("plugin:/charts/chart.lua")), "-- chart");
    EXPECT_EQ(cfg.resources.describe().size(), 4U); // the library's two packages, the app, the plugin
}

TEST(DefaultConfig, ArchivesThenPrefixFoldersOverrideEmbeddedFiles) {
    const test::ScopedEnv noDev{"IMLUI_DEV_PATHS", "0"};
    test::TempDir dir;
    std::string exe = (dir.path() / "app.exe").string();
    char* argv[] = {exe.data()};

    // Archives in file name order: b.pak wins over a.pak.
    // (pak.lua, not imlui.lua: in dev builds the library's source folder is on top of everything.)
    writeFile(dir.path() / "a.pak", makePak(dir, "imlui:/", {{"pak.lua", "-- a.pak"}, {"extra.lua", "-- a.pak"}}));
    std::filesystem::remove_all(dir.path() / "pak_src");
    writeFile(dir.path() / "b.pak", makePak(dir, "app:/", {{"data.txt", "b.pak"}, {"pak.txt", "b.pak"}}));
    std::filesystem::remove_all(dir.path() / "pak_src");
    // Folders named after a prefix override the archives.
    dir.write("imlui/extra.lua", "-- loose");
    dir.write("imlui/fonts/extra.txt", "loose font folder");
    dir.write("app/pak.txt", "loose");

    const EngineConfig cfg = defaultConfig(1, argv, {testApp()});
    const Resources& res = cfg.resources;
    EXPECT_EQ(text(res.read("imlui:/pak.lua")), "-- a.pak");
    EXPECT_EQ(text(res.read("imlui:/extra.lua")), "-- loose");
    EXPECT_EQ(text(res.read("imlui:/fonts/extra.txt")), "loose font folder");
    EXPECT_EQ(text(res.read("imlui:/fonts/Roboto-Regular.ttf")).size(),
              readSource(std::string{IMLUI_FONT_DIR} + "/Roboto-Regular.ttf").size());
    EXPECT_EQ(text(res.read("app:/main.lua")), "return Text { text = 'embedded app' }");
    EXPECT_EQ(text(res.read("app:/data.txt")), "b.pak");
    EXPECT_EQ(text(res.read("app:/pak.txt")), "loose");
    // embedded x3, two archives, two folders
    EXPECT_EQ(res.describe().size(), 7U);
}

TEST(DefaultConfig, DevSourceFoldersGoOnTop) {
    test::TempDir dir;
    std::string exe = (dir.path() / "bin" / "app.exe").string();
    char* argv[] = {exe.data()};
    dir.write("src/main.lua", "return Text { text = 'source' }");
    dir.write("src/StatCard.lua", "return {}");
    dir.write("bin/app/main.lua", "return Text { text = 'loose' }");
    const std::string source = (dir.path() / "src").string();

    {
        const test::ScopedEnv dev{"IMLUI_DEV_PATHS", "1"};
        const EngineConfig cfg = defaultConfig(1, argv, {testApp(source)});
        EXPECT_EQ(cfg.entry, "app:/main.lua");
        EXPECT_EQ(text(cfg.resources.read("app:/main.lua")), "return Text { text = 'source' }");
        EXPECT_TRUE(cfg.resources.exists("app:/StatCard.lua"));                // components next to the script
        EXPECT_EQ(text(cfg.resources.read("app:/data.txt")), "embedded data"); // falls back to the embedded copy
        EXPECT_TRUE(cfg.resources.modified("app:/main.lua"));                  // a directory: hot reload sees changes
    }
    {
        // IMLUI_DEV_PATHS=0 runs like a release: the loose folder next to the executable wins.
        const test::ScopedEnv noDev{"IMLUI_DEV_PATHS", "0"};
        const EngineConfig cfg = defaultConfig(1, argv, {testApp(source)});
        EXPECT_EQ(text(cfg.resources.read("app:/main.lua")), "return Text { text = 'loose' }");
        EXPECT_FALSE(cfg.resources.exists("app:/StatCard.lua"));
    }
    {
        // A source folder that doesn't exist (the binary moved to another machine) is skipped.
        const test::ScopedEnv dev{"IMLUI_DEV_PATHS", "1"};
        const EngineConfig cfg = defaultConfig(1, argv, {testApp((dir.path() / "gone").string())});
        EXPECT_EQ(text(cfg.resources.read("app:/main.lua")), "return Text { text = 'loose' }");
    }
}

TEST(DefaultConfig, PackagesWithoutFilesMountTheirSourceFolder) {
    // IMLUI_DEV_PATHS=0 skips the source folders of embedded packages, not the only copy of the files.
    const test::ScopedEnv noDev{"IMLUI_DEV_PATHS", "0"};
    test::TempDir dir;
    std::string exe = (dir.path() / "bin" / "app.exe").string();
    char* argv[] = {exe.data()};
    dir.write("src/main.lua", "return Text { text = 'source' }");
    const ResourcePackage sources{"app:/", nullptr, (dir.path() / "src").string()};

    const EngineConfig cfg = defaultConfig(1, argv, {sources});
    EXPECT_EQ(text(cfg.resources.read("app:/main.lua")), "return Text { text = 'source' }");
    EXPECT_TRUE(cfg.resources.modified("app:/main.lua"));

    // A missing folder (the executable moved) is logged as an error, and nothing is mounted for it.
    const ResourcePackage gone{"app:/", nullptr, (dir.path() / "gone").string()};
    const EngineConfig moved = defaultConfig(1, argv, {gone});
    EXPECT_FALSE(moved.resources.exists("app:/main.lua"));
    EXPECT_EQ(moved.resources.describe().size(), cfg.resources.describe().size() - 1);
}

TEST(DefaultConfig, ScriptArgumentReplacesTheApp) {
    const test::ScopedEnv dev{"IMLUI_DEV_PATHS", "1"};
    test::TempDir dir;
    std::string exe = (dir.path() / "bin" / "app.exe").string();
    dir.write("src/data.txt", "source data");
    dir.write("bin/app/data.txt", "loose data");
    writeFile(dir.path() / "bin" / "app.pak", makePak(dir, "app:/", {{"data.txt", "pak data"}}));
    dir.write("other/counter.lua", "return Text { text = 'other' }");
    const std::string source = (dir.path() / "src").string();

    std::string script = (dir.path() / "other" / "counter.lua").string();
    char* argv[] = {exe.data(), script.data()};
    const EngineConfig cfg = defaultConfig(2, argv, {testApp(source)});
    EXPECT_EQ(cfg.entry, "app:/counter.lua");
    EXPECT_EQ(text(cfg.resources.read("app:/counter.lua")), "return Text { text = 'other' }");
    // Nothing of the app package (embedded, source folder), the archive or the app/ folder is under it.
    EXPECT_FALSE(cfg.resources.exists("app:/main.lua"));
    EXPECT_FALSE(cfg.resources.exists("app:/data.txt"));
    EXPECT_TRUE(cfg.resources.exists("imlui:/imlui.lua")); // the library stays

    // A relative script that isn't in the current folder is looked up next to the executable.
    dir.write("bin/scripts/hello/main.lua", "return Text { text = 'next to the exe' }");
    std::string relative = "scripts/hello/main.lua";
    char* withRelative[] = {exe.data(), relative.data()};
    const EngineConfig next = defaultConfig(2, withRelative, {testApp(source)});
    EXPECT_EQ(next.entry, "app:/main.lua");
    EXPECT_EQ(text(next.resources.read("app:/main.lua")), "return Text { text = 'next to the exe' }");

    // A missing script is still mounted where it was looked for, so the Engine reports it there.
    std::string missing = (dir.path() / "nowhere" / "main.lua").string();
    char* withMissing[] = {exe.data(), missing.data()};
    const EngineConfig none = defaultConfig(2, withMissing, {testApp(source)});
    EXPECT_FALSE(none.resources.exists("app:/main.lua"));
}

TEST(ReleaseConfig, HeadlessUiRunsOnEmbeddedLuaAndFonts) {
    test::HeadlessFrame::Options options;
    options.embedded = true;
    options.builtins = true;
    test::HeadlessFrame h{options};
    EXPECT_TRUE(h.resources().origin("imlui:/imlui.lua").starts_with(libraryOrigin("imlui:/")));
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
    EXPECT_EQ(pak->mountPoint(), "app:/smoke/");
    EXPECT_EQ(text(pak->read("main.lua")), readSource(std::string{IMLUI_SMOKE_APP_DIR} + "/main.lua"));
    EXPECT_EQ(text(pak->read("Tile.lua")), readSource(std::string{IMLUI_SMOKE_APP_DIR} + "/Tile.lua"));
    EXPECT_FALSE(pak->exists("resources.lua")); // excluded by data/smoke_pak.lua
}
