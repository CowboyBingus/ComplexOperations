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
void CoXlTestNearProbeCounts(uint32_t* replay, uint32_t* tile);
void CoXlTestNearProbeClear(void);
LONG CoXlTestNearResetState(void);
void CoXlTestNearReset(LONG state, int pins);
void CoXlTestNearResetTick(void);
void CoXlTestNearResetDone(LONG done);
void CoXlTestNearAtmosMapsEarlier(void);
void CoXlTestNearResetEarlier(void);
LONG CoXlTestNearNamesSky(void);
void CoXlTestNearCraterMaps(uint64_t source, uint64_t offset, uint64_t scorch);
int CoXlTestNearCraterMapsMake(int newLevel);
void CoXlTestNearCraterMapsEarlier(void);
void CoXlTestNearAtmosMaps(uint64_t color, uint64_t transmittance);
int CoXlTestNearAtmosMapsMake(int newLevel);
void CoXlTestNearReplayEnd(void);
void CoXlTestNearConsumeQueue(uint64_t queue, void* original);
uint64_t CoXlTestNearNamesQueue(void);
uint32_t CoXlTestNearProbeOutside(void);

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
#define SOURCE 0xFDF5405Bu /* the craters' maps: generated_heightmap_source, _offset, generated_materials_scorch */
#define OFFSET 0x4758A80Au
#define SCORCH 0xB1AA630Du
#define SKY 0x5E058EEFu   /* the sky LUTs: atmospheric_scattering_color, _transmittance */
#define SKYT 0xEA1A2316u
#define SKY0 0x5800ull
#define SKY1 0x5900ull
#define STAND0 0x5000ull
#define STAND1 0x6000ull
#define STAND2 0x7000ull
#define FRAMES 0x10C4u      /* G+: frames of frame_history_invalidation */
#define PROBE_ALL 0x7938494u /* G+: a six-face probe capture asked for */
#define CUT 0x78AC014u      /* G+: the camera cut */
#define PROBE_WEIGHT 0x7938498u
#define PROBE_FACES 0x793849Cu
#define PROBE_SKIP 0x79384A2u /* G+: the next render makes no probe capture */

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
static uint64_t g_craterSeen[3]; /* what the last render call saw under SOURCE, OFFSET, SCORCH */
static uint64_t g_skySeen[2];    /* and under SKY, SKYT */
static void See(void)
{
    g_seen[0] = *Slot(HEIGHT); g_seen[1] = *Slot(MATERIALS); g_seen[2] = *Slot(ROADS); g_runs++;
    g_craterSeen[0] = *Slot(SOURCE); g_craterSeen[1] = *Slot(OFFSET); g_craterSeen[2] = *Slot(SCORCH);
    g_skySeen[0] = *Slot(SKY); g_skySeen[1] = *Slot(SKYT);
}
/* The render config's textures as find returns them ({+8 format, +0x14 width, +0x18 height}) and what create made. */
static uint8_t g_configs[5][0x70]; /* the craters' three, then the sky LUTs (+0x1C depth) */
static int g_creates;
static uint32_t g_made[8][4]; /* w, h, mips, format */
static uint64_t Find(uint32_t name)
{
    const uint32_t names[5] = {SOURCE, OFFSET, SCORCH, SKY, SKYT};
    for (int k = 0; k < 5; k++) if (name == names[k]) return (uint64_t)(uintptr_t)g_configs[k];
    return 0;
}
static uint64_t Create(uint32_t w, uint32_t h, uint32_t mips, uint32_t format, uint32_t id, uint32_t flags)
{
    (void)id; (void)flags;
    if (g_creates < 8) { g_made[g_creates][0] = w; g_made[g_creates][1] = h; g_made[g_creates][2] = mips; g_made[g_creates][3] = format; }
    return 0x9000ull + (uint64_t)(g_creates++);
}
static int g_releases;
static void Release(uint64_t resource) { (void)resource; g_releases++; }
static void Configs(uint32_t side)
{
    const uint32_t formats[3] = {22, 31, 51};
    for (int k = 0; k < 3; k++) { memset(g_configs[k], 0, sizeof g_configs[k]); memcpy(g_configs[k] + 8, &formats[k], 4); memcpy(g_configs[k] + 0x14, &side, 4); memcpy(g_configs[k] + 0x18, &side, 4); }
}
static void SkyConfigs(uint32_t side, uint32_t depth)
{
    const uint32_t formats[2] = {10, 24};
    for (int k = 3; k < 5; k++)
    {
        memset(g_configs[k], 0, sizeof g_configs[k]); memcpy(g_configs[k] + 8, &formats[k - 3], 4);
        memcpy(g_configs[k] + 0x14, &side, 4); memcpy(g_configs[k] + 0x18, &side, 4); memcpy(g_configs[k] + 0x1C, &depth, 4);
    }
}
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
        uint64_t (*find)(uint32_t) = Find; memcpy(image + TABLE + 0x158, &find, 8);
        uint64_t (*create)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = Create; memcpy(image + TABLE + 0x170, &create, 8);
        void (*release)(uint64_t) = Release; memcpy(image + TABLE + 0x198, &release, 8);
        g_G = VirtualAlloc(NULL, PROBE_FACES + 0x1000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(g_G, 0x2000, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(g_G + (PROBE_ALL & ~0xFFFu), 0x1000, MEM_COMMIT, PAGE_READWRITE);
        VirtualAlloc(g_G + (CUT & ~0xFFFu), 0x1000, MEM_COMMIT, PAGE_READWRITE);
        uint64_t G = (uint64_t)(uintptr_t)g_G;
        memcpy(image + 0x3326340u, &G, 8);
    }
    memset(g_names, 0, sizeof g_names);
    *Slot(HEIGHT) = ONEMIP; *Slot(ROADS) = ONEMIP; *Slot(MATERIALS) = 0;
    CoXlTestNearRun(0, replaying, TILE0, TILE1, ONEMIP, (void*)Original); /* the textures and the one-mip copy */
    g_publishes = g_unpublishes = 0; g_runs = 0; g_endInside = 0; g_probeInside = 0;
    memset(g_seen, 0xFF, sizeof g_seen);
    *(uint32_t*)(g_queue + 0x10) = 3;
    CoXlTestNearProbeClear();
    CoXlTestNearReset(0, 1);
    CoXlTestNearCraterMaps(0, 0, 0);
    CoXlTestNearAtmosMaps(0, 0);
    memset(g_craterSeen, 0xFF, sizeof g_craterSeen);
    memset(g_skySeen, 0xFF, sizeof g_skySeen);
}

