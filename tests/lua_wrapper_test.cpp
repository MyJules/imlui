// Tests for the lua wrapper (Lua compiled as C++: errors are C++ exceptions), lua.stack_guard, imlui.lua.errors and
// imlui.lua.value.
#include <gtest/gtest.h>

import std;

import lua;
import lua.stack_guard;
import imlui.lua.errors;
import imlui.lua.value;

using namespace imlui;

namespace {

class LuaWrapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_state = lua::newstate();
        lua::openlibs(m_state);
        luaui::clearErrors();
    }

    void TearDown() override {
        lua::close(m_state);
        luaui::clearErrors();
    }

    /// Compiles `code` and pushes the resulting chunk (a function).
    void load(std::string_view code) {
        ASSERT_EQ(lua::loadbuffer(m_state, code, "=test"), lua::ok) << lua::tostring(m_state, -1);
    }

    /// Runs `code` and leaves its first result on the stack.
    void run(std::string_view code) {
        load(code);
        ASSERT_EQ(lua::pcall(m_state, 0, 1, 0), lua::ok) << lua::tostring(m_state, -1);
    }

    lua::State* m_state = nullptr;
};

bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

} // namespace

// ------------------------------------------------------------------ Lua errors are C++ exceptions

namespace {

/// Sets `*flag` when destroyed.
struct SetOnDestroy {
    bool* flag;
    explicit SetOnDestroy(bool* f) : flag{f} {}
    ~SetOnDestroy() { *flag = true; }
    SetOnDestroy(const SetOnDestroy&) = delete;
    SetOnDestroy& operator=(const SetOnDestroy&) = delete;
};

bool destroyed = false;

/// A C function with a live C++ object (a std::string too) that raises a Lua error.
int raiseWithLiveObject(lua::State* state) {
    const SetOnDestroy guard{&destroyed};
    const std::string message = "raised with " + std::string{"a live object"};
    lua::error(state, message);
}

/// Arg 1: a function. Calls it unprotected, with a live C++ object in this frame.
int callWithLiveObject(lua::State* state) {
    const SetOnDestroy guard{&destroyed};
    lua::pushvalue(state, 1);
    lua::call(state, 0, 0);
    return 0;
}

int throwBadAlloc(lua::State*) { throw std::bad_alloc{}; }

/// Raises after pushing values under a StackGuard: the guard must leave the error value on top.
int raiseUnderStackGuard(lua::State* state) {
    const lua::StackGuard guard{state};
    lua::pushinteger(state, 1);
    lua::pushinteger(state, 2);
    lua::error(state, "raised under a guard");
}

} // namespace

// The property this build exists for: a Lua error unwinds a C function's frame like a C++ exception, running the
// destructors of its live objects, and the protected call still gets the error message.
TEST_F(LuaWrapperTest, ALuaErrorRunsTheDestructorsOfTheFramesItUnwinds) {
    destroyed = false;
    lua::pushcfunction(m_state, &raiseWithLiveObject);
    ASSERT_NE(lua::pcall(m_state, 0, 0, 0), lua::ok);
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(lua::tostring(m_state, -1), "raised with a live object");
}

// Through Lua frames too: C function -> Lua function -> C function that raises; both C frames are unwound.
TEST_F(LuaWrapperTest, ALuaErrorUnwindsCFramesAcrossLuaFrames) {
    destroyed = false;
    lua::pushcfunction(m_state, &raiseWithLiveObject);
    lua::setglobal(m_state, "raiser");
    lua::pushcfunction(m_state, &callWithLiveObject);
    load("raiser()");
    ASSERT_NE(lua::pcall(m_state, 1, 0, 0), lua::ok);
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(lua::tostring(m_state, -1), "raised with a live object");
}

// C++ code can see the error pass as lua::Raised (and must rethrow it).
TEST_F(LuaWrapperTest, ALuaErrorIsRaisedThroughCxx) {
    lua::pushcfunction(m_state, [](lua::State* state) -> int {
        try {
            lua::error(state, "boom");
        } catch (const lua::Raised&) {
            lua::pushboolean(state, true);
            lua::setglobal(state, "seen");
            throw;
        }
    });
    ASSERT_NE(lua::pcall(m_state, 0, 0, 0), lua::ok);
    EXPECT_EQ(lua::tostring(m_state, -1), "boom");
    lua::getglobal(m_state, "seen");
    EXPECT_TRUE(lua::toboolean(m_state, -1));
}

// A std::bad_alloc that reaches a protected call is a Lua memory error, with Lua's own message.
TEST_F(LuaWrapperTest, BadAllocUnderAProtectedCallIsAMemoryError) {
    lua::pushcfunction(m_state, &throwBadAlloc);
    EXPECT_NE(lua::pcall(m_state, 0, 0, 0), lua::ok);
    EXPECT_EQ(lua::tostring(m_state, -1), "not enough memory");
    load("return pcall(...)"); // from Lua as well
    lua::pushcfunction(m_state, &throwBadAlloc);
    ASSERT_EQ(lua::pcall(m_state, 1, 2, 0), lua::ok);
    EXPECT_FALSE(lua::toboolean(m_state, -2));
    EXPECT_EQ(lua::tostring(m_state, -1), "not enough memory");
}

