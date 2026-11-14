/* Unwind data for code the mod writes next to a game function and jumps to from its body (a stub that may call C):
 * without it a stack walk or an exception from the C code finds no entry for the stub, takes it for a leaf and loses
 * the game's frames. Each range unwinds its own prologue codes, then as the game function does at the site. */
#ifndef CO_UNWIND_H
#define CO_UNWIND_H

#include <stddef.h>
#include <stdint.h>

#define CO_UNWIND_PUSH(at, reg) ((uint16_t)((at) | ((0u | ((reg) << 4)) << 8)))                     /* UWOP_PUSH_NONVOL after `at` bytes */
#define CO_UNWIND_ALLOC(at, size) ((uint16_t)((at) | ((2u | ((((size) - 8u) / 8u) << 4)) << 8))) /* UWOP_ALLOC_SMALL, 8..128 */

/* A range of a stub block: offset and length from the block, its prologue `prolog` bytes with `count` codes (the last
 * first, as in a function's UNWIND_INFO). */
typedef struct CoUnwindRange { uint32_t begin, length; uint8_t prolog, count; uint16_t codes[6]; } CoUnwindRange;

/* One function table based at `block`, so it covers the block's stubs only (Proton's lookup stops at the first table
 * whose range holds the address): the ranges, then the game function at `site`. handler: the one exception handler the
 * game's data may name (dropped: its stack-cookie check). space: `bytes` writable bytes in the block after its stubs,
 * readable once it is protected (1024 do). 0 when registered, -2 for another handler, -1 for a range code other than
 * a push or a small allocation or for data that does not fit. The tables stay for the session, as the stubs do. */
int co_unwind_block(uint8_t* block, const CoUnwindRange* ranges, int count, uintptr_t site, uintptr_t handler, uint8_t* space, size_t bytes);

#endif
