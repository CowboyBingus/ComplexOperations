/* The tile's names while a tile replays (proto/difficulty_world_terrain.c NearNamesOpen, NearConsume, NearRunAt,
 * NearProbe, NearResetTick): generated_heightmap, painted_roads_height and generated_materials hold the level's maps
 * except inside the merge's job consumer while it runs the replay's jobs (and a replay step's dispatch). The light probe
 * and the view, drawn before the consumer in the same render callback, bound the tile's maps under them before and
 * tinted the frame olive. After a replayed finalize the game's history reset is asked for and applied once. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
uint64_t CoXlTestNearRun(int consumer, int active, uint64_t tile0, uint64_t tile1, uint64_t oneMip, void* original);
void CoXlTestNearReplaying(int active);
void CoXlTestNearConsume(uint64_t queue, int replays, void* original);
int CoXlTestNearNamesOpen(void);
void CoXlTestNearNamesClose(int opened);
uint64_t CoXlTestNearProbe(void* original);
void CoXlTestNearProbeCounts(uint32_t* replay, uint32_t* tile, uint32_t* other);
void CoXlTestNearProbeClear(void);
void CoXlTestFarGrassNames(int on);
LONG CoXlTestNearResetState(void);
void CoXlTestNearReset(LONG state, int pins);
void CoXlTestNearResetTick(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define API 0x3700000u   /* free room in the fake image: the api and its +0x10 table */
#define TABLE 0x3700100u
#define HEIGHT 0x2A9E3232u
#define MATERIALS 0x95E7E3ACu
#define ROADS 0xBF45B677u
#define TILE0 0x1000ull
#define TILE1 0x2000ull
#define ONEMIP 0x3000ull
#define FRAMES 0x10C4u      /* G+: frames of frame_history_invalidation */
#define PROBE_ALL 0x7938494u /* G+: a six-face probe capture asked for */
#define PROBE_WEIGHT 0x7938498u
#define PROBE_FACES 0x793849Cu

/* The published-name overlay: a name's resource, 0 when unpublished (the render config's own). */
static struct { uint32_t name; uint64_t resource; } g_names[8];
static int g_publishes, g_unpublishes;
static uint64_t g_seen[3]; /* what the last render call saw under HEIGHT, MATERIALS, ROADS */
static int g_runs, g_endInside, g_probeInside;
static uint8_t g_queue[0x20]; /* a job queue: +0x10 the jobs it holds */
static uint8_t* g_G;          /* G, [game+0x3326340]: reserved, its two pages of fields committed */

static uint64_t* Slot(uint32_t name)
{
    for (int k = 0; k < 8; k++) if (g_names[k].name == name) return &g_names[k].resource;
    for (int k = 0; k < 8; k++) if (g_names[k].name == 0) { g_names[k].name = name; return &g_names[k].resource; }
    return NULL;
}
static void Publish(uint32_t name, uint64_t resource) { *Slot(name) = resource; g_publishes++; }
static void Unpublish(uint32_t name) { *Slot(name) = 0; g_unpublishes++; }
static void See(void) { g_seen[0] = *Slot(HEIGHT); g_seen[1] = *Slot(MATERIALS); g_seen[2] = *Slot(ROADS); g_runs++; }
static int SawLevel(void) { return g_seen[0] == ONEMIP && g_seen[2] == ONEMIP && g_seen[1] == 0; }
static int SawTile(void) { return g_seen[0] == TILE0 && g_seen[2] == TILE0 && g_seen[1] == TILE1; }
static int LevelNow(void) { return *Slot(HEIGHT) == ONEMIP && *Slot(ROADS) == ONEMIP && *Slot(MATERIALS) == 0; }
static int32_t Frames(void) { int32_t f; memcpy(&f, g_G + FRAMES, 4); return f; }
static void SetFrames(int32_t f) { memcpy(g_G + FRAMES, &f, 4); }

