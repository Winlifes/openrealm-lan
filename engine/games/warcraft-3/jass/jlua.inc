/* Lua map execution shares the existing typed host API and JASS scheduler.
 * Included by jdo.c so there is one owner of values, handles and frame state. */
#include "vendor/lua53/namespace.h"
#include "vendor/lua53/src/lua.h"
#include "vendor/lua53/src/lauxlib.h"
#include "vendor/lua53/src/lualib.h"
#include "vendor/eris53/eris.h"

#define BZ_JLUA_HANDLE "Warcraft.Handle" // registry key; typed, retained VM value
#define BZ_JLUA_ARRAY "Warcraft.Array" // registry key; live JASS-global array view
#define BZ_JLUA_HEAP_LIMIT (64u << 20) // bytes; bounds one private heap snapshot

typedef struct { uint8_t *bytes; size_t size, capacity, position; bool reading; } jassLuaBuffer_t;
typedef struct { jassFunc_t *function; uint32_t previous; } jassLuaDeclaration_t;

struct jass_lua {
    jass_t *root, *active;
    lua_State *state, *running;
    int callbacks, identities, jobs, permanents, pending;
    int enum_handles, pending_enum_handles;
    uint32_t next_callback, next_job, active_job;
    bool asynchronous;
    uint32_t source_identity;
    jassSnapshot_t *snapshot;
    jassFunc_t *declarations_before_load;
    jassLuaDeclaration_t *declaration_changes;
    uint32_t declaration_changes_count;
    bool staging;
};

typedef struct { jassVar_t value; } jassLuaHandle_t;

static jassLua_t *jlua_vm(lua_State *state) { return *(jassLua_t **)lua_getextraspace(state); }
static jass_t *jlua_jass(lua_State *state) {
    jassLua_t *vm = jlua_vm(state);
    return vm->active ? vm->active : vm->root;
}
static void jlua_pushvalue(lua_State *state, jass_t *j, jassVar_t const *value);
static void jlua_pushargument(lua_State *state, int index, jass_t *j, jassType_t const *type);
static int jlua_proxy(lua_State *state);
static int jlua_persist_handle(lua_State *state);
static int jlua_restore_handle(lua_State *state);
static int jlua_restore_array(lua_State *state);
static int jlua_persist_array(lua_State *state);
static void jlua_initialize_permanents(jassLua_t *vm);

/* Lua scripts commonly save a BJ/native in a local before overriding its
 * global name. That captured binding must not resolve back to the new alias. */
static jassFunc_t const *jlua_original_function(jass_t *j, cstring_t name) {
    jass_t *root = jass_root(j);
    for (jassFunc_t const *func = root->function_hash[jass_hash(name)]; func; func = func->hash_next)
        if (!func->lua_id && !strcmp(func->name, name)) return func;
    return NULL;
}

/* The engine stores rawcodes in little-endian byte order. Lua keeps retail's
 * numeric FourCC value; convert only typed rawcode API slots, never arbitrary integers. */
static uint32_t jlua_swap_code(uint32_t value) {
    return (value >> 24) | ((value >> 8) & 0xff00u) | ((value << 8) & 0xff0000u) | (value << 24);
}

static bool jlua_raw_argument(cstring_t name) {
    static cstring_t const names[] = {
        "abilCode", "abilId", "abilityId", "buffId", "buffcode", "doodadID", "effectID",
        "itemId", "objectId", "portraitUnitId", "skinId", "techid", "terrainType", "unitId",
        "unitType", "newUnitId", "inID", "inItemID", "pickedItemId", NULL
    };
    for (uint32_t i = 0; names[i]; i++) if (!strcasecmp(name, names[i])) return true;
    return false;
}

static bool jlua_raw_return(cstring_t name) {
    static cstring_t const names[] = {
        "UnitId", "GetUnitTypeId", "GetItemTypeId", "GetDestructableTypeId", "GetSpellAbilityId",
        "GetLearnedSkill", "GetResearched", "GetTrainedUnitType",
        "ChooseRandomCreep", "ChooseRandomNPBuilding", "ChooseRandomItem", "ChooseRandomItemEx",
        "ChooseRandomItemBJ", "ChooseRandomItemExBJ", "RandomDistChoose", "GetTerrainType",
        "BlzGetUnitSkin", "BlzGetItemSkin", "BlzGetDestructableSkin", NULL
    };
    for (uint32_t i = 0; names[i]; i++) if (!strcmp(name, names[i])) return true;
    return false;
}

static int jlua_fourcc(lua_State *state) {
    size_t size;
    uint8_t const *text = (uint8_t const *)luaL_checklstring(state, 1, &size);
    if (size != 4) return luaL_error(state, "FourCC requires exactly four bytes");
    uint32_t value = ((uint32_t)text[0] << 24) | ((uint32_t)text[1] << 16) | ((uint32_t)text[2] << 8) | text[3];
    lua_pushinteger(state, (int32_t)value);
    return 1;
}

static int jlua_array_default(lua_State *state) { lua_pushvalue(state, lua_upvalueindex(1)); return 1; }

static int jlua_new_array(lua_State *state) {
    lua_settop(state, 1);
    lua_newtable(state);
    lua_newtable(state);
    lua_pushvalue(state, 1);
    lua_pushcclosure(state, jlua_array_default, 1);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, -2);
    return 1;
}

/* A Lua userdata retains the complete VM value, including its owned-handle refcount. */
static int jlua_handle_gc(lua_State *state) {
    jassLuaHandle_t *handle = luaL_checkudata(state, 1, BZ_JLUA_HANDLE);
    jass_setnull(&handle->value);
    return 0;
}

static int jlua_handle_equal(lua_State *state) {
    jassLuaHandle_t *a = luaL_checkudata(state, 1, BZ_JLUA_HANDLE);
    jassLuaHandle_t *b = luaL_testudata(state, 2, BZ_JLUA_HANDLE);
    bool equal = b && a->value.value == b->value.value;
    if (b && a->value.value && b->value.value && a->value.type == b->value.type &&
        jass_valuehandle(a->value.type->name)) equal = !memcmp(a->value.value, b->value.value, sizeof(uint32_t));
    lua_pushboolean(state, equal);
    return 1;
}

/* Map libraries parse the declared Warcraft type before the colon. The
 * address is opaque diagnostic text, never a persistent handle identifier. */
