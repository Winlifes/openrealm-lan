/* Exercise the production language dispatcher, typed host calls and scheduler. */
#include "shared/test.h"
#include "games/warcraft-3/jass/jass.h"
#include <stdlib.h>

#define BZ_JLUA_OBSERVATIONS 32 // values; bounds the fixture's externally observed host effects
#define BZ_JLUA_SNAPSHOT_BYTES (1u << 20) // bytes; bounds heap-and-JASS continuation fixtures

typedef struct { uint8_t *bytes; uint32_t size, position; bool reading; } jluaSnapshot_t;

static uint32_t jlua_clock;
static int32_t jlua_values[BZ_JLUA_OBSERVATIONS];
static uint32_t jlua_count;
static jassFunc_t const *jlua_saved;

static bool jlua_snapshot_io(void *context, void *data, uint32_t size) {
    jluaSnapshot_t *io = context;
    uint32_t limit = io->reading ? io->size : BZ_JLUA_SNAPSHOT_BYTES;
    if (io->position > limit || size > limit - io->position) return false;
    if (io->reading) memcpy(data, io->bytes + io->position, size);
    else memcpy(io->bytes + io->position, data, size);
    io->position += size;
    if (!io->reading) io->size = io->position;
    return true;
}

static void *jlua_alloc(long size) { return calloc(1, (size_t)size); }
static uint32_t jlua_now(void) { return jlua_clock; }
static uint32_t jlua_record(jass_t *j) {
    if (jlua_count < BZ_JLUA_OBSERVATIONS) jlua_values[jlua_count++] = jass_checkinteger(j, 1);
    return 0;
}
static uint32_t jlua_wait(jass_t *j) { jass_sleep(j, jass_checkinteger(j, 1)); return 0; }
static uint32_t jlua_remember(jass_t *j) { jlua_saved = jass_checkcode(j, 1); return 0; }
static uint32_t jlua_invoke(jass_t *j) { jass_pushfunction(j, jlua_saved); return jass_call(j, 0); }
static uint32_t jlua_fail(jass_t *j) { jass_rterror(j, "original native failure"); return 0; }
static uint32_t jlua_new_token(jass_t *j) {
    int32_t value = jass_checkinteger(j, 1);
    int32_t *token = jass_newhandle(j, sizeof(*token), "token");
    *token = value;
    return 1;
}
static uint32_t jlua_read_token(jass_t *j) {
    int32_t *token = jass_checkhandle(j, 1, "token");
    return jass_pushinteger(j, token ? *token : -1);
}
static uint32_t jlua_unit_id(jass_t *j) {
    char const *text = jass_checkstring(j, 1);
    return jass_pushinteger(j, MAKEFOURCC(text[0], text[1], text[2], text[3]));
}
static uint32_t jlua_unit_id_string(jass_t *j) {
    uint32_t value = jass_checkinteger(j, 1);
    char text[5] = { value, value >> 8, value >> 16, value >> 24, 0 };
    return jass_pushstring(j, text);
}
static uint32_t jlua_new_point(jass_t *j) {
    float value = jass_checknumber(j, 1);
    float *point = jass_newhandle(j, sizeof(float) * 2, "location");
    point[0] = value; point[1] = -value;
    return 1;
}
static uint32_t jlua_read_point(jass_t *j) {
    float const *point = jass_checkhandle(j, 1, "location");
    return jass_pushinteger(j, point ? (int32_t)point[0] : -1);
}
static uint32_t jlua_read_flag(jass_t *j) { return jass_pushinteger(j, jass_checkboolean(j, 1) ? 1 : 0); }
static uint32_t jlua_empty_text(jass_t *j) { return jass_pushinteger(j, !jass_checkstring(j, 1)[0]); }
static uint32_t jlua_unit_type(jass_t *j) {
    int32_t value = jass_checkinteger(j, 1);
    int32_t *result = jass_newhandle(j, sizeof(*result), "unittype");
    *result = value; return 1;
}
static uint32_t jlua_race(jass_t *j) {
    int32_t value = jass_checkinteger(j, 1);
    int32_t *result = jass_newhandle(j, sizeof(*result), "race");
    *result = value; return 1;
}
static uint32_t jlua_frame_point(jass_t *j) {
    int32_t value = jass_checkinteger(j, 1);
    int32_t *result = jass_newhandle(j, sizeof(*result), "framepointtype");
    *result = value; return 1;
}
static uint32_t jlua_read_frame_origin(jass_t *j) {
    int32_t const argument = jass_checkinteger(j, 1);
    uint32_t value = 0;
    if (!jass_readenumconstant(j, "FRAME_ORIGIN", "framepointtype", &value)) {
        jass_rterror(j, "typed constant was unavailable from the native call");
        return 0;
    }
    T_EQ(jass_checkinteger(j, 1), argument);
    return jass_pushinteger(j, argument + value);
}
static uint32_t jlua_invoke_code(jass_t *j) {
    jassFunc_t const *callback = jass_checkcode(j, 1);
    jass_pushfunction(j, callback);
    return jass_call(j, 0);
}
/* These typed host boundaries exercise the production expression allocator,
 * evaluator and snapshot codec without copying their implementation. */
