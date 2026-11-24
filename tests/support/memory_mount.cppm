// MemoryMount: an immutable mount over a fixed set of files held in memory, like an embedded or .pak mount
// (modified() is always nullopt). Test-only (built into imlui_tests).
export module imlui.test.memory_mount;

import std;

import imlui.resources.mount;

export namespace imlui::test {

class MemoryMount final : public Mount {
public:
    /// Files by normalized path (`a/b.lua`) and contents. Their folders exist implicitly.
    MemoryMount(std::initializer_list<std::pair<const std::string, std::string>> files, std::string name = "memory");

    [[nodiscard]] std::optional<ResourceData> read(std::string_view path) const override;
    [[nodiscard]] bool exists(std::string_view path) const override;
    [[nodiscard]] std::vector<std::string> list(std::string_view path) const override;
    [[nodiscard]] std::optional<ResourceTime> modified(std::string_view path) const override;
    [[nodiscard]] std::string origin() const override;

private:
    std::map<std::string, std::string, std::less<>> m_files;
    std::string m_name;
};

} // namespace imlui::test

module :private;

namespace imlui::test {

MemoryMount::MemoryMount(std::initializer_list<std::pair<const std::string, std::string>> files, std::string name)
    : m_files{files}, m_name{std::move(name)} {}

std::optional<ResourceData> MemoryMount::read(std::string_view path) const {
    const auto it = m_files.find(path);
    if (it == m_files.end()) return std::nullopt;
    return ResourceData{std::as_bytes(std::span{it->second.data(), it->second.size()})};
}

bool MemoryMount::exists(std::string_view path) const {
    if (path.empty() || m_files.contains(path)) return true;
    const std::string folder = std::string{path} + "/";
    const auto it = m_files.lower_bound(folder);
    return it != m_files.end() && it->first.starts_with(folder);
}

std::vector<std::string> MemoryMount::list(std::string_view path) const {
    std::vector<std::string> out;
    const std::string folder = path.empty() ? std::string{} : std::string{path} + "/";
    for (const auto& [file, content] : m_files) {
        if (!file.starts_with(folder)) continue;
        const std::string rest = file.substr(folder.size());
        const std::size_t slash = rest.find('/');
        std::string entry = slash == std::string::npos ? rest : rest.substr(0, slash + 1);
        if (out.empty() || out.back() != entry) out.push_back(std::move(entry));
    }
    return out;
}

std::optional<ResourceTime> MemoryMount::modified(std::string_view) const { return std::nullopt; }

std::string MemoryMount::origin() const { return "memory '" + m_name + "'"; }

} // namespace imlui::test
