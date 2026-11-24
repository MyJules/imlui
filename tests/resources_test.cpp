// Tests for the resource system: name normalization and resolving (imlui.resources.name), DirectoryMount, mount
// stacking in Resources, and the Lua side (the module searchers, ui.__load, ui.resource.*) through the headless
// harness with an `app:/` mount on a temporary folder.
#include <gtest/gtest.h>

import imlui.resources;
import imlui.resources.name;
import imlui.resources.mount;
import imlui.resources.directory_mount;
import imlui.lua.errors;
import imlui.test.headless_frame;
import imlui.test.memory_mount;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

std::string text(std::optional<std::span<const std::byte>> bytes) {
    if (!bytes) return "<none>";
    return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
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
    EXPECT_EQ(text(first), "old") << "spans read earlier stay valid";
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
        local tailOk, tailErr = pcall(helper.tailUrl, "info.txt")
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

TEST_F(LuaResourcesTest, ModulesInImluiPrefixLoadInTheGlobalEnvironment) {
    m_frame.resources().mount("imlui:/", memory({{"extra/mod.lua", "return { env = _ENV == _G, name = ... }"}}));
    run(R"(
        local m = require("extra.mod")
        assert(m.env, "imlui:/ modules run in the global environment")
        assert(m.name == "extra.mod")
    )");
}
