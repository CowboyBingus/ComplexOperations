/* A mission's level init over a running preview: the generator is cancelled and stepped until it is idle before the
 * game's level init runs, however long that takes; a level init that finds it idle is the game's alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define LEVEL 0x1500000u /* a level generator, in the image the fake host reads */
static uint8_t* g_level;
static char g_trace[64];
static int g_busy, g_inits;
static uint64_t g_clock, g_args[4];
static uint32_t State(void) { uint32_t s; memcpy(&s, g_level + 0x1E4, 4); return s; }
static void Set(uint32_t s) { memcpy(g_level + 0x1E4, &s, 4); }
static void Note(char c) { size_t n = strlen(g_trace); if (n + 1 < sizeof g_trace) { g_trace[n] = c; g_trace[n + 1] = 0; } }
/* The game's reset and step, as far as the state goes: 1 and 3 cancel; 2 finishes; 4 ends once the job is done; 5
 * finishes once the other pipeline's job is done. */
static void Reset(uintptr_t level) { Note('r'); if (State() == 1 || State() == 3) Set(4); }
static void Step(uintptr_t level)
{
    Note('s');
    if (State() == 2) Set(3);
    else if (State() == 4) { if (g_busy > 0) g_busy--; else Set(0); }
    else if (State() == 5) { if (g_busy > 0) g_busy--; else Set(3); }
}
static int Count(const char* text) { int n = 0; for (const char* p = g_fake.log; (p = strstr(p, text)) != NULL; p++) n++; return n; }
static uint64_t Clock(void) { return g_clock += 100; }
static void Init(uint64_t level, uint64_t rdx, uint32_t r8, uint64_t preview)
{
    const uint64_t args[4] = {level, rdx, r8, preview};
    memcpy(g_args, args, sizeof args); g_inits++; Note('i');
}

static void Start(const char* name, uint32_t state, int busy)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_drain_test_reset();
    co_drain_test_image(g_fake.image);
    memset(g_level, 0, 0x10000);
    Set(state);
    g_trace[0] = 0; g_busy = busy; g_clock = 0; g_inits = 0;
    co_drain_test_calls((void*)Init, Reset, Step);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_level = g_fake.image + LEVEL;
    const uintptr_t level = (uintptr_t)g_level;

    Start("the check", 0, 0);
    const uintptr_t game = (uintptr_t)g_fake.image;
    CHECK(co_drain_check(game) == NULL);
    {
        const uint32_t entries[] = {CO_LEVEL_INIT, CO_GEN_RESET, CO_GEN_STEP}, reads[] = {0xF70D2Bu + 2u, 0xF73F37u + 2u};
        for (int i = 0; i < 3; i++)
        {
            Start("a function is another game's", 0, 0);
            g_fake.noMatch[0] = entries[i]; co_drain_test_reset();
            CHECK(co_drain_check(game) != NULL && co_drain_install(game) != NULL && g_fake.hookCount == 0);
        }
        for (int i = 0; i < 2; i++)
        {
            Start("the state is read elsewhere", 0, 0);
            g_fake.image[reads[i]] ^= 4; co_drain_test_reset();
            CHECK(co_drain_check(game) != NULL && co_drain_install(game) != NULL && g_fake.hookCount == 0);
        }
        Start("the install", 0, 0);
        co_drain_test_reset();
        CHECK(co_drain_install(game) == NULL && g_fake.hookCount == 1 && g_fake.hooks[0].target == CO_LEVEL_INIT && g_fake.writes == 0);
        CHECK(co_drain_install(game) == NULL && g_fake.hookCount == 1);
        Start("no hook", 0, 0);
        co_drain_test_reset();
        g_fake.noHook = CO_LEVEL_INIT; CHECK(co_drain_install(game) != NULL);
    }

    Start("an idle generator", 0, 0);
    CHECK(co_drain(level, Reset, Step, Clock) == 0 && g_trace[0] == 0 && g_clock == 0);
    Start("another pipeline", 5, 2);
    CHECK(co_drain(level, Reset, Step, Clock) == 4 && strcmp(g_trace, "sssrs") == 0 && State() == 0); /* 5 -> 3 -> 4 -> 0 */
    Start("an unknown state", 6, 0);
    CHECK(co_drain(level, Reset, Step, Clock) == 0 && g_trace[0] == 0 && State() == 6);
    Start("a retry", 1, 0);
    g_level[0xF950] = 1; /* the game's own loop is around this level init */
    CHECK(co_drain(level, Reset, Step, Clock) == 0 && g_trace[0] == 0 && State() == 1);

    Start("a generating preview", 1, 3);
    CHECK(co_drain(level, Reset, Step, Clock) == 4 && strcmp(g_trace, "rssss") == 0 && State() == 0); /* cancelled once, stepped while its job runs */
    Start("a preview whose passes are done", 2, 0);
    CHECK(co_drain(level, Reset, Step, Clock) == 2 && strcmp(g_trace, "srs") == 0 && State() == 0);   /* the reset takes no state 2 */
    Start("a finished preview", 3, 1);
    CHECK(co_drain(level, Reset, Step, Clock) == 2 && strcmp(g_trace, "rss") == 0 && State() == 0);
    Start("a cancelling preview", 4, 0);
    CHECK(co_drain(level, Reset, Step, Clock) == 1 && strcmp(g_trace, "s") == 0 && State() == 0);
    Start("a slow job", 1, 250);
    CHECK(co_drain(level, Reset, Step, Clock) == 251 && State() == 0); /* 25 s, 100 ms a look: waited for, no limit */
    CHECK(Count("still waits") == 3 && Count("state 4, ") == 3 && fake_host_logged("state 4, 2000 ms") && fake_host_logged("state 4, 12000 ms") &&
        fake_host_logged("state 4, 22000 ms")); /* at 2 s, then every 10 s */
    Start("a quick job", 1, 15);
    CHECK(co_drain(level, Reset, Step, Clock) == 16 && State() == 0 && g_fake.log[0] == 0); /* under 2 s: no line */

    Start("a mission's level init", 1, 2);
    co_drain_test_init(level, 0x22, 0x33, 0);
    CHECK(strcmp(g_trace, "rsssi") == 0 && g_inits == 1 && g_args[0] == level && g_args[1] == 0x22 && g_args[2] == 0x33 && g_args[3] == 0);
    CHECK(fake_host_logged("a preview was still generating") && fake_host_logged("ended in 3 steps"));
    Start("a mission's level init on an idle generator", 0, 0);
    co_drain_test_init(level, 0x22, 0x33, 0);
    CHECK(strcmp(g_trace, "i") == 0 && g_fake.log[0] == 0);
    Start("a preview's level init", 1, 2);
    co_drain_test_init(level, 0x22, 0x33, 0x44);
    CHECK(strcmp(g_trace, "i") == 0 && State() == 1 && g_args[3] == 0x44 && g_fake.log[0] == 0); /* the preview's own; nothing waited for */
    Start("a mission's level init over a refresh run", 5, 3);
    co_drain_test_init(level, 0x22, 0x33, 0);
    CHECK(strcmp(g_trace, "ssssrsi") == 0 && g_inits == 1 && State() == 0 && fake_host_logged("ended in 5 steps"));

    if (g_failed) { printf("drain: %d checks failed\n", g_failed); return 1; }
    printf("drain: all cases passed\n");
    return 0;
}
