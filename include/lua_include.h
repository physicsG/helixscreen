// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Lua is compiled as C++ (LUA_OBJS in the Makefile), so its API has C++ linkage and a Lua
// error is a C++ exception. Include it only through this header.
#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "lua/lualib.h"
