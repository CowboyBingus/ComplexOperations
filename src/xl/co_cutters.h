/* The engine's navmesh cutters (AI obstacles): one handle pool a physics world, [exe+0x27C6330 + 8 x world], with
 * room for the physics config's +0x4C: 65536 under HellForge, 1048576 with the mod's early room (co_early_sites.h). A forest instance whose type has flag 0x40 gets one
 * (0x814F00 -> 0x812CC0), and its handle is stored as the instance's property 0x2A without a check. With the pool
 * full the allocator 0x75230 gives 0 (0x752F4), so the instance holds 0. Every reader of a handle skips 0 (0x812AE0,
 * 0x8128E0, 0x77F655) except the destroy 0x8127A0: it takes the pool of the batch's first handle (handle >> 30) and
 * reads [pool+0x28], so a batch that starts with 0 reads world 0's empty slot (crash exe+0x812807, read of 0x28).
 * The forest's destroy callback 0x815A10 hands it the obstacle handles in batches of up to 64. */
#ifndef CO_CUTTERS_H
#define CO_CUTTERS_H

#include "co.h"

#define CO_CUTTERS_DESTROY 0x8127A0u /* exe: (rcx handles, edx count), no result */
#define CO_CUTTERS_MAKE 0x812CC0u    /* exe: (rcx description) -> handle, 0 when none was made */
#define CO_CUTTERS_POOLS 0x27C6330u  /* exe: a world's pool, 8 bytes a world (stored at 0x7D8090) */
#define CO_CUTTERS_USED 0x20u        /* pool: handles in use (lock inc 0x75331, lock dec 0x754A6) */
#define CO_CUTTERS_ROOM 0x24u        /* pool: its room (0x7D7F60) */
#define CO_CUTTERS_WORLD 2u          /* the mission's physics world, as the streamer reads its bodies */
#define CO_CUTTERS_BATCH 64u         /* the destroy's batches as 0x815A10 cuts them (0x815CD1) */

/* Copies the handles that are not 0 to `kept`, in order. Returns how many. */
uint32_t co_cutters_keep(const uint32_t* handles, uint32_t count, uint32_t* kept);
/* A world's pool: 1 with its handles in use and its room, 0 when it does not read (no world, a size out of range). */
int co_cutters_pool(uintptr_t exe, uint32_t world, uint32_t* used, uint32_t* room);
/* 1 when the mission world's pool reads and holds at least `share` of its room. */
int co_cutters_over(uintptr_t exe, float share);
/* Hooks the destroy (0 handles left out) and the make (failures counted). NULL, or why nothing was hooked. */
const char* co_cutters_install(uintptr_t exe);
/* Since the install: handles of 0 left out of a destroy, cutters not made, and the pool when the first was not made. */
typedef struct CoCuttersSeen { uint64_t dropped, failed; uint32_t usedAtFail, roomAtFail; } CoCuttersSeen;
void co_cutters_seen(CoCuttersSeen* out);

#ifdef CO_TEST
void co_cutters_test_reset(void);
#endif

#endif
