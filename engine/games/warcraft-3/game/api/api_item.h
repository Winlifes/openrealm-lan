uint32_t CreateItem(jass_t *j) {
    int32_t itemid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    ItemData_t const *data;

    /* Human09 recreates Muradin's inventory after the Frostmourne cinematic.
     * Empty inventory slots arrive here as item ID 0; do not pass that
     * sentinel into the generic entity spawner. */
    if (!itemid) {
        fprintf(stderr, "CreateItem: refusing empty item ID at (%.1f, %.1f)\n", x, y);
        return jass_pushnullhandle(j, "item");
    }
    data = G_ItemData((uint32_t)itemid);
    if (!data || !data->file) {
        fprintf(stderr, "CreateItem: unresolved item ID 0x%08x at (%.1f, %.1f)\n",
                (uint32_t)itemid, x, y);
        return jass_pushnullhandle(j, "item");
    }
    edict_t *item = SP_SpawnAtLocation(itemid, 0, &MAKE(vec2_t, x, y));
    return jass_pushlighthandle(j, item, "item");
}
/* Blz* wrappers add a skin argument; the engine keeps one appearance per
 * item ID, so the skin is accepted and ignored. */
uint32_t BlzCreateItemWithSkin(jass_t *j) {
    int32_t itemid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    ItemData_t const *data;

    if (!itemid) {
        return jass_pushnullhandle(j, "item");
    }
    data = G_ItemData((uint32_t)itemid);
    if (!data || !data->file) {
        fprintf(stderr, "BlzCreateItemWithSkin: unresolved item ID 0x%08x at (%.1f, %.1f)\n",
                (uint32_t)itemid, x, y);
        return jass_pushnullhandle(j, "item");
    }
    edict_t *item = SP_SpawnAtLocation(itemid, 0, &MAKE(vec2_t, x, y));
    return jass_pushlighthandle(j, item, "item");
}
uint32_t RemoveItem(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    if (whichItem) G_RemoveItem(whichItem);
    return 0;
}
uint32_t GetItemPlayer(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return item && G_IsItem(item) && item->s.player < MAX_PLAYERS
        ? jass_pushlighthandle(j, G_GetPlayerByNumber(item->s.player), "player")
        : jass_pushnullhandle(j, "player");
}
uint32_t GetItemTypeId(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? (int32_t)item->class_id : 0);
}
/* Item tinting changes replaceable team-color textures, not item owner or gameplay rawcode. */
uint32_t BlzSetItemColor(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    uint32_t *color = jass_checkhandle(j, 2, "playercolor");
    if (G_IsItem(item) && color) G_SetEntityTeamColor(&item->s, *color);
    return 0;
}
/* Equipment inserts a class before Unknown/Any in modern common.j. */
uint32_t GetItemType(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    cstring_t cls = G_IsItem(item) ? item->data.ItemData->itemClass : NULL;
    API_ALLOC(uint32_t, itemtype);
    *itemtype = G_ItemTypeFromClass(j, cls);
    return 1;
}
uint32_t GetItemLevel(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? item->data.ItemData->level : 0);
}
uint32_t GetItemEquipmentType(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    uint32_t value;
    if (!G_ItemEquipmentType(item, &value)) {
        if (G_IsItem(item)) jass_rterror(j, "GetItemEquipmentType: unrecognized authored equipment type");
        return jass_pushnullhandle(j, "equipmentType");
    }
    API_ALLOC(uint32_t, equipmentType);
    *equipmentType = value;
    return 1;
}
uint32_t GetItemTag(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    uint32_t value;
    if (!G_ItemTag(item, &value)) {
        if (G_IsItem(item)) jass_rterror(j, "GetItemTag: unrecognized authored item tag");
        return jass_pushnullhandle(j, "itemTag");
    }
    API_ALLOC(uint32_t, itemTag);
    *itemTag = value;
    return 1;
}
uint32_t GetItemCharges(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? (int32_t)G_ItemCharges(item) : 0);
}
uint32_t SetItemCharges(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    int32_t charges = jass_checkinteger(j, 2);
    if (item) G_SetItemCharges(item, (uint32_t)MAX(charges, 0));
    return 0;
}
uint32_t SetItemDropID(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    int32_t unit_id = jass_checkinteger(j, 2);
    item_t *rec = G_ItemState(item);
    if (rec) rec->drop_id = (uint32_t)unit_id;
    return 0;
}
uint32_t GetItemDropID(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    item_t const *rec = G_ItemState(item);
    return jass_pushinteger(j, rec ? (int32_t)rec->drop_id : 0);
}
uint32_t GetItemX(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushnumber(j, item ? item->s.origin.x : 0);
}
uint32_t GetItemY(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushnumber(j, item ? item->s.origin.y : 0);
}
uint32_t SetItemPosition(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    if (item && G_GetItem(item)->in_world) {
        item->s.origin.x = x;
        item->s.origin.y = y;
        item->s.origin.z = CM_GetHeightAtPoint(x, y);
        item->s.origin2 = MAKE(vec2_t, x, y);
        gi.LinkEntity(item);
    }
    return 0;
}
uint32_t SetItemDropOnDeath(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    item_t *rec = G_ItemState(item);
    bool flag = jass_checkboolean(j, 2);
    if (item && rec) {
        rec->drop_on_death_set = true;
        rec->drop_on_death = flag;
    }
    return 0;
}
uint32_t SetItemDroppable(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    item_t *rec = G_ItemState(item);
    bool flag = jass_checkboolean(j, 2);
    if (item && rec) {
        rec->droppable_set = true;
        rec->droppable = flag;
    }
    return 0;
}
uint32_t SetItemPlayer(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    player_t *player = jass_checkhandle(j, 2, "player");
    bool changeColor = jass_checkboolean(j, 3);
    /* Ownership is independent of the item carrier. The changeColor flag
     * controls presentation only; item visuals do not currently expose a
     * separate, mutable player-colour override. */
    (void)changeColor;
    if (item && G_IsItem(item) && player && PLAYER_NUM(player) < MAX_PLAYERS)
        item->s.player = PLAYER_NUM(player);
    return 0;
}
uint32_t SetItemInvulnerable(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    bool flag = jass_checkboolean(j, 2);
    if (item && G_IsItem(item)) G_SetUnitFlag(item, UNIT_FLAG_INVULNERABLE, flag);
    return 0;
}
uint32_t IsItemInvulnerable(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, item && G_IsItem(item) && G_UnitHasFlag(item, UNIT_FLAG_INVULNERABLE));
}
uint32_t GetManipulatedItem(jass_t *j) {
    edict_t *item = jass_getcontext(j)->source;
    return item && G_IsItem(item) ? jass_pushlighthandle(j, item, "item") : jass_pushnullhandle(j, "item");
}
uint32_t GetOrderTargetItem(jass_t *j) {
    return jass_pushnullhandle(j, "item");
}
uint32_t GetFilterItem(jass_t *j) {
    extern edict_t *currentenumitem;
    return jass_pushlighthandle(j, currentenumitem, "item");
}
uint32_t GetEnumItem(jass_t *j) {
    extern edict_t *currentenumitem;
    return jass_pushlighthandle(j, currentenumitem, "item");
}
uint32_t EnumItemsInRect(jass_t *j) {
    /* The filter and action both observe the candidate item. Preserve the
     * caller's context across recursive enumerations, including null rects. */
    extern edict_t *currentenumitem;
    edict_t *previous = currentenumitem;
    box2_t *r = jass_checkhandle(j, 1, "rect");
    jassFunc_t const *filter = jass_checkhandle(j, 2, "boolexpr");
    jassFunc_t const *actionFunc = jass_checkcode(j, 3);
    if (!r) return 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        item_t const *rec = G_ItemState(ent);
        if (!rec || !rec->in_world ||
            !Box2_containsPoint(r, &ent->s.origin2)) continue;
        currentenumitem = ent;
        if (jass_evaluateboolexpr(j, filter, ent) && actionFunc) {
            /* A filter may recursively enumerate other items. */
            currentenumitem = ent;
            jass_pushfunction(j, actionFunc);
            jass_call(j, 0);
        }
        currentenumitem = previous;
    }
    currentenumitem = previous;
    return 0;
}
uint32_t GetItemName(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    cstring_t name = whichItem ? G_ObjectName(whichItem->class_id) : NULL;
    return jass_pushstring(j, name ? name : "");
}
uint32_t GetItemUserData(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, whichItem ? G_GetItem(whichItem)->user_data : 0);
}
uint32_t SetItemUserData(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    if (whichItem) G_GetItem(whichItem)->user_data = jass_checkinteger(j, 2);
    return 0;
}
uint32_t SetItemVisible(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    bool show = jass_checkboolean(j, 2);
    if (!whichItem || !G_IsItem(whichItem)) return 0;
    if (show) {
        whichItem->s.renderfx &= ~RF_HIDDEN;
        whichItem->svflags &= ~SVF_NOCLIENT;
    } else {
        whichItem->s.renderfx |= RF_HIDDEN;
        whichItem->svflags |= SVF_NOCLIENT;
    }
    return 0;
}
uint32_t IsItemVisible(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, whichItem && !(whichItem->s.renderfx & RF_HIDDEN) &&
                              !(whichItem->svflags & SVF_NOCLIENT));
}
uint32_t IsItemOwned(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    item_t *rec = G_GetItem(whichItem);
    return jass_pushboolean(j, whichItem && rec->carrier && !rec->in_world);
}
uint32_t IsItemPowerup(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    ItemData_t const *data = whichItem ? whichItem->data.ItemData : NULL;
    if (!data && whichItem) data = G_ItemData(whichItem->class_id);
    return jass_pushboolean(j, data && data->powerup);
}
uint32_t SetItemPawnable(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    item_t *rec = G_GetItem(whichItem);
    bool flag = jass_checkboolean(j, 2);
    if (whichItem) {
        rec->pawnable_set = true;
        rec->pawnable = flag;
    }
    return 0;
}