static int jlua_handle_string(lua_State *state) {
    jassLuaHandle_t *handle = luaL_checkudata(state, 1, BZ_JLUA_HANDLE);
    lua_pushfstring(state, "%s: %p", handle->value.type->name, handle->value.value);
    return 1;
}

/* Lua table keys use raw userdata identity, bypassing __eq. Enums are values
 * within a concrete Warcraft type, not pointers to their temporary payloads. */
static int jlua_enum_cache(lua_State *state) {
    lua_newtable(state);
    lua_newtable(state);
    lua_pushliteral(state, "v"); lua_setfield(state, -2, "__mode");
    lua_setmetatable(state, -2);
    return luaL_ref(state, LUA_REGISTRYINDEX);
}

static void jlua_enum_key(lua_State *state, jassVar_t const *value) {
    lua_pushfstring(state, "%s:%I", value->type->name, (lua_Integer)*(uint32_t const *)value->value);
}

static bool jlua_cached_enum(lua_State *state, jassVar_t const *value) {
    jassLua_t *vm = jlua_vm(state);
    int cache = vm->pending_enum_handles != LUA_NOREF ? vm->pending_enum_handles : vm->enum_handles;
    lua_rawgeti(state, LUA_REGISTRYINDEX, cache);
    jlua_enum_key(state, value); lua_rawget(state, -2);
    lua_remove(state, -2);
    if (!lua_isnil(state, -1)) return true;
    lua_pop(state, 1); return false;
}

static void jlua_remember_enum(lua_State *state, int index) {
    jassLua_t *vm = jlua_vm(state);
    int cache = vm->pending_enum_handles != LUA_NOREF ? vm->pending_enum_handles : vm->enum_handles;
    index = lua_absindex(state, index);
    jassLuaHandle_t *handle = lua_touserdata(state, index);
    lua_rawgeti(state, LUA_REGISTRYINDEX, cache);
    jlua_enum_key(state, &handle->value); lua_pushvalue(state, index); lua_rawset(state, -3);
    lua_pop(state, 1);
}

static jassFunc_t *jlua_declare(jass_t *root, cstring_t name, uint32_t id) {
    jassFunc_t *func = JASSALLOC(jassFunc_t);
    char *owned_name = jass_alloc(strlen(name) + 1);
    strcpy(owned_name, name);
    func->name = owned_name;
    func->lua_id = id;
    func->returns = &jass_types[jasstype_handle];
    ADD_TO_LIST(func, root->functions);
    jassFunc_t **bucket = &root->function_hash[jass_hash(name)];
    func->hash_next = *bucket;
    *bucket = func;
    return func;
}

/* Every closure has a unique internal code identity; named entry aliases can
 * change without redirecting already-registered timers and trigger actions. */
static jassFunc_t const *jlua_callback(lua_State *state, int index, cstring_t name) {
    jassLua_t *vm = jlua_vm(state);
    jass_t *root = vm->root;
    uint32_t id;
    index = lua_absindex(state, index);
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->identities);
    lua_pushvalue(state, index);
    lua_rawget(state, -2);
    id = (uint32_t)lua_tointeger(state, -1);
    lua_pop(state, 2);
    bool create = !id;
    if (create) id = ++vm->next_callback;
    char generated[64];
    snprintf(generated, sizeof(generated), "__lua_callback_%u", id);
    jassFunc_t const *func = find_function(root, generated);
    if (!func) func = jlua_declare(root, generated, id);
    if (name) {
        jassFunc_t const *alias = find_function(root, name);
        if (!alias || alias->lua_id != id) jlua_declare(root, name, id);
    }
    if (!create) return func;
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->callbacks);
    lua_pushvalue(state, index);
    lua_rawseti(state, -2, id);
    lua_pop(state, 1);
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->identities);
    lua_pushvalue(state, index);
    lua_pushinteger(state, id);
    lua_rawset(state, -3);
    lua_pop(state, 1);
    return func;
}

static void jlua_checkargument(lua_State *state, int index, jassType_t const *type) {
    JASSTYPEID base = get_base_type(type) - jass_types;
    if (lua_isnoneornil(state, index) && (base == jasstype_handle || base == jasstype_code || base == jasstype_string || base == jasstype_boolean)) return;
    switch (base) {
        case jasstype_integer: luaL_checkinteger(state, index); break;
        case jasstype_real: luaL_checknumber(state, index); break;
        case jasstype_string: luaL_checkstring(state, index); break;
        case jasstype_boolean: luaL_checktype(state, index, LUA_TBOOLEAN); break;
        case jasstype_code: luaL_checktype(state, index, LUA_TFUNCTION); break;
        case jasstype_handle: {
            jassLuaHandle_t *handle = luaL_checkudata(state, index, BZ_JLUA_HANDLE);
            if (!is_handle_convertible(handle->value.type, type)) luaL_argerror(state, index, type->name);
            break;
        }
        default: luaL_error(state, "unsupported native argument type");
    }
}

static void jlua_pushargument(lua_State *state, int index, jass_t *j, jassType_t const *type) {
    JASSTYPEID base = get_base_type(type) - jass_types;
    if (lua_isnoneornil(state, index)) {
        if (base == jasstype_handle) jass_pushnullhandle(j, type->name);
        else if (base == jasstype_string) jass_pushstring(j, NULL);
        else if (base == jasstype_boolean) jass_pushboolean(j, false);
        else if (base == jasstype_code) jass_pushfunction(j, NULL);
        else jass_pushnull(j);
        return;
    }
    switch (base) {
        case jasstype_integer: jass_pushinteger(j, (int32_t)lua_tointeger(state, index)); break;
        case jasstype_real: jass_pushnumber(j, (float)lua_tonumber(state, index)); break;
        case jasstype_string: jass_pushstring(j, lua_tostring(state, index)); break;
        case jasstype_boolean: jass_pushboolean(j, lua_toboolean(state, index)); break;
        case jasstype_code: jass_pushfunction(j, jlua_callback(state, index, NULL)); break;
        case jasstype_handle: {
            jassLuaHandle_t *handle = lua_touserdata(state, index);
            jass_pushnullhandle(j, type->name);
            jass_copy(j, jass_topvalue(j), &handle->value);
            break;
        }
        default: break;
    }
}