// catchCxx turns a C++ exception into a Lua error with its message, and lets a Lua error raised inside pass.
TEST_F(LuaWrapperTest, CatchCxxConvertsCxxExceptionsAndPassesLuaErrors) {
    lua::pushcfunction(m_state, [](lua::State* state) -> int {
        return luaui::catchCxx(state, []() -> int { throw std::runtime_error{"bad"}; }, "where", ".detail");
    });
    ASSERT_NE(lua::pcall(m_state, 0, 0, 0), lua::ok);
    EXPECT_EQ(lua::tostring(m_state, -1), "where.detail: bad");
    lua::pushcfunction(m_state, [](lua::State* state) -> int {
        return luaui::catchCxx(state, [state]() -> int { lua::error(state, "a Lua error"); }, "where");
    });
    ASSERT_NE(lua::pcall(m_state, 0, 0, 0), lua::ok);
    EXPECT_EQ(lua::tostring(m_state, -1), "a Lua error");
}

// ------------------------------------------------------------------ StackGuard

TEST_F(LuaWrapperTest, StackGuardRestoresTop) {
    lua::pushinteger(m_state, 1);
    int top = lua::gettop(m_state);
    {
        lua::StackGuard guard{m_state};
        lua::pushinteger(m_state, 2);
        lua::pushstring(m_state, "x");
        lua::newtable(m_state);
        EXPECT_EQ(lua::gettop(m_state), top + 3);
    }
    EXPECT_EQ(lua::gettop(m_state), top);
    EXPECT_EQ(lua::tointeger(m_state, -1), 1);
}

TEST_F(LuaWrapperTest, StackGuardsNest) {
    lua::StackGuard outer{m_state};
    lua::pushnil(m_state);
    {
        lua::StackGuard inner{m_state};
        lua::pushnil(m_state);
        lua::pushnil(m_state);
    }
    EXPECT_EQ(lua::gettop(m_state), 1);
}

// A Lua error unwinding a StackGuard leaves the stack alone: its message stays on top for the protected call.
TEST_F(LuaWrapperTest, StackGuardKeepsTheErrorOfALuaErrorThatUnwindsIt) {
    lua::pushcfunction(m_state, &raiseUnderStackGuard);
    ASSERT_NE(lua::pcall(m_state, 0, 0, 0), lua::ok);
    EXPECT_EQ(lua::tostring(m_state, -1), "raised under a guard");
}

// ------------------------------------------------------------------ safeCall

TEST_F(LuaWrapperTest, SafeCallSuccessLeavesResults) {
    load("return 10, 20");
    int top = lua::gettop(m_state) - 1;
    EXPECT_TRUE(luaui::safeCall(m_state, 0, 2));
    EXPECT_EQ(lua::gettop(m_state), top + 2);
    EXPECT_EQ(lua::tointeger(m_state, -2), 10);
    EXPECT_EQ(lua::tointeger(m_state, -1), 20);
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LuaWrapperTest, SafeCallPassesArguments) {
    load("local a, b = ...; return a .. b");
    lua::pushstring(m_state, "foo");
    lua::pushstring(m_state, "bar");
    EXPECT_TRUE(luaui::safeCall(m_state, 2, 1));
    EXPECT_EQ(lua::tostring(m_state, -1), "foobar");
}

TEST_F(LuaWrapperTest, SafeCallFailureReportsErrorWithTraceback) {
    lua::pushstring(m_state, "below");
    int top = lua::gettop(m_state);
    load("local function inner() error('it broke') end inner()");
    lua::pushinteger(m_state, 1); // an argument, consumed by the call too
    EXPECT_FALSE(luaui::safeCall(m_state, 1, 3));
    // On failure nothing is left: not the function, its arguments or results.
    EXPECT_EQ(lua::gettop(m_state), top);
    EXPECT_EQ(lua::tostring(m_state, -1), "below");
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_TRUE(contains(luaui::errors()[0], "it broke")) << luaui::errors()[0];
    EXPECT_TRUE(contains(luaui::errors()[0], "stack traceback")) << luaui::errors()[0];
}

TEST_F(LuaWrapperTest, SafeCallWithNonStringError) {
    load("error({})");
    EXPECT_FALSE(luaui::safeCall(m_state, 0, 0));
    EXPECT_EQ(lua::gettop(m_state), 0);
    ASSERT_EQ(luaui::errors().size(), 1u);
    EXPECT_TRUE(contains(luaui::errors()[0], "table")) << luaui::errors()[0];
}

// ------------------------------------------------------------------ errors

TEST_F(LuaWrapperTest, ReportErrorDeduplicates) {
    luaui::reportError("same");
    luaui::reportError("same");
    luaui::reportError("other");
    ASSERT_EQ(luaui::errors().size(), 2u);
    EXPECT_EQ(luaui::errors()[0], "same");
    EXPECT_EQ(luaui::errors()[1], "other");
    luaui::clearErrors();
    EXPECT_TRUE(luaui::errors().empty());
}

