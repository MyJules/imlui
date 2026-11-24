// ScopedEnv: sets an environment variable for one test and restores it on destruction. Test-only.
module;

#include <stdlib.h> // _putenv_s, setenv, unsetenv: not in `import std`

export module imlui.test.scoped_env;

import std;

export namespace imlui::test {

class ScopedEnv {
public:
    /// Sets `name` to `value` until destruction.
    ScopedEnv(std::string name, const std::string& value);
    ~ScopedEnv();
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    ScopedEnv(ScopedEnv&&) = delete;
    ScopedEnv& operator=(ScopedEnv&&) = delete;

private:
    std::string m_name;
    std::optional<std::string> m_old;
};

} // namespace imlui::test

module :private;

namespace imlui::test {

namespace {

// getenv is deprecated by MSVC's CRT; the tests touch the environment from one thread only.
std::optional<std::string> get(const std::string& name) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const char* value = std::getenv(name.c_str());
#pragma clang diagnostic pop
    return value ? std::optional<std::string>{value} : std::nullopt;
}

void set(const std::string& name, const std::optional<std::string>& value) {
#ifdef _WIN32
    // An empty value removes the variable on Windows.
    _putenv_s(name.c_str(), value ? value->c_str() : "");
#else
    if (value)
        setenv(name.c_str(), value->c_str(), 1);
    else
        unsetenv(name.c_str());
#endif
}

} // namespace

ScopedEnv::ScopedEnv(std::string name, const std::string& value) : m_name{std::move(name)}, m_old{get(m_name)} {
    set(m_name, value);
}

ScopedEnv::~ScopedEnv() { set(m_name, m_old); }

} // namespace imlui::test
