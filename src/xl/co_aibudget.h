/* The AI path threads' time a step ([hkaiWorld+0x47C], stock 0.002 s; living_world_aibudget.c writes it). The game's
 * stock value is only readable from a world this mod has not written yet: after a mission the old world still holds
 * the lowered value for some frames, and the next mission's world can take the same address. A stock read as the
 * mod's own value made the raise a no-op for a whole mission. */
#ifndef CO_AIBUDGET_H
#define CO_AIBUDGET_H

#include "co.h"

typedef struct CoAiBudget { uintptr_t world; float stock, wrote; int raised; } CoAiBudget;

/* The game's budget for `world`, whose field reads `cur` now. A value this mod wrote is never taken as the game's. */
float co_aibudget_stock(CoAiBudget* b, uintptr_t world, float cur);
/* The budget for this step: `lowUs`, or the stock one from a wait over `raiseMs` until it is under half of that.
 * The caller writes it and passes what it wrote to co_aibudget_wrote. */
float co_aibudget_want(CoAiBudget* b, float waitS, int lowUs, int raiseMs);
void co_aibudget_wrote(CoAiBudget* b, float value);
/* The level ended: the next world is read anew, the stock known so far stays. */
void co_aibudget_leave(CoAiBudget* b);

#endif
