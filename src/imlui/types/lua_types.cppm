// LuaTypes: what the Lua language-server definitions say about C++ values (imlui.types.lua_definitions). Each
// LuaConvert<T> describes its values as a LuaLS type expression (luaTypeOf<T>: "integer", "string[]",
// "table<string, Vec2>", "Future<Report>"); the named types those expressions refer to (a struct with a LuaFields
// name becomes a `---@class`, an enum with an EnumNames name an `---@alias`) are declared once in a LuaTypes, which
// the generator writes out sorted by name.
//
// Flows. A LuaTypes also records which way each named type's values go: Output (C++ hands them to Lua: a property's
// value, a method's result, a signal argument) or Input (Lua hands them to C++: a writable property, a method
// parameter, a raw prop). The generator sets the flow before it describes a member (setFlow), a struct with a name
// records it (use) and describes its fields in it too, so a struct inside an output-only one is output-only as well.
// The generator describes everything twice (restart in between): the second pass knows every use, and declares an
// output-only struct with required fields (outputOnly), any other with optional ones.
export module imlui.types.lua_types;

import std;

export namespace imlui {

class LuaTypes {
public:
    /// Describes a C++ type: returns its LuaLS type expression and declares the named types it uses in `types`.
    /// TypeInfo keeps one per property, method parameter and result, and typed signal argument (luaTypeOf<V>).
    using Describe = std::string (*)(LuaTypes& types);

    /// Which way the values being described go (see the module comment).
    enum class Flow {
        Input,  ///< Lua -> C++: a table may leave out a struct's fields (they keep their defaults)
        Output, ///< C++ -> Lua: every field is there
    };

    /// The flow of what is described next; Input until set (the conservative one: optional fields).
    [[nodiscard]] Flow flow() const;
    void setFlow(Flow flow);
    /// Records that named type `name` is used in the current flow. True the first time for that flow: the caller then
    /// describes what the type contains (in the same flow), so the types inside it get the use too.
    bool use(std::string_view name);
    /// Whether named type `name` was used, and only in the Output flow (this pass or one before restart()).
    [[nodiscard]] bool outputOnly(std::string_view name) const;
    /// Starts another pass over the same members: drops the claims and declarations, keeps the uses (and the flow).
    void restart();

    /// True the first time named type `name` is asked for: the caller then builds its declaration and define()s it.
    /// Later calls return false, also while the declaration is still being built (a type that contains itself).
    bool claim(std::string_view name);
    /// The declaration of named type `name`: annotation lines ending in a newline ("---@alias Quality ...\n").
    void define(std::string_view name, std::string declaration);
    /// The declarations by name, sorted (deterministic output).
    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& definitions() const;

    /// The type expression of a LuaConvert without a luaType hook, from its error-message `name`: the LuaLS built-in
    /// types ("boolean", "integer", "number", "string", "table", "function", "nil", "any") as they are, "color" as
    /// `Color`, anything else `any`.
    [[nodiscard]] static std::string fromName(std::string_view name);
    /// `expr` in parentheses when it is a union or a function type, so that a suffix (`?`, `[]`) applies to all of it:
    /// "(integer|string)".
    [[nodiscard]] static std::string grouped(std::string_view expr);
    /// `expr?`, grouped; an expression that already ends in `?` stays as it is.
    [[nodiscard]] static std::string optional(std::string_view expr);
    /// `expr` without a trailing `?` (for a field that is optional already: `---@field max? number`).
    [[nodiscard]] static std::string_view required(std::string_view expr);
    /// Whether `text` is a Lua name (letters, digits, `_`, not starting with a digit, not a keyword).
    [[nodiscard]] static bool isIdentifier(std::string_view text);
    /// `text` as a Lua string literal in double quotes.
    [[nodiscard]] static std::string quoted(std::string_view text);
    /// `text` as LuaLS description lines, `---` and a line of it each (put right above what they describe: a
    /// `---@class`, a `---@field`, a function's annotations); "" for no text. Empty lines at its start and end are
    /// dropped, a `\r` and trailing spaces too, other control characters (a tab too) become spaces. A line whose text
    /// starts with `@` or `|` gets a `\` before it (Markdown shows the character alone), so LuaLS doesn't read an
    /// annotation or an alias value.
    [[nodiscard]] static std::string docLines(std::string_view text);

private:
    /// The flows a named type was used in.
    struct Uses {
        bool input = false;
        bool output = false;
    };

