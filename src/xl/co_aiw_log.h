/* A research build's log of the engine's nav world ids (KB engine/crash-7dafb4-nav-world-slot, DESIGN-AUDIT d495db9
 * ruling R1): who creates and destroys each id, the id ring and the AI-world slots around every call, and the first
 * steering query that finds its slot empty. It changes no behaviour, and only a CO_RESEARCH build has it. */
#ifndef CO_AIW_LOG_H
#define CO_AIW_LOG_H

#include "co.h"

#ifdef CO_RESEARCH
#define CO_AIW_CREATE 0x78D330u  /* exe: (owner, rdx, r8 AI settings, r9) -> id; interface 0x27CD490 slot 0 */
#define CO_AIW_DESTROY 0x78F000u /* exe: (id); interface slot 1, and called by the exe itself at 3 sites */
#define CO_AIW_HEAD 0x2362884u   /* exe u32: ids handed out; create takes ring[head & 3] */
#define CO_AIW_TAIL 0x190D1F8u   /* exe u32: ids given back; destroy puts its id at ring[tail & 3] */
#define CO_AIW_RING 0x190D460u   /* exe u32[4] */
#define CO_AIW_RECORDS 0x27BA870u /* exe: per id 0xB0 bytes, its id at +0x18 and its flags at +0x1C */
#define CO_AIW_OWNERS 0x27BAB30u /* exe u64[4]: the owner create was given */
#define CO_AIW_SLOTS 0x190D4A0u  /* exe: per id 0x80 bytes, the AI world's pointer at +0 */
#define CO_AIW_QUERY 0x7DAF10u   /* exe: the steering-wall query, (id, query, segments, room) */

/* Hooks the three functions once, with helldivers2.exe's and game.dll's bases. NULL, or why nothing was hooked. */
const char* co_aiw_log_install(uintptr_t exe, uintptr_t game);
#ifdef CO_TEST
void co_aiw_log_test_reset(void);
#endif
#endif

#endif
