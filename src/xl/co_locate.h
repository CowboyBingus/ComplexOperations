/* A loot container's location. The game finds it by the container's unit in the level's location array (the leaf
 * 0xF73E50, from the loot component's set-up 0x9D2B70), and a point of interest counts its loot by that index.
 * HellForge moves the array in the generation block and leaves this one instruction at the game's place, where the
 * block is empty: every container gets no location. A complex mission's containers are looked up where the array is;
 * a normal mission's keep HellForge's answer. */
#ifndef CO_LOCATE_H
#define CO_LOCATE_H

#include "co.h"

/* NULL when the site holds the game's instruction, or the game's with the array's offset as HellForge reports it (a
 * HellForge that moves the site itself). Reads only. */
const char* co_locate_check(uintptr_t game);
/* The game's instruction becomes a jump to a stub that reads the offset of the mission in play. Nothing is written
 * where HellForge moved the site itself. NULL, or why; the site is as it was after a failure. */
const char* co_locate_install(uintptr_t game);
/* The mission in play: 1 a complex one, 0 any other and the ship. Nothing before the install. */
void co_locate_live(int complex);
/* The array's offset in the block for a mission of this kind. */
uint32_t co_locate_at(int complex);

/* The stub for `code`: mov ecx, [at]; lea rcx, [rcx+r10]; jmp back. */
#define CO_LOCATE_STUB 15u
size_t co_locate_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t at);

#endif
