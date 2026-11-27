// Tests for the resource system: name normalization and resolving (imlui.resources.name), DirectoryMount, mount
// stacking in Resources, and the Lua side (the module searchers, ui.__load, ui.resource.*) through the headless
// harness with an `app:/` mount on a temporary folder.
#include <gtest/gtest.h>

import std;

import imlui.resources;
import imlui.resources.name;
import imlui.resources.mount;
import imlui.resources.directory_mount;
import imlui.lua.errors;
import imlui.test.file_lock;
import imlui.test.headless_frame;
import imlui.test.memory_mount;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

std::string text(const std::optional<ResourceData>& bytes) {
    if (!bytes) return "<none>";
    return std::string{bytes->text()};
}

/// A MemoryMount (immutable, like an embedded one) with `files`.
std::shared_ptr<Mount> memory(std::initializer_list<std::pair<const std::string, std::string>> files,
                              std::string name = "memory") {
    return std::make_shared<test::MemoryMount>(files, std::move(name));
}

std::vector<std::string> strings(std::initializer_list<const char*> list) { return {list.begin(), list.end()}; }

} // namespace

// ------------------------------------------------------------------ names

TEST(ResourceName, NormalizesFullNames) {
    EXPECT_EQ(normalizeName("app:/main.lua"), "app:/main.lua");
    EXPECT_EQ(normalizeName("app:/a/./b//c.lua"), "app:/a/b/c.lua");
    EXPECT_EQ(normalizeName("app:/a/../b.lua"), "app:/b.lua");
    EXPECT_EQ(normalizeName("app://a"), "app:/a");
    EXPECT_EQ(normalizeName("app:/"), "app:/");
    EXPECT_EQ(normalizeName("app:/a/"), "app:/a");
    EXPECT_EQ(normalizeName("my-lib_2:/x"), "my-lib_2:/x");
    const auto parsed = parseName("imlui:/fonts/./Roboto-Regular.ttf");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->prefix, "imlui");
    EXPECT_EQ(parsed->path, "fonts/Roboto-Regular.ttf");
    EXPECT_EQ(parsed->full(), "imlui:/fonts/Roboto-Regular.ttf");
}

TEST(ResourceName, RejectsEscapingTheRoot) {
    EXPECT_FALSE(normalizeName("app:/.."));
    EXPECT_FALSE(normalizeName("app:/../x"));
    EXPECT_FALSE(normalizeName("app:/a/../../x"));
    EXPECT_FALSE(normalizePath("../a"));
    EXPECT_FALSE(normalizePath("a/../../b"));
}

TEST(ResourceName, RejectsOsPathsAndMalformedNames) {
    EXPECT_FALSE(normalizeName("C:/Windows/x")); // a drive letter is not a prefix
    EXPECT_FALSE(normalizeName("c:/x"));         // prefixes are at least two characters
    EXPECT_FALSE(normalizeName("/etc/passwd"));  // no prefix
    EXPECT_FALSE(normalizeName("App:/x"));       // prefixes are lowercase
    EXPECT_FALSE(normalizeName("1app:/x"));      // and start with a letter
    EXPECT_FALSE(normalizeName("app:x"));        // `:/` is required
    EXPECT_FALSE(normalizeName("app:/a\\b"));    // forward slashes only
    EXPECT_FALSE(normalizeName("app:/a/c:/x"));  // no `:` inside the path
    EXPECT_FALSE(normalizeName(std::string_view{"app:/a\0b", 8}));
    EXPECT_FALSE(normalizeName("main.lua"));
}

TEST(ResourceName, NormalizesPaths) {
    EXPECT_EQ(normalizePath(""), "");
    EXPECT_EQ(normalizePath("a//./b/../c"), "a/c");
    EXPECT_EQ(normalizePath("a/"), "a");
    EXPECT_FALSE(normalizePath("/a"));
}

TEST(ResourceName, ResolvesRelativeNamesAgainstTheBaseFolder) {
    EXPECT_EQ(resolveName("app:/sub/main.lua", "x.lua"), "app:/sub/x.lua");
    EXPECT_EQ(resolveName("app:/sub/main.lua", "./data/x.json"), "app:/sub/data/x.json");
    EXPECT_EQ(resolveName("app:/sub/main.lua", "../y.lua"), "app:/y.lua");
    EXPECT_EQ(resolveName("app:/sub/", "x"), "app:/sub/x");
    EXPECT_EQ(resolveName("app:/", "x"), "app:/x");
    EXPECT_EQ(resolveName("app:/sub/main.lua", "imlui:/a/../b"), "imlui:/b");
    EXPECT_FALSE(resolveName("app:/sub/main.lua", "../../y.lua"));
    EXPECT_FALSE(resolveName("app:/main.lua", "/abs/path"));
    EXPECT_FALSE(resolveName("app:/main.lua", "C:/abs/path"));
    EXPECT_FALSE(resolveName("app:/main.lua", ""));
    EXPECT_FALSE(resolveName("not a name", "x"));
    EXPECT_EQ(folderOf("app:/a/b.lua"), "app:/a/");
    EXPECT_EQ(folderOf("app:/a/"), "app:/a/");
    EXPECT_EQ(folderOf("app:/b.lua"), "app:/");
}

// ------------------------------------------------------------------ DirectoryMount

TEST(DirectoryMount, ReadsListsAndChecksFiles) {
    test::TempDir dir;
    dir.write("main.lua", "return 1");
    dir.write("sub/a.txt", "A");
    dir.write("sub/deeper/b.txt", "B");
    DirectoryMount mount{dir.path()};

    EXPECT_EQ(text(mount.read("main.lua")), "return 1");
    EXPECT_EQ(text(mount.read("sub/deeper/b.txt")), "B");
    EXPECT_FALSE(mount.read("missing.lua"));
    EXPECT_FALSE(mount.read("sub")); // a folder isn't a file
    EXPECT_TRUE(mount.exists(""));
    EXPECT_TRUE(mount.exists("sub"));
    EXPECT_TRUE(mount.exists("sub/a.txt"));
    EXPECT_FALSE(mount.exists("nope"));
    EXPECT_EQ(mount.list(""), strings({"main.lua", "sub/"}));
    EXPECT_EQ(mount.list("sub"), strings({"a.txt", "deeper/"}));
    EXPECT_TRUE(mount.list("main.lua").empty());
    EXPECT_NE(mount.origin().find("directory"), std::string::npos);
}

