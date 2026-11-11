/* Complex Operations: shared declarations. Game build 25480438 only; every address is checked before use. */
#ifndef CO_H
#define CO_H

#include <stddef.h>
#include <stdint.h>

#include "hellforge_mod.h"

#define CO_GAME_BUILD "25480438"
/* The build's player count (scripts/build.sh --players). Four leaves the join gate, the client's own index tests, the
 * SOS beacon and the lobby's size as the game has them. */
#ifndef CO_PLAYERS
#define CO_PLAYERS 4
#endif

extern const HfApi* g_coApi;
extern HfMod* g_coMod;

void co_log(const char* format, ...);

/* Eight ship hellpods (co_pods.c). Called once, on the loader's thread, when game.dll is loaded. */
void co_pods_install(uintptr_t game);

/* Free address space within reach of a 32-bit offset from `target`; 0 when there is none. */
uintptr_t co_alloc_near(uintptr_t target, size_t size);

/* Eight slots in the per-player component managers (co_managers.c). `install` once with game.dll's base; `state` in
 * the state constructor's hook with the component block, before the game's constructor runs. */
void co_managers_install(uintptr_t game);
void co_managers_state(uintptr_t block);
/* 1 while the managers have eight slots. The join limit and the guards of the score manager's loot rows follow it. */
int co_managers_moved(void);
#ifdef CO_CANARY
void co_managers_canary(int fill);
void co_session_canary(int fill);
#endif

/* The engine's peer list message (co_engine.c). Once, with helldivers2.exe's base (0 when unknown). */
void co_engine_install(uintptr_t engine);

/* The session's peer lists (co_session.c). `install` once with game.dll's base, on the loader's thread; `state` in
 * the state constructor's hook. */
void co_session_install(uintptr_t game);
void co_session_state(void);

/* The session lists' counts (co_counts.c). `install` once with game.dll's base, before the session's; `apply` when the
 * arrays are moved, `undo` before they move back, `session_init` behind the game's session init. */
void co_counts_install(uintptr_t game);
void co_counts_apply(uintptr_t context, uintptr_t extension);
void co_counts_undo(void);
void co_counts_session_init(void);

/* Tables a build for eight moves at run time (co_ui.c). `install` once with game.dll's base, before the state
 * constructor's hook goes in; `state` in that hook with the component block. `guarded` is 0 while a table with four
 * entries lacks its guards: then no player index above 3 may exist. */
void co_ui_install(uintptr_t game);
void co_ui_state(uintptr_t block);
int co_ui_guarded(void);

/* Extraction for eight (co_extract.c), for caves co_counts.c writes. `ready` once before they go in: 0, or the place of
 * game code they call that is not the reviewed one. co_seats_on is 1 once the Pelican's unit was seen with both nodes. */
uint32_t co_extract_seats_ready(uintptr_t game);
uint32_t co_extract_boosters_ready(uintptr_t game);
int co_seats_on(void);
void co_boosters_update(uintptr_t b);

#endif
