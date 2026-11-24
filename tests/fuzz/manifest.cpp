// Fuzz target `manifest`: a resource manifest's Lua text (imlui.tools.resource_manifest). evaluateManifest() runs it
// in the sandbox (no io/os/load, an instruction and a memory limit) and reads the groups it returns; groups it accepts
// are resolved against a small fixture folder (tests/fuzz/manifest_root, IMLUI_FUZZ_MANIFEST_ROOT) as if the manifest
// were its resources.lua. Nothing may crash or hang, whatever the text.
import std;

import imlui.tools.resource_manifest;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

/// A `base` the fixture can safely be resolved with: a relative folder name inside it. Others are valid (`..`,
/// `C:/`), but would scan folders outside the fixture, a whole drive with `include = "**"`.
bool staysInFixture(std::string_view base) {
    if (base.empty() || base.starts_with('/') || base.find("..") != std::string_view::npos) return false;
    return std::ranges::all_of(base, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '.' || c == '/';
    });
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view source{reinterpret_cast<const char*>(data), size};
    const std::filesystem::path manifest = std::filesystem::path{IMLUI_FUZZ_MANIFEST_ROOT} / "resources.lua";

    std::string error;
    const auto groups = imlui::tools::evaluateManifest(source, manifest, error);
    if (!groups) {
        if (error.empty()) std::abort(); // a failed manifest always says why
        return 0;
    }
    if (groups->empty()) std::abort();
    for (const auto& group : *groups)
        if (!staysInFixture(group.base)) return 0;

    const auto resolved = imlui::tools::resolveManifest(*groups, manifest, error);
    if (!resolved) {
        if (error.empty()) std::abort();
        return 0;
    }
    if (resolved->groups.size() != groups->size()) std::abort();
    for (const auto& group : resolved->groups)
        if (!group.mountPoint.ends_with('/')) std::abort();
    (void)imlui::tools::manifestDependencies(*resolved);
    (void)imlui::tools::manifestListing(*resolved);
    return 0;
}
