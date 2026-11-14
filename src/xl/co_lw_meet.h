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
#define CO_LW_MEET_NOTICE 300.0f     /* m beyond that: a road patrol on its way to a player is known, and the far share keeps its room */
/* 1: a player on another machine may look through a 4x scope, so a squad comes online beyond that band (861 m for a
 * 2 m unit) and never where such a player could draw it. 0: such a player is taken to look unscoped beyond the
 * host's band: squads come online nearer and use less of the far share, and a scoped one may see one appear. */
#define CO_LW_MEET_PEER_SCOPED 1
#define CO_LW_MEET_STAY_PACE 1.5f    /* m/s: the walk in from where it came online, for how long it stays */
#define CO_LW_MEET_WATCH_M 300.0f    /* m: a patrol watched walking within this ... */
#define CO_LW_MEET_WATCH_S 5         /* ... for this many seconds running is a sighting a player noticed */
#define CO_LW_MEET_LOOK_MS 1000u     /* a patrol on a player's road: its sight test where it stands */
#define CO_LW_MEET_LOOK_SLOW_MS 2000u /* any other patrol walking toward a player */
#define CO_LW_MEET_COVER_MS 120000u  /* a remembered point this old is forgotten: the players have moved */
#define CO_LW_MEET_COVER_STEPS 6     /* steps back to cover in one approach; then it walks on */
/* Road patrols hold this many sixteenths of their bubble's budget at most, far and near together, by the routes
 * the meet rules opened (a far request on a player's road, a filler's swap): 13 of 72 solo, 4 of 23 at 8 apart, 18
 * of 96 at 8 together. It is a quarter of the 12/16 the near squads have beside the far share, so the landscape
 * near a player keeps three quarters of its units, and its far share stays its own. */
#define CO_LW_MEET_ROAD_16THS 3
#define CO_LW_MEET_FAR_8THS 4        /* far units of every kind hold this many eighths of the budgets at most */
#define CO_LW_MEET_HOLDS 8           /* one hold a bubble at most */
#define CO_LW_MEET_SEEN_GAP 5        /* samples (seconds) out of view before the next sight of a squad is a new sighting */
#define CO_LW_MEET_PLAYERS 8
/* More patrols on a player's road: road patrols are small, and the nearest one out of sight is called along the
 * road past each player there, as often as the road patrols' part of the bubble (co_lw_meet_road_cap) passes them. */
#define CO_LW_MEET_ROAD_MEMBERS 4    /* a road patrol's units: three squads at once on a solo bubble's 13 */
#define CO_LW_MEET_PACE 2.0f         /* m/s: a patrol's walk (the squads' default speed; 1.97 measured online) */
#define CO_LW_MEET_CALL_NEAR 150.0f  /* m: a player this near a road node is on or by that road */
#define CO_LW_MEET_CALL_HOPS 23      /* road nodes out from the player a called patrol may stand: 2.3 km, a plan's 32 nodes with the way past */
/* A called patrol passes the player and walks on: its way ends this many nodes (400 m) past them at least, through
 * any junction, and from there at the road's next junction, CO_LW_MEET_CALL_PAST_MAX nodes past at most. A way that
 * ended by the player would bring every patrol to them to fight. */
#define CO_LW_MEET_CALL_PAST 4
#define CO_LW_MEET_CALL_PAST_MAX 8
#define CO_LW_MEET_CALL_MOVING 1.0f  /* m/s: a player this fast has patrols called from ahead of them */
#define CO_LW_MEET_CALL_LOST 150.0f  /* m farther from its player than it has been: a called patrol is let go */
#define CO_LW_MEET_CALL_MIN_MS 60000u  /* never more than a patrol a minute for one player */
#define CO_LW_MEET_CALL_MAX_MS 1800000u /* and one in half an hour at least: 8 players together share one bubble's part (1261 s each) */
#define CO_LW_MEET_ZONE_WAIT_MS 90000u /* a patrol refused the far share stands beyond the band this long at most, then walks on */
#define CO_LW_MEET_ZONE_ASK_MS 4000u   /* its refusal is this fresh: it asks every 3 s */
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
/* The road patrols' units of a bubble's `budget`: CO_LW_MEET_ROAD_16THS sixteenths. */
int co_lw_meet_road_cap(int budget);
/* The far units of every kind `budget` (all bubbles') allows: CO_LW_MEET_FAR_8THS eighths. */
int co_lw_meet_far_total(int budget);
/* 1 for an online squad that never leaves to make room: a road patrol within `rOn` of a player is what they came
 * for, while its bubble's road patrols (`roadUnits`, itself among them) hold no more than their part of `budget`. */
int co_lw_meet_keeps(int road, float near2, float rOn, int roadUnits, int budget);
/* 1 when `player` (x, y) lies within `width` of the way through the `n` points. */
int co_lw_meet_on_way(const float (*way)[2], int n, const float* player, float width);
/* What a patrol does `d` from its nearest player and `over` beyond what any player could draw of it (negative
 * inside a band), after a look where it stands: `onRoad` on a player's road, `hidden` the look's result, `aheadSeen`
 * its next stretch in a player's view, `cover` a remembered point it may step back to, `measured` its group's size
 * known (a guessed band is no ground to come online in clear sight). Inside `rMin` and from CO_LW_MEET_OUTER
 * beyond the bands it has no rule here. */
