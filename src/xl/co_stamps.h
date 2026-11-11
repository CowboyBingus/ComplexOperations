/* The generator's stamp index (0xC04B80: placed stamps, locations and roads by area) per level. HellForge's level 11
 * gives every index 16383 slots; a world of 16384 m needs 131071 and cells' bitsets that hold them. The index is built
 * anew in the same memory at each level, so the memory is made large enough as the game loads and the constructor
 * reads its two counts from the mod: HellForge's on a normal level, to the byte the index stock builds. */
#ifndef CO_STAMPS_H
#define CO_STAMPS_H

#include "co.h"

/* As game.dll is loaded, before a generator exists: the size's bytes a cell, so that the memory every generator gets
 * holds either layout. `check` reads only; `room` is NULL when it went in. HellForge pins and writes other sites. */
const char* co_stamps_room_check(uintptr_t game);
const char* co_stamps_room(uintptr_t game);

/* Once HellForge's level 11 is in: its five writes as its source makes them, the game's bytes between, the room. */
const char* co_stamps_check(uintptr_t game);
/* The constructor's slot count and layout part become two stubs that read the level's counts. All or none. */
const char* co_stamps_install(uintptr_t game);
/* Level init, before the index is rebuilt for the level. Nothing before the install. */
void co_stamps_level(int complex);
/* What the constructor builds now: the slots, and whether they are a complex level's. HellForge's before the install. */
uint32_t co_stamps_slots(void);
int co_stamps_complex(void);

/* The two stubs, for `code`; `counts` is where the slots (+0) and the qwords a cell (+4) lie. */
#define CO_STAMP_SLOTS_STUB 12u
#define CO_STAMP_LAYOUT_STUB 58u
size_t co_stamps_slots_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t counts);
size_t co_stamps_layout_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t counts);

#endif
