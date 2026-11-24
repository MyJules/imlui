// Tests for resource manifests (imlui.tools.resource_manifest, read by imlui_rc and imlui_pack): glob patterns,
// groups, include/exclude, the compression override, errors (a missing prefix, a missing file, a path leaving the
// base folder, the sandbox), the dependency file and file listing, merging groups into one .pak, and end to end: the
// modules imlui_add_resources generates match their manifest, embedded (the library's in release builds, a forced
// EMBED ON copy of the smoke app's) and not embedded (the library's in dev builds, a forced EMBED OFF copy).
#include <gtest/gtest.h>

import std;
import imlui.resources;
import imlui.resources.mount;
import imlui.resources.packed_mount;
import imlui.resources.package;
import imlui.resources.pak_mount;
import imlui.embedded.imlui;
import imlui.embedded.smoke_embedded;
#ifdef IMLUI_TEST_DEV_PATHS
import imlui.embedded.smoke_sources;
#endif
import imlui.tools.depfile;
import imlui.resources.glob;
import imlui.tools.pack;
import imlui.tools.resource_encoding;
import imlui.tools.resource_manifest;
import imlui.tools.source_files;
import imlui.test.temp_dir;

using namespace imlui;

namespace {

namespace fs = std::filesystem;

/// Writes `source` as `<dir>/<name>` and parses it; the error, if any, in `error`.
std::optional<tools::ResourceManifest> manifest(const test::TempDir& dir, std::string_view source, std::string& error,
                                                std::string_view name = "resources.lua") {
    dir.write(name, source);
    return tools::loadManifest(dir.path() / name, error);
}

/// The paths of a group's files.
std::vector<std::string> paths(const tools::ResourceGroup& group) {
    std::vector<std::string> out;
    for (const auto& f : group.files)
        out.push_back(f.path);
    return out;
}

/// The error a manifest fails with ("" when it parses).
std::string failure(const test::TempDir& dir, std::string_view source) {
    std::string error;
    return manifest(dir, source, error) ? std::string{} : error;
}

std::string repeated(std::string_view seed, int times) {
    std::string out;
    for (int i = 0; i < times; ++i)
        out += seed;
    return out;
}

} // namespace

// ------------------------------------------------------------------ glob patterns

TEST(Glob, MatchesStarQuestionMarkAndDoubleStar) {
    EXPECT_TRUE(globMatch("*.lua", "main.lua"));
    EXPECT_FALSE(globMatch("*.lua", "ui/Card.lua")); // `*` stays in one folder
    EXPECT_FALSE(globMatch("*.lua", "main.luac"));
    EXPECT_TRUE(globMatch("ma?n.lua", "main.lua"));
    EXPECT_FALSE(globMatch("a?b", "a/b"));
    EXPECT_TRUE(globMatch("**", "a/b/c.txt"));
    EXPECT_TRUE(globMatch("**/*.lua", "main.lua")); // `**/` matches no folder too
    EXPECT_TRUE(globMatch("**/*.lua", "ui/cards/Card.lua"));
    EXPECT_FALSE(globMatch("**/*.lua", "ui/logo.png"));
    EXPECT_TRUE(globMatch("ui/**", "ui/a/b.png"));
    EXPECT_FALSE(globMatch("ui/**", "uix/b.png"));
    EXPECT_TRUE(globMatch("a/**/b.txt", "a/b.txt"));
    EXPECT_TRUE(globMatch("a/**/b.txt", "a/x/y/b.txt"));
    EXPECT_TRUE(globMatch("main.lua", "main.lua"));
    EXPECT_FALSE(globMatch("main.lua", "x/main.lua"));
}

TEST(Glob, RootsAndProblems) {
    EXPECT_EQ(globRoot("ui/icons/*.png"), "ui/icons");
    EXPECT_EQ(globRoot("ui/main.lua"), "ui");
    EXPECT_EQ(globRoot("*.lua"), "");
    EXPECT_EQ(globRoot("**/x/*.lua"), "");
    EXPECT_EQ(globProblem("ui/**/*.lua"), "");
    for (const char* bad : {"", "/abs/*.lua", "../*.lua", "a/../b", "a//b", "./a", "a\\b", "c:/x"})
        EXPECT_NE(globProblem(bad), "") << bad;
}

