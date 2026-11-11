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
/* The same with the allocation type given: MEM_RESERVE alone takes the address space only. */
uintptr_t co_alloc_near_as(uintptr_t target, size_t size, uint32_t type);

/* Eight slots in the per-player component managers (co_managers.c). `install` once with game.dll's base; `state` in
 * the state constructor's hook with the component block, before the game's constructor runs. */
void co_managers_install(uintptr_t game);
void co_managers_state(uintptr_t block);
/* 1 while the managers have eight slots. The join limit and the guards of the score manager's loot rows follow it. */
int co_managers_moved(void);
/* For code that reads a manager itself: where the member the game has at `offset` of the manager held at `global` is
 * now, and how many players the manager takes now. Both change when the managers move or move back. */
uint32_t co_managers_offset(uint32_t global, uint32_t offset);
uint32_t co_managers_slots(uint32_t global);
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

/* Extraction for eight (co_extract.c), for caves co_counts.c writes. `ready` once before they go in; the seats' gives 0,
 * or the place of game code they call that is not the reviewed one (co_counts.c checks the boosters'). co_seats_on is 1
 * once a gunship's unit was seen with both nodes; co_seat_node is the node seat 8 or 9 attaches to on that vehicle. */
uint32_t co_extract_seats_ready(uintptr_t game);
void co_extract_boosters_ready(uintptr_t game);
int co_seats_on(void);
uint32_t co_seat_node(uintptr_t entity, uint32_t seat);
void co_boosters_update(uintptr_t b);

/* The extra large world (xl/co_xl.c): every install of it, NULL or why one refused. Not called yet. */
const char* CoXlInstall(uintptr_t game);

/* Whether an operation is a complex operation (co_roll.c): the same answer on every peer, from the operation record's
 * seed and planet. `chance_65536` is the chance in 1/65536; 65536 and above is always. */
int co_complex_roll(uint32_t operation_seed, uint32_t planet, uint32_t chance_65536);
/* A level-11 mission's seed with the decision in it, and the decision read back (co_roll.c). */
uint32_t co_seed_marked(uint32_t mission_seed, int complex);
int co_seed_is_complex(uint32_t mission_seed);

/* The complex part of the mod (co_complex.c): what it needs, and what a machine does with a level-11 mission. */
#define CO_COMPLEX_LEVEL 11u
#define CO_COMPLEX_CHANCE 16384u     /* in 1/65536: one level-11 operation in four */
#define CO_COMPLEX_COMMIT_MB 28672u  /* commit an extra large map was measured to reach */
typedef struct CoComplexNeeds
{
    int capacity_call, regions, live_load, capacity_granted; /* at the mod's start */
    int level_11, moved, stock_stubs, own_sites;             /* at game load */
    uint32_t commit_free_mb;
} CoComplexNeeds;
enum { CO_HOST_ROW_NONE, CO_HOST_ROW_OFF, CO_HOST_ROW_ON };
enum { CO_MISSION_NORMAL, CO_MISSION_COMPLEX, CO_MISSION_LEAVE };
const char* co_complex_refusal(const CoComplexNeeds* needs);
int co_complex_decision(int is_host, int own_on, int host_row, uint32_t mission_seed);
int co_complex_stays(int own_on, int host_row);
/* One generation's decision. A retry of the same generation (level init again with the retry flag, another seed in
 * the level) keeps the first init's answer; `first_seed` is the seed of the descriptor the generation began with. */
typedef struct CoGeneration { uintptr_t level; uint32_t seed; int decision, held; } CoGeneration;
int co_generation_decision(CoGeneration* g, uintptr_t level, uint32_t first_seed, int retry, uint32_t difficulty, int is_host, int own_on, int host_row);
uint32_t co_complex_entry_seed(uint32_t mission_seed, uint32_t difficulty, uint32_t operation_seed, uint32_t planet, int rolls);

/* Room in the map generator (co_capacity.c). `init` asks HellForge for it, from hellforge_mod_init only. `poll` reads
 * what was given once level 11 is installed, or gives up at `now_ms` past the wait: 1 once settled, with one log line
 * when the complex part is off. `on` is 1 only when every need holds. */
#define CO_GEN_LOCATIONS 65024u /* everything placed on an extra large map; also the most HellForge gives */
#define CO_GEN_WAIT_MS 60000u   /* HellForge installs level 11 up to 20 s after the game loaded */
void co_capacity_init(void);
int co_capacity_poll(uint64_t now_ms);
/* Whether this machine decides complex operations for the boards it builds: the host when its part is on; a client
 * only with the host's row saying on (its own war table then shows what the host's does). */
int co_complex_rolls(int is_host, int own_on, int host_row);

/* The session as the complex part asks it (co_peers.c). The host's answer is one row of the mod's shared table. */
#define CO_SHARE_KEY 0x434F2D434F4D504Cull /* "CO-COMPL" */
#define CO_SHARE_PROTOCOL 1u
typedef struct CoHostRow { uint8_t protocol, on; } CoHostRow;
int co_peers_is_host(void);  /* solo counts: the board it builds is its own */
int co_peers_host_row(void); /* CO_HOST_ROW_: as this machine holds it now */

/* The mark (co_mark.c): a level-11 mission entry's seed carries whether its operation is complex. `install` hooks the
 * entry fill, or says why not; `entry` is the hook's work behind the game's own fill. */
#define CO_MARK_FILL 0x11E6340u
#define CO_WAR_OBJECT 0x347CEE8u
const char* co_mark_install(uintptr_t game);
void co_mark_entry(uintptr_t game, uint32_t slot, uint32_t difficulty, uint32_t operation_seed, uintptr_t entry, int rolls);

const CoComplexNeeds* co_capacity_needs(void);
int co_complex_on(void);
/* Where a region of the generator's block lies and how many elements it holds (HF_GEN_ names): 0 until settled and
 * moved, so code that reads the block finds nothing to read before. `block` is the block's size. */
uint32_t co_gen_at(int region);
uint32_t co_gen_places(int region);
uint32_t co_gen_block(void);

#endif
