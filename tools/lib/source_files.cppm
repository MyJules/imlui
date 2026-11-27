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

/// A 64-bit FNV-1a hash of `bytes` (not cryptographic: it tells versions of a file apart).
[[nodiscard]] std::uint64_t contentHash(std::span<const std::byte> bytes);

/// The contents of `file`; nullopt when it can't be read.
[[nodiscard]] std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& file);

/// Writes `bytes` to `file` through a temporary file next to it (`<file>.tmp-<random>-<n>`, unique, so two runs
/// writing the same output at once don't write into one temporary file) renamed over it, so `file` holds either its
/// old or its new contents, never a partial write (an interrupted build, a full disk). A rename that fails because
/// another process holds `file` open (on Windows: clang reading a module, an indexer or a virus scanner, without
/// sharing delete access) is retried for about 2 s. The folder must exist. False, with the reason in `error` when
/// given, when it can't be written; the old file is then untouched and the temporary file removed.
[[nodiscard]] bool replaceFile(const std::filesystem::path& file, std::span<const std::byte> bytes,
                               std::string* error = nullptr);

/// A UTF-8 string as a path, and back (on Windows a plain std::string would be read in the ANSI code page).
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view s);
[[nodiscard]] std::string utf8(const std::filesystem::path& p);
/// utf8(), or nullopt when `p` has no UTF-8 form (on Windows, a name with an unpaired surrogate), where utf8() throws.
[[nodiscard]] std::optional<std::string> utf8Name(const std::filesystem::path& p);

/// Whether `relative`, a path made relative to a base folder (lexically_relative), lies outside that folder: it is
/// empty, absolute, or its first element is `..`. (`..foo.png` is a file name, not a way out.)
[[nodiscard]] bool leavesBase(const std::filesystem::path& relative);

/// A tool's run function: the arguments after the program name, UTF-8; returns the exit code.
using ToolRun = int (*)(std::vector<std::string> args);

/// A tool's main(): calls `run` with the arguments after the program name as UTF-8. On Windows the tools start at
/// wmain() and pass its UTF-16 `argv` (main()'s is in the ANSI code page, which loses any other character); elsewhere
/// `argv` is taken as UTF-8. An exception (a name with no UTF-8 form, out of memory) is printed as
/// `<tool>: <what>` and exits with 1, instead of ending the process with abort().
[[nodiscard]] int toolMain(std::string_view tool, int argc, const wchar_t* const* argv, ToolRun run);
[[nodiscard]] int toolMain(std::string_view tool, int argc, const char* const* argv, ToolRun run);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

fs::path pathFromUtf8(std::string_view s) { return fs::path{std::u8string{s.begin(), s.end()}}; }

std::string utf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string{s.begin(), s.end()};
}

std::optional<std::string> utf8Name(const fs::path& p) {
    try {
        return utf8(p);
    } catch (const std::system_error&) {
        return std::nullopt;
    }
}

bool leavesBase(const fs::path& relative) {
    return relative.empty() || relative.is_absolute() || *relative.begin() == "..";
}

namespace {

/// Runs `run` with the arguments `convert` makes, printing what is thrown.
template <class Convert>
int guardedMain(std::string_view tool, ToolRun run, Convert convert) {
    try {
        return run(convert());
    } catch (const std::exception& e) {
        std::cerr << tool << ": " << e.what() << '\n';
    } catch (...) {
        std::cerr << tool << ": unknown error\n";
    }
    return 1;
}

} // namespace

int toolMain(std::string_view tool, int argc, const wchar_t* const* argv, ToolRun run) {
    return guardedMain(tool, run, [&] {
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) {
            try {
                const std::u8string arg = fs::path{argv[i]}.u8string(); // as given: not utf8()'s generic form
                args.emplace_back(arg.begin(), arg.end());
            } catch (const std::system_error&) {
                throw std::runtime_error{"argument " + std::to_string(i) + " has no UTF-8 form"};
            }
        }
        return args;
    });
}

int toolMain(std::string_view tool, int argc, const char* const* argv, ToolRun run) {
    return guardedMain(tool, run, [&] { return std::vector<std::string>{argv + std::min(argc, 1), argv + argc}; });
}

std::uint64_t contentHash(std::span<const std::byte> bytes) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const std::byte b : bytes) {
        h ^= std::to_integer<std::uint64_t>(b);
        h *= 0x100000001b3ULL;
    }
    return h;
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

namespace {

/// A suffix no other run (another process, or another call in this one) picks: random bits from the OS, which differ
/// between processes started at the same moment, and a counter, which differs between calls.
std::string uniqueSuffix() {
    static std::atomic<std::uint64_t> counter{0};
    static const std::uint64_t random = [] {
        std::random_device device;
        return (std::uint64_t{device()} << 32) ^ device();
    }();
    return std::format(".tmp-{:016x}-{}", random, counter.fetch_add(1));
}

/// Whether a failed rename may succeed once another process lets go of the file: on Windows a file open without
/// FILE_SHARE_DELETE can't be replaced (ERROR_SHARING_VIOLATION, ERROR_LOCK_VIOLATION, or ERROR_ACCESS_DENIED, which a
/// file pending deletion gives too).
bool transient(const std::error_code& ec) {
    if (ec.category() == std::system_category() && (ec.value() == 5 || ec.value() == 32 || ec.value() == 33))
        return true;
    return ec == std::errc::permission_denied || ec == std::errc::device_or_resource_busy ||
           ec == std::errc::resource_unavailable_try_again;
}

} // namespace

bool replaceFile(const fs::path& file, std::span<const std::byte> bytes, std::string* error) {
    fs::path temporary = file;
    temporary += uniqueSuffix();
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
    // Backing off from 10 ms to 250 ms: about 2 s in all, long enough for a compiler or a scanner to close the file.
    auto delay = std::chrono::milliseconds{10};
    for (auto waited = std::chrono::milliseconds{0};; waited += delay, delay = std::min(delay * 2, decltype(delay){250})) {
        fs::rename(temporary, file, ec);
        if (!ec || !transient(ec) || waited >= std::chrono::seconds{2}) break;
        std::this_thread::sleep_for(delay);
    }
    if (ec) {
        if (error) *error = "can't replace '" + utf8(file) + "': " + ec.message();
        std::error_code ignored;
        fs::remove(temporary, ignored);
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
        auto name = utf8Name(relative);
        if (!name) {
            error = "a file name in '" + utf8(lexical.parent_path()) + "' has no UTF-8 form";
            return std::nullopt;
        }
        out.push_back({std::move(*name), file, std::nullopt});
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