static uint32_t jlua_condition(jass_t *j) { return jass_pushlighthandle(j, (handle_t)jass_checkcode(j, 1), "boolexpr"); }
static uint32_t jlua_and(jass_t *j) {
    jassBoolExprParams_t params = { .op = '&', .a = jass_checkhandle(j, 1, "boolexpr"), .b = jass_checkhandle(j, 2, "boolexpr") };
    return jass_pushlighthandle(j, (handle_t)jass_makeboolexpr(j, &params), "boolexpr");
}
static uint32_t jlua_not(jass_t *j) {
    jassBoolExprParams_t params = { .op = '!', .a = jass_checkhandle(j, 1, "boolexpr") };
    return jass_pushlighthandle(j, (handle_t)jass_makeboolexpr(j, &params), "boolexpr");
}
static uint32_t jlua_eval(jass_t *j) {
    return jass_pushboolean(j, jass_evaluateboolexpr(j, jass_checkhandle(j, 1, "boolexpr"), NULL));
}
static jassModule_t const jlua_natives[] = {
    { "Record", jlua_record }, { "WaitMilliseconds", jlua_wait },
    { "Remember", jlua_remember }, { "Invoke", jlua_invoke }, { "ForceError", jlua_fail },
    { "NewToken", jlua_new_token }, { "ReadToken", jlua_read_token },
    { "UnitId", jlua_unit_id }, { "UnitId2String", jlua_unit_id_string },
    { "NewPoint", jlua_new_point }, { "ReadPoint", jlua_read_point }, { "InvokeCode", jlua_invoke_code },
    { "ReadFlag", jlua_read_flag }, { "EmptyText", jlua_empty_text },
    { "NewUnitType", jlua_unit_type }, { "NewRace", jlua_race },
    { "NewFramePoint", jlua_frame_point }, { "ReadFrameOrigin", jlua_read_frame_origin },
    { "Condition", jlua_condition }, { "And", jlua_and }, { "Not", jlua_not }, { "Eval", jlua_eval }, { NULL, NULL }
};

static jass_t *jlua_fixture(void) {
    jlua_clock = jlua_count = 0;
    jlua_saved = NULL;
    memset(jlua_values, 0, sizeof(jlua_values));
    jassHost_t host = { .MemAlloc = jlua_alloc, .MemFree = free, .GetTime = jlua_now, .natives = jlua_natives };
    jass_sethost(&host);
    jass_t *j = jass_newstate();
    char common[] =
        "type token extends handle\ntype location extends handle\ntype unittype extends handle\ntype race extends handle\n"
        "type framepointtype extends handle\n"
        "native Record takes integer n returns nothing\n"
        "native WaitMilliseconds takes integer duration returns nothing\n"
        "native Remember takes code callback returns nothing\n"
        "native Invoke takes nothing returns nothing\n"
        "native ForceError takes nothing returns nothing\n"
        "native NewToken takes integer value returns token\n"
        "native ReadToken takes token value returns integer\n"
        "native UnitId takes string unitIdString returns integer\n"
        "native UnitId2String takes integer unitid returns string\n"
        "native NewPoint takes real value returns location\n"
        "native ReadPoint takes location value returns integer\n"
        "native ReadFlag takes boolean value returns integer\n"
        "native EmptyText takes string value returns integer\n"
        "native NewUnitType takes integer value returns unittype\n"
        "native NewRace takes integer value returns race\n"
        "native NewFramePoint takes integer value returns framepointtype\n"
        "native ReadFrameOrigin takes integer value returns integer\n"
        "native InvokeCode takes code callback returns nothing\n"
        "globals\ninteger shared = 3\ninteger array sharedArray\n"
        "location sharedPoint=null\ncode sharedCode=null\n"
        "constant unittype CLASSIFICATION = NewUnitType(15)\n"
        "constant framepointtype FRAME_ORIGIN = NewFramePoint(8)\nendglobals\n"
        "function AddWait takes integer n returns integer\n"
        "call WaitMilliseconds(40)\nreturn n + 7\nendfunction\n"
        "function WaitToken takes token value returns integer\n"
        "call WaitMilliseconds(40)\nreturn ReadToken(value)\nendfunction\n"
        "function Dispatch takes nothing returns nothing\ncall Invoke()\nendfunction\n"
        "function RestoredCallback takes nothing returns nothing\ncall InvokeCode(sharedCode)\nendfunction\n"
        "function OriginalMark takes nothing returns nothing\ncall Record(29)\nendfunction\n";
    T_ASSERT(jass_dobuffer(j, common));
    return j;
}

static bool jlua_load(jass_t *j, char const *source) {
    char *copy = strdup(source);
    bool loaded = jass_dobuffer_ex(j, copy, JASS_MODE_LUA);
    T_ASSERT(loaded);
    free(copy);
    return loaded;
}

static jass_t *jlua_bool_fixture(void) {
    jass_t *j = jlua_fixture();
    char source[] =
        "type boolexpr extends handle\n"
        "native Condition takes code callback returns boolexpr\n"
        "native And takes boolexpr a, boolexpr b returns boolexpr\n"
        "native Not takes boolexpr a returns boolexpr\n"
        "native Eval takes boolexpr value returns boolean\n"
        "globals\n boolexpr sharedGate = null\nendglobals\n";
    T_ASSERT(jass_dobuffer(j, source));
    return j;
}

/* Lua's local userdata must resolve its expression before the heap is decoded,
 * even without the game's earlier host-callback declaration section. */
