// Keeps the element reference from drifting: every built-in type registered by registerBuiltinTypes() (its declared
// properties, raw properties, signals, methods, default property and non-visual flag) must be described in
// docs/elements.md, and its properties, raw properties and signals listed in the doc comment of its class
// (src/imlui/ui/elements/*.cppm). The markdown and
// the sources are read from the source tree (IMLUI_DOCS_DIR, IMLUI_ELEMENTS_DIR) and parsed simply:
//
//  * a type's section in elements.md is the text under the `##`/`###` heading that names it (`### Column / Item`),
//    or, for a type without one, the table row that starts with it (the inline containers); a section that says it
//    takes "every `ScrollView` prop" also counts that type's section, and so does "Stacking containers" (`spacing`,
//    `align`, `cull`) for the types it lists;
//  * a property is listed when it appears in backticks (`spacing`, or `x`, `y`), a signal or method when
//    "`name(" does;
//  * a type with a default property is in the "Positional strings" table; the "Non-visual elements" list holds
//    exactly the non-visual types;
//  * a class doc comment is the `///` block right above `class Name [final] : public Base`, together with its base
//    class's block (a subclass such as TextArea says "every TextField prop"); a member is listed when its name
//    appears in it as a word.
#include <gtest/gtest.h>

import imlui.resources.directory_mount;
import imlui.types.registry;
import imlui.types.type_info;
import imlui.ui.elements.builtin;

using namespace imlui;

namespace {

std::string readFile(const char* dir, std::string_view name) {
    const DirectoryMount mount{dir};
    const auto bytes = mount.read(name);
    if (!bytes) return {};
    return {reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// `word` occurs in `text` with no identifier character right before or after it.
bool containsWord(std::string_view text, std::string_view word) {
    for (std::size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1)) {
        const bool before = at > 0 && isWordChar(text[at - 1]);
        const std::size_t end = at + word.size();
        const bool after = end < text.size() && isWordChar(text[end]);
        if (!before && !after) return true;
    }
    return false;
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        start = end + 1;
    }
    return out;
}

/// elements.md split at its `##` / `###` headings.
struct Section {
    std::string heading;
    std::string body;
};

std::vector<Section> sections(std::string_view markdown) {
    std::vector<Section> out;
    for (std::string_view line : lines(markdown)) {
        if (line.starts_with("## ") || line.starts_with("### ")) {
            out.push_back({std::string{line.substr(line.find(' ') + 1)}, {}});
        } else if (!out.empty()) {
            out.back().body += line;
            out.back().body += '\n';
        }
    }
    return out;
}

class ElementReference : public ::testing::Test {
protected:
    void SetUp() override {
        registerBuiltinTypes(m_types);
        m_markdown = readFile(IMLUI_DOCS_DIR, "elements.md");
        ASSERT_FALSE(m_markdown.empty()) << IMLUI_DOCS_DIR "/elements.md";
        m_sections = sections(m_markdown);
    }

    [[nodiscard]] const Section* section(std::string_view heading) const {
        for (const Section& s : m_sections)
            if (s.heading == heading) return &s;
        return nullptr;
    }

    /// The text that documents type `name` (see the file comment), empty when there is none.
    [[nodiscard]] std::string docsFor(const std::string& name, int depth = 0) const {
        std::string text;
        for (const Section& s : m_sections)
            if (containsWord(s.heading, name)) text += s.body;
        if (text.empty()) {
            const std::string row = "| `" + name + "` |";
            for (std::string_view line : lines(m_markdown))
                if (line.starts_with(row)) text += std::string{line} + "\n";
        }
        // "It takes every `ScrollView` prop": that type's section too.
        const std::string_view marker = "every `";
        for (std::size_t at = text.find(marker); depth == 0 && at != std::string::npos;
             at = text.find(marker, at + 1)) {
            const std::size_t start = at + marker.size(), end = text.find('`', start);
            if (end == std::string::npos) break;
            const std::string other = text.substr(start, end - start);
            if (other != name && m_types.find(other)) text += docsFor(other, depth + 1);
        }
        return text;
    }

    TypeRegistry m_types;
    std::string m_markdown;
    std::vector<Section> m_sections;
};

} // namespace

