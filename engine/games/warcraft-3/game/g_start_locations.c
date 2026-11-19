/* Resolve authored config()/W3I start locations before main creates melee
 * units. These preferences belong to WC3 map configuration, not transport. */
#include "g_local.h"

#define START_PLACEMENT_TRIALS 32 /* shuffled candidate assignments per map */
#define START_PLACEMENT_PASSES 16 /* improving swap passes per candidate */

typedef struct {
    uint32_t high, low;
    double ally_distance;
} startPlacementScore_t;

typedef struct {
    gameClient_t *players[MAX_PLAYERS];
    int32_t locations[MAX_PLAYERS];
    bool locked[MAX_PLAYERS];
    uint32_t count, location_count;
} startPlacement_t;

static void G_ReadStartPriorityMasks(startLocationPriorities_t *table, uint32_t low, uint32_t high) {
    FOR_LOOP(i, MAX_PLAYERS) {
        uint32_t bit = 1u << i;
        if (!((low | high) & bit)) continue;
        table->slots[table->count].location = i;
        table->slots[table->count++].priority = high & bit ? 1 : 0;
    }
}

void G_InitStartLocationConfiguration(mapInfo_t const *info) {
    level.setup.placement = 2; /* Unconfigured/campaign maps retain their authored locations. */
    level.setup.forced_start_locations = level.setup.defined_start_locations = 0;
    memset(level.setup.start_prio, 0, sizeof(level.setup.start_prio));
    memset(level.setup.enemy_start_prio, 0, sizeof(level.setup.enemy_start_prio));
    if (!info) return;
    FOR_LOOP(i, MAX_PLAYERS) {
        mapPlayer_t const *player = info->players + i;
        if (player->used) level.setup.defined_start_locations |= 1u << i;
        G_ReadStartPriorityMasks(level.setup.start_prio + i, player->allyLowPrioritiesFlags, player->allyHighPrioritiesFlags);
        G_ReadStartPriorityMasks(level.setup.enemy_start_prio + i, player->enemyLowPrioritiesFlags, player->enemyHighPrioritiesFlags);
    }
}

static uint32_t G_StartLocationPriority(startLocationPriorities_t const *table, int32_t location) {
    FOR_LOOP(i, table->count) if (table->slots[i].location == location) return table->slots[i].priority;
    return 2; /* MAP_LOC_PRIO_NOT: no priority, rather than a forbidden start. */
}

static startPlacementScore_t G_StartPlacementScore(mapInfo_t const *info, startPlacement_t const *placement) {
    startPlacementScore_t score = {0};
    FOR_LOOP(a, placement->count) {
        FOR_LOOP(b, placement->count) {
            if (a == b) continue;
            bool allies = placement->players[a]->ps.team == placement->players[b]->ps.team;
            int32_t from = placement->locations[a], to = placement->locations[b];
            startLocationPriorities_t const *table = allies ? level.setup.start_prio + from : level.setup.enemy_start_prio + from;
            uint32_t priority = G_StartLocationPriority(table, to);
            score.high += priority == 1;
            score.low += priority == 0;
            if (allies) {
                vec2_t p = info->players[from].startingPosition, q = info->players[to].startingPosition;
                double dx = (double)p.x - q.x, dy = (double)p.y - q.y;
                score.ally_distance += dx * dx + dy * dy;
            }
        }
    }
    return score;
}

static bool G_StartPlacementBetter(startPlacementScore_t a, startPlacementScore_t b) {
    if (a.high != b.high) return a.high > b.high;
    if (a.low != b.low) return a.low > b.low;
    return a.ally_distance < b.ally_distance;
}

static void G_SwapStartLocations(startPlacement_t *placement, uint32_t a, uint32_t b) {
    int32_t old = placement->locations[a];
    placement->locations[a] = placement->locations[b];
    placement->locations[b] = old;
}

static void G_ShuffleStartLocations(startPlacement_t *placement) {
    uint32_t free_rows[MAX_PLAYERS], count = 0;
    FOR_LOOP(i, placement->location_count) if (!placement->locked[i]) free_rows[count++] = i;
    for (uint32_t i = count; i > 1; i--) G_SwapStartLocations(placement, free_rows[i - 1], free_rows[rand() % i]);
}

