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
/* Reserves the space every page within reach of helldivers2.exe comes from: taken as the mod starts, since the game's
 * heaps fill what lies near the exe before the complex part installs. co_alloc_near_as then answers a target inside
 * the exe's image with committed pages from it, while it has room. 0 when the space could not be had. */
int co_near_exe_reserve(uintptr_t exe, size_t image);

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

/* The loadout cards for eight (co_cards.c). The card's height in reference units, which the caves of every store of
 * it read (780, the game's, or 532). The callbacks: `open` before the init's card loop and `ready` behind it (rdi = the
 * screen), `layout` in front of each of its updates (rcx = the screen). */
extern float co_card_height;
void co_cards_open(CoRegs* regs);
void co_cards_ready(CoRegs* regs);
void co_cards_layout(CoRegs* regs);
void co_cards_install(uintptr_t game);
/* Eight cards for a complex operation, more than four players, or a player on a card past the fourth (NULL: not read). */
int co_cards_eight(int complex, uint32_t players, const int32_t cardPlayers[8]);
/* A card's place for a layout: the game's (nothing set), the game's four cards, or eight in two rows. x is the anchor
 * in the 1920-wide container, y the bottom in it; the figure's scale and its y below the card's top. With four, cards
 * 5 to 8 are parked below the window. */
enum { CO_CARDS_GAME, CO_CARDS_FOUR, CO_CARDS_EIGHT };
typedef struct CoCardPlace { float x, y, scale, figureScale, figureY; } CoCardPlace;
float co_cards_scale(float rootHeight, float hudScale);
int co_cards_place(int mode, float rootHeight, float hudScale, uint32_t card, CoCardPlace* p);
/* The size a widget is at, or a running size animation's target in the table `tweens`: 1 when animating. */
int co_cards_size_target(uintptr_t widget, uintptr_t tweens, float out[2]);
/* An open screen's cards from four to eight: the slot to 532 and each card part sized by 780 to its end at once. */
void co_cards_to_eight(uintptr_t screen);
#ifdef CO_TEST
void co_cards_test_mode(int mode, uintptr_t screen);
int co_cards_test_get_mode(void);
void co_cards_test_clock(uint64_t (*clock)(void));
unsigned co_cards_test_complex_reads(void);
#endif

/* The stack fixes' callbacks for avatars (co_avatars.c), in the counts' caves. `install` once with game.dll's base. */
void co_avatars_install(uintptr_t game);
void co_avatars_dist_sum(CoRegs* regs);
void co_avatars_enc_spread(CoRegs* regs);
void co_avatars_dormancy(CoRegs* regs);

/* Extraction for eight (co_extract.c), for caves co_counts.c writes. `ready` once before they go in; the seats' gives 0,
 * or the place of game code they call that is not the reviewed one (co_counts.c checks the boosters'). co_seats_on is 1
 * once a gunship's unit was seen with both nodes; co_seat_node is the node seat 8 or 9 attaches to on that vehicle. */
uint32_t co_extract_seats_ready(uintptr_t game);
void co_extract_boosters_ready(uintptr_t game);
int co_seats_on(void);
uint32_t co_seat_node(uintptr_t entity, uint32_t seat);
void co_boosters_update(uintptr_t b);

/* The extra large world (xl/co_xl.c): every install of it, NULL or why one refused. Only co_complex_start calls it. */
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
#define CO_COMPLEX_MISSIONS 3u      /* an operation's missions, all on the one map */
typedef struct CoComplexNeeds
{
    int capacity_call, regions, live_load, capacity_granted; /* at the mod's start */
    int level_11, moved, stock_stubs, own_sites;             /* once level 11 is installed */
    uint32_t commit_free_mb;
    int early, records;                                      /* as game.dll is loaded: the engine's room, the records' hook */
} CoComplexNeeds;
enum { CO_HOST_ROW_NONE, CO_HOST_ROW_OFF, CO_HOST_ROW_ON };
enum { CO_MISSION_NORMAL, CO_MISSION_COMPLEX, CO_MISSION_LEAVE };
const char* co_complex_refusal(const CoComplexNeeds* needs);
/* 1 when the refusal is that the game's code is not the build this mod was made for. */
int co_complex_refused_build(const CoComplexNeeds* needs);
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
/* game.dll, once it is loaded: with it the poll also asks HellForge's chained steps, the mod's own sites and the
 * memory. Without it those stay unasked and the part off. */