TEST(jass_lua, composite_filter_fresh_snapshot_preserves_lua_aliases_and_callbacks) {
    char const *source =
        "local n=0; function main() "
        "local gate=And(Condition(function() n=n+1; return true end), Not(Condition(function() return false end))); "
        "sharedGate=gate; WaitMilliseconds(40); "
        "Record(Eval(gate) and 1 or 0); Record(gate==sharedGate and 1 or 0); Record(n) end; "
        "function extra() sharedGate=Not(Not(Not(Condition(function() return false end)))) end";
    jass_t *j = jlua_bool_fixture();
    if (!jlua_load(j, source)) { jass_close(j); return; }
    jass_callbyname(j, "main", true); jass_runevents(j);
    T_EQ(jlua_count, 0);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context = &io, .transfer = jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec); T_ASSERT(saved); jass_close(j);
    if (!saved) { free(io.bytes); return; }
    j = jlua_bool_fixture();
    if (jlua_load(j, source)) {
        /* Fresh initialization may have more nodes and closures than the save. */
        jass_callbyname(j, "extra", true); jass_runevents(j);
        uint32_t complete_size = io.size;
        io.position = 0; io.reading = true; io.size--;
        T_ASSERT(!jass_readsnapshot(j, &codec));
        T_ASSERT(!jass_rterror_pending(j));
        io.size = complete_size;
        io.position = 0; io.reading = true;
        bool loaded = jass_readsnapshot(j, &codec); T_ASSERT(loaded);
        if (loaded) {
            jlua_clock = 40; jass_runevents(j);
            T_EQ(jlua_count, 3);
            T_EQ(jlua_values[0], 1); T_EQ(jlua_values[1], 1); T_EQ(jlua_values[2], 1);
            T_ASSERT(!jass_rterror_pending(j));
            io.position = 0; io.reading = false;
            T_ASSERT(jass_writesnapshot(j, &codec));
            io.position = 0; io.reading = true;
            T_ASSERT(jass_readsnapshot(j, &codec));
        }
    }
    jass_close(j); free(io.bytes);
}

static jass_t *jlua_bool_jass_fixture(void) {
    jass_t *j = jlua_bool_fixture();
    char source[] =
        "globals\n boolexpr aliasGate = null\nendglobals\n"
        "function main takes nothing returns nothing\n"
        " set sharedGate = And(null, null)\n set aliasGate = sharedGate\nendfunction\n"
        "function Extra takes nothing returns nothing\n"
        " set sharedGate = Not(Not(Not(null)))\n set aliasGate = sharedGate\nendfunction\n"
        "function Check takes nothing returns nothing\n"
        " if Eval(sharedGate) then\n call Record(1)\n else\n call Record(0)\n endif\n"
        " if sharedGate == aliasGate then\n call Record(7)\n endif\nendfunction\n";
    T_ASSERT(jass_dobuffer(j, source));
    return j;
}

TEST(jass_expression, composite_snapshot_retires_extra_nodes_and_preserves_prelude_aliases) {
    jass_t *j = jlua_bool_jass_fixture();
    jass_callbyname(j, "main", false);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context = &io, .transfer = jlua_snapshot_io };
    T_ASSERT(jass_callbackdeclarations(j, &codec, false));
    uint32_t prefix = io.position;
    T_ASSERT(jass_writesnapshot(j, &codec));
    jass_close(j);
    j = jlua_bool_jass_fixture(); jass_callbyname(j, "Extra", false);
    io.position = 0; io.reading = true;
    T_ASSERT(jass_callbackdeclarations(j, &codec, true));
    T_EQ(io.position, prefix);
    bool loaded = jass_readsnapshot(j, &codec); T_ASSERT(loaded);
    if (loaded) {
        jass_callbyname(j, "Check", false);
        T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 1); T_EQ(jlua_values[1], 7);
        io.position = 0; io.reading = false;
        T_ASSERT(jass_callbackdeclarations(j, &codec, false));
        uint32_t nodes; memcpy(&nodes, io.bytes + sizeof(uint32_t), sizeof(nodes));
        T_EQ(nodes, 1);
        T_ASSERT(jass_writesnapshot(j, &codec));
        io.position = 0; io.reading = true;
        T_ASSERT(jass_callbackdeclarations(j, &codec, true));
        T_ASSERT(jass_readsnapshot(j, &codec));
        jass_callbyname(j, "Check", false);
        T_EQ(jlua_count, 4); T_EQ(jlua_values[2], 1); T_EQ(jlua_values[3], 7);
    }
    jass_close(j); free(io.bytes);
}

TEST(jass_expression, truncated_composite_snapshot_preserves_live_graph_and_identity_counter) {
    jass_t *j = jlua_bool_jass_fixture();
    jass_callbyname(j, "main", false);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context = &io, .transfer = jlua_snapshot_io };
    T_ASSERT(jass_writesnapshot(j, &codec)); jass_close(j);
    j = jlua_bool_jass_fixture(); jass_callbyname(j, "Extra", false);
    io.position = 0; io.reading = true; io.size--;
    T_ASSERT(!jass_readsnapshot(j, &codec));
    jass_callbyname(j, "Check", false);
    T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 0); T_EQ(jlua_values[1], 7);
    io.position = 0; io.reading = false;
    T_ASSERT(jass_callbackdeclarations(j, &codec, false));
    uint32_t nodes; memcpy(&nodes, io.bytes + sizeof(uint32_t), sizeof(nodes));
    T_EQ(nodes, 3);
    jass_close(j); free(io.bytes);
}