TEST_F(LuaWrapperTest, ReportErrorKeepsNewest32) {
    for (int i = 0; i < 40; ++i) luaui::reportError("error " + std::to_string(i));
    ASSERT_EQ(luaui::errors().size(), 32u);
    EXPECT_EQ(luaui::errors().front(), "error 8");
    EXPECT_EQ(luaui::errors().back(), "error 39");
}

// ------------------------------------------------------------------ Type

TEST_F(LuaWrapperTest, TypeEnumMatchesValues) {
    run("return { nilValue = nil, b = true, n = 1.5, s = 'x', t = {}, f = print, co = coroutine.create(print) }");
    EXPECT_EQ(lua::getfield(m_state, -1, "nilValue"), lua::Type::Nil);
    lua::pop(m_state);
    EXPECT_EQ(lua::getfield(m_state, -1, "b"), lua::Type::Boolean);
    lua::pop(m_state);
    EXPECT_EQ(lua::getfield(m_state, -1, "n"), lua::Type::Number);
    lua::pop(m_state);
    EXPECT_EQ(lua::getfield(m_state, -1, "s"), lua::Type::String);
    lua::pop(m_state);
    EXPECT_EQ(lua::getfield(m_state, -1, "t"), lua::Type::Table);
    lua::pop(m_state);
    EXPECT_EQ(lua::getfield(m_state, -1, "f"), lua::Type::Function);
    lua::pop(m_state);
    EXPECT_EQ(lua::getfield(m_state, -1, "co"), lua::Type::Thread);
    lua::pop(m_state);

    lua::pushlightuserdata(m_state, m_state);
    EXPECT_EQ(lua::type(m_state, -1), lua::Type::LightUserData);
    lua::pop(m_state);
    EXPECT_EQ(lua::type(m_state, lua::gettop(m_state) + 1), lua::Type::None);
}

TEST_F(LuaWrapperTest, TypeEnumValuesMatchLuaConstants) {
    EXPECT_EQ(static_cast<int>(lua::Type::None), -1);
    EXPECT_EQ(static_cast<int>(lua::Type::Nil), 0);
    EXPECT_EQ(static_cast<int>(lua::Type::Thread), 8);
}

TEST_F(LuaWrapperTest, GlobalAndRawAccessReturnTypes) {
    run("answer = 42; return { 'first' }");
    EXPECT_EQ(lua::rawgeti(m_state, -1, 1), lua::Type::String);
    EXPECT_EQ(lua::tostring(m_state, -1), "first");
    lua::pop(m_state);
    EXPECT_EQ(lua::rawgeti(m_state, -1, 2), lua::Type::Nil);
    lua::pop(m_state);
    EXPECT_EQ(lua::getglobal(m_state, "answer"), lua::Type::Number);
    EXPECT_EQ(lua::tointeger(m_state, -1), 42);
    lua::pop(m_state);
    EXPECT_EQ(lua::getglobal(m_state, "missing"), lua::Type::Nil);
}

TEST_F(LuaWrapperTest, TypeNameAndToStringAny) {
    run("return setmetatable({}, { __tostring = function() return 'custom' end })");
    EXPECT_STREQ(lua::typeName(m_state, -1), "table");
    int top = lua::gettop(m_state);
    EXPECT_EQ(lua::toStringAny(m_state, -1), "custom");
    EXPECT_EQ(lua::gettop(m_state), top);
    lua::pushboolean(m_state, true);
    EXPECT_EQ(lua::toStringAny(m_state, -1), "true");
}

// ------------------------------------------------------------------ Value

TEST_F(LuaWrapperTest, PushValueKinds) {
    luaui::pushValue(m_state, luaui::Value{});
    EXPECT_EQ(lua::type(m_state, -1), lua::Type::Nil);
    luaui::pushValue(m_state, luaui::Value{true});
    EXPECT_EQ(lua::type(m_state, -1), lua::Type::Boolean);
    EXPECT_TRUE(lua::toboolean(m_state, -1));
    luaui::pushValue(m_state, luaui::Value{3.0});
    EXPECT_TRUE(lua::isinteger(m_state, -1));
    EXPECT_EQ(lua::tointeger(m_state, -1), 3);
    luaui::pushValue(m_state, luaui::Value{2.5});
    EXPECT_FALSE(lua::isinteger(m_state, -1));
    EXPECT_DOUBLE_EQ(lua::tonumber(m_state, -1), 2.5);
    luaui::pushValue(m_state, luaui::Value{std::string{"hello"}});
    EXPECT_EQ(lua::tostring(m_state, -1), "hello");
    EXPECT_EQ(lua::gettop(m_state), 5);
}

TEST_F(LuaWrapperTest, IsBindDetectsBindObjects) {
    run("return { yes = { __bind = true }, no = { __bind = false }, plain = {}, str = 'x' }");
    for (const char* key : {"yes", "no", "plain", "str"}) {
        lua::getfield(m_state, -1, key);
        int top = lua::gettop(m_state);
        EXPECT_EQ(luaui::isBind(m_state, -1), std::string_view{key} == "yes") << key;
        EXPECT_EQ(lua::gettop(m_state), top) << key;
        lua::pop(m_state);
    }
}