void co_capacity_game(uintptr_t game);
/* The engine's room for a world of 16384 m (co_early.c): bodies and static actors 0x100000, actor connectors
 * 0x400000, a broadphase of 17408 m, a navigation grid of 32 x 32 cells. The engine lays these out once, in the plugin
 * setup, so they go in as game.dll is loaded. `check` reads only; `install` is NULL when all went in, else why and
 * every site is as it was. */
#define CO_EARLY_WORLD 16384.0f
#define CO_EARLY_CELLS 32u
const char* co_early_check(uintptr_t game, uintptr_t exe);
const char* co_early_install(uintptr_t game, uintptr_t exe);
int co_early_in(void);
/* The entity records' move to 16384 (xl/co_xl.c): hooked from `early`, done when the game builds its entity state.
 * 0 not asked, 1 hooked, 2 moved, -1 the game's 2048 stay. */
void CoXlEarly(uintptr_t game);
int CoXlRecords(void);
/* From the game-loaded event, after co_capacity_game: decides the engine's room on what is known then (HellForge's
 * capacity granted at boot, the mod's own sites, the memory) and puts it in. */
void co_capacity_early(void);
/* Once the poll has settled, whatever it decided: what a normal level needs in a process with the engine's room (the
 * navigation grid's plan per level). 1 when there is nothing to do or it went in. */
int co_capacity_normal(void);
/* 0 builds a mod whose complex part is never on, whatever the machine has: the rest of the mod alone. */
#define CO_COMPLEX_COMPLETE 1
/* Once the part is on: every install of it. 1 when all went in; else the part is off again and the log says why. */
int co_complex_start(void);
int co_capacity_poll(uint64_t now_ms);
/* Whether this machine decides complex operations for the boards it builds: the host when its part is on; a client
 * only with the host's row saying on (its own war table then shows what the host's does). */
int co_complex_rolls(int is_host, int own_on, int host_row);

/* The session as the complex part asks it (co_peers.c). The host's answer is one row of the mod's shared table. */
#define CO_SHARE_KEY 0x434F2D434F4D504Cull /* "CO-COMPL" */
#define CO_SHARE_PROTOCOL 1u
typedef struct CoHostRow { uint8_t protocol, on; } CoHostRow;
int co_peers_is_host(void);  /* solo counts: the board it builds is its own */
/* What the other machines said they can do. A client tells the host once it holds the host's row, with its own game
 * peer id in the message; a machine that said nothing is unknown, never assumed able. */
#define CO_PEERS_MAX 16u
enum { CO_PEER_UNKNOWN, CO_PEER_OFF, CO_PEER_ON };
typedef struct CoCapability { uint8_t protocol, on, reserved[6]; uint64_t peer; } CoCapability;
typedef struct CoReports { struct { uint64_t peer; uint8_t capability; } rows[CO_PEERS_MAX]; uint32_t count; } CoReports;
int co_capability_read(const void* data, uint32_t bytes, uint64_t* peer);
void co_reports_note(CoReports* reports, uint64_t peer, int capability);
int co_reports_get(const CoReports* reports, uint64_t peer);
/* At launch, as host: 1 when the mission's mark must go, because this machine's part is off or a machine in a slot
 * (`present`, 0 for a free one; `own` is this machine) is not known to be able. The override text decides for itself. */
int co_launch_clears(const CoReports* reports, const uint64_t* present, uint32_t slots, uint64_t own, int own_on, int override_set);
/* co_launch_clears on what LaunchClear reads at a launch now: 1 the mark goes, 0 it stays, -1 a client's or unread. */
int co_launch_would_clear(void);
/* A join, as host: into a mission whose seed is marked only a machine known to be able comes; an unknown one is held
 * for the limit (its message is on the way), then refused. */
enum { CO_JOIN_ADMIT, CO_JOIN_HOLD, CO_JOIN_DENY };
#define CO_JOIN_LIMIT_MS 15000u
int co_join_decision(int marked, int capability, uint64_t waited_ms);
/* 1 when a generation seed string ("f3fb", the mission seed in 8 hex digits, 8 more, 2, the level in 2) is a level-11
 * mission's with the mark. */
int co_seed_text_marked(const char* text);
/* The join gate (co_join.c): the host's confirmation of a join, 0x108C760(L, peer, join id, the peer's reported id,
 * int), held the way the game holds one (slot state 4) while the session's seed string is marked and the peer has not
 * said what it can do, and refused as "Denied" when it cannot or stays silent for the limit. `gate` is the detour's
 * decision for one call: CO_JOIN_ADMIT means the game's own function decides. */