static startPlacementScore_t G_ImproveStartPlacement(mapInfo_t const *info, startPlacement_t *placement) {
    startPlacementScore_t score = G_StartPlacementScore(info, placement);
    FOR_LOOP(pass, START_PLACEMENT_PASSES) {
        startPlacementScore_t best = score;
        uint32_t best_a = 0, best_b = 0;
        FOR_LOOP(a, placement->count) {
            if (placement->locked[a]) continue;
            for (uint32_t b = a + 1; b < placement->location_count; b++) {
                if (placement->locked[b]) continue;
                G_SwapStartLocations(placement, a, b);
                startPlacementScore_t trial = G_StartPlacementScore(info, placement);
                G_SwapStartLocations(placement, a, b);
                if (G_StartPlacementBetter(trial, best)) { best = trial; best_a = a; best_b = b; }
            }
        }
        if (!G_StartPlacementBetter(best, score)) break;
        G_SwapStartLocations(placement, best_a, best_b);
        score = best;
    }
    return score;
}

void G_ResolveMapStartLocations(mapInfo_t const *info) {
    startPlacement_t placement = {0};
    uint32_t defined = level.setup.defined_start_locations, reserved = 0, locked_locations = 0;
    if (!info) return;
    /* W3I starts also cover closed slots that remain candidates for random play. */
    FOR_LOOP(i, MAX_PLAYERS) if (info->players[i].used) defined |= 1u << i;
    FOR_LOOP(i, MAX_PLAYERS) {
        gameClient_t *client = G_GetPlayerClientByNumber(i);
        if (!client) continue;
        bool forced = (level.setup.forced_start_locations & (1u << i)) || (info->players[i].flags & fixed_start_position);
        int32_t location = client->ps.start_location;
        if (forced && location >= 0 && location < MAX_PLAYERS) reserved |= 1u << location;
        if (client->jass.controller > 1) continue; /* human/computer combat slots only */
        if (forced) {
            if (location < 0 || location >= MAX_PLAYERS || !(defined & (1u << location)) || (locked_locations & (1u << location))) return;
            locked_locations |= 1u << location;
        }
        uint32_t row = placement.count++;
        placement.players[row] = client;
        placement.locked[row] = forced;
        placement.locations[row] = location;
    }
    if (!placement.count) return;
    placement.location_count = placement.count;
    if (level.setup.placement == 0 || level.setup.placement == 3) {
        int32_t available[MAX_PLAYERS];
        uint32_t count = 0, used = 0;
        FOR_LOOP(i, MAX_PLAYERS) if ((defined & (1u << i)) && !(reserved & (1u << i))) available[count++] = i;
        FOR_LOOP(i, placement.count) if (!placement.locked[i]) used++;
        /* A malformed map cannot supply a unique assignment. Keep the script's
         * locations instead of inventing coordinates or overwriting forces. */
        if (count < used) return;
        used = 0;
        FOR_LOOP(i, placement.count) if (!placement.locked[i]) placement.locations[i] = available[used++];
        while (used < count) placement.locations[placement.location_count++] = available[used++];
        G_ShuffleStartLocations(&placement);
        if (level.setup.placement == 3) {
            startPlacement_t best = placement;
            startPlacementScore_t score = G_ImproveStartPlacement(info, &best);
            for (uint32_t trial = 1; trial < START_PLACEMENT_TRIALS; trial++) {
                G_ShuffleStartLocations(&placement);
                startPlacementScore_t candidate = G_ImproveStartPlacement(info, &placement);
                if (G_StartPlacementBetter(candidate, score)) { best = placement; score = candidate; }
            }
            placement = best;
        }
    }
    FOR_LOOP(i, placement.count) {
        gameClient_t *client = placement.players[i];
        int32_t location = placement.locations[i];
        if (location < 0 || location >= MAX_PLAYERS || !(defined & (1u << location))) continue;
        vec2_t origin = info->players[location].startingPosition;
        client->ps.start_location = location;
        client->ps.vieworigin = G_MakeServerOrigin(origin.x, origin.y, 0);
        client->camera.state.position = client->camera.old_state.position = origin;
        client->camera.target_height = client->ps.vieworigin.z;
    }
}
