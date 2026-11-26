# Client Architecture

The client (`client/`) is the presentation layer of OpenWarcraft3. In the normal renderer path it owns the SDL2 window, the OpenGL context, the input system, and the connection to the server. It never runs game logic — all simulation takes place on the server.

## Startup

The client is initialised by `CL_Init` in `cl_main.c`, which:

1. Binds the OpenGL renderer through `R_GetAPI`.
2. Initialises the renderer.
3. Initialises the client-side UI library with `M_GetAPI`.
4. Registers client-side console commands.
5. Installs menu or gameplay key bindings depending on startup mode.

Startup mode is selected in `common/main.c` from the `map` and `connect` cvars:

- no `map` and no `connect` — menu mode, client-side UI only
- `map` set — listen-server mode
- `connect` set — remote-client mode

The initial menu command comes from `ui_start_command`, defaulting to `menu_main`.

## Main Loop

`CL_Frame(DWORD msec)` is called once per rendered frame by the platform layer (`common/main.c`). It performs the following steps in order:

```c
void CL_Frame(DWORD msec) {
    cl.time += msec;
    menu.Refresh(msec);   // 1. update active client-side UI screen
    CL_Input();         // 2. sample keyboard / mouse
    CL_ReadPackets();   // 3. apply incoming server messages
    CL_SendCommand();   // 4. execute commands and forward to server
    CL_PrepRefresh();   // 5. build scene for the renderer
    SCR_UpdateScreen(); // 6. draw world, UI, and console
}
```

### 1. CL_ReadPackets

Drains the loopback or UDP receive buffer and dispatches each message by its `svc_*` opcode:

| Opcode | Handler | Effect |
|--------|---------|--------|
| `svc_spawnbaseline` | `CL_ParseBaseline` | Initialise entity `s` field from delta |
| `svc_packetentities` | `CL_ReadPacketEntities` | Apply per-frame entity delta |
| `svc_configstring` | `CL_ParseConfigString` | Update model, image, and font configstrings |
| `svc_frame` | `CL_ParseFrame` | Receive server frame timing |
| `svc_unit_ui` | `CL_ParseUnitUI` | Forward selected-unit data to the UI library |
| `svc_map_list`, `svc_map_info`, `svc_game_list`, `svc_player_list` | list handlers | Feed menu/list UI screens |

### 2. CL_Input

`CL_Input` pumps SDL events through key bindings and shared pan/look/zoom/selection controls. All game builds
use this one implementation. Direct touchscreen contacts use the same UI and binding
path alongside ordinary mouse and keyboard events; see [SDL2 touch input](touch-input.md). Bindings and independent config options select behavior; there is no RTS/orbit mode.
Controller changes are typed `INPUTCMD` operations: focus XY, orbit Euler/distance, or movement bits/milliseconds.
Game commands such as selection and abilities continue through `clc_stringcmd`.

