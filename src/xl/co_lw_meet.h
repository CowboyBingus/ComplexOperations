/* A player on or near a road sees the patrols walk it. An offline squad came online only where it stood unseen, so
 * one on an open road walked up to the player offline and passed unseen. Here: the rules by which a patrol on its
 * way to a player comes online at the last point of its way that was out of sight, or beyond what is drawn, and
 * walks into view; the wait at the ring's edge for one that could not; and what each player has near, as numbers. */
#ifndef CO_LW_MEET_H
#define CO_LW_MEET_H

#include <stddef.h>
#include <stdint.h>

#define CO_LW_MEET_EDGE 5.0f        /* m outside the ring: where a road patrol waits (as an event squad does) */
#define CO_LW_MEET_WAIT_MS 60000u   /* waiting this long without coming online, it turns back (D) */
#define CO_LW_MEET_SWAP_MS 5000u    /* at most one filler squad leaves for a waiting road patrol in this time */
#define CO_LW_MEET_CLOSE 100.0f     /* m: an offline road patrol this near a player is counted as missed */
#define CO_LW_MEET_ROAD 250.0f      /* m: road squads online within this of a player are "near" in the notes */
#define CO_LW_MEET_STEP 4.0f        /* m between the points tried on the way back out */
#define CO_LW_MEET_WAY 80.0f         /* m: a player this near a road patrol's way stands on its road (D) */
#define CO_LW_MEET_OUTER 100.0f      /* m beyond the draw band: where a patrol on an open road comes online */
#define CO_LW_MEET_LOOK_MS 1000u     /* a patrol on a player's road: its sight test where it stands */
#define CO_LW_MEET_LOOK_SLOW_MS 2000u /* any other patrol walking toward a player */
#define CO_LW_MEET_COVER_MS 120000u  /* a remembered point this old is forgotten: the players have moved */
#define CO_LW_MEET_COVER_STEPS 6     /* steps back to cover in one approach; then it walks on */
#define CO_LW_MEET_STAY_MS 300000u   /* a squad that came online on its way in is not taken offline for distance this long */
#define CO_LW_MEET_ROAD_SHARE 2      /* road patrols take a bubble's budget / this by swap, and are kept up to it */
#define CO_LW_MEET_FAR_RESERVE 2     /* the far share / this is kept for patrols on a player's road */
#define CO_LW_MEET_HOLDS 8           /* one hold a bubble at most */
#define CO_LW_MEET_SEEN_GAP 5        /* samples (seconds) out of view before the next sight of a squad is a new sighting */
#define CO_LW_MEET_PLAYERS 8
#define CO_LW_MEET_RINGS 3          /* 50, 100 and 150 m */

enum { CO_LW_MEET_WAITS, CO_LW_MEET_BACK, CO_LW_MEET_TURNS };
/* How a patrol on its way in asks: by the rules it had; where it stands, by the near test; at the remembered point
 * it has stepped back to; where it stands beyond the draw band. */
enum { CO_LW_MEET_WALK, CO_LW_MEET_ASK, CO_LW_MEET_ASK_COVER, CO_LW_MEET_ASK_BEYOND };

/* 1 when a step from `at` to `to` ends within `edge` of one of the `n` players (x, y, z each) and nearer that player
 * than `at` is: the step goes into the ring. */
int co_lw_meet_enters(const float* at, const float* to, const float (*players)[3], int n, float edge);
/* The first point from `at` toward `back`, CO_LW_MEET_STEP apart and `back` itself last, that lies `edge` or more
 * from every player, into `out`. 0 with none. */
int co_lw_meet_back(const float* at, const float* back, const float (*players)[3], int n, float edge, float* out);
/* What a patrol waiting since `sinceMs` does: it turns back after CO_LW_MEET_WAIT_MS, steps back out while a player
 * is within `ring` of it (`nearest2` the squared distance), and waits otherwise. */
int co_lw_meet_next(uint64_t sinceMs, uint64_t nowMs, float nearest2, float ring);
/* 1 for an online squad that never leaves to make room: a road patrol within `rOn` of a player is what they came
 * for, while its bubble's road patrols (`roadUnits`, itself among them) hold no more than their share of `budget`. */
int co_lw_meet_keeps(int road, float near2, float rOn, int roadUnits, int budget);
/* 1 when `player` (x, y) lies within `width` of the way through the `n` points. */
int co_lw_meet_on_way(const float (*way)[2], int n, const float* player, float width);
/* What a patrol does `d` from its nearest player, after a look where it stands: `onRoad` on a player's road,
 * `hidden` the look's result, `aheadSeen` its next stretch in a player's view, `cover` a remembered point it may
 * step back to. Inside `rMin` and beyond `band` + CO_LW_MEET_OUTER it has no rule here. */
int co_lw_meet_act(int onRoad, float d, float rMin, float rOn, float band, int hidden, int aheadSeen, int cover);

