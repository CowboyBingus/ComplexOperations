/* The tile's stamp units' spawns (proto/difficulty_world_move.c MoveStampsSelect): a tile's ~32768 spawns held the game
 * thread 83-120 ms in one frame. They now go over frames within a budget, and the frames together make the same spawns
 * in the same order with the same poses, and stage the same records, as one unbounded frame does. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestMoverWorld(uint64_t world);
void CoXlTestLwNavLevel(long level);
void CoXlTestStreamSetup(uintptr_t game, uint8_t* context, uint8_t* array, uint32_t count);
void CoXlTestStreamSlot(uint32_t i, float x, float y, uint64_t unit, int mission);
void CoXlTestStampPose(uint32_t i, const float* pose);
uint32_t CoXlTestStampCandidateSize(void);
void CoXlTestStampPlan(float cell, const uint32_t* start, void* cand, uint32_t count);
void CoXlTestStampsSelectAt(float cx, float cy);
int CoXlTestStampsSelect(uint64_t locs, uint32_t count, float half, double budget);
uint32_t CoXlTestStampsStaged(const uint8_t** loc, const uint32_t** index, const uint64_t** unit, uint32_t* temp);
void CoXlTestStampUnits(uint64_t* units, uint32_t n);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define HALF 8192.0f
#define GRID 64
#define CELL 256.0f
#define TABLE 60
#define PLANNED 400
#define MAXCALLS 2048
typedef struct { uint64_t resource; float x, y; } Call;
static Call g_calls[MAXCALLS];
static int g_n, g_badWorld, g_failAt = -1;
static uint64_t g_world, g_next;
static double g_spinMs;

static void Spin(double ms)
{
    LARGE_INTEGER hz, t0, t; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    do QueryPerformanceCounter(&t); while ((double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart < ms);
}
static uint64_t Spawn(uint64_t world, uint64_t resource, const float* pose, uint64_t a, uint64_t b)
{
    (void)a; (void)b;
    if (world != g_world) g_badWorld++;
    int k = g_n++;
    if (k < MAXCALLS) { g_calls[k].resource = resource; g_calls[k].x = pose[12]; g_calls[k].y = pose[13]; }
    Spin(g_spinMs);
    return k == g_failAt ? 0 : 0x10000u + ++g_next;
}

static uint8_t g_locs[TABLE * 0xE0];
static uint8_t* g_cand;
static uint64_t g_holders[PLANNED][2];
static uint32_t g_start[GRID * GRID + 1];

/* a deterministic spread around the tile at the origin: some in the inner reach, some in the margin, some out */
static float Spread(uint32_t k, int axis) { return (float)(((k * (axis ? 7919u : 104729u)) % 4200u)) - 2100.0f; }