TEST_F(ElementReference, ElementsMdDescribesEveryBuiltinType) {
    const std::vector<std::string> names = m_types.typeNames();
    ASSERT_GT(names.size(), 50u);
    const Section* stacking = section("Stacking containers");
    ASSERT_NE(stacking, nullptr);
    for (const std::string& name : names) {
        const TypeInfo& info = *m_types.find(name);
        std::string doc = docsFor(name);
        EXPECT_FALSE(doc.empty()) << name << ": no section or table row in elements.md";
        if (doc.empty()) continue;
        if (stacking->body.find("`" + name + "`") != std::string::npos) doc += stacking->body;
        for (const TypeInfo::Property& p : info.properties())
            EXPECT_NE(doc.find("`" + p.name + "`"), std::string::npos) << name << ": property " << p.name;
        for (const TypeInfo::RawProperty& p : info.rawProperties())
            EXPECT_NE(doc.find("`" + p.name + "`"), std::string::npos) << name << ": raw property " << p.name;
        for (const std::string& signal : info.signals())
            EXPECT_NE(doc.find("`" + signal + "("), std::string::npos) << name << ": signal " << signal;
        for (const TypeInfo::Method& m : info.methods())
            EXPECT_TRUE(doc.find("`" + m.name + "(") != std::string::npos ||
                        doc.find(":" + m.name + "(") != std::string::npos)
                << name << ": method " << m.name;
    }
}

TEST_F(ElementReference, ElementsMdListsDefaultPropertiesAndNonVisualTypes) {
    const Section* positional = section("Positional strings");
    const Section* nonVisual = section("Non-visual elements");
    ASSERT_NE(positional, nullptr);
    ASSERT_NE(nonVisual, nullptr);
    for (const std::string& name : m_types.typeNames()) {
        const TypeInfo& info = *m_types.find(name);
        const std::string quoted = "`" + name + "`";
        if (!info.defaultProperty().empty())
            EXPECT_NE(positional->body.find(quoted), std::string::npos)
                << name << ": default property '" << info.defaultProperty() << "' not in Positional strings";
        EXPECT_EQ(nonVisual->body.find(quoted) != std::string::npos, !info.visual())
            << name << (info.visual() ? " is visual but listed" : " is non-visual but not listed")
            << " in Non-visual elements";
    }
}

TEST_F(ElementReference, ClassDocCommentsListTheirMembers) {
    // registerType<Class>("Name") in builtin.cppm: the class of each built-in name.
    const std::string builtin = readFile(IMLUI_ELEMENTS_DIR, "builtin.cppm");
    ASSERT_FALSE(builtin.empty());
    const DirectoryMount elements{IMLUI_ELEMENTS_DIR};
    std::string sources;
    for (const std::string& file : elements.list(""))
        if (file.ends_with(".cppm")) sources += readFile(IMLUI_ELEMENTS_DIR, file);

    // The `///` block right above `class <cls> [final] : public <base>`, plus its base class's.
    auto comment = [&](const std::string& cls, const auto& self, int depth) -> std::string {
        const std::size_t at = sources.find("\nclass " + cls + " ");
        if (at == std::string::npos) return {};
        std::string block;
        std::size_t lineEnd = at;
        while (lineEnd > 0) {
            const std::size_t lineStart = sources.rfind('\n', lineEnd - 1);
            if (lineStart == std::string::npos) break;
            const std::string_view line{sources.data() + lineStart + 1, lineEnd - lineStart - 1};
            if (!line.starts_with("///")) break;
            block.insert(0, std::string{line} + "\n");
            lineEnd = lineStart;
        }
        const std::size_t baseStart = sources.find("public ", at) + std::string_view{"public "}.size();
        const std::string base = sources.substr(baseStart, sources.find_first_of(" {\r\n", baseStart) - baseStart);
        if (base != "Element" && depth < 4) block += self(base, self, depth + 1);
        return block;
    };

    int checked = 0;
    const std::string_view call = "registerType<";
    for (std::size_t at = builtin.find(call); at != std::string::npos; at = builtin.find(call, at + 1)) {
        const std::size_t clsStart = at + call.size(), clsEnd = builtin.find('>', clsStart);
        const std::size_t nameStart = builtin.find('"', clsEnd) + 1, nameEnd = builtin.find('"', nameStart);
        const std::string cls = builtin.substr(clsStart, clsEnd - clsStart);
        const std::string name = builtin.substr(nameStart, nameEnd - nameStart);
        const TypeInfo* info = m_types.find(name);
        ASSERT_NE(info, nullptr) << name;
        const std::string doc = comment(cls, comment, 0);
        EXPECT_FALSE(doc.empty()) << cls << ": no /// doc comment above the class";
        for (const TypeInfo::Property& p : info->properties())
            EXPECT_TRUE(containsWord(doc, p.name)) << cls << " (" << name << "): property " << p.name;
        for (const TypeInfo::RawProperty& p : info->rawProperties())
            EXPECT_TRUE(containsWord(doc, p.name)) << cls << " (" << name << "): raw property " << p.name;
        for (const std::string& signal : info->signals())
            EXPECT_TRUE(containsWord(doc, signal)) << cls << " (" << name << "): signal " << signal;
        ++checked;
    }
    EXPECT_EQ(checked, static_cast<int>(m_types.typeNames().size()));
}
