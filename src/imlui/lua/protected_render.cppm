// A protected entry for a render pass: the last line of defence against a Lua error that an unprotected Lua API
// call raised while rendering (metamethods, a lua::error in a native function reached without pcall, ...).
//
// Without it such an error reaches Lua's panic handler, and Lua aborts the process. Here the pass runs inside a
// protected call: the error is reported like any script error (luaui::errors(), the error overlay), and the ImGui
// stacks the pass left open (Begin/End, BeginChild, tables, Push/Pop) are unwound, so the frame can still end.
//
// It is a last resort, not a way of handling errors: Lua is compiled as C, so its error longjmps past the C++
// frames of the pass without running their destructors. Everything that calls into Lua while rendering should
// still go through luaui::safeCall.
export module imlui.lua.protected_render;

import std;

import lua;
import imgui;
import imlui.lua.errors;

export namespace imlui::luaui {

/// Calls fn(state) in protected mode, with the `nargs` values on top of the stack as its arguments (stack
/// indices 1..nargs inside fn); pops them. A Lua error raised inside, or a C++ exception thrown by fn, is
/// reported (with a traceback) instead of raised, and the ImGui stacks fn left open are closed. Call it inside
/// an ImGui frame. Returns false after an error.
bool renderProtected(lua::State* state, int nargs, std::function<void(lua::State*)> fn);

} // namespace imlui::luaui

module :private;

namespace imlui::luaui {

namespace {

using RenderFn = std::function<void(lua::State*)>;

/// Upvalue 1: the RenderFn (light userdata). A C++ exception becomes a Lua error: it must not unwind through
/// lua::pcall's C frames.
int renderTrampoline(lua::State* state) {
    auto* fn = static_cast<RenderFn*>(lua::touserdata(state, lua::upvalueindex(1)));
    bool failed = false;
    {
        std::string message;
        try {
            (*fn)(state);
        } catch (const std::exception& e) {
            message = std::string{"C++ exception while rendering: "} + e.what();
            failed = true;
        } catch (...) {
            message = "unknown C++ exception while rendering";
            failed = true;
        }
        if (failed) lua::pushstring(state, message);
    }
    if (failed) lua::raise(state); // after `message` is destroyed: raise doesn't return
    return 0;
}

/// Closes what the failed pass left open, back to `saved`, without ImGui's asserts: an error that cut a pass
/// short is the expected case here, not a programming error.
void recoverImGui(const ImGuiErrorRecoveryState& saved) {
    ImGuiIO& io = ImGui::GetIO();
    const bool asserts = io.ConfigErrorRecoveryEnableAssert;
    const bool debugLog = io.ConfigErrorRecoveryEnableDebugLog;
    const bool tooltip = io.ConfigErrorRecoveryEnableTooltip;
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableDebugLog = false;
    io.ConfigErrorRecoveryEnableTooltip = false;
    ImGui::ErrorRecoveryTryToRecoverState(&saved);
    io.ConfigErrorRecoveryEnableAssert = asserts;
    io.ConfigErrorRecoveryEnableDebugLog = debugLog;
    io.ConfigErrorRecoveryEnableTooltip = tooltip;
}

} // namespace

bool renderProtected(lua::State* state, int nargs, std::function<void(lua::State*)> fn) {
    ImGuiErrorRecoveryState saved;
    ImGui::ErrorRecoveryStoreState(&saved);
    lua::pushlightuserdata(state, &fn);
    lua::pushcclosure(state, &renderTrampoline, 1);
    lua::insert(state, -(nargs + 1));
    if (safeCall(state, nargs, 0)) return true;
    recoverImGui(saved);
    return false;
}

} // namespace imlui::luaui
