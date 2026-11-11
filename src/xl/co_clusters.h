/* GenerateLocationClusters (op 0x18, 0xF79520) makes at least one cluster per N locations of a kind at two counts.
 * HellForge's level 11 has a stub at each with N = 24 in its code. A complex level spreads an operation's content over
 * 16384 m and takes N = 3, so the mod's stub goes in front of HellForge's jump: it reads the pass's two numbers and
 * hands every pass that has HellForge's 24 on to HellForge's stub, which is neither changed nor written. */
#ifndef CO_CLUSTERS_H
#define CO_CLUSTERS_H

#include "co.h"

/* NULL when both sites hold HellForge's jump to its stub of the form this was made for. `stubs`, when given, gets
 * where each jump leads. Reads only. */
const char* co_clusters_check(uintptr_t game, uintptr_t stubs[2]);
/* One site's stub for `code`: cmp dword [per], 23; je HellForge's stub; the complex level's count, back into the game.
 * `per` is the address of {the divisor - 1, the shift}. Returns its size. */
#define CO_CLUSTER_HEAD 13u
#define CO_CLUSTER_SLOT 0x80u
size_t co_clusters_stub(uint8_t* out, uintptr_t code, uintptr_t game, int which, uintptr_t per, uintptr_t stock);
/* Checks, builds a page of the mod's own and points HellForge's two jumps at it. NULL, or why both are as they were. */
const char* co_clusters_install(uintptr_t game);
/* The cluster pass sets its numbers as it begins and HellForge's again as it ends; the pass's lock is the caller's.
 * Nothing before the install. */
void co_clusters_pass(int complex);
/* The locations a cluster of this pass stands for: 24, or 3 while a complex level's pass runs. */
uint32_t co_clusters_per(void);

#ifdef CO_TEST
void co_clusters_test_reset(void);
const uint8_t* co_clusters_test_page(void);
#endif

#endif
