#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"

bool run_test_jass(cstring_t src);
bool run_test_jass_error(cstring_t src, cstring_t expected);

/* Original Worm War looks up the triggering Scorpion through a handle array.
 * Keep the authored local types and comparison order in the queued action. */
TEST(wc3_jass_expression, trigger_callback_false_condition_restores_locals_for_elseif_array) {
    T_ASSERT(run_test_jass(
        "globals\n integer count = 3\n trigger array watched\n integer hits = 0\n"
        " unit array eatingEffect\n boolean array respawn\nendglobals\n"
        "function IsWormAbleToEatScorpion takes integer nPlayerIndex returns boolean\n"
        " return eatingEffect[nPlayerIndex] != null\nendfunction\n"
        "function ScorpionHitsActions takes nothing returns nothing\n"
        " local integer nScorpionIndex = 0\n"
        " local trigger trg = GetTriggeringTrigger()\n"
        " local unit uHit = GetTriggerUnit()\n"
        " local integer nPlayerIndex = 0\n"
        " set nScorpionIndex = 0\n"
        " loop\n"
        "  exitwhen nScorpionIndex >= count\n"
        "  exitwhen trg == watched[nScorpionIndex]\n"
        "  set nScorpionIndex = nScorpionIndex + 1\n"
        " endloop\n"
        " call BJassAssert(nScorpionIndex == 1, \"trigger-array callback index\")\n"
        " set nPlayerIndex = nScorpionIndex + 3\n"
        " if IsWormAbleToEatScorpion(nPlayerIndex) then\n"
        "  call BJassAssert(false, \"unexpected eating effect\")\n"
        " elseif not respawn[nPlayerIndex] then\n"
        "  call BJassAssert(nPlayerIndex == 4, \"elseif lost parent local\")\n"
        "  set hits = hits + 1\n"
        " endif\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        " call BJassAssert(hits == 1, \"queued action completed\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        " set watched[1] = CreateTrigger()\n"
        " call TriggerAddAction(watched[1], function ScorpionHitsActions)\n"
        " call TriggerExecute(watched[1])\n"
        "endfunction\n"));
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_jass_expression, suspended_condition_and_nested_branches_restore_parent_scope) {
    T_ASSERT(run_test_jass(
        "globals\n integer stage = 0\n boolean array respawn\nendglobals\n"
        "function DelayedFalse takes integer key returns boolean\n"
        " local integer preserved = key + 1\n"
        " set stage = 1\n"
        " call TriggerSleepAction(0.25)\n"
        " call BJassAssert(preserved == 12, \"condition local across wait\")\n"
        " return false\n"
        "endfunction\n"
        "function Accept takes integer key returns boolean\n return key == 11\nendfunction\n"
        "function Reject takes integer key returns boolean\n return key != 11\nendfunction\n"
        "function CheckBranches takes nothing returns nothing\n"
        " local integer key = 11\n"
        " if DelayedFalse(key) then\n"
        "  call BJassAssert(false, \"false condition entered\")\n"
        " elseif Accept(key) then\n"
        "  call BJassAssert(key == 11, \"elseif function argument scope\")\n"
        "  if Reject(key) then\n"
        "   call BJassAssert(false, \"nested false condition entered\")\n"
        "  elseif respawn[key] then\n"
        "   call BJassAssert(false, \"nested array condition\")\n"
        "  elseif not Reject(key) then\n"
        "   call BJassAssert(key == 11, \"negated true branch scope\")\n"
        "   set stage = 2\n"
        "  endif\n"
        " endif\n"
        "endfunction\n"
        "function waiting takes nothing returns nothing\n call BJassAssert(stage == 1, \"condition must wait\")\nendfunction\n"
        "function verify takes nothing returns nothing\n call BJassAssert(stage == 2, \"condition chain resumed\")\nendfunction\n"
        "function main takes nothing returns nothing\n call CheckBranches()\nendfunction\n"));
    jass_callbyname(level.vm, "waiting", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.time += 300;
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_jass_expression, mixed_numeric_equality_and_signed_zero) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        " call BJassAssert(0.0 == 0, \"real equals integer\")\n"
        " call BJassAssert(0 == 0.0, \"integer equals real\")\n"
        " call BJassAssert(4.0 == 4, \"positive mixed equality\")\n"
        " call BJassAssert(-4 == -4.0, \"negative mixed equality\")\n"
        " call BJassAssert(4.0 != 5, \"mixed inequality\")\n"
        " call BJassAssert(5 != 4.0, \"reverse mixed inequality\")\n"
        " call BJassAssert(-0.0 == 0.0, \"signed zeros compare numerically\")\n"
        "endfunction\n"));
}

TEST(wc3_jass_expression, logical_first_argument_retains_following_string_in_coroutine) {
    T_ASSERT(run_test_jass_error(
        "function main takes nothing returns nothing\n"
        " call BJassAssert(false and true, \"logical argument tail\")\n"
        "endfunction\n", "assertion failed: logical argument tail"));
}

