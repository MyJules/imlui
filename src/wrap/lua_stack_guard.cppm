// RAII guard that restores the Lua stack top on scope exit.
export module lua.stack_guard;

import std;

import lua;

export namespace lua {

/// Restores the stack top recorded at construction when it goes out of scope normally. While an exception unwinds it
/// (a Lua error: lua::Raised) it leaves the stack alone: the error value is on top, where the protected call that
/// catches the error takes it from, and that call resets the stack anyway.
class StackGuard {
public:
    explicit StackGuard(State* state);
    ~StackGuard();
    StackGuard(const StackGuard&) = delete;
    StackGuard& operator=(const StackGuard&) = delete;

private:
    State* m_state;
    int m_top;
    int m_exceptions; ///< std::uncaught_exceptions() at construction
};

} // namespace lua

module :private;

namespace lua {

StackGuard::StackGuard(State* state) : m_state{state}, m_top{gettop(state)}, m_exceptions{std::uncaught_exceptions()} {}
StackGuard::~StackGuard() {
    if (std::uncaught_exceptions() == m_exceptions) settop(m_state, m_top);
}

} // namespace lua
