/* The status-effect volumes keep every flagged unit of the loaded levels in a registry of 4096 (M = [game + 0x347CF20]:
 * a list and a hash over it). At the 4097th unit the game's append stores past the list onto its count (0xACF907) and
 * its insert, finding no free slot, writes through a null one (0xACF966): the game ends. A complex operation's three
 * cities pass 4096, so the list and the hash move to the mod's memory with room for more. The room is the process's:
 * up to 4096 units every level registers, finds and removes the same units in the same order as the game alone. */
#ifndef CO_STATUS_H
#define CO_STATUS_H

#include "co.h"

#define CO_STATUS_LIST 24576u /* under the destruction callbacks' map of 0x8000, which the same loop fills */
#define CO_STATUS_HASH 65536u

/* NULL when the seven sites that use the game's list hold the game's bytes. Reads only. */
const char* co_status_check(uintptr_t game);
/* The list in the mod's memory, with the units the game's list holds now, and a stub at each site. All or none.
 * On the game thread: the units are copied while nothing appends. */
const char* co_status_install(uintptr_t game);
/* On the game thread, every tick: the hash in the mod's memory once the registry exists with the game's own table (its
 * units filed again). 1 when it went in at this call. Nothing before the install. */
int co_status_hash(uintptr_t game);
/* Units left out since the install: a full list or hash. */
uint32_t co_status_skips(void);

/* One site's stub for `code`; `data` holds the list's places (u32), the skips (u32) and the list's address. */
#define CO_STATUS_STUB 0x40u
size_t co_status_stub(uint8_t* out, uintptr_t code, uintptr_t game, int which, uintptr_t data);
/* What the sixth stub calls in place of the game's append and hash insert (0x13CC3F6..0x13CC429): 0 leaves the unit
 * out, where the game would have written past the list or through a null slot. */
int co_status_append(uint64_t manager, uint32_t id, uint64_t unit);

#endif
