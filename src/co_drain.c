/* A mission's level init over a running preview (co.h). Leaving the ship cancels a generating preview without waiting,
 * and level init neither tests the generator's state nor its job: it clears the pipeline the job still reads. The
 * detour ends the old generation first, with the game's own reset and step, as the game's retry loop does. */
#include <string.h>
#include <windows.h>

#include "co.h"
#ifdef CO_RESEARCH
#include "xl/co_aiw_log.h"
#endif

#define STATE 0x1E4u  /* in the level generator: 0 idle, 1 generating, 2 passes done, 3 finished, 4 cancelling, 5 another pipeline */
#define RETRY 0xF950u /* a retry of the same generation: the game's own loop is around this call */

static const char kInitPin[] = "48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 28 FD FF FF 48 81 EC A0 03 00 00 0F 29 70 B8";
static const char kResetPin[] = "40 53 48 81 EC 40 02 00 00 48 8B 05 F0 B2 6C 01 48 33";
static const char kStepPin[] = "40 55 53 56 57 41 54 48 8D AC 24 40 FC FF FF 48 81 EC C0 04 00 00";
/* Both read the state they act on from [rcx+0x1E4], right behind their stack check. */
static const uint8_t kStateRead[6] = {0x8B, 0x81, 0xE4, 0x01, 0x00, 0x00};
#define RESET_READS 0xF70D2Bu
#define STEP_READS 0xF73F37u

typedef void (*InitFn)(uint64_t level, uint64_t rdx, uint32_t r8, uint64_t preview);
typedef void (*LevelFn)(uintptr_t level);
static InitFn g_init;
static LevelFn g_reset, g_step;

int co_drain(uintptr_t level, void (*reset)(uintptr_t), void (*step)(uintptr_t), uint64_t (*now)(void))
{
    uint32_t state = 0;
    uint8_t retry = 1;
    int steps = 0;
    if (g_coApi->read(level + RETRY, &retry, 1) != 0 || retry != 0 || g_coApi->read(level + STATE, &state, 4) != 0 || state < 1 || state > 5) return 0;
    const uint64_t since = now();
    uint64_t next = CO_DRAIN_NOTE_MS;
    for (;;)
    {
        if (g_coApi->read(level + STATE, &state, 4) != 0 || state == 0 || state > 5) return steps;
        /* no limit, as the game's regenerate: a level init beside the old job would share its process-wide state. The
         * lines make a wait that does not end visible in the log. */
        const uint64_t waited = now() - since;
        if (waited >= next)
        {
            co_log("complex operations: a mission's level init still waits for the level generator: state %u, %llu ms", state, (unsigned long long)waited);
            do next += CO_DRAIN_EVERY_MS; while (next <= waited);
        }
        /* the reset handles 1 and 3 only; the step turns 2 into 3, 5 into 3 once the other pipeline is done, and 4 into
         * 0 once the job and its tokens are done */
        if (state == 1 || state == 3) reset(level);
        step(level);
        steps++;
    }
}

static uint64_t Now(void) { return GetTickCount64(); }

static void InitDetour(uint64_t level, uint64_t rdx, uint32_t r8, uint64_t preview)
{
    if (preview == 0 && level != 0) /* a mission's; a preview's own level init is the preview tick's business */
    {
#ifdef CO_RESEARCH
        co_aiw_log_mission((uintptr_t)level);
#endif
        const uint64_t since = Now();
        const int steps = co_drain((uintptr_t)level, g_reset, g_step, Now);
        if (steps > 0) co_log("complex operations: a preview was still generating at a mission's level init: ended in %d steps, waited %u ms", steps, (uint32_t)(Now() - since));
    }
    g_init(level, rdx, r8, preview);
}

const char* co_drain_check(uintptr_t game)
{
    uint8_t reset[sizeof kStateRead], step[sizeof kStateRead];
    if (g_coApi->code_matches(game + CO_LEVEL_INIT, kInitPin) != 1 || g_coApi->code_matches(game + CO_GEN_RESET, kResetPin) != 1 ||
        g_coApi->code_matches(game + CO_GEN_STEP, kStepPin) != 1 || g_coApi->read(game + RESET_READS, reset, sizeof reset) != 0 ||
        g_coApi->read(game + STEP_READS, step, sizeof step) != 0 || memcmp(reset, kStateRead, sizeof reset) != 0 || memcmp(step, kStateRead, sizeof step) != 0)
        return "the level generator's start is not the game's code this mod was made for";
    return NULL;
}

const char* co_drain_install(uintptr_t game)
{
    const char* why;
    if (g_init != NULL) return NULL;
    if ((why = co_drain_check(game)) != NULL) return why;
    g_reset = (LevelFn)(game + CO_GEN_RESET);
    g_step = (LevelFn)(game + CO_GEN_STEP);
    if (g_coApi->hook(g_coMod, game + CO_LEVEL_INIT, (void*)InitDetour, (void**)&g_init) != 0) { g_init = NULL; return "the level generator's start could not be hooked"; }
    return NULL;
}

#ifdef CO_TEST
void co_drain_test_reset(void) { g_init = NULL; g_reset = g_step = NULL; }
void co_drain_test_image(uint8_t* image) { memcpy(image + RESET_READS, kStateRead, sizeof kStateRead); memcpy(image + STEP_READS, kStateRead, sizeof kStateRead); }
void co_drain_test_calls(void* init, void (*reset)(uintptr_t), void (*step)(uintptr_t)) { g_init = (InitFn)init; g_reset = reset; g_step = step; }
void co_drain_test_init(uint64_t level, uint64_t rdx, uint32_t r8, uint64_t preview) { InitDetour(level, rdx, r8, preview); }
#endif