// ------------------------------------------------------------------ groups and selection

TEST(ResourceManifest, OneGroupWithFilesAndPatterns) {
    test::TempDir dir;
    dir.write("app/main.lua", "-- main");
    dir.write("app/ui/Card.lua", "-- card");
    dir.write("app/ui/deep/Badge.lua", "-- badge");
    dir.write("app/logo.png", "png");
    dir.write("app/notes.md", "# notes");
    dir.write("app/data/config.json", "{}");
    std::string error;
    const auto m = manifest(dir, R"(return {
        prefix = "app",
        base = "app",
        files = { "notes.md" },
        include = { "**/*.lua", "*.png" },
        exclude = "ui/deep/**",
    })",
                            error);
    ASSERT_TRUE(m) << error;
    ASSERT_EQ(m->groups.size(), 1U);
    const auto& g = m->groups[0];
    EXPECT_EQ(g.mountPoint, "app:/");
    EXPECT_TRUE(fs::equivalent(g.base, dir.path() / "app"));
    EXPECT_EQ(paths(g), (std::vector<std::string>{"logo.png", "main.lua", "notes.md", "ui/Card.lua"}));
    for (const auto& f : g.files) {
        EXPECT_TRUE(fs::equivalent(f.file, dir.path() / "app" / f.path)) << f.path;
        EXPECT_FALSE(f.compress) << f.path;
    }
    // The selection as written, for the dev folder mount (ResourceSelection), which then serves the same files.
    EXPECT_EQ(g.listed, (std::vector<std::string>{"notes.md"}));
    EXPECT_EQ(g.include, (std::vector<std::string>{"**/*.lua", "*.png"}));
    EXPECT_EQ(g.exclude, (std::vector<std::string>{"ui/deep/**"}));
    const ResourceSelection selection{.files = g.listed, .include = g.include, .exclude = g.exclude, .unmatched = {}};
    std::vector<std::string> served;
    for (const auto& entry : fs::recursive_directory_iterator(g.base))
        if (entry.is_regular_file()) {
            const auto relative = entry.path().lexically_relative(g.base).generic_u8string();
            std::string path{relative.begin(), relative.end()};
            if (selection.contains(path)) served.push_back(std::move(path));
        }
    std::ranges::sort(served);
    EXPECT_EQ(served, paths(g));
}

TEST(ResourceManifest, ListOfGroupsWithMountFolders) {
    test::TempDir dir;
    dir.write("lua/imlui.lua", "-- lib");
    dir.write("fonts/a.ttf", "font");
    dir.write("fonts/LICENSE.txt", "license");
    std::string error;
    const auto m = manifest(dir, R"(return {
        { prefix = "imlui", base = "lua", include = "**" },
        { prefix = "imlui", mount = "/fonts/", base = "fonts", files = { "a.ttf" } },
    })",
                            error);
    ASSERT_TRUE(m) << error;
    ASSERT_EQ(m->groups.size(), 2U);
    EXPECT_EQ(m->groups[0].mountPoint, "imlui:/");
    EXPECT_EQ(paths(m->groups[0]), (std::vector<std::string>{"imlui.lua"}));
    EXPECT_EQ(m->groups[1].mountPoint, "imlui:/fonts/");
    EXPECT_EQ(paths(m->groups[1]), (std::vector<std::string>{"a.ttf"}));
    EXPECT_TRUE(fs::equivalent(m->groups[1].base, dir.path() / "fonts"));
}

TEST(ResourceManifest, NeverMatchesItselfUnlessListed) {
    test::TempDir dir;
    dir.write("main.lua", "-- main");
    std::string error;
    auto m = manifest(dir, R"(return { prefix = "app", include = "**" })", error);
    ASSERT_TRUE(m) << error;
    EXPECT_EQ(paths(m->groups[0]), (std::vector<std::string>{"main.lua"}));

    m = manifest(dir, R"(return { prefix = "app", files = { "resources.lua" }, include = "**" })", error);
    ASSERT_TRUE(m) << error;
    EXPECT_EQ(paths(m->groups[0]), (std::vector<std::string>{"main.lua", "resources.lua"}));
}

