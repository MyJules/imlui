// The files the resource tools pack: where each one comes from and its path in the mount, plus the file helpers
// the tools share.
export module imlui.tools.source_files;

import std;

export namespace imlui::tools {

/// One input file and its path in the mount.
struct SourceFile {
    std::string path; ///< relative to the base folder, `/`-separated (`fonts/Roboto-Regular.ttf`)
    std::filesystem::path file;
    /// Overrides the size-based compression rule (a manifest's `compress = { [path] = true|false }`); nullopt
    /// applies it.
    std::optional<bool> compress;
};

/// Names `files` relative to `base`, or every regular file under `base` (recursively) when `files` is empty.
/// nullopt, with the reason in `error`, when a file is missing, outside `base`, or two files get the same path.
[[nodiscard]] std::optional<std::vector<SourceFile>>
collectFiles(const std::filesystem::path& base, const std::vector<std::filesystem::path>& files, std::string& error);

/// The contents of `file`; nullopt when it can't be read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& file);

/// A UTF-8 string as a path, and back (on Windows a plain std::string would be read in the ANSI code page).
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view s);
[[nodiscard]] std::string utf8(const std::filesystem::path& p);

/// Whether `relative`, a path made relative to a base folder (lexically_relative), lies outside that folder: it is
/// empty, absolute, or its first element is `..`. (`..foo.png` is a file name, not a way out.)
[[nodiscard]] bool leavesBase(const std::filesystem::path& relative);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

fs::path pathFromUtf8(std::string_view s) { return fs::path{std::u8string{s.begin(), s.end()}}; }

std::string utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string{s.begin(), s.end()};
}

bool leavesBase(const fs::path& relative) {
    return relative.empty() || relative.is_absolute() || *relative.begin() == "..";
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
        if (leavesBase(relative)) {
            error = "'" + utf8(input) + "' isn't under the base folder '" + utf8(root) + "'";
            return std::nullopt;
        }
        out.push_back({utf8(relative), file, std::nullopt});
    }
    std::ranges::sort(out, {}, &SourceFile::path);
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i].path == out[i - 1].path) {
            error = "'" + out[i].path + "' is listed twice";
            return std::nullopt;
        }
    return out;
}

} // namespace imlui::tools
