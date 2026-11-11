/* HellForge's level 11 as the mod finds it in a game: its jumps at the generator's sites and the stubs they lead to,
 * filled the way HellForge's install fills them (native/mod-loader/difficulty_maps.c, _cities.c, _diag.c). */
#ifndef CO_FAKE_STOCK_H
#define CO_FAKE_STOCK_H

#include <stdint.h>

/* Where the stubs lie in the image: one per 0x80 bytes, HellForge's values at +0xF00. */
#define FAKE_STOCK_STUBS 0x3110000u
#define FAKE_STOCK_SPACING 4.0f   /* stand-ins for HellForge's own values: told apart from the mod's */
#define FAKE_STOCK_GRID 5.0f
#define FAKE_STOCK_FACTOR 6.0f
void fake_stock_chains(uint8_t* image, float disk);
uint32_t fake_stock_stub(int chain);

#endif
