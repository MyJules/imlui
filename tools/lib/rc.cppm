// imlui_rc: the build-time tool behind imlui_add_resources (cmake/ImluiResources.cmake). It reads a resource manifest
// (imlui.tools.resource_manifest), encodes its files and writes them as a blob plus a C++ module that embeds the blob
// with `#embed`, and a dependency file for Ninja:
//
//     imlui_rc [--dev-paths] <manifest> <name> <output folder>
//         writes <output folder>/<name>.bin, <name>.cppm and <name>.d
//     imlui_rc --source-dirs <manifest> <name> <output folder>
//         writes <output folder>/<name>.cppm without the files (IMLUI_EMBED_RESOURCES=OFF): every package has only its
//         mount point and its group's base folder, and the module depends on nothing but the manifest
//     imlui_rc --list <manifest> <listing file>
//         writes the manifest's file list (manifestListing()), only when it changed
//
// The dependency file lists the scanned folders, so adding or removing a file reruns imlui_rc, but on Windows Ninja
// reads folder times from the parent folder's listing, which NTFS updates lazily. So the build also runs
// `imlui_rc --list` on every build (cheap: no file is read) and makes the listing an input of the embedding.
//
// The generated module is `imlui.embedded.<name>`, exporting `imlui::embedded::<name>::packages()` (one
// ResourcePackage per group of the manifest: its files, its mount point and, with --dev-paths, the group's absolute
// base folder as the dev source folder, plus the group's selection (its `files`, `include` and `exclude`, and the
// manifest's path when it lies there, which no pattern matches), so the folder mount serves the same files) and
// `mount(Resources&)` (mounts every group's files). With --source-dirs the packages' files are null and
// `mount()` mounts the source folders instead.
export module imlui.tools.rc;

import std;

import imlui.tools.resource_encoding;
import imlui.tools.depfile;
import imlui.tools.resource_manifest;
import imlui.tools.source_files;

export namespace imlui::tools {

/// The tool's main(); returns the process exit code.
[[nodiscard]] int runRc(int argc, char** argv);

/// True when `name` can name a generated module and namespace: lowercase ASCII letters, digits and `_`, starting
/// with a letter.
[[nodiscard]] bool isResourceIdentifier(std::string_view name);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

namespace {

/// `s` as a C++ string literal body. Octal escapes, so a following digit can't extend them.
std::string escaped(std::string_view s) {
    std::string out;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\' || u < 0x20 || u >= 0x7f) {
            out += '\\';
            out += static_cast<char>('0' + ((u >> 6) & 7));
            out += static_cast<char>('0' + ((u >> 3) & 7));
            out += static_cast<char>('0' + (u & 7));
        } else {
            out += c;
        }
    }
    return out;
}

/// Leaves `file` alone (and its time: nothing that depends on it rebuilds) when it already holds `bytes`. Otherwise
/// replaces it whole (replaceFile), so an interrupted run never leaves a truncated blob or module whose time is newer
/// than its inputs. Ninja then rebuilds what includes it: clang lists a file pulled in with `#embed` in the
/// dependency file of the module that embeds it, like a header.
bool writeIfChanged(const fs::path& file, std::span<const std::byte> bytes) {
    if (auto old = readFile(file); old && std::ranges::equal(*old, bytes)) return true;
    return replaceFile(file, bytes);
}

struct Record {
    std::string path;
    std::uint64_t offset = 0;
    std::uint64_t storedSize = 0;
    std::uint64_t size = 0;
    bool compressed = false;
};

/// One group as the generated module embeds it.
struct EmbeddedGroup {
    std::string mountPoint;
    std::string sourceDir; ///< "" without --dev-paths
    // The group's selection, for the folder mount (ResourceSelection); unused without a source folder.
    std::vector<std::string> files;
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::vector<std::string> unmatched; ///< the manifest's path in sourceDir, when it lies there
    std::vector<Record> records;
};

/// The manifest's path inside folder `base` as a one-element list (no pattern matches the manifest, so the dev folder
/// mount serves it only when `files` lists it), or an empty list when it lies elsewhere.
std::vector<std::string> manifestIn(const fs::path& manifest, const fs::path& base) {
    std::error_code ec;
    const fs::path relative = fs::weakly_canonical(manifest, ec).lexically_relative(base);
    if (ec || relative.empty() || *relative.begin() == "..") return {};
    return {utf8(relative)};
}

/// `strings` as a C++ braced list of string literals.
std::string stringList(const std::vector<std::string>& strings) {
    std::string s = "{";
    for (std::size_t i = 0; i < strings.size(); ++i)
        s += (i ? ", \"" : "\"") + escaped(strings[i]) + "\"";
    return s + "}";
}

/// The group's ResourceSelection as a C++ expression; std::nullopt without a source folder (nothing to select from).
std::string selectionSource(const EmbeddedGroup& g) {
    if (g.sourceDir.empty()) return "std::nullopt";
    return "ResourceSelection{" + stringList(g.files) + ", " + stringList(g.include) + ", " + stringList(g.exclude) +
           ", " + stringList(g.unmatched) + "}";
}