TEST(ResourceManifest, NewFilesMatchingAPatternAreFound) {
    test::TempDir dir;
    dir.write("ui/a.lua", "-- a");
    std::string error;
    const std::string source = R"(return { prefix = "app", include = "ui/**/*.lua" })";
    auto before = manifest(dir, source, error);
    ASSERT_TRUE(before) << error;
    dir.write("ui/nested/b.lua", "-- b");
    auto after = manifest(dir, source, error);
    ASSERT_TRUE(after) << error;
    EXPECT_EQ(paths(after->groups[0]), (std::vector<std::string>{"ui/a.lua", "ui/nested/b.lua"}));
    // The listing (compared on every build) changes with the set of files.
    EXPECT_NE(tools::manifestListing(*before), tools::manifestListing(*after));
    EXPECT_NE(tools::manifestListing(*after).find("app:/ui/nested/b.lua "), std::string::npos);
}

TEST(ResourceManifest, CompressOverridesTheSizeRule) {
    test::TempDir dir;
    const std::string text = repeated("compress me please ", 50);
    dir.write("big.txt", text);
    dir.write("tiny.txt", "tiny");
    dir.write("photo.png", repeated("png", 100));
    std::string error;
    const auto m = manifest(dir, R"(return {
        prefix = "app",
        include = "*.*",
        compress = { ["big.txt"] = false, ["tiny.txt"] = true, ["photo.png"] = true },
    })",
                            error);
    ASSERT_TRUE(m) << error;
    std::map<std::string, std::optional<bool>> compress;
    for (const auto& f : m->groups[0].files)
        compress[f.path] = f.compress;
    EXPECT_EQ(compress["big.txt"], false);
    EXPECT_EQ(compress["tiny.txt"], true);
    EXPECT_EQ(compress["photo.png"], true);

    const auto bytes = [](std::string_view s) { return std::as_bytes(std::span{s.data(), s.size()}); };
    EXPECT_TRUE(tools::encodeResource("big.txt", bytes(text)).compressed); // the size rule alone
    EXPECT_FALSE(tools::encodeResource("big.txt", bytes(text), false).compressed);
    EXPECT_TRUE(tools::encodeResource("tiny.txt", bytes("tiny"), true).compressed);
    EXPECT_TRUE(tools::encodeResource("photo.png", bytes(repeated("png", 100)), true).compressed);
    EXPECT_FALSE(tools::encodeResource("empty.txt", bytes(""), true).compressed); // nothing to compress
}

// ------------------------------------------------------------------ errors

TEST(ResourceManifest, MissingPrefix) {
    test::TempDir dir;
    dir.write("main.lua", "");
    EXPECT_NE(failure(dir, R"(return { files = { "main.lua" } })").find("'prefix' is required"), std::string::npos);
    EXPECT_NE(failure(dir, R"(return { { prefix = "app", files = "main.lua" }, { files = "main.lua" } })")
                  .find("group 2: 'prefix' is required"),
              std::string::npos);
    EXPECT_NE(failure(dir, R"(return { prefix = "App", files = "main.lua" })").find("prefix 'App'"), std::string::npos);
}

TEST(ResourceManifest, FileNotFound) {
    test::TempDir dir;
    const std::string error = failure(dir, R"(return { prefix = "app", files = { "missing.lua" } })");
    EXPECT_NE(error.find("file 'missing.lua' not found"), std::string::npos) << error;
    EXPECT_NE(failure(dir, R"(return { prefix = "app", base = "nowhere", include = "**" })").find("base folder"),
              std::string::npos);
}

