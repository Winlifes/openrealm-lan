#include "server.h"

/* Send one persistent minimap icon operation to a player.  Fixed payload so
 * the client can bounds-check like svc_minimap_ping. */
void SV_MinimapIcon(edict_t *ent, uint32_t op, uint32_t slot, vec2_t const *position, color32_t color,
                    uint32_t visible) {
    client_t *client;

    if (op != MINIMAP_ICON_ADD && op != MINIMAP_ICON_REMOVE && op != MINIMAP_ICON_CLEAR &&
        op != MINIMAP_ICON_VISIBLE) return;
    if (op != MINIMAP_ICON_CLEAR && slot >= MINIMAP_ICON_SLOTS) return;
    client = SV_ClientForEntityRecipient(ent);
    if (!client) return;
    MSG_WriteByte(&client->netchan.message, svc_minimap_icon);
    MSG_WriteByte(&client->netchan.message, (int)op);
    MSG_WriteByte(&client->netchan.message, (int)slot);
    MSG_WriteFloat(&client->netchan.message, position ? position->x : 0.0f);
    MSG_WriteFloat(&client->netchan.message, position ? position->y : 0.0f);
    MSG_WriteByte(&client->netchan.message, color.r);
    MSG_WriteByte(&client->netchan.message, color.g);
    MSG_WriteByte(&client->netchan.message, color.b);
    MSG_WriteByte(&client->netchan.message, color.a);
    MSG_WriteByte(&client->netchan.message, (int)(visible ? 1 : 0));
    Netchan_Transmit(NS_SERVER, &client->netchan);
}

/* Send one reliable transient attention marker without adding snapshot or save state. */
void SV_MinimapPing(edict_t *ent, vec2_t const *position, float duration, color32_t color, uint32_t flags) {
    client_t *client = NULL;

    if (!ent || !position || !isfinite(position->x) || !isfinite(position->y) || !isfinite(duration) ||
        duration <= 0.0f || duration > MINIMAP_PING_DURATION_MAX || flags > 0xff) {
        fprintf(stderr, "SV_MinimapPing: invalid duration=%.3f flags=0x%x\n", duration, (unsigned)flags);
        return;
    }
    client = SV_ClientForEntityRecipient(ent);
    if (!client) {
        fprintf(stderr, "SV_MinimapPing: no recipient for player=%u\n", (unsigned)ent->s.player);
        return;
    }

    MSG_WriteByte(&client->netchan.message, svc_minimap_ping);
    MSG_WriteFloat(&client->netchan.message, position->x);
    MSG_WriteFloat(&client->netchan.message, position->y);
    MSG_WriteFloat(&client->netchan.message, duration);
    MSG_WriteByte(&client->netchan.message, color.r);
    MSG_WriteByte(&client->netchan.message, color.g);
    MSG_WriteByte(&client->netchan.message, color.b);
    MSG_WriteByte(&client->netchan.message, color.a);
    MSG_WriteByte(&client->netchan.message, (int)flags);
    Netchan_Transmit(NS_SERVER, &client->netchan);
}