/// `group` for the generated module, without records: mount point and, with `sourceDir`, the folder and selection.
EmbeddedGroup embeddedGroup(const ResourceManifest& manifest, const ResourceGroup& group, bool sourceDir) {
    EmbeddedGroup out;
    out.mountPoint = group.mountPoint;
    if (!sourceDir) return out;
    out.sourceDir = utf8(group.base);
    out.files = group.listed;
    out.include = group.include;
    out.exclude = group.exclude;
    out.unmatched = manifestIn(manifest.file, group.base);
    return out;
}

std::string moduleSource(std::string_view name, const fs::path& manifest, const std::vector<EmbeddedGroup>& groups) {
    const std::string n{name};
    std::string s;
    s += "// Generated by imlui_rc (imlui_add_resources) from " + escaped(utf8(manifest)) + ". Do not edit.\n";
    s += "export module imlui.embedded." + n + ";\n\n";
    s += "import std;\n\n";
    s += "import imlui.resources;\nimport imlui.resources.embedded_mount;\nimport imlui.resources.package;\n\n";
    s += "export namespace imlui::embedded::" + n + " {\n\n";
    s += "/// The manifest's groups, for defaultConfig(): each one's files, mount point and dev source folder.\n";
    for (const auto& g : groups) {
        s += "///  - `" + escaped(g.mountPoint) + "`: " + std::to_string(g.records.size()) + " files, ";
        s += g.sourceDir.empty() ? std::string{"no source folder (IMLUI_DEV_PATHS=OFF)"} : escaped(g.sourceDir);
        s += "\n";
    }
    s += "[[nodiscard]] std::vector<ResourcePackage> packages();\n";
    s += "/// Mounts the files of every package at its mount point.\n";
    s += "void mount(Resources& resources);\n\n";
    s += "} // namespace imlui::embedded::" + n + "\n\nmodule :private;\n\n";
    s += "namespace imlui::embedded::" + n + " {\n\nnamespace {\n\n";
    s += "// #embed is a C23 feature that clang offers in C++ as an extension; this generated file is the one place\n";
    s += "// that uses it.\n";
    s += "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wc23-extensions\"\n";
    s += "constexpr unsigned char blob[] = {\n#embed \"" + n + ".bin\" if_empty(0)\n};\n";
    s += "#pragma clang diagnostic pop\n\n";
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const std::string index = std::to_string(i);
        const auto& records = groups[i].records;
        if (records.empty()) {
            s += "constexpr std::span<const EmbeddedMount::Record> records" + index + "{};\n\n";
        } else {
            s += "constexpr EmbeddedMount::Record records" + index + "[] = {\n";
            for (const auto& r : records)
                s += "    {\"" + escaped(r.path) + "\", " + std::to_string(r.offset) + ", " +
                     std::to_string(r.storedSize) + ", " + std::to_string(r.size) + ", " +
                     (r.compressed ? "true" : "false") + "},\n";
            s += "};\n\n";
        }
        // One group keeps the plain name in the mount's origin; several add the mount point.
        const std::string mountName = groups.size() == 1 ? n : n + " (" + groups[i].mountPoint + ")";
        s += "std::shared_ptr<EmbeddedMount> files" + index + "() {\n";
        s += "    static const auto instance = std::make_shared<EmbeddedMount>(\"" + escaped(mountName) +
             "\", std::as_bytes(std::span{blob}), records" + index + ");\n";
        s += "    return instance;\n}\n\n";
    }
    s += "} // namespace\n\n";
    s += "std::vector<ResourcePackage> packages() {\n    return {\n";
    for (std::size_t i = 0; i < groups.size(); ++i)
        s += "        {\"" + escaped(groups[i].mountPoint) + "\", files" + std::to_string(i) + "(), \"" +
             escaped(groups[i].sourceDir) + "\", " + selectionSource(groups[i]) + "},\n";
    s += "    };\n}\n\n";
    s += "void mount(Resources& resources) {\n";
    s += "    for (const ResourcePackage& p : packages())\n        resources.mount(p.mountPoint, p.files);\n}\n\n";
    s += "} // namespace imlui::embedded::" + n + "\n";
    return s;
}

