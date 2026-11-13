/* Where the world streams for a player who is passing through. A reinforcement hellpod sweeps its player sideways at up
 * to 1000 m/s (co_podstart.h), and the new Helldiver first shows up at the pod's start. Read as it is, that path asks
 * for terrain tiles and stamps the player never stands near; each tile build costs the game thread seconds. While a
 * player moves faster than any Helldiver or vehicle, or jumps, the readers keep the last place the player held still;
 * one second calm, and the new place counts. */
#ifndef CO_SETTLE_H
#define CO_SETTLE_H

#include <stdint.h>

#define CO_SETTLE_SLOTS 8      /* the player manager's slots, as EvpPlayers lists them */
#define CO_SETTLE_FAST 60.0f   /* m/s across the ground */
#define CO_SETTLE_JUMP 200.0f  /* m between two readings */
#define CO_SETTLE_CALM_MS 1000u
#define CO_SETTLE_SAMPLE_MS 100u /* the shortest time a speed is read over */
/* Streamed stamps whose nearest player is this far are left behind: removed with the put-back's batch, not one by one. */
#define CO_SETTLE_BEHIND 2000.0f

typedef struct CoSettleSlot { float held[2], last[2]; uint64_t lastMs, calmMs; uint8_t seen, passing, calm; } CoSettleSlot;
typedef struct CoSettle { CoSettleSlot slot[CO_SETTLE_SLOTS]; long level; } CoSettle;

/* A level's start forgets every player (level numbers as g_world.levels counts them). */
void co_settle_level(CoSettle* s, long level);
/* The player in slot `player` read at `at` (x, y) at `nowMs`: the place its readers use, in `use`. Its first reading
 * counts as it is. 1 while the player is passing (`use` is then the place it held). A slot past the table is used as
 * read. */
int co_settle_at(CoSettle* s, uint32_t player, const float* at, uint64_t nowMs, float* use);
/* Removals a stream scan may make: `normal`, or `behind` once a location is CO_SETTLE_BEHIND from every player
 * (`nearest2` the squared distance). */
uint32_t co_settle_removals(float nearest2, uint32_t normal, uint32_t behind);

#endif