TEST(DirectoryMount, RejectsPathsThatArentNormalized) {
    test::TempDir dir;
    dir.write("inner/x.txt", "x");
    dir.write("top.txt", "t");
    DirectoryMount mount{dir.path() / "inner"};
    EXPECT_EQ(text(mount.read("x.txt")), "x");
    EXPECT_FALSE(mount.read("../top.txt"));
    EXPECT_FALSE(mount.exists("../top.txt"));
    EXPECT_FALSE(mount.read("./x.txt"));
    EXPECT_FALSE(mount.read("/x.txt"));
    EXPECT_TRUE(mount.list("..").empty());
}

TEST(DirectoryMount, ReportsModificationTimesAndRereadsChangedFiles) {
    test::TempDir dir;
    dir.write("a.lua", "old");
    DirectoryMount mount{dir.path()};
    const auto before = mount.modified("a.lua");
    ASSERT_TRUE(before);
    EXPECT_FALSE(mount.modified("missing.lua"));
    EXPECT_FALSE(mount.modified("")); // folders have no time

    const auto first = mount.read("a.lua");
    ASSERT_TRUE(first);
    EXPECT_EQ(mount.read("a.lua")->data(), first->data()) << "an unchanged file is served from the cache";

    dir.write("a.lua", "newer");
    dir.touch("a.lua", 10);
    const auto after = mount.modified("a.lua");
    ASSERT_TRUE(after);
    EXPECT_GT(*after, *before);
    EXPECT_EQ(text(mount.read("a.lua")), "newer");
    EXPECT_EQ(text(first), "old") << "bytes read earlier stay valid while their handle lives";
}

TEST(DirectoryMount, FreesSupersededBuffersOnceNothingHoldsThem) {
    test::TempDir dir;
    dir.write("a.txt", "v0");
    DirectoryMount mount{dir.path()};
    std::vector<std::weak_ptr<const void>> versions;
    const auto held = mount.read("a.txt"); // a holder of version 0, like a font in use
    ASSERT_TRUE(held && held->owner());
    versions.push_back(held->owner());
    for (int i = 1; i <= 20; ++i) {
        dir.write("a.txt", "v" + std::to_string(i) + std::string(static_cast<std::size_t>(i) * 1000, 'x'));
        dir.touch("a.txt", 10 * i);
        const auto bytes = mount.read("a.txt"); // dropped at the end of the iteration
        ASSERT_TRUE(bytes);
        EXPECT_TRUE(bytes->text().starts_with("v" + std::to_string(i)));
        versions.push_back(bytes->owner());
    }
    // Live: the version still held and the latest one (the mount's cache). Every other buffer is gone.
    for (std::size_t i = 0; i < versions.size(); ++i)
        EXPECT_EQ(versions[i].expired(), i != 0 && i + 1 != versions.size()) << "version " << i;
    EXPECT_EQ(text(held), "v0") << "bytes held by a handle stay valid";

    // Removing the file drops the cache's reference too.
    dir.remove("a.txt");
    EXPECT_FALSE(mount.read("a.txt"));
    EXPECT_TRUE(versions.back().expired());
}

// The cache keeps only the files read most recently alive of its own (DirectoryMount::retainedBytes in all); other
// data lives while a handle holds it, so a file read once (an image, a large data file) is freed when dropped instead
// of staying in memory for the mount's lifetime. Re-reading works and still sees changes.
TEST(DirectoryMount, ReleasesDataNothingHolds) {
    test::TempDir dir;
    const std::size_t large = DirectoryMount::retainedBytes + 1;
    dir.write("large.bin", std::string(large, 'a'));
    DirectoryMount mount{dir.path()};
    std::weak_ptr<const void> first;
    {
        const auto bytes = mount.read("large.bin");
        ASSERT_TRUE(bytes);
        EXPECT_EQ(bytes->size(), large);
        first = bytes->owner();
        const auto again = mount.read("large.bin");
        ASSERT_TRUE(again);
        EXPECT_EQ(again->data(), bytes->data()) << "served from the cache while a handle holds it";
    }
    EXPECT_TRUE(first.expired()) << "a file larger than retainedBytes is released with its last handle";
    const auto reread = mount.read("large.bin");
    ASSERT_TRUE(reread);
    EXPECT_EQ(reread->size(), large);
    EXPECT_EQ(reread->text().front(), 'a');

    dir.write("large.bin", std::string(large, 'b'));
    dir.touch("large.bin", 10);
    auto changed = mount.read("large.bin");
    ASSERT_TRUE(changed);
    EXPECT_EQ(changed->text().front(), 'b') << "a change is still seen";
    EXPECT_EQ(reread->text().front(), 'a') << "the handle to the earlier bytes keeps them";

    // Smaller files stay alive while they fit, the least recently read going first.
    const std::size_t third = DirectoryMount::retainedBytes / 3 + 1;
    std::map<std::string, std::weak_ptr<const void>> owners;
    for (const char* name : {"a.bin", "b.bin", "c.bin"}) {
        dir.write(name, std::string(third, name[0]));
        owners[name] = mount.read(name)->owner();
    }
    EXPECT_TRUE(owners["a.bin"].expired()) << "three thirds and a bit don't fit";
    EXPECT_FALSE(owners["b.bin"].expired());
    EXPECT_FALSE(owners["c.bin"].expired());
    EXPECT_EQ(mount.read("b.bin")->owner(), owners["b.bin"].lock()) << "a retained file is served from the cache";
    owners["a.bin"] = mount.read("a.bin")->owner(); // now c.bin is the least recently read
    EXPECT_TRUE(owners["c.bin"].expired());
    EXPECT_FALSE(owners["a.bin"].expired());
    EXPECT_FALSE(owners["b.bin"].expired());
    EXPECT_EQ(mount.read("c.bin")->text().front(), 'c');

    // A changed file that can't be read and whose previous bytes are gone is missing until it can be read.
    dir.write("large.bin", std::string(large, 'c'));
    dir.touch("large.bin", 20);
    {
        test::FileLock lock{dir.path() / "large.bin"};
        if (!lock.locked()) GTEST_SKIP() << "can't lock a file here";
        EXPECT_EQ(text(mount.read("large.bin")).front(), 'b') << "the bytes a handle still holds";
    }
    dir.write("large.bin", std::string(large, 'd'));
    dir.touch("large.bin", 30);
    {
        test::FileLock lock{dir.path() / "large.bin"};
        if (!lock.locked()) GTEST_SKIP() << "can't lock a file here";
        changed.reset();
        EXPECT_FALSE(mount.read("large.bin")) << "nothing holds the previous bytes";
        EXPECT_FALSE(mount.modified("large.bin")) << "changed, for the next poll";
    }
    EXPECT_EQ(text(mount.read("large.bin")).front(), 'd');
    EXPECT_TRUE(mount.modified("large.bin"));
}