typedef enum {
    BOOL_BAD_COUNT, BOOL_BAD_OP, BOOL_BAD_MARKER, BOOL_SELF_REFERENCE, BOOL_BAD_NOT, BOOL_TRUNCATED_SECOND
} jluaBoolCorrupt_t;
static void jlua_bool_reject_case(jluaBoolCorrupt_t mode) {
    jass_t *j = jlua_bool_jass_fixture();
    jass_callbyname(j, "Extra", false);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context = &io, .transfer = jlua_snapshot_io };
    uint32_t absent = 0, count = mode == BOOL_BAD_COUNT ? UINT32_MAX : mode == BOOL_TRUNCATED_SECOND ? 2 : 1, id = 1;
    char op = mode == BOOL_BAD_OP ? '?' : '!';
    uint8_t a = mode == BOOL_BAD_MARKER ? 2 : mode == BOOL_SELF_REFERENCE ? 1 : 0;
    uint8_t b = 0;
    T_ASSERT(jlua_snapshot_io(&io, &absent, sizeof(absent)));
    T_ASSERT(jlua_snapshot_io(&io, &count, sizeof(count)));
    T_ASSERT(jlua_snapshot_io(&io, &op, sizeof(op)));
    T_ASSERT(jlua_snapshot_io(&io, &a, sizeof(a)));
    if (a == 1) T_ASSERT(jlua_snapshot_io(&io, &id, sizeof(id)));
    else {
        T_ASSERT(jlua_snapshot_io(&io, &absent, sizeof(absent)));
        T_ASSERT(jlua_snapshot_io(&io, &absent, sizeof(absent)));
    }
    T_ASSERT(jlua_snapshot_io(&io, &b, sizeof(b)));
    if (mode == BOOL_BAD_NOT) {
        uint32_t kind = 1, length = sizeof("Check");
        char name[] = "Check";
        T_ASSERT(jlua_snapshot_io(&io, &kind, sizeof(kind)));
        T_ASSERT(jlua_snapshot_io(&io, &length, sizeof(length)));
        T_ASSERT(jlua_snapshot_io(&io, name, sizeof(name)));
    }
    else {
        T_ASSERT(jlua_snapshot_io(&io, &absent, sizeof(absent)));
        T_ASSERT(jlua_snapshot_io(&io, &absent, sizeof(absent)));
    }
    io.position = 0; io.reading = true;
    T_ASSERT(!jass_callbackdeclarations(j, &codec, true));
    jass_callbyname(j, "Check", false);
    T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 0); T_EQ(jlua_values[1], 7);
    jass_close(j); free(io.bytes);
}
TEST(jass_expression, composite_declarations_reject_excessive_count) { jlua_bool_reject_case(BOOL_BAD_COUNT); }
TEST(jass_expression, composite_declarations_reject_unknown_operator) { jlua_bool_reject_case(BOOL_BAD_OP); }
TEST(jass_expression, composite_declarations_reject_unknown_operand_marker) { jlua_bool_reject_case(BOOL_BAD_MARKER); }
TEST(jass_expression, composite_declarations_reject_self_reference) { jlua_bool_reject_case(BOOL_SELF_REFERENCE); }
TEST(jass_expression, composite_declarations_reject_second_not_operand) { jlua_bool_reject_case(BOOL_BAD_NOT); }
TEST(jass_expression, truncated_composite_declarations_discard_staged_nodes) { jlua_bool_reject_case(BOOL_TRUNCATED_SECOND); }

TEST(jass_expression, rejects_snapshot_before_composite_declarations_without_mutation) {
    jass_t *j = jlua_bool_jass_fixture(); jass_callbyname(j, "main", false);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context = &io, .transfer = jlua_snapshot_io };
    T_ASSERT(jass_writesnapshot(j, &codec));
    uint32_t old = 16; memcpy(io.bytes + sizeof(uint32_t), &old, sizeof(old));
    io.position = 0; io.reading = true;
    T_ASSERT(!jass_readsnapshot(j, &codec));
    jass_callbyname(j, "Check", false);
    T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 1); T_EQ(jlua_values[1], 7);
    jass_close(j); free(io.bytes);
}

TEST(jass_lua, typed_constant_reads_validate_domain_and_preserve_failed_output) {
    jass_t *j = jlua_fixture();
    char globals[] = "globals\nframepointtype mutablePoint = NewFramePoint(3)\n"
        "constant framepointtype nullPoint = null\nconstant integer ordinary = 19\n"
        "constant token object = NewToken(17)\nendglobals\n";
    T_ASSERT(jass_dobuffer(j, globals));
    uint32_t value = 37;
    T_ASSERT(jass_readenumconstant(j, "FRAME_ORIGIN", "framepointtype", &value)); T_EQ(value, 8);
    value = 37;
    T_ASSERT(!jass_readenumconstant(j, "missing", "framepointtype", &value));
    T_ASSERT(!jass_readenumconstant(j, "mutablePoint", "framepointtype", &value));
    T_ASSERT(!jass_readenumconstant(j, "nullPoint", "framepointtype", &value));
    T_ASSERT(!jass_readenumconstant(j, "ordinary", "framepointtype", &value));
    T_ASSERT(!jass_readenumconstant(j, "FRAME_ORIGIN", "unittype", &value));
    T_ASSERT(!jass_readenumconstant(j, "object", "token", &value));
    T_ASSERT(!jass_readenumconstant(NULL, "FRAME_ORIGIN", "framepointtype", &value));
    T_ASSERT(!jass_readenumconstant(j, NULL, "framepointtype", &value));
    T_ASSERT(!jass_readenumconstant(j, "FRAME_ORIGIN", NULL, &value));
    T_ASSERT(!jass_readenumconstant(j, "FRAME_ORIGIN", "framepointtype", NULL));
    T_EQ(value, 37);
    jass_close(j);
}