int main(void)
{
    const uint64_t queue = (uint64_t)(uintptr_t)g_queue;
    uint32_t replay = 0, tile = 0;

    Setup("the player's view while a tile replays: the level's maps, no name touched", 1);
    CHECK(CoXlTestNearRun(0, 1, TILE0, TILE1, ONEMIP, (void*)Original) == 1 + 2 + 3 + 0 + 5 + 6 + 7 + 8);
    CHECK(g_runs == 1 && SawLevel());
    CHECK(g_publishes == 0 && g_unpublishes == 0);

    Setup("the consumer on the replay's queue: the tile's maps inside, the level's after", 1);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(g_runs == 1 && SawTile());
    CHECK(LevelNow());
    CHECK(g_publishes == 3 + 2 && g_unpublishes == 3); /* the tile's three, then the one-mip copy's two */

    Setup("the craters' maps: stand-ins inside the consumer, the render config's own after", 1);
    CoXlTestNearCraterMaps(STAND0, STAND1, STAND2);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawTile() && g_craterSeen[0] == STAND0 && g_craterSeen[1] == STAND1 && g_craterSeen[2] == STAND2);
    CHECK(LevelNow() && *Slot(SOURCE) == 0 && *Slot(OFFSET) == 0 && *Slot(SCORCH) == 0);
    CHECK(g_publishes == 6 + 2 && g_unpublishes == 6);

    Setup("no stand-ins made: the craters' names are not touched", 1);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(g_craterSeen[0] == 0 && g_craterSeen[1] == 0 && g_craterSeen[2] == 0);
    CHECK(g_publishes == 3 + 2 && g_unpublishes == 3);

    Setup("an earlier level's stand-ins are not published", 1);
    CoXlTestNearCraterMaps(STAND0, STAND1, STAND2);
    CoXlTestNearCraterMapsEarlier();
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawTile() && g_craterSeen[0] == 0 && g_craterSeen[1] == 0 && g_craterSeen[2] == 0);

    Setup("the stand-ins are made once a level, as the render config's maps", 1);
    Configs(2560); g_creates = 0; g_releases = 0;
    CHECK(CoXlTestNearCraterMapsMake(1) == 1);
    CHECK(g_creates == 3);
    CHECK(g_made[0][0] == 2560 && g_made[0][1] == 2560 && g_made[0][2] == 1 && g_made[0][3] == 22);
    CHECK(g_made[1][3] == 31 && g_made[2][3] == 51);
    CHECK(CoXlTestNearCraterMapsMake(0) == 1 && g_creates == 3); /* the same level: kept */
    CHECK(CoXlTestNearCraterMapsMake(1) == 1 && g_creates == 6 && g_releases == 3); /* a new level: the old ones go */

    Setup("a crater map of another size: no stand-ins, no tile", 1);
    Configs(2048); g_creates = 0;
    CHECK(CoXlTestNearCraterMapsMake(1) == 0);
    CHECK(g_creates == 0);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(g_craterSeen[0] == 0);

    Setup("the sky LUTs: stand-ins inside the consumer, the render config's own after", 1);
    CoXlTestNearAtmosMaps(SKY0, SKY1);
    {
        LONG sky = CoXlTestNearNamesSky();
        CoXlTestNearConsume(queue, 1, (void*)Consumer);
        CHECK(CoXlTestNearNamesSky() == sky + 1);
        CoXlTestNearAtmosMaps(SKY0, 0); /* one stand-in is not a kept LUT pair */
        *(uint32_t*)(g_queue + 0x10) = 3;
        CoXlTestNearConsume(queue, 1, (void*)Consumer);
        CHECK(CoXlTestNearNamesSky() == sky + 1);
        CoXlTestNearAtmosMaps(SKY0, SKY1);
        memset(g_skySeen, 0xFF, sizeof g_skySeen); *(uint32_t*)(g_queue + 0x10) = 3;
        CoXlTestNearConsume(queue, 1, (void*)Consumer);
        CHECK(CoXlTestNearNamesSky() == sky + 2);
    }
    CHECK(SawTile() && g_skySeen[0] == SKY0 && g_skySeen[1] == SKY1);
    CHECK(*Slot(SKY) == 0 && *Slot(SKYT) == 0 && LevelNow());

    Setup("the sky LUT stand-ins: once a level, 2D at the LUT's side and format", 1);
    SkyConfigs(32, 96); g_creates = 0; g_releases = 0;
    CHECK(CoXlTestNearAtmosMapsMake(1) == 2 && g_creates == 2);
    CHECK(g_made[0][0] == 32 && g_made[0][1] == 32 && g_made[0][2] == 1 && g_made[0][3] == 10 && g_made[1][3] == 24);
    CHECK(fake_host_logged("sky LUT stand-in 0 for 0x5E058EEF: 0x9000, 32 x 32 of format 10, flags 0 (the LUT: 32 x 32 x 96, bytes +0x60 0, +0x63 0, +0x64 0)"));
    CHECK(fake_host_logged("sky LUT stand-in 1 for 0xEA1A2316: 0x9001, 32 x 32 of format 24"));
    CHECK(CoXlTestNearAtmosMapsMake(0) == 2 && g_creates == 2);
    CHECK(CoXlTestNearAtmosMapsMake(1) == 2 && g_creates == 4 && g_releases == 2);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(g_skySeen[0] == 0x9002ull && g_skySeen[1] == 0x9003ull && *Slot(SKY) == 0);

    Setup("a sky LUT of another shape: no stand-in, the tile's other names as before", 1);
    SkyConfigs(32, 64); g_creates = 0;
    CHECK(CoXlTestNearAtmosMapsMake(1) == 0 && g_creates == 0);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawTile() && g_skySeen[0] == 0 && g_skySeen[1] == 0);
    CHECK(fake_host_logged("is not the reviewed one (32 x 32 x 64)"));

    Setup("after the replay's end its queue is forgotten: a later drain opens nothing and asks for no reset", 1);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(CoXlTestNearNamesQueue() == queue);
    CoXlTestNearReplayEnd();
    CHECK(CoXlTestNearNamesQueue() == 0 && LevelNow());
    CHECK(fake_host_logged("brackets had both sky LUT stand-ins published"));
    CoXlTestNearReplaying(1); /* even with a next replay under way before its own start */
    CoXlTestNearReset(1, 1);
    g_publishes = 0; *(uint32_t*)(g_queue + 0x10) = 3;
    CoXlTestNearConsumeQueue(queue, (void*)Consumer);
    CHECK(SawLevel() && g_publishes == 0 && CoXlTestNearResetState() == 1);
    CoXlTestNearReplaying(0);

    Setup("a replay that fails with its finalize queued keeps the queue: the job's drain asks for the long reset", 1);
    CoXlTestNearAtmosMaps(SKY0, SKY1);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CoXlTestNearReset(1, 1);
    CoXlTestNearReplayEnd();
    CHECK(CoXlTestNearNamesQueue() == queue && CoXlTestNearResetState() == 1);
    g_publishes = 0; *(uint32_t*)(g_queue + 0x10) = 0;
    CoXlTestNearConsumeQueue(queue, (void*)Consumer); /* an empty drain is not the job's */
    CHECK(CoXlTestNearResetState() == 1 && CoXlTestNearNamesQueue() == queue);
    *(uint32_t*)(g_queue + 0x10) = 1; memset(g_skySeen, 0xFF, sizeof g_skySeen);
    CoXlTestNearConsumeQueue(queue, (void*)Consumer);
    CHECK(SawLevel() && g_publishes == 0 && g_skySeen[0] == 0); /* under the level's names: the real LUTs */
    CHECK(CoXlTestNearResetState() == 2 && CoXlTestNearNamesQueue() == 0);
    SetFrames(0);
    CoXlTestNearResetTick();
    CHECK(Frames() == 8); /* the stand-ins exist, but this clear went to the LUTs */
    CHECK(fake_host_logged("the sky LUTs were cleared: its jobs ran outside the replay"));
    *(uint32_t*)(g_queue + 0x10) = 1;
    CoXlTestNearConsumeQueue(queue, (void*)Consumer);
    CHECK(CoXlTestNearResetState() == 0); /* once */

    Setup("the next replay starts before the queued finalize drained: its jobs run in that replay's bracket, short reset", 1);
    CoXlTestNearAtmosMaps(SKY0, SKY1);
    CoXlTestNearReset(1, 1);
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(SawTile() && g_skySeen[0] == SKY0 && CoXlTestNearResetState() == 2 && CoXlTestNearNamesQueue() == queue);
    SetFrames(0);
    CoXlTestNearResetTick();
    CHECK(Frames() == 3);

    Setup("a finalize queued in the level before: its level's end dropped the job, no reset in this level", 1);
    CoXlTestNearReset(1, 1);
    CoXlTestNearResetEarlier();
    CoXlTestNearReplayEnd();
    *(uint32_t*)(g_queue + 0x10) = 2;
    CoXlTestNearConsumeQueue(queue, (void*)Consumer);
    CHECK(CoXlTestNearResetState() == 0 && CoXlTestNearNamesQueue() == 0);
    SetFrames(0); g_G[CUT] = 0;
    CoXlTestNearResetTick();
    CHECK(Frames() == 0 && g_G[CUT] == 0);

    Setup("a capture with the tile's names out and no replay running is counted, then noted at the replay's end", 1);
    {
        int opened = CoXlTestNearNamesOpen();
        CHECK(opened == 1);
        CoXlTestNearReplaying(0);
        CoXlTestNearProbe((void*)Probe);
        CHECK(CoXlTestNearProbeOutside() == 1);
        CoXlTestNearNamesClose(opened);
        CoXlTestNearProbe((void*)Probe);
        CHECK(CoXlTestNearProbeOutside() == 1);
        CoXlTestNearReplayEnd();
        CHECK(CoXlTestNearProbeOutside() == 0 && fake_host_logged("1 captures saw them with no replay running"));
    }

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
    CHECK(CoXlTestNearResetState() == 0); /* a replay's end asks for no reset of its own: only a finalize's jobs do */

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
    CoXlTestNearProbeCounts(&replay, &tile);
    CHECK(replay == 2 && tile == 0);

    Setup("a probe capture inside the bracket is counted as the tile's", 1);
    g_probeInside = 1;
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CoXlTestNearProbeCounts(&replay, &tile);
    CHECK(replay == 1 && tile == 1);

    Setup("runs of captures inside the bracket: the first four of a replay noted, every one counted", 1);
    g_probeInside = 1;
    const size_t logFrom = strlen(g_fake.log);
    for (int k = 0; k < 7; k++)
    {
        *(uint32_t*)(g_queue + 0x10) = 3;
        CoXlTestNearConsume(queue, 1, (void*)Consumer);
        CoXlTestNearProbe((void*)Probe); /* outside: ends the run */
    }
    CoXlTestNearProbeCounts(&replay, &tile);
    CHECK(replay == 14 && tile == 7);
    {
        int notes = 0;
        for (const char* at = g_fake.log + logFrom; (at = strstr(at, "captures saw the moving tile's maps")) != NULL; at++) notes++;
        CHECK(notes == 4);
        CHECK(fake_host_logged("later runs of this replay are only counted"));
    }

    Setup("a finalize's jobs run: the reset is asked for, then applied once on the game thread", 1);
    SetFrames(0);
    memset(g_G + PROBE_ALL, 0, 12); g_G[CUT] = 0; g_G[PROBE_SKIP] = 0;
    uint32_t faces = 5; memcpy(g_G + PROBE_FACES, &faces, 4);
    float weight = 0;
    CoXlTestNearReset(1, 1);
    CoXlTestNearResetTick(); /* queued, its jobs not run yet: a reset now would be undone by the clears */
    CHECK(Frames() == 0 && g_G[PROBE_ALL] == 0 && g_G[PROBE_SKIP] == 0 && g_G[CUT] == 0 && CoXlTestNearResetState() == 1);
    *(uint32_t*)(g_queue + 0x10) = 3;
    CoXlTestNearConsume(queue, 1, (void*)Consumer);
    CHECK(CoXlTestNearResetState() == 2);
    /* the consumer writes nothing itself: the probe update that sends the weight runs after the level update */
    memcpy(&weight, g_G + PROBE_WEIGHT, 4); memcpy(&faces, g_G + PROBE_FACES, 4);
    CHECK(weight == 0.0f && faces == 5 && g_G[PROBE_ALL] == 0 && g_G[PROBE_SKIP] == 0 && g_G[CUT] == 0 && Frames() == 0);
    CoXlTestNearResetTick();
    memcpy(&weight, g_G + PROBE_WEIGHT, 4); memcpy(&faces, g_G + PROBE_FACES, 4);
    /* the game's own sequence in one tick: this frame's capture skipped, six faces at weight 1 owed to the next */
    CHECK(Frames() == 8 && g_G[CUT] == 1 && g_G[PROBE_SKIP] == 1 && g_G[PROBE_ALL] == 1 && weight == 1.0f && faces == 0);
    CHECK(g_G[PROBE_SKIP - 1] == 0 && g_G[PROBE_SKIP + 1] == 0);
    CHECK(fake_host_logged("after a replayed finalize: 8 frames of history invalidation"));
    CHECK(fake_host_logged("probe capture skipped for one frame, then six faces at weight 1; the sky LUTs were cleared"));
    CHECK(CoXlTestNearResetState() == 0);
    SetFrames(0); memset(g_G + PROBE_ALL, 0, 12); g_G[CUT] = 0; g_G[PROBE_SKIP] = 0;
    *(uint32_t*)(g_queue + 0x10) = 0;
    CoXlTestNearConsume(queue, 0, (void*)Consumer);
    CoXlTestNearResetTick(); /* once: later level updates and consumer calls write nothing */
    CHECK(Frames() == 0 && g_G[PROBE_ALL] == 0 && g_G[PROBE_SKIP] == 0 && g_G[CUT] == 0);

    Setup("the sky LUTs kept by their stand-ins: the game's own three frames", 1);
    CoXlTestNearAtmosMaps(SKY0, SKY1);
    CoXlTestNearReset(2, 1);
    SetFrames(0);
    CoXlTestNearResetTick();
    CHECK(Frames() == 3 && g_G[PROBE_ALL] == 1 && g_G[PROBE_SKIP] == 1 && g_G[CUT] == 1);
    CHECK(fake_host_logged("3 frames of history invalidation from this frame, camera cut, probe capture skipped for one frame, then six faces "
        "at weight 1; the sky LUTs were kept by their stand-ins"));
    CoXlTestNearReset(2, 1);
    SetFrames(2);
    CoXlTestNearResetTick();
    CHECK(Frames() == 3); /* a shorter count the game left is raised */

    Setup("one stand-in missing, or stand-ins of the level before: the LUT is cleared, eight frames", 1);
    CoXlTestNearAtmosMaps(SKY0, 0);
    CoXlTestNearReset(2, 1);
    SetFrames(0);
    CoXlTestNearResetTick();
    CHECK(Frames() == 8);
    CoXlTestNearAtmosMaps(0, SKY1);
    CoXlTestNearReset(2, 1);
    SetFrames(0);
    CoXlTestNearResetTick();
    CHECK(Frames() == 8);
    CoXlTestNearAtmosMaps(SKY0, SKY1);
    CoXlTestNearAtmosMapsEarlier();
    CoXlTestNearReset(2, 1);
    SetFrames(0);
    CoXlTestNearResetTick();
    CHECK(Frames() == 8);

    Setup("the reset's line is bounded: the 256th says so, the 257th is applied and not logged", 1);
    CoXlTestNearResetDone(255);
    CoXlTestNearReset(2, 1);
    SetFrames(0);
    {
        size_t from = strlen(g_fake.log);
        CoXlTestNearResetTick();
        CHECK(Frames() == 8 && strstr(g_fake.log + from, "history reset 256 after a replayed finalize") != NULL);
        CHECK(strstr(g_fake.log + from, "; later resets are not logged") != NULL);
        from = strlen(g_fake.log);
        CoXlTestNearReset(2, 1);
        SetFrames(0); g_G[CUT] = 0;
        CoXlTestNearResetTick();
        CHECK(Frames() == 8 && g_G[CUT] == 1 && CoXlTestNearResetState() == 0);
        CHECK(strstr(g_fake.log + from, "history reset") == NULL);
    }
    CoXlTestNearResetDone(0);

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
    SetFrames(0); g_G[PROBE_ALL] = 0; g_G[CUT] = 0; g_G[PROBE_SKIP] = 0;
    { float w0 = 0.5f; memcpy(g_G + PROBE_WEIGHT, &w0, 4); }
    CoXlTestNearResetTick();
    { float w1 = 0; memcpy(&w1, g_G + PROBE_WEIGHT, 4); CHECK(w1 == 0.5f); }
    CHECK(Frames() == 0 && g_G[PROBE_ALL] == 0 && g_G[PROBE_SKIP] == 0 && g_G[CUT] == 0 && CoXlTestNearResetState() == 2);

    CoXlTestNearReset(0, 1);
    CoXlTestNearReplaying(0);
    if (g_failed) { printf("nearnames: %d check(s) failed\n", g_failed); return 1; }
    printf("nearnames: all checks passed\n");
    return 0;
}
