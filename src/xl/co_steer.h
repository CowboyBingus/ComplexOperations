/* The steering-wall query on an empty AI world slot (KB engine/crash-7dafb4-nav-world-slot). The engine's query
 * (exe 0x7DAF10, nav API [[game+0x3326330]+0x48]) reads the AI world of a nav id from a table of 4 slots and faults
 * when the slot is empty, a stock state seen at an XL city mission's start. The guard answers no segments there. */
#ifndef CO_STEER_H
#define CO_STEER_H

#include "co.h"

#define CO_STEER_QUERY 0x7DAF10u  /* exe: (id, query, segments, room) -> segments written */
#define CO_STEER_SLOTS 0x190D4A0u /* exe: the AI worlds, one pointer at the start of each 0x80-byte slot */
#define CO_STEER_SLOT_COUNT 4u

/* Hooks the query once. NULL, or why the game's query stays as it is. */
const char* co_steer_install(uintptr_t exe);

#ifdef CO_TEST
void co_steer_test_reset(void);
#endif

#endif