TEST(jass_lua, typed_constant_reads_use_root_from_jass_and_lua_coroutines) {
    jass_t *j = jlua_fixture();
    char source[] = "function main takes nothing returns nothing\n"
        "call Record(ReadFrameOrigin(70))\ncall WaitMilliseconds(2)\n"
        "call Record(ReadFrameOrigin(71))\nendfunction\n";
    T_ASSERT(jass_dobuffer(j, source));
    jass_callbyname(j, "main", true); jass_runevents(j);
    T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 78);
    jlua_clock = 3; jass_runevents(j);
    T_EQ(jlua_count, 2); T_EQ(jlua_values[1], 79); T_ASSERT(!jass_rterror_pending(j));
    jass_close(j);

    j = jlua_fixture();
    if (jlua_load(j, "function main() Record(ReadFrameOrigin(20)); WaitMilliseconds(2); Record(ReadFrameOrigin(21)) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 28);
        jlua_clock = 3; jass_runevents(j);
        T_EQ(jlua_count, 2); T_EQ(jlua_values[1], 29); T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, map_entries_share_typed_jass_globals) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function config() shared=11; sharedArray[5]=23 end\n"
                     "function main() Record(shared+sharedArray[5]); Record(_VERSION=='Lua 5.3' and 1 or 0) end")) {
        T_ASSERT(jass_hasfunction(j, "main"));
        jass_callbyname(j, "config", false);
        jass_callbyname(j, "main", true);
        jass_runevents(j);
        T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 34); T_EQ(jlua_values[1], 1);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, native_sleep_retains_lua_locals_and_calls_once) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() local n=19; Record(n); WaitMilliseconds(50); Record(n+1) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 19);
        jlua_clock=49; jass_runevents(j); T_EQ(jlua_count, 1);
        jlua_clock=50; jass_runevents(j); T_EQ(jlua_count, 2); T_EQ(jlua_values[1], 20);
        jass_runevents(j); T_EQ(jlua_count, 2); T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, jass_child_wait_returns_to_original_lua_expression) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() local value=AddWait(5); Record(value*2) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j); T_EQ(jlua_count, 0);
        jlua_clock=39; jass_runevents(j); T_EQ(jlua_count, 0);
        jlua_clock=40; jass_runevents(j); T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 24);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, anonymous_code_callback_keeps_shared_closure_upvalue) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "local n=6; function main() Remember(function() n=n+1; Record(n) end); Dispatch(); Invoke() end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 7); T_EQ(jlua_values[1], 8);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, typed_owned_handle_survives_jass_stack_release) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() local t=NewToken(37); local a=t; collectgarbage();"
                     "Record(ReadToken(a)); Record(t==a and 1 or 0); Record(ReadToken(nil)) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 3); T_EQ(jlua_values[0], 37); T_EQ(jlua_values[1], 1); T_EQ(jlua_values[2], -1);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, handle_strings_expose_concrete_map_types) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "local function kind(h) local s=tostring(h); return s:sub(1,(s:find(':',1,true) or 0)-1) end; "
                     "function main() local p=NewPoint(19); local t=NewToken(7); "
                     "Record(kind(p)=='location' and 1 or 0); Record(kind(t)=='token' and 1 or 0); "
                     "Record(type(p)=='userdata' and 1 or 0); Record(tostring(nil)=='nil' and 1 or 0) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 4);
        FOR_LOOP(i, 4) T_EQ(jlua_values[i], 1);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, enum_keys_follow_type_and_value_identity) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "local names={[CLASSIFICATION]='worker'}; function main() "
                     "Record(names[CLASSIFICATION]=='worker' and 1 or 0); "
                     "Record(names[NewUnitType(15)]=='worker' and 1 or 0); "
                     "Record(rawequal(CLASSIFICATION,NewUnitType(15)) and 1 or 0); "
                     "Record(names[NewRace(15)]==nil and 1 or 0); "
                     "local zero={[NewUnitType(0)]='zero'}; Record(zero[NewUnitType(0)]=='zero' and 1 or 0); "
                     "collectgarbage('collect'); Record(names[CLASSIFICATION]=='worker' and 1 or 0) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 6); FOR_LOOP(i, 6) T_EQ(jlua_values[i], 1);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, enum_cache_does_not_retain_unreferenced_userdata) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() local weak=setmetatable({},{__mode='v'}); "
                     "do local h=NewUnitType(222); weak[1]=h end; collectgarbage('collect'); "
                     "Record(weak[1]==nil and 1 or 0) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 1);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, enum_keys_restore_in_fresh_vm_and_survive_failed_read) {
    char const *source = "local names={[CLASSIFICATION]=31,[FRAME_ORIGIN]=73}; function main() WaitMilliseconds(40); "
        "Record(names[CLASSIFICATION] or -1); Record(names[NewUnitType(15)] or -1); Record(names[NewFramePoint(8)] or -1) end; "
        "function verify() Record((names[CLASSIFICATION] or -1)+(names[FRAME_ORIGIN] or -1)) end";
    jass_t *j = jlua_fixture();
    if (!jlua_load(j, source)) { jass_close(j); return; }
    jass_callbyname(j, "main", true); jass_runevents(j);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context=&io, .transfer=jlua_snapshot_io };
    bool saved=jass_writesnapshot(j,&codec); T_ASSERT(saved); jass_close(j);
    if (!saved) { free(io.bytes); return; }
    j=jlua_fixture();
    if (jlua_load(j,source)) {
        io.position=0; io.reading=true;
        bool loaded=jass_readsnapshot(j,&codec); T_ASSERT(loaded);
        if (loaded) {
            jass_callbyname(j,"verify",true); jass_runevents(j);
            T_EQ(jlua_count,1); T_EQ(jlua_values[0],104);
            /* Truncate the later JASS continuation after the Lua heap decoded.
             * Its staged cache must not replace the live table-key identities. */
            io.size--; io.position=0;
            T_ASSERT(!jass_readsnapshot(j,&codec));
            jass_callbyname(j,"verify",true); jass_runevents(j);
            T_EQ(jlua_count,2); T_EQ(jlua_values[1],104);
            jlua_clock=40; jass_runevents(j);
            T_EQ(jlua_count,5); T_EQ(jlua_values[2],31); T_EQ(jlua_values[3],31); T_EQ(jlua_values[4],73);
            T_ASSERT(!jass_rterror_pending(j));
        }
    }
    jass_close(j); free(io.bytes);
}

