/* Instructions the extra large world pins or writes where HellForge's level 11 has a step of its own. HellForge's step
 * is installed first and is never changed: the mod accepts its jump as the game's instruction, leaves a list or a
 * store HellForge already guards to its guard, and puts the stamps' thinning in front of HellForge's merge stub. */
#ifndef CO_BESIDE_H
#define CO_BESIDE_H

#include "co.h"

/* What `size` bytes at `at` are: the game's (`bytes`), HellForge's jump (E9 rel32, 0x90 up to `jump` bytes, the game's
 * bytes behind; `stub` gets where it leads), or neither. Reads only. */
enum { CO_BESIDE_OTHER = -1, CO_BESIDE_GAME, CO_BESIDE_STOCK };
int co_beside_form(uintptr_t at, const uint8_t* bytes, size_t size, size_t jump, uintptr_t* stub);

/* A pattern of hex bytes ("48 8B ..") at game + rva, in which each of HellForge's jumps that lie inside it
 * (co_beside_sites.h) counts as the instruction it replaced. 1 when it matches. */
int co_beside_matches(uintptr_t game, uint32_t rva, const char* pattern);

/* HellForge's stub behind its jump, as its source builds it: the merge's (capacity test, the two instructions, back to
 * 0xF35037), the mine list's (512 places) and the records' store's (a null slot skipped). 1 when `stub` is that one. */
int co_beside_merge(uintptr_t game, uintptr_t stub);
int co_beside_mine(uintptr_t game, uintptr_t stub);
int co_beside_store(uintptr_t game, uintptr_t stub);

/* The last 12 bytes of the thinning's stub, for a stamp it keeps: the merge's two instructions and a jump back to
 * 0xF35037, or with `stub` (HellForge's merge stub) a jump to it, which does both behind its own test. */
#define CO_BESIDE_TAIL 12u
void co_beside_place_tail(uint8_t out[CO_BESIDE_TAIL], uintptr_t code, uintptr_t game, uintptr_t stub);

#endif
