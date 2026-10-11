// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "lua_include.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("vendored Lua runs a chunk and reports a runtime error", "[lua]") {
    lua_State* L = luaL_newstate();
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);

    REQUIRE(luaL_dostring(L, "return 6 * 7") == LUA_OK);
    CHECK(lua_tointeger(L, -1) == 42);
    lua_pop(L, 1);

    // luaL_dostring's macro folds any error to 0/1, so the specific code is
    // asserted on the protected call itself.
    REQUIRE(luaL_loadstring(L, "error('boom')") == LUA_OK);
    CHECK(lua_pcall(L, 0, 0, 0) == LUA_ERRRUN);
    lua_close(L);
}

// Only a C++ build of Lua runs this destructor; a C build longjmps past it.
TEST_CASE("a Lua error unwinds C++ destructors in a C function", "[lua]") {
    static int destroyed = 0;
    struct Probe {
        ~Probe() {
            ++destroyed;
        }
    };
    destroyed = 0;

    lua_State* L = luaL_newstate();
    lua_pushcfunction(L, [](lua_State* s) -> int {
        Probe p;
        return luaL_error(s, "raised");
    });
    CHECK(lua_pcall(L, 0, 0, 0) == LUA_ERRRUN);
    CHECK(destroyed == 1);
    lua_close(L);
}

#endif // HELIX_HAS_PLUGINS