TEST(jass_lua, captured_handle_formatter_restores_in_fresh_vm) {
    char const *source = "function main() local h=NewPoint(37); local format=getmetatable(h).__tostring; "
        "WaitMilliseconds(40); Record(format(h):match('^location: ') and 1 or 0); Record(ReadPoint(h)) end";
    jass_t *j = jlua_fixture();
    if (!jlua_load(j, source)) { jass_close(j); return; }
    jass_callbyname(j, "main", true); jass_runevents(j);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context=&io, .transfer=jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec); T_ASSERT(saved); jass_close(j);
    if (!saved) { free(io.bytes); return; }
    j = jlua_fixture();
    if (jlua_load(j, source)) {
        io.position=0; io.reading=true;
        bool loaded = jass_readsnapshot(j, &codec); T_ASSERT(loaded);
        if (loaded) {
            jlua_clock=40; jass_runevents(j);
            T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 1); T_EQ(jlua_values[1], 37);
            T_ASSERT(!jass_rterror_pending(j));
        }
    }
    jass_close(j); free(io.bytes);
}

TEST(jass_lua, rejects_snapshot_before_handle_formatter_identity) {
    jass_t *j = jlua_fixture();
    if (!jlua_load(j, "function main() WaitMilliseconds(40) end")) { jass_close(j); return; }
    jass_callbyname(j, "main", true); jass_runevents(j);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context=&io, .transfer=jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec); T_ASSERT(saved);
    if (saved) {
        uint32_t old = 13;
        memcpy(io.bytes + sizeof(uint32_t), &old, sizeof(old));
        io.position=0; io.reading=true;
        T_ASSERT(!jass_readsnapshot(j, &codec));
    }
    jass_close(j); free(io.bytes);
}

TEST(jass_lua, rejects_snapshot_before_canonical_enum_keys) {
    jass_t *j = jlua_fixture();
    if (!jlua_load(j, "function main() WaitMilliseconds(40) end")) { jass_close(j); return; }
    jass_callbyname(j, "main", true); jass_runevents(j);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context=&io, .transfer=jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec); T_ASSERT(saved);
    if (saved) {
        for (uint32_t old=14; old<=15; old++) {
            memcpy(io.bytes + sizeof(uint32_t), &old, sizeof(old));
            io.position=0; io.reading=true;
            T_ASSERT(!jass_readsnapshot(j, &codec));
        }
    }
    jass_close(j); free(io.bytes);
}

TEST(jass_lua, omitted_nullable_arguments_reach_typed_host_values) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() Record(ReadToken()); Record(ReadPoint()); "
                     "Record(EmptyText()); Remember(); Record(ReadToken(nil)) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 4); T_EQ(jlua_values[0], -1); T_EQ(jlua_values[1], -1);
        T_EQ(jlua_values[2], 1); T_EQ(jlua_values[3], -1); T_NULL(jlua_saved);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, nil_boolean_arguments_are_false_without_losing_true) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() Record(ReadFlag(nil)); Record(ReadFlag()); "
                     "Record(ReadFlag(false)); Record(ReadFlag(true)) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 4); T_EQ(jlua_values[0], 0); T_EQ(jlua_values[1], 0);
        T_EQ(jlua_values[2], 0); T_EQ(jlua_values[3], 1);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, missing_required_and_wrong_types_still_fail_before_host_call) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() local ok=pcall(NewToken); Record(not ok and 1 or 0); "
                     "ok=pcall(ReadFlag,0); Record(not ok and 1 or 0); "
                     "ok=pcall(ReadToken,NewPoint(3)); Record(not ok and 1 or 0) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 3); FOR_LOOP(i, 3) T_EQ(jlua_values[i], 1);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, omitted_nullable_argument_survives_sleeping_jass_child) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() Record(WaitToken()) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j); T_EQ(jlua_count, 0);
        jlua_clock=40; jass_runevents(j);
        T_EQ(jlua_count, 1); T_EQ(jlua_values[0], -1);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, protected_native_error_returns_through_lua_pcall) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() local ok,msg=pcall(ForceError);"
                     "Record(not ok and msg:find('original native failure',1,true) and 1 or 0); Record(17) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 1); T_EQ(jlua_values[1], 17);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, unhandled_error_stops_only_its_script_chain) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() Record(1); ForceError(); Record(2) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 1); T_ASSERT(jass_rterror_pending(j));
        T_ASSERT(strstr(jass_rterror_message(j), "original native failure") != NULL);
    }
    jass_close(j);
}