#define CO_JOIN_GATE 0x108C760u
#define CO_HOST_UPDATE 0x108A440u  /* (L): every session update, on the thread the gate runs on */
#define CO_JOIN_REMOVE 0x108B6B0u  /* (L, peer): the peer's slot and its place in the sync lists */
#define CO_JOIN_DENIED 0xBE3CF0u   /* (peer, join id, reason, extra): the client shows "Denied" */
const char* co_join_check(uintptr_t game);
const char* co_join_install(uintptr_t game);
int co_join_gate(uintptr_t sync, uint64_t peer, uint64_t join, uint64_t reported, uint32_t value, uint64_t now_ms);
/* A mission's level init while a war-table preview still generates (co_drain.c): the game starts over the running
 * generation. The detour of 0xF71220 first cancels it (states 1-5) and steps the generator until it is idle, with no
 * time limit, as the game's regenerate does; a line at 2 s and every 10 s after shows a long wait. */
#define CO_LEVEL_INIT 0xF71220u
#define CO_GEN_RESET 0xF70D10u     /* (lg): states 1 and 3 become 4, cancelling */
#define CO_GEN_STEP 0xF73F10u      /* (lg): the generator's step; state 4 becomes 0 once its job has ended */
#define CO_DRAIN_NOTE_MS 2000u
#define CO_DRAIN_EVERY_MS 10000u
const char* co_drain_check(uintptr_t game);
const char* co_drain_install(uintptr_t game);
/* The steps taken until the generator was idle. `now` reads the clock, for the wait's log lines only. */
int co_drain(uintptr_t level, void (*reset)(uintptr_t), void (*step)(uintptr_t), uint64_t (*now)(void));
#ifdef CO_TEST
void co_join_test_reset(void);
void co_join_test_image(uint8_t* image, uint8_t slots);
void co_join_test_calls(void* gate, void* update, void* remove, void* denied, uint32_t slots);
uint64_t co_join_test_confirm(uint64_t sync, uint64_t peer, uint64_t join, uint64_t reported, uint32_t value);
uint64_t co_join_test_update(uint64_t sync);
void co_drain_test_reset(void);
void co_drain_test_image(uint8_t* image);
void co_drain_test_calls(void* init, void (*reset)(uintptr_t), void (*step)(uintptr_t));
void co_drain_test_init(uint64_t level, uint64_t rdx, uint32_t r8, uint64_t preview);
#endif
/* The live side (co_peers.c). `start` once at game load; `tick` on the game thread: the host keeps its row set, a
 * client sends its message once it holds the row, each tried again while HellForge refuses. `launch_install` hooks
 * the seed string's build so a host's launch drops the mark when not every machine can make the mission. */
#define CO_SESSION_OBJECT 0x347CEF0u
#define CO_LAUNCH_STRING 0x12D2CF0u
#define CO_GAME_STATE 0x3326340u
void co_peers_start(uintptr_t game);
void co_peers_tick(uint64_t now_ms);
const CoReports* co_peers_reports(void);
const char* co_launch_check(uintptr_t game);
const char* co_launch_install(uintptr_t game);
int co_peers_host_row(void); /* CO_HOST_ROW_: as this machine holds it now */

/* The mark (co_mark.c): a level-11 mission entry's seed carries whether its operation is complex. `install` hooks the
 * entry fill, or says why not; `entry` is the hook's work behind the game's own fill. */
#define CO_MARK_FILL 0x11E6340u
#define CO_WAR_OBJECT 0x347CEE8u
const char* co_mark_check(uintptr_t game); /* NULL when the fill is the game's; reads only */
const char* co_mark_install(uintptr_t game);
void co_mark_entry(uintptr_t game, uint32_t slot, uint32_t difficulty, uint32_t operation_seed, uintptr_t entry, int rolls);

const CoComplexNeeds* co_capacity_needs(void);
/* 1 once the poll has settled; before that co_complex_on() is 0 though the part may still come on. */
int co_capacity_settled(void);
int co_complex_on(void);
/* Where a region of the generator's block lies and how many elements it holds (HF_GEN_ names): 0 until settled and
 * moved, so code that reads the block finds nothing to read before. `block` is the block's size. */
uint32_t co_gen_at(int region);
uint32_t co_gen_places(int region);
uint32_t co_gen_block(void);

#endif
