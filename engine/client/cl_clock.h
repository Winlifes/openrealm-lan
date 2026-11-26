/* Presentation clocks for snapshot interpolation.
 *
 * Snapshots leave the server on a fixed tick but reach the client with variable delay (browser WebSocket relay,
 * Wi-Fi, a slow server frame) and, under a rate limit, at variable intervals. Rendering at "newest snapshot time plus
 * time since it arrived" made every late packet a stall and every early or bunched one a forward jump.
 *
 * Both clocks here advance with real time and are steered, never snapped, toward the fastest recent delivery: real
 * time plus the largest (server time - arrival time) offset in a window of applied snapshots.
 *  - time is the client's view of the server clock (cl.time): UI timers, effects and scene animation.
 *  - render is the server time the world is drawn at. It runs `target_delay` behind that line. The delay covers
 *    the usual gap between a snapshot becoming due and its arrival, so ordinary jitter lands inside the buffered
 *    interval. A snapshot later than that lets render run up to CL_CLOCK_EXTRAPOLATE_MSEC past the newest one,
 *    where entities continue along their last motion, rather than freezing the picture. Covering every late
 *    packet instead made Wi-Fi play lag a quarter of a second behind the player's commands.
 * Steering changes speed by at most 10%, and neither clock runs backwards except when a new timeline starts (first
 * snapshot, map change, an error too large to steer out).
 *
 * Pure state and arithmetic: the client feeds it arrival times and reads back times, so tests can replay any
 * arrival pattern deterministically. */
#ifndef cl_clock_h
#define cl_clock_h

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define CL_CLOCK_WINDOW 32               /* recent applied snapshots that estimate offset and delay: ~3 s at 10 Hz */
#define CL_CLOCK_RESET_MSEC 500.0        /* a larger error starts a new timeline instead of steering */
#define CL_CLOCK_SLEW_MSEC 1000.0        /* an error is steered out over roughly this much real time */
#define CL_CLOCK_MAX_SLEW 0.1            /* steering runs a clock at 90..110% of real time */
#define CL_CLOCK_DELAY_MARGIN_MSEC 16.0  /* render delay beyond the measured need: one 60 Hz frame of quantization */
#define CL_CLOCK_MAX_DELAY_MSEC 500.0    /* never buffer more than this, however bad the link */
#define CL_CLOCK_STARVED_SNAPSHOTS 2     /* consecutive starved arrivals mean the offset window is stale */
#define CL_CLOCK_RETAINED 4              /* snapshot poses the client keeps: about 200 ms of jitter at 10 Hz */
#define CL_CLOCK_EXTRAPOLATE_MSEC 150.0  /* render may run this far past the newest snapshot before it waits */
#define CL_CLOCK_COVERED_SNAPSHOTS 8     /* the latest-arriving quarter of the window may need extrapolation */

typedef struct {
    bool valid;            /* seeded by a snapshot; until then time simply follows real time */
    bool held;             /* render reached the extrapolation limit since the last arrival */
    double time;           /* client view of the server clock; cl.time is its floor, ms */
    double render;         /* server time the world is drawn at, ms */
    double target_delay;   /* how far render aims to run behind the fastest-delivery line, ms */
    double waited;         /* render time lost waiting at the newest snapshot since the timeline began, ms */
    double extrapolated;   /* render time drawn past the newest snapshot since the timeline began, ms */
    uint32_t retained[CL_CLOCK_RETAINED]; /* server times of the retained snapshots, newest last */
    uint32_t retained_count;
    uint32_t count, next;  /* filled window entries and the slot the next arrival replaces */
    uint32_t starved;      /* consecutive arrivals that found render waiting */
    int64_t offset[CL_CLOCK_WINDOW];    /* server time minus client real time when each snapshot was applied */
    uint32_t interval[CL_CLOCK_WINDOW]; /* server time since the snapshot before it */
} clClock_t;

static inline uint32_t CL_ClockLatest(clClock_t const *clock) {
    return clock->retained[CL_CLOCK_RETAINED - 1];
}

static inline int64_t CL_ClockMaxOffset(clClock_t const *clock) {
    int64_t best = clock->offset[0];
    for (uint32_t i = 1; i < clock->count; i++) if (clock->offset[i] > best) best = clock->offset[i];
    return best;
}

/* Each snapshot was due when the fastest-delivery line reached the previous snapshot's time and arrived
 * `interval + lateness` after that. The render delay covers all but the CL_CLOCK_COVERED_SNAPSHOTS largest such
 * needs in the window, and the largest one less what extrapolation may cover, so a late snapshot never freezes
 * the picture and periodic Wi-Fi spikes do not set the delay for everyone. */
static inline double CL_ClockNeededDelay(clClock_t const *clock, uint32_t default_interval) {
    int64_t const best = CL_ClockMaxOffset(clock);
    double need[CL_CLOCK_WINDOW];
    uint32_t skip;
    if (!clock->count) return default_interval;
    for (uint32_t i = 0; i < clock->count; i++) {
        double const value = (double)clock->interval[i] + (double)(best - clock->offset[i]);
        uint32_t k = i;
        for (; k > 0 && need[k - 1] < value; k--) need[k] = need[k - 1]; /* largest first */
        need[k] = value;
    }
    skip = clock->count * CL_CLOCK_COVERED_SNAPSHOTS / CL_CLOCK_WINDOW;
    return fmax(need[skip], need[0] - CL_CLOCK_EXTRAPOLATE_MSEC);
}

/* Render must stay within the intervals between the retained poses or it falls behind the oldest one. Use the
 * shortest run of that many consecutive intervals in the window; jitter beyond it waits at the newest snapshot. */
