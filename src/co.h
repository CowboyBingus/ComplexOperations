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

/* The registry's list of loadout card widgets (co_registry.c), for the eight cards of a build for eight. `cards` moves it
 * to the mod's room for eight, from the counts' apply before any card exists; 1 when it is there. `back` returns it to
 * the game's list while no card is registered in it. */
int co_registry_cards(uintptr_t game);
void co_registry_back(void);

/* Tables a build for eight moves at run time (co_ui.c). `install` once with game.dll's base, before the state
 * constructor's hook goes in; `state` in that hook with the component block. `guarded` is 0 while a table with four
 * entries lacks its guards: then no player index above 3 may exist. */
void co_ui_install(uintptr_t game);
void co_ui_state(uintptr_t block);
int co_ui_guarded(void);

/* What a cave's callback sees: the registers as the game had them, in the order the cave pushed them (co_counts.c). */
typedef struct CoRegs { uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rbx, rdx, rcx, rax, flags; } CoRegs;

/* The stack fixes' callbacks for avatars (co_avatars.c), in the counts' caves. `install` once with game.dll's base. */
void co_avatars_install(uintptr_t game);
void co_avatars_dist_sum(CoRegs* regs);
void co_avatars_enc_spread(CoRegs* regs);

#endif
