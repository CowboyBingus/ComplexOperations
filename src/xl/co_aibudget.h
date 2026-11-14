/* The AI path threads' time a step ([hkaiWorld+0x47C], stock 0.002 s; living_world_aibudget.c writes it). One AI
 * world serves the whole session, so the game's stock value is only readable before this mod's first write: a stock
 * read as the mod's own lowered value made the raise a no-op for a whole second mission. */
#ifndef CO_AIBUDGET_H
#define CO_AIBUDGET_H

#include "co.h"

typedef struct CoAiBudget { uintptr_t world; float stock, wrote; int raised, draining; uintptr_t run; int applied; } CoAiBudget;
/* No game build has had a budget under this, and the mod's lowered one is: a read under it is never the game's. */
#define CO_AIB_STOCK_LEAST 0.001f

/* The bounded drain. One run applies the navmesh changes queued at its start one by one inside the budget, and the
 * cuts and paths wait behind them: 15 minutes at 500 us with tens of thousands of tree cutters removed a minute. A
 * run this old, or this many changes queued behind it, gets `us` a step: 8 ms is half a 60 fps frame at most. */
#define CO_AIB_DRAIN_US 8000
#define CO_AIB_DRAIN_RUN_MS 3000
#define CO_AIB_DRAIN_QUEUE 1024
typedef struct CoAiDrain { int us, runMs, queue; } CoAiDrain;

/* The game's budget for `world`, whose field reads `cur` now, 0 when it is not known: a value this mod wrote, or one
 * under CO_AIB_STOCK_LEAST (another instance of the mod wrote it), is never taken as the game's. */
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
/* The level ended: the world is read anew, the stock known so far stays. */
void co_aibudget_leave(CoAiBudget* b);

#endif
