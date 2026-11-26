// LuaTypes: what the Lua language-server definitions say about C++ values (imlui.types.lua_definitions). Each
// LuaConvert<T> describes its values as a LuaLS type expression (luaTypeOf<T>: "integer", "string[]",
// "table<string, Vec2>", "Future<Report>"); the named types those expressions refer to (a struct with a LuaFields
// name becomes a `---@class`, an enum with an EnumNames name an `---@alias`) are declared once in a LuaTypes, which
// the generator writes out sorted by name.
export module imlui.types.lua_types;

import std;

export namespace imlui {

class LuaTypes {
public:
    /// Describes a C++ type: returns its LuaLS type expression and declares the named types it uses in `types`.
    /// TypeInfo keeps one per property, method parameter and result, and typed signal argument (luaTypeOf<V>).
    using Describe = std::string (*)(LuaTypes& types);

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

private:
    std::set<std::string, std::less<>> m_claimed;
    std::map<std::string, std::string, std::less<>> m_definitions;
};

} // namespace imlui

module :private;

namespace imlui {

bool LuaTypes::claim(std::string_view name) { return m_claimed.emplace(name).second; }

void LuaTypes::define(std::string_view name, std::string declaration) {
    m_definitions.insert_or_assign(std::string{name}, std::move(declaration));
}

const std::map<std::string, std::string, std::less<>>& LuaTypes::definitions() const { return m_definitions; }

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

} // namespace imlui