/* Campaign / Blizzard.j native batch: item flags are read straight from the
 * ItemData row; the Reforged field setters are accepted unmodeled. */
uint32_t IsItemIdPawnable(jass_t *j) {
    ItemData_t const *data = G_ItemData((uint32_t)jass_checkinteger(j, 1));
    return jass_pushboolean(j, data ? data->pawnable : 0);
}
uint32_t IsItemIdSellable(jass_t *j) {
    ItemData_t const *data = G_ItemData((uint32_t)jass_checkinteger(j, 1));
    return jass_pushboolean(j, data ? data->sellable : 0);
}
uint32_t IsItemIdPowerup(jass_t *j) {
    ItemData_t const *data = G_ItemData((uint32_t)jass_checkinteger(j, 1));
    return jass_pushboolean(j, data ? data->powerup : 0);
}
uint32_t IsItemPawnable(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    ItemData_t const *data = item ? G_ItemData(item->class_id) : NULL;
    return jass_pushboolean(j, data ? data->pawnable : 0);
}
uint32_t IsItemSellable(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    ItemData_t const *data = item ? G_ItemData(item->class_id) : NULL;
    return jass_pushboolean(j, data ? data->sellable : 0);
}
uint32_t BlzItemAddAbility(jass_t *j) { (void)j; return jass_pushboolean(j, 1); }
uint32_t BlzItemRemoveAbility(jass_t *j) { (void)j; return jass_pushboolean(j, 1); }
uint32_t BlzSetItemBooleanField(jass_t *j) { (void)j; return jass_pushboolean(j, 1); }
uint32_t BlzSetItemIntegerField(jass_t *j) { (void)j; return jass_pushboolean(j, 1); }
uint32_t BlzSetItemRealField(jass_t *j) { (void)j; return jass_pushboolean(j, 1); }
uint32_t BlzSetItemStringField(jass_t *j) { (void)j; return jass_pushboolean(j, 1); }
