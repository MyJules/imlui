// TypeInfo: the type-erased description of a registered C++ type, filled in by TypeBuilder<T>
// (properties, signals, methods, visual flag, factory) and used by TypeRegistry to bind it to Lua.
module;

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module imlui.types.type_info;

import lua;
import imlui.types.object;
import imlui.types.element;

export namespace imlui {

class TypeInfo {
public:
    /// Result of assigning a Lua value to a property.
    enum class Assign { Unchanged, Changed, TypeMismatch };

    struct Property {
        std::string name;
        std::string_view typeName; ///< LuaConvert<V>::name, for error messages
        /// Pushes the getter's value.
        std::function<void(lua::State*, const Object&)> push;
        /// Converts the value at `idx` and calls the setter if it differs from the getter's value.
        /// Empty for read-only properties.
        std::function<Assign(lua::State*, int, Object&)> assign;
        /// True when nil is a value of the property (std::optional): nil is then synced, not skipped.
        bool nullable = false;
    };

    struct Method {
        std::string name;
        /// Calls the method with the Lua arguments starting at `firstArg`. Returns the number of results
        /// pushed, or -1 with an error message pushed instead.
        std::function<int(lua::State*, Object&, int firstArg)> call;
    };

    using Factory = std::function<std::unique_ptr<Element>()>;

    explicit TypeInfo(std::string name);

    [[nodiscard]] const std::string& name() const;

    [[nodiscard]] bool visual() const;
    void setVisual(bool visual);

    /// The property a positional first value sets: `Gauge { 0.5 }` with defaultProperty "value".
    [[nodiscard]] const std::string& defaultProperty() const;
    void setDefaultProperty(std::string name);

    [[nodiscard]] const std::vector<Property>& properties() const;
    [[nodiscard]] const std::vector<Method>& methods() const;
    [[nodiscard]] const std::vector<std::string>& signals() const;
    [[nodiscard]] const Property* findProperty(std::string_view name) const;
    [[nodiscard]] const Method* findMethod(std::string_view name) const;
    [[nodiscard]] bool hasSignal(std::string_view name) const;

    /// Adds a member. A name may only be used once across properties, methods and signals.
    void addProperty(Property property);
    void addMethod(Method method);
    void addSignal(std::string name);

    /// Element types have a factory; singleton types don't.
    [[nodiscard]] const Factory& factory() const;
    void setFactory(Factory factory);

private:
    void checkUnique(std::string_view name) const;

    std::string m_name;
    bool m_visual = true;
    std::string m_defaultProperty;
    std::vector<Property> m_properties;
    std::vector<Method> m_methods;
    std::vector<std::string> m_signals;
    Factory m_factory;
};

} // namespace imlui

module :private;

namespace imlui {

TypeInfo::TypeInfo(std::string name) : m_name{std::move(name)} {}

const std::string& TypeInfo::name() const { return m_name; }

bool TypeInfo::visual() const { return m_visual; }
void TypeInfo::setVisual(bool visual) { m_visual = visual; }

const std::string& TypeInfo::defaultProperty() const { return m_defaultProperty; }
void TypeInfo::setDefaultProperty(std::string name) { m_defaultProperty = std::move(name); }

const std::vector<TypeInfo::Property>& TypeInfo::properties() const { return m_properties; }
const std::vector<TypeInfo::Method>& TypeInfo::methods() const { return m_methods; }
const std::vector<std::string>& TypeInfo::signals() const { return m_signals; }

const TypeInfo::Property* TypeInfo::findProperty(std::string_view name) const {
    for (const auto& p : m_properties)
        if (p.name == name) return &p;
    return nullptr;
}

const TypeInfo::Method* TypeInfo::findMethod(std::string_view name) const {
    for (const auto& m : m_methods)
        if (m.name == name) return &m;
    return nullptr;
}

bool TypeInfo::hasSignal(std::string_view name) const {
    for (const auto& s : m_signals)
        if (s == name) return true;
    return false;
}

void TypeInfo::checkUnique(std::string_view name) const {
    // A clash would make one member unreachable (methods are looked up through the element table's
    // metatable, so a method named like a property would be read as that property's binding).
    if (findProperty(name) || findMethod(name) || hasSignal(name))
        throw std::invalid_argument{m_name + ": member '" + std::string{name} + "' is declared twice"};
}

void TypeInfo::addProperty(Property property) {
    checkUnique(property.name);
    m_properties.push_back(std::move(property));
}

void TypeInfo::addMethod(Method method) {
    checkUnique(method.name);
    m_methods.push_back(std::move(method));
}

void TypeInfo::addSignal(std::string name) {
    checkUnique(name);
    m_signals.push_back(std::move(name));
}

const TypeInfo::Factory& TypeInfo::factory() const { return m_factory; }
void TypeInfo::setFactory(Factory factory) { m_factory = std::move(factory); }

} // namespace imlui
