/* Test switch HELLFORGE_TEST_DAYLIGHT=1 (development only; this machine's view only): missions at a wanted minute of
 * the day, for test runs that must be well lit. Included by co_xl.c after living_world.c. Static analysis (build
 * 25480438; read in the dump or run from it unless marked I):
 * - The time of day is an output: minutes 0..1440 (noon 720) at [G+0x78AE668], G = [game+0x3326340], recomputed every
 *   frame (0x1017EF0, stored 0x102744A) from the sun's direction W+0x120C, which the sky job takes from the observer
 *   planet's transform 0x1022270: its orbit chain 0x101C7C0 times its spin.
 * - Both angles come from 0x1016C20(xmm2 = period (double), xmm3 = phase (float), [rsp+0x28] = t): the phase itself
 *   when period < 0 (0x1016CBE..0x1016CC9). The spin's call is 0x102244E, the orbit's 0x101C8FB (every body's).
 * - Both calls go to stubs with period -1: the spin stands at angle X, each orbit at its own phase. Then the clock
 *   is linear in X and stands still. With the orbit left running a planet whose day is its orbit keeps its clock
 *   moving, and the game bends the sun's path (0x10174A0), so the clock answers X unevenly. */
#define DAY_SITE 0x102244Eu
#define DAY_ORBIT_SITE 0x101C8FBu
#define DAY_SPIN 0x1016C20u
#define DAY_ORBIT_STUB 0x40u /* the orbit's stub in the block; the spin's is at 0 */
/* The stub's data page: -1.0 (double) at +0x1000, X at +0x1008, the wanted minute at +0x100C. The test harness
 * stores the minute there (sunctl.py); the tick moves X. NULL when not installed. */
static volatile float* g_dayAngle;
static volatile float* g_dayWant;
/* HELLFORGE_TEST_DAYLIGHT_MIN=<60..1380>: the minute of the day wanted at the start in place of noon (a low sun
 * shows a bank's shadow). */
static float g_dayMinutes = 720.0f;
#include "../co_daylight.h"

