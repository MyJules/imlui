// The exception Lua throws to raise an error. Lua is compiled as C++ (imlui_lua.cpp): lua_error, luaL_error, a
// memory error ... throw it, and the protected call that catches it (lua_pcall, lua_resume) reads the error from
// the Lua state. Included by imlui_lua.cpp and by the wrapper module (src/wrap/lua.cppm, exported as lua::Raised),
// so C++ code between Lua frames can let it pass: catch it only to rethrow it, never to swallow it.
#pragma once

struct imlui_lua_error {};
