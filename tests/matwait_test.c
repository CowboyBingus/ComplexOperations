/* Op 0x41's setup create (proto/xl_free.c, A10c): its fence wait at exe 0x1A8449 returns at once inside the setup and
 * is done by the game's own 0x106030 two frame kicks later, or at a teardown or the level's end; every other caller,
 * another device table, no render thread or a full list waits as the game does. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestMatWait(uintptr_t exe, void* orig);
uint8_t CoXlTestMatWaitFrom(int in, uint64_t r, uint32_t id, uintptr_t from);
void CoXlTestMatWaitTick(void);
void CoXlTestMatWaitDrain(void);
void CoXlTestMatWaitNote(void);
uint32_t CoXlTestMatWaitPending(void);
void CoXlTestMatRes(const void* fns, int ready, void* teardown);
void CoXlTestMatResTeardown(uint64_t world);
void CoXlTestNearLevelLeft(int ship);
void CoXlTestMatWaitTimed(uintptr_t frameAt, uintptr_t destroyAt);
double CoXlTestMoveReleaseFenceMs(void);
void CoXlTestMoveFrameMark(int mark, double ms);
double CoXlTestMoveFrameFenceMs(void);
int CoXlTestMatWaitInstall(uintptr_t* frameAt, uintptr_t* destroyAt);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define EXE ((uintptr_t)0x7FF600000000ull)
static uint8_t g_r[0x1A60], g_r2[0x1A60];
static uint64_t g_dev[2], g_dev11[2];
static int g_waits; static uint64_t g_waitR[16]; static uint32_t g_waitId[16];

static uint8_t SlowWait(uint64_t r, uint32_t id) { (void)r; (void)id; g_waits++; Sleep(20); return 1; }
static uint8_t Wait(uint64_t r, uint32_t id) { if (g_waits < 16) { g_waitR[g_waits] = r; g_waitId[g_waits] = id; } g_waits++; return 1; }

static void Renderer(uint8_t* r, int threaded, uint32_t kick, uint64_t* dev, int at1138)
{
    memset(r, 0, 0x1A60);
    r[0x1480] = (uint8_t)threaded;
    *(uint32_t*)(r + 0x1A50) = kick;
    *(uint64_t*)(r + (at1138 ? 0x1138 : 0x1168)) = (uint64_t)(uintptr_t)dev;
}
static int g_teardowns, g_waitsAtTeardown;
static void Teardown(uint64_t world) { (void)world; g_teardowns++; g_waitsAtTeardown = g_waits; }
static void Kick(uint8_t* r, uint32_t kick) { *(uint32_t*)(r + 0x1A50) = kick; }
static uint64_t R(uint8_t* r) { return (uint64_t)(uintptr_t)r; }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    const uintptr_t site = EXE + 0x1A844Eu;
    g_dev[0] = EXE + 0x16955D8u;   /* d3d12's device table */
    g_dev11[0] = EXE + 0x1696788u; /* d3d11's */

    g_case = "inside the setup, from 0x1A8449: no wait now; the game's wait two frame kicks later, with the same renderer and fence";
    CoXlTestMatWait(EXE, (void*)Wait);
    Renderer(g_r, 1, 50, g_dev, 0);
    g_waits = 0;
    CHECK(CoXlTestMatWaitFrom(1, R(g_r), 7, site) == 0 && g_waits == 0 && CoXlTestMatWaitPending() == 1);
    Kick(g_r, 51);
    CoXlTestMatWaitTick();
    CHECK(g_waits == 0 && CoXlTestMatWaitPending() == 1);
    Kick(g_r, 52);
    CoXlTestMatWaitTick();
    CHECK(g_waits == 1 && g_waitR[0] == R(g_r) && g_waitId[0] == 7 && CoXlTestMatWaitPending() == 0);

    g_case = "the kick count wraps: still two kicks";
    Renderer(g_r, 1, 0xFFFFFFFFu, g_dev, 0);
    g_waits = 0;
    CHECK(CoXlTestMatWaitFrom(1, R(g_r), 3, site) == 0);
    Kick(g_r, 0);
    CoXlTestMatWaitTick();
    CHECK(g_waits == 0);
    Kick(g_r, 1);
    CoXlTestMatWaitTick();
    CHECK(g_waits == 1 && g_waitId[0] == 3);

    g_case = "the device at +0x1138 when +0x1168 is empty";
    Renderer(g_r2, 1, 10, g_dev, 1);
    g_waits = 0;
    CHECK(CoXlTestMatWaitFrom(1, R(g_r2), 4, site) == 0 && g_waits == 0 && CoXlTestMatWaitPending() == 1);
    CoXlTestMatWaitDrain();
    CHECK(g_waits == 1 && g_waitR[0] == R(g_r2));

    g_case = "every other caller waits now: another return address, or outside the setup";
    CoXlTestMatWait(EXE, (void*)Wait);
    Renderer(g_r, 1, 60, g_dev, 0);
    g_waits = 0;
    CHECK(CoXlTestMatWaitFrom(1, R(g_r), 9, EXE + 0x8A7C9u) == 1 && g_waits == 1 && g_waitId[0] == 9);
    CHECK(CoXlTestMatWaitFrom(0, R(g_r), 9, site) == 1 && g_waits == 2);
    CHECK(CoXlTestMatWaitPending() == 0);

    g_case = "d3d11's table or no render thread: the setup waits now, counted";
    Renderer(g_r, 1, 60, g_dev11, 0);
    g_waits = 0;
    CHECK(CoXlTestMatWaitFrom(1, R(g_r), 5, site) == 1 && g_waits == 1);
    Renderer(g_r, 0, 60, g_dev, 0);
    CHECK(CoXlTestMatWaitFrom(1, R(g_r), 5, site) == 1 && g_waits == 2 && CoXlTestMatWaitPending() == 0);

    g_case = "a full list: the 9th waits now; a drain waits all, whatever their kicks";
    CoXlTestMatWait(EXE, (void*)Wait);
    Renderer(g_r, 1, 70, g_dev, 0);
    g_waits = 0;
    for (uint32_t k = 0; k < 8; k++) CHECK(CoXlTestMatWaitFrom(1, R(g_r), 20 + k, site) == 0);
    CHECK(g_waits == 0 && CoXlTestMatWaitPending() == 8);
    CHECK(CoXlTestMatWaitFrom(1, R(g_r), 99, site) == 1 && g_waits == 1 && g_waitId[0] == 99);
    CoXlTestMatWaitDrain();
    CHECK(g_waits == 9 && g_waitId[1] == 20 && g_waitId[8] == 27 && CoXlTestMatWaitPending() == 0);

    g_case = "the world's teardown (exe 0x1A6E00) waits them before the game's teardown runs";
    static uint8_t noFns[512];
    CoXlTestMatRes(noFns, 1, (void*)Teardown);
    Renderer(g_r, 1, 80, g_dev, 0);
    g_waits = 0;
    CHECK(CoXlTestMatWaitFrom(1, R(g_r), 11, site) == 0 && CoXlTestMatWaitFrom(1, R(g_r), 12, site) == 0);
    CoXlTestMatResTeardown(0x1234);
    CHECK(g_teardowns == 1 && g_waitsAtTeardown == 2 && g_waitId[0] == 11 && g_waitId[1] == 12 && CoXlTestMatWaitPending() == 0);

    g_case = "a level's end waits them";
    g_waits = 0;
    CHECK(CoXlTestMatWaitFrom(1, R(g_r), 13, site) == 0 && CoXlTestMatWaitPending() == 1);
    CoXlTestNearLevelLeft(0);
    CHECK(g_waits == 1 && g_waitId[0] == 13 && CoXlTestMatWaitPending() == 0);

    g_case = "the build's line, then the counts start again";
    g_fake.log[0] = 0;
    CoXlTestMatWaitNote();
    CHECK(fake_host_logged("difficulty: moving tiles: op 0x41's setup create waits this build: 11 deferred, 11 completed later, pending max 8 (now 0), "
        "1 waited inline (list full, no render thread or not d3d12)"));
    g_fake.log[0] = 0;
    CoXlTestMatWaitNote();
    CHECK(strstr(g_fake.log, "setup create waits") == NULL);

    g_case = "the frame loop's fence (return 0x8A7C9) and a terrain destroy's (0x1ADFB8) wait as the game's, timed, inside the setup too";
    {
        enum { MF_TICK, MF_LEVEL };
        CoXlTestMatWait(EXE, (void*)SlowWait);
        CoXlTestMatWaitTimed(EXE + 0x8A7C9u, EXE + 0x1ADFB8u);
        Renderer(g_r, 1, 90, g_dev, 0);
        g_waits = 0;
        CoXlTestMoveFrameMark(MF_TICK, 0.0); /* the early update: CoXlTick to the level update */
        CHECK(CoXlTestMatWaitFrom(1, R(g_r), 4, EXE + 0x8A7C9u) == 1 && g_waits == 1 && CoXlTestMatWaitPending() == 0);
        const double fence = CoXlTestMoveFrameFenceMs();
        CHECK(fence >= 15.0 && fence < 1000.0);
        CoXlTestMoveFrameMark(MF_LEVEL, 0.0);
        CHECK(CoXlTestMatWaitFrom(0, R(g_r), 4, EXE + 0x8A7C9u) == 1 && g_waits == 2 && CoXlTestMoveFrameFenceMs() == fence); /* after the level update: not its part */
        CHECK(CoXlTestMatWaitFrom(1, R(g_r), 5, EXE + 0x1ADFB8u) == 1 && g_waits == 3 && CoXlTestMatWaitPending() == 0);
        CHECK(CoXlTestMoveReleaseFenceMs() >= 15.0 && CoXlTestMoveFrameFenceMs() == fence);
        CHECK(CoXlTestMatWaitFrom(1, R(g_r), 6, EXE + 0x1A844Eu) == 0 && g_waits == 3 && CoXlTestMatWaitPending() == 1); /* the setup's still deferred */
        CoXlTestMatWaitDrain();
        CoXlTestMatWaitTimed(0, 0); /* their pins did not match: the game's wait, untimed */
        CoXlTestMoveFrameMark(MF_TICK, 100.0);
        g_waits = 0;
        CHECK(CoXlTestMatWaitFrom(0, R(g_r), 4, EXE + 0x8A7C9u) == 1 && g_waits == 1 && CoXlTestMoveFrameFenceMs() == -1.0);
        CHECK(CoXlTestMatWaitFrom(0, R(g_r), 5, EXE + 0x1ADFB8u) == 1 && g_waits == 2 && CoXlTestMoveReleaseFenceMs() == 0.0);
    }

    g_case = "the install: each timed wait on its own pins; one not the reviewed code leaves it untimed and the other and A10c as they are";
    {
        const uintptr_t x = (uintptr_t)fake_host_exe();
        uintptr_t frame = 1, destroy = 1;
        g_fake.log[0] = 0;
        CHECK(CoXlTestMatWaitInstall(&frame, &destroy) == 1 && frame == x + 0x8A7C9u && destroy == x + 0x1ADFB8u);
        CHECK(fake_host_logged("the frame loop's fence wait (exe 0x8A7C4) timed; a terrain destroy's fence wait (exe 0x1ADFB3) timed"));
        const uint32_t pins[3] = {0x8A7A3u, 0x8A7B8u, 0x1ADFA1u};
        for (int k = 0; k < 3; k++)
        {
            g_fake.noMatchExe[0] = pins[k];
            g_fake.log[0] = 0;
            CHECK(CoXlTestMatWaitInstall(&frame, &destroy) == 1);
            CHECK(k < 2 ? frame == 0 && destroy == x + 0x1ADFB8u : frame == x + 0x8A7C9u && destroy == 0);
            CHECK(fake_host_logged(k < 2 ? "the frame loop's fence wait (exe 0x8A7C4) not timed (not the reviewed code); a terrain destroy's fence wait (exe 0x1ADFB3) timed"
                                         : "the frame loop's fence wait (exe 0x8A7C4) timed; a terrain destroy's fence wait (exe 0x1ADFB3) not timed (not the reviewed code)"));
        }
        g_fake.noMatchExe[0] = 0x1A843Au; /* A10c's own pin: no hook, nothing timed */
        CHECK(CoXlTestMatWaitInstall(&frame, &destroy) == -1 && frame == 0 && destroy == 0);
        g_fake.noMatchExe[0] = 0;
        CoXlTestMatWaitTimed(0, 0);
    }

    if (g_failed) { printf("matwait: %d check(s) failed\n", g_failed); return 1; }
    printf("matwait: all checks passed\n");
    return 0;
}