    std::set<std::string, std::less<>> m_claimed;
    std::map<std::string, std::string, std::less<>> m_definitions;
    std::map<std::string, Uses, std::less<>> m_uses;
    Flow m_flow = Flow::Input;
};

} // namespace imlui

module :private;

namespace imlui {

bool LuaTypes::claim(std::string_view name) { return m_claimed.emplace(name).second; }

void LuaTypes::define(std::string_view name, std::string declaration) {
    m_definitions.insert_or_assign(std::string{name}, std::move(declaration));
}

const std::map<std::string, std::string, std::less<>>& LuaTypes::definitions() const { return m_definitions; }

LuaTypes::Flow LuaTypes::flow() const { return m_flow; }
void LuaTypes::setFlow(Flow flow) { m_flow = flow; }

bool LuaTypes::use(std::string_view name) {
    auto it = m_uses.find(name);
    if (it == m_uses.end()) it = m_uses.emplace(std::string{name}, Uses{}).first;
    bool& seen = m_flow == Flow::Output ? it->second.output : it->second.input;
    if (seen) return false;
    seen = true;
    return true;
}

bool LuaTypes::outputOnly(std::string_view name) const {
    const auto it = m_uses.find(name);
    return it != m_uses.end() && it->second.output && !it->second.input;
}

void LuaTypes::restart() {
    m_claimed.clear();
    m_definitions.clear();
}

std::string LuaTypes::fromName(std::string_view name) {
    static constexpr std::array<std::string_view, 8> builtin{"boolean", "integer",  "number", "string",
                                                             "table",   "function", "nil",    "any"};
    if (std::ranges::find(builtin, name) != builtin.end()) return std::string{name};
    if (name == "color") return "Color";
    return "any";
}

std::string LuaTypes::grouped(std::string_view expr) {
    // A union or function type anywhere but inside brackets would bind the suffix to its last part only.
    int depth = 0;
    bool wrap = expr.starts_with("fun(");
    for (const char c : expr) {
        if (c == '(' || c == '<' || c == '[' || c == '{')
            ++depth;
        else if (c == ')' || c == '>' || c == ']' || c == '}')
            --depth;
        else if (c == '|' && depth == 0)
            wrap = true;
    }
    if (!wrap) return std::string{expr};
    return "(" + std::string{expr} + ")";
}

std::string LuaTypes::optional(std::string_view expr) {
    if (expr.ends_with('?')) return std::string{expr};
    return grouped(expr) + "?";
}

std::string_view LuaTypes::required(std::string_view expr) {
    if (expr.ends_with('?')) expr.remove_suffix(1);
    return expr;
}

bool LuaTypes::isIdentifier(std::string_view text) {
    static constexpr std::array<std::string_view, 22> keywords{
        "and", "break", "do",  "else", "elseif", "end",    "false",  "for",  "function", "goto",  "if",
        "in",  "local", "nil", "not",  "or",     "repeat", "return", "then", "true",     "until", "while"};
    if (text.empty() || (text[0] >= '0' && text[0] <= '9')) return false;
    const bool chars = std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
    return chars && std::ranges::find(keywords, text) == keywords.end();
}

std::string LuaTypes::quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string LuaTypes::docLines(std::string_view text) {
    std::vector<std::string> lines{std::string{}};
    for (const char c : text) {
        if (c == '\n')
            lines.emplace_back();
        else if (c != '\r')
            lines.back() += static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c;
    }
    for (std::string& line : lines) {
        while (line.ends_with(' '))
            line.pop_back();
        const std::size_t first = line.find_first_not_of(' ');
        if (first != std::string::npos && (line[first] == '@' || line[first] == '|')) line.insert(first, "\\");
    }
    const auto nonEmpty = [](const std::string& line) { return !line.empty(); };
    const auto begin = std::ranges::find_if(lines, nonEmpty);
    const auto end = std::ranges::find_if(lines.rbegin(), lines.rend(), nonEmpty).base();
    std::string out;
    for (auto it = begin; it < end; ++it)
        out += "---" + *it + "\n";
    return out;
}

} // namespace imlui