/* The last point of a patrol's way where the near sight test passed, and where in its plan that was. */
typedef struct CoLwMeetCover { float at[2]; uint64_t ms; uint16_t node, next; uint8_t pathAt, known, steps; } CoLwMeetCover;
void co_lw_meet_cover_set(CoLwMeetCover* c, const float* at, uint16_t node, uint16_t next, uint8_t pathAt, uint64_t nowMs);
/* 1 when the squad at `at`, walking `path` from `pathAt`, may step back to the remembered point: known, under
 * CO_LW_MEET_COVER_MS old, on the plan it still walks, `edge` or more from every player, a metre or more behind,
 * and fewer than CO_LW_MEET_COVER_STEPS steps taken. */
int co_lw_meet_cover_ok(const CoLwMeetCover* c, const float* at, const uint16_t* path, uint8_t pathAt, uint8_t pathLen, const float (*players)[3], int n, float edge, uint64_t nowMs);
/* 1 when a road patrol of `members` may have a filler squad leave for it: the bubble's road patrols stay within
 * their share of `budget`. */
int co_lw_meet_road_room(int roadUnits, int members, int budget);
/* 1 when a far squad of `members` fits the far share `limit`: `farUnits` far units now, `roadFar` of them patrols on
 * a player's road. With `reserve` the others leave limit / CO_LW_MEET_FAR_RESERVE to those patrols. */
int co_lw_meet_far_room(int farUnits, int roadFar, int members, int limit, int onRoad, int reserve);
/* The travel command's step for `dt` seconds at `v` m/s: a tick of 0.1 s at most, 2 m at most, so the hellpod rule
 * (co_settle.h) reads it as the speed it stands for. */
float co_lw_meet_travel_step(float v, float dt);

/* Room held for patrols, one squad a bubble: `bubble` is each hold's bubble now (-1 for none). */
typedef struct CoLwMeetHolds { int idx[CO_LW_MEET_HOLDS]; uint32_t units[CO_LW_MEET_HOLDS]; uint64_t until[CO_LW_MEET_HOLDS]; } CoLwMeetHolds;
/* The units held against squad `requester` in `bubble`: the running holds of that bubble but its own. */
uint32_t co_lw_meet_held(const CoLwMeetHolds* h, const int* bubble, int requester, int inBubble, uint64_t nowMs);
/* A hold for squad `idx`: 0 when `inBubble` has a running hold or the table is full. */
int co_lw_meet_hold(CoLwMeetHolds* h, const int* bubble, int idx, uint32_t units, int inBubble, uint64_t nowMs, uint64_t forMs);
/* The squad's hold ends (it came online, or its intent changed). */
void co_lw_meet_hold_end(CoLwMeetHolds* h, int idx);

typedef struct CoLwMeetPlayer
{
    double ms, roadMs, squadMs, anyMs, nearestMs, unitMs[CO_LW_MEET_RINGS];
    float least; uint64_t lastMs; uint8_t known;
    uint32_t seenN, roadSeen, roadAny, roadNew, artNew, otherSeen, otherNew, angleN; double angle; /* co_lw_meet_seen */
} CoLwMeetPlayer;
typedef struct CoLwMeet { CoLwMeetPlayer player[CO_LW_MEET_PLAYERS]; } CoLwMeet;
/* Player `slot`, read about once a second: `byRoad` whether a road is by them, and the `n` online squads' distances
 * to them, units and whether each is a road squad. The time since the slot's last sample (3 s at most) is weighed. */
void co_lw_meet_sample(CoLwMeet* m, uint32_t slot, uint64_t nowMs, int byRoad, const float* dist, const uint8_t* units, const uint8_t* road, uint32_t n);
/* What player `slot` sees, read about once a second: patrols online with a clear line, on screen where the view is
 * known, and walking. `road` / `other`: such road patrols (by squad kind) and other patrols now; `roadNew`, `artNew`
 * (the arterials' among roadNew), `otherNew`: those not seen for CO_LW_MEET_SEEN_GAP samples before; `angle`: degrees
 * between the view and the nearest road's direction, negative when not by a road or the view is not known. */
void co_lw_meet_seen(CoLwMeet* m, uint32_t slot, uint32_t road, uint32_t roadNew, uint32_t artNew, uint32_t other, uint32_t otherNew, float angle);
/* 1 when a squad seen now is a new sighting: `gap` its samples out of view, counted on here (0 while seen). */
int co_lw_meet_sighting(uint8_t* gap, int seen);
/* The slot's line of what it saw, into `out`. 0 for a slot with no sample. */
int co_lw_meet_seen_note(const CoLwMeet* m, uint32_t slot, char* out, size_t size);
/* The slot's line since the last restart, into `out`. 0 for a slot with no time in it. */
int co_lw_meet_note(const CoLwMeet* m, uint32_t slot, char* out, size_t size);
/* After the notes: the sums start again (the last sample's time stays). */
void co_lw_meet_restart(CoLwMeet* m);

#endif
