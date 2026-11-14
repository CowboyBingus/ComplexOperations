/* The trees' navmesh cutters the mod would switch on near players and squads (co_cutters.h hooks the make and the
 * destroy; the engine's switch is 0x812AE0). A tree's cutter added to the AI world costs a queued change, and a
 * remove scans every added cutter (0xBE8D32, 0xC45810): 150 000 added ones stalled navigation for minutes. This
 * keeps one record a pool slot and picks what to add and remove a tick. No game call and no clock of its own. */
#ifndef CO_CUTSET_H
#define CO_CUTSET_H

#include <stddef.h>
#include <stdint.h>

#define CO_CUTSET_CELL 32.0f        /* a grid cell's side in metres: about 30 trees at 0.03 a m2 */
#define CO_CUTSET_GRID 512          /* cells a side, 16 km; a place past it falls in the edge cell */
#define CO_CUTSET_SLOTS (1u << 20)  /* pool slots: the mod's early room, the most that leaves a generation bit */
#define CO_CUTSET_CENTRES 128       /* 8 players and 120 online squads; one inside a player's reach needs none */
#define CO_CUTSET_BATCH 64          /* handles one 0x812AE0 call takes here, as the destroy's batches (0x815D04) */
#define CO_CUTSET_ADDS 512          /* most adds a tick: 2048 a second at 4 Hz against a need of about 1230 */
#define CO_CUTSET_REMOVES 16        /* removes a tick: each scans every added cutter, and one left on is harmless */
#define CO_CUTSET_CAP 48000         /* most cutters switched on: 8 player discs hold about 19 000 */
#define CO_CUTSET_LEAVE_MS 5000     /* a cell out of every centre's reach this long before its cutters go */
#define CO_CUTSET_TICK_MS 250
#define CO_CUTSET_PLAYER_IN 160.0f  /* covers the director's spawns (100 m) and garrisons (150 m) */
#define CO_CUTSET_PLAYER_OUT 224.0f
#define CO_CUTSET_SQUAD_IN 64.0f
#define CO_CUTSET_SQUAD_OUT 96.0f
#define CO_CUTSET_HOLD_MS 30000     /* a player whose Helldiver does not read keeps the last place, as the squads' */

typedef struct CoCutCentre { float x, y, in, out; } CoCutCentre;
typedef struct CoCutRec { uint32_t handle; float x, y; uint32_t next; } CoCutRec;
/* `at`: the last tick a centre's `out` reached the cell; `metres`: the nearest centre to its middle on that tick;
 * `epoch`: the set's epoch when a cutter of the cell last went on. */
typedef struct CoCutCell { uint32_t head, at, pending, on, epoch; uint16_t metres, listed; } CoCutCell;
typedef struct CoCutCand { float d2; uint32_t rec; } CoCutCand;
typedef struct CoCutHeld { uint64_t peer; float x, y; uint32_t at; int used; } CoCutHeld;

typedef struct CoCutSet
{
    CoCutRec* rec; uint8_t* flag; uint32_t* slot; CoCutCell* cell; uint32_t* onCell;
    uint32_t made, freeHead, records, wanted, on, onCells, now; int dirty;
    uintptr_t world, pool; uint32_t mask;
    uint32_t cap, adds, leaveMs;
    uint32_t epoch; /* the caller's count of navmesh runs begun so far, kept current by it */
    uint64_t added, removed, shed, replaced, stale, refused, binds, stranded;
    CoCutCand heap[CO_CUTSET_ADDS];
} CoCutSet;

/* One tick's choice: `add` nearest first, `remove` for 0x812AE0(.., 0). `waiting` wanted ones in reach that were
 * not added (at most the tick's adds), `over` more in reach than one tick weighs, `capped` 1 when the cap held some;
 * `shed` of the removes made room at the cap. */
typedef struct CoCutTick
{
    uint32_t add[CO_CUTSET_ADDS], adds, remove[CO_CUTSET_BATCH], removes, waiting, over, shed; int capped;
} CoCutTick;

/* The memory the set needs (29 MB at these sizes; pages are only touched as slots, records and cells are used). */
size_t co_cutset_bytes(void);
/* `memory` of co_cutset_bytes(), zeroed, for the set's life. */
void co_cutset_init(CoCutSet* s, void* memory);
/* The AI world and the pool the handles belong to, and the pool's index mask ([pool+0x28]). A change drops every
 * record: a handle's generation is one bit at 2^20 slots, so an old record could name a new cutter. The wanted
 * ones not on are counted as stranded: held back by the mod, nothing adds them now. 1 when bound;
 * 0 (nothing recorded) with a null pointer or a mask that is not 2^k - 1 below CO_CUTSET_SLOTS. */
int co_cutset_bind(CoCutSet* s, uintptr_t world, uintptr_t pool, uint32_t mask);
/* Drops every record; the binding stays. */
void co_cutset_clear(CoCutSet* s);
/* 1 when co_cutset_made would take a cutter at this place: bound, and the place is a number. */
int co_cutset_takes(const CoCutSet* s, float x, float y);
/* A tree's cutter was made: `wanted` is the game's own "add now". A record its slot still held is dropped. */
int co_cutset_made(CoCutSet* s, uint32_t handle, float x, float y, int wanted);
/* The game itself added this record's cutter (the mod did not hold the make back): it counts as on, past the cap
 * too. 1 when a waiting record was marked. */
int co_cutset_on(CoCutSet* s, uint32_t handle);
/* Another cutter took this handle's slot: a record still there is of a dead cutter. */
void co_cutset_taken(CoCutSet* s, uint32_t handle);
/* The engine destroys these handles: their records go. A record under another handle of the same slot stays, as the
 * engine's destroy leaves that slot alone (0x812835). Returns the records dropped. */
uint32_t co_cutset_gone(CoCutSet* s, const uint32_t* handles, uint32_t count);
/* The tick. Adds: wanted records within a centre's `in`, nearest first, up to the set's adds and its cap. Removes,
 * at most `removes`: cells out of every centre's `out` for leaveMs, the longest out first; at the cap, a cell whose
 * every record is farther than the record it makes room for, the farthest first. The set counts each as done. */
void co_cutset_tick(CoCutSet* s, const CoCutCentre* centres, uint32_t count, uint32_t nowMs, uint32_t removes, CoCutTick* out);
/* 1 when the navmesh has every cutter within `r` of (x, y) cut in: no wanted record there waits (`waiting` counts
 * them), and a run that began after the last add in a cell the disc touches has ended. `epoch` is the count of runs
 * begun now, `inFlight` whether one is out: a run takes every change queued before it began, so the first to begin
 * after an add holds it, and it has ended once none is out or a second has begun. */
int co_cutset_cut(const CoCutSet* s, float x, float y, float r, uint32_t epoch, int inFlight, uint32_t* waiting);
/* 1 when one of these centres reaches everything `c` does (its `in` disc and its `out` square): `c` would add no
 * cutter and keep none, so a squad beside a player takes no centre. Only the order of adds and sheds can differ. */
int co_cutset_covered(const CoCutCentre* centres, uint32_t count, const CoCutCentre* c);
/* A player's place for this tick, kept in `held` of `slots` entries (zeroed at a level's start). 1: the place read.
 * 2: the Helldiver does not read (dead, waiting, dropping in) and the last place read stands in, for
 * CO_CUTSET_HOLD_MS: the cutters around a dead player stay for whoever is still there. 0: no place. */
int co_cutset_player(CoCutHeld* held, uint32_t slots, uint64_t peer, int reads, float x, float y, uint32_t nowMs, float* place);

#endif
