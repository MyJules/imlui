// imlui_pack: builds a .pak archive (imlui.resources.pak_format) from files, for PakMount.
//
//     imlui_pack [--depfile <file>] <output.pak> <manifest.lua>          the files of a resource manifest
//                                                                        (imlui.tools.resource_manifest)
//     imlui_pack <output.pak> <mount point> <base folder> [file...]      files relative to the base folder, or all
//                                                                        of its files when none are given
//
// An archive has one mount point, so the groups of a manifest must share their prefix: the archive is mounted at
// the deepest folder that holds every group's mount point, and each group's files are stored under their folder
// below it (groups at `imlui:/` and `imlui:/fonts/` make an archive at `imlui:/` with `fonts/Roboto-Regular.ttf`).
// Each file is stored compressed with zstd when that saves enough, like embedded data
// (imlui.tools.resource_encoding), unless the manifest's `compress` says otherwise.
module;

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module imlui.tools.pack;

import imlui.resources.name;
import imlui.resources.pak_format;
import imlui.tools.depfile;
import imlui.tools.resource_encoding;
import imlui.tools.resource_manifest;
import imlui.tools.source_files;

export namespace imlui::tools {

/// The tool's main(); returns the process exit code.
[[nodiscard]] int runPack(int argc, char** argv);

/// The bytes of a .pak archive holding `files`, mounted at `mountPoint`; nullopt (with the reason in `error`) when
/// the mount point is invalid or a file can't be read.
[[nodiscard]] std::optional<std::vector<std::byte>> packFiles(std::string_view mountPoint,
                                                              const std::vector<SourceFile>& files, std::string& error);

/// The groups of `manifest` as the files of one archive: its mount point (the deepest folder holding every group's
/// mount point) and every file, named relative to it. nullopt (with the reason in `error`) when the groups have
/// different prefixes or two of them name the same file.
[[nodiscard]] std::optional<std::pair<std::string, std::vector<SourceFile>>>
mergeGroups(const ResourceManifest& manifest, std::string& error);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

namespace {

int fail(std::string_view message) {
    std::cerr << "imlui_pack: " << message << '\n';
    return 1;
}

/// The folder segments of mount point `point` (`imlui:/a/b/` -> {"a", "b"}).
std::vector<std::string> foldersOf(std::string_view point) {
    std::vector<std::string> out;
    std::string_view rest = point.substr(point.find(":/") + 2);
    while (!rest.empty()) {
        const std::size_t slash = rest.find('/');
        out.emplace_back(rest.substr(0, slash));
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
    }
    return out;
}

} // namespace

std::optional<std::pair<std::string, std::vector<SourceFile>>> mergeGroups(const ResourceManifest& manifest,
                                                                           std::string& error) {
    if (manifest.groups.empty()) {
        error = "the manifest has no groups";
        return std::nullopt;
    }
    const std::string& first = manifest.groups.front().mountPoint;
    const std::string prefix = first.substr(0, first.find(':'));
    std::vector<std::string> common = foldersOf(first);
    for (const auto& group : manifest.groups) {
        if (group.mountPoint.substr(0, group.mountPoint.find(':')) != prefix) {
            error = "a .pak archive has one mount point, so the groups of a manifest must share their prefix ('" +
                    first + "' and '" + group.mountPoint + "')";
            return std::nullopt;
        }
        const std::vector<std::string> folders = foldersOf(group.mountPoint);
        common.erase(std::ranges::mismatch(common, folders).in1, common.end());
    }
    std::string point = prefix + ":/";
    for (const auto& folder : common)
        point += folder + "/";

    std::vector<SourceFile> files;
    for (const auto& group : manifest.groups) {
        const std::string below = group.mountPoint.substr(point.size());
        for (const auto& file : group.files)
            files.push_back({below + file.path, file.file, file.compress});
    }
    std::ranges::sort(files, {}, &SourceFile::path);
    for (std::size_t i = 1; i < files.size(); ++i)
        if (files[i].path == files[i - 1].path) {
            error = "two groups put a file at '" + point + files[i].path + "'";
            return std::nullopt;
        }
    return std::pair{point, std::move(files)};
}

std::optional<std::vector<std::byte>> packFiles(std::string_view mountPoint, const std::vector<SourceFile>& files,
                                                std::string& error) {
    const auto point = normalizeName(mountPoint);
    if (!point) {
        error = "invalid mount point '" + std::string{mountPoint} + "'";
        return std::nullopt;
    }
    std::vector<EncodedResource> encoded;
    encoded.reserve(files.size());
    std::vector<PakFile> entries;
    for (const auto& f : files) {
        const auto normalized = normalizePath(f.path);
        if (!normalized || *normalized != f.path) {
            error = "'" + f.path + "' isn't a valid resource path";
            return std::nullopt;
        }
        const auto data = readFile(f.file);
        if (!data) {
            error = "can't read '" + utf8(f.file) + "'";
            return std::nullopt;
        }
        const auto& e = encoded.emplace_back(encodeResource(f.path, *data, f.compress));
        entries.push_back({f.path, e.stored, e.size, e.compressed});
    }
    std::string folder = *point;
    if (!folder.ends_with('/')) folder += '/';
    return writePak(folder, entries);
}

int runPack(int argc, char** argv) {
    std::vector<std::string_view> args{argv + 1, argv + argc};
    fs::path depfile;
    if (args.size() >= 2 && args.front() == "--depfile") {
        depfile = pathFromUtf8(args[1]);
        args.erase(args.begin(), args.begin() + 2);
    }
    if (args.size() < 2)
        return fail("usage: imlui_pack [--depfile <file>] <output.pak> <manifest.lua>\n"
                    "       imlui_pack <output.pak> <mount point> <base folder> [file...]");
    const fs::path output = pathFromUtf8(args[0]);
    std::string error;
    std::string mountPoint;
    std::vector<SourceFile> files;
    std::vector<fs::path> dependencies;
    if (args.size() == 2) {
        const auto manifest = loadManifest(pathFromUtf8(args[1]), error);
        if (!manifest) return fail(error);
        auto merged = mergeGroups(*manifest, error);
        if (!merged) return fail(error);
        mountPoint = std::move(merged->first);
        files = std::move(merged->second);
        dependencies = manifestDependencies(*manifest);
    } else {
        if (!depfile.empty()) return fail("--depfile needs a manifest");
        mountPoint = args[1];
        std::vector<fs::path> inputs;
        for (std::size_t i = 3; i < args.size(); ++i)
            inputs.push_back(pathFromUtf8(args[i]));
        auto collected = collectFiles(pathFromUtf8(args[2]), inputs, error);
        if (!collected) return fail(error);
        files = std::move(*collected);
    }
    const auto pak = packFiles(mountPoint, files, error);
    if (!pak) return fail(error);
    std::ofstream out{output, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(pak->data()), static_cast<std::streamsize>(pak->size()));
    if (!out) return fail("can't write '" + utf8(output) + "'");
    std::error_code ec;
    if (!depfile.empty() && !writeDepfile(depfile, fs::absolute(output, ec), dependencies))
        return fail("can't write '" + utf8(depfile) + "'");
    return 0;
}

} // namespace imlui::tools