static void jlua_pushvalue(lua_State *state, jass_t *j, jassVar_t const *value) {
    JASSTYPEID base = jass_getvarbasetype(value);
    if (!value->value && base != jasstype_integer && base != jasstype_real && base != jasstype_boolean) {
        lua_pushnil(state);
        return;
    }
    switch (base) {
        case jasstype_integer: lua_pushinteger(state, value->value ? *(int32_t *)value->value : 0); break;
        case jasstype_real: lua_pushnumber(state, value->value ? *(float *)value->value : 0); break;
        case jasstype_boolean: lua_pushboolean(state, value->value && *(bool *)value->value); break;
        case jasstype_string: lua_pushstring(state, value->value); break;
        case jasstype_code: {
            jassFunc_t const *func = value->value;
            jassLua_t *vm = jlua_vm(state);
            if (func->lua_id) {
                lua_rawgeti(state, LUA_REGISTRYINDEX, vm->callbacks);
                lua_rawgeti(state, -1, func->lua_id);
                lua_remove(state, -2);
            } else {
                lua_pushstring(state, func->name);
                lua_pushcclosure(state, jlua_proxy, 1);
            }
            break;
        }
        case jasstype_handle: {
            if (value->ref && value->ref->invalid) { lua_pushnil(state); break; }
            bool enumeration = jass_valuehandle(value->type->name);
            if (enumeration && jlua_cached_enum(state, value)) break;
            jassLuaHandle_t *handle = lua_newuserdata(state, sizeof(*handle));
            memset(handle, 0, sizeof(*handle));
            handle->value.type = value->type;
            jass_copy(j, &handle->value, value);
            luaL_setmetatable(state, BZ_JLUA_HANDLE);
            if (enumeration) jlua_remember_enum(state, -1);
            break;
        }
        default: lua_pushnil(state); break;
    }
}

/* Lua checks all arguments before any JASS stack mutation, including protected-call failures. */
static int jlua_after_jass(lua_State *state, int status, lua_KContext context) {
    (void)status;
    return lua_gettop(state) - (int)context;
}

static int jlua_proxy(lua_State *state) {
    jassLua_t *vm = jlua_vm(state);
    jass_t *j = jlua_jass(state), *root = vm->root;
    cstring_t name = lua_tostring(state, lua_upvalueindex(1));
    jassFunc_t const *func = jlua_original_function(root, name);
    jasscoroutine_t *co = root->current_coroutine;
    uint32_t arguments = 0, results = 0, base = j->num_stack;
    jassVar_t *saved_pointer = j->stack_pointer;
    if (!func || (func->native && !func->nativefunc)) return luaL_error(state, "unbound native: %s", name);
    FOR_EACH_LIST(jassarg_t const, arg, func->args) jlua_checkargument(state, ++arguments, arg->type);
    if ((uint32_t)lua_gettop(state) > arguments) return luaL_error(state, "%s: expected %d arguments", name, (int)arguments);
    /* Missing trailing nullable arguments are Lua nil; materialize them only
     * after all supplied/required values passed validation, before a BJ yields. */
    lua_settop(state, (int)arguments);
    jass_pushfunction(j, func);
    uint32_t index = 1;
    FOR_EACH_LIST(jassarg_t const, arg, func->args) {
        jlua_pushargument(state, index++, j, arg->type);
        if (arg->type == &jass_types[jasstype_integer] && jlua_raw_argument(arg->name))
            *(uint32_t *)jass_topvalue(j)->value = jlua_swap_code(*(uint32_t *)jass_topvalue(j)->value);
    }
    /* A BJ child runs on the same heap-frame scheduler, so waits do not leave a live C stack. */
    if (!func->native && !func->lua_id && co && vm->asynchronous) {
        jassdict_t *locals = NULL;
        index = 1;
        FOR_EACH_LIST(jassarg_t const, arg, func->args) {
            jassdict_t *local = JASSALLOC(jassdict_t);
            local->key = arg->name;
            local->value.type = arg->type;
            jass_copy(j, &local->value, j->stack + base + index++);
            PUSH_BACK(jassdict_t, local, locals);
        }
        jass_discard(j, j->num_stack - base);
        jass_coroutine_pushframe(co, JASS_FRAME_FUNCTION, func, func->code, locals)->return_lua_job = vm->active_job;
        return lua_yieldk(state, 0, arguments, jlua_after_jass);
    }
    /* Catch a native's longjmp here, before it can cross Lua's protected C frames. */
    jassErrorFrame_t boundary = { .previous = root->error_boundary };
    bool previous_failure = root->rterror_pending;
    char previous_message[sizeof(root->rterror_message)];
    memcpy(previous_message, root->rterror_message, sizeof(previous_message));
    root->error_boundary = &boundary;
    if (setjmp(boundary.jump)) {
        char error[sizeof(root->rterror_message)];
        snprintf(error, sizeof(error), "%s", root->rterror_message);
        root->error_boundary = boundary.previous;
        jass_discard(j, j->num_stack - base);
        j->stack_pointer = saved_pointer;
        root->rterror_pending = previous_failure;
        memcpy(root->rterror_message, previous_message, sizeof(previous_message));
        return luaL_error(state, "%s", error);
    }
    results = jass_call(j, arguments);
    root->error_boundary = boundary.previous;
    FOR_LOOP(i, results) {
        jassVar_t const *value = j->stack + j->num_stack - results + i;
        if (jlua_raw_return(name) && jass_getvarbasetype(value) == jasstype_integer)
            lua_pushinteger(state, (int32_t)jlua_swap_code(*(uint32_t *)value->value));
        else jlua_pushvalue(state, j, value);
    }
    jass_discard(j, results);
    if (co && co->yielded) {
        if (!vm->asynchronous) return luaL_error(state, "wait in a synchronous callback: %s", name);
        return lua_yieldk(state, results, arguments, jlua_after_jass);
    }
    return results;
}

static jassVar_t *jlua_array(lua_State *state) {
    char const *name = luaL_checkudata(state, 1, BZ_JLUA_ARRAY);
    return find_global(jlua_vm(state)->root, name);
}

static int jlua_array_index(lua_State *state) {
    jassVar_t *array = jlua_array(state);
    uint32_t index = (uint32_t)luaL_checkinteger(state, 2);
    jassVar_t const *value = find_array_value(array, index);
    jassVar_t empty = { .type = array->type };
    jlua_pushvalue(state, jlua_jass(state), value ? value : &empty);
    return 1;
}

static int jlua_array_set(lua_State *state) {
    jass_t *j = jlua_jass(state);
    jassVar_t *array = jlua_array(state);
    uint32_t index = (uint32_t)luaL_checkinteger(state, 2);
    jlua_checkargument(state, 3, array->type);
    jlua_pushargument(state, 3, j, array->type);
    jass_copy(j, ensure_array_value(j, array, index), jass_topvalue(j));
    jass_discard(j, 1);
    return 0;
}

