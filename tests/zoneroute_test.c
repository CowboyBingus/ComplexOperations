/* A near tile's zone heights and the level's route distance (proto/difficulty_world_terrain.c NearPlan, NearRunStep,
 * NearRunAt). RenderZoneHeightmap (op 0x3D) scales each zone's height by generated_route_distance at the level's uv;
 * only RenderRouteSegments (0x0D, one job on the route-distance generator [R+0x58]) writes it, after 0x3D. A moving
 * tile's merge is made again with that texture new and unwritten, so the plan runs the level's 0x0D first with its job
 * in the level's pixels (the job's own gui transform, no tile transform), then 0x3D, then the tile's own 0x0D as
 * before. Records in the level's order from a planet 211 run; the dispatcher and the render interface are the test's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
int CoXlTestNearPlan(uint64_t records, uint32_t at, uint8_t* kinds, uint8_t* ops, uint32_t* recs, int max);
void CoXlTestNearMerge(uint64_t R, uint64_t cam0, uint64_t cam1, uint64_t gui, float scale, float ox, float oy, void* original);
int CoXlTestNearRunStep(uintptr_t level, uint64_t records, int step, void* dispatch);
LONG CoXlTestNearRouteArmed(void);
void CoXlTestNearJob(const uint8_t* job, uint64_t camera);
void CoXlTestNearReplaying(int active);
LONG CoXlTestNearResetState(void);
void CoXlTestNearReset(LONG state, int pins);
void CoXlTestNearTextures(uint64_t tile0, uint64_t tile1);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

enum { RECORD = 0, TILE = 1, LEVEL = 2 }; /* the plan's step kinds */
#define API 0x3700000u   /* free room in the fake image: the api and its +0xD0 table */
#define TABLE 0x3700200u
#define NAMES 0x3700600u /* the api's +0x10 table: publish +0x210, unpublish +0x218 */
#define CAM0 0x5000ull
#define CAM1 0x6000ull
#define GUI 0x7000ull
#define ROUTES 0x8000ull /* [R+0x58]: terrain_generator_route_distance */
#define HEIGHTS 0x9000ull /* [R+0x28]: terrain_generator_heightmap */
#define SCALE 7.1f
#define OX -16160.8f
#define OY -12519.1f

static const uint8_t kOps[] = {
    0x00, 0x19, 0x90, 0x01, 0x25, 0x07, 0x12, 0x75, 0x79, 0x9F, 0xB9, 0x9A, 0x15, 0x16, 0x06, 0x17, 0x82, 0x18, 0x8B, 0x98,
    0x7B, 0x76, 0x74, 0x77, 0x78, 0x11, 0x7C, 0x98, 0xA0, 0x98, 0x82, 0x8B, 0xA0, 0x8E, 0x85, 0x28, 0xA4, 0x79, 0x7B, 0x7C,
    0x7A, 0x7E, 0x08, 0x29, 0x2A, 0x29, 0xB6, 0x86, 0x99, 0x4E, 0x8D, 0x7B, 0x7D, 0xB9, 0x7A, 0x7E, 0xA5, 0x13, 0x88, 0x8D,
    0x92, 0x9E, 0x0E, 0x05, 0x0F, 0x02, 0x03, 0x37, 0x1C, 0x93, 0xAD, 0x09, 0xAB, 0x5F, 0x04, 0x2C, 0x2E, 0x2D, 0x38, 0x53,
    0x5A, 0x54, 0x39, 0x3A, 0x3C, 0x40, 0x5B, 0x5C, 0x87, 0x26, 0xB3, 0x2B, 0x5F, 0x8F, 0x1F, 0x1D, 0x1D, 0x1D, 0x20, 0xA6,
    0x31, 0xBA, 0x2F, 0x1E, 0x0A, 0x94, 0x3E, 0x3F, 0x3D, 0x0D, 0x0C, 0x5B, 0x5C, 0xB8, 0xB7, 0x5D, 0x5E, 0x4C, 0x36, 0x1A,
    0x1B, 0x33, 0x32, 0x35, 0x68, 0x63, 0x42, 0x41, 0x64, 0x65, 0x66};
#define AT65 129u
#define AT3D 108u
#define AT0D 109u

static uint8_t* g_records;
static uint8_t g_R[0x200];
static uint8_t g_level[0x400];