Shared input sends a game-specific string command only when the selected game's `CL_Game*` hook provides one.
`CL_GameOrderQueueReleaseCommand()` is implemented by Warcraft III and returns `NULL` in WoW and SC2, so the
WC3-only `orderqueuerelease` command is not sent to servers without its handler. See the [WC3 Shift Order Queue](../games/warcraft-3/order-queue.md#input-protocol).

Player-controlled orbit zoom follows the same boundary. `CL_GameCameraZoomPolicy()` lets a game provide an effective
minimum/default/maximum without putting game data rules in `client/cl_input.c`; WC3 uses it for Reforged preferences
and W3I force-zoom flags. Scripted camera state remains game/server owned. See [WC3 Reforged camera zoom](../games/warcraft-3/camera-zoom.md).

SDL window events remain client-owned. Events that can change the OpenGL drawable (`MOVED`, `RESIZED`, `SIZE_CHANGED`, and `DISPLAY_CHANGED` on SDL versions that provide it) call the mandatory renderer `WindowChanged` export. The renderer only marks drawable state dirty at that point; the next `R_BeginFrame` re-queries the drawable once after event pumping has completed. This preserves the client/renderer boundary and avoids steady-state drawable polling.

### 3. CL_SendCommand

Input writes `clc_input` to the netchannel message buffer. `CL_SendCommand` flushes queued commands through
loopback or UDP; `SV_ParseClientMessage` validates input and invokes the mandatory game `ClientInput` export on
the client's assigned player edict. The game owns movement and camera constraints. See [shared input](shared-input.md).

### 4. CL_PrepRefresh

Builds the render scene:
- Sets the camera view origin and angles from `cl.viewangles`.
- Calls `CL_AddEntities` which iterates `cl.entities` and calls `V_AddEntity` for each visible entity. Entities with the `EF_NODRAW` flag are skipped.
- Client-side temporary effects (`client/cl_tent.c`) add short-lived visual-only entities such as blood splats and hit sparks.

`CL_AddEntities` samples the runtime `cl_debug_entities` diagnostic CVar once
at the beginning of each scene entity batch and passes the resulting Boolean
to `V_AddClientEntity`. This avoids repeated named CVar searches for every
entity when diagnostics are normally disabled. A subsequent scene rebuild
resamples the flag, so console toggles still take effect without storing a
long-lived CVar pointer. Model conversion, interpolation, scene order, and the
existing once-per-second paused-construction diagnostic are unchanged. The
`client_entities.debug_mode_samples_once_per_batch_and_tracks_runtime_toggle`
regression covers repeated entity conversion and toggling between batches.

### 5. SCR_UpdateScreen

Calls into the renderer API:

1. `R_BeginFrame` — clear colour/depth, update matrices.
2. `R_RenderFrame` — draw all entities, terrain, water, particles.
3. `ui.DrawFrame` — draw the active client-side UI screen.
4. Console overlay draws debug text.
5. `R_EndFrame` — present the frame.

### WC3 transient ribbon lifetime

MDX ribbon geometry is authored by a model emitter, but its live edge history is
renderer-owned (`trail_t`) and keyed by render-entity number. This follows the
Quake 3 temporary-effect boundary: the game supplies the emitter/material data;
the renderer owns the transient presentation pool and advances it once per view.

When an entity disappears without a death/decay render (for example, an impact
immediately frees its edict), `games/warcraft-3/renderer/mdx/r_mdx_ribbons.c`
copies each live trail into the global detached-ribbon list. The next view ages
and draws those copies through the normal MDX material path, fading edge alpha by
age until the authored ribbon lifespan expires. If the same entity number is
drawn again, its detached copies are discarded before live emission resumes.

The list is presentation-only: it does not keep an edict alive, alter server
state, or own model assets. `MDLX_ForgetRibbonModel` removes both the model's
live ribbon registry entry and detached copies before model release. The
headless renderer tests cover the one-frame detach delay, expiry, reattachment,
and entity-number reuse paths.

## Camera samples

`playerState.viewangles` is the only view orientation on the wire: Euler degrees in `ROTATE_ZYX` order `{pitch, roll, yaw}`. Do not send a parallel quaternion — Euler→quat is lossless, quat→Euler is not.

`CL_ParsePlayerInfo` copies `vieworigin`, `viewangles`, `distance`, `fov`, `znear`, and `zfar` onto `viewDef.camerastate[]`. Clip planes are required camera samples, same as `fov`: every game must author them on `playerState` through `player_set_lens` so FOV and clip cannot be written apart. Zero is a real value, not a keep-previous sentinel. Gameplay defaults live in `CL_GameDefaultCamera` (`WC3_CAMERA_DEFAULT_*`, `WOW_CAMERA_FOV` plus `WOW_WORLD_*_CLIP`, SC2 map camera); Input controls do not invent clip. Every game supplies full focus XYZ, including actor eye height where needed. `Matrix4_getCameraMatrix` converts both snapshots with `Quaternion_fromEuler`, slerps, and builds the orbit view with `Matrix4_fromViewQuat`. Games that previously packed a non-Euler value into a component (SC2 camera height on `z`) must put a real Euler on the snapshot; height belongs in `vieworigin.z`.

Terrain-relative cameras keep exact terrain plus authored offset in each `camerastate` sample. `CL_PredictCameraPosition`
rebases Z by `height(new XY) - height(old XY)` before changing either sample's XY; the same helper handles immediate input and
prediction reapplied to an unacknowledged server snapshot. Changing XY alone used the previous terrain Z as a new authored offset,
which caused a vertical jump when the server acknowledged the move. `playerstate` remains the untouched server sample.

At render time, `Matrix4_getCameraMatrix` extracts both terrain-relative offsets and interpolates those offsets, then adds the
blurred terrain height at the interpolated XY. Using only the latest offset made scripted Z transitions jump at snapshot frequency.
The resulting `viewDef.target` accompanies the projection matrix and is used by drag-plane tracing and shadow focus. The same final
orbit view is inverted once to publish the rendered eye in `viewDef.camerastate[0].eye`; camera-relative effects such as the WC3 sky
model and particles must use that eye rather than the look-at target. `renderer/r_trace.c` owns screen rays and picking. Its drag plane
must use the rendered target, since exact snapshot terrain
can change beneath a camera held steady by the spatial filter. Absolute-height cameras keep ordinary XYZ interpolation.

`viewoffset` and `eyeoffset` are copied onto each `camerastate` sample as well. They are game-evaluated transient offsets
(camera shake): `Matrix4_getCameraMatrix` lerps the two samples, adds them to the look-at target and the derived eye, and
rebuilds the view with a world-up `lookAt`. The client holds no waveform, rate or mode for them, which is the
[game/client boundary](game-client-boundary.md) in practice. Covered by
`client_camera.server_view_offsets_interpolate_onto_target_and_eye` and
`net.playerinfo_view_offsets_roundtrip_into_camera_sample`.

Regression coverage: `net.camera_prediction_preserves_terrain_offsets` exercises repeated predictions and pending/acknowledged
packets; `client_camera.terrain_offsets_interpolate` checks the actual projection and absolute-height path;
`client_camera.rendered_eye_tracks_orbit_distance` locks the derived eye used by camera-relative rendering;
`renderer_view.pan_plane_uses_rendered_target` checks screen-ray intersections across exact-height changes and a moving rendered
plane. Run `make test` for all three, `make test-client-camera` for the focused projection check, or the in-engine command
`build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'client_camera.*' +com_frame_limit 100`.

WoW authors actor Z plus `WOW_CAMERA_EYE_HEIGHT` in its server focus sample; the client does not replace it.
Native camera angles convert at the game boundary; see [coordinates and camera angles](https://github.com/corepunch/open-realm/blob/main/AXIS.md) for the
SC2 azimuth and WoW horizon/heading mappings. The historical `{pitch, roll, yaw}` shorthand describes view Euler
components, not interchangeable native gameplay angles.

## Entity Interpolation

The client keeps four poses per entity, `current`, `prev` and `older[0..1]`, for the last four applied server frames,
and four camera samples, `viewDef.camerastate[0..3]`, newest first. `cl.poses` records their server times, oldest
first (`CL_POSE_SAMPLES`, from `CL_CLOCK_RETAINED`). Each new server frame shifts both sets back once in
`CL_ParseFrame`. A paused server's keepalive repeats its frame number: it refreshes `current` but adds no sample, so the
picture does not settle and then jump when play resumes. An undecodable frame adds no sample either.

The world is drawn at the presentation clock's render time (`client/cl_clock.h`), not at "newest snapshot time plus
time since it arrived". That older rule set `cl.time` to each snapshot's server time on arrival. A late packet froze
units at their last pose, the next one jumped them forward, and two snapshots read in one browser frame skipped a whole
tick. A late packet also moved `cl.time` backwards, which `V_AdvanceSceneTime` treated as a new render epoch, so
particles and global-sequence animation hiccuped as well.

The clock advances with real time and is steered, never snapped, toward the fastest recent delivery. That target is
real time plus the largest `server time - arrival time` offset over the last 16 applied snapshots.

- `cl.time` follows that line. UI timers, client effects and scene animation see a monotonic server-time estimate.
- The render time runs `target_delay` behind it. The delay is the widest recent gap between a snapshot becoming due
  and its arrival (the interval plus its lateness), plus a 16 ms margin for frame quantization. It is capped by the
  three newest intervals, because only four poses are retained: about 200 ms of jitter at the 10 Hz tick, enough for
  a TCP retransmission.

Steering changes the render speed by at most 10%. The render time never passes the newest snapshot; time spent waiting
there accumulates in `clClock_t.waited`. If the render time reaches the newest snapshot on two consecutive arrivals, the
offset window is stale (the server fell behind real time) and is dropped. If several snapshots land together after a
long wait, the render time continues from the oldest retained pose instead of waiting for steering to catch up. A
server time earlier than the newest one, or an error over 500 ms, starts a new timeline at that snapshot.

`V_SelectPosePair` chooses the newest retained pair whose older end the render time has reached, stores its age in
`cl.poses.pair_age`, and sets `viewDef.lerpfrac` from the snapshots' real server-time spacing. A rate-limited link
(`sv_rate`) skips server frames, so intervals can be 200 ms. The old fixed `FRAMETIME` divisor moved units for half of
each interval and froze them for the rest. `Matrix4_getCameraMatrix` and `V_AddClientEntity` use the same pair, so a
camera locked to a unit does not shake against it. An entity whose model changed since the pair's newer pose is drawn
at `current`, rather than old animation frames on the new model.

An entity interpolates when its previous pose belongs to the previously applied frame, however many server frames
apart. The server omits unchanged entities from a delta, so `CL_ReadPacketEntities` stamps every entity in the
snapshot with the frame number, not just those it decoded. Previously a unit that idled for one frame, or any unit
behind a rate-limited link, snapped instead of moving smoothly. When a snapshot is encoded against an older
acknowledged frame, the client restores that base to decode the message. It then recovers `prev` and `older[]` from
the frames applied before, newest first, for models with a matching entity identity. A full recovery snapshot uses
the same pose history. A pose is recovered only if the next newer one was. When no pose history is available, the
client starts an interpolation interval at the newly decoded state.

Skeletal animation samples the same pair. When both poses' animation frames lie in one MDX sequence,
`MDLX_BindBoneMatrices` evaluates the keyframes once at the interpolated animation time (`MDLX_InterpolatedAnimationTime`).
Looping sequences can cross the end of the clip between samples. Frames in different sequences, restarted one-shot
actions, and jumps over `MDLX_MAX_INTERPOLATED_ADVANCE_MSEC` use a pose blend.

With `scr_showfps`, the overlay adds `Frame max` (the longest frame interval), `Buffer` (the current render delay)
and `Wait` (time spent waiting for a late snapshot) over 500 ms windows, so a stutter report can tell an engine hitch
from network starvation.

The replay tests drive scripted arrival times through `CL_ParseServerMessage`, the clock and `V_AddClientEntity`.
`client_clock.jittered_snapshots_move_entities_at_a_steady_rate` covers 0-70 ms jitter at 120 Hz, with a 150 ms
spike delivered together with the next snapshot. `client_clock.rate_limited_snapshots_interpolate_across_skipped_frames`
covers 200 ms intervals. Both require the drawn speed to stay within 15% of the true speed on every frame.
`client_clock.steers_without_reversing_and_recovers_from_a_late_timeline` covers monotonicity, the newest-snapshot
bound, the wait counter and a 300 ms server stall. `client_frames.*` covers idle entities, rebuilt delta bases and
paused keepalives. `client_camera.older_pairs_blend_the_matching_earlier_samples` covers the camera pairs,
`renderer_model.mdx_bone_pose_samples_the_interpolated_time_within_a_sequence` covers animation time, and
`client_screen.frame_stats_report_longest_frame_and_snapshot_waits` covers the overlay.

`client_clock.full_snapshot_keeps_the_displayed_motion_interval` covers recovery after a full snapshot. The
double-click window uses `CL_RealTime()` so a stalled snapshot clock leaves the input timing intact.

Rebuilding a full snapshot or an older acknowledged delta base can remove an entity that appeared in the previous display frame. If the new packet adds it again, `CL_ReadPacketEntities` decodes against the spawn baseline and starts both render poses at the decoded state. A consecutive frame number does not imply that the restored base contains the entity. `client_frames.recovered_snapshot_does_not_animate_building_from_baseline` exercises both recovery paths.

`V_RenderView` keeps a presentation-only scene clock across frames so paused views can submit cached entities with `deltaTime = 0`. Map/session server time can restart from zero while the process and renderer remain alive, so `V_AdvanceSceneTime` treats `now < last` as a new render epoch: it resets `view.time` to `now` and emits zero delta for that frame. The non-active preview path applies the same non-wrapping elapsed-time rule. Never subtract a retained `DWORD` scene timestamp from a newly reset map clock without this epoch check; unsigned wrap otherwise looks like roughly `2^32` milliseconds and can burst particles/animations/effects for one frame. The regression is covered by `net.scene_time_rewind_starts_a_new_render_epoch`.

## Console and HUD

`cl_console.c` maintains an in-game console that can be toggled with the tilde key. `cl_scrn.c` coordinates frame presentation and the UI draw pass.

Server-authored layout controls normally dispatch their `onclick` command on left-button release. A command whose first character is
`+` follows the Quake held-command convention instead: `cl_scrn.c` dispatches it on mouse-down, captures the formatted command, and
sends the same command with a leading `-` on mouse-up even if the pointer has left the frame. This is generic input transport; game
modules may use it for press-and-hold controls without adding game-specific branches to `client/`.

Configstrings are also live server state, not connect-time-only metadata. A game-side `gi.configstring()` call reaches
`PF_Confignstring()`, which delegates storage to the generic server-owned `SV_SetConfigString()`. That helper clears
`sv.syncstrings[index]`; `SV_FindIndex()` uses the same path when it creates model/sound/image entries. `SV_QueuePendingConfigStrings()`
appends each unsynced value to every current client's reliable message before the normal spawned-client datagram. `PF_Unicast()` calls
the same helper before appending an immediate game payload, because layout authoring can allocate an image/font/model index and then
unicast a frame that references it in the same game callback. This guarantees the `svc_configstring` precedes the dependent layout in
the same outbound message instead of leaving a first-use frame temporarily bound to an unregistered slot.
`CL_ParseConfigString` keeps the already-loaded model/image handle when the resent path is unchanged; begin and same-map load
used to `ReleaseModel`/`LoadModel` every slot. A changed or newly filled slot still binds immediately
so late presentation models (after `CL_PrepRefresh`) stay coherent. Do not mutate `sv.configstrings[]` directly for runtime values,
or already-connected clients may retain stale data. Keeping the mutation helper in `server/` also lets standalone server-network
tests cover the real resync implementation without linking the game-import wrapper in `sv_game.c`.

Low fixed configstrings may also carry compact references to resources already owned by a normal precache pool. For example, the
optional generic environment-light slots `CS_TERRAIN_LIGHT_MODEL` and `CS_ENTITY_LIGHT_MODEL` contain decimal `CS_MODELS` indices;
they do not duplicate model paths or create a second loader. The client resolves those references to model handles and copies the
generic `UI_PLAYERSTAT_ENV_PHASE` clock onto `viewDef_t`. It does not include a game header to do that. The active game renderer
evaluates those optional inputs into `ENVIRONLIGHT` samples (`terrainLight` / `entityLight`); draw paths consume the samples, not
the MDX handles. See [Environment Lighting](environment-lighting.md).

`CS_SCENE_FOG` is another low fixed generic presentation contract. Its payload is `style start end density r g b`; style 0 disables
scene-distance fog and positive styles enable it. `client/cl_view.c` parses the payload without game headers and reduces enabled values
to `viewDef_t`'s generic linear `fogStart` / `fogEnd` / `fogColor` fields. Richer producer-owned style/density semantics stay outside
the client until the renderer has a proven generic equation for them. Game renderers decide which world surfaces consume the view
sample; see WC3 [Environmental Terrain Fog](../games/warcraft-3/environmental-fog.md) for the first producer.

Server-authored text frames may also bind to live snapshot values instead of forcing a complete layout resend whenever a number changes.
`playerState.stats[16]` is the generic environment/day-phase clock (`UI_PLAYERSTAT_ENV_PHASE`).
`playerState.stats[18..21]` are reserved generic selection-UI slots (current/max health and current/max mana), serialized as the two
existing packed `NFT_LONG` stat pairs. `UI_STAT_SELECTION_HEALTH_TEXT` and `UI_STAT_SELECTION_MANA_TEXT` make `SCR_GetStringValue()`
format those values at draw time. Game modules may populate these reserved slots for a sole-selected entity; client layout code must keep
the binding generic rather than looking up game-specific entity types or rules.

`playerState.stats[23]` is the generic environment-presentation variant (`UI_PLAYERSTAT_ENV_VARIANT`). `FT_SPRITE` may opt into
`UIFLAG_SPRITE_STAT_SEQUENCE`; in that mode `frame.value` names the secondary `stats[]` slot whose value replaces an explicit `#N`
sequence selector before the ordinary `frame.stat` normalized `@ratio` phase is appended. This keeps variant selection generic and
snapshot-driven: the client does not need to know why a game chose sequence 0 versus sequence 1.

## Connection lifetime and timeout

`CL_CheckTimeout` treats packet age as a signed 32-bit elapsed interval. A
receive timestamp newer than the sampled frame clock is recent, rather than
an unsigned interval of almost 49 days. Positive elapsed time still works
across a 32-bit clock wrap. Real silence exceeding `cl_timeout` must persist
for the existing confirmation frames before disconnecting; receiving a recent
packet resets that confirmation. A published browser failure recorded
`time=904125 last_packet=904146` while the WebSocket was still open. This
establishes the negative-age failure; it does not by itself establish why the
browser's receive timestamp overtook its sampled frame clock.

The network-browser leave import (`Online_Leave`) ends an existing client
session for LAN/UDP and browser bridges as well as EOS. It sends the normal
disconnect packet, closes the client transport, and releases any Internet
reservation. Only an owned Internet host shuts down its local server. An
embedding browser's optional `Module.onSessionEnded` hook receives the same
notification as a terminal disconnect or normal world exit. Menu navigation
remains game-owned: Warcraft III's Cancel and Escape both queue the appropriate
network browser; solo map browsing returns to the solo menu. Starting a match
does not invoke this leave path.

`client_session.recent_packet_ahead_of_the_sampled_clock_is_not_a_timeout`
covers the observed negative age, a recent packet across clock wrap, and true
silence across wrap. `client_session.leaving_a_lan_lobby_sends_disconnect_and_closes_transport`
checks the actual disconnect datagram, transport closure, and repeated leave.
The `menu_fdf.lan_lobby_*_leaves_the_session_after_*` tests drive the Cancel
button and Escape through the command queue, verify the leave import runs after
input dispatch, and reject a late lobby echo in both classic and TFT menus.

## Key Files

| File | Purpose |
|------|---------|
| `client/cl_main.c` | `CL_Frame`, `CL_Init`, `CL_ReadPackets` |
| `client/cl_input.c` | Input sampling, shared `+select` / `zoom` / `group` |
| `client/cl_parse.c` | Server message handlers |
| `client/cl_view.c` | Camera, `CL_PrepRefresh`, `V_AddEntity` |
| `client/cl_tent.c` | Temporary client-side effects |
| `client/cl_scrn.c` | Screen update and UI draw pass |
| `client/cl_console.c` | In-game console |
| `client/keys.c` | Key event dispatch and modifier-aware binding table (`bind SHIFT+1`) |
| `client/cl_control_groups.c` | Numbered control groups on `cl.groups`; `group` / `group add` / `group assign` |
| `common/net.c` | Loopback transport shared by client and server |

## See Also

- [Server-selected effects](server-selected-effects.md) — generic camera samples on the 32-bit player-state mask
- [WC3 cinematics](../games/warcraft-3/cinematics.md) — server Euler lerp, client quat slerp