// A selection (a manifest group's `files`, `include` and `exclude`; a dev build's source folder) limits the mount to
// the files the build would embed: the others aren't read, found, timed or listed, and folders without a selected file
// don't show. A file added later that matches a pattern is served at once (hot reload), as the next build embeds it.
TEST(DirectoryMount, ServesOnlyItsSelection) {
    test::TempDir dir;
    dir.write("resources.lua", "return {}");
    dir.write("main.lua", "return 1");
    dir.write("notes.txt", "not selected");
    dir.write("ui/Card.lua", "card");
    dir.write("ui/tests/CardTest.lua", "excluded");
    dir.write("docs/readme.md", "nothing selected in here");
    dir.write("data/listed.json", "{}");
    dir.write("data/other.json", "{}");
    dir.write("sub/resources.lua", "-- another path: a pattern matches it");
    DirectoryMount mount{dir.path(), ResourceSelection{.files = {"data/listed.json"},
                                                       .include = {"**/*.lua"},
                                                       .exclude = {"ui/tests/**"},
                                                       .unmatched = {"resources.lua"}}};
    EXPECT_EQ(text(mount.read("main.lua")), "return 1");
    EXPECT_EQ(text(mount.read("ui/Card.lua")), "card");
    EXPECT_EQ(text(mount.read("data/listed.json")), "{}");
    EXPECT_EQ(text(mount.read("sub/resources.lua")), "-- another path: a pattern matches it");
    for (const char* unlisted : {"resources.lua", "notes.txt", "ui/tests/CardTest.lua", "data/other.json"}) {
        EXPECT_FALSE(mount.read(unlisted)) << unlisted;
        EXPECT_FALSE(mount.exists(unlisted)) << unlisted;
        EXPECT_FALSE(mount.modified(unlisted)) << unlisted;
    }
    EXPECT_TRUE(mount.modified("main.lua"));
    EXPECT_EQ(mount.list(""), strings({"data/", "main.lua", "sub/", "ui/"}));
    EXPECT_EQ(mount.list("ui"), strings({"Card.lua"}));
    EXPECT_EQ(mount.list("data"), strings({"listed.json"}));
    EXPECT_TRUE(mount.list("docs").empty());
    EXPECT_FALSE(mount.exists("docs"));
    EXPECT_FALSE(mount.exists("ui/tests"));
    EXPECT_TRUE(mount.exists("ui"));

    // New files: one a pattern matches is there at once, one nothing selects isn't.
    dir.write("ui/New.lua", "new");
    dir.write("docs/more.md", "still nothing");
    EXPECT_EQ(text(mount.read("ui/New.lua")), "new");
    EXPECT_EQ(mount.list("ui"), strings({"Card.lua", "New.lua"}));
    EXPECT_FALSE(mount.exists("docs/more.md"));
    dir.write("docs/guide.lua", "a pattern matches it");
    EXPECT_TRUE(mount.exists("docs"));
    EXPECT_EQ(mount.list("docs"), strings({"guide.lua"}));
}

// No pattern matches the manifest, but a manifest listed in `files` is a resource like any other: served by the dev
// folder (not hidden, which would fall back to a stale embedded copy).
TEST(DirectoryMount, ServesAManifestItsSelectionLists) {
    test::TempDir dir;
    dir.write("resources.lua", "return { prefix = 'app', files = { 'resources.lua' } }");
    DirectoryMount listed{
        dir.path(), ResourceSelection{.files = {"resources.lua"}, .include = {"**"}, .unmatched = {"resources.lua"}}};
    EXPECT_EQ(text(listed.read("resources.lua")), "return { prefix = 'app', files = { 'resources.lua' } }");
    EXPECT_TRUE(listed.modified("resources.lua"));
    EXPECT_EQ(listed.list(""), strings({"resources.lua"}));
    DirectoryMount matched{dir.path(), ResourceSelection{.include = {"**"}, .unmatched = {"resources.lua"}}};
    EXPECT_FALSE(matched.read("resources.lua"));
    EXPECT_TRUE(matched.list("").empty());
}

TEST(ResourceSelection, ContainsAndFolders) {
    const ResourceSelection s{.files = {"a/b/listed.txt"}, .include = {"ui/**/*.lua", "*.png"}, .exclude = {"ui/x/**"}};
    EXPECT_TRUE(s.contains("a/b/listed.txt"));
    EXPECT_TRUE(s.contains("ui/Card.lua"));
    EXPECT_TRUE(s.contains("logo.png"));
    EXPECT_FALSE(s.contains("ui/x/Card.lua"));
    EXPECT_FALSE(s.contains("sub/logo.png"));
    EXPECT_TRUE(s.mayContainUnder(""));
    EXPECT_TRUE(s.mayContainUnder("a"));
    EXPECT_TRUE(s.mayContainUnder("a/b"));
    EXPECT_FALSE(s.mayContainUnder("a/b/listed.txt"));
    EXPECT_TRUE(s.mayContainUnder("ui/deep/er"));
    EXPECT_FALSE(s.mayContainUnder("docs"));
    EXPECT_FALSE(ResourceSelection{}.mayContainUnder(""));
}

// A changed file that can't be opened right now (an editor holding it without sharing while it saves) is served from
// its previous bytes, not dropped (which would fall back to a stale embedded copy), and modified() keeps reporting the
// time of those bytes until a read succeeds, so a hot reload polling it tries again.
TEST(DirectoryMount, ServesThePreviousVersionWhileAChangedFileCantBeRead) {
    test::TempDir dir;
    dir.write("a.lua", "v1");
    DirectoryMount mount{dir.path()};
    const auto v1 = mount.modified("a.lua");
    ASSERT_TRUE(v1);
    ASSERT_EQ(text(mount.read("a.lua")), "v1");

    dir.write("a.lua", "v2");
    dir.touch("a.lua", 10);
    {
        test::FileLock lock{dir.path() / "a.lua"};
        if (!lock.locked()) GTEST_SKIP() << "can't lock a file here";
        const auto v2 = mount.modified("a.lua");
        ASSERT_TRUE(v2);
        EXPECT_NE(*v2, *v1) << "the change is seen";
        EXPECT_EQ(text(mount.read("a.lua")), "v1") << "the previous bytes, not nothing";
        EXPECT_EQ(mount.modified("a.lua"), v1) << "still changed for the next poll";
    }
    EXPECT_EQ(text(mount.read("a.lua")), "v2");
    EXPECT_NE(mount.modified("a.lua"), v1);

    // A file that was never read and can't be: missing, and no time, until it can be.
    dir.write("b.lua", "b");
    {
        test::FileLock lock{dir.path() / "b.lua"};
        if (!lock.locked()) GTEST_SKIP() << "can't lock a file here";
        EXPECT_TRUE(mount.modified("b.lua"));
        EXPECT_FALSE(mount.read("b.lua"));
        EXPECT_FALSE(mount.modified("b.lua"));
    }
    EXPECT_EQ(text(mount.read("b.lua")), "b");
    EXPECT_TRUE(mount.modified("b.lua"));
}

