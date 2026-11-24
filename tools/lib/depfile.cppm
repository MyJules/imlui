// Make-style dependency files (`<target>: <dependency>...`), as Ninja reads them through add_custom_command(DEPFILE):
// imlui_rc and imlui_pack list the manifest, every packed file and every folder they scanned, so editing, adding or
// removing a file reruns them.
module;

#include <filesystem>
#include <fstream>
#include <span>
#include <string>

export module imlui.tools.depfile;

import imlui.tools.source_files;

export namespace imlui::tools {

/// The text of a dependency file saying that `target` depends on `dependencies`. Paths are written `/`-separated,
/// with spaces, `#` and `$` escaped.
[[nodiscard]] std::string depfileText(const std::filesystem::path& target,
                                      std::span<const std::filesystem::path> dependencies);

/// Writes depfileText() to `file`; false when it can't be written.
[[nodiscard]] bool writeDepfile(const std::filesystem::path& file, const std::filesystem::path& target,
                                std::span<const std::filesystem::path> dependencies);

} // namespace imlui::tools

module :private;

namespace imlui::tools {

namespace fs = std::filesystem;

namespace {

std::string escaped(const fs::path& path) {
    std::string out;
    for (const char c : utf8(path)) {
        if (c == ' ' || c == '#')
            out += '\\';
        else if (c == '$')
            out += '$';
        out += c;
    }
    return out;
}

} // namespace

std::string depfileText(const fs::path& target, std::span<const fs::path> dependencies) {
    std::string out = escaped(target) + ":";
    for (const auto& dependency : dependencies)
        out += " \\\n  " + escaped(dependency);
    out += '\n';
    return out;
}

bool writeDepfile(const fs::path& file, const fs::path& target, std::span<const fs::path> dependencies) {
    const std::string text = depfileText(target, dependencies);
    std::ofstream stream{file, std::ios::binary | std::ios::trunc};
    stream << text;
    return static_cast<bool>(stream);
}

} // namespace imlui::tools
