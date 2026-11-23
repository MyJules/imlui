// RAII guard that restores the Lua stack top on scope exit.
export module lua.stack_guard;

import lua;

export namespace lua {

/// Restores the stack top recorded at construction when it goes out of scope.
class StackGuard {
public:
    explicit StackGuard(State* state);
    ~StackGuard();
    StackGuard(const StackGuard&) = delete;
    StackGuard& operator=(const StackGuard&) = delete;

private:
    State* m_state;
    int m_top;
};

} // namespace lua

module :private;

namespace lua {

StackGuard::StackGuard(State* state) : m_state{state}, m_top{gettop(state)} {}
StackGuard::~StackGuard() { settop(m_state, m_top); }

} // namespace lua