static int jlua_global_index(lua_State *state) {
    jass_t *j = jlua_jass(state), *root = jlua_vm(state)->root;
    cstring_t name = luaL_checkstring(state, 2);
    jassVar_t *value = find_global(root, name);
    if (value && value->array) {
        char *array = lua_newuserdata(state, strlen(name) + 1);
        strcpy(array, name);
        luaL_setmetatable(state, BZ_JLUA_ARRAY);
    } else if (value) jlua_pushvalue(state, j, value);
    else if (jlua_original_function(root, name)) {
        lua_pushvalue(state, 2);
        lua_pushcclosure(state, jlua_proxy, 1);
    } else lua_pushnil(state);
    return 1;
}

static int jlua_global_set(lua_State *state) {
    jass_t *j = jlua_jass(state);
    cstring_t name = luaL_checkstring(state, 2);
    jassVar_t *value = find_global(jlua_vm(state)->root, name);
    if (!value) { lua_rawset(state, 1); return 0; }
    if (value->constant || value->array) return luaL_error(state, "cannot assign JASS declaration: %s", name);
    jlua_checkargument(state, 3, value->type);
    jlua_pushargument(state, 3, j, value->type);
    jass_copy(j, value, jass_topvalue(j));
    jass_discard(j, 1);
    return 0;
}

static bool jlua_initialize(jass_t *j) {
    jassLua_t *vm = JASSALLOC(jassLua_t);
    vm->root = j;
    vm->state = luaL_newstate();
    if (!vm->state) { jass_free(vm); jass_setruntimeerror(j, "cannot allocate Lua map runtime"); return false; }
    *(jassLua_t **)lua_getextraspace(vm->state) = vm;
    j->lua = vm;
    lua_State *state = vm->state;
    luaL_openlibs(state);
    lua_pushcfunction(state, jlua_fourcc); lua_setglobal(state, "FourCC");
    lua_pushcfunction(state, jlua_new_array); lua_setglobal(state, "__jarray");
    lua_newtable(state); vm->callbacks = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_newtable(state); vm->identities = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_newtable(state); vm->jobs = luaL_ref(state, LUA_REGISTRYINDEX);
    vm->enum_handles = jlua_enum_cache(state);
    vm->pending_enum_handles = LUA_NOREF;
    luaL_newmetatable(state, BZ_JLUA_HANDLE);
    lua_pushcfunction(state, jlua_handle_gc); lua_setfield(state, -2, "__gc");
    lua_pushcfunction(state, jlua_handle_equal); lua_setfield(state, -2, "__eq");
    lua_pushcfunction(state, jlua_handle_string); lua_setfield(state, -2, "__tostring");
    lua_pushcfunction(state, jlua_persist_handle); lua_setfield(state, -2, "__persist");
    lua_pop(state, 1);
    luaL_newmetatable(state, BZ_JLUA_ARRAY);
    lua_pushcfunction(state, jlua_array_index); lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, jlua_array_set); lua_setfield(state, -2, "__newindex");
    lua_pushcfunction(state, jlua_persist_array); lua_setfield(state, -2, "__persist");
    lua_pop(state, 1);
    vm->pending = LUA_NOREF;
    jlua_initialize_permanents(vm);
    lua_pushglobaltable(state);
    lua_newtable(state);
    lua_pushcfunction(state, jlua_global_index); lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, jlua_global_set); lua_setfield(state, -2, "__newindex");
    lua_setmetatable(state, -2);
    lua_pop(state, 1);
    return true;
}

/* Preserve the source bytes: JASS comment stripping must never process Lua strings. */
static bool jass_lua_load(jass_t *j, cstring_t source, size_t size, cstring_t name) {
    jass_t *root = jass_root(j);
    if (!root->lua && !jlua_initialize(root)) return false;
    jassLua_t *vm = root->lua;
    lua_State *state = vm->state;
    jass_t *previous = vm->active;
    vm->active = j;
    int status = luaL_loadbufferx(state, source, size, name, "t");
    if (status == LUA_OK) status = lua_pcall(state, 0, 0, 0);
    vm->active = previous;
    if (status != LUA_OK) {
        jass_setruntimeerror(root, lua_tostring(state, -1));
        lua_pop(state, 1);
        return false;
    }
    vm->source_identity = jass_snapshot_hashbytes(vm->source_identity ? vm->source_identity : 2166136261u, source, size);
    lua_pushglobaltable(state);
    lua_pushnil(state);
    while (lua_next(state, -2)) {
        if (lua_type(state, -2) == LUA_TSTRING && lua_isfunction(state, -1) && !lua_iscfunction(state, -1))
            jlua_callback(state, -1, lua_tostring(state, -2));
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    return true;
}

static void jass_lua_bind_name(jass_t *j, cstring_t name) {
    jassLua_t *vm = jass_root(j)->lua;
    if (!vm) return;
    lua_State *state = vm->state;
    lua_pushglobaltable(state);
    lua_pushstring(state, name);
    lua_rawget(state, -2);
    if (lua_isfunction(state, -1)) jlua_callback(state, -1, name);
    lua_pop(state, 2);
}

static lua_State *jlua_job(jassLua_t *vm, uint32_t id) {
    lua_rawgeti(vm->state, LUA_REGISTRYINDEX, vm->jobs);
    lua_rawgeti(vm->state, -1, id);
    lua_State *thread = lua_tothread(vm->state, -1);
    lua_pop(vm->state, 2);
    return thread;
}

static lua_State *jlua_new_job(jassLua_t *vm, uint32_t *id, jassFunc_t const *func) {
    lua_State *state = vm->state;
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->jobs);
    lua_State *thread = lua_newthread(state);
    *id = ++vm->next_job;
    lua_rawseti(state, -2, *id);
    lua_pop(state, 1);
    lua_rawgeti(thread, LUA_REGISTRYINDEX, vm->callbacks);
    lua_rawgeti(thread, -1, func->lua_id);
    lua_remove(thread, -2);
    return thread;
}

static void jlua_delete_job(jassLua_t *vm, uint32_t id) {
    lua_rawgeti(vm->state, LUA_REGISTRYINDEX, vm->jobs);
    lua_pushnil(vm->state);
    lua_rawseti(vm->state, -2, id);
    lua_pop(vm->state, 1);
}

