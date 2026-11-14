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
#include "co_cutset.h"

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


/* The trees' cutters, record only (co_cutset.h): with this armed, on a complex level the make hook records each
 * cutter the forest makes (the call that returns to CO_CUTTERS_FOREST_RETURN, type 3, world 2) and the destroy hook
 * drops it. The description is read, never written: the game adds every cutter as before. */
#define CO_CUTTERS_FOREST_RETURN 0x8155F8u /* exe: after the forest's call of the make (0x8155F3) */
#define CO_CUTTERS_AI_WORLDS 0x190D4A0u    /* exe: a physics world's AI world, 0x80 bytes a world (0x812BB4) */
#define CO_CUTTERS_MASK 0x28u              /* pool: the handle's slot bits (0x7D7F9F, read at 0x812B47) */
#define CO_CUTTERS_DESC_TYPE 0x0u          /* description: 3 is a cutter bound to a physics body (0x812D1B) */
#define CO_CUTTERS_DESC_WORLD 0x4u         /* (0x812CDF) */
#define CO_CUTTERS_DESC_ADD 0xCu           /* "add to the AI world now" (0x8132F8; the forest's store 0x815508) */
#define CO_CUTTERS_DESC_MATRIX 0x10u       /* pointer to a 4x4 matrix; the place is its +0x30, +0x34 (0x8154AB, 0x815521) */

/* Switching (co_cutters_switch): the hook clears the forest's "add now" for the call, so the game makes the cutter
 * and does not add it, and the tick adds the ones in reach through the engine's own switch. */
#define CO_CUTTERS_SWITCH 0x812AE0u   /* exe: (rcx handles, edx count, r8d on); skips 0, a stale handle, one added already */
#define CO_CUTTERS_STEP_TASK 0x20u    /* AI world record: the step task's handle, 0 from the join to the kick (0x7D79F4, 0x7D865A) */
#define CO_CUTTERS_ALIVE_MS 1000u     /* with no tick this long the game adds a new cutter itself, as it always did */

/* Arms the record with `cap` cutters on and `adds` a tick at most. NULL, or why not: the engine's code at the
 * sites above and at the switch 0x812AE0 is not the code this was made for, or no memory. */
const char* co_cutters_record_arm(uintptr_t exe, uint32_t cap, uint32_t adds);
/* A complex level is live, or no longer: leaving drops every record. Returns the records dropped. */
uint32_t co_cutters_live(int complex);
/* 1: makes are held back and the tick switches cutters on (and off, when it is given removes). 0: record only. */
void co_cutters_switch(int on);
typedef struct CoCuttersTick
{
    int bound, hooked, capped;
    uint32_t records, wanted, on, cap, adds, removes, shed, waiting, over;
    uint64_t trees, others, unbound, refused, replaced, stale, destroyed, binds;
    uintptr_t world;
    double us, holdUs; /* the tick, and the part of it under the set's lock (the engine's adds are inside it) */
    /* makes and destroys since the start on the threads other than the tick's, a level's first trees too, and how
     * many such threads (the first 64); records dropped while they waited */
    uint64_t makesOff, destroysOff, stranded; uint32_t threadsOff;
    /* switching: the tick was skipped with the AI step in flight; makes held back, added by the game with no tick
     * alive, added at once because the record was refused, or not added (the AI world or the pool had gone); handles
     * sent to the switch */
    int skipped;
    uint64_t held, stock, atOnce, noWorld, sentOn, sentOff, skips;
} CoCuttersTick;
/* One tick of the set over these centres, game thread: what it would add and remove, counted as done. Record only:
 * nothing is sent to the engine. Switching: sent to CO_CUTTERS_SWITCH in batches, unless the AI step is in flight
 * (then nothing is chosen). 0 when not armed or no complex level is live. */
int co_cutters_tick(const CoCutCentre* centres, uint32_t count, uint32_t nowMs, uint32_t removes, CoCuttersTick* out);

#ifdef CO_TEST
void co_cutters_test_reset(void);
/* The make hook as a call that returns to `from`. */
uint32_t co_cutters_test_make(const void* description, uintptr_t from);
typedef struct CoCuttersPin { uint32_t rva; const char* pin; } CoCuttersPin;
const CoCuttersPin* co_cutters_test_record_pins(size_t* count);
const CoCutSet* co_cutters_test_set(void);
void co_cutters_test_switch(void (*fn)(const uint32_t* handles, uint32_t count, int on));
#endif

#endif