TEST(jass_lua, fourcc_keeps_original_numeric_value_at_engine_boundary) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() local id=FourCC('hfoo'); Record(id==0x68666f6f and 1 or 0);"
                     "Record(UnitId('hfoo')==id and 1 or 0); Record(UnitId2String(id)=='hfoo' and 1 or 0);"
                     "Record(id) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 4); T_EQ(jlua_values[0], 1); T_EQ(jlua_values[1], 1);
        T_EQ(jlua_values[2], 1); T_EQ(jlua_values[3], 0x68666f6f);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

static void jlua_restore_wait(char const *wait, bool wrapped) {
    char source[1024];
    snprintf(source, sizeof(source), "%slocal n=10; runs=0; kept=function() n=n+1; Record(n) end; "
             "function main() runs=runs+1; local a={v=31}; a.self=a; local result=%s; "
             "Record((result or 12)+a.self.v+runs); kept() end",
             wrapped ? "local original=AddWait; function AddWait(n) return original(n) end; " : "", wait);
    jass_t *j = jlua_fixture();
    if (!jlua_load(j, source)) { jass_close(j); return; }
    jass_callbyname(j, "main", true); jass_runevents(j); T_EQ(jlua_count, 0);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context = &io, .transfer = jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec); T_ASSERT(saved);
    jass_close(j);
    if (!saved) { free(io.bytes); return; }
    j = jlua_fixture();
    if (jlua_load(j, source)) {
        jlua_clock = 1000;
        io.position = 0; io.reading = true;
        bool loaded = jass_readsnapshot(j, &codec); T_ASSERT(loaded);
        if (loaded) {
            jlua_clock = 1039; jass_runevents(j); T_EQ(jlua_count, 0);
            jlua_clock = 1040; jass_runevents(j); T_EQ(jlua_count, 2);
            T_EQ(jlua_values[0], 44); T_EQ(jlua_values[1], 11);
            T_ASSERT(!jass_rterror_pending(j));
        }
    }
    jass_close(j); free(io.bytes);
}

TEST(jass_lua, fresh_vm_load_resumes_lua_heap_and_sleeping_jass_child) { jlua_restore_wait("AddWait(5)", false); }
TEST(jass_lua, fresh_vm_load_resumes_native_wait_inside_lua_stack) { jlua_restore_wait("WaitMilliseconds(40)", false); }
TEST(jass_lua, fresh_vm_load_keeps_waiting_original_bj_under_lua_override) { jlua_restore_wait("AddWait(5)", true); }

/* Runs unchanged in the native and MEMORY64 VM runner. The saved continuation
 * resumes after a real wait in an independently created interpreter. */
TEST(jass_expression, fresh_vm_snapshot_retains_logical_results_and_skipped_side_effects) {
    char const *source =
        "globals\n integer calls = 0\nendglobals\n"
        "function Touch takes nothing returns boolean\n set calls = calls + 1\n return true\nendfunction\n"
        "function CheckAfter takes nothing returns nothing\n call Record(calls)\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " local boolean a = false and Touch()\n local boolean b = true or Touch()\n"
        " if calls == 0 then\n call Record(10)\n endif\n"
        " if 4.0 == 4 then\n call Record(11)\n endif\n"
        " if true or true and false then\n call Record(-1)\n else\n call Record(12)\n endif\n"
        " call WaitMilliseconds(40)\n"
        " if (not a) and b then\n call Record(13)\n endif\n"
        "endfunction\n";
    jass_t *j = jlua_fixture();
    char *copy = strdup(source);
    T_ASSERT(jass_dobuffer(j, copy));
    free(copy);
    jass_callbyname(j, "main", true);
    jass_runevents(j);
    T_EQ(jlua_count, 3);
    T_EQ(jlua_values[0], 10); T_EQ(jlua_values[1], 11); T_EQ(jlua_values[2], 12);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context = &io, .transfer = jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec);
    T_ASSERT(saved);
    jass_close(j);
    if (!saved) { free(io.bytes); return; }
    j = jlua_fixture();
    copy = strdup(source);
    T_ASSERT(jass_dobuffer(j, copy));
    free(copy);
    jlua_clock = 1000;
    io.position = 0; io.reading = true;
    bool loaded = jass_readsnapshot(j, &codec);
    T_ASSERT(loaded);
    if (loaded) {
        jlua_clock = 1039; jass_runevents(j); T_EQ(jlua_count, 0);
        jlua_clock = 1040; jass_runevents(j); T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 13);
        jass_callbyname(j, "CheckAfter", false);
        T_EQ(jlua_count, 2); T_EQ(jlua_values[1], 0);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
    free(io.bytes);
}