static void Setup(void)
{
    for (uint32_t i = 0; i < TABLE; i++)
    {
        float xy[2] = {Spread(i, 0) + HALF, Spread(i, 1) + HALF};
        memcpy(g_locs + (size_t)i * 0xE0, xy, 8);
        g_locs[(size_t)i * 0xE0 + 0x40] = (uint8_t)i; /* a byte of its own, to tell the staged copies apart */
        CoXlTestStreamSlot(i, xy[0] - HALF, xy[1] - HALF, 0, 0);
        float pose[16] = {0};
        pose[12] = xy[0] - HALF; pose[13] = xy[1] - HALF;
        CoXlTestStampPose(i, pose);
    }
    /* the plan's entries bucketed by generator metres, as g_plan holds them */
    const uint32_t size = CoXlTestStampCandidateSize();
    g_cand = (uint8_t*)VirtualAlloc(NULL, (size_t)size * PLANNED, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    static uint32_t counts[GRID * GRID], fill[GRID * GRID], order[PLANNED];
    float at[PLANNED][2];
    memset(counts, 0, sizeof counts);
    for (uint32_t k = 0; k < PLANNED; k++)
    {
        at[k][0] = Spread(k + 1000, 0) + HALF; at[k][1] = Spread(k + 1000, 1) + HALF;
        counts[(int)(at[k][1] / CELL) * GRID + (int)(at[k][0] / CELL)]++;
    }
    g_start[0] = 0;
    for (int b = 0; b < GRID * GRID; b++) { g_start[b + 1] = g_start[b] + counts[b]; fill[b] = g_start[b]; }
    for (uint32_t k = 0; k < PLANNED; k++) order[fill[(int)(at[k][1] / CELL) * GRID + (int)(at[k][0] / CELL)]++] = k;
    for (uint32_t e = 0; e < PLANNED; e++)
    {
        uint32_t k = order[e];
        uint8_t* loc = g_cand + (size_t)e * size;
        memcpy(loc, at[k], 8);
        float angle = 0.25f * (float)k; memcpy(loc + 8, &angle, 4);
        loc[0x40] = (uint8_t)(0x80 + k);
        g_holders[k][1] = k % 13 == 5 ? 0 : 0x5000u + k; /* some holders without a resource: skipped */
        uint64_t holder = (uint64_t)(uintptr_t)g_holders[k];
        memcpy(loc + 0x90, &holder, 8);
    }
    CoXlTestStampPlan(CELL, g_start, g_cand, PLANNED);
}

typedef struct { int n, frames; uint32_t staged, temp; Call calls[MAXCALLS]; uint8_t loc[MAXCALLS][0xE0]; uint32_t index[MAXCALLS]; uint64_t unit[MAXCALLS]; } Result;
static Result g_one, g_sliced;

static int Run(Result* out, double budget, double spin, int maxFrames)
{
    g_n = 0; g_next = 0; g_spinMs = spin;
    CoXlTestStampsSelectAt(0.0f, 0.0f);
    int r = 0, frames = 0;
    while (frames < maxFrames && (r = CoXlTestStampsSelect((uint64_t)(uintptr_t)g_locs, TABLE, HALF, budget)) == 0) frames++;
    out->frames = frames + 1;
    out->n = g_n;
    memcpy(out->calls, g_calls, sizeof(Call) * (size_t)(g_n < MAXCALLS ? g_n : MAXCALLS));
    const uint8_t* loc; const uint32_t* index; const uint64_t* unit;
    out->staged = CoXlTestStampsStaged(&loc, &index, &unit, &out->temp);
    memcpy(out->loc, loc, (size_t)out->staged * 0xE0); memcpy(out->index, index, 4 * (size_t)out->staged); memcpy(out->unit, unit, 8 * (size_t)out->temp);
    return r;
}

static int Same(const Result* a, const Result* b)
{
    if (a->n != b->n || a->staged != b->staged || a->temp != b->temp) return 0;
    for (int k = 0; k < a->n; k++)
        if (a->calls[k].resource != b->calls[k].resource || a->calls[k].x != b->calls[k].x || a->calls[k].y != b->calls[k].y) return 0;
    return memcmp(a->loc, b->loc, (size_t)a->staged * 0xE0) == 0 && memcmp(a->index, b->index, 4 * (size_t)a->staged) == 0 &&
        memcmp(a->unit, b->unit, 8 * (size_t)a->temp) == 0;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* image = g_fake.image;
    CoXlTestGame((uintptr_t)image);
    CoXlTestLwNavLevel(7);
    static uint8_t wi[0x10], table[0x400];
    uint64_t tableAt = (uint64_t)(uintptr_t)table, wiAt = (uint64_t)(uintptr_t)wi;
    memcpy(wi + 8, &tableAt, 8);
    memcpy(image + 0x3326308u, &wiAt, 8);
    uint64_t (*spawn)(uint64_t, uint64_t, const float*, uint64_t, uint64_t) = Spawn;
    memcpy(table + 0x380, &spawn, 8);
    static uint8_t world[0x100], context[0x100];
    g_world = (uint64_t)(uintptr_t)world;
    CoXlTestMoverWorld(g_world);
    CoXlTestStreamSetup((uintptr_t)image, context, g_locs, TABLE);
    Setup();

    g_case = "one unbounded frame: the table's slots in reach, then the plan's inner pass, then its margin";
    CHECK(Run(&g_one, 1e30, 0.0, 1) == 1 && g_one.frames == 1);
    CHECK(g_one.n > 40 && g_badWorld == 0);
    CHECK(g_one.staged == (uint32_t)g_one.n && g_one.temp == g_one.staged);
    {
        int tableFirst = 1, seenPlan = 0, innerFirst = 1, seenMargin = 0;
        for (uint32_t k = 0; k < g_one.staged; k++)
        {
            int plan = g_one.index[k] == 0xFFFFFFFFu;
            if (!plan && seenPlan) tableFirst = 0;
            seenPlan |= plan;
            if (!plan) continue;
            float x = g_one.calls[k].x + HALF, y = g_one.calls[k].y - 20000.0f + HALF; /* WindowPose's translation: loc - W/2 */
            float dx = x - HALF, dy = y - HALF;
            int inner = dx <= 1408.0f && dx >= -1408.0f && dy <= 1408.0f && dy >= -1408.0f;
            if (inner && seenMargin) innerFirst = 0;
            seenMargin |= !inner;
        }
        CHECK(tableFirst && seenPlan && innerFirst && seenMargin);
    }
    CHECK(fake_host_logged("their units made over 1 frames"));

    g_case = "spawns of 0.3 ms in 1 ms frames: the same spawns, poses, order and staged records over many frames";
    CHECK(Run(&g_sliced, 1.0, 0.3, 10000) == 1);
    CHECK(g_sliced.frames >= g_one.n / 5 && g_sliced.frames <= g_one.n);
    CHECK(Same(&g_one, &g_sliced));

    g_case = "one spawn a frame (a budget of 0): still the same, one frame per spawn";
    CHECK(Run(&g_sliced, 0.0, 0.0, 10000) == 1);
    CHECK(g_sliced.frames == g_one.n + 1 || g_sliced.frames == g_one.n); /* the last frame may find nothing left to make */
    CHECK(Same(&g_one, &g_sliced));

    g_case = "a spawn that makes no unit: counted, left out of the staging, the rest the same";
    g_failAt = 17;
    CHECK(Run(&g_sliced, 0.0, 0.0, 10000) == 1);
    CHECK(g_sliced.n == g_one.n && g_sliced.staged == g_one.staged - 1);
    CHECK(fake_host_logged("1 stamp units could not be made at the offset"));
    g_failAt = -1;

    g_case = "the level gone between two frames: -1, and the units made so far stay listed for the failure's destroys";
    g_n = 0; g_next = 0; g_spinMs = 0.0;
    CoXlTestStampsSelectAt(0.0f, 0.0f);
    CHECK(CoXlTestStampsSelect((uint64_t)(uintptr_t)g_locs, TABLE, HALF, 0.0) == 0);
    CHECK(CoXlTestStampsSelect((uint64_t)(uintptr_t)g_locs, TABLE, HALF, 0.0) == 0);
    CoXlTestLwNavLevel(8);
    {
        const int before = g_n;
        CHECK(CoXlTestStampsSelect((uint64_t)(uintptr_t)g_locs, TABLE, HALF, 0.0) == -1 && g_n == before);
        const uint8_t* loc; const uint32_t* index; const uint64_t* unit; uint32_t temp = 0;
        CoXlTestStampsStaged(&loc, &index, &unit, &temp);
        CHECK(temp == 2 && unit[0] == 0x10001u && unit[1] == 0x10002u);
    }
    CoXlTestLwNavLevel(7);
    CoXlTestStreamSetup((uintptr_t)image, context, g_locs, TABLE); /* clears the slots: set again */
    Setup();

    g_case = "a new select after a cut one starts from the beginning";
    CHECK(Run(&g_sliced, 1e30, 0.0, 1) == 1 && Same(&g_one, &g_sliced));

    if (g_failed) { printf("stampselect: %d check(s) failed\n", g_failed); return 1; }
    printf("stampselect: all checks passed\n");
    return 0;
}