/* Each resume borrows event/local-player state only for its own scheduler slice. */
static void jass_lua_resume_frame(jass_t *j, jasscoroutine_t *co, jassCoroutineframe_t *frame) {
    jassLua_t *vm = jass_root(j)->lua;
    bool first = !frame->lua_job;
    lua_State *thread = first ? jlua_new_job(vm, &frame->lua_job, frame->func) : jlua_job(vm, frame->lua_job);
    if (!thread) jass_rterror(j, "missing Lua continuation job");
    jass_t *previous = vm->active;
    lua_State *running = vm->running;
    uint32_t active_job = vm->active_job;
    bool asynchronous = vm->asynchronous;
    vm->active = j; vm->running = thread; vm->active_job = frame->lua_job; vm->asynchronous = true;
    int arguments = first ? 0 : (int)frame->loop_count;
    frame->loop_count = 0;
    int status = lua_resume(thread, running, arguments);
    vm->active = previous; vm->running = running; vm->active_job = active_job; vm->asynchronous = asynchronous;
    if (status == LUA_YIELD) {
        if (co->frames == frame && !co->yielded) jass_sleep(j, 0);
        return;
    }
    if (status != LUA_OK) jass_rterror(j, lua_tostring(thread, -1));
    if (frame->return_boolean && lua_gettop(thread)) jass_pushboolean(j, lua_toboolean(thread, 1));
    jass_coroutine_return(co);
}

/* Filters/ForGroup-style synchronous callbacks have their own Lua thread but may not suspend their C caller. */
static uint32_t jass_lua_call(jass_t *j, jassFunc_t const *func, uint32_t args) {
    jassLua_t *vm = jass_root(j)->lua;
    uint32_t job;
    lua_State *thread = jlua_new_job(vm, &job, func);
    FOR_LOOP(i, args) jlua_pushvalue(thread, j, j->stack_pointer + i + 1);
    jass_t *previous = vm->active;
    lua_State *running = vm->running;
    bool asynchronous = vm->asynchronous;
    vm->active = j; vm->running = thread; vm->asynchronous = false;
    int status = lua_resume(thread, running, args);
    vm->active = previous; vm->running = running; vm->asynchronous = asynchronous;
    if (status != LUA_OK) {
        char error[512];
        snprintf(error, sizeof(error), "%s", status == LUA_YIELD ? "wait in synchronous Lua callback" : lua_tostring(thread, -1));
        jlua_delete_job(vm, job);
        jass_rterror(j, error);
    }
    uint32_t results = lua_gettop(thread);
    FOR_LOOP(i, results) {
        int index = i + 1;
        if (lua_isboolean(thread, index)) jass_pushboolean(j, lua_toboolean(thread, index));
        else if (lua_isinteger(thread, index)) jass_pushinteger(j, (int32_t)lua_tointeger(thread, index));
        else if (lua_isnumber(thread, index)) jass_pushnumber(j, (float)lua_tonumber(thread, index));
        else if (lua_isstring(thread, index)) jass_pushstring(j, lua_tostring(thread, index));
        else if (luaL_testudata(thread, index, BZ_JLUA_HANDLE)) {
            jassLuaHandle_t *handle = lua_touserdata(thread, index);
            jass_pushnullhandle(j, handle->value.type->name); jass_copy(j, jass_topvalue(j), &handle->value);
        } else jass_pushnull(j);
    }
    jlua_delete_job(vm, job);
    return results;
}

static void jass_lua_return(jasscoroutine_t *co, jassCoroutineframe_t *frame) {
    if (!frame->return_lua_job) return;
    jass_t *j = co->state;
    jassLua_t *vm = jass_root(j)->lua;
    lua_State *thread = jlua_job(vm, frame->return_lua_job);
    uint32_t results = j->num_stack > 1 ? j->num_stack - 1 : 0;
    FOR_LOOP(i, results) {
        jassVar_t const *value = j->stack + i + 1;
        if (jlua_raw_return(frame->func->name) && jass_getvarbasetype(value) == jasstype_integer)
            lua_pushinteger(thread, (int32_t)jlua_swap_code(*(uint32_t *)value->value));
        else jlua_pushvalue(thread, j, value);
    }
    FOR_EACH_LIST(jassCoroutineframe_t, parent, frame->next) {
        if (parent->lua_job == frame->return_lua_job) { parent->loop_count = results; break; }
    }
}

static void jass_lua_release_frame(jass_t *j, jassCoroutineframe_t *frame) {
    jassLua_t *vm = jass_root(j)->lua;
    if (vm && !vm->staging && frame->lua_job) jlua_delete_job(vm, frame->lua_job);
}

static uint32_t jass_lua_identity(jass_t const *j) { return j->lua ? j->lua->source_identity : 0; }

static bool jlua_buffer_io(void *context, void *data, uint32_t size) {
    jassLuaBuffer_t *buffer = context;
    if (buffer->position > BZ_JLUA_HEAP_LIMIT || size > BZ_JLUA_HEAP_LIMIT - buffer->position) return false;
    size_t end = buffer->position + size;
    if (buffer->reading) {
        if (end > buffer->size) return false;
        memcpy(data, buffer->bytes + buffer->position, size);
    } else {
        if (end > buffer->capacity) {
            size_t capacity = MAX(end, buffer->capacity ? buffer->capacity * 2 : 256);
            uint8_t *bytes = realloc(buffer->bytes, capacity);
            if (!bytes) return false;
            buffer->bytes = bytes; buffer->capacity = capacity;
        }
        memcpy(buffer->bytes + buffer->position, data, size);
        buffer->size = end;
    }
    buffer->position = end;
    return true;
}

/* Host objects use the same codec/identity map as JASS globals, not raw Lua userdata bytes. */
static int jlua_persist_handle(lua_State *state) {
    jassLua_t *vm = jlua_vm(state);
    jassLuaHandle_t *handle = luaL_checkudata(state, 1, BZ_JLUA_HANDLE);
    jassLuaBuffer_t buffer = { 0 };
    jassSnapshot_t codec = { .context = &buffer, .transfer = jlua_buffer_io };
    if (!vm->snapshot || !jass_snapshot_writevar(vm->root, &codec, &handle->value)) {
        free(buffer.bytes);
        return luaL_error(state, "cannot persist typed Warcraft handle");
    }
    lua_pushlstring(state, (char const *)buffer.bytes, buffer.size);
    free(buffer.bytes);
    lua_pushcclosure(state, jlua_restore_handle, 1);
    return 1;
}

