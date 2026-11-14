/* The AI path threads' time a step ([hkaiWorld+0x47C], stock 0.002 s; living_world_aibudget.c writes it). One AI
 * world serves the whole session, so the game's stock value is only readable before this mod's first write: a stock
 * read as the mod's own lowered value made the raise a no-op for a whole second mission. */
#ifndef CO_AIBUDGET_H
#define CO_AIBUDGET_H

#include "co.h"

typedef struct CoAiBudget { uintptr_t world; float stock, wrote; int raised, draining; uintptr_t run; int applied, ran; } CoAiBudget;
/* Whose value co_aibudget_step returned last (`ran`): the join statistics name the budget a step ran with, and the
 * largest of three wins, so the part that asked is not always the one that was written. */
enum { CO_AIB_RAN_LEVER, CO_AIB_RAN_DRAIN, CO_AIB_RAN_MORE };
/* The game's budget is 0.002 (game.dll's constant) and the world's own default 0.003 (exe 0xBB0CCD); the mod's
 * lowered one is under this range and its raised ones over it, so a read outside it is never taken as the game's. */
#define CO_AIB_STOCK_LEAST 0.001f
#define CO_AIB_STOCK_MOST 0.003f

/* The bounded drain. One run applies the navmesh changes queued at its start one by one inside the budget, and the
 * cuts and paths wait behind them: 15 minutes at 500 us with tens of thousands of tree cutters removed a minute. A
 * run this old, or this many changes queued behind it, gets `us` a step: 8 ms is half a 60 fps frame at most. */
#define CO_AIB_DRAIN_US 8000
#define CO_AIB_DRAIN_RUN_MS 3000
#define CO_AIB_DRAIN_QUEUE 1024
typedef struct CoAiDrain { int us, runMs, queue; } CoAiDrain;

/* The game's budget for `world`, whose field reads `cur` now, 0 when it is not known: a value this mod wrote, or one
 * outside CO_AIB_STOCK_LEAST..CO_AIB_STOCK_MOST (another instance of the mod wrote it), is never taken as the game's. */
float co_aibudget_stock(CoAiBudget* b, uintptr_t world, float cur);
/* The budget for this step: `lowUs`, or the stock one from a wait over `raiseMs` until it is under half of that.
 * The caller writes it and passes what it wrote to co_aibudget_wrote. */
float co_aibudget_want(CoAiBudget* b, float waitS, int lowUs, int raiseMs);
void co_aibudget_wrote(CoAiBudget* b, float value);
/* The drain's budget for this step, 0 for none: `inFlight` a run is in flight, `runS` its age, `queued` the changes
 * waiting for the next run (under 0: not read). It lasts until the run is young and the queue under a quarter of
 * `d->queue`, so the next run takes a long queue at the same budget. `d->us` 0 is off. */
float co_aibudget_drain(CoAiBudget* b, const CoAiDrain* d, int inFlight, float runS, int queued);
/* The budget the world's field holds this step: the lever's (the stock one with `leverOn` 0), or the largest of that,
 * the drain's and `moreS`, which is another part's raise for the same step (0 for none); 0 while the stock is not
 * known, and the field is then left alone. The caller writes it when the field differs and passes what it wrote to
 * co_aibudget_wrote. */
float co_aibudget_step(CoAiBudget* b, const CoAiDrain* d, int leverOn, float waitS, int lowUs, int raiseMs, int inFlight, float runS, int queued, float moreS);
/* Navmesh changes the run in flight applied since the last call: `run` is that run (0 for none) and `sum` the changes
 * it has applied so far. A new run counts from its start. */
int co_aibudget_applied(CoAiBudget* b, uintptr_t run, int sum);
/* 1 with `*bytes`, a multiple of 8 and at most `most`, when a stack copy from `rsp` stays inside the thread's stack
 * `limit`..`top`; 0 when `rsp` is not on it. */
int co_aibudget_stack(uintptr_t rsp, uintptr_t limit, uintptr_t top, size_t most, size_t* bytes);
/* The level ended: the world is read anew, the stock known so far stays. */
void co_aibudget_leave(CoAiBudget* b);

#endif
