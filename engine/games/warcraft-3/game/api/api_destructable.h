uint32_t CreateDestructable(jass_t *j) {
    int32_t objectid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float face = jass_checknumber(j, 4);
    float scale = jass_checknumber(j, 5);
    int32_t variation = jass_checkinteger(j, 6);
    edict_t *d = G_CreateDestructable(objectid, x, y, CM_GetHeightAtPoint(x, y),
                                     DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
/* Shared typed argument decoder for current editor-generated creation calls. */
static uint32_t CreateModernDestructable(jass_t *j,bool has_z,bool dead,bool has_skin,bool has_pitch_roll,bool has_color) {
    uint32_t id=jass_checkinteger(j,1);float x=jass_checknumber(j,2),y=jass_checknumber(j,3);
    int argument=4;
    float z=has_z?jass_checknumber(j,argument++):CM_GetHeightAtPoint(x,y);
    float face=jass_checknumber(j,argument++),roll=0,pitch=0;
    if(has_pitch_roll){roll=jass_checknumber(j,argument++);pitch=jass_checknumber(j,argument++);}
    float scale=jass_checknumber(j,argument++);uint32_t variation=jass_checkinteger(j,argument++);
    uint32_t skin=has_skin?jass_checkinteger(j,argument++):id;
    uint32_t *color=has_color?jass_checkhandle(j,argument,"playercolor"):NULL;
    /* The current WC3 snapshot contract has one heading. Do not discard a
     * tilted placement and report it as supported: standard retail scripts
     * use zero here; tilted custom placements need a pose protocol extension. */
    if(!isfinite(roll)||!isfinite(pitch)||fabsf(remainderf(roll,360))>0.0001f||fabsf(remainderf(pitch,360))>0.0001f) {
        jass_rterror(j,"non-zero destructable pitch/roll is not supported by the classic pose contract");return 0;
    }
    if(skin&&skin!=id&&(!G_DestructableData(skin)->file||!*G_DestructableData(skin)->file)) {
        jass_rterror(j,"unknown destructable skin");return 0;
    }
    edict_t *ent=dead?G_CreateDeadDestructable(id,x,y,z,DEG2RAD(face),scale,variation)
                     :G_CreateDestructable(id,x,y,z,DEG2RAD(face),scale,variation);
    if(ent&&skin&&skin!=id)G_SetDestructableSkin(ent,skin);
    /* Modern campaign generators append playercolor after the skin; retain it
     * in the ordinary entity presentation payload, including explicit red (0). */
    if(ent&&color)G_SetEntityTeamColor(&ent->s,*color);
    return jass_pushlighthandle(j,ent,"destructable");
}
#define MODERN_DESTRUCTABLE(Name,z,dead,skin,tilt) uint32_t Name(jass_t *j){return CreateModernDestructable(j,z,dead,skin,tilt,false);}
MODERN_DESTRUCTABLE(BlzCreateDestructableWithSkin,false,false,true,false)
MODERN_DESTRUCTABLE(BlzCreateDestructableZWithSkin,true,false,true,false)
MODERN_DESTRUCTABLE(BlzCreateDeadDestructableWithSkin,false,true,true,false)
MODERN_DESTRUCTABLE(BlzCreateDeadDestructableZWithSkin,true,true,true,false)
MODERN_DESTRUCTABLE(BlzCreateDestructablePitchRoll,false,false,false,true)
MODERN_DESTRUCTABLE(BlzCreateDestructableZPitchRoll,true,false,false,true)
MODERN_DESTRUCTABLE(BlzCreateDeadDestructablePitchRoll,false,true,false,true)
MODERN_DESTRUCTABLE(BlzCreateDeadDestructableZPitchRoll,true,true,false,true)
MODERN_DESTRUCTABLE(BlzCreateDestructableWithSkinPitchRoll,false,false,true,true)
MODERN_DESTRUCTABLE(BlzCreateDestructableZWithSkinPitchRoll,true,false,true,true)
MODERN_DESTRUCTABLE(BlzCreateDeadDestructableWithSkinPitchRoll,false,true,true,true)
MODERN_DESTRUCTABLE(BlzCreateDeadDestructableZWithSkinPitchRoll,true,true,true,true)
#undef MODERN_DESTRUCTABLE
#define COLORED_DESTRUCTABLE(Name,z,dead) uint32_t Name(jass_t *j){return CreateModernDestructable(j,z,dead,true,true,true);}
COLORED_DESTRUCTABLE(BlzCreateDestructableWithSkinPitchRollColor,false,false)
COLORED_DESTRUCTABLE(BlzCreateDestructableZWithSkinPitchRollColor,true,false)
COLORED_DESTRUCTABLE(BlzCreateDeadDestructableWithSkinPitchRollColor,false,true)
COLORED_DESTRUCTABLE(BlzCreateDeadDestructableZWithSkinPitchRollColor,true,true)
#undef COLORED_DESTRUCTABLE
uint32_t CreateDestructableZ(jass_t *j) {
    int32_t objectid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float z = jass_checknumber(j, 4);
    float face = jass_checknumber(j, 5);
    float scale = jass_checknumber(j, 6);
    int32_t variation = jass_checkinteger(j, 7);
    edict_t *d = G_CreateDestructable(objectid, x, y, z, DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
uint32_t CreateDeadDestructable(jass_t *j) {
    int32_t objectid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float face = jass_checknumber(j, 4);
    float scale = jass_checknumber(j, 5);
    int32_t variation = jass_checkinteger(j, 6);
    edict_t *d = G_CreateDeadDestructable(objectid, x, y, CM_GetHeightAtPoint(x, y),
                                         DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
uint32_t CreateDeadDestructableZ(jass_t *j) {
    int32_t objectid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float z = jass_checknumber(j, 4);
    float face = jass_checknumber(j, 5);
    float scale = jass_checknumber(j, 6);
    int32_t variation = jass_checkinteger(j, 7);
    edict_t *d = G_CreateDeadDestructable(objectid, x, y, z,
                                         DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
uint32_t RemoveDestructable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    G_RemoveDestructable(d);
    return 0;
}
uint32_t SetDestructableVertexColor(jass_t *j) {
    return API_SetWidgetVertexColor(j, "destructable");
}
uint32_t KillDestructable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    G_KillDestructable(d, NULL);
    return 0;
}
/* Ghidra: SetDestructableInvulnerable=FUN_003f83a0 sets an invuln flag bit on
 * the destructable (vtable+0xac); IsDestructableInvulnerable=FUN_003f83d0 reads
 * it (bit 3 of flags @+0x20).  Our edict uses `UNIT_FLAG_INVULNERABLE`, honored
 * by the damage path, so reuse it. */
uint32_t SetDestructableInvulnerable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    bool flag = jass_checkboolean(j, 2);
    if (d) {
        G_SetUnitFlag(d, UNIT_FLAG_INVULNERABLE, flag);
    }
    return 0;
}
uint32_t IsDestructableInvulnerable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushboolean(j, d && G_UnitHasFlag(d, UNIT_FLAG_INVULNERABLE));
}
uint32_t EnumDestructablesInRect(jass_t *j) {
    /* Visit every destructable inside the rect, exposing each as the enum
     * destructable (GetEnumDestructable) while the action runs.  Mirrors
     * GroupEnumUnitsInRect + ForGroup; like GroupEnumUnitsInRect we ignore the
     * boolexpr filter (arg 2) for now. */
    extern edict_t *currentdestructable;
    /* Actions may invoke this native recursively; preserve the outer enum
     * widget so GetEnumDestructable/GetFilterDestructable keep their context. */
    edict_t *previousdestructable = currentdestructable;
    box2_t *r = jass_checkhandle(j, 1, "rect");
    jassFunc_t const *actionFunc = jass_checkcode(j, 3);
    if (!r) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (G_IsDestructable(ent) && Box2_containsPoint(r, &ent->s.origin2)) {
            currentdestructable = ent;
            if (actionFunc) {
                jass_pushfunction(j, actionFunc);
                jass_call(j, 0);
            }
        }
    }
    currentdestructable = previousdestructable;
    return 0;
}
uint32_t GetDestructableTypeId(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushinteger(j, d ? (int32_t)d->class_id : 0);
}
uint32_t GetDestructableName(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    cstring_t name = G_IsDestructable(d) && d->data.DestructableData ? d->data.DestructableData->displayName : NULL;
    if (name && !strncmp(name, "WESTRING_", 9)) {
        cstring_t localized = FindConfigValue("WorldEditStrings", name);
        if (localized) name = localized;
    }
    if (name) name = G_LevelString(name);
    size_t length = name ? strlen(name) : 0;
    if (length >= 2 && name[0] == '"' && name[length - 1] == '"') return jass_pushstringlen(j, name + 1, (uint32_t)length - 2);
    return jass_pushstring(j, name ? name : "");
}
uint32_t GetDestructableX(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? d->s.origin.x : 0);
}
uint32_t GetDestructableY(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? d->s.origin.y : 0);
}
uint32_t SetDestructableLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    float life = jass_checknumber(j, 2);
    G_SetDestructableLife(d, life);
    return 0;
}
uint32_t GetDestructableLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? (float)d->health.value : 0);
}
uint32_t SetDestructableMaxLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    float max = jass_checknumber(j, 2);
    if (d) {
        d->health.max_value = MAX(0.0f, max);
        if (d->health.value > d->health.max_value || d->health.max_value <= 0.0f) {
            G_SetDestructableLife(d, d->health.max_value);
        }
    }
    return 0;
}
uint32_t GetDestructableMaxLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? (float)d->health.max_value : 0);
}
uint32_t SetDestructableOccluderHeight(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    float const height = jass_checknumber(j, 2);
    if (G_IsDestructable(d)) {
        G_GetDestructable(d)->occluder_height = height;
        if (height > 0.0f || d->targtype == TARG_TREE)
            d->s.flags |= EF_FOW_BLOCKER;
        else
            d->s.flags &= ~EF_FOW_BLOCKER;
        G_FowMarkBlockersDirty();
        gi.LinkEntity(d);
    }
    return 0;
}
uint32_t GetDestructableOccluderHeight(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, G_IsDestructable(d) ? G_GetDestructable(d)->occluder_height : 0.0f);
}
uint32_t DestructableRestoreLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    float life = jass_checknumber(j, 2);
    bool birth = jass_checkboolean(j, 3);
    G_RestoreDestructable(d, life, birth);
    return 0;
}
uint32_t QueueDestructableAnimation(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    cstring_t const animation = jass_checkstring(j, 2);
    if (G_IsDestructable(d) && animation)
        d->queued_animation = G_InternAnimationName(animation);
    return 0;
}
uint32_t SetDestructableAnimation(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    cstring_t animation = jass_checkstring(j, 2);
    if (G_IsDestructable(d) && animation) {
        d->queued_animation = "";
        G_SetUnitAnimation(d, animation);
        G_SetUnitFlag(d, UNIT_FLAG_ANIMATION_OVERRIDE, d->animation != NULL);
        if (d->animation) d->s.frame = d->animation->interval[0];
    }
    return 0;
}
/* Destructables share the saved animation clock with units; the native only
 * changes its rate, preserving the current MDX sequence and gameplay state. */
uint32_t SetDestructableAnimationSpeed(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    float speed = jass_checknumber(j, 2);
    if (G_IsDestructable(d)) d->animation_speed = MAX(0.0f, speed);
    return 0;
}
/* Ghidra: ShowDestructable=FUN_003f8790 — show (flag!=0) calls the entity's
 * show method (vtable+0x84), hide calls hide (vtable+0x88).  Our equivalent of
 * that visibility toggle is the RF_HIDDEN renderfx bit, exactly as ShowUnit. */
uint32_t ShowDestructable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    bool show = jass_checkboolean(j, 2);
    if (d) {
        bool const was_hidden = !!(d->s.renderfx & RF_HIDDEN);
        if (show) {
            d->s.renderfx &= ~RF_HIDDEN;
        } else {
            d->s.renderfx |= RF_HIDDEN;
        }
        if ((d->s.flags & EF_FOW_BLOCKER) && was_hidden != !!(d->s.renderfx & RF_HIDDEN)) G_FowMarkBlockersDirty();
    }
    return 0;
}