// A DirectoryMount never throws (a failure reads as "not found"): a name that isn't valid UTF-8 (which the
// file system can't convert) is simply not found, also below an existing file.
TEST(DirectoryMount, NeverThrows) {
    test::TempDir dir;
    dir.write("main.lua", "return 1");
    DirectoryMount mount{dir.path()};
    for (const char* bad : {"\xff", "main.lua/\xff", "\xc3(x", "sub/\xed\xa0\x80.lua"}) {
        EXPECT_NO_THROW({
            EXPECT_FALSE(mount.read(bad));
            EXPECT_FALSE(mount.exists(bad));
            EXPECT_TRUE(mount.list(bad).empty());
            EXPECT_FALSE(mount.modified(bad));
        }) << bad;
    }
    EXPECT_EQ(text(mount.read("main.lua")), "return 1");
}

// A file whose name has no UTF-8 form (on Windows, an unpaired surrogate) has no resource name: it's left out, and the
// other files of its folder are still served (the conversion used to throw while the folder was listed for the
// spelling check, failing every read there).
TEST(DirectoryMount, SkipsNamesWithoutAUtf8Form) {
    test::TempDir dir;
    dir.write("sub/good.lua", "good");
    dir.write("sub/deeper/x.lua", "x");
    try {
        std::wstring name = L"bad";
        name += static_cast<wchar_t>(0xD800);
        std::filesystem::create_directories(dir.path() / "sub" / (name + L"dir"));
        std::ofstream{dir.path() / "sub" / (name + L".lua")} << "bad";
        std::ofstream{dir.path() / "sub" / (name + L"dir") / L"y.lua"} << "y";
        if (!std::filesystem::exists(dir.path() / "sub" / (name + L".lua"))) GTEST_SKIP() << "no such names here";
    } catch (const std::exception& e) {
        GTEST_SKIP() << "can't make a file with an unpaired surrogate in its name: " << e.what();
    }
    for (const bool withSelection : {false, true}) {
        SCOPED_TRACE(withSelection ? "with a selection" : "without a selection");
        auto mount = withSelection ? std::make_shared<DirectoryMount>(dir.path(), ResourceSelection{.include = {"**"}})
                                   : std::make_shared<DirectoryMount>(dir.path());
        EXPECT_EQ(text(mount->read("sub/good.lua")), "good");
        EXPECT_TRUE(mount->exists("sub/good.lua"));
        EXPECT_EQ(mount->list("sub"), strings({"deeper/", "good.lua"}));
        EXPECT_EQ(mount->listFiles(""), strings({"sub/deeper/x.lua", "sub/good.lua"}));
        EXPECT_EQ(mount->list(""), strings({"sub/"}));
    }
}

// Names are case-sensitive, as in the embedded and .pak mounts, also where the file system isn't (Windows): a wrong
// case in any segment isn't found, so it fails in development too, and can't get around the selection.
TEST(DirectoryMount, NamesAreCaseSensitive) {
    test::TempDir dir;
    dir.write("Main.lua", "main");
    dir.write("Sub/Data.txt", "data");
    dir.write("resources.lua", "return {}");
    DirectoryMount mount{dir.path(), ResourceSelection{.include = {"**"}, .unmatched = {"resources.lua"}}};
    EXPECT_EQ(text(mount.read("Main.lua")), "main");
    EXPECT_EQ(text(mount.read("Sub/Data.txt")), "data");
    EXPECT_TRUE(mount.modified("Sub/Data.txt"));
    EXPECT_EQ(mount.list("Sub"), (std::vector<std::string>{"Data.txt"}));

    for (const char* wrong : {"main.lua", "MAIN.LUA", "sub/Data.txt", "Sub/data.txt", "RESOURCES.LUA"}) {
        EXPECT_FALSE(mount.read(wrong)) << wrong;
        EXPECT_FALSE(mount.exists(wrong)) << wrong;
        EXPECT_FALSE(mount.modified(wrong)) << wrong;
    }
    EXPECT_FALSE(mount.exists("sub"));
    EXPECT_TRUE(mount.list("sub").empty());

    // Renaming the file to another case: the old spelling goes, the new one works (the folder listing is redone).
    std::filesystem::rename(dir.path() / "Main.lua", dir.path() / "main.lua");
    EXPECT_FALSE(mount.read("Main.lua"));
    EXPECT_FALSE(mount.modified("Main.lua"));
    EXPECT_EQ(text(mount.read("main.lua")), "main");

    // A file added later is found by its exact name.
    dir.write("Sub/New.txt", "new");
    EXPECT_EQ(text(mount.read("Sub/New.txt")), "new");
    EXPECT_FALSE(mount.read("Sub/new.txt"));
}

// A file added within the same tick of its folder's time leaves that time unchanged: a miss in the cached listing
// lists the folder again (the file used to stay missing until something else changed the folder).
TEST(DirectoryMount, FindsAFileAddedWithoutChangingTheFolderTime) {
    test::TempDir dir;
    dir.write("ui/a.lua", "a");
    DirectoryMount mount{dir.path()};
    const std::filesystem::path folder = dir.path() / "ui";
    const auto time = std::filesystem::last_write_time(folder);
    EXPECT_FALSE(mount.exists("ui/b.lua")); // lists the folder
    dir.write("ui/b.lua", "b");
    std::error_code ec;
    std::filesystem::last_write_time(folder, time, ec); // as if in the same tick
    if (ec || std::filesystem::last_write_time(folder) != time)
        GTEST_SKIP() << "can't set a folder's time here: " << ec.message();
    EXPECT_TRUE(mount.exists("ui/b.lua"));
    EXPECT_EQ(text(mount.read("ui/b.lua")), "b");
    EXPECT_FALSE(mount.exists("ui/c.lua"));
}