/* The gui transform as the job's render_world got it. */
static struct { uint64_t gui; float x, y; int n; } g_offset, g_scale;
static void Offset(uint64_t gui, float x, float y) { g_offset.gui = gui; g_offset.x = x; g_offset.y = y; g_offset.n++; }
static void Scale(uint64_t gui, float x, float y) { g_scale.gui = gui; g_scale.x = x; g_scale.y = y; g_scale.n++; }
static int g_runs;
static uint64_t Original(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{
    g_runs++;
    return a + b + c + d + e + f + g + h;
}

/* A job as the queue holds it: 0x0D's carries its own gui transform (offset 0, scale 1; game 0xF532C5..0xF5330F). */
static void Job(uint8_t* job, uint64_t camera, uint64_t generator)
{
    memset(job, 0, 0x1D0);
    const float zero[2] = {0, 0}, one[2] = {1, 1};
    uint64_t gui = GUI;
    memcpy(job + 0x08, &camera, 8); memcpy(job + 0x10, &generator, 8);
    job[0x38] = 1; job[0x39] = 1;
    memcpy(job + 0x40, &gui, 8); memcpy(job + 0x48, zero, 8); memcpy(job + 0x50, one, 8);
}
static void Consume(uint64_t camera, uint64_t generator)
{
    uint8_t job[0x1D0];
    Job(job, camera, generator);
    memset(&g_offset, 0, sizeof g_offset); memset(&g_scale, 0, sizeof g_scale);
    CoXlTestNearJob(job, camera);
}
static int LevelPixels(void) { return g_offset.n == 1 && g_offset.x == 0 && g_offset.y == 0 && g_scale.n == 1 && g_scale.x == 1 && g_scale.y == 1; }
static int TilePixels(void) { return g_offset.n == 1 && g_offset.x == OX && g_offset.y == OY && g_scale.n == 1 && g_scale.x == SCALE && g_scale.y == SCALE; }

/* The test's dispatcher: 0x0D queues its route-distance job and the consumer takes it at once. */
static uint32_t g_dispatched;
static int g_published, g_publishedAtDispatch; /* the tile's names published (the step's bracket) */
static void Publish(uint32_t name, uint64_t resource) { (void)name; (void)resource; g_published = 1; }
static void Unpublish(uint32_t name) { (void)name; g_published = 0; }
static LONG g_armedAtDispatch;
static int g_consume;
static uint64_t Dispatch(uint64_t record, uint64_t context, uint64_t index, uint64_t d, uint64_t phase, uint64_t f, uint64_t g, uint64_t h)
{
    (void)context; (void)d; (void)phase; (void)f; (void)g; (void)h;
    memcpy(&g_dispatched, (const void*)(uintptr_t)record, 4);
    g_armedAtDispatch = CoXlTestNearRouteArmed();
    g_publishedAtDispatch = g_published;
    if (g_consume) Consume(CAM0, ROUTES);
    *(volatile uint32_t*)(uintptr_t)index = 1;
    return 0;
}

static void Setup(const char* name)
{
    static int made;
    g_case = name;
    if (!made)
    {
        made = 1;
        g_coApi = fake_host_reset(CO_GAME_BUILD);
        g_coMod = (HfMod*)&g_fake;
        uint8_t* image = g_fake.image;
        CoXlTestGame((uintptr_t)image);
        uint64_t api = (uint64_t)(uintptr_t)(image + API), table = (uint64_t)(uintptr_t)(image + TABLE);
        memcpy(image + 0x3326308u, &api, 8);
        memcpy(image + API + 0xD0, &table, 8);
        void (*offset)(uint64_t, float, float) = Offset; memcpy(image + TABLE + 0x258, &offset, 8);
        void (*scale)(uint64_t, float, float) = Scale; memcpy(image + TABLE + 0x270, &scale, 8);
        uint64_t names = (uint64_t)(uintptr_t)(image + NAMES);
        memcpy(image + API + 0x10, &names, 8);
        void (*publish)(uint32_t, uint64_t) = Publish; memcpy(image + NAMES + 0x210, &publish, 8);
        void (*unpublish)(uint32_t) = Unpublish; memcpy(image + NAMES + 0x218, &unpublish, 8);
        g_records = calloc(sizeof kOps, 0x19B0);
        for (size_t i = 0; i < sizeof kOps; i++) memcpy(g_records + i * 0x19B0, &(uint32_t){kOps[i]}, 4);
        uint64_t gen = (uint64_t)(uintptr_t)g_R; /* [level+0x2C8]: only read as there */
        memcpy(g_level + 0x2C8, &gen, 8);
    }
    uint64_t routes = ROUTES, heights = HEIGHTS;
    memcpy(g_R + 0x58, &routes, 8); memcpy(g_R + 0x28, &heights, 8);
    CoXlTestNearMerge((uint64_t)(uintptr_t)g_R, CAM0, CAM1, GUI, SCALE, OX, OY, (void*)Original);
    g_dispatched = 0; g_armedAtDispatch = -1; g_consume = 1; g_runs = 0;
}

static uint8_t g_kinds[48], g_ops[48];
static uint32_t g_recs[48];
static int Plan(uint32_t at) { return CoXlTestNearPlan((uint64_t)(uintptr_t)g_records, at, g_kinds, g_ops, g_recs, 48); }
static int StepOf(int kind, uint8_t op, int n) { for (int s = 0; s < n; s++) if (g_kinds[s] == kind && g_ops[s] == op) return s; return -1; }

int main(void)
{
    Setup("the plan: the level's 0x0D right before 0x3D, the tile's own 0x0D after it");
    int n = Plan(AT65);
    CHECK(n == 10);
    int level = StepOf(LEVEL, 0x0D, n), zones = StepOf(RECORD, 0x3D, n), own = StepOf(RECORD, 0x0D, n);
    CHECK(level >= 0 && zones == level + 1 && own == zones + 1);
    CHECK(level >= 0 && g_recs[level] == AT0D);
    CHECK(zones >= 0 && g_recs[zones] == AT3D);
    CHECK(own >= 0 && g_recs[own] == AT0D);
    static const uint8_t order[10] = {0x39, 0x3C, 0x40, 0x0D, 0x3D, 0x0D, 0x63, 0x42, 0x41, 0x64};
    for (int s = 0; s < n && s < 10; s++) CHECK(g_ops[s] == order[s]);
    int levels = 0;
    for (int s = 0; s < n; s++) levels += g_kinds[s] == LEVEL;
    CHECK(levels == 1);

    Setup("records that end before 0x0D: no plan, as before");
    CHECK(Plan(AT0D) == -1);

    Setup("the level's 0x0D: armed before its record runs, its job in the level's pixels, then disarmed");
    n = Plan(AT65); level = StepOf(LEVEL, 0x0D, n);
    CHECK(CoXlTestNearRunStep((uintptr_t)g_level, (uint64_t)(uintptr_t)g_records, level, (void*)Dispatch) == 1);
    CHECK(g_dispatched == 0x0D);
    CHECK(g_armedAtDispatch == 1);
    CHECK(LevelPixels());
    CHECK(g_offset.gui == GUI && g_scale.gui == GUI && g_runs == 1);
    CHECK(CoXlTestNearRouteArmed() == 0);

    Setup("then 0x3D's job and the tile's own 0x0D job: the tile's pixels");
    Consume(CAM0, HEIGHTS);
    CHECK(TilePixels());
    Consume(CAM0, ROUTES);
    CHECK(TilePixels());

    Setup("a job on another generator while armed: the tile's pixels, still armed for the route job");
    n = Plan(AT65); level = StepOf(LEVEL, 0x0D, n);
    g_consume = 0;
    CHECK(CoXlTestNearRunStep((uintptr_t)g_level, (uint64_t)(uintptr_t)g_records, level, (void*)Dispatch) == 1);
    CHECK(CoXlTestNearRouteArmed() == 1);
    Consume(CAM0, HEIGHTS);
    CHECK(TilePixels());
    CHECK(CoXlTestNearRouteArmed() == 1);
    Consume(CAM0, ROUTES);
    CHECK(LevelPixels());
    CHECK(CoXlTestNearRouteArmed() == 0);

    Setup("a new plan drops an armed route job a failed replay left");
    n = Plan(AT65); level = StepOf(LEVEL, 0x0D, n);
    g_consume = 0;
    CoXlTestNearRunStep((uintptr_t)g_level, (uint64_t)(uintptr_t)g_records, level, (void*)Dispatch);
    CHECK(CoXlTestNearRouteArmed() == 1);
    CHECK(Plan(AT65) == 10);
    CHECK(CoXlTestNearRouteArmed() == 0);
    Consume(CAM0, ROUTES);
    CHECK(TilePixels());

    Setup("no route-distance generator in the merge: the step is skipped, nothing armed");
    n = Plan(AT65); level = StepOf(LEVEL, 0x0D, n);
    memset(g_R + 0x58, 0, 8);
    CHECK(CoXlTestNearRunStep((uintptr_t)g_level, (uint64_t)(uintptr_t)g_records, level, (void*)Dispatch) == 1);
    CHECK(g_dispatched == 0);
    CHECK(CoXlTestNearRouteArmed() == 0);

    Setup("the finalize's step (0x64) asks for the history reset; its jobs then let it go out");
    n = Plan(AT65);
    int finalize = StepOf(RECORD, 0x64, n);
    CoXlTestNearReset(0, 1);
    CHECK(finalize >= 0 && CoXlTestNearRunStep((uintptr_t)g_level, (uint64_t)(uintptr_t)g_records, finalize, (void*)Dispatch) == 1);
    CHECK(g_dispatched == 0x64 && CoXlTestNearResetState() == 1);

    Setup("a step with the tile's textures made: its dispatch sees the tile's names, the level's after");
    n = Plan(AT65); zones = StepOf(RECORD, 0x3D, n);
    CoXlTestNearTextures(0x1000, 0x2000);
    g_publishedAtDispatch = 0;
    CHECK(CoXlTestNearRunStep((uintptr_t)g_level, (uint64_t)(uintptr_t)g_records, zones, (void*)Dispatch) == 1);
    CHECK(g_publishedAtDispatch == 1 && g_published == 0);
    CoXlTestNearTextures(0, 0);

    Setup("another op's step asks for nothing");
    n = Plan(AT65); zones = StepOf(RECORD, 0x3D, n);
    CoXlTestNearReset(0, 1);
    CHECK(CoXlTestNearRunStep((uintptr_t)g_level, (uint64_t)(uintptr_t)g_records, zones, (void*)Dispatch) == 1);
    CHECK(g_dispatched == 0x3D && CoXlTestNearResetState() == 0);
    CoXlTestNearReset(0, 1);

    CoXlTestNearReplaying(0);
    if (g_failed) { printf("zoneroute: %d check(s) failed\n", g_failed); return 1; }
    printf("zoneroute: all checks passed\n");
    return 0;
}