static inline double CL_ClockBufferedSpan(clClock_t const *clock, uint32_t default_interval) {
    uint32_t const run = CL_CLOCK_RETAINED - 1;
    double span = (double)run * default_interval;
    bool found = false;
    for (uint32_t end = 1; end + run <= clock->count + 1; end++) {
        double sum = 0.0;
        for (uint32_t k = 0; k < run; k++)
            sum += clock->interval[(clock->next + CL_CLOCK_WINDOW - end - k) % CL_CLOCK_WINDOW];
        if (!found || sum < span) span = sum, found = true;
    }
    return span;
}

static inline void CL_ClockRecord(clClock_t *clock, uint32_t servertime, uint32_t realtime, uint32_t interval,
                                  uint32_t default_interval) {
    double delay;
    clock->offset[clock->next] = (int64_t)servertime - (int64_t)realtime;
    clock->interval[clock->next] = interval;
    clock->next = (clock->next + 1) % CL_CLOCK_WINDOW;
    if (clock->count < CL_CLOCK_WINDOW) clock->count++;
    delay = CL_ClockNeededDelay(clock, default_interval) + CL_CLOCK_DELAY_MARGIN_MSEC;
    delay = fmin(delay, CL_ClockBufferedSpan(clock, default_interval));
    clock->target_delay = fmin(delay, CL_CLOCK_MAX_DELAY_MSEC);
}

static inline void CL_ClockRetain(clClock_t *clock, uint32_t servertime) {
    for (uint32_t i = 0; i + 1 < CL_CLOCK_RETAINED; i++) clock->retained[i] = clock->retained[i + 1];
    clock->retained[CL_CLOCK_RETAINED - 1] = servertime;
    if (clock->retained_count < CL_CLOCK_RETAINED) clock->retained_count++;
}

/* Start a timeline at this snapshot: the presentation time is its server time, as it was before smoothing. */
static inline void CL_ClockReset(clClock_t *clock, uint32_t servertime, uint32_t realtime, uint32_t default_interval) {
    memset(clock, 0, sizeof(*clock));
    clock->valid = true;
    clock->time = servertime;
    CL_ClockRetain(clock, servertime);
    CL_ClockRecord(clock, servertime, realtime, default_interval, default_interval);
    clock->render = (double)servertime - clock->target_delay;
}

/* Record a newly applied server frame. A repeated server time (a paused server's keepalive frame) carries no
 * timing information; an earlier one is a new timeline. */
static inline void CL_ClockSnapshot(clClock_t *clock, uint32_t servertime, uint32_t realtime, uint32_t default_interval) {
    uint32_t const latest = CL_ClockLatest(clock);
    if (!clock->valid || servertime < latest) {
        CL_ClockReset(clock, servertime, realtime, default_interval);
        return;
    }
    if (servertime == latest) return;
    clock->starved = clock->held ? clock->starved + 1 : 0;
    clock->held = false;
    /* Repeatedly waiting at the newest snapshot means arrivals now run later than the window's fastest one, e.g.
     * after the server fell behind real time. Forget the old arrivals instead of waiting for them to age out. */
    if (clock->starved >= CL_CLOCK_STARVED_SNAPSHOTS) clock->count = clock->next = clock->starved = 0;
    CL_ClockRetain(clock, servertime);
    CL_ClockRecord(clock, servertime, realtime, servertime - latest, default_interval);
    if (fabs((double)realtime + (double)CL_ClockMaxOffset(clock) - clock->time) > CL_CLOCK_RESET_MSEC) {
        CL_ClockReset(clock, servertime, realtime, default_interval);
        return;
    }
    /* After a long wait several snapshots can land together. The pose render was waiting on is no longer retained:
     * continue from the oldest one that is instead of freezing until steering catches up. */
    if (clock->retained_count == CL_CLOCK_RETAINED && clock->render < clock->retained[0])
        clock->render = clock->retained[0];
}

static inline double CL_ClockSteer(double clock, double target, uint32_t msec) {
    double const slew = fmax(-CL_CLOCK_MAX_SLEW, fmin(CL_CLOCK_MAX_SLEW, (target - clock) / CL_CLOCK_SLEW_MSEC));
    return clock + msec * (1.0 + slew);
}

/* Advance by one client frame of `msec` real milliseconds ending at `realtime`; returns the new cl.time.
 * Before the first snapshot the caller's time simply advances. */
static inline uint32_t CL_ClockAdvance(clClock_t *clock, uint32_t time, uint32_t msec, uint32_t realtime) {
    double line, next;
    uint32_t latest;
    if (!clock->valid) return time + msec;
    latest = CL_ClockLatest(clock);
    line = (double)realtime + (double)CL_ClockMaxOffset(clock);
    /* The server clock estimate may lead the newest snapshot by up to the buffered delay; a paused or silent server
     * stops it there, as it stops the world. */
    next = fmin(CL_ClockSteer(clock->time, line, msec), (double)latest + clock->target_delay);
    clock->time = fmax(clock->time, next);
    next = CL_ClockSteer(clock->render, line - clock->target_delay, msec);
    if (next >= latest + CL_CLOCK_EXTRAPOLATE_MSEC) {
        double const held = fmax(clock->render, (double)latest + CL_CLOCK_EXTRAPOLATE_MSEC);
        clock->waited += next - held;
        next = held;
        clock->held = true;
    }
    if (next > latest) clock->extrapolated += next - fmax(clock->render, (double)latest);
    clock->render = next;
    return (uint32_t)clock->time;
}

/* Server time the world is drawn at: interpolate the retained snapshots around this. It may lie up to
 * CL_CLOCK_EXTRAPOLATE_MSEC past the newest snapshot. */
static inline double CL_ClockRenderTime(clClock_t const *clock) {
    return clock->render;
}

#endif /* cl_clock_h */
