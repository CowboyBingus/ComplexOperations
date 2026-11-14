/* The living world as each player sees it. The squads' own notes count what is online; none says how many patrols a
 * player can see, or whether those walk. "Barren" is a statement about exactly that, so every build writes it: per
 * player, the patrol squads with a clear line from the eye, near and out to the draw limit, how many of them move and
 * how many stand by a road, and how often a sighting begins. Where a peer looks is not known: sight is the line alone. */
#ifndef CO_LW_VIEW_H
#define CO_LW_VIEW_H

#include <stddef.h>
#include <stdint.h>

#define CO_LW_VIEW_SQUADS 9216     /* the squad table's size (LW_SQUADS_MAX) */
#define CO_LW_VIEW_PLAYERS 8
#define CO_LW_VIEW_NEAR 300.0f     /* m: a squad nearer is read at a glance; farther it is a few pixels */
#define CO_LW_VIEW_WALK 0.3f       /* m/s over CO_LW_VIEW_WALK_MS: slower is standing (a formation shuffles) */
#define CO_LW_VIEW_WALK_MS 5000u
#define CO_LW_VIEW_STEP_MS 3000u   /* the longest time one sample stands for: a squad not sampled for longer was away */
#define CO_LW_VIEW_GAP_MS 5000u    /* out of a player's sight this long before the next sight of it is a new sighting */
#define CO_LW_VIEW_CALM_MS 10000u  /* a squad calm this long before its next alert is a fight it started */
#define CO_LW_VIEW_STAND_MS 30000u /* a far squad that has not walked for this long has lost its place in the far share */
#define CO_LW_VIEW_FAST 60.0f      /* m/s: a player faster than this is in a hellpod, and has no speed */
#define CO_LW_VIEW_LEAD_FROM 8.0f  /* m/s: faster than a Helldiver runs, so a vehicle */
#define CO_LW_VIEW_LEAD_S 10.0f    /* the spawn band looks this many seconds ahead of a vehicle */
#define CO_LW_VIEW_LEAD_MAX 200.0f /* m, at most: the squads ahead are then inside the offline line before it takes them */

enum { CO_LW_VIEW_UNSEEN, CO_LW_VIEW_SEEN_NEAR, CO_LW_VIEW_SEEN_FAR };

enum { CO_LW_VIEW_FAR_NONE, CO_LW_VIEW_FAR_WALKS, CO_LW_VIEW_FAR_STANDS };

typedef struct CoLwViewSquad
{
    uint64_t lastMs, refMs, walkedMs, calmMs, unseenMs[CO_LW_VIEW_PLAYERS];
    float ref[2]; uint32_t harm;
    uint8_t known, walking, fighting, seen, ever; /* seen, ever: a bit per player */
} CoLwViewSquad;
typedef struct CoLwViewPlayer
{
    double nearMs, farMs, nearUnitMs, farUnitMs, movingMs, roadMs, pathM;
    uint32_t sightings, movingSightings;
    uint64_t lastMs; float last[2], v[2]; uint8_t known;
} CoLwViewPlayer;
typedef struct CoLwViewSums { double onlineMs, unitMs, movingMs, roadMs, farUnitMs, farStandMs; uint32_t fights, fightsHit; } CoLwViewSums;
typedef struct CoLwView
{
    CoLwViewSquad squad[CO_LW_VIEW_SQUADS]; CoLwViewPlayer player[CO_LW_VIEW_PLAYERS]; CoLwViewSums sums;
    uint64_t sinceMs; long level;
} CoLwView;
/* sightline: came online by sightline; patrol: on no errand; fighting: a member at alert 3 or more; harm: its members
 * lost and the health the others miss, which only grows; seen: per player, one of CO_LW_VIEW_UNSEEN / SEEN_NEAR /
 * SEEN_FAR (a clear line within CO_LW_VIEW_NEAR, or from there to the draw limit). */
typedef struct CoLwViewSample
{
    uint32_t index, units, harm; uint64_t nowMs; float pos[2];
    uint8_t road, sightline, patrol, fighting, players, seen[CO_LW_VIEW_PLAYERS];
} CoLwViewSample;

