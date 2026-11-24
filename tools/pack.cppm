// imlui_pack: builds a .pak archive (imlui.resources.pak_format) from files, for PakMount.
//
//     imlui_pack <output.pak> <mount point> <base folder> [file...]   files relative to the base folder, or all
//                                                                      of its files when none are given
//     imlui_pack <output.pak> @<spec file>                             a spec written by imlui_add_pak
//
// Each file is stored compressed with zstd when that saves enough, like embedded data
// (imlui.tools.resource_encoding).
module;

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module imlui.tools.pack;

import imlui.resources.name;
import imlui.resources.pak_format;
import imlui.tools.resource_encoding;
import imlui.tools.resource_spec;

export namespace imlui::tools {

/// The tool's main(); returns the process exit code.
[[nodiscard]] int runPack(int argc, char** argv);

/// The bytes of a .pak archive holding `files`, mounted at `mountPoint`; nullopt (with the reason in `error`) when
/// the mount point is invalid or a file can't be read.
[[nodiscard]] std::optional<std::vector<std::byte>> packFiles(std::string_view mountPoint,
                                                              const std::vector<SourceFile>& files, std::string& error);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

namespace {

int fail(std::string_view message) {
    std::cerr << "imlui_pack: " << message << '\n';
    return 1;
}

} // namespace

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
        const auto& e = encoded.emplace_back(encodeResource(f.path, *data));
        entries.push_back({f.path, e.stored, e.size, e.compressed});
    }
    std::string folder = *point;
    if (!folder.ends_with('/')) folder += '/';
    return writePak(folder, entries);
}

int runPack(int argc, char** argv) {
    if (argc < 3)
        return fail("usage: imlui_pack <output.pak> <mount point> <base folder> [file...]\n"
                    "       imlui_pack <output.pak> @<spec file>");
    const fs::path output{argv[1]};
    const std::string_view second{argv[2]};
    std::string error;
    std::string mountPoint;
    std::vector<SourceFile> files;
    if (second.starts_with('@')) {
        auto spec = readSpec(fs::path{second.substr(1)}, error);
        if (!spec) return fail(error);
        mountPoint = spec->mountPoint;
        files = std::move(spec->files);
    } else {
        if (argc < 4) return fail("missing the base folder");
        mountPoint = second;
        std::vector<fs::path> inputs;
        for (int i = 4; i < argc; ++i)
            inputs.emplace_back(argv[i]);
        auto collected = collectFiles(fs::path{argv[3]}, inputs, error);
        if (!collected) return fail(error);
        files = std::move(*collected);
    }
    const auto pak = packFiles(mountPoint, files, error);
    if (!pak) return fail(error);
    std::ofstream out{output, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(pak->data()), static_cast<std::streamsize>(pak->size()));
    if (!out) return fail("can't write '" + utf8(output) + "'");
    return 0;
}

} // namespace imlui::tools