// Whether a folder holds a selected file is kept per folder while the folder's time is unchanged, and the answer for a
// folder combines its own entries with its subfolders', each checked against its own time: a file added or removed
// deep down (which changes only its own folder's time) shows at once in every folder above it.
TEST(DirectoryMount, FoldersHoldingSelectedFilesFollowChangesDeepDown) {
    test::TempDir dir;
    dir.write("a/b/c/notes.txt", "not selected");
    dir.write("a/other/x.txt", "not selected");
    DirectoryMount mount{dir.path(), ResourceSelection{.include = {"**/*.lua"}}};
    // Older than a tick, so the kept answers are used, not listed again (FindsAFileAddedWithoutChangingTheFolderTime).
    for (const char* folder : {"a", "a/b", "a/b/c", "a/other", ""})
        dir.touch(folder, -10);
    for (int i = 0; i < 2; ++i) { // the second time from the kept answers
        EXPECT_FALSE(mount.exists("a"));
        EXPECT_EQ(mount.list(""), strings({}));
        EXPECT_EQ(mount.list("a"), strings({}));
    }
    dir.write("a/b/c/deep.lua", "deep");
    EXPECT_TRUE(mount.exists("a"));
    EXPECT_TRUE(mount.exists("a/b"));
    EXPECT_FALSE(mount.exists("a/other"));
    EXPECT_EQ(mount.list(""), strings({"a/"}));
    EXPECT_EQ(mount.list("a"), strings({"b/"}));
    dir.touch("a/b/c", -10);
    EXPECT_EQ(mount.list("a/b"), strings({"c/"}));
    dir.remove("a/b/c/deep.lua");
    EXPECT_FALSE(mount.exists("a"));
    EXPECT_EQ(mount.list(""), strings({}));
}

// One entry that can't be queried (a link to nothing) doesn't end the listing of a folder.
TEST(DirectoryMount, ListsPastAnEntryThatCantBeQueried) {
    test::TempDir dir;
    dir.write("b.txt", "b");
    dir.write("c/d.txt", "d");
    std::error_code ec;
    std::filesystem::create_symlink(dir.path() / "missing.txt", dir.path() / "a_dangling", ec);
    if (ec) GTEST_SKIP() << "can't create a symbolic link here: " << ec.message();
    DirectoryMount mount{dir.path()};
    EXPECT_EQ(mount.list(""), strings({"b.txt", "c/"}));
}

// A link to a folder (a symbolic link, or on Windows a junction) isn't listed or walked, as the manifest scan doesn't
// descend into one either: a link to the folder itself or a parent made listRecursive, which the hot-reload watcher
// polls, walk loop/loop/... until the path got too long (with two links, exponentially). A file is still read through
// a link by its name.
TEST(DirectoryMount, DoesntWalkIntoLinkedFolders) {
    test::TempDir dir;
    dir.write("a.lua", "a");
    dir.write("sub/b.lua", "b");
    if (!dir.linkFolder("sub/up", "") || !dir.linkFolder("sub/self", "sub"))
        GTEST_SKIP() << "can't create a link to a folder here";
    for (const bool withSelection : {false, true}) {
        SCOPED_TRACE(withSelection ? "with a selection" : "without a selection");
        auto mount = withSelection
                         ? std::make_shared<DirectoryMount>(dir.path(), ResourceSelection{.include = {"**/*.lua"}})
                         : std::make_shared<DirectoryMount>(dir.path());
        EXPECT_EQ(mount->list("sub"), strings({"b.lua"}));
        EXPECT_EQ(mount->listFiles(""), strings({"a.lua", "sub/b.lua"}));
        Resources res;
        ASSERT_TRUE(res.mount("app:/", mount));
        EXPECT_EQ(res.listRecursive("app:/"), strings({"app:/a.lua", "app:/sub/b.lua"}));
        EXPECT_EQ(text(mount->read("sub/up/a.lua")), "a");
        EXPECT_EQ(text(mount->read("sub/self/self/b.lua")), "b");
    }
}

// With a selection, listRecursive walks the folder once (it used to walk every subfolder again for each level above it,
// on every hot-reload poll), and still prunes folders no pattern can reach.
TEST(DirectoryMount, ListsASelectionRecursively) {
    test::TempDir dir;
    // A tree five levels deep, three folders wide: 364 folders with a file each.
    std::vector<std::string> expected;
    const auto fill = [&](const auto& self, const std::string& folder, int depth) -> void {
        const std::string file = folder + (folder.empty() ? "" : "/") + "f.lua";
        dir.write(file, "");
        expected.push_back("app:/" + file);
        dir.write(folder + (folder.empty() ? "" : "/") + "skip.txt", "");
        if (depth == 0) return;
        for (const char* sub : {"a", "b", "c"})
            self(self, folder + (folder.empty() ? "" : "/") + sub, depth - 1);
    };
    fill(fill, "", 5);
    dir.write("docs/x/readme.lua", "");
    std::ranges::sort(expected);
    Resources res;
    ASSERT_TRUE(res.mount(
        "app:/", std::make_shared<DirectoryMount>(
                     dir.path(), ResourceSelection{.include = {"a/**/*.lua", "b/**/*.lua", "c/**/*.lua", "f.lua"}})));
    EXPECT_EQ(res.listRecursive("app:/"), expected);
}

// ------------------------------------------------------------------ Resources

TEST(Resources, LaterMountsOverrideEarlierOnes) {
    Resources res;
    ASSERT_TRUE(res.mount("app:/", memory({{"x.lua", "base x"}, {"y.lua", "base y"}, {"lib/z.lua", "z"}}, "base")));
    ASSERT_TRUE(res.mount("app:/", memory({{"x.lua", "override x"}, {"w.lua", "w"}}, "override")));
    EXPECT_EQ(text(res.read("app:/x.lua")), "override x");
    EXPECT_EQ(text(res.read("app:/y.lua")), "base y") << "files the newer mount lacks fall back to the older one";
    EXPECT_EQ(text(res.read("app:/./lib/../w.lua")), "w") << "names are normalized";
    EXPECT_EQ(res.list("app:/"), strings({"lib/", "w.lua", "x.lua", "y.lua"}));
    EXPECT_EQ(res.listRecursive("app:/"), strings({"app:/lib/z.lua", "app:/w.lua", "app:/x.lua", "app:/y.lua"}));
    EXPECT_EQ(res.origin("app:/x.lua"), "memory 'override' x.lua");
    EXPECT_EQ(res.origin("app:/y.lua"), "memory 'base' y.lua");
    EXPECT_EQ(res.origin("app:/missing"), "");
}