int co_lw_meet_act(int onRoad, float d, float rMin, float rOn, float over, int hidden, int aheadSeen, int cover, int measured);
/* How long a squad that came online on its way in is not taken offline for distance: its walk from `keep` to `rOff`. */
uint64_t co_lw_meet_stay_ms(float keep, float rOff);
/* 1 once, when a squad has been watched walking for CO_LW_MEET_WATCH_S samples running: `run` counts them, 255 once
 * told, 0 again when it next comes online. */
int co_lw_meet_watched(uint8_t* run, int seen);

/* The last point of a patrol's way where the near sight test passed, and where in its plan that was. */
typedef struct CoLwMeetCover { float at[2]; uint64_t ms; uint16_t node, next; uint8_t pathAt, known, steps; } CoLwMeetCover;
void co_lw_meet_cover_set(CoLwMeetCover* c, const float* at, uint16_t node, uint16_t next, uint8_t pathAt, uint64_t nowMs);
/* 1 when the squad at `at`, walking `path` from `pathAt`, may step back to the remembered point: known, under
 * CO_LW_MEET_COVER_MS old, on the plan it still walks, `edge` or more from every player, a metre or more behind,
 * and fewer than CO_LW_MEET_COVER_STEPS steps taken. */
int co_lw_meet_cover_ok(const CoLwMeetCover* c, const float* at, const uint16_t* path, uint8_t pathAt, uint8_t pathLen, const float (*players)[3], int n, float edge, uint64_t nowMs);
/* 1 when a road patrol of `members` may have a filler squad leave for it: the bubble's road patrols stay within
 * their part of `budget`. */
int co_lw_meet_road_room(int roadUnits, int members, int budget);
/* The far units a new far squad is measured against: the landscape's by its own rule (walking ones, and standing
 * ones past `limit`), and every far unit of patrols that came online on a player's road (`roadFar`, `roadStand` of
 * them standing). `walking` and `standing` hold the road's too. */
int co_lw_meet_far_sum(int walking, int standing, int roadFar, int roadStand, int limit);
/* 1 when a far squad of `members` has room: `farUnits` far units now (co_lw_meet_far_sum), `roadFar` of them
 * patrols on a player's road, and never more than `total` with it. A patrol on a player's road (`onRoad`) is
 * measured by its bubble's road patrols, `roadHeld` units far and near, against their part `roadLimit`. The others
 * have the far share `limit` to themselves, whatever the road holds. `roadLimit` < 0 (no density rules): all share
 * `limit`. */
int co_lw_meet_far_room(int farUnits, int roadFar, int members, int limit, int onRoad, int roadHeld, int roadLimit, int total);
/* The time between two patrols called to one player's road: a patrol's time online, `way` m from where it comes
 * online past the player to where it goes offline, at its pace and the player's `speed` toward it, over the squads
 * of `members` the bubble's road patrols may hold at once (`cap` units), for each of the `sharers` players of that
 * bubble on a road. Faster, the bubble would refuse them. */
uint32_t co_lw_meet_call_ms(int cap, int members, int sharers, float way, float speed);
/* The side of the road a moving player's patrol is called from: of the `n` directions `dirs` out of the player's
 * road node, the one most along the velocity `v` (m/s). -1 for a player slower than CO_LW_MEET_CALL_MOVING (the
 * sides then take turns). `only` is set when the player is as fast as a patrol walks: one called from another side
 * would never catch up. */
int co_lw_meet_call_ahead(const float* v, const float (*dirs)[2], int n, int* only);
/* 1 while a called patrol's way goes on beyond the `past` nodes it has past its player: `fork` when the road ahead
 * is a junction or an end (no single way on). */
int co_lw_meet_call_on(int past, int fork);
/* At a junction the way past goes straight on: of the `n` directions `dirs`, the one most along `in`. -1 with none
 * ahead (a turn back is no way on). */
int co_lw_meet_call_straight(const float* in, const float (*dirs)[2], int n);
/* 1 when a called patrol `d` m from its player has lost them: `least`, the nearest it has been since the call (0 at
 * the call), is CO_LW_MEET_CALL_LOST behind it. The player walks away faster than it follows, or it has passed. */
int co_lw_meet_call_lost(float d, float* least);
/* How long player `slot` of `players` waits for its first call: its part of the period `periodMs`, so the players'
 * calls fall evenly over it. */
uint32_t co_lw_meet_call_first(uint32_t periodMs, int slot, int players);
/* 1 when a patrol `hops` road nodes from the player, on the wanted side of them or not, is the better one to call
 * than the best so far (`bestHops` < 0: none): the wanted side first, then the nearer. */
int co_lw_meet_call_better(int hops, int onSide, int bestHops, int bestOnSide);
/* 1 while a patrol beyond the band, refused for the far share at `refusedMs`, stands where it is: its refusal is
 * fresh and it has stood less than CO_LW_MEET_ZONE_WAIT_MS since `sinceMs`. */
int co_lw_meet_zone_waits(uint64_t sinceMs, uint64_t refusedMs, uint64_t nowMs);
/* A road patrol's units of a group of `size`. */
int co_lw_meet_road_members(int size);
/* The beside command's point: the first of the `n` candidates (nearest first) with a clear line to the road and no
 * enemy near. -1 with none; `least` then gets the clear one with the fewest enemies, -1 when none is clear. */
int co_lw_meet_beside(const uint8_t* clear, const uint16_t* foes, int n, int* least);
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
