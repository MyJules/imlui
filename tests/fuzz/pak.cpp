// Fuzz target `pak`: a .pak archive from untrusted bytes (an archive next to the executable). PakMount::fromBytes
// parses and checks it (parsePak); every file of an archive it accepts is then read, which decompresses the zstd ones
// (PackedMount), and the mount is listed like Resources does. Nothing may crash, whatever the bytes.
import std;

import imlui.log;
import imlui.resources.pak_mount;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

/// Files that declare more than this aren't read: the format allows up to pakMaxFileSize (2 GiB), which is only slow
/// to allocate, not wrong, and would trip the fuzzer's memory limit.
constexpr std::uint64_t maxReadSize = std::uint64_t{16} << 20;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // A corrupt file's read logs an error: formatted still, but not printed for every input.
    static const bool quiet = (imlui::log::setLevel(imlui::log::Level::Critical), true);
    (void)quiet;

    const auto* first = reinterpret_cast<const std::byte*>(data);
    std::vector<std::byte> bytes{first, first + size};

    std::string error;
    const auto pak = imlui::PakMount::fromBytes(std::move(bytes), "fuzz.pak", &error);
    if (!pak) {
        if (error.empty()) std::abort(); // a rejected archive always says why
        return 0;
    }
    if (pak->mountPoint().empty() || !pak->mountPoint().ends_with('/')) std::abort();

    std::vector<std::string> folders{""};
    for (const auto& entry : pak->entries()) {
        if (!pak->exists(entry.path)) std::abort();
        for (std::size_t slash = entry.path.find('/'); slash != std::string::npos;
             slash = entry.path.find('/', slash + 1))
            folders.push_back(entry.path.substr(0, slash));
        if (entry.size > maxReadSize) continue;
        // Twice: the second read of a compressed file comes from the cache.
        for (int i = 0; i < 2; ++i)
            if (const auto file = pak->read(entry.path); file && file->size() != entry.size) std::abort();
    }
    for (const auto& folder : folders) {
        for (const auto& name : pak->list(folder))
            if (name.empty()) std::abort();
        (void)pak->exists(folder);
    }
    (void)pak->origin();
    return 0;
}
