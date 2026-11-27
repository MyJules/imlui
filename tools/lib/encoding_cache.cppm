// EncodingCache: the encoded form of each resource file (encodeResource()) kept between runs of the resource tools, so
// a rerun after one file changed compresses that file only. zstd at level 19 takes about a second per MiB, and
// imlui_rc encodes every file of a manifest whenever any of them (or the manifest) changed.
//
// One file per input in the cache folder, named after the key of what decides the encoding: the content (its size and
// a 64-bit hash), whether the path's extension is compressible(), the manifest's `compress` override, the zstd level
// and version, and the encoding rule's own version (encodingRuleVersion). An entry holds that key again, the result
// (stored as is, or the zstd frame) and a checksum of all of it. It is used only when it parses, its key and checksum
// match and, for a frame, the frame decompresses to exactly the input; anything else (a stale, truncated or corrupt
// entry, a hash collision) is encoded again and the entry rewritten. So the output is the same as without a cache: a
// frame is whatever the same zstd at the same level made of the same bytes, and the stored-or-compressed choice is
// checksummed with the key it was made for.
export module imlui.tools.encoding_cache;

import std;

import zstd.compress;
import imlui.tools.resource_encoding;
import imlui.tools.source_files;

export namespace imlui::tools {

/// The version of encodeResource()'s rule (the size threshold, the 10 % saving, compressible()), part of every cache
/// key: change it with the rule, so entries made under the old one aren't used.
inline constexpr std::uint32_t encodingRuleVersion = 1;

/// encodeResource() with its results kept in a folder between runs (see the module comment).
class EncodingCache {
public:
    /// Entries live in `folder`, created when the first one is written. An empty path: no cache, every call encodes.
    explicit EncodingCache(std::filesystem::path folder);

    /// encodeResource(path, data, compress), from the cache when it holds an intact entry for the same input;
    /// otherwise encoded and the entry (re)written. Failing to write the entry only costs the next run the encoding.
    [[nodiscard]] EncodedResource encode(std::string_view path, std::span<const std::byte> data,
                                         std::optional<bool> compress);

    /// Removes the entries this run didn't use (earlier versions of the files, files no longer packed) and temporary
    /// files left by an interrupted run, so the folder holds what the next run can use. Best effort.
    void prune() const;

    /// How many encode() calls the cache served, and how many encoded.
    [[nodiscard]] std::size_t hits() const;
    [[nodiscard]] std::size_t misses() const;

private:
    std::filesystem::path m_folder;
    std::set<std::string> m_used; ///< entry file names this run read or wrote
    std::size_t m_hits = 0;
    std::size_t m_misses = 0;
};

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view entryMagic = "IMLUIRC-ENCODED\n";

void putU64(std::vector<std::byte>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xffU));
}

std::uint64_t getU64(std::span<const std::byte> in, std::size_t at) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i)
        v |= std::to_integer<std::uint64_t>(in[at + i]) << (8 * i);
    return v;
}

std::span<const std::byte> bytesOf(std::string_view s) { return std::as_bytes(std::span{s.data(), s.size()}); }

/// What decides how a file is encoded, as text (part of an entry, and hashed into its name).
std::string keyOf(std::string_view path, std::span<const std::byte> data, std::optional<bool> compress) {
    return std::format("rule={} zstd={} level={} compressible={} override={} size={} hash={:016x}\n",
                       encodingRuleVersion, zstd::version(), zstd::maxLevel, compressible(path) ? 1 : 0,
                       compress ? (*compress ? "on" : "off") : "auto", data.size(), contentHash(data));
}

/// An entry: the magic, the key, a flag (1: `stored` is a zstd frame), the stored bytes' size and the bytes (a frame
/// only; a file stored as is needs none, it is the input), then a checksum of everything before it.
std::vector<std::byte> entryBytes(std::string_view key, const EncodedResource& encoded) {
    std::vector<std::byte> out;
    for (const auto part : {entryMagic, key})
        for (const std::byte b : bytesOf(part))
            out.push_back(b);
    out.push_back(std::byte{encoded.compressed ? std::uint8_t{1} : std::uint8_t{0}});
    const std::span<const std::byte> frame =
        encoded.compressed ? std::span<const std::byte>{encoded.stored} : std::span<const std::byte>{};
    putU64(out, frame.size());
    out.insert(out.end(), frame.begin(), frame.end());
    putU64(out, contentHash(out));
    return out;
}

/// The encoding an entry holds for `data` under `key`, or nullopt when it isn't an intact entry for them.
std::optional<EncodedResource> parseEntry(std::span<const std::byte> entry, std::string_view key,
                                          std::span<const std::byte> data) {
    const std::size_t head = entryMagic.size() + key.size();
    if (entry.size() < head + 1 + 8 + 8) return std::nullopt;
    const auto body = entry.first(entry.size() - 8);
    if (getU64(entry, body.size()) != contentHash(body)) return std::nullopt; // truncated or corrupt
    const auto expected = [&](std::size_t at, std::string_view s) {
        return std::ranges::equal(entry.subspan(at, s.size()), bytesOf(s));
    };
    if (!expected(0, entryMagic) || !expected(entryMagic.size(), key)) return std::nullopt; // another input
    const auto flag = std::to_integer<int>(entry[head]);
    const std::uint64_t frameSize = getU64(entry, head + 1);
    if (flag > 1 || frameSize != body.size() - (head + 1 + 8) || (flag == 0 && frameSize != 0)) return std::nullopt;
    EncodedResource out;
    out.size = data.size();
    out.compressed = flag == 1;
    if (!out.compressed) {
        out.stored.assign(data.begin(), data.end());
        return out;
    }
    const auto frame = body.subspan(head + 1 + 8);
    // The frame is trusted only for the bytes it really holds: what decides the output is the input, not the hash.
    if (!zstd::decompressesTo(frame, data)) return std::nullopt;
    out.stored.assign(frame.begin(), frame.end());
    return out;
}

} // namespace

EncodingCache::EncodingCache(fs::path folder) : m_folder{std::move(folder)} {}

EncodedResource EncodingCache::encode(std::string_view path, std::span<const std::byte> data,
                                      std::optional<bool> compress) {
    if (m_folder.empty()) {
        ++m_misses;
        return encodeResource(path, data, compress);
    }
    const std::string key = keyOf(path, data, compress);
    const std::string name = std::format("{:016x}.enc", contentHash(bytesOf(key)));
    m_used.insert(name);
    const fs::path file = m_folder / name;
    if (const auto entry = readFile(file))
        if (auto cached = parseEntry(*entry, key, data)) {
            ++m_hits;
            return std::move(*cached);
        }
    ++m_misses;
    EncodedResource encoded = encodeResource(path, data, compress);
    std::error_code ec;
    fs::create_directories(m_folder, ec);
    (void)replaceFile(file, entryBytes(key, encoded)); // a cache that can't be written is only slower
    return encoded;
}

void EncodingCache::prune() const {
    if (m_folder.empty()) return;
    std::error_code ec;
    std::vector<fs::path> unused;
    for (auto it = fs::directory_iterator(m_folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const auto name = utf8Name(it->path().filename());
        if (!name || !m_used.contains(*name)) unused.push_back(it->path());
    }
    for (const auto& file : unused)
        fs::remove(file, ec);
}

std::size_t EncodingCache::hits() const { return m_hits; }
std::size_t EncodingCache::misses() const { return m_misses; }

} // namespace imlui::tools
