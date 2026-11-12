/* The stamp packages a complex level requested early and never reached record 94 for: the first main-thread step of
 * the next level releases them, a normal level's too, and never a step of the complex level that holds them. Run on
 * the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestLevelInit(uintptr_t level);
void CoXlTestGenLevel(float current);
void CoXlTestEarlyHeld(const uint64_t* ids, uint32_t count);
int CoXlTestEarlyState(void);
void CoXlTestEarlyStep(uint64_t records, uint64_t index, uint64_t mask);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define API 0x3600000u   /* [game+0x3326308]: the engine's interface, +0x10 the table whose +0x308 unloads a list */
#define TABLE 0x3601000u
#define MAIN 0u          /* the dispatcher's mask on a main-thread step (0xF26140); the job passes 2 */
#define JOB 2u

static uint8_t* g_lg;
static uint64_t g_records;
static uint32_t g_index;
static int g_releases;
static uint64_t g_released[8];
static uint32_t g_releasedCount;

static void FakeRelease(uint64_t* ids, uint32_t count)
{
    g_releases++; g_releasedCount = count;
    memcpy(g_released, ids, (count < 8 ? count : 8) * sizeof *ids);
}

static void Step(uint64_t mask) { CoXlTestEarlyStep(g_records, (uint64_t)(uintptr_t)&g_index, mask); }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    const uintptr_t game = (uintptr_t)g_fake.image, api = game + API, table = game + TABLE, release = (uintptr_t)FakeRelease;
    memcpy(g_fake.image + 0x3326308u, &api, 8);
    memcpy(g_fake.image + API + 0x10, &table, 8);
    memcpy(g_fake.image + TABLE + 0x308, &release, 8);
    CoXlTestGame(game);
    g_lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_lg == NULL) { printf("earlyrelease: no memory\n"); return 1; }
    g_records = (uint64_t)(uintptr_t)(g_lg + 0x10000); /* the level's own records: [lg+0x2A8] */
    memcpy(g_lg + 0x2A8, &g_records, 8);
    CoXlTestLevelInit((uintptr_t)g_lg);
    const uint64_t ids[3] = {0x1111, 0x2222, 0x3333};

    g_case = "inside the complex level that holds them";
    CoXlTestGenLevel(8.0f);
    CoXlTestEarlyHeld(ids, 3);
    g_index = 40; Step(MAIN); Step(JOB); g_index = 41; Step(MAIN);
    CHECK(g_releases == 0 && CoXlTestEarlyState() == 1);

    g_case = "the next level is a normal one: a job step";
    CoXlTestGenLevel(1.0f);
    g_index = 0; Step(JOB);
    CHECK(g_releases == 0);
    g_case = "the next level is a normal one: its first main-thread step";
    Step(MAIN);
    CHECK(g_releases == 1 && g_releasedCount == 3 && memcmp(g_released, ids, sizeof ids) == 0 && CoXlTestEarlyState() == -1);
    CHECK(fake_host_logged("its 3 early packages released"));
    g_case = "the next level is a normal one: a later main-thread step";
    g_index = 1; Step(MAIN); Step(JOB);
    CHECK(g_releases == 1);

    g_case = "another level's records";
    CoXlTestGenLevel(8.0f);
    CoXlTestEarlyHeld(ids, 3);
    CoXlTestGenLevel(1.0f);
    CoXlTestEarlyStep(g_records + 0x100, (uint64_t)(uintptr_t)&g_index, MAIN); /* a tile replay's or a runner's own record */
    CHECK(g_releases == 1);
    Step(MAIN);
    CHECK(g_releases == 2 && g_releasedCount == 3);

    g_case = "the next level is a complex one";
    CoXlTestEarlyHeld(ids, 2);
    CoXlTestGenLevel(8.0f);
    Step(JOB);
    CHECK(g_releases == 2);
    Step(MAIN);
    CHECK(g_releases == 3 && g_releasedCount == 2);

    if (g_failed) { printf("earlyrelease: %d checks failed\n", g_failed); return 1; }
    printf("earlyrelease: all cases passed\n");
    return 0;
}
