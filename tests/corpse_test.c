/* The ring corpse freeze: two settled samples, then the game's two calls once; a corpse whose ragdolls already hold a
 * level-3 request (HellForge 0.5's corpse-freeze, or disposal) gets no second one, so the game's one release still
 * wakes it; a corpse the ragdoll pose sync still moves waits. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

void CoXlTestCorpseSetup(uintptr_t game, void* handles, void* valid, void* pos, void* enable, void* level, void* held);
int CoXlTestCorpseTick(uint32_t unit, ULONGLONG now);
void CoXlTestCorpseCounts(uint32_t* frozen, uint32_t* already, uint32_t* posed);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s (line %d)\n", g_case, #what, __LINE__); g_failed++; } } while (0)

#define UNIT 0x1234u
static uint32_t g_actors[2] = {11, 12}, g_ragdolls[3] = {21, 22, 23};
static float g_pos[3];
static int g_levels, g_enables, g_lastLevel;
static uint8_t g_held[3];
static int Handles(uint32_t unit, uint32_t mask, uint8_t* out)
{
    if (unit != UNIT || mask != 5) return 0;
    uint64_t a = (uint64_t)(uintptr_t)g_actors, r = (uint64_t)(uintptr_t)g_ragdolls; uint32_t na = 2, nr = 3;
    memcpy(out, &a, 8); memcpy(out + 0x38, &na, 4); memcpy(out + 0x10, &r, 8); memcpy(out + 0x40, &nr, 4);
    return 1;
}
static uint8_t Valid(uint32_t unit) { return unit == UNIT; }
static const float* Pos(uint32_t unit, int node) { (void)unit; (void)node; return g_pos; }
static void Enable(const uint32_t* ids, uint32_t n, int on) { (void)ids; (void)n; (void)on; g_enables++; }
static void Level(const uint32_t* ids, uint32_t n, int level) { g_levels++; g_lastLevel = level; for (uint32_t i = 0; i < n; i++) g_held[ids[i] - 21]++; }
static uint8_t Held(uint32_t id, int level) { return level == 3 && id >= 21 && id <= 23 ? g_held[id - 21] : 0; }

/* The pose sync's manager in the fake game image: one entity on `unit`, moving or not. */
static uint8_t* g_game;
static uint8_t g_manager[0x60], g_entity[0x20];
static uint8_t* g_runtime;
static void Pose(uint32_t unit, int moving)
{
    uint64_t m = (uint64_t)(uintptr_t)g_manager, ents = (uint64_t)(uintptr_t)g_entity, list[1], rt = (uint64_t)(uintptr_t)g_runtime;
    static uint64_t entities[1];
    entities[0] = ents; list[0] = (uint64_t)(uintptr_t)entities;
    uint32_t count = 1;
    memcpy(g_game + 0x3326948u, &m, 8);
    memcpy(g_manager + 0x10, &count, 4); memcpy(g_manager + 0x38, &list[0], 8); memcpy(g_manager + 0x48, &rt, 8);
    memcpy(g_entity + 0xC, &unit, 4);
    g_runtime[0x2BA4] = (uint8_t)moving;
}

static void Reset(const char* name)
{
    g_case = name;
    memset(g_game + 0x3326948u, 0, 8);
    memset(g_held, 0, sizeof g_held); memset(g_pos, 0, sizeof g_pos);
    g_levels = g_enables = g_lastLevel = 0;
    CoXlTestCorpseSetup((uintptr_t)g_game, (void*)Handles, (void*)Valid, (void*)Pos, (void*)Enable, (void*)Level, (void*)Held);
}

int main(void)
{
    uint32_t frozen, already, posed;
    g_game = (uint8_t*)VirtualAlloc(NULL, 0x3400000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    g_runtime = (uint8_t*)VirtualAlloc(NULL, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (g_game == NULL || g_runtime == NULL) { printf("corpse: no memory\n"); return 1; }

    Reset("two settled samples, then the game's calls once");
    CHECK(CoXlTestCorpseTick(UNIT, 1000) == 0);  /* the first sample */
    CHECK(CoXlTestCorpseTick(UNIT, 1500) == 0);  /* not a second yet */
    CHECK(CoXlTestCorpseTick(UNIT, 2000) == 0);  /* settled once: the game's disposal would freeze here */
    CHECK(g_levels == 0);
    CHECK(CoXlTestCorpseTick(UNIT, 3000) == 1);  /* settled twice */
    CHECK(g_levels == 1 && g_lastLevel == 3 && g_enables == 1 && g_held[0] == 1 && g_held[2] == 1);
    CHECK(CoXlTestCorpseTick(UNIT, 9000) == 1 && g_levels == 1); /* once */
    CoXlTestCorpseCounts(&frozen, &already, &posed);
    CHECK(frozen == 1 && already == 0 && posed == 0);

    Reset("a moving corpse starts over");
    CHECK(CoXlTestCorpseTick(UNIT, 1000) == 0 && CoXlTestCorpseTick(UNIT, 2000) == 0);
    g_pos[0] = 5.0f;
    CHECK(CoXlTestCorpseTick(UNIT, 3000) == 0);
    CHECK(CoXlTestCorpseTick(UNIT, 4000) == 0 && g_levels == 0); /* settled once again */
    CHECK(CoXlTestCorpseTick(UNIT, 5000) == 1 && g_levels == 1);

    Reset("a corpse HellForge's corpse-freeze (or disposal) froze gets no second request");
    g_held[1] = 1;
    CHECK(CoXlTestCorpseTick(UNIT, 1000) == 0 && CoXlTestCorpseTick(UNIT, 2000) == 0);
    CHECK(CoXlTestCorpseTick(UNIT, 3000) == 1);
    CHECK(g_levels == 0 && g_enables == 0 && g_held[1] == 1);
    CoXlTestCorpseCounts(&frozen, &already, &posed);
    CHECK(frozen == 0 && already == 1);

    Reset("a corpse the pose sync still moves waits");
    Pose(UNIT, 1);
    CHECK(CoXlTestCorpseTick(UNIT, 1000) == 0 && CoXlTestCorpseTick(UNIT, 2000) == 0);
    CHECK(CoXlTestCorpseTick(UNIT, 3000) == 0 && CoXlTestCorpseTick(UNIT, 4000) == 0 && g_levels == 0);
    CoXlTestCorpseCounts(&frozen, &already, &posed);
    CHECK(posed == 2);
    Pose(UNIT, 0);
    CHECK(CoXlTestCorpseTick(UNIT, 5000) == 1 && g_levels == 1);
    Reset("another unit's pose sync does not hold it");
    Pose(UNIT + 1, 1);
    CHECK(CoXlTestCorpseTick(UNIT, 1000) == 0 && CoXlTestCorpseTick(UNIT, 2000) == 0 && CoXlTestCorpseTick(UNIT, 3000) == 1 && g_levels == 1);

    if (g_failed) { printf("corpse: %d checks failed\n", g_failed); return 1; }
    printf("corpse: all cases passed\n");
    return 0;
}
