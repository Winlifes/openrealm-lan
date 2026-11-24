# Multiboard And TextTag JASS Natives

Server-owned registries for DotA-style scoreboards and floating combat/gold text.
Parent tracking: [dota-map-playability.md](dota-map-playability.md) / GitHub #434.

## Contract

| Handle | Registry | Notes |
| --- | --- | --- |
| `multiboard` | `level.multiboards[]` | Title, rows/cols, cells, per-client display/minimize bits |
| `multiboarditem` | `level.multiboard_items[]` | Refcounted view into one cell; `ReleaseItem` frees the view |
| `texttag` | `level.texttags[]` | Text, color, unit/world anchor, velocity, lifespan, visibility |

`MultiboardDisplay` / `MultiboardMinimize` / `SetTextTagVisibility` follow the leaderboard local model: `currentplayer` (GetLocalPlayer context) updates one client slot; a global call updates every connected client mask.

Clearing or destroying a multiboard detaches its outstanding item handles. `MultiboardClear` empties the cell grid and preserves the board title and per-client display state, so a map can rebuild the rows for its next round.

## Implemented DotA Surface

Multiboard: create/destroy, display, minimize/`IsMultiboardMinimized`, title and
title color, row/column count, get/release item, per-item
style/value/color/width/icon, broadcast style/width.

`MultiboardSetTitleTextColor` retains RGBA independently of the label and marks
displayed recipients dirty. `title_color_set` distinguishes an explicit zero
alpha from the default theme color. Both expanded and minimized custom titles
use the retained value; destroying/reallocating a board restores theme defaults.
The 1.27.1.7085 native at `0x6f20eea0` truncates each integer argument to its low
byte, packs ARGB, and calls `0x6f2498f0`; that stores the color at board `+0x44`
and updates the live title through `0x6f3df8d0 -> 0x6f0fdf70`. This is historical
binary evidence, not a claim that the protected supplied 3.0.1 body was decoded.

Custom-board text uses `UI_WriteColoredProxyFrame`, the existing proxy serializer
with explicit color preserved, including alpha zero. `UI_WriteProxyFrame`
retains its older unset-color convenience for other callers. No UI wire field
or generic-client logic changes. The four title regressions drive JASS through
layout capture, including label replacement, minimized state, transparent title
and cell values, byte truncation, destruction/reuse, and save/load.

TextTag (11): create/destroy, text+height, color, `SetTextTagPos` (terrain-relative world coordinates), `PosUnit`, velocity, visibility, permanent, lifespan, fadepoint.

The bounded text-tag pool has 100 handles. Exhaustion returns a typed null handle;
it does not abort the calling script or overwrite a live label. The supplied
3.0.1 Bomber Command script explicitly checks `CreateTextTagLocBJ` results for
null in `CreateTextTagLocForPlayer`, `CreateTextTagLocForFriend` and `AirportUp`.
This supports a recoverable allocation failure, not a claim about Blizzard's
private slot-selection algorithm. Keep the original map unmodified.

`G_RunFrame` calls `G_UpdateTextTags` after script/timer work. It advances the
existing age in seconds by `FRAMETIME / 1000`, then releases nonpermanent tags
at their lifespan, publishing the existing generation-keyed removal. Permanent
labels retain their slots. Freed slots get a new generation on reuse. Previously
the client could hide a label while its server handle leaked indefinitely;
Bomber Command then exhausted the pool during repeated gold/lumber labels.
Age already has the same saved meaning and serialized field; this repair adds
no save field or wire change. The real `RunFrame -> WriteGame -> ReadGame ->
RunFrame` regression checks the remaining lifespan and slot reuse.

Still outstanding: `SetTextTagSuspended` is a validation-only stub, and the
current client-side motion/fade clock does not apply an explicit script age or
restore the elapsed presentation age after load. Do not describe this lifecycle
repair as complete floating-text presentation parity.

`IsMultiboardDisplayed`, `MultiboardSuppressDisplay`, and `MultiboardClear` are registered.

## Presentation Gaps

- **Multiboard HUD:** `hud_multiboard.c` now emits `svc_layout` for a displayed custom board (title, cell text/colors/icons, minimized title) or the automatic Team Resources display. Single-board ownership is per client; showing a custom board replaces the previous custom board. Suppression hides the presentation without changing requested visibility.
- **Retail parity gaps:** stock multiboard FDF skin/geometry, cell width, UI minimize interaction and leaderboard coexistence still need asset-driven verification. Custom boards take precedence over Team Resources, and suppression hides both; see [Team Resources and Advanced Shared Control](team-resources.md). The current panel uses a conservative provisional layout.
- **Local color and byte arguments:** title/cell state is currently shared by the server registry; arbitrary `GetLocalPlayer`-only color overrides still need recipient-specific state. The existing cell/texttag color helper clamps arguments, while the audited historical broadcast-cell native also truncates bytes. Title truncation and explicit zero-alpha layout are covered here; the other native argument policies remain separate gaps.
- **TextTag coordinates:** `SetTextTagPos` stores world X/Y and a terrain-relative height offset and detaches any previous unit anchor. `SetTextTagPosUnit` retains its unit-following behavior; both reuse the existing keyed text-tag presentation and saved state.
- **TextTag draw:** JASS texttags publish keyed `TE_TEXT_TAG` create/update/remove events with resolved text, colour, font, anchor, visibility, motion and lifetime. The generic client updates one active label per texttag handle, follows a unit anchor, and draws it with the same world-text renderer as one-shot `TE_FLOATING_TEXT`. Resource-gain labels remain independent one-shot events ([resource-gain-text.md](resource-gain-text.md)).

Do not widen `entityState_t` / `playerState_t` for either widget.

## Save/Load

Save format version 76 persists multiboards, item views, and texttags (including `texttag.unit` via `F_EDICT` and texttag presentation generations) and JASS handle identity through registry indexes, plus per-client suppression and Team Resources collapse flags.

Native version 168 / browser version 29 add `title_color_set` and `title_color`
to `multiboard_fields`. Older formats are rejected; JASS snapshot stays 16.
Runtime-only frame color selection is rebuilt after loading.

## Verification

```bash
make test-wc3-engine WC3_PATTERN='wc3_api.multiboard*'
make test-wc3-engine WC3_PATTERN='wc3_api.texttag*'
make test-wc3-engine WC3_PATTERN='wc3_api.leaderboard*'
```

Tests live in `games/warcraft-3/game/tests/t_multiboard.c`.

Team Resources is a distinct engine-managed display on `LAYER_GAME_3` (`WC3_LAYER_MULTIBOARD`), separate from the `LAYER_GAME_2` command-error overlay; see [Team Resources and Advanced Shared Control](team-resources.md).
