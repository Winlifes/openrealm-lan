/* Kept in a separate translation unit so both Lua API versions are exercised. */
#include "vendor/lua/src/lua.h"
#include "vendor/lua/src/lauxlib.h"
#include "vendor/lua/src/lualib.h"
#include <string.h>

int test_lua54_independent_vm(void) {
    lua_State *state = luaL_newstate();
    int ok;
    if (!state) return 0;
    luaL_openlibs(state);
    ok = luaL_dostring(state, "assert(_VERSION == 'Lua 5.4'); return 54") == LUA_OK;
    ok = ok && lua_tointeger(state, -1) == 54 && lua_version(state) == 504;
    lua_close(state);
    return ok;
}