static int jlua_restore_handle(lua_State *state) {
    jassLua_t *vm = jlua_vm(state);
    size_t size;
    char const *bytes = lua_tolstring(state, lua_upvalueindex(1), &size);
    if (!vm->snapshot || !bytes) return luaL_error(state, "handle restore outside world snapshot");
    jassLuaBuffer_t buffer = { .bytes = (uint8_t *)bytes, .size = size, .reading = true };
    jassSnapshot_t codec = { .context = &buffer, .transfer = jlua_buffer_io, .handles = vm->snapshot->handles };
    jassLuaHandle_t *handle = lua_newuserdata(state, sizeof(*handle));
    memset(handle, 0, sizeof(*handle));
    luaL_setmetatable(state, BZ_JLUA_HANDLE);
    bool ok = jass_snapshot_readvar(vm->root, &codec, &handle->value);
    vm->snapshot->handles = codec.handles;
    if (!ok || buffer.position != size) return luaL_error(state, "invalid typed Warcraft handle");
    if (handle->value.value && jass_valuehandle(handle->value.type->name)) {
        if (jlua_cached_enum(state, &handle->value)) lua_remove(state, -2);
        else jlua_remember_enum(state, -1);
    }
    return 1;
}

static int jlua_restore_array(lua_State *state) {
    char const *name = lua_tostring(state, lua_upvalueindex(1));
    jassVar_t *value = name ? find_global(jlua_vm(state)->root, name) : NULL;
    if (!value || !value->array) return luaL_error(state, "unknown JASS array in Lua heap");
    char *array = lua_newuserdata(state, strlen(name) + 1);
    strcpy(array, name);
    luaL_setmetatable(state, BZ_JLUA_ARRAY);
    return 1;
}

static int jlua_persist_array(lua_State *state) {
    char const *name = luaL_checkudata(state, 1, BZ_JLUA_ARRAY);
    lua_pushstring(state, name);
    lua_pushcclosure(state, jlua_restore_array, 1);
    return 1;
}

static void jlua_permanent_value(lua_State *state, int table, cstring_t name) {
    lua_setfield(state, table, name);
}

/* Capture immutable C identities before map code can replace any library member.
 * Globals and library tables themselves remain in the serialized object graph. */
static void jlua_initialize_permanents(jassLua_t *vm) {
    lua_State *state = vm->state;
    lua_newtable(state);
    int table = lua_gettop(state);
    cstring_t const libraries[] = { "_G", "coroutine", "math", "string", "table", "utf8", "debug", "package", "io", "os", NULL };
    for (uint32_t i = 0; libraries[i]; i++) {
        lua_getglobal(state, libraries[i]);
        lua_pushnil(state);
        while (lua_next(state, -2)) {
            if (lua_type(state, -2) == LUA_TSTRING && lua_iscfunction(state, -1)) {
                char name[128];
                snprintf(name, sizeof(name), "%s.%s", libraries[i], lua_tostring(state, -2));
                lua_pushcfunction(state, lua_tocfunction(state, -1));
                jlua_permanent_value(state, table, name);
            }
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
    }
    lua_getglobal(state, "package"); lua_getfield(state, -1, "searchers");
    for (int i = 1; i <= 4; i++) {
        char name[64];
        snprintf(name, sizeof(name), "package.searchers.%d", i);
        lua_rawgeti(state, -1, i);
        if (lua_iscfunction(state, -1)) {
            lua_pushcfunction(state, lua_tocfunction(state, -1));
            jlua_permanent_value(state, table, name);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 2);
    cstring_t const streams[] = { "stdin", "stdout", "stderr", NULL };
    lua_getglobal(state, "io");
    for (uint32_t i = 0; streams[i]; i++) {
        lua_getfield(state, -1, streams[i]);
        char name[64];
        snprintf(name, sizeof(name), "io.%s", streams[i]);
        jlua_permanent_value(state, table, name);
    }
    lua_pop(state, 1);
    struct { cstring_t name; lua_CFunction function; } const host_functions[] = {
        { "host.proxy", jlua_proxy }, { "host.global_index", jlua_global_index }, { "host.global_set", jlua_global_set },
        { "host.handle_gc", jlua_handle_gc }, { "host.handle_equal", jlua_handle_equal },
        { "host.handle_string", jlua_handle_string },
        { "host.persist_handle", jlua_persist_handle }, { "host.restore_handle", jlua_restore_handle },
        { "host.array_index", jlua_array_index }, { "host.array_set", jlua_array_set },
        { "host.persist_array", jlua_persist_array }, { "host.restore_array", jlua_restore_array },
        { "host.array_default", jlua_array_default },
        { "host.after_jass", (lua_CFunction)(void (*)(void))jlua_after_jass }, { NULL, NULL }
    };
    for (uint32_t i = 0; host_functions[i].name; i++) {
        lua_pushcfunction(state, host_functions[i].function);
        jlua_permanent_value(state, table, host_functions[i].name);
    }
    luaL_getmetatable(state, BZ_JLUA_HANDLE); jlua_permanent_value(state, table, "host.handle_metatable");
    luaL_getmetatable(state, BZ_JLUA_ARRAY); jlua_permanent_value(state, table, "host.array_metatable");
    luaL_getmetatable(state, LUA_FILEHANDLE); jlua_permanent_value(state, table, "host.file_metatable");
    vm->permanents = luaL_ref(state, LUA_REGISTRYINDEX);
}

static void jlua_permanents(lua_State *state, bool reading) {
    jassLua_t *vm = jlua_vm(state);
    lua_newtable(state);
    int output = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->permanents);
    lua_pushnil(state);
    while (lua_next(state, -2)) {
        lua_pushvalue(state, reading ? -2 : -1);
        lua_pushvalue(state, reading ? -2 : -3);
        lua_rawset(state, output);
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
}

static int jlua_heap_dump(lua_State *state) { eris_persist(state, 1, 2); return 1; }
static int jlua_heap_load(lua_State *state) { eris_unpersist(state, 1, 2); return 1; }

static void jlua_heap_root(lua_State *state) {
    jassLua_t *vm = jlua_vm(state);
    lua_newtable(state);
    lua_pushglobaltable(state); lua_setfield(state, -2, "globals");
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->callbacks); lua_setfield(state, -2, "callbacks");
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->identities); lua_setfield(state, -2, "identities");
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->jobs); lua_setfield(state, -2, "jobs");
    lua_pushinteger(state, vm->next_callback); lua_setfield(state, -2, "next_callback");
    lua_pushinteger(state, vm->next_job); lua_setfield(state, -2, "next_job");
    lua_getfield(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE); lua_setfield(state, -2, "loaded_modules");
    lua_getfield(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE); lua_setfield(state, -2, "preloaded_modules");
}

