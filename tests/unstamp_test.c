/* The tile's stamp units after a build (proto/difficulty_world_move.c MoveStampUnitsDestroy): each destroy scans the
 * world's unit lists (exe 0x338960), so 30000 of them in one frame held it for about 240 ms. They now go over frames,
 * each frame within its budget, each unit once, in order, with the animation wait before a frame's first destroy. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestMoverWorld(uint64_t world);
void CoXlTestStampUnits(uint64_t* units, uint32_t n);
int CoXlTestStampUnitsDestroy(double budget);
uint32_t CoXlTestStampUnitsGone(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define UNITS 40
static uint64_t g_world;
static uint64_t g_seen[UNITS * 2];
static int g_destroys, g_badWorld;
static double g_spinMs;

static void Spin(double ms)
{
    LARGE_INTEGER hz, t0, t; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    do QueryPerformanceCounter(&t); while ((double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart < ms);
}
static void Destroy(uint64_t world, uint64_t unit, uint64_t a, uint64_t b)
{
    (void)a; (void)b;
    if (world != g_world) g_badWorld++;
    if (g_destroys < UNITS * 2) g_seen[g_destroys] = unit;
    g_destroys++;
    Spin(g_spinMs);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* image = g_fake.image;
    uint8_t* exe = fake_host_exe();
    CoXlTestGame((uintptr_t)image);
    /* the world interface: its table's destroy (+0x388) and the reviewed animation wait (+0x410, exe 0x3340A0), here
     * mov rcx,[rcx+0x230050]; jmp +0; ret on an executable page */
    static uint8_t wi[0x10], table[0x420];
    uint64_t tableAt = (uint64_t)(uintptr_t)table, wiAt = (uint64_t)(uintptr_t)wi;
    memcpy(wi + 8, &tableAt, 8);
    memcpy(image + 0x3326308u, &wiAt, 8);
    void (*destroy)(uint64_t, uint64_t, uint64_t, uint64_t) = Destroy;
    memcpy(table + 0x388, &destroy, 8);
    static const uint8_t kWait[] = {0x48, 0x8B, 0x89, 0x50, 0x00, 0x23, 0x00, 0xE9, 0, 0, 0, 0, 0xC3};
    memcpy(exe + 0x3340A0u, kWait, sizeof kWait);
    DWORD old = 0;
    VirtualProtect(exe + 0x334000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    uint64_t waitAt = (uint64_t)(uintptr_t)(exe + 0x3340A0u);
    memcpy(table + 0x410, &waitAt, 8);
    uint8_t* world = VirtualAlloc(NULL, 0x231000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_world = (uint64_t)(uintptr_t)world;
    CoXlTestMoverWorld(g_world);
    static uint64_t units[UNITS];

    g_case = "units that cost 1 ms each: a 4 ms frame takes a few, every unit goes once and in order";
    for (int k = 0; k < UNITS; k++) units[k] = 0x1000u + (uint64_t)k;
    units[7] = 0; /* a spawn that made none: skipped */
    CoXlTestStampUnits(units, UNITS);
    g_destroys = 0; g_spinMs = 1.0;
    int frames = 0, done = 0;
    uint32_t before = 0;
    while (!done && frames < 100)
    {
        done = CoXlTestStampUnitsDestroy(4.0);
        frames++;
        uint32_t gone = done ? UNITS : CoXlTestStampUnitsGone();
        CHECK(gone > before); /* at least one a frame */
        CHECK(gone - before <= 5); /* the budget: four 1 ms destroys, the fifth's start counted */
        before = gone;
    }
    CHECK(done && frames >= UNITS / 5);
    CHECK(g_destroys == UNITS - 1 && g_badWorld == 0);
    {
        int order = 1;
        for (int k = 0, at = 0; k < UNITS; k++) if (units[k] != 0) order &= g_seen[at++] == units[k];
        CHECK(order);
    }
    CHECK(fake_host_logged("difficulty: moving tiles: the tile's 40 stamp units went over"));
    CHECK(CoXlTestStampUnitsDestroy(4.0) == 1 && g_destroys == UNITS - 1); /* none left: nothing more */

    g_case = "cheap units: one frame takes them all";
    CoXlTestStampUnits(units, UNITS);
    g_destroys = 0; g_spinMs = 0.0;
    CHECK(CoXlTestStampUnitsDestroy(1e30) == 1 && g_destroys == UNITS - 1);

    g_case = "no units: done at once, nothing called";
    CoXlTestStampUnits(units, 0);
    g_destroys = 0;
    CHECK(CoXlTestStampUnitsDestroy(4.0) == 1 && g_destroys == 0);

    g_case = "the animation wait not the reviewed one: the units are kept, nothing destroyed";
    CoXlTestStampUnits(units, UNITS);
    g_destroys = 0;
    g_fake.strictCode = 1; /* the host compares the bytes */
    exe[0x3340A0u] ^= 0x40;
    CHECK(CoXlTestStampUnitsDestroy(4.0) == 1 && g_destroys == 0);
    CHECK(fake_host_logged("the tile's stamp units kept: the animation wait is not the reviewed one"));
    exe[0x3340A0u] ^= 0x40;

    if (g_failed) { printf("unstamp: %d check(s) failed\n", g_failed); return 1; }
    printf("unstamp: all checks passed\n");
    return 0;
}
