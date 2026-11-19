/* Original-version language and real heap/coroutine persistence contracts. */
#include "shared/test.h"
#include "vendor/lua53/namespace.h"
#include "vendor/lua53/src/lua.h"
#include "vendor/lua53/src/lauxlib.h"
#include "vendor/lua53/src/lualib.h"
#include "vendor/eris53/eris.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* A blob outlives its originating VM; no process pointer is kept as saved state. */
typedef struct { char *data; size_t size; } lua53Blob_t;
static lua_KContext lua53_context;
static int lua53_finalized;
static char lua53_error[256];
int test_lua54_independent_vm(void);

static int lua53_after_pause(lua_State *state, int status, lua_KContext context) {
    (void)status;
    lua_pushinteger(state, (lua_Integer)context);
    return 1;
}

static int lua53_pause(lua_State *state) { return lua_yieldk(state, 0, lua53_context, lua53_after_pause); }
static int lua53_gc(lua_State *state) { (void)state; lua53_finalized++; return 0; }

static int lua53_handle(lua_State *state) {
    int value = (int)luaL_checkinteger(state, 1);
    int *data = lua_newuserdata(state, sizeof(*data));
    *data = value;
    luaL_setmetatable(state, "Lua53TestHandle");
    return 1;
}

static int lua53_handle_value(lua_State *state) {
    int *data = luaL_checkudata(state, 1, "Lua53TestHandle");
    lua_pushinteger(state, *data);
    return 1;
}

static int lua53_restore_handle(lua_State *state) {
    lua_settop(state, 0);
    lua_pushvalue(state, lua_upvalueindex(1));
    return lua53_handle(state);
}

/* Exercise Eris's actual userdata constructor protocol, rather than a raw pointer dump. */
static int lua53_persist_handle(lua_State *state) {
    int *data = luaL_checkudata(state, 1, "Lua53TestHandle");
    lua_pushinteger(state, *data);
    lua_pushcclosure(state, lua53_restore_handle, 1);
    return 1;
}

static lua_State *lua53_new(void) {
    lua_State *state = luaL_newstate();
    if (!state) return NULL;
    luaL_openlibs(state);
    lua_pushcfunction(state, lua53_pause);
    lua_setglobal(state, "host_pause");
    lua_pushcfunction(state, lua53_handle);
    lua_setglobal(state, "make_handle");
    lua_pushcfunction(state, lua53_handle_value);
    lua_setglobal(state, "handle_value");
    lua_pushinteger(state, (lua_Integer)lua53_context);
    lua_setglobal(state, "HOST_CONTEXT");
    luaL_newmetatable(state, "Lua53TestHandle");
    lua_pushcfunction(state, lua53_gc);
    lua_setfield(state, -2, "__gc");
    lua_pushcfunction(state, lua53_persist_handle);
    lua_setfield(state, -2, "__persist");
    lua_pop(state, 1);
    return state;
}

/* Function identity is resolved by name in the new VM, including continuation functions. */
static void lua53_permanent(lua_State *state, int table, int value, char const *name, bool reading) {
    value = lua_absindex(state, value);
    if (reading) { lua_pushstring(state, name); lua_pushvalue(state, value); }
    else { lua_pushvalue(state, value); lua_pushstring(state, name); }
    lua_rawset(state, table);
}