/* The surrounding reader stages globals/coroutines; Lua roots are likewise
 * committed only after the complete outer stream has passed validation. */
static bool jlua_callback_declarations(jass_t *j, jassSnapshot_t *snapshot, bool reading) {
    uint32_t present = j->lua != NULL;
    if (!jass_snapshot_io(snapshot, &present, sizeof(present)) || present != (j->lua != NULL)) return false;
    if (!present) return true;
    jassLua_t *vm = j->lua;
    uint32_t declarations = 0;
    if (!reading) {
        FOR_EACH_LIST(jassFunc_t const, func, j->functions)
            if (func->lua_id && find_function(j, func->name) == func) declarations++;
    }
    if (!jass_snapshot_io(snapshot, &declarations, sizeof(declarations)) || declarations > BZ_JASS_SNAPSHOT_MAX_COUNT) return false;
    if (reading) {
        vm->staging = true;
        vm->declarations_before_load = j->functions;
        vm->declaration_changes = calloc(declarations, sizeof(*vm->declaration_changes));
        if (declarations && !vm->declaration_changes) return false;
        FOR_LOOP(i, declarations) {
            uint32_t id;
            string_t name = NULL;
            if (!jass_snapshot_io(snapshot, &id, sizeof(id)) || !id || id > BZ_JASS_SNAPSHOT_MAX_COUNT ||
                !jass_snapshot_readstr(snapshot, &name) || !name) { SAFE_DELETE(name, jass_free); return false; }
            jassFunc_t *func = (jassFunc_t *)find_function(j, name);
            if (func && !func->lua_id) { jass_free(name); return false; }
            if (func) {
                FOR_LOOP(k, vm->declaration_changes_count)
                    if (vm->declaration_changes[k].function == func) { jass_free(name); return false; }
                vm->declaration_changes[vm->declaration_changes_count++] = (jassLuaDeclaration_t){ func, func->lua_id };
                func->lua_id = id;
            } else {
                func = jlua_declare(j, name, id);
                vm->declaration_changes[vm->declaration_changes_count++] = (jassLuaDeclaration_t){ func, 0 };
            }
            jass_free(name);
        }
    } else {
        FOR_EACH_LIST(jassFunc_t const, func, j->functions) {
            if (func->lua_id && find_function(j, func->name) == func &&
                (!jass_snapshot_io(snapshot, (void *)&func->lua_id, sizeof(func->lua_id)) ||
                 !jass_snapshot_writestr(snapshot, func->name))) return false;
        }
    }
    return true;
}

/* Host timers/triggers deserialize before host-handle relocation becomes available.
 * Their callback declarations are an explicit earlier section of the game save. */
bool jass_callbackdeclarations(jass_t *j, jassSnapshot_t *snapshot, bool reading) {
    jass_t *root = jass_root(j);
    bool ok = jlua_callback_declarations(root, snapshot, reading) && jass_booldeclarations(root, snapshot, reading);
    jassLua_t *vm = root->lua;
    if (reading && vm && vm->staging) {
        if (ok) {
            free(vm->declaration_changes); vm->declaration_changes = NULL;
            vm->declaration_changes_count = 0; vm->staging = false;
        } else jass_lua_finish_snapshot(root, false);
    }
    return ok;
}

static bool jass_lua_snapshot(jass_t *j, jassSnapshot_t *snapshot, bool reading) {
    if (!jlua_callback_declarations(j, snapshot, reading)) return false;
    /* Lua userdata can contain boolexpr handles. Declare the complete graph
     * after its Lua callbacks, before decoding either VM's saved values. */
    if (!jass_booldeclarations(j, snapshot, reading)) return false;
    if (!j->lua) return true;
    jassLua_t *vm = j->lua;
    lua_State *state = vm->state;
    uint32_t size = 0;
    if (reading) {
        if (!jass_snapshot_io(snapshot, &size, sizeof(size)) || !size || size > BZ_JLUA_HEAP_LIMIT) return false;
        lua_pushcfunction(state, jlua_heap_load);
        jlua_permanents(state, true);
        char *bytes = malloc(size);
        if (!bytes) { lua_pop(state, 2); return false; }
        bool transferred = jass_snapshot_io(snapshot, bytes, size);
        if (transferred) lua_pushlstring(state, bytes, size);
        free(bytes);
        if (!transferred) { lua_pop(state, 2); return false; }
    } else {
        lua_pushcfunction(state, jlua_heap_dump);
        jlua_permanents(state, false);
        jlua_heap_root(state);
    }
    if (reading) vm->pending_enum_handles = jlua_enum_cache(state);
    vm->snapshot = snapshot;
    int status = lua_pcall(state, 2, 1, 0);
    vm->snapshot = NULL;
    if (status != LUA_OK) {
        fprintf(stderr, "Lua snapshot: %s\n", lua_tostring(state, -1));
        lua_pop(state, 1);
        return false;
    }
    if (reading) {
        if (!lua_istable(state, -1)) { lua_pop(state, 1); return false; }
        vm->pending = luaL_ref(state, LUA_REGISTRYINDEX);
        return true;
    }
    size_t length;
    char const *bytes = lua_tolstring(state, -1, &length);
    bool ok = bytes && length && length <= BZ_JLUA_HEAP_LIMIT;
    size = (uint32_t)length;
    if (ok) ok = jass_snapshot_io(snapshot, &size, sizeof(size)) && jass_snapshot_io(snapshot, (void *)bytes, size);
    lua_pop(state, 1);
    return ok;
}

/* Validate heap roots and every cross-language frame edge before committing
 * either VM. A corrupt job ID must reject the save rather than restart main. */
