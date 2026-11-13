/* The war-table crash's watch, on unless HELLFORGE_CO_WATCH=0 or a file wartable-watch.off in the mod's folder. The map screen's
 * object O is built when the screen is pushed. Two crashes found its event panel Z overwritten: a widget W (W+0 0.5f,
 * W+0xE0 0), then the layout node N4 (element 0, first child 0xC00C1017). This puts hardware write watchpoints on those
 * words in every thread while O lives, and logs who writes them (capped), with times for the XL preview's phases. */
#ifndef CO_WATCH_H
#define CO_WATCH_H

#include <stdint.h>

/* The screen, build 25480438: the menu's screen factory 0x14984E0 (case 24 at 0x149958D) allocates 0x239C30 bytes,
 * zeroes them, calls the constructor and stores its return (O) at [menu+0x118] (0x14995C8); the screen free 0x14AC340
 * (case 24 at 0x14AC6FE) frees [menu+0x118] and clears it. The constructor has no other caller. */
#define CO_WATCH_SCREEN_ID 24u
#define CO_WATCH_SCREEN_SLOT 0x118u
#define CO_WATCH_SCREEN_SIZE 0x239C30u
#define CO_WATCH_CTOR 0x14A4C60u
#define CO_WATCH_FREE 0x14AC340u
/* W = O + 0x804C0 (Z, built at 0x14A4F53) + 0x32048 (X, built at 0x14A66EB) + 0x488 (W, 0x17A476C). */
#define CO_WATCH_Z 0x804C0u
#define CO_WATCH_X 0x32048u
#define CO_WATCH_W_IN_X 0x488u
#define CO_WATCH_W (CO_WATCH_Z + CO_WATCH_X + CO_WATCH_W_IN_X)
/* N4 = Z + 0x3578 (a panel widget C, built by 0x1A18E50 from the panel init 0x187D390 at 0x187D7C5) + 0x2B900 (C's own
 * layout node, linked under the panel's root at 0x187D943). Only C's constructor writes its element (+0, at 0x1A18FE1:
 * C) and first child (+0x10, at 0x1A18FB0: 0); nothing adds children to it. The trap's address is the next instruction. */
#define CO_WATCH_C_IN_Z 0x3578u
#define CO_WATCH_NODE_IN_C 0x2B900u
#define CO_WATCH_N4 (CO_WATCH_Z + CO_WATCH_C_IN_Z + CO_WATCH_NODE_IN_C)
#define CO_WATCH_NODE_INIT_CHILD 0x1A18FB7u
#define CO_WATCH_NODE_INIT_ELEMENT 0x1A18FE8u
/* The widget library: its setters and the tree walk the crash ran in (0x1446310, 0x1448AD0, 0x144C400, 0x144D4D0). */
#define CO_WATCH_LIBRARY_FIRST 0x1436000u
#define CO_WATCH_LIBRARY_END 0x1453000u

/* The watched words, offsets in O, one debug register each (Dr0..Dr3), in this order. */
#define CO_WATCH_SPOTS 4
enum { CO_WATCH_FLAGS, CO_WATCH_LINK, CO_WATCH_ELEMENT, CO_WATCH_CHILD };
typedef struct CoWatchSpot { uint32_t offset, length; const char* name; } CoWatchSpot;
extern const CoWatchSpot co_watch_spots[CO_WATCH_SPOTS];
typedef struct CoWatchPlan { uintptr_t object, at[CO_WATCH_SPOTS]; uint32_t length[CO_WATCH_SPOTS]; int count; } CoWatchPlan;

/* The spots in an object at `object`: 1, or 0 when a spot is not aligned to its length (a watchpoint needs it). */
int co_watch_plan(uintptr_t object, CoWatchPlan* plan);
/* Dr7 for a plan: Lk set and RWk = 01 (write) with LENk for each register used; 0 for a length that is not 1, 2, 4 or
 * 8 or an address not aligned to it. CO_WATCH_DR7_BITS are every bit the watch owns (L0-L3, G0-G3, RW/LEN 0-3). */
#define CO_WATCH_DR7_BITS 0xFFFF00FFull
uint64_t co_watch_dr7(const CoWatchPlan* plan);
/* Whether the watch's handler takes a debug exception: Dr6 names a register (bits 0-3), or (Wine reports Dr6 as 0) the
 * thread is one the watch armed and the trap is not a TF single step. */
enum { CO_WATCH_PASS, CO_WATCH_CLAIM, CO_WATCH_CLAIM_UNKNOWN };
int co_watch_claim(uint32_t code, uint64_t dr6, uint64_t eflags, int armed);
/* 1 for an address in game.dll's widget library. */
int co_watch_in_library(uintptr_t game, uintptr_t rip);
/* Where a write came from: the widget library, C's constructor setting up N4, or anywhere else. */
enum { CO_WATCH_ELSEWHERE, CO_WATCH_IN_LIBRARY, CO_WATCH_IN_NODE_INIT };
int co_watch_known(uintptr_t game, uintptr_t rip);
/* 1 when a spot's value is not one its field holds: the flags a float, the link not a pointer, N4's element 0 or not a
 * pointer, its first child not a pointer. */
int co_watch_suspicious(int spot, uint64_t value);

/* Which hits get a full log line: the first few from each place and each one that leaves a suspicious value, each
 * kind up to its cap for the session; the rest are only counted (per place, logged when O goes). The widget library
 * writes W thousands of times an opening, so a repeat costs one count. */
#define CO_WATCH_PLACES 128
#define CO_WATCH_PER_PLACE 3
#define CO_WATCH_CAP_NEW 96
#define CO_WATCH_CAP_SUSPICIOUS 64
enum { CO_WATCH_LOG_NEW = 1, CO_WATCH_LOG_SUSPICIOUS = 2 };
typedef struct CoWatchFilter
{
    volatile uintptr_t place[CO_WATCH_PLACES];
    volatile long count[CO_WATCH_PLACES];
    volatile long known[CO_WATCH_PLACES];
    volatile long logged[2], overflow;
} CoWatchFilter;
int co_watch_filter(CoWatchFilter* filter, uintptr_t rip, int known, int suspicious);

/* The game side. `on` is the switch. `install` once with game.dll's base: NULL, or why the watch stays off. `tick` from the game-thread
 * tick (the XL preview's state); `level` from the XL level init (`preview` 0 for a mission's level); `mark` one dated
 * line for the XL code's phases. */
int co_watch_on(void);
const char* co_watch_install(uintptr_t game);
void co_watch_tick(void);
void co_watch_level(uintptr_t level, uintptr_t preview);
void co_watch_mark(const char* format, ...) __attribute__((format(printf, 1, 2)));

#ifdef CO_TEST
void co_watch_test_pins(uint8_t* image); /* the reviewed bytes, into a stand-in game image */
int co_watch_test_settle(unsigned ms);   /* waits until the worker has done every job asked so far: 1, or 0 on time out */
int co_watch_test_self(void);            /* the self-test's traps: 2 when both watchpoint lengths trapped */
long co_watch_test_hits(void);
int co_watch_test_armed(unsigned long thread);
#endif

#endif