/// The module for --source-dirs: the same API, packages without files.
std::string sourceDirsModuleSource(std::string_view name, const fs::path& manifest,
                                   const std::vector<EmbeddedGroup>& groups) {
    const std::string n{name};
    std::string s;
    s += "// Generated by imlui_rc --source-dirs (imlui_add_resources, not embedded) from " + escaped(utf8(manifest)) +
         ". Do not edit.\n";
    s += "export module imlui.embedded." + n + ";\n\n";
    s += "import std;\n\n";
    s += "import imlui.resources;\nimport imlui.resources.directory_mount;\nimport imlui.resources.package;\n\n";
    s += "export namespace imlui::embedded::" + n + " {\n\n";
    s += "/// The manifest's groups, for defaultConfig(). Not embedded (IMLUI_EMBED_RESOURCES=OFF): each one has its\n";
    s += "/// mount point and source folder, no files.\n";
    for (const auto& g : groups)
        s += "///  - `" + escaped(g.mountPoint) + "`: " + escaped(g.sourceDir) + "\n";
    s += "[[nodiscard]] std::vector<ResourcePackage> packages();\n";
    s += "/// Mounts the source folder of every package at its mount point.\n";
    s += "void mount(Resources& resources);\n\n";
    s += "} // namespace imlui::embedded::" + n + "\n\nmodule :private;\n\n";
    s += "namespace imlui::embedded::" + n + " {\n\n";
    s += "std::vector<ResourcePackage> packages() {\n    return {\n";
    for (const auto& g : groups)
        s += "        {\"" + escaped(g.mountPoint) + "\", nullptr, \"" + escaped(g.sourceDir) + "\", " +
             selectionSource(g) + "},\n";
    s += "    };\n}\n\n";
    s += "void mount(Resources& resources) {\n";
    s += "    for (const ResourcePackage& p : packages()) {\n";
    s += "        const std::u8string dir{p.sourceDir.begin(), p.sourceDir.end()};\n";
    s += "        resources.mount(p.mountPoint, std::make_shared<DirectoryMount>(std::filesystem::path{dir}, "
         "p.selection));\n";
    s += "    }\n}\n\n";
    s += "} // namespace imlui::embedded::" + n + "\n";
    return s;
}

int fail(std::string_view message) {
    std::cerr << "imlui_rc: " << message << '\n';
    return 1;
}

} // namespace

bool isResourceIdentifier(std::string_view name) {
    if (name.empty() || name.front() < 'a' || name.front() > 'z') return false;
    for (const char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

int runRc(int argc, char** argv) {
    std::vector<std::string_view> args{argv + 1, argv + argc};
    if (args.size() == 3 && args[0] == "--list") {
        std::string error;
        const auto manifest = loadManifest(pathFromUtf8(args[1]), error);
        if (!manifest) return fail(error);
        const std::string listing = manifestListing(*manifest);
        if (!writeIfChanged(pathFromUtf8(args[2]), std::as_bytes(std::span{listing})))
            return fail("can't write '" + std::string{args[2]} + "'");
        return 0;
    }
    const bool devPaths = !args.empty() && args.front() == "--dev-paths";
    const bool sourceDirs = !args.empty() && args.front() == "--source-dirs";
    if (devPaths || sourceDirs) args.erase(args.begin());
    if (args.size() != 3)
        return fail("usage: imlui_rc [--dev-paths | --source-dirs] <manifest> <name> <output folder>\n"
                    "       imlui_rc --list <manifest> <listing file>");
    const std::string name{args[1]};
    if (!isResourceIdentifier(name)) return fail("invalid name '" + name + "'");
    std::string error;
    const auto manifest = loadManifest(pathFromUtf8(args[0]), error);
    if (!manifest) return fail(error);

    const fs::path outDir = pathFromUtf8(args[2]);
    std::error_code ec;
    fs::create_directories(outDir, ec);
    const fs::path moduleFile = fs::absolute(outDir / (name + ".cppm"), ec);
    if (sourceDirs) {
        std::vector<EmbeddedGroup> groups;
        for (const auto& group : manifest->groups)
            groups.push_back(embeddedGroup(*manifest, group, true));
        const std::string source = sourceDirsModuleSource(name, manifest->file, groups);
        if (!writeIfChanged(moduleFile, std::as_bytes(std::span{source})))
            return fail("can't write to '" + utf8(outDir) + "'");
        return 0;
    }

    std::vector<std::byte> blob;
    std::vector<EmbeddedGroup> groups;
    for (const auto& group : manifest->groups) {
        auto& out = groups.emplace_back(embeddedGroup(*manifest, group, devPaths));
        for (const auto& f : group.files) {
            const auto data = readFile(f.file);
            if (!data) return fail("can't read '" + utf8(f.file) + "'");
            const EncodedResource encoded = encodeResource(f.path, *data, f.compress);
            out.records.push_back({f.path, blob.size(), encoded.stored.size(), encoded.size, encoded.compressed});
            blob.insert(blob.end(), encoded.stored.begin(), encoded.stored.end());
        }
    }

    const std::string source = moduleSource(name, manifest->file, groups);
    if (!writeIfChanged(outDir / (name + ".bin"), blob) ||
        !writeIfChanged(moduleFile, std::as_bytes(std::span{source})) ||
        !writeDepfile(outDir / (name + ".d"), moduleFile, manifestDependencies(*manifest)))
        return fail("can't write to '" + utf8(outDir) + "'");
    return 0;
}

} // namespace imlui::tools
