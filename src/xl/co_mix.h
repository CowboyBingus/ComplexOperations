/* Which mission rows HellForge's level 11 mixes a partner mission into ("two missions in one"). HellForge keeps that
 * table to itself, so the mod works it out again by HellForge's own rules (its MixInstall) from the game's tables as
 * they stand after its install: an operation's missions on one map repeat a mixing row's primaries another way. */
#ifndef CO_MIX_H
#define CO_MIX_H

#include "co.h"

#define CO_MIX_ROWS 162u
/* The rows that are a base, and whether HellForge mixes at all (the steps its MixInstall leaves in the base rows).
 * NULL, or why a table could not be read. What HellForge added after its MixInstall is left out as it was then: its
 * steps of weight 0, the radar station's entries, the city missions it makes for level 11 alone. */
const char* co_mix_bases(uintptr_t game, uint8_t base[CO_MIX_ROWS], int* mixing);
/* The same, refused unless the rows are the ones worked out from the game's image when the mod was built
 * (co_mix_rows.h): another table is not one this was checked against. */
const char* co_mix_read(uintptr_t game, uint8_t base[CO_MIX_ROWS], int* mixing);

#endif
