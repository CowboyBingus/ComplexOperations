/* The structured maps' vertex weld (FinalizeVoronoi, 0xFBBB20) files vertices in a 24 x 24 bucket grid whose blocks
 * hold 16; a full bucket fails the weld and the level starts over. On a world of 16384 m a bucket is 683 m and a city
 * puts about 108 vertices in one. The weld's memory is made anew at every call, so its blocks get room for 256 in
 * every level, and how many a block takes is the level's: 16 on a normal one, HellForge's weld to the vertex. */
#ifndef CO_WELD_H
#define CO_WELD_H

#include "co.h"

/* NULL when the pool's eight values are HellForge's and the eight other sites and the endpoint's entry the game's. */
const char* co_weld_check(uintptr_t game);
/* The pool laid out for 256 a block, the three tests of a full block reading the level's limit, and the endpoint's
 * lookup hooked. All or none. */
const char* co_weld_install(uintptr_t game);
/* Level init: a block's places for this level, and whose lookup finds an endpoint. Nothing before the install. */
void co_weld_level(int complex);
uint32_t co_weld_limit(void);

/* One test's stub for `code`: cmp r8d or edx with the dword at `limit`, the game's branch, the way on. */
#define CO_WELD_STUB 18u
size_t co_weld_full_stub(uint8_t* out, uintptr_t code, uintptr_t game, int which, uintptr_t limit);
/* A complex level's endpoint lookup: the node of the block the vertex lies in, where the game looks in the block of
 * the vertex's snapped position. `original` is the game's, for a vertex outside the pool. */
typedef uint64_t (*CoWeldEndpointFn)(uint64_t weld, uint64_t vertex, uint64_t nodes, uint64_t out);
uint64_t co_weld_endpoint(uint64_t weld, uint64_t vertex, uint64_t nodes, uint64_t out, CoWeldEndpointFn original);

#endif
