/* Sites with two paths: the game's own instructions and the mod's, picked by a pointer the site jumps through. A
 * complex mission takes the mod's paths, any other level the game's, and the switch is one aligned pointer store a
 * site, so a thread in either path finishes it as it began. The flags are untouched on the way in. */
#ifndef CO_SWITCH_H
#define CO_SWITCH_H

#include "co.h"

enum { CO_FIX_REL32 = 1, CO_FIX_IMM64 = 2 };
/* A field of the mod's path: a rel32 to `to` (its instruction ends with the field) or an imm64 of `to`. */
typedef struct CoSwitchFix { uint8_t at, kind; uintptr_t to; } CoSwitchFix;

typedef struct CoSwitchSite
{
    uint32_t rva;
    uint8_t size;             /* bytes replaced: a jump, then `fill` */
    const uint8_t* pin;       /* the game's `size` bytes */
    uint8_t copy;             /* the game's path: pin[0, copy), then a jump to game + back */
    uint32_t back;
    uint8_t rel[3];           /* offsets of rel32 fields in pin[0, copy) whose instruction ends with them; 0 for none */
    const uint8_t* game;      /* or, where the pin has rel8 branches, the game's path written out (copy its length) */
    CoSwitchFix gameFix[2];
    const uint8_t* mod;       /* the mod's path: these bytes, then a jump to game + modBack */
    uint8_t modLen;
    uint32_t modBack;
    CoSwitchFix fix[2];
    uint8_t fill;             /* 0x90, or 0xCC where nothing runs after the jump */
    const uint8_t* after;     /* or the size - 5 bytes behind the jump: never run, kept for another mod's checks */
    const char* what;
} CoSwitchSite;

/* NULL when every site holds its pin. Reads only. */
const char* co_switch_check(uintptr_t game, const CoSwitchSite* sites, size_t count);
enum { CO_SWITCH_DENSITY, CO_SWITCH_DEFER, CO_SWITCH_GROUPS };
/* The sites as one batch of `group`: its paths in a page of their own, then the jumps. NULL, or why; on a failure
 * every site of the batch is the game's code again. Each site starts on the path co_switch_live last chose. */
const char* co_switch_install(uintptr_t game, const CoSwitchSite* sites, size_t count, int group);
/* The mod's paths while `on`, else the game's, for every installed site of `group`. */
void co_switch_live(int group, int on);
/* The path a site runs now: 1 the mod's, 0 the game's, -1 not installed. */
int co_switch_path(uintptr_t site);

#endif
