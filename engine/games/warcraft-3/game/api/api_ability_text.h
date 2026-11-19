static uint32_t AbilityTextGet(jass_t *j, abilityTextField_t field) {
    uint32_t ability = jass_checkinteger(j, 1);
    int32_t rank = jass_checkinteger(j, 2);
    string_t text = G_CopyAbilityTooltipText(ability, field, rank, currentplayer ? PLAYER_NUM(currentplayer) : -1);
    uint32_t result = jass_pushstring(j, text ? text : "");
    if (text) gi.MemFree(text);
    return result;
}

static uint32_t AbilityTextSet(jass_t *j, abilityTextField_t field) {
    uint32_t ability = jass_checkinteger(j, 1);
    cstring_t text = jass_checkstring(j, 2);
    int32_t rank = jass_checkinteger(j, 3);
    G_SetAbilityTooltipText(ability, field, rank, currentplayer ? PLAYER_NUM(currentplayer) : -1, text);
    return 0;
}

#define ABILITY_TEXT_API(name, field) \
uint32_t BlzGetAbility##name(jass_t *j) { return AbilityTextGet(j, field); } \
uint32_t BlzSetAbility##name(jass_t *j) { return AbilityTextSet(j, field); }
ABILITY_TEXT_API(Tooltip, ABILITY_TEXT_NORMAL)
ABILITY_TEXT_API(ActivatedTooltip, ABILITY_TEXT_ACTIVATED)
ABILITY_TEXT_API(ExtendedTooltip, ABILITY_TEXT_EXTENDED)
ABILITY_TEXT_API(ActivatedExtendedTooltip, ABILITY_TEXT_ACTIVATED_EXTENDED)
ABILITY_TEXT_API(ResearchTooltip, ABILITY_TEXT_RESEARCH)
ABILITY_TEXT_API(ResearchExtendedTooltip, ABILITY_TEXT_RESEARCH_EXTENDED)
#undef ABILITY_TEXT_API