TEST(Resources, PrefixesAreSeparateNamespaces) {
    Resources res;
    res.mount("app:/", memory({{"a.lua", "app"}}));
    res.mount("imlui:/", memory({{"b.lua", "lib"}}));
    EXPECT_EQ(text(res.read("app:/a.lua")), "app");
    EXPECT_FALSE(res.read("app:/b.lua"));
    EXPECT_FALSE(res.read("imlui:/a.lua"));
    EXPECT_FALSE(res.read("other:/a.lua"));
    EXPECT_EQ(res.prefixes(), strings({"app", "imlui"}));
}

TEST(Resources, MountsInsideAFolder) {
    Resources res;
    res.mount("imlui:/", memory({{"imlui.lua", "dsl"}}));
    res.mount("imlui:/assets/fonts/", memory({{"Roboto.ttf", "font"}}));
    EXPECT_EQ(text(res.read("imlui:/assets/fonts/Roboto.ttf")), "font");
    EXPECT_FALSE(res.read("imlui:/Roboto.ttf"));
    EXPECT_TRUE(res.exists("imlui:/assets")) << "folders holding a mount point exist";
    EXPECT_TRUE(res.exists("imlui:/assets/fonts/"));
    EXPECT_EQ(res.list("imlui:/"), strings({"assets/", "imlui.lua"}));
    EXPECT_EQ(res.list("imlui:/assets"), strings({"fonts/"}));
    EXPECT_EQ(res.listRecursive("imlui:/"), strings({"imlui:/assets/fonts/Roboto.ttf", "imlui:/imlui.lua"}));
    // A folder name with or without its trailing slash, as in list(): the folder, not its parent.
    EXPECT_EQ(res.listRecursive("imlui:/assets"), strings({"imlui:/assets/fonts/Roboto.ttf"}));
    EXPECT_EQ(res.listRecursive("imlui:/assets/fonts"), strings({"imlui:/assets/fonts/Roboto.ttf"}));
    EXPECT_EQ(res.listRecursive("imlui:/assets/fonts/"), strings({"imlui:/assets/fonts/Roboto.ttf"}));
    EXPECT_EQ(res.list("imlui:/assets/fonts"), res.list("imlui:/assets/fonts/"));
    EXPECT_TRUE(res.listRecursive("imlui:/imlui.lua").empty()); // a file isn't a folder
    EXPECT_TRUE(res.listRecursive("not a name").empty());
    EXPECT_EQ(res.describe(), strings({"imlui:/ <- memory 'memory'", "imlui:/assets/fonts/ <- memory 'memory'"}));
}

TEST(Resources, RejectsInvalidNamesAndMountPoints) {
    Resources res;
    EXPECT_FALSE(res.mount("C:/", memory({})));
    EXPECT_FALSE(res.mount("app:/", nullptr));
    EXPECT_TRUE(res.prefixes().empty());
    res.mount("app:/", memory({{"a.lua", "a"}}));
    EXPECT_FALSE(res.read("app:/../a.lua"));
    EXPECT_FALSE(res.exists("app:/../a.lua"));
    EXPECT_FALSE(res.read("a.lua"));
    EXPECT_TRUE(res.list("app:/..").empty());
}

TEST(Resources, ModifiedComesFromTheServingMount) {
    test::TempDir dir;
    dir.write("loose.lua", "loose");
    Resources res;
    res.mount("app:/", memory({{"loose.lua", "embedded"}, {"fixed.lua", "fixed"}}));
    res.mount("app:/", std::make_shared<DirectoryMount>(dir.path()));
    EXPECT_EQ(text(res.read("app:/loose.lua")), "loose");
    EXPECT_TRUE(res.modified("app:/loose.lua")) << "served by the directory: mutable";
    EXPECT_FALSE(res.modified("app:/fixed.lua")) << "served by the immutable mount";
    EXPECT_FALSE(res.modified("app:/missing.lua"));
    EXPECT_EQ(res.readText("app:/fixed.lua"), "fixed");
}

// ------------------------------------------------------------------ Lua

namespace {

/// A harness with `app:/` on a temporary folder holding the given files.
class LuaResourcesTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir.write("main.lua", R"(
            local Card = require("Card")
            return Column { id = "rootColumn", Card { title = "x" } })");
        m_dir.write("Card.lua", R"(
            return function(props) return Text { text = props.title, here = ui.resource.url("data.json") } end)");
        m_dir.write("data.json", "{\"n\": 1}");
        m_dir.write("sub/helper.lua", R"(
            return { url = function(n) local u = ui.resource.url(n); return u end,
                     read = function(n) local t = ui.resource.read(n); return t end,
                     tailUrl = function(n) return ui.resource.url(n) end })");
        m_dir.write("sub/info.txt", "sub info");
        // Code that uses the plain ui table (require("imlui")) instead of its script's own.
        m_dir.write("sub/plain.lua", R"(
            local plain = require("imlui")
            return { url = function(n) local u = plain.resource.url(n); return u end,
                     tailUrl = function(n) return plain.resource.url(n) end })");
        m_dir.write("pages/page.lua", R"(
            local data = ui.resource.read("data.txt")
            sharedFromPage = "page"
            ui.fromPage = true
            return { data = data, seen = sharedFromMain, img = Image { source = "pic.png" },
                     tailRead = function() return ui.resource.read("data.txt") end,
                     tailFreeRead = function() return resource.read("data.txt") end,
                     tailImage = function() return Image { "pic.png" } end,
                     tailUiImage = function() return ui.Image { "pic.png" } end })");
        m_dir.write("pages/data.txt", "page data");
        m_dir.write("Broken.lua", "local x = = 1");
        m_dir.write("pkg/init.lua", "return { name = 'pkg' }");
        m_frame.resources().mount("app:/", std::make_shared<DirectoryMount>(m_dir.path()));
    }

    /// Runs `code` in the UI environment; fails the test on any error.
    void run(std::string_view code) { ASSERT_EQ(m_frame.run(code), ""); }

    test::TempDir m_dir;
    test::HeadlessFrame m_frame;
};

} // namespace

TEST_F(LuaResourcesTest, ImluiIsLoadedThroughTheResourceSearcher) {
    run(R"(
        assert(package.loaded.imlui == ui)
        local info = debug.getinfo(ui.element, "S")
        assert(info.source == "@imlui:/imlui.lua", info.source)
    )");
    EXPECT_TRUE(m_frame.loadedResources().contains("imlui:/imlui.lua"));
}

