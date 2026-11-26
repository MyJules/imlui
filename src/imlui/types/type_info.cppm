// TypeInfo: the type-erased description of a registered C++ type, filled in by TypeBuilder<T>
// (properties, signals, methods, visual flag, factory) and used by TypeRegistry to bind it to Lua. The LuaLS types of
// the members (LuaTypes::Describe) and the docs of the type and its members are kept for the generated Lua definitions
// (imlui.types.lua_definitions).
export module imlui.types.type_info;

import std;

import lua;
import imlui.types.script_object;
import imlui.types.element;
import imlui.types.lua_types;

export namespace imlui {

class TypeInfo {
public:
    /// Result of assigning a Lua value to a property.
    enum class Assign { Unchanged, Changed, TypeMismatch };

    struct Property {
        std::string name;
        /// What it is, for the Lua definitions (TypeBuilder::doc); "" when undocumented.
        std::string doc;
        /// `name` as a lua::Key: the sync reads the property's field through it. Set by addProperty().
        std::optional<lua::Key> key;
        std::string_view typeName; ///< LuaConvert<V>::name, for error messages
        /// luaTypeOf<V>, for the Lua definitions.
        LuaTypes::Describe luaType = nullptr;
        /// Pushes the getter's value.
        std::function<void(lua::State*, const ScriptObject&)> push;
        /// Converts the value at `idx` and calls the setter if it differs from the getter's value.
        /// Empty for read-only properties.
        std::function<Assign(lua::State*, int, ScriptObject&)> assign;
        /// Why the value at `idx` doesn't convert, after assign() returned TypeMismatch (luaMismatch<V>: "number
        /// expected, got string", or with a path into the value "[2].x: number expected, got string"). Set with
        /// `assign`.
        std::string (*mismatch)(lua::State*, int) = nullptr;
        /// True when nil is a value of the property (std::optional): nil is then synced as std::nullopt.
        bool nullable = false;
        /// True when a Lua function is a value of the property (LuaFunction): the sync then passes a function field on
        /// as the value instead of calling it as a binding.
        bool takesFunction = false;
        /// The value nil resets the property to, captured for an object right after its construction:
        /// the declared default (TypeBuilder::property(name, get, set, defaultValue)), or else a copy of
        /// the getter's value. Empty for read-only and nullable properties, and for value types that
        /// can't be copied (nil then keeps the current value).
        std::function<std::shared_ptr<const void>(const ScriptObject&)> captureDefault;
        /// Sets the property to a value captured by captureDefault (the setter only runs when it differs).
        std::function<Assign(const void*, ScriptObject&)> reset;
    };

    /// Per-object defaults, one entry per property (nullptr where captureDefault is empty).
    using Defaults = std::vector<std::shared_ptr<const void>>;

    /// A method parameter or signal argument, for the Lua definitions: its name and luaTypeOf<A>.
    struct Param {
        std::string name;
        LuaTypes::Describe type = nullptr;
    };

    struct Method {
        std::string name;
        std::string doc; ///< for the Lua definitions; "" when undocumented
        /// Calls the method with the Lua arguments starting at `firstArg`. Returns the number of results
        /// pushed; raises a Lua error for a bad argument or a C++ exception.
        std::function<int(lua::State*, ScriptObject&, int firstArg)> call;
        /// For the Lua definitions: the parameters, and luaTypeOf<R> of the result (nullptr: none, `void`).
        std::vector<Param> params;
        LuaTypes::Describe result = nullptr;
    };

    struct Signal {
        std::string name;
        /// The arguments after `self`, when declared with types (TypeBuilder::signal<Args...>); for the Lua
        /// definitions only (emit() isn't checked against them). std::nullopt: not declared.
        std::optional<std::vector<Param>> args;
        std::string doc; ///< for the Lua definitions; "" when undocumented
    };

    using Factory = std::function<std::unique_ptr<Element>()>;

    explicit TypeInfo(std::string name);

    [[nodiscard]] const std::string& name() const;

    /// What the type is, for the Lua definitions (TypeBuilder::doc); "" when undocumented.
    [[nodiscard]] const std::string& doc() const;
    void setDoc(std::string doc);
    /// Sets the doc of the declared property, method or signal `name`. Returns false when there is none.
    bool setMemberDoc(std::string_view name, std::string doc);

    [[nodiscard]] bool visual() const;
    void setVisual(bool visual);

    /// Whether an element of this type with no `width` fills the available width (like `width = -1`): its own
    /// default, declared with TypeBuilder::fillsWidth() / fillsWidthWhen(). Layouts that share space (Row) read it
    /// before rendering. `object` is the synced instance; it is only looked at when a predicate was declared.
    [[nodiscard]] bool fillsWidth(const ScriptObject* object) const;
    /// Whether fillsWidth() needs the instance (a predicate was declared).
    [[nodiscard]] bool fillsWidthDependsOnInstance() const;
    void setFillsWidth(bool fills);
    void setFillsWidthWhen(std::function<bool(const ScriptObject&)> predicate);

    /// The property a positional first value sets: `Gauge { 0.5 }` with defaultProperty "value".
    [[nodiscard]] const std::string& defaultProperty() const;
    void setDefaultProperty(std::string name);

