// The input of the resource tools: which files go into an embedded block or a .pak archive, under which names.
module;

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module imlui.tools.resource_spec;

export namespace imlui::tools {

/// One input file and its path in the mount.
struct SourceFile {
    std::string path; ///< relative to the base folder, `/`-separated (`fonts/Roboto-Regular.ttf`)
    std::filesystem::path file;
};

/// What to pack. CMake writes it as a UTF-8 text file, one `key=value` per line:
///
///     name=imlui            identifier of the generated module (imlui_rc only)
///     mount=imlui:/         the mount point
///     base=E:/x/lua         the folder the paths are relative to
///     source=E:/x/lua       optional: the dev source folder baked into the module (imlui_rc only; dev builds)
///     file=E:/x/lua/a.lua   one line per file (absolute, or relative to base)
struct ResourceSpec {
    std::string name;
    std::string mountPoint;
    std::filesystem::path base;
    std::string sourceDir;         ///< UTF-8, "" unless the build has IMLUI_DEV_PATHS=ON
    std::vector<SourceFile> files; ///< sorted by path
};

/// Reads a spec file. nullopt, with the reason in `error`, when it can't be read, a key is unknown, or a file
/// isn't a regular file under base.
[[nodiscard]] std::optional<ResourceSpec> readSpec(const std::filesystem::path& specFile, std::string& error);

/// Names `files` relative to `base`, or every regular file under `base` (recursively) when `files` is empty.
/// nullopt, with the reason in `error`, when a file is missing, outside `base`, or two files get the same path.
[[nodiscard]] std::optional<std::vector<SourceFile>>
collectFiles(const std::filesystem::path& base, const std::vector<std::filesystem::path>& files, std::string& error);

/// The contents of `file`; nullopt when it can't be read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& file);

/// A UTF-8 string as a path, and back (on Windows a plain std::string would be read in the ANSI code page).
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view s);
[[nodiscard]] std::string utf8(const std::filesystem::path& p);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

fs::path pathFromUtf8(std::string_view s) { return fs::path{std::u8string{s.begin(), s.end()}}; }

std::string utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string{s.begin(), s.end()};
}

std::optional<std::vector<std::byte>> readFile(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    std::ifstream in{file, std::ios::binary};
    if (ec || !in) return std::nullopt;
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) return std::nullopt;
    return bytes;
}

std::optional<std::vector<SourceFile>> collectFiles(const fs::path& base, const std::vector<fs::path>& files,
                                                    std::string& error) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(base, ec);
    if (ec || !fs::is_directory(root, ec)) {
        error = "base folder '" + utf8(base) + "' doesn't exist";
        return std::nullopt;
    }
    std::vector<fs::path> inputs = files;
    if (inputs.empty())
        for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec))
            if (it->is_regular_file(ec)) inputs.push_back(it->path());

    std::vector<SourceFile> out;
    for (const auto& input : inputs) {
        const fs::path file = fs::weakly_canonical(input.is_absolute() ? input : root / input, ec);
        if (ec || !fs::is_regular_file(file, ec)) {
            error = "'" + utf8(input) + "' isn't a file";
            return std::nullopt;
        }
        const fs::path relative = file.lexically_relative(root);
        const std::string path = utf8(relative);
        if (relative.empty() || path.starts_with("..") || relative.is_absolute()) {
            error = "'" + utf8(input) + "' isn't under the base folder '" + utf8(root) + "'";
            return std::nullopt;
        }
        out.push_back({path, file});
    }
    std::ranges::sort(out, {}, &SourceFile::path);
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i].path == out[i - 1].path) {
            error = "'" + out[i].path + "' is listed twice";
            return std::nullopt;
        }
    return out;
}

std::optional<ResourceSpec> readSpec(const fs::path& specFile, std::string& error) {
    std::ifstream in{specFile, std::ios::binary};
    if (!in) {
        error = "can't read '" + utf8(specFile) + "'";
        return std::nullopt;
    }
    ResourceSpec spec;
    std::vector<fs::path> files;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        const std::string_view key = std::string_view{line}.substr(0, eq);
        const std::string_view value =
            eq == std::string::npos ? std::string_view{} : std::string_view{line}.substr(eq + 1);
        if (key == "name")
            spec.name = value;
        else if (key == "mount")
            spec.mountPoint = value;
        else if (key == "base")
            spec.base = pathFromUtf8(value);
        else if (key == "source")
            spec.sourceDir = value;
        else if (key == "file")
            files.push_back(pathFromUtf8(value));
        else {
            error = utf8(specFile) + ": unknown line '" + line + "'";
            return std::nullopt;
        }
    }
    // An empty file list means "the whole base folder" to collectFiles(); a spec lists its files explicitly.
    if (files.empty()) return spec;
    auto collected = collectFiles(spec.base, files, error);
    if (!collected) return std::nullopt;
    spec.files = std::move(*collected);
    return spec;
}

} // namespace imlui::tools
