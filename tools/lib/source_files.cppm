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

/// Names `files` relative to `base`, or every regular file under `base` (recursively, not into linked folders) when
/// `files` is empty. A file is named by its own path, also when it is a link (`logo.png` for a link to
/// `art/logo-v2.png`); it must resolve to a file under `base`.
/// nullopt, with the reason in `error`, when a file is missing, outside `base`, two files get the same path, or a
/// folder under `base` can't be listed (an incomplete list would make an incomplete archive). Entries that can't be
/// queried (a link to nothing) aren't files and are left out.
[[nodiscard]] std::optional<std::vector<SourceFile>>
collectFiles(const std::filesystem::path& base, const std::vector<std::filesystem::path>& files, std::string& error);

/// The contents of `file`; nullopt when it can't be read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& file);

/// Writes `bytes` to `file` through a temporary file next to it (`<file>.tmp`) renamed over it, so `file` holds
/// either its old or its new contents, never a partial write (an interrupted build, a full disk). The folder must
/// exist. False, with the reason in `error` when given, when it can't be written; the old file is then untouched.
[[nodiscard]] bool replaceFile(const std::filesystem::path& file, std::span<const std::byte> bytes,
                               std::string* error = nullptr);

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

bool replaceFile(const fs::path& file, std::span<const std::byte> bytes, std::string* error) {
    fs::path temporary = file;
    temporary += ".tmp";
    std::error_code ec;
    {
        std::ofstream out{temporary, std::ios::binary | std::ios::trunc};
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            fs::remove(temporary, ec);
            if (error) *error = "can't write '" + utf8(temporary) + "'";
            return false;
        }
    }
    fs::rename(temporary, file, ec);
    if (ec) {
        if (error) *error = "can't replace '" + utf8(file) + "': " + ec.message();
        fs::remove(temporary, ec);
        return false;
    }
    return true;
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
    if (inputs.empty()) {
        for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            std::error_code entryEc; // its own: an entry that can't be queried isn't a file, and the walk goes on
            if (it->is_regular_file(entryEc)) inputs.push_back(it->path());
        }
        if (ec) {
            error = "can't list the files under '" + utf8(root) + "': " + ec.message();
            return std::nullopt;
        }
    }

    // The base as given (made absolute) and resolved: a file is named by where it is, not where a link leads.
    const fs::path given = fs::absolute(base, ec).lexically_normal();
    std::vector<SourceFile> out;
    for (const auto& input : inputs) {
        const fs::path lexical = (input.is_absolute() ? input : root / input).lexically_normal();
        const fs::path file = fs::weakly_canonical(lexical, ec);
        if (ec || !fs::is_regular_file(file, ec)) {
            error = "'" + utf8(input) + "' isn't a file";
            return std::nullopt;
        }
        // The canonical path decides containment (a link may lead out of the base folder); the name comes from the
        // path itself, so a linked file keeps its own name, as in a manifest's scan.
        fs::path relative = lexical.lexically_relative(root);
        if (leavesBase(relative)) relative = lexical.lexically_relative(given);
        if (leavesBase(relative)) relative = file.lexically_relative(root); // spelled through a link above the base
        if (leavesBase(relative) || leavesBase(file.lexically_relative(root))) {
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
