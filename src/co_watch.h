/* The war-table crash's watch, on unless HELLFORGE_CO_WATCH=0 or a file wartable-watch.off in the mod's folder. The map screen's
 * object O is built when the screen is pushed; a widget W inside it was found overwritten with floats (W+0 0.5f,
 * W+0xE0 0, W+0xE4 64.0f) and a pointer into O (W+0xB8). This puts hardware write watchpoints on those words of W in
 * every thread while O lives, and logs who writes them (capped), with times for the XL preview's phases beside them. */
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
/* The widget library: its setters and the tree walk the crash ran in (0x1446310, 0x1448AD0, 0x144C400, 0x144D4D0). */
#define CO_WATCH_LIBRARY_FIRST 0x1436000u
#define CO_WATCH_LIBRARY_END 0x1453000u

/* The watched words of W, one debug register each (Dr0..Dr3), in this order. */
#define CO_WATCH_SPOTS 4
enum { CO_WATCH_LINK, CO_WATCH_FLAGS, CO_WATCH_DIRTY, CO_WATCH_TWEEN };
typedef struct CoWatchSpot { uint32_t offset, length; const char* name; } CoWatchSpot;
extern const CoWatchSpot co_watch_spots[CO_WATCH_SPOTS];
typedef struct CoWatchPlan { uintptr_t object, at[CO_WATCH_SPOTS]; uint32_t length[CO_WATCH_SPOTS]; int count; } CoWatchPlan;

/* The spots of W in an object at `object`: 1, or 0 when a spot is not aligned to its length (a watchpoint needs it). */
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
/* 1 when a spot's value is not one its field holds: the link not a pointer, the flags a float, the dirty bits or the
 * tween slots past their bit fields. */
int co_watch_suspicious(int spot, uint64_t value);

/* Which hits get a full log line: the first from each place, each one that leaves a suspicious value, and those
 * outside the widget library, each kind up to its cap; the rest are only counted (per place, logged when O goes). */
#define CO_WATCH_PLACES 128
#define CO_WATCH_CAP_NEW 64
#define CO_WATCH_CAP_SUSPICIOUS 64
#define CO_WATCH_CAP_OUTSIDE 64
enum { CO_WATCH_LOG_NEW = 1, CO_WATCH_LOG_SUSPICIOUS = 2, CO_WATCH_LOG_OUTSIDE = 4 };
typedef struct CoWatchFilter
{
    volatile uintptr_t place[CO_WATCH_PLACES];
    volatile long count[CO_WATCH_PLACES];
    volatile long library[CO_WATCH_PLACES];
    volatile long logged[3], overflow;
} CoWatchFilter;
int co_watch_filter(CoWatchFilter* filter, uintptr_t rip, int inLibrary, int suspicious);

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