    [[nodiscard]] const std::vector<Property>& properties() const;
    [[nodiscard]] const std::vector<Method>& methods() const;
    [[nodiscard]] const std::vector<std::string>& signals() const;
    /// The signals with their declared argument types, in declaration order (the same names as signals()).
    [[nodiscard]] const std::vector<Signal>& signalDeclarations() const;
    [[nodiscard]] const Property* findProperty(std::string_view name) const;
    [[nodiscard]] const Method* findMethod(std::string_view name) const;
    [[nodiscard]] bool hasSignal(std::string_view name) const;

    /// Adds a member. A name may only be used once across properties, methods and signals.
    void addProperty(Property property);
    void addMethod(Method method);
    void addSignal(std::string name, std::optional<std::vector<Param>> args = std::nullopt);

    /// Element types have a factory; singleton types don't.
    [[nodiscard]] const Factory& factory() const;
    void setFactory(Factory factory);

    /// Captures the nil-reset values of every property from `object`, just constructed.
    [[nodiscard]] Defaults captureDefaults(const ScriptObject& object) const;
    /// The nil-reset values of the type's elements: captured (captureDefaults) from `first`, the first instance
    /// constructed, and shared by every instance after it. A property's default is its value right after
    /// construction, which member initializers and a constructor's assignments make the same for every instance;
    /// one capture per instance cost an allocation per property for every element built. Main thread only.
    [[nodiscard]] std::shared_ptr<const Defaults> sharedDefaults(const ScriptObject& first) const;

private:
    void checkUnique(std::string_view name) const;

    std::string m_name;
    std::string m_doc;
    bool m_visual = true;
    bool m_fillsWidth = false;
    std::function<bool(const ScriptObject&)> m_fillsWidthWhen;
    std::string m_defaultProperty;
    std::vector<Property> m_properties;
    std::vector<Method> m_methods;
    std::vector<std::string> m_signals;
    std::vector<Signal> m_signalDeclarations;
    Factory m_factory;
    mutable std::shared_ptr<const Defaults> m_sharedDefaults; ///< sharedDefaults(), once captured
};

} // namespace imlui

module :private;

namespace imlui {

TypeInfo::TypeInfo(std::string name) : m_name{std::move(name)} {}

const std::string& TypeInfo::name() const { return m_name; }

const std::string& TypeInfo::doc() const { return m_doc; }
void TypeInfo::setDoc(std::string doc) { m_doc = std::move(doc); }

bool TypeInfo::setMemberDoc(std::string_view name, std::string doc) {
    std::string* target = nullptr;
    for (auto& p : m_properties)
        if (p.name == name) target = &p.doc;
    for (auto& m : m_methods)
        if (m.name == name) target = &m.doc;
    for (auto& s : m_signalDeclarations)
        if (s.name == name) target = &s.doc;
    if (!target) return false;
    *target = std::move(doc);
    return true;
}

bool TypeInfo::visual() const { return m_visual; }
void TypeInfo::setVisual(bool visual) { m_visual = visual; }

bool TypeInfo::fillsWidth(const ScriptObject* object) const {
    if (m_fillsWidthWhen) return object && m_fillsWidthWhen(*object);
    return m_fillsWidth;
}
bool TypeInfo::fillsWidthDependsOnInstance() const { return static_cast<bool>(m_fillsWidthWhen); }
void TypeInfo::setFillsWidth(bool fills) {
    m_fillsWidth = fills;
    m_fillsWidthWhen = nullptr;
}
void TypeInfo::setFillsWidthWhen(std::function<bool(const ScriptObject&)> predicate) {
    m_fillsWidthWhen = std::move(predicate);
}

const std::string& TypeInfo::defaultProperty() const { return m_defaultProperty; }
void TypeInfo::setDefaultProperty(std::string name) { m_defaultProperty = std::move(name); }

const std::vector<TypeInfo::Property>& TypeInfo::properties() const { return m_properties; }
const std::vector<TypeInfo::Method>& TypeInfo::methods() const { return m_methods; }
const std::vector<std::string>& TypeInfo::signals() const { return m_signals; }
const std::vector<TypeInfo::Signal>& TypeInfo::signalDeclarations() const { return m_signalDeclarations; }

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
    property.key.emplace(property.name);
    m_properties.push_back(std::move(property));
}

void TypeInfo::addMethod(Method method) {
    checkUnique(method.name);
    m_methods.push_back(std::move(method));
}

void TypeInfo::addSignal(std::string name, std::optional<std::vector<Param>> args) {
    checkUnique(name);
    m_signals.push_back(name);
    m_signalDeclarations.push_back({.name = std::move(name), .args = std::move(args), .doc = {}});
}

const TypeInfo::Factory& TypeInfo::factory() const { return m_factory; }
void TypeInfo::setFactory(Factory factory) { m_factory = std::move(factory); }

TypeInfo::Defaults TypeInfo::captureDefaults(const ScriptObject& object) const {
    Defaults out;
    out.reserve(m_properties.size());
    for (const auto& p : m_properties)
        out.push_back(p.captureDefault ? p.captureDefault(object) : nullptr);
    return out;
}

std::shared_ptr<const TypeInfo::Defaults> TypeInfo::sharedDefaults(const ScriptObject& first) const {
    if (!m_sharedDefaults) m_sharedDefaults = std::make_shared<const Defaults>(captureDefaults(first));
    return m_sharedDefaults;
}

} // namespace imlui