static void lua53_perms(lua_State *state, bool reading) {
    char const *libs[] = { "_G", "coroutine", "math", "string", "table", "utf8", "debug", "package", "io", "os", NULL };
    lua_newtable(state);
    int table = lua_gettop(state);
    lua_pushglobaltable(state);
    lua53_permanent(state, table, -1, "globals", reading);
    lua_pop(state, 1);
    for (int i = 0; libs[i]; i++) {
        lua_getglobal(state, libs[i]);
        lua_pushnil(state);
        while (lua_next(state, -2)) {
            if (lua_type(state, -2) == LUA_TSTRING && lua_iscfunction(state, -1)) {
                char name[128];
                snprintf(name, sizeof(name), "%s.%s", libs[i], lua_tostring(state, -2));
                lua53_permanent(state, table, -1, name, reading);
            }
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
    }
    /* Eris uses this value only for identity; it converts the signature back before invocation. */
    lua_pushcfunction(state, (lua_CFunction)(void (*)(void))lua53_after_pause);
    lua53_permanent(state, table, -1, "host.after_pause", reading);
    lua_pop(state, 1);
    lua_pushcfunction(state, lua53_restore_handle);
    lua53_permanent(state, table, -1, "host.restore_handle", reading);
    lua_pop(state, 1);
}

static int lua53_dump(lua_State *state) { eris_persist(state, 1, 2); return 1; }
static int lua53_load(lua_State *state) { eris_unpersist(state, 1, 2); return 1; }

static bool lua53_run(lua_State *state, char const *source) {
    int code = luaL_dostring(state, source);
    if (code != LUA_OK) fprintf(stderr, "Lua53 fixture: %s\n", lua_tostring(state, -1));
    return code == LUA_OK;
}

static lua53Blob_t lua53_save(lua_State *state) {
    lua53Blob_t result = { 0 };
    lua_pushcfunction(state, lua53_dump);
    lua53_perms(state, false);
    lua_getglobal(state, "saved");
    int code = lua_pcall(state, 2, 1, 0);
    T_EQ(code, LUA_OK);
    if (code == LUA_OK) {
        char const *data = lua_tolstring(state, -1, &result.size);
        result.data = malloc(result.size);
        T_NOT_NULL(result.data);
        if (result.data) memcpy(result.data, data, result.size);
    } else fprintf(stderr, "Lua53 persist: %s\n", lua_tostring(state, -1));
    lua_pop(state, 1);
    return result;
}

static bool lua53_restore(lua_State *state, lua53Blob_t blob) {
    lua53_error[0] = '\0';
    lua_pushcfunction(state, lua53_load);
    lua53_perms(state, true);
    lua_pushlstring(state, blob.data, blob.size);
    int code = lua_pcall(state, 2, 1, 0);
    if (code != LUA_OK) {
        snprintf(lua53_error, sizeof(lua53_error), "%s", lua_tostring(state, -1));
        fprintf(stderr, "Lua53 unpersist: %s\n", lua53_error);
        lua_pop(state, 1);
        return false;
    }
    lua_setglobal(state, "restored");
    return true;
}

/* Close the complete source VM before loading; the destination owns fresh objects. */
static void lua53_round_trip(char const *setup, char const *verify) {
    lua_State *state = lua53_new();
    T_NOT_NULL(state);
    if (!state) return;
    T_ASSERT(lua53_run(state, setup));
    lua53Blob_t blob = lua53_save(state);
    lua_close(state);
    if (!blob.data) return;
    state = lua53_new();
    T_NOT_NULL(state);
    if (state) {
        bool loaded = lua53_restore(state, blob);
        T_ASSERT(loaded);
        if (loaded) T_ASSERT(lua53_run(state, verify));
        lua_close(state);
    }
    free(blob.data);
}

TEST(wc3_lua53, original_language_vm_coexists_with_lua54) {
    lua_State *state = lua53_new();
    T_NOT_NULL(state);
    if (!state) return;
    T_STREQ(LUA_RELEASE, "Lua 5.3.4");
    T_EQ(*lua_version(state), 503);
    T_ASSERT(lua53_run(state, "assert(_VERSION=='Lua 5.3'); assert(math.type(1)=='integer'); assert(7//2==3)"));
    T_ASSERT(lua53_run(state, "local f=assert(load(string.dump(function() return 13 end))); assert(f()==13)"));
    T_ASSERT(test_lua54_independent_vm());
    T_ASSERT(lua53_run(state, "assert(_VERSION=='Lua 5.3'); assert(string.unpack('i8',string.pack('i8',4294967313))==4294967313)"));
    lua_close(state);
}

TEST(wc3_lua53, tables_closures_and_shared_upvalues_survive_fresh_vm) {
    lua53_round_trip(
        "local t={n=5}; t.self=t; t[t]='key'; "
        "saved={a=t,b=t,read=function() return t.n end,add=function(n) t.n=t.n+n end}",
        "local r=restored; assert(rawequal(r.a,r.b)); assert(rawequal(r.a,r.a.self)); "
        "assert(r.a[r.a]=='key'); assert(r.read()==5); r.add(7); assert(r.read()==12)");
}

TEST(wc3_lua53, userdata_aliases_restore_through_constructor_and_finalize_once) {
    lua53_finalized = 0;
    lua53_round_trip("local h=make_handle(37); saved={a=h,b=h}",
        "assert(rawequal(restored.a,restored.b)); assert(handle_value(restored.a)==37)");
    T_EQ(lua53_finalized, 2);
}

TEST(wc3_lua53, yielded_lua_thread_retains_stack_locals_and_closure_state) {
    lua53_round_trip(
        "local t={n=5}; local co=coroutine.create(function() local n=9; coroutine.yield(); t.n=t.n+n; return t.n end); "
        "assert(coroutine.resume(co)); saved={t=t,co=co}",
        "local r=restored; assert(coroutine.status(r.co)=='suspended'); "
        "local ok,n=coroutine.resume(r.co); assert(ok and n==14 and r.t.n==14); assert(coroutine.status(r.co)=='dead')");
}

TEST(wc3_lua53, c_continuation_preserves_pointer_width_context) {
    lua53_context = sizeof(lua_KContext) > 4 ? (lua_KContext)UINT64_C(4294967313) : 17;
    lua53_round_trip(
        "local co=coroutine.create(function() local n=9; return host_pause()+n end); "
        "assert(coroutine.resume(co)); saved={co=co,context=HOST_CONTEXT}",
        "local r=restored; local ok,n=coroutine.resume(r.co); "
        "assert(ok and n==r.context+9,'continuation context: '..tostring(ok)..' / '..tostring(n))");
}

TEST(wc3_lua53, c_continuation_with_zero_status_flags_resumes_once) {
    lua53_context = 17;
    lua53_round_trip(
        "local co=coroutine.create(function() return host_pause() end); "
        "assert(coroutine.resume(co)); saved={co=co}",
        "local ok,n=coroutine.resume(restored.co); assert(ok and n==17,'C continuation omitted'); "
        "assert(coroutine.status(restored.co)=='dead')");
}

TEST(wc3_lua53, unstarted_coroutine_retains_its_initial_entrypoint) {
    lua53_round_trip("saved={co=coroutine.create(function() return 37 end)}",
        "local ok,n=coroutine.resume(restored.co); assert(ok and n==37)");
}

TEST(wc3_lua53, yielded_comparison_metamethod_retains_operator_continuation) {
    lua53_round_trip(
        "local t=setmetatable({},{__lt=function(a,b) coroutine.yield(); return false end}); "
        "local co=coroutine.create(function() return t<=t end); assert(coroutine.resume(co)); saved={co=co}",
        "local ok,result=coroutine.resume(restored.co); assert(ok and result==true)");
}

TEST(wc3_lua53, c_continuation_preserves_large_negative_context) {
    lua53_context = sizeof(lua_KContext) > 4 ? (lua_KContext)-INT64_C(1099511627759) : -17;
    lua53_round_trip(
        "local co=coroutine.create(function() return host_pause() end); "
        "assert(coroutine.resume(co)); saved={co=co,context=HOST_CONTEXT}",
        "local r=restored; local ok,n=coroutine.resume(r.co); assert(ok and n==r.context,'negative continuation context truncated')");
}

TEST(wc3_lua53, protected_error_after_restored_wait_reaches_original_handler) {
    lua53_context = 17;
    lua53_round_trip(
        "local co=coroutine.create(function() "
        "local ok,v=xpcall(function() host_pause(); error('expected failure') end,function(e) return 'handled:'..e end); "
        "return ok,v end); assert(coroutine.resume(co)); saved={co=co}",
        "local ok,inner,msg=coroutine.resume(restored.co); assert(ok and inner==false); "
        "assert(msg:match('handled:.*expected failure'))");
}

TEST(wc3_lua53, wrapped_coroutine_retains_native_wrapper_identity) {
    lua53_round_trip(
        "local co=coroutine.wrap(function() local n=11; coroutine.yield(n); return n+1 end); "
        "assert(co()==11); saved={co=co}",
        "assert(restored.co()==12)");
}

TEST(wc3_lua53, malformed_blob_is_rejected_inside_protected_call) {
    lua_State *state = lua53_new();
    T_NOT_NULL(state);
    if (!state) return;
    T_ASSERT(lua53_run(state, "saved={n=17}"));
    lua53Blob_t blob = lua53_save(state);
    if (blob.data) {
        blob.data[0] ^= 1;
        T_ASSERT(!lua53_restore(state, blob));
        T_ASSERT(lua53_run(state, "assert(saved.n==17); assert(restored==nil)"));
        free(blob.data);
    }
    lua_close(state);
}

TEST(wc3_lua53, prior_eris_stream_is_rejected_without_migration) {
    lua_State *state = lua53_new();
    T_NOT_NULL(state);
    if (!state) return;
    T_ASSERT(lua53_run(state, "saved={n=17}"));
    lua53Blob_t blob = lua53_save(state);
    if (blob.data) {
        memcpy(blob.data, "ERIS", 4);
        T_ASSERT(!lua53_restore(state, blob));
        T_ASSERT(lua53_run(state, "assert(saved.n==17); assert(restored==nil)"));
        free(blob.data);
    }
    lua_close(state);
}

TEST(wc3_lua53, another_pointer_width_is_rejected_before_body_decode) {
    lua_State *state = lua53_new();
    T_NOT_NULL(state);
    if (!state) return;
    T_ASSERT(lua53_run(state, "saved={n=17}"));
    lua53Blob_t blob = lua53_save(state);
    if (blob.data) {
        size_t size_pos = 4 + 1 + sizeof(lua_Number) + 1 + 1;
        blob.data[size_pos] = sizeof(size_t) == 8 ? 4 : 8;
        T_ASSERT(!lua53_restore(state, blob));
        T_STREQ(lua53_error, "incompatible snapshot layout");
        T_ASSERT(lua53_run(state, "assert(saved.n==17); assert(restored==nil)"));
        free(blob.data);
    }
    lua_close(state);
}
