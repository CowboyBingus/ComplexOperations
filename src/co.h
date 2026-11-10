/* Complex Operations: shared declarations. Game build 25480438 only; every address is checked before use. */
#ifndef CO_H
#define CO_H

#include <stddef.h>
#include <stdint.h>

#include "hellforge_mod.h"

#define CO_GAME_BUILD "25480438"

extern const HfApi* g_coApi;
extern HfMod* g_coMod;

void co_log(const char* format, ...);

/* Eight ship hellpods (co_pods.c). Called once, on the loader's thread, when game.dll is loaded. */
void co_pods_install(uintptr_t game);

#endif