// Resource names are case-sensitive, so a listed file must be spelled as on disk: on a file system that ignores case
// (Windows, macOS) `Main.lua` would find main.lua and embed it as `Main.lua`, which the dev folder mount (strict about
// case) wouldn't serve, and a pattern would add `main.lua` a second time.
TEST(ResourceManifest, ListedFilesMustBeSpelledAsOnDisk) {
    test::TempDir dir;
    dir.write("ui/main.lua", "-- main");
    const std::string error =
        failure(dir, R"(return { prefix = "app", files = { "UI/Main.lua" }, include = "**/*.lua" })");
    EXPECT_NE(error, "");
    if (fs::exists(dir.path() / "UI" / "MAIN.LUA")) // case-insensitive: found, but spelled differently
        EXPECT_NE(error.find("is spelled 'ui/main.lua' on disk"), std::string::npos) << error;
    EXPECT_EQ(failure(dir, R"(return { prefix = "app", files = { "ui/main.lua" }, include = "**/*.lua" })"), "");
}

TEST(ResourceManifest, PathsCantLeaveTheBaseFolder) {
    test::TempDir dir;
    dir.write("secret.txt", "secret");
    dir.write("app/main.lua", "");
    const std::string error = failure(dir, R"(return { prefix = "app", base = "app", files = { "../secret.txt" } })");
    EXPECT_NE(error.find("outside the base folder"), std::string::npos) << error;
    EXPECT_NE(failure(dir, R"(return { prefix = "app", base = "app", include = "../*" })").find("pattern '../*'"),
              std::string::npos);
    EXPECT_NE(failure(dir, R"(return { prefix = "app", base = "app", files = { "/abs.txt" } })"), "");
    EXPECT_NE(failure(dir, R"(return { prefix = "app", mount = "../up", files = "app/main.lua" })").find("mount"),
              std::string::npos);
}

// Only a first path element `..` leaves the folder: names that merely start with two dots are ordinary files.
TEST(ResourceManifest, NamesStartingWithTwoDotsStayInside) {
    test::TempDir dir;
    dir.write("app/..foo.png", "png");
    dir.write("app/..config/x.lua", "-- x");
    std::string error;
    const auto m = manifest(
        dir, R"(return { prefix = "app", base = "app", files = { "..foo.png" }, include = "**/*.lua" })", error);
    ASSERT_TRUE(m) << error;
    EXPECT_EQ(paths(m->groups[0]), (std::vector<std::string>{"..config/x.lua", "..foo.png"}));

    const auto collected = tools::collectFiles(dir.path() / "app", {}, error);
    ASSERT_TRUE(collected) << error;
    ASSERT_EQ(collected->size(), 2U);
    EXPECT_EQ((*collected)[0].path, "..config/x.lua");
    EXPECT_EQ((*collected)[1].path, "..foo.png");
    EXPECT_FALSE(tools::collectFiles(dir.path() / "app", {dir.path() / "resources.lua"}, error));
}

// imlui_pack's by-hand mode packs every file under a folder: an entry that can't be queried (a link to nothing) isn't a
// file and doesn't end the walk early (which would make an incomplete archive), and a folder that doesn't exist fails.
TEST(SourceFiles, CollectsPastEntriesThatCantBeQueried) {
    test::TempDir dir;
    dir.write("app/a.txt", "a");
    dir.write("app/z/z.txt", "z");
    std::error_code ec;
    fs::create_symlink(dir.path() / "app" / "missing.txt", dir.path() / "app" / "b_dangling", ec);
    if (ec) GTEST_SKIP() << "can't create a symbolic link here: " << ec.message();
    std::string error;
    const auto collected = tools::collectFiles(dir.path() / "app", {}, error);
    ASSERT_TRUE(collected) << error;
    std::vector<std::string> names;
    for (const auto& f : *collected)
        names.push_back(f.path);
    EXPECT_EQ(names, (std::vector<std::string>{"a.txt", "z/z.txt"}));
    EXPECT_FALSE(tools::collectFiles(dir.path() / "nowhere", {}, error));
}

// The tools replace their outputs through a temporary file, so an interrupted write never leaves a partial one.
TEST(SourceFiles, ReplaceFileWritesWholeFilesOnly) {
    test::TempDir dir;
    const fs::path file = dir.path() / "out.bin";
    const auto bytes = [](std::string_view s) { return std::as_bytes(std::span{s.data(), s.size()}); };
    ASSERT_TRUE(tools::replaceFile(file, bytes("first")));
    ASSERT_TRUE(tools::replaceFile(file, bytes("second, longer")));
    EXPECT_EQ(tools::readFile(file)->size(), std::string_view{"second, longer"}.size());
    EXPECT_FALSE(fs::exists(dir.path() / "out.bin.tmp"));
    std::string error;
    EXPECT_FALSE(tools::replaceFile(dir.path() / "no" / "such" / "folder.bin", bytes("x"), &error));
    EXPECT_NE(error, "");
}

// A file an `include` pattern matches is checked like a `files` entry: a link to a file outside the base folder is
// rejected.
TEST(ResourceManifest, IncludedLinksCantLeaveTheBaseFolder) {
    test::TempDir dir;
    dir.write("secret.txt", "secret");
    dir.write("app/main.lua", "");
    std::error_code ec;
    fs::create_symlink(dir.path() / "secret.txt", dir.path() / "app" / "secret.txt", ec);
    if (ec) GTEST_SKIP() << "can't create a symbolic link here: " << ec.message();
    const std::string error = failure(dir, R"(return { prefix = "app", base = "app", include = "*" })");
    EXPECT_NE(error.find("'secret.txt' is outside the base folder"), std::string::npos) << error;
    EXPECT_EQ(failure(dir, R"(return { prefix = "app", base = "app", include = "*.lua" })"), "");
}

TEST(ResourceManifest, RunsInASandbox) {
    test::TempDir dir;
    dir.write("main.lua", "");
    for (const char* code : {"require('x')", "io.open('x')", "os.exit(1)", "load('return 1')()", "dofile('x')",
                             "loadfile('x')", "string.rep('x', 2)", "collectgarbage()"}) {
        const std::string error = failure(dir, std::string{code} + "\nreturn { prefix = 'app', files = 'main.lua' }");
        // A runtime error, reported at the manifest's file:line.
        EXPECT_NE(error.find("resources.lua:1:"), std::string::npos) << code << ": " << error;
    }
    // The base library is there: a manifest can compute its lists.
    std::string error;
    const auto m = manifest(dir, R"(
        local files = {}
        for _, name in ipairs({ "main" }) do files[#files + 1] = name .. ".lua" end
        return { prefix = "app", files = files })",
                            error);
    EXPECT_TRUE(m) << error;
    // Bytecode is refused.
    EXPECT_NE(failure(dir, "\x1bLua"), "");
}

TEST(ResourceManifest, RunawayManifestsFail) {
    test::TempDir dir;
    // An endless loop hits the instruction limit instead of hanging the build.
    EXPECT_NE(failure(dir, "while true do end").find("runs too long"), std::string::npos);
    // A runaway string or table hits the memory limit.
    EXPECT_NE(failure(dir, "local s = 'x'\nfor i = 1, 40 do s = s .. s end").find("not enough memory"),
              std::string::npos);
    // Strings that aren't UTF-8 are reported, not thrown on as paths (found by the `manifest` fuzz target).
    for (const char* group : {"files = 'a\xff.lua'", "base = '\xc0\xaf'", "include = '\xed\xa0\x80/**'",
                              "files = 'x', compress = { ['\xf4\x90\x80\x80'] = true }", "mount = '\xe2\x82'"})
        EXPECT_NE(failure(dir, std::string{"return { prefix = 'app', "} + group + " }").find("isn't valid UTF-8"),
                  std::string::npos)
            << group;
    // Both halves of parseManifest, as the fuzz target calls them.
    std::string error;
    const auto groups = imlui::tools::evaluateManifest("return { prefix = 'app', base = 'nowhere', files = 'x' }",
                                                       dir.path() / "resources.lua", error);
    ASSERT_TRUE(groups) << error;
    ASSERT_EQ(groups->size(), 1u);
    EXPECT_EQ((*groups)[0].base, "nowhere");
    EXPECT_FALSE(imlui::tools::resolveManifest(*groups, dir.path() / "resources.lua", error));
    EXPECT_NE(error.find("base folder 'nowhere' doesn't exist"), std::string::npos) << error;
}

TEST(ResourceManifest, BadShapesAreReported) {
    test::TempDir dir;
    dir.write("main.lua", "");
    const auto expectError = [&](std::string_view source, std::string_view expected) {
        const std::string error = failure(dir, source);
        EXPECT_NE(error.find(expected), std::string::npos) << source << " -> " << error;
    };
    expectError("return 42", "must return a group table");
    expectError("return {", "resources.lua:1:"); // syntax error
    expectError(R"(return { prefix = "app", file = "main.lua" })", "unknown field 'file'");
    expectError(R"(return { prefix = "app", files = { 1 } })", "'files' must be a string or a list of strings");
    expectError(R"(return { prefix = "app", mount = 3, files = "main.lua" })", "'mount' must be a string");
    expectError(R"(return { prefix = "app", files = "main.lua", compress = { ["main.lua"] = 1 } })",
                "'compress' must map file paths");
    expectError(R"(return { prefix = "app", files = "main.lua", compress = { ["other.lua"] = false } })",
                "compress: 'other.lua' isn't one of the group's files");
    expectError(R"(return { prefix = "app" })", "selects no files");
    expectError(R"(return { { prefix = "app", files = "main.lua" }, prefix = "app" })", "a list of groups");
    expectError(R"(return { "main.lua" })", "group 1: must be a table");
}

// ------------------------------------------------------------------ dependency file

TEST(ResourceManifest, DependenciesListTheManifestFilesAndScannedFolders) {
    test::TempDir dir;
    dir.write("app/main.lua", "");
    dir.write("app/ui/Card.lua", "");
    dir.write("app/explicit.txt", "");
    std::string error;
    const auto m =
        manifest(dir, R"(return { prefix = "app", base = "app", files = "explicit.txt", include = "ui/**" })", error);
    ASSERT_TRUE(m) << error;
    const auto deps = tools::manifestDependencies(*m);
    const auto has = [&](const fs::path& p) {
        return std::ranges::any_of(deps, [&](const fs::path& d) { return fs::equivalent(d, p); });
    };
    EXPECT_TRUE(has(dir.path() / "resources.lua"));
    EXPECT_TRUE(has(dir.path() / "app" / "explicit.txt"));
    EXPECT_TRUE(has(dir.path() / "app" / "ui" / "Card.lua"));
    EXPECT_TRUE(has(dir.path() / "app" / "ui"));        // scanned: a new file there changes the selection
    EXPECT_FALSE(has(dir.path() / "app" / "main.lua")); // not selected
    EXPECT_FALSE(has(dir.path() / "app"));              // only ui/ is scanned
    EXPECT_EQ(deps.size(), 4U);
}

TEST(Depfile, WritesOneRuleWithEscapedPaths) {
    const std::vector<fs::path> deps{fs::path{"C:/src/a b.lua"}, fs::path{"C:/src/c#$.txt"}};
    EXPECT_EQ(tools::depfileText(fs::path{"C:/build/out.cppm"}, deps),
              "C:/build/out.cppm: \\\n  C:/src/a\\ b.lua \\\n  C:/src/c\\#$$.txt\n");
}

// ------------------------------------------------------------------ .pak archives of several groups

TEST(ResourceManifest, GroupsMergeIntoOneArchiveUnderTheirCommonFolder) {
    test::TempDir dir;
    dir.write("lua/imlui.lua", "-- lib");
    dir.write("fonts/a.ttf", "font");
    std::string error;
    auto m = manifest(dir, R"(return {
        { prefix = "imlui", mount = "fonts", base = "fonts", include = "**" },
        { prefix = "imlui", mount = "fonts/extra", base = "lua", include = "**" },
    })",
                      error);
    ASSERT_TRUE(m) << error;
    auto merged = tools::mergeGroups(*m, error);
    ASSERT_TRUE(merged) << error;
    EXPECT_EQ(merged->first, "imlui:/fonts/");
    std::vector<std::string> names;
    for (const auto& f : merged->second)
        names.push_back(f.path);
    EXPECT_EQ(names, (std::vector<std::string>{"a.ttf", "extra/imlui.lua"}));

    m = manifest(dir, R"(return {
        { prefix = "imlui", base = "lua", include = "**" },
        { prefix = "imlui", mount = "fonts", base = "fonts", include = "**" },
    })",
                 error);
    ASSERT_TRUE(m) << error;
    merged = tools::mergeGroups(*m, error);
    ASSERT_TRUE(merged) << error;
    EXPECT_EQ(merged->first, "imlui:/");
    const auto pak = tools::packFiles(merged->first, merged->second, error);
    ASSERT_TRUE(pak) << error;
    const auto mount = PakMount::fromBytes(*pak, "merged");
    ASSERT_TRUE(mount);
    EXPECT_EQ(mount->mountPoint(), "imlui:/");
    EXPECT_TRUE(mount->exists("fonts/a.ttf"));
    EXPECT_TRUE(mount->exists("imlui.lua"));
}

TEST(ResourceManifest, ArchivesNeedOnePrefixAndDistinctFiles) {
    test::TempDir dir;
    dir.write("a/x.txt", "a");
    dir.write("b/x.txt", "b");
    std::string error;
    auto m = manifest(dir, R"(return {
        { prefix = "app", base = "a", include = "**" },
        { prefix = "imlui", base = "b", include = "**" },
    })",
                      error);
    ASSERT_TRUE(m) << error;
    EXPECT_FALSE(tools::mergeGroups(*m, error));
    EXPECT_NE(error.find("share their prefix"), std::string::npos) << error;

    m = manifest(dir, R"(return {
        { prefix = "app", base = "a", include = "**" },
        { prefix = "app", base = "b", include = "**" },
    })",
                 error);
    ASSERT_TRUE(m) << error;
    EXPECT_FALSE(tools::mergeGroups(*m, error));
    EXPECT_NE(error.find("app:/x.txt"), std::string::npos) << error;
}

// ------------------------------------------------------------------ end to end: generated modules match their manifest

namespace {

/// Checks generated `packages` against manifest `file`: one package per group, at its mount point. Embedded, each
/// holds exactly the group's files (and, with IMLUI_DEV_PATHS=ON, names the group's base folder); not embedded, it
/// has no files and names the base folder, its only copy.
void expectMatchesManifest(const std::vector<ResourcePackage>& packages, const fs::path& file, bool embedded) {
    std::string error;
    const auto m = tools::loadManifest(file, error);
    ASSERT_TRUE(m) << error;
    ASSERT_EQ(packages.size(), m->groups.size());
    for (std::size_t i = 0; i < packages.size(); ++i) {
        const auto& group = m->groups[i];
        EXPECT_EQ(packages[i].mountPoint, group.mountPoint);
        if (!embedded) {
            EXPECT_EQ(packages[i].files, nullptr) << group.mountPoint;
            ASSERT_FALSE(packages[i].sourceDir.empty()) << group.mountPoint;
            EXPECT_TRUE(fs::equivalent(fs::path{packages[i].sourceDir}, group.base)) << group.mountPoint;
            continue;
        }
        const auto* files = dynamic_cast<const PackedMount*>(packages[i].files.get());
        ASSERT_NE(files, nullptr) << group.mountPoint;
        std::vector<std::string> stored;
        for (const auto& e : files->entries())
            stored.push_back(e.path);
        std::ranges::sort(stored);
        EXPECT_EQ(stored, paths(group)) << group.mountPoint;
        if (!packages[i].sourceDir.empty()) // IMLUI_DEV_PATHS=ON: the group's base folder
            EXPECT_TRUE(fs::equivalent(fs::path{packages[i].sourceDir}, group.base));
    }
}

#ifdef IMLUI_TEST_EMBED_RESOURCES
constexpr bool libraryEmbedded = true;
#else
constexpr bool libraryEmbedded = false;
#endif

} // namespace

// imlui.embedded.imlui (target imlui) follows IMLUI_EMBED_RESOURCES: the embedded shape in ship builds, the source
// folders in dev builds (the forced copies below cover the other shape in every configuration; an install's embedded
// copy, imlui_files_install, is checked by the package tests).
TEST(ResourceManifest, EmbeddedLibraryMatchesItsManifest) {
    expectMatchesManifest(embedded::imlui::packages(), fs::path{IMLUI_RESOURCES_DIR} / "resources.lua",
                          libraryEmbedded);
    // With a source folder, each package carries its group's selection; the manifest lies in the first group's
    // folder, where no pattern matches it (no imlui:/resources.lua in dev builds either).
    const auto packages = embedded::imlui::packages();
    if (!packages[0].sourceDir.empty()) {
        ASSERT_TRUE(packages[0].selection && packages[1].selection);
        EXPECT_EQ(packages[0].selection->include, std::vector<std::string>{"**"});
        EXPECT_EQ(packages[0].selection->unmatched, std::vector<std::string>{"resources.lua"});
        EXPECT_FALSE(packages[0].selection->contains("resources.lua"));
        EXPECT_TRUE(packages[1].selection->unmatched.empty());
        EXPECT_TRUE(packages[1].selection->contains("Roboto-Regular.ttf"));
        EXPECT_FALSE(packages[1].selection->contains("README.md"));
    } else {
        EXPECT_FALSE(packages[0].selection); // nothing to select from, and no source paths in a release binary
    }
    std::string error;
    const auto m = tools::loadManifest(fs::path{IMLUI_RESOURCES_DIR} / "resources.lua", error);
    ASSERT_TRUE(m) << error;
    EXPECT_FALSE(std::ranges::any_of(m->groups, [](const tools::ResourceGroup& g) {
        return std::ranges::any_of(g.files, [](const tools::SourceFile& f) { return f.path == "resources.lua"; });
    }));
}

// imlui_add_resources(... EMBED ON) embeds whatever IMLUI_EMBED_RESOURCES says.
TEST(ResourceManifest, ForcedEmbeddingMatchesItsManifest) {
    expectMatchesManifest(embedded::smoke_embedded::packages(), fs::path{IMLUI_SMOKE_APP_DIR} / "resources.lua", true);
}

#ifdef IMLUI_TEST_DEV_PATHS
// imlui_add_resources(... EMBED OFF): the same module, packages with a mount point and a source folder only, and
// mount() mounts those folders.
TEST(ResourceManifest, NotEmbeddedNamesTheSourceFolders) {
    const auto packages = embedded::smoke_sources::packages();
    expectMatchesManifest(packages, fs::path{IMLUI_SMOKE_APP_DIR} / "resources.lua", false);
    Resources res;
    embedded::smoke_sources::mount(res);
    EXPECT_TRUE(res.exists("app:/main.lua"));
    EXPECT_TRUE(res.exists("app:/images/rgba_4x2.png"));
    // The folders serve what the manifest selects, as embedded: no pattern matches the manifest, and the image
    // group's folder (tests/data) shows only its one listed file, not the rest of the folder.
    ASSERT_TRUE(packages[0].selection && packages[1].selection);
    EXPECT_EQ(packages[0].selection->unmatched, std::vector<std::string>{"resources.lua"});
    EXPECT_EQ(packages[1].selection->unmatched, std::vector<std::string>{"smoke_app/resources.lua"});
    EXPECT_EQ(packages[1].selection->files, std::vector<std::string>{"rgba_4x2.png"});
    EXPECT_FALSE(res.exists("app:/resources.lua"));
    EXPECT_FALSE(res.read("app:/images/smoke_app/resources.lua"));
    EXPECT_FALSE(res.exists("app:/images/smoke_app/main.lua"));
    EXPECT_FALSE(res.exists("app:/images/orange_8x8.jpg"));
    EXPECT_EQ(res.list("app:/images/"), std::vector<std::string>{"rgba_4x2.png"});
    EXPECT_TRUE(res.modified("app:/main.lua")); // directories: hot reload sees changes
    EXPECT_TRUE(res.origin("app:/main.lua").starts_with("directory '"));
}
#endif