static bool jass_lua_validate_snapshot(jass_t *j, jasscoroutine_t const *coroutines) {
    jassLua_t *vm = j->lua;
    if (!vm) {
        FOR_EACH_LIST(jasscoroutine_t const, co, coroutines)
            FOR_EACH_LIST(jassCoroutineframe_t const, frame, co->frames)
                if (frame->lua_job || frame->return_lua_job) return false;
        return true;
    }
    lua_State *state = vm->state;
    int base = lua_gettop(state);
    bool valid = false;
    if (vm->pending == LUA_NOREF) return false;
    lua_rawgeti(state, LUA_REGISTRYINDEX, vm->pending);
    cstring_t const fields[] = { "globals", "callbacks", "identities", "jobs", "loaded_modules", "preloaded_modules", NULL };
    for (uint32_t i = 0; fields[i]; i++) {
        lua_getfield(state, -1, fields[i]);
        bool table = lua_istable(state, -1); lua_pop(state, 1);
        if (!table) goto done;
    }
    lua_getfield(state, -1, "next_callback");
    lua_Integer callbacks = lua_tointeger(state, -1);
    bool integer = lua_isinteger(state, -1); lua_pop(state, 1);
    if (!integer || callbacks < 0 || callbacks > BZ_JASS_SNAPSHOT_MAX_COUNT) goto done;
    lua_getfield(state, -1, "next_job");
    lua_Integer jobs = lua_tointeger(state, -1);
    integer = lua_isinteger(state, -1); lua_pop(state, 1);
    if (!integer || jobs < 0 || (uint64_t)jobs > UINT32_MAX) goto done;
    lua_getfield(state, -1, "callbacks");
    FOR_LOOP(i, vm->declaration_changes_count) {
        uint32_t id = vm->declaration_changes[i].function->lua_id;
        if (id > callbacks) goto done;
        lua_rawgeti(state, -1, id);
        bool function = lua_isfunction(state, -1); lua_pop(state, 1);
        if (!function) goto done;
    }
    lua_pop(state, 1);
    lua_getfield(state, -1, "jobs");
    FOR_EACH_LIST(jasscoroutine_t const, co, coroutines) {
        FOR_EACH_LIST(jassCoroutineframe_t const, frame, co->frames) {
            uint32_t ids[] = { frame->lua_job, frame->return_lua_job };
            if (frame->lua_job && (frame->type != JASS_FRAME_FUNCTION || !frame->func || !frame->func->lua_id)) goto done;
            FOR_LOOP(i, sizeof(ids) / sizeof(*ids)) {
                if (!ids[i]) continue;
                if (ids[i] > jobs) goto done;
                lua_rawgeti(state, -1, ids[i]);
                bool thread = lua_isthread(state, -1); lua_pop(state, 1);
                if (!thread) goto done;
            }
            if (frame->return_lua_job) {
                bool parent_found = false;
                FOR_EACH_LIST(jassCoroutineframe_t const, parent, frame->next)
                    if (parent->lua_job == frame->return_lua_job) parent_found = true;
                if (!parent_found) goto done;
            }
        }
    }
    valid = true;
done:
    lua_settop(state, base);
    return valid;
}

static void jass_lua_finish_snapshot(jass_t *j, bool success) {
    jassLua_t *vm = j->lua;
    if (!vm || !vm->staging) return;
    lua_State *state = vm->state;
    if (success && vm->pending != LUA_NOREF) {
        lua_rawgeti(state, LUA_REGISTRYINDEX, vm->pending);
        lua_getfield(state, -1, "globals"); lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        struct { cstring_t name; int *reference; } fields[] = {
            { "callbacks", &vm->callbacks }, { "identities", &vm->identities }, { "jobs", &vm->jobs }
        };
        FOR_LOOP(i, sizeof(fields) / sizeof(*fields)) {
            luaL_unref(state, LUA_REGISTRYINDEX, *fields[i].reference);
            lua_getfield(state, -1, fields[i].name); *fields[i].reference = luaL_ref(state, LUA_REGISTRYINDEX);
        }
        lua_getfield(state, -1, "next_callback"); vm->next_callback = (uint32_t)lua_tointeger(state, -1); lua_pop(state, 1);
        lua_getfield(state, -1, "next_job"); vm->next_job = (uint32_t)lua_tointeger(state, -1); lua_pop(state, 1);
        lua_getfield(state, -1, "loaded_modules"); lua_setfield(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
        lua_getfield(state, -1, "preloaded_modules"); lua_setfield(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
        lua_pop(state, 1);
        luaL_unref(state, LUA_REGISTRYINDEX, vm->enum_handles);
        vm->enum_handles = vm->pending_enum_handles;
        vm->pending_enum_handles = LUA_NOREF;
        /* Fresh main/AI initialization can declare callbacks absent from the
         * saved heap. Keeping those declarations poisons the next save with
         * callback IDs beyond the restored callback table. Retire them only
         * after the complete snapshot has committed; a failed read keeps the
         * live VM's declarations and heap intact. */
        jassFunc_t **link = &j->functions;
        while (*link) {
            jassFunc_t *func = *link;
            bool saved = !func->lua_id;
            FOR_LOOP(i, vm->declaration_changes_count) if (vm->declaration_changes[i].function == func) saved = true;
            if (saved) { link = &func->next; continue; }
            *link = func->next;
            jass_free((handle_t)func->name); jass_free(func);
        }
        memset(j->function_hash, 0, sizeof(j->function_hash));
        FOR_EACH_LIST(jassFunc_t, func, j->functions) {
            jassFunc_t **tail = &j->function_hash[jass_hash(func->name)];
            while (*tail) tail = &(*tail)->hash_next;
            func->hash_next = NULL; *tail = func;
        }
    } else {
        FOR_LOOP(i, vm->declaration_changes_count)
            vm->declaration_changes[i].function->lua_id = vm->declaration_changes[i].previous;
        while (j->functions != vm->declarations_before_load) {
            jassFunc_t *func = j->functions;
            j->functions = func->next;
            if (func->nativefunc != jass_boolexpr_native) jass_free((handle_t)func->name);
            jass_free(func);
        }
        memset(j->function_hash, 0, sizeof(j->function_hash));
        FOR_EACH_LIST(jassFunc_t, func, j->functions) {
            jassFunc_t **tail = &j->function_hash[jass_hash(func->name)];
            while (*tail) tail = &(*tail)->hash_next;
            func->hash_next = NULL; *tail = func;
        }
    }
    luaL_unref(state, LUA_REGISTRYINDEX, vm->pending); vm->pending = LUA_NOREF;
    luaL_unref(state, LUA_REGISTRYINDEX, vm->pending_enum_handles); vm->pending_enum_handles = LUA_NOREF;
    free(vm->declaration_changes); vm->declaration_changes = NULL; vm->declaration_changes_count = 0;
    vm->staging = false;
}

static void jass_lua_close(jass_t *j) {
    jassLua_t *vm = j->lua;
    if (!vm) return;
    lua_close(vm->state);
    jass_free(vm);
    j->lua = NULL;
}
