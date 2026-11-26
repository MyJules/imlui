// The hover docs of the built-in element types (TypeBuilder::doc, shown by the editor through the Lua definitions):
// every built-in registered by registerBuiltinTypes() (and every built-in singleton) documents the type and each
// declared property, raw property, signal and method. The `on<Name>Changed` signals of properties are documented
// automatically, the common props in ElementProps. A new built-in, or a new member of one, fails here until it has its
// doc.
#include <gtest/gtest.h>

import std;

import imlui.types.registry;
import imlui.types.type_info;
import imlui.ui.elements.builtin;

using namespace imlui;

namespace {

/// What `info` leaves undocumented: "the type", ".prop", ".onSignal", ":method".
std::vector<std::string> undocumented(const TypeInfo& info) {
    std::vector<std::string> out;
    if (info.doc().empty()) out.emplace_back("the type");
    for (const TypeInfo::Property& p : info.properties())
        if (p.doc.empty()) out.push_back("." + p.name);
    for (const TypeInfo::RawProperty& p : info.rawProperties())
        if (p.doc.empty()) out.push_back("." + p.name + " (raw)");
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

    std::vector<std::pair<std::string, const TypeInfo*>> types;
    for (const std::string& name : registry.typeNames())
        types.emplace_back(name, registry.find(name));
    for (const auto& entry : registry.singletonTypes())
        types.push_back(entry);
    ASSERT_FALSE(types.empty());

    for (const auto& [name, info] : types) {
        ASSERT_NE(info, nullptr) << name;
        const std::vector<std::string> missing = undocumented(*info);
        EXPECT_TRUE(missing.empty()) << name << " must be documented completely (TypeBuilder::doc); undocumented: "
                                     << joined(missing);
    }
}