static uint64_t Original(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{
    See();
    return a + b + c + d + e + f + g + h;
}
static uint64_t Probe(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h, uint64_t i)
{
    See();
    return a + b + c + d + e + f + g + h + i;
}
static void Consumer(uint64_t queue)
{
    See();
    if (g_probeInside) CoXlTestNearProbe((void*)Probe);
    if (g_endInside) CoXlTestNearReplaying(0); /* the replay's end inside the call (NearReplayEnd puts the level's back itself) */
    *(uint32_t*)(uintptr_t)(queue + 0x10) = 0;
}

/* The names as play has them; a tile replaying (`replaying`) with its textures made, or none. */
static void Setup(const char* name, int replaying)
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
        memcpy(image + API + 0x10, &table, 8);
        void (*publish)(uint32_t, uint64_t) = Publish; memcpy(image + TABLE + 0x210, &publish, 8);
        void (*unpublish)(uint32_t) = Unpublish; memcpy(image + TABLE + 0x218, &unpublish, 8);
        g_G = VirtualAlloc(NULL, PROBE_FACES + 0x1000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(g_G, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(g_G + (PROBE_ALL & ~0xFFFu), 0x1000, MEM_COMMIT, PAGE_READWRITE);
        uint64_t G = (uint64_t)(uintptr_t)g_G;
        memcpy(image + 0x3326340u, &G, 8);
    }
    memset(g_names, 0, sizeof g_names);
    *Slot(HEIGHT) = ONEMIP; *Slot(ROADS) = ONEMIP; *Slot(MATERIALS) = 0;
    CoXlTestNearRun(0, replaying, TILE0, TILE1, ONEMIP, (void*)Original); /* the textures and the one-mip copy */
    g_publishes = g_unpublishes = 0; g_runs = 0; g_endInside = 0; g_probeInside = 0;
    memset(g_seen, 0xFF, sizeof g_seen);
    *(uint32_t*)(g_queue + 0x10) = 3;
    CoXlTestFarGrassNames(0);
    CoXlTestNearProbeClear();
    CoXlTestNearReset(0, 1);
}

int main(void)
{
    const uint64_t queue = (uint64_t)(uintptr_t)g_queue;
    uint32_t replay = 0, tile = 0, other = 0;

    Setup("the player's view while a tile replays: the level's maps, no name touched", 1);
    CHECK(CoXlTestNearRun(0, 1, TILE0, TILE1, ONEMIP, (void*)Original) == 1 + 2 + 3 + 0 + 5 + 6 + 7 + 8);
    CHECK(g_runs == 1 && SawLevel());
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("the consumer on the replay's queue: the tile's maps inside, the level's after", 1);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(g_runs == 1 && SawTile());
    CHECK(LevelNow());
    CHECK(g_publishes == 3 + 2 && g_unpublishes == 3); /* the tile's three, then the one-mip copy's two */

    Setup("the consumer on another queue: no name touched", 1);
    CoXlTestNearConsume(queue, 0, (void*)Consumer);
    CHECK(g_runs == 1 && SawLevel());
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("the replay's queue empty: no name touched", 1);
    *(uint32_t*)(g_queue + 0x10) = 0;
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(g_runs == 1 && SawLevel());
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("no replay: the consumer runs with the level's maps", 0);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawLevel());
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("a replay with no textures yet: nothing published", 1);
    CoXlTestNearRun(0, 1, 0, 0, ONEMIP, (void*)Original);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawLevel());
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("a step's bracket around the consumer: the tile's maps until the outer one closes", 1);
    int opened = CoXlTestNearNamesOpen();
    CHECK(opened == 1);
    CHECK(*Slot(HEIGHT) == TILE0 && *Slot(MATERIALS) == TILE1 && *Slot(ROADS) == TILE0);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawTile());
    CHECK(*Slot(HEIGHT) == TILE0 && *Slot(MATERIALS) == TILE1); /* the inner close keeps them */
    CHECK(g_publishes == 3 && g_unpublishes == 0);
    CoXlTestNearNamesClose(opened);
    CHECK(LevelNow());

    Setup("the replay ends inside the consumer: the level's maps after", 1);
    g_endInside = 1;
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawTile());
    CHECK(LevelNow());

    Setup("one frame of a build: probe, view, consumer, probe", 1);
    CoXlTestNearProbe((void*)Probe);
    CHECK(SawLevel());
    CoXlTestNearRun(0, 1, TILE0, TILE1, ONEMIP, (void*)Original);
    CHECK(SawLevel());
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawTile());
    *(uint32_t*)(g_queue + 0x10) = 3;
    CHECK(CoXlTestNearProbe((void*)Probe) == 45);
    CHECK(SawLevel() && g_runs == 4);
    CoXlTestNearProbeCounts(&replay, &tile, &other);
    CHECK(replay == 2 && tile == 0 && other == 0);

    Setup("a probe capture inside the bracket is counted as the tile's", 1);
    g_probeInside = 1;
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CoXlTestNearProbeCounts(&replay, &tile, &other);
    CHECK(replay == 1 && tile == 1 && other == 1);

    Setup("far grass's textures published: its captures are counted, not the replay's", 0);
    CoXlTestFarGrassNames(1);
    CoXlTestNearProbe((void*)Probe);
    CoXlTestNearProbe((void*)Probe);
    CoXlTestFarGrassNames(0);
    CoXlTestNearProbe((void*)Probe);
    CoXlTestNearProbeCounts(&replay, &tile, &other);
    CHECK(replay == 0 && tile == 0 && other == 2);

    Setup("a finalize's jobs run: the reset is asked for, then applied once on the game thread", 1);
    CoXlTestNearReset(1, 1);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(CoXlTestNearResetState() == 2);
    SetFrames(0);
    memset(g_G + PROBE_ALL, 0, 12);
    uint32_t faces = 5; memcpy(g_G + PROBE_FACES, &faces, 4);
    CoXlTestNearResetTick();
    float weight = 0; memcpy(&weight, g_G + PROBE_WEIGHT, 4); memcpy(&faces, g_G + PROBE_FACES, 4);
    CHECK(Frames() == 8 && g_G[PROBE_ALL] == 1 && weight == 1.0f && faces == 0);
    CHECK(CoXlTestNearResetState() == 0);
    SetFrames(0);
    CoXlTestNearResetTick();
    CHECK(Frames() == 0); /* once */

    Setup("a longer reset the game asked for is kept", 1);
    CoXlTestNearReset(2, 1);
    SetFrames(40);
    CoXlTestNearResetTick();
    CHECK(Frames() == 40 && CoXlTestNearResetState() == 0);

    Setup("no finalize queued: the consumer asks for nothing", 1);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(CoXlTestNearResetState() == 0);

    Setup("another queue's jobs do not stand for the finalize's", 1);
    CoXlTestNearReset(1, 1);
    CoXlTestNearConsume(queue, 0, (void*)Consumer);
    CHECK(CoXlTestNearResetState() == 1);

    Setup("the reset's code not the reviewed one: nothing written", 1);
    CoXlTestNearReset(2, -1);
    SetFrames(0); g_G[PROBE_ALL] = 0;
    CoXlTestNearResetTick();
    CHECK(Frames() == 0 && g_G[PROBE_ALL] == 0 && CoXlTestNearResetState() == 2);

    CoXlTestNearReset(0, 1);
    CoXlTestNearReplaying(0);
    if (g_failed) { printf("nearnames: %d check(s) failed\n", g_failed); return 1; }
    printf("nearnames: all checks passed\n");
    return 0;
}
