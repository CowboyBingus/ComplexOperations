/* A player on or near a road meets the road patrols. An offline squad came online only between the spawn ring and the
 * online radius; one that walked into the ring offline stayed offline and passed the player unseen. Here: the rules
 * that keep a road patrol waiting at the ring's edge until it is online, and what each player has near, as numbers. */
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
#define CO_LW_MEET_PLAYERS 8
#define CO_LW_MEET_RINGS 3          /* 50, 100 and 150 m */

enum { CO_LW_MEET_WAITS, CO_LW_MEET_BACK, CO_LW_MEET_TURNS };

/* 1 when a step from `at` to `to` ends within `edge` of one of the `n` players (x, y, z each) and nearer that player
 * than `at` is: the step goes into the ring. */
int co_lw_meet_enters(const float* at, const float* to, const float (*players)[3], int n, float edge);
/* The first point from `at` toward `back`, CO_LW_MEET_STEP apart and `back` itself last, that lies `edge` or more
 * from every player, into `out`. 0 with none. */
int co_lw_meet_back(const float* at, const float* back, const float (*players)[3], int n, float edge, float* out);
/* What a patrol waiting since `sinceMs` does: it turns back after CO_LW_MEET_WAIT_MS, steps back out while a player
 * is within `ring` of it (`nearest2` the squared distance), and waits otherwise. */
int co_lw_meet_next(uint64_t sinceMs, uint64_t nowMs, float nearest2, float ring);
/* 1 for an online squad that never leaves to make room: a road patrol within `rOn` of a player is what they came for. */
int co_lw_meet_keeps(int road, float near2, float rOn);

typedef struct CoLwMeetPlayer
{
    double ms, roadMs, squadMs, anyMs, nearestMs, unitMs[CO_LW_MEET_RINGS];
    float least; uint64_t lastMs; uint8_t known;
} CoLwMeetPlayer;
typedef struct CoLwMeet { CoLwMeetPlayer player[CO_LW_MEET_PLAYERS]; } CoLwMeet;
/* Player `slot`, read about once a second: `byRoad` whether a road is by them, and the `n` online squads' distances
 * to them, units and whether each is a road squad. The time since the slot's last sample (3 s at most) is weighed. */
void co_lw_meet_sample(CoLwMeet* m, uint32_t slot, uint64_t nowMs, int byRoad, const float* dist, const uint8_t* units, const uint8_t* road, uint32_t n);
/* The slot's line since the last restart, into `out`. 0 for a slot with no time in it. */
int co_lw_meet_note(const CoLwMeet* m, uint32_t slot, char* out, size_t size);
/* After the notes: the sums start again (the last sample's time stays). */
void co_lw_meet_restart(CoLwMeet* m);

#endif