/* A level's start forgets every squad, every player and the sums. */
void co_lw_view_level(CoLwView* v, long level, uint64_t nowMs);
/* Player `slot` at `at` (x, y): its speed and its way go to the sums. A jump (a hellpod, a new Helldiver) is no speed. */
void co_lw_view_player(CoLwView* v, uint32_t slot, const float* at, uint64_t nowMs);
/* Where the spawn band of player `slot` looks: `at` moved ahead along a vehicle's way, into `lead`. 1 when it moved. */
int co_lw_view_lead(const CoLwView* v, uint32_t slot, const float* at, float* lead);
/* The squared distance the band test uses for a squad whose nearest player is sqrt(near2) away and whose nearest lead
 * point sqrt(lead2): inside the band when the lead point has it in the band and no player stands nearer than rMin. */
float co_lw_view_band2(float near2, float lead2, float rMin, float rOn);
/* One online squad, read about once a second. Its time since its last sample goes to the sums. */
void co_lw_view_sample(CoLwView* v, const CoLwViewSample* s);
/* The squad went offline or died: its next sample starts afresh. */
void co_lw_view_gone(CoLwView* v, uint32_t index);
/* How long the squad has stood (not walked CO_LW_VIEW_WALK), in ms; 0 for a squad not sampled yet. */
uint64_t co_lw_view_stood(const CoLwView* v, uint32_t index, uint64_t nowMs);
/* An online squad's place among the far ones: none (not by sightline, or a player has walked up to it: a near squad
 * now), one that walks, or one that has stood CO_LW_VIEW_STAND_MS. */
int co_lw_view_far(int farOn, float dist2, float rOn, uint64_t stoodMs);
/* The units a new far squad is measured against. The walking ones hold the far share; the standing ones have a share
 * of the same size of their own and count here only past it, so far squads never hold more than two shares. */
int co_lw_view_far_sum(int walking, int standing, int limit);
/* A far squad that stands is given up (and may go offline) once it has stood two stand times and the stall recovery
 * has had its turns: its goal sent again, then two new goals (three tries); one without a goal after four stand times. */
int co_lw_view_gave_up(uint64_t stoodMs, uint32_t stallTries, int hasGoal);
/* A scan that tests a few squads a turn starts where the last one stopped, or the first few are all it ever tests:
 * the k-th squad of a turn that starts at `start` (the next turn's start, for k the squads this one looked at). */
uint32_t co_lw_view_scan_at(uint32_t start, uint32_t k, uint32_t count);
/* The host's line for the time since the last note, into `out`. 0 with nothing to say (no time passed). */
int co_lw_view_note(const CoLwView* v, uint64_t nowMs, char* out, size_t size);
/* Player `slot`'s line for the same time. 0 for a player not read in it. */
int co_lw_view_player_note(const CoLwView* v, uint32_t slot, uint64_t nowMs, char* out, size_t size);
/* After the notes: the sums start again. */
void co_lw_view_restart(CoLwView* v, uint64_t nowMs);

/* Whether a patrol with a goal walks: a goal sent does not always move a squad, and a squad at its goal is not always
 * sent on. Per online squad, read about once a second: its way in the note's window and in the last few samples, and
 * the one state it was in at every sample of the window. */
#define CO_LW_MOVE_STILL 9.0f /* m in a 30 s window: under 0.3 m/s is standing */
#define CO_LW_MOVE_EVERY 5    /* samples to a squad's own line */
#define CO_LW_MOVE_NAMED 8    /* squads a list names */
/* Not a calm patrol; calm and on its way to a goal; calm without a goal; calm within the arrival distance of its goal. */
enum { CO_LW_MOVE_OTHER, CO_LW_MOVE_GOAL, CO_LW_MOVE_NO_GOAL, CO_LW_MOVE_THERE };
typedef struct CoLwMove { float last[2], way, way5; uint8_t on, whole, state, n5; } CoLwMove;
int co_lw_move_state(int patrol, int calm, int hasGoal, float left, float arrive);
/* One sample. 1 on every CO_LW_MOVE_EVERY-th, with the way since the last such one in *way5. A squad's first sample
 * (it came online inside the window) makes the window no whole one. */
int co_lw_move_sample(CoLwMove* m, const float* at, int state, float* way5);
/* The squad went offline or died. */
void co_lw_move_gone(CoLwMove* m);
/* The window's line for the first `count` squads into `out`, and the next window begins: each squad in the state of
 * its last sample. */
void co_lw_move_note(CoLwMove* m, uint32_t count, char* out, size_t size);
/* A research switch that is on unless its text begins with 0: `length` as GetEnvironmentVariableA returns it (0 unset,
 * and every build but a research build reads each switch as unset), `fallback` for unset. */
int co_lw_view_switch(const char* text, unsigned long length, int fallback);

#endif
