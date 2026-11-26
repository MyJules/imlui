// The hover docs of the built-in element types (TypeBuilder::doc, shown by the editor through the Lua definitions):
// every built-in registered by registerBuiltinTypes() should document the type and each declared property, signal and
// method (the `on<Name>Changed` signals of properties are documented automatically, the common props in ElementProps).
//
// While the built-ins are being documented it is informational: it prints what each type leaves undocumented (run
// `imlui_tests --gtest_filter=ElementDocs.*`) and fails only for the types in `documentedTypes`, which must stay
// complete. Document a type, add its name (and its aliases') there. Once every built-in is documented, set
// `requireEveryBuiltin` to true: every built-in must then be complete.
#include <gtest/gtest.h>

import std;

import imlui.types.registry;
import imlui.types.type_info;
import imlui.ui.elements.builtin;

using namespace imlui;

namespace {

/// Every built-in (and built-in singleton) must be documented completely, not only `documentedTypes`.
constexpr bool requireEveryBuiltin = false;

/// The built-ins documented completely, by Lua name (an alias is a type of its own: list it too).
const std::set<std::string, std::less<>> documentedTypes{
    "Button",
};

/// What `info` leaves undocumented: "the type", ".prop", ".onSignal", ":method".
std::vector<std::string> undocumented(const TypeInfo& info) {
    std::vector<std::string> out;
    if (info.doc().empty()) out.emplace_back("the type");
    for (const TypeInfo::Property& p : info.properties())
        if (p.doc.empty()) out.push_back("." + p.name);
    for (const TypeInfo::Signal& s : info.signalDeclarations())
        if (s.doc.empty()) out.push_back("." + s.name);
    for (const TypeInfo::Method& m : info.methods())
        if (m.doc.empty()) out.push_back(":" + m.name);
    return out;
}

std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& part : parts)
        out += (out.empty() ? "" : ", ") + part;
    return out;
}

} // namespace

TEST(ElementDocs, BuiltinsAreDocumented) {
    TypeRegistry registry;
    registerBuiltinTypes(registry);
    for (const std::string& name : documentedTypes)
        EXPECT_NE(registry.find(name), nullptr) << "documentedTypes lists '" << name << "', which isn't a built-in";

    std::vector<std::pair<std::string, const TypeInfo*>> types;
    for (const std::string& name : registry.typeNames())
        types.emplace_back(name, registry.find(name));
    for (const auto& entry : registry.singletonTypes())
        types.push_back(entry);

    std::string report;
    std::string completeUnlisted;
    std::size_t complete = 0;
    for (const auto& [name, info] : types) {
        ASSERT_NE(info, nullptr) << name;
        const std::vector<std::string> missing = undocumented(*info);
        if (requireEveryBuiltin || documentedTypes.contains(name))
            EXPECT_TRUE(missing.empty()) << name << " must be documented completely; undocumented: " << joined(missing);
        if (missing.empty()) {
            ++complete;
            if (!documentedTypes.contains(name)) completeUnlisted += " " + name;
        } else {
            report += "  " + name + ": " + joined(missing) + "\n";
        }
    }
    std::cout << "[ ElementDocs ] " << complete << " of " << types.size() << " built-in types documented completely.\n";
    if (!completeUnlisted.empty())
        std::cout << "[ ElementDocs ] Complete, add to documentedTypes:" << completeUnlisted << "\n";
    if (!report.empty()) std::cout << "[ ElementDocs ] Undocumented:\n" << report;
}