static void InstallDaylight(uintptr_t game)
{
    char t[4] = "";
    if (game == 0 || !(GetEnvironmentVariableA("HELLFORGE_TEST_DAYLIGHT", t, sizeof t) > 0 && t[0] == '1')) return;
    char minutes[8] = "";
    if (GetEnvironmentVariableA("HELLFORGE_TEST_DAYLIGHT_MIN", minutes, sizeof minutes) > 0)
    {
        const float want = (float)atof(minutes);
        if (want >= 60.0f && want <= 1380.0f) g_dayMinutes = want;
    }
    if (!ApiCodeMatches(game + DAY_SITE, "E8 CD 47 FF FF") ||
        !ApiCodeMatches(game + DAY_ORBIT_SITE, "E8 20 A3 FF FF") ||
        !ApiCodeMatches(game + 0x1016C71u, "F2 44 0F 10 05 CE FF 3A 01 0F 28 CA F2 0F 10 84 24 20 01 00 00") ||
        !ApiCodeMatches(game + 0x1016CBAu, "45 0F 57 FF 66 0F 2F CE F2 44 0F 5A F8 76 04 44 0F 28 FF"))
    { Note("test: daylight: the sky's spin or orbit call is not the reviewed code; nothing is patched, missions keep their time of day"); return; }
    uint8_t* block = AllocateNearGame(game, 0x2000);
    if (block == NULL) { Note("test: daylight: no memory within 2 GB of game.dll"); return; }
    uint8_t code[21] = {0xF2, 0x0F, 0x10, 0x15, 0, 0, 0, 0,  /* movsd xmm2, [rip+period] (-1.0; rel32 at 4, next 8) */
        0xF3, 0x0F, 0x10, 0x1D, 0, 0, 0, 0,                  /* movss xmm3, [rip+X] (rel32 at 12, next 16) */
        0xE9, 0, 0, 0, 0};                                   /* jmp 0x1016C20 (rel32 at 17, next 21) */
    int32_t rel = 0x1000 - 8; memcpy(code + 4, &rel, 4);
    rel = 0x1008 - 16; memcpy(code + 12, &rel, 4);
    int64_t on = (int64_t)(game + DAY_SPIN) - (int64_t)(uintptr_t)(block + 21);
    if (!InRel32(on)) { Note("test: daylight: the stub is out of reach of game.dll"); return; }
    rel = (int32_t)on; memcpy(code + 17, &rel, 4);
    memcpy(block, code, sizeof code);
    /* The orbit's stub: movsd xmm2, [rip+period]; jmp 0x1016C20. xmm3 keeps the body's own phase. */
    uint8_t orbit[13] = {0xF2, 0x0F, 0x10, 0x15, 0, 0, 0, 0, 0xE9, 0, 0, 0, 0};
    rel = 0x1000 - (int32_t)(DAY_ORBIT_STUB + 8); memcpy(orbit + 4, &rel, 4);
    on = (int64_t)(game + DAY_SPIN) - (int64_t)(uintptr_t)(block + DAY_ORBIT_STUB + 13);
    if (!InRel32(on)) { Note("test: daylight: the orbit's stub is out of reach of game.dll"); return; }
    rel = (int32_t)on; memcpy(orbit + 9, &rel, 4);
    memcpy(block + DAY_ORBIT_STUB, orbit, sizeof orbit);
    double period = -1.0; memcpy(block + 0x1000, &period, 8);
    float zero = 0.0f; memcpy(block + 0x1008, &zero, 4);
    memcpy(block + 0x100C, &g_dayMinutes, 4);
    DWORD protect;
    VirtualProtect(block, 0x1000, PAGE_EXECUTE_READ, &protect);
    FlushInstructionCache(GetCurrentProcess(), block, 0x1000);
    uint8_t call[5] = {0xE8, 0, 0, 0, 0};
    int64_t to = (int64_t)(uintptr_t)block - (int64_t)(game + DAY_SITE + 5);
    if (!InRel32(to)) { Note("test: daylight: the stub is out of reach of the call site"); return; }
    rel = (int32_t)to; memcpy(call + 1, &rel, 4);
    uint8_t orbitCall[5] = {0xE8, 0, 0, 0, 0};
    to = (int64_t)(uintptr_t)(block + DAY_ORBIT_STUB) - (int64_t)(game + DAY_ORBIT_SITE + 5);
    if (!InRel32(to)) { Note("test: daylight: the orbit's stub is out of reach of its call site"); return; }
    rel = (int32_t)to; memcpy(orbitCall + 1, &rel, 4);
    /* The orbit first: a spin fixed alone is the uneven switch this replaces, so it is never left on its own. */
    if (ApiWrite(game + DAY_ORBIT_SITE, orbitCall, 5)) { Note("test: daylight: the orbit's call site could not be written; missions keep their time of day"); return; }
    if (ApiWrite(game + DAY_SITE, call, 5))
    {
        const uint8_t back[5] = {0xE8, 0x20, 0xA3, 0xFF, 0xFF};
        const int left = ApiWrite(game + DAY_ORBIT_SITE, back, 5);
        Note("test: daylight: the spin's call site could not be written; the orbit's call is %s", left ? "LEFT FIXED (bodies stand still)" : "put back");
        return;
    }
    g_dayAngle = (volatile float*)(block + 0x1008);
    g_dayWant = (volatile float*)(block + 0x100C);
    Note("test: daylight: the sun's spin (0x102244E) and the orbits (0x101C8FB) are fixed (HELLFORGE_TEST_DAYLIGHT=1); the clock is held at %.0f min, or the minute stored at the stub's +0x100C", (double)g_dayMinutes);
}

/* Game thread, every tick: every 30 ticks the clock is read and X stepped until it reads the wanted minute. No level
 * key: a sky is installed at the ship, a preview and a mission alike, and the read back follows each. */
static void DaylightTick(void)
{
    static CoDayLoop loop; static int frames; static float noted = -1.0f;
    uintptr_t game = g_lw.game ? g_lw.game : ApiModuleBase("game.dll", NULL), g;
    if (g_dayAngle == NULL || !EvpPtr(game + 0x3326340u, &g) || ++frames < 30) return;
    frames = 0;
    float m = -1, night = -1;
    SafeRead(g + 0x78AE668u, &m, 4); SafeRead(g + 0x78AE5BCu, &night, 4);
    const float want = *g_dayWant;
    loop.x = *g_dayAngle; /* the harness may still store an angle (the old sunctl path) */
    const char* why = NULL;
    const float before = loop.x;
    const int stepped = co_day_loop(&loop, m, want, &why);
    if (why) Note("test: daylight: %s (clock %.0f min, wanted %.0f, angle %.3f rad)", why, (double)m, (double)want, (double)before);
    if (stepped)
    {
        *g_dayAngle = loop.x;
        Note("test: daylight: the clock reads %.0f min (night %.2f), wanted %.0f: angle %.3f -> %.3f rad", (double)m, (double)night, (double)want, (double)before, (double)loop.x);
        noted = -1.0f;
    }
    else if (!loop.resting && m >= 0 && fabsf(co_day_diff(m, want)) <= CO_DAY_NEAR && noted != want)
    {
        noted = want;
        Note("test: daylight: %.0f min (night %.2f) at %.3f rad", (double)m, (double)night, (double)loop.x);
    }
}