TEST_F(LuaResourcesTest, LoadRunsTheEntryScriptAndRequiresComponentsNextToIt) {
    run(R"(
        root = ui.__load("app:/main.lua")
        assert(root.__type == "Column")
        local card = root[1]
        assert(card.__type == "Text" and card.text == "x")
        assert(card.here == "app:/data.json", card.here)
        assert(ui.__script == "app:/main.lua" and ui.__dir == "app:/")
        assert(package.loaded.Card ~= nil)
    )");
    EXPECT_TRUE(m_frame.loadedResources().contains("app:/main.lua"));
    EXPECT_TRUE(m_frame.loadedResources().contains("app:/Card.lua"));
}

TEST_F(LuaResourcesTest, RequireFindsInitFilesAndReportsWhatItTried) {
    run(R"(
        ui.__load("app:/main.lua")
        assert(require("pkg").name == "pkg")
        local ok, err = pcall(require, "Nope")
        assert(not ok)
        assert(err:find("no resource 'app:/Nope.lua'", 1, true), err)
        assert(err:find("no resource 'imlui:/Nope.lua'", 1, true), err)
    )");
}

// `require` reads resources only: Lua's stock file searcher (which on Windows looks in `.\?.lua` first) and the C
// searchers (DLLs on package.cpath) are gone, and package.path, package.cpath and package.loadlib can't bring them
// back. Run from a folder holding Stray.lua, the stock searcher would have found it.
TEST_F(LuaResourcesTest, RequireNeverReadsFilesOrLoadsLibraries) {
    test::TempDir cwd;
    cwd.write("Stray.lua", "strayRan = true; return {}");
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(cwd.path());
    const std::string error = m_frame.run(R"(
        assert(package.path == "" and package.cpath == "", "no search paths")
        assert(package.loadlib == nil, "no DLL loading")
        assert(#package.searchers == 3, "preload, the script folder's and imlui:/'s: " .. #package.searchers)

        local ok, err = pcall(require, "Stray")
        assert(not ok and strayRan == nil, "a .lua file in the current folder isn't a module")
        assert(err:find("module 'Stray' not found", 1, true), err)
        assert(err:find("no field package.preload['Stray']", 1, true), err)
        assert(err:find("no resource 'imlui:/Stray.lua'", 1, true), err)
        assert(not err:find("no file", 1, true), "no file searcher was asked: " .. err)

        -- A script that sets the path finds nothing either: no searcher reads it.
        package.path = "./?.lua"
        ok = pcall(require, "Stray")
        assert(not ok and strayRan == nil, "package.path is not searched")

        -- package.preload, for modules a host registers, still works.
        package.preload.Preloaded = function() return "preloaded" end
        assert(require("Preloaded") == "preloaded")
    )");
    std::filesystem::current_path(previous);
    EXPECT_EQ(error, "");
}

TEST_F(LuaResourcesTest, ErrorsNameTheResourceAndLine) {
    run(R"(
        ui.__load("app:/main.lua")
        local ok, err = pcall(require, "Broken")
        assert(not ok)
        assert(err:find("app:/Broken.lua:1:", 1, true), err)
        local ok2, err2 = pcall(ui.__load, "app:/missing.lua")
        assert(not ok2 and err2:find("resource 'app:/missing.lua' not found", 1, true), err2)
    )");
}

TEST_F(LuaResourcesTest, ResourceFunctionsResolveAgainstTheCallingScript) {
    run(R"(
        ui.__load("app:/main.lua")
        assert(ui.resource.url("data.json") == "app:/data.json")
        assert(ui.resource.url("imlui:/x/../imlui.lua") == "imlui:/imlui.lua")
        assert(ui.resource.read("data.json") == '{"n": 1}')
        assert(ui.resource.exists("data.json") and ui.resource.exists("sub") and not ui.resource.exists("nope"))
        local text, err = ui.resource.read("nope.txt")
        assert(text == nil and err:find("not found", 1, true), err)

        local helper = require("sub.helper")
        assert(helper.url("info.txt") == "app:/sub/info.txt", helper.url("info.txt"))
        assert(helper.url("../data.json") == "app:/data.json")
        assert(helper.read("info.txt") == "sub info")
        assert(helper.tailUrl("app:/a/../b") == "app:/b", "full names work in tail calls")
        assert(helper.tailUrl("info.txt") == "app:/sub/info.txt", "a script's own ui resolves in tail calls too")

        -- The plain ui table finds the calling script on the stack, which a tail call loses.
        local plain = require("sub.plain")
        assert(plain.url("info.txt") == "app:/sub/info.txt", plain.url("info.txt"))
        assert(plain.tailUrl("app:/a/../b") == "app:/b")
        local tailOk, tailErr = pcall(plain.tailUrl, "info.txt")
        assert(not tailOk and tailErr:find("tail call", 1, true), tailErr)
        -- So does code loaded from a string.
        local fromString = load([[return ui.resource.url("x")]], "=string", "t", ui.env)
        tailOk, tailErr = pcall(fromString)
        assert(not tailOk and tailErr:find("tail call", 1, true), tailErr)

        local ok, e = pcall(ui.resource.url, "../../outside")
        assert(not ok and e:find("invalid resource name", 1, true), e)
        ok, e = pcall(ui.resource.read, "C:/Windows/win.ini")
        assert(not ok, "OS paths are not resource names")
        ok, e = pcall(ui.resource.exists, 42)
        assert(not ok and e:find("must be a string", 1, true), e)
    )");
    EXPECT_TRUE(m_frame.loadedResources().contains("app:/sub/info.txt"));
}

TEST_F(LuaResourcesTest, ComponentsInSubfoldersResolveAgainstTheirOwnFolder) {
    run(R"(
        ui.__load("app:/main.lua")
        sharedFromMain = "main"
        local page = require("pages.page")
        assert(page.data == "page data", tostring(page.data))
        assert(page.img.__base == "app:/pages/page.lua", page.img.__base)
        assert(page.tailRead() == "page data", "tail calls resolve against the component's folder")
        assert(page.tailFreeRead() == "page data", "the free name `resource` is the script's own too")
        assert(page.tailImage().__base == "app:/pages/page.lua")
        assert(page.tailUiImage().__base == "app:/pages/page.lua")

        -- Scripts still share one environment: globals, ids and writes to ui.
        assert(page.seen == "main")
        assert(sharedFromPage == "page")
        assert(ui.fromPage == true)
        assert(ui.resource.url("data.txt") == "app:/data.txt", "code outside the scripts uses the main script")
    )");
    EXPECT_TRUE(m_frame.loadedResources().contains("app:/pages/data.txt"));
}

// A module's top level runs at app level, even when a component instance requires it first: its ids are app ids.
TEST_F(LuaResourcesTest, AModuleRequiredInsideAComponentRegistersItsIdsAtAppLevel) {
    m_dir.write("Shared.lua", R"(return Text { id = "sharedText" })");
    run(R"(
        ui.__load("app:/main.lua")
        local C = component(function(props, ids)
            componentIds = ids
            local shared = require("Shared")
            return Column { Text { id = "own" }, shared }
        end)
        root = C {}
        assert(ui.ids.sharedText == root[2] and componentIds.sharedText == nil)
        assert(componentIds.own == root[1] and ui.ids.own == nil)
    )");
    EXPECT_TRUE(luaui::errors().empty());
}

// Loading the script again starts without ids: the ids of the previous load aren't duplicates.
TEST_F(LuaResourcesTest, LoadingAgainStartsWithoutIds) {
    run(R"(
        first = ui.__load("app:/main.lua")
        second = ui.__load("app:/main.lua")
        assert(ui.ids.rootColumn == second)
    )");
    EXPECT_TRUE(luaui::errors().empty()) << luaui::errors().front();
}

TEST_F(LuaResourcesTest, ModulesInImluiPrefixLoadInTheGlobalEnvironment) {
    m_frame.resources().mount("imlui:/", memory({{"extra/mod.lua", "return { env = _ENV == _G, name = ... }"}}));
    run(R"(
        local m = require("extra.mod")
        assert(m.env, "imlui:/ modules run in the global environment")
        assert(m.name == "extra.mod")
    )");
}

// A folder named like a module's file (`x.lua/`) isn't the module: require goes on to `x/init.lua`, next to the script
// and in imlui:/ alike.
TEST_F(LuaResourcesTest, AFolderNamedLikeAModuleFileIsSkipped) {
    m_dir.write("dirmod.lua/readme.txt", "not a module");
    m_dir.write("dirmod/init.lua", "return { name = 'app init' }");
    m_frame.resources().mount("imlui:/", memory({{"libmod.lua/readme.txt", "not a module"},
                                                 {"libmod/init.lua", "return { name = 'imlui init' }"}}));
    run(R"(
        ui.__load("app:/main.lua")
        assert(require("dirmod").name == "app init")
        assert(require("libmod").name == "imlui init")
    )");
    EXPECT_TRUE(m_frame.loadedResources().contains("app:/dirmod/init.lua"));
    EXPECT_TRUE(m_frame.loadedResources().contains("imlui:/libmod/init.lua"));
}

// loadModule finds and loads a module with one read: the chunk and its name, nil and the names it tried, or false and
// the load error.
TEST_F(LuaResourcesTest, LoadModuleFindsAndLoads) {
    run(R"(
        local r = require("imlui.resource")
        local chunk, name = r.loadModule("pkg", "app:/")
        assert(type(chunk) == "function" and name == "app:/pkg/init.lua", tostring(name))
        assert(chunk().name == "pkg")
        local none, tried = r.loadModule("Nope", "app:/")
        assert(none == nil and tried:find("no resource 'app:/Nope.lua'", 1, true), tostring(tried))
        assert(tried:find("no resource 'app:/Nope/init.lua'", 1, true), tried)
        local bad, err = r.loadModule("Broken", "app:/")
        assert(bad == false and err:find("app:/Broken.lua:1:", 1, true), tostring(err))
    )");
}

// Free names in a resource script: `ui` and the functions bound to the script (Image, theme, ...) are cached, but a
// global a script assigns takes precedence as soon as it exists, and again not once it's gone; ids, ui members and
// globals are looked up each time.
TEST_F(LuaResourcesTest, FreeNamesFollowGlobalsAssignedLater) {
    m_dir.write("names.lua", R"(
        return { image = function() return Image end, ui = function() return ui end,
                 late = function() return lateGlobal end, id = function() return laterId end })");
    m_dir.write("shadow.lua", "Image = 'shadowed'; lateGlobal = 1; return true");
    run(R"(
        ui.__load("app:/main.lua")
        local env, plainUi = ui.env, ui
        local names = require("names")
        local bound = names.image()
        assert(type(bound) == "function" and bound ~= ui.Image)
        assert(names.image() == bound and names.ui().Image == bound and names.ui().state == ui.state)
        assert(names.late() == nil and names.id() == nil)
        require("shadow")
        assert(names.image() == "shadowed", tostring(names.image()))
        assert(names.late() == 1)
        env.Image = nil
        assert(names.image() == bound)
        local t = Text { id = "laterId" }
        assert(names.id() == t)
        env.ui = "mine"
        assert(names.ui() == "mine")
        env.ui = nil
        assert(names.ui().Image == bound and plainUi.ids.laterId == t)
    )");
}

namespace {

/// A mount whose every call throws, as one that runs out of memory would.
class ThrowingMount final : public Mount {
public:
    [[nodiscard]] std::optional<ResourceData> read(std::string_view) const override { throw std::bad_alloc{}; }
    [[nodiscard]] bool exists(std::string_view) const override { throw std::bad_alloc{}; }
    [[nodiscard]] std::vector<std::string> list(std::string_view) const override { throw std::bad_alloc{}; }
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view) const override { throw std::bad_alloc{}; }
    [[nodiscard]] std::string origin() const override { return "throwing"; }
};

} // namespace

// A C++ exception from a mount doesn't raise: the library functions return nil and a message (exists, false).
TEST_F(LuaResourcesTest, ExceptionsBecomeFailuresNotCrashes) {
    m_frame.resources().mount("app:/boom/", std::make_shared<ThrowingMount>());
    run(R"(
        local r = require("imlui.resource")
        local text, err = r.read("app:/boom/x.txt")
        assert(text == nil and err == "out of memory", tostring(err))
        local fn, loadErr = r.load("app:/boom/x.lua")
        assert(fn == nil and loadErr == "out of memory", tostring(loadErr))
        local name, findErr = r.find("x", "app:/boom/")
        assert(name == nil and findErr == "out of memory", tostring(findErr))
        assert(r.exists("app:/boom/x.txt") == false)
        assert(r.read("app:/data.json") == '{"n": 1}', "other names still work")
    )");
}