TEST(jass_expression, fresh_vm_snapshot_resumes_bare_return_before_unreachable_source) {
    char const *source =
        "function Early takes nothing returns nothing\n"
        " call WaitMilliseconds(40)\n return\n"
        " call Record(-1)\n if true then\n call Record(-2)\n endif\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " call Early()\n call Record(17)\nendfunction\n";
    jass_t *j = jlua_fixture();
    char *copy = strdup(source);
    bool parsed = jass_dobuffer(j, copy);
    T_ASSERT(parsed);
    free(copy);
    if (!parsed) { jass_close(j); return; }
    jass_callbyname(j, "main", true);
    jass_runevents(j);
    T_EQ(jlua_count, 0);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context = &io, .transfer = jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec);
    T_ASSERT(saved);
    jass_close(j);
    if (!saved) { free(io.bytes); return; }
    j = jlua_fixture();
    copy = strdup(source);
    parsed = jass_dobuffer(j, copy);
    T_ASSERT(parsed);
    free(copy);
    if (parsed) {
        jlua_clock = 1000;
        io.position = 0; io.reading = true;
        bool loaded = jass_readsnapshot(j, &codec);
        T_ASSERT(loaded);
        if (loaded) {
            jlua_clock = 1039; jass_runevents(j); T_EQ(jlua_count, 0);
            jlua_clock = 1040; jass_runevents(j); T_EQ(jlua_count, 1);
            T_EQ(jlua_values[0], 17);
            T_ASSERT(!jass_rterror_pending(j));
        }
    }
    jass_close(j);
    free(io.bytes);
}

TEST(jass_lua, saved_handles_and_dynamic_callback_share_jass_and_lua_identity) {
    char const *source = "local n=10; function main() local h=NewPoint(37); sharedPoint=h; "
        "sharedCode=function() n=n+1; Record(n) end; WaitMilliseconds(40); "
        "Record(ReadPoint(h)); Record(h==sharedPoint and 1 or 0); sharedCode() end";
    jass_t *j = jlua_fixture();
    if (!jlua_load(j, source)) { jass_close(j); return; }
    jass_callbyname(j, "main", true); jass_runevents(j);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context=&io, .transfer=jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec); T_ASSERT(saved); jass_close(j);
    if (!saved) { free(io.bytes); return; }
    j = jlua_fixture();
    if (jlua_load(j, source)) {
        io.position=0; io.reading=true;
        bool loaded = jass_readsnapshot(j, &codec); T_ASSERT(loaded);
        if (loaded) {
            jass_callbyname(j, "RestoredCallback", true); jass_runevents(j);
            T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 11);
            jlua_clock=40; jass_runevents(j);
            T_EQ(jlua_count, 4); T_EQ(jlua_values[1], 37); T_EQ(jlua_values[2], 1); T_EQ(jlua_values[3], 12);
            T_ASSERT(!jass_rterror_pending(j));
        }
    }
    jass_close(j); free(io.bytes);
}

TEST(jass_lua, truncated_restore_preserves_live_heap_and_pending_wait) {
    jass_t *j = jlua_fixture();
    if (!jlua_load(j, "local n=31; function main() local a=AddWait(5); Record(a+n) end")) { jass_close(j); return; }
    jass_callbyname(j, "main", true); jass_runevents(j);
    jluaSnapshot_t io = { .bytes = malloc(BZ_JLUA_SNAPSHOT_BYTES) };
    jassSnapshot_t codec = { .context=&io, .transfer=jlua_snapshot_io };
    bool saved = jass_writesnapshot(j, &codec); T_ASSERT(saved);
    if (saved) {
        io.size--; io.position=0; io.reading=true;
        T_ASSERT(!jass_readsnapshot(j, &codec));
        jlua_clock=40; jass_runevents(j);
        T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 43); T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j); free(io.bytes);
}

TEST(jass_lua, global_alias_and_later_function_assignment_resolve_by_name) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function original() Record(1) end; alias=original; "
                     "function main() late=function() Record(9) end end")) {
        jass_callbyname(j, "alias", false); T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 1);
        jass_callbyname(j, "main", true); jass_runevents(j);
        jass_callbyname(j, "late", true); jass_runevents(j);
        T_EQ(jlua_count, 2); T_EQ(jlua_values[1], 9); T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, excess_native_arguments_report_original_signature) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() local ok,msg=pcall(Record,1,2); "
                     "Record(not ok and msg:find('expected 1 arguments',1,true) and 1 or 0) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 1); T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, handled_native_error_does_not_clear_an_earlier_vm_failure) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "function main() pcall(ForceError) end")) {
        jass_pushfunction(j, jass_functionbyname(j, "ForceError")); jass_call(j, 0);
        T_ASSERT(jass_rterror_pending(j));
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_ASSERT(jass_rterror_pending(j)); T_STREQ(jass_rterror_message(j), "original native failure");
    }
    jass_close(j);
}

TEST(jass_lua, saved_native_wrapper_keeps_original_binding_after_global_override) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "local original=Record; function Record(n) original(n+1) end; function main() Record(7) end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 1); T_EQ(jlua_values[0], 8); T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}

TEST(jass_lua, saved_bj_wrapper_does_not_recurse_into_later_lua_override) {
    jass_t *j = jlua_fixture();
    if (jlua_load(j, "local original=OriginalMark; function OriginalMark() original(); Record(30) end; "
                     "function main() OriginalMark() end")) {
        jass_callbyname(j, "main", true); jass_runevents(j);
        T_EQ(jlua_count, 2); T_EQ(jlua_values[0], 29); T_EQ(jlua_values[1], 30);
        T_ASSERT(!jass_rterror_pending(j));
    }
    jass_close(j);
}