TEST(wc3_jass_expression, logical_first_argument_retains_following_string_synchronously) {
    T_ASSERT(run_test_jass(
        "function CheckSync takes nothing returns nothing\n"
        " call BJassAssert(false or false, \"synchronous logical tail\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\nendfunction\n"));
    jass_callbyname(level.vm, "CheckSync", false);
    T_ASSERT(jass_rterror_pending(level.vm));
    T_STREQ(jass_rterror_message(level.vm), "assertion failed: synchronous logical tail");
}

/* pjass grammar.y gives OR higher binding than AND, and its issue #10 records
 * the original game's false-and-true-or-true short-circuit result. Do not
 * substitute conventional C precedence for authored JASS expressions. */
TEST(wc3_jass_expression, original_jass_or_precedes_and_and_parentheses_override_it) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        " local boolean a = true or true and false\n"
        " local boolean b = (true or true) and false\n"
        " local boolean c = true or (true and false)\n"
        " local boolean d = false and true or true\n"
        " call BJassAssert(not a, \"JASS or precedence\")\n"
        " call BJassAssert(not b, \"parenthesized left operand\")\n"
        " call BJassAssert(c, \"parenthesized right operand\")\n"
        " call BJassAssert(not d, \"original JASS short-circuit grouping\")\n"
        "endfunction\n"));
}

TEST(wc3_jass_expression, boolean_operators_short_circuit_side_effects) {
    T_ASSERT(run_test_jass(
        "globals\n integer touched = 0\nendglobals\n"
        "function Touch takes nothing returns boolean\n"
        " set touched = touched + 1\n return true\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " local boolean a = false and Touch()\n"
        " local boolean b = true or Touch()\n"
        " call BJassAssert(touched == 0, \"short-circuited calls have no side effects\")\n"
        " set a = true and Touch()\n"
        " set b = false or Touch()\n"
        " call BJassAssert(touched == 2, \"required operands each run once\")\n"
        "endfunction\n"));
}

TEST(wc3_jass_expression, ordinary_function_with_operator_name_keeps_ordinary_arguments) {
    T_ASSERT(run_test_jass(
        "globals\n integer touched = 0\nendglobals\n"
        "function Touch takes nothing returns boolean\n"
        " set touched = touched + 1\n return true\nendfunction\n"
        "function __and takes boolean a, boolean b returns boolean\n return b\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " local boolean value = __and(false, Touch())\n"
        " call BJassAssert(value, \"ordinary function result\")\n"
        " call BJassAssert(touched == 1, \"ordinary function arguments are not short-circuited\")\n"
        "endfunction\n"));
}

TEST(wc3_jass_expression, galaxy_retains_c_logical_and_bitwise_ordering) {
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    char source[] =
        "void GalaxyExpressions() {\n"
        " bool a = true || true && false;\n"
        " bool b = false && true || true;\n"
        " BJassAssert(a, \"Galaxy logical precedence\");\n"
        " BJassAssert(b, \"Galaxy reverse logical precedence\");\n"
        " BJassAssert((2 | 4 & 1) == 2, \"Galaxy bitwise precedence\");\n"
        " BJassAssert(((2 | 4) & 1) == 0, \"Galaxy explicit bitwise grouping\");\n"
        "}\n";
    T_ASSERT(jass_dobuffer_ex(level.vm, source, JASS_MODE_GALAXY));
    jass_callbyname(level.vm, "GalaxyExpressions", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* Original Bomber Command and Azeroth Grand Prix keep unreachable source
 * after a bare return. The next line remains a statement, not its value. */
TEST(wc3_jass_expression, bare_return_before_unreachable_if) {
    T_ASSERT(run_test_jass(
        "globals\n integer touched = 0\nendglobals\n"
        "function Early takes nothing returns nothing\n"
        " return\n\n // Original map's old implementation follows.\n"
        " if true then\n set touched = 1\n endif\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " call Early()\n call BJassAssert(touched == 0, \"returned before if\")\nendfunction\n"));
}

TEST(wc3_jass_expression, bare_return_before_unreachable_call) {
    T_ASSERT(run_test_jass(
        "function Early takes nothing returns nothing\n"
        " return // Debug source below remains parsed.\n"
        " call BJassAssert(false, \"unreachable call\")\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " call Early()\n call BJassAssert(true, \"caller continues\")\nendfunction\n"));
}

TEST(wc3_jass_expression, bare_return_in_loop_propagates_without_reading_next_set) {
    T_ASSERT(run_test_jass(
        "globals\n integer touched = 0\nendglobals\n"
        "function Early takes nothing returns nothing\n"
        " loop\n if true then\n return\n set touched = 1\n endif\n endloop\n"
        " set touched = 2\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " call Early()\n call BJassAssert(touched == 0, \"nested return\")\nendfunction\n"));
}

TEST(wc3_jass_expression, return_value_and_bare_return_keep_crlf_boundaries) {
    T_ASSERT(run_test_jass(
        "function Value takes nothing returns integer\r\n"
        " return \t (3 + 4)\r\nendfunction\r\n"
        "function Early takes nothing returns nothing\r\n"
        " return \t // Preserve CRLF.\r\n"
        " call BJassAssert(false, \"unreachable CRLF call\")\r\nendfunction\r\n"
        "function main takes nothing returns nothing\r\n"
        " call Early()\r\n call BJassAssert(Value() == 7, \"valued return\")\r\nendfunction\r\n"));
}
#endif
