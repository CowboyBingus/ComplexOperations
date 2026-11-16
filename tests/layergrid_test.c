/* Stamps on the extra large map's moving tiles: a stamp's layer units take their heights from a tile's 1 m grid (the
 * placed units, exe 0xDFA60's inline read) or from the drawn ground (0xD5B10), and a tile that comes or goes changes
 * the ground under stamps that are already standing. Every grid holds the ground as drawn now, a stamp gets the grid of
 * the tile it stands by, the sampler answers while a tile builds, and the streamed stamps on a changed tile's ground go
 * out and come back. Run on the fake game. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_framecost.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestWorldLevel(uintptr_t level, uintptr_t preview);
void CoXlTestGenLevel(float current);
int CoXlTestTileAddAt(float cx, float cy, const uint16_t* heights, uint32_t n, float size);
void CoXlTestTileRemove(int slot);
void CoXlTestCoarse(const uint16_t* heights, uint32_t n, float size);
void CoXlTestNearBuilding(int building);
void CoXlTestLayerTile(int slot, int in, float cx, float cy);
void CoXlTestLayerTileLater(int slot, int in, float cx, float cy);
int CoXlTestLayerBusy(void);
void CoXlTestNearLevelLeft(int ship);
int CoXlTestStreamLayerBusy(void);
int CoXlTestLayerThreadDone(void);
uint32_t CoXlTestLayerFills(void);

/* Two pollers of one finished change, started together. */
static volatile LONG g_go;
static DWORD WINAPI Poller(LPVOID unused)
{
    (void)unused;
    while (!g_go) YieldProcessor();
    CoXlTestLayerBusy();
    return 0;
}
uint64_t CoXlTestLayerField(uint64_t unit, uint64_t field);
int CoXlTestLayerHeight(uint64_t desc, float x, float y, float* h);
void CoXlTestStreamSetup(uintptr_t game, uint8_t* context, uint8_t* array, uint32_t count);
void CoXlTestStreamSlot(uint32_t i, float x, float y, uint64_t unit, int mission);
uint32_t CoXlTestStampsReplace(int slot, float cx, float cy);
uint32_t CoXlTestTileGo(int slot);
void CoXlTestHeightKept(float* data, uint32_t cols, uint32_t rows, float extent);
float CoXlTestVisibleWithNone(float x, float y);
float CoXlTestVisibleAt(float x, float y);
void CoXlTestStreamObserver(int on, float x, float y);
int CoXlTestRespawnRoom(uint32_t i, float distance2);
uint32_t CoXlTestSpawnDue(uint32_t i, float distance2);
int CoXlTestStreamFlag(uint32_t i);
void CoXlTestStreamFlagClear(uint32_t i);
int CoXlTestStreamQueued(uint32_t i);
void CoXlTestStreamScan(void);
uint32_t CoXlTestStreamScanFrames(int frames);
uint32_t CoXlTestStreamDrain(double ms, uint32_t* waiting);
uint32_t CoXlTestStreamDrainMost(uint32_t most, double ms, uint32_t* waiting);
uint32_t CoXlTestStreamChange(unsigned long long now, int layerBusy, unsigned long long* next);
void CoXlTestStreamChangeAt(unsigned long long next);
void CoXlTestStreamExpired(uint32_t* now, uint32_t* most);
uint32_t CoXlTestStreamQueue(uint32_t* slots, uint32_t room);
void CoXlTestStreamStatic(int skip, int changed, uint32_t* frames, uint32_t* moved, uint32_t* quiet, uint32_t* leaves);
void CoXlTestStreamTick(void);
void CoXlTestStreamBelt(uint32_t* gone, uint32_t* looks, long* destroys);
void CoXlTestStreamLooks(uint32_t* bad, uint32_t* fails, int* off);
uint32_t CoXlTestWindowEvictedFar(void);
uint32_t CoXlTestStreamDrainLayer(uint32_t most, double ms, int layer, uint32_t* waiting, int* cut);
uint32_t CoXlTestStreamDrainLeast(uint32_t most, double ms, int layer, uint32_t least, uint32_t* waiting, int* cut);
uint32_t CoXlTestStreamFloorLeast(uint32_t expired, double ownMs);
int CoXlTestStreamDropFarPush(uint32_t i);
int CoXlTestStreamDropGovPush(uint32_t i);
void CoXlTestStreamFarNearRemoved(uint32_t* window, uint32_t* governor);
int CoXlTestStreamDropKind(uint32_t i);
uint32_t CoXlTestStreamFarNearSkipped(void);
double CoXlTestStreamOwn(double ownMs);
void CoXlTestStreamOwnLearn(double ms, uint32_t removed);
double CoXlTestStreamRemoveRoom(void);
unsigned long long CoXlTestStreamRemoveWaitNow(void);
double CoXlTestStreamExitRate(double rate);
int CoXlTestWindowGovernHolds(void);
void CoXlTestStreamGovActs(int on);
int CoXlTestSlChangeMark(void);
uint32_t CoXlTestStreamChangeTick(void);
void CoXlTestSlChangeTickSet(uint32_t tick);
uint32_t CoXlTestStreamSpawnTried(void);
uint32_t CoXlTestStreamLeastNow(void);
void CoXlTestStreamScanAt(unsigned long long at);
void CoXlTestStreamSamples(double* sum, uint32_t* count);
void CoXlTestStreamFloorDue(uint32_t* due, uint32_t* spawned);
int CoXlTestStreamDueInReach(float reach2);
void CoXlTestStreamSpawnedSet(uint32_t i, int spawned);
void CoXlTestStreamDistance(uint32_t i, float distance2);
void CoXlTestStreamFloorMore(uint32_t* stuck, uint32_t* dry, double* rebuiltSum, uint32_t* least, uint32_t* exits, uint32_t* exitChanges);
int CoXlTestStreamFloorStuck(double quietHi, uint32_t leaves, double moveMs);
int CoXlTestRenderApart(int stampLeave, int step, uint8_t op);
void CoXlTestStreamTickNear(int stampLeave, int step, uint8_t op);
void CoXlTestStreamExpiredSet(uint32_t n);
int CoXlTestStreamDropPush(uint32_t i);
void CoXlTestStreamFloorStats(uint32_t* held, double* quietHiSum, uint32_t* streamedMoved, uint32_t* streamedQuiet, double* waitMs, double* waitMost);
void CoXlTestStreamLayerSet(uint32_t i, uint32_t layer);
void CoXlTestStreamChangeStats(uint32_t* changes, uint32_t* backlogs, uint32_t* drainCuts, uint32_t* spawnCuts, uint32_t* removed, uint32_t drains[5]);
void CoXlTestStreamNote(void);
void CoXlTestStreamDue(uint32_t i, uint64_t unit);
void CoXlTestSlReset(void);
void CoXlTestSlAsk(int on);
void CoXlTestStOrig(int slot, void* fn);
void* CoXlTestStThunk(int slot);
void CoXlTestStOn(void);
long long CoXlTestStCalls(int slot);
void CoXlTestStFrame(int kind);
void CoXlTestStNote(void);
uint64_t CoXlTestStKindCalls(int kind, int slot);
uint32_t CoXlTestStKindFrames(int kind);
void CoXlTestSlChain(const uintptr_t* frames, int n, int game, int frame);
void CoXlTestSlSampleText(char* out, size_t room);
void CoXlTestSlSampleCall(void);
/* The step timers' stand-ins: a timed original spinning 50 us, the activation's counted one. */
static int g_stFakeCalls;
static uint64_t StFakeTimed(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    do QueryPerformanceCounter(&t1); while ((t1.QuadPart - t0.QuadPart) * 20000 < f.QuadPart);
    g_stFakeCalls++;
    return a * 1000 + b * 100 + c * 10 + d;
}
static uint64_t StFakeActivate(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { g_stFakeCalls++; return a * b + c + d; }
void CoXlTestSlSet(int on, uintptr_t exe, void* count, void* info, void* rule, void* create);
int CoXlTestSlOn(void);
const char* CoXlTestSlInstall(uintptr_t exe);
const char* CoXlTestSlEarly(uintptr_t exe);
const char* CoXlTestLayersEarly(uintptr_t game, uintptr_t exe);
int CoXlTestSlLayers(void);
void CoXlTestStreamSlNote(void);
void CoXlTestStreamLayersInstall(void);
int CoXlTestSlCount(void);
void CoXlTestSlInfo(uint8_t layer, uint8_t* out);
void CoXlTestSlRule(const uint32_t* ids, int n, const uint8_t* bodies, uint8_t* out);
void* CoXlTestSlCreate(void* writer, uint32_t* id);
uint32_t CoXlTestSlLayerOf(uint32_t index);
void CoXlTestSlFrame(uint32_t layer);
int CoXlTestSlFlushDue(uint64_t world);
int CoXlTestSlLeaves(uint32_t leaves[5]);
void CoXlTestSlPick(const uint32_t leaves[5], const uint32_t queued[5], int due, int* turn, int* drain, uint32_t* spawn);
void CoXlTestSlCounts(long* created, long* marked, long* routed);
void CoXlTestStreamLayersPick(int due, int* drain, uint32_t* spawn, uint32_t* rebuilt);
double CoXlTestStreamBudget(double quietHi, double rebuildMs, double moveMs);
void CoXlTestStreamCost(double removeMs, double spawnMs, double* removeNow, double* spawnNow, int* kind);
void CoXlTestStreamCostKind(int kind);
void CoXlTestStreamCostLearn(void);
uint32_t CoXlTestStreamLeaves(void);
void CoXlTestFarGrassOut(int out);
int CoXlTestStreamRemovePick(unsigned long long now, double lastWhole, int spawnsWait);
void CoXlTestStreamRemoveWait(unsigned long long at, int lastKind);
void CoXlTestStreamPace(uint32_t* removeFrames, uint32_t* forced, uint32_t* spawnFrames, uint32_t* grassHolds, uint32_t* grassForced);
int CoXlTestStreamGovern(unsigned long long now, double* last);
int CoXlTestStreamGovernMay(uint32_t s);
void CoXlTestStreamGovernSet(int on);
void CoXlTestStreamGovernStats(uint32_t* onTurns, uint32_t* offTurns, uint32_t* held, uint32_t* drained, uint32_t* nearKept);
void CoXlTestWindowGovernEvict(void);
void CoXlTestStreamPlayers(int n, const float* xy);
uint32_t CoXlTestStreamGovernHeldRespawns(void);
uint32_t CoXlTestWindowGovernEvictRange(uint32_t base, uint32_t high);
void co_framecost_grass(int out);
void co_framecost_apart(void);
void CoXlTestStreamBudgetStats(double* sum, uint32_t* floors, uint32_t* builds);
uint32_t CoXlTestStreamNextTick(void);
void CoXlTestMoveStepMark(double ms);
void co_framecost_test_reset(int64_t freq);
void co_framecost_test_begin(int64_t now, int play, long mission);
void co_framecost_test_world(int enter, int64_t now);
void co_framecost_bodies(int changed);
int co_framecost_recent(double* quietHi, double* quietWorld, double* lastWorld);
int CoXlTestMoveMarks(double* ms);
void CoXlTestMoveStepAt(uint32_t tick);
uint32_t CoXlTestStreamStaticTile(void);
void CoXlTestStreamMakeEstimate(double perLocation);
uint32_t CoXlTestStreamTicks(void);
void CoXlTestStreamTicksAdd(void);
uint32_t CoXlTestStreamTileDefers(void);
int CoXlTestSlCountFrom(uintptr_t back);
void CoXlTestStreamFlushCost(double ms, uint32_t locations, uint32_t* frames, double* sum, double* most, double* slope, double* fixed);
int CoXlTestStreamFlushLater(uint32_t locations);
int CoXlTestStreamFlushAfter(void);
int CoXlTestStreamFlushPending(void);
void co_shim_install(void);
int co_safe_find(uintptr_t list, size_t count, uint64_t value);
void CoXlTestMission(uintptr_t level, int complex);
void CoXlTestStreamOutSince(uint32_t i, unsigned long long since);
void CoXlTestStreamOut2(uint32_t i, float radius);
uint32_t CoXlTestWindowEvictFar(uint32_t base, uint32_t high);
void CoXlTestStreamSlotState(uint32_t i, int* live, int* due, uint64_t* unit);
void CoXlTestStreamSpawned(uint32_t i, int* spawned, int* due);
int CoXlTestLayerWhy(uint64_t unit, uint64_t field, int* slot);
float CoXlTestTraceGridAt(int slot, float x, float y);
void CoXlTestProbePlayer(int count, float x, float y);
void CoXlTestProbe(char kind, float x, float y, float used, int why);
void CoXlTestProbeCounts(long* reads, long* off, long* written);
uint64_t CoXlTestLayerSpawn(uint64_t unit, uint64_t count, uint64_t field);
uint64_t CoXlTestThunk(int which, float* out, uint64_t* r9, uint64_t* fifth);
int CoXlTestUnwind(uintptr_t site, int pre, uint64_t out[10]);
int CoXlTestUnwindWith(uintptr_t site, uintptr_t handler, int kind, int pre, uint64_t out[10]);
void CoXlTestLayerChosen(void (*fn)(uint64_t));
void CoXlTestGridReady(int slot, LONG ready);
void CoXlTestLayerCounts(long* grids, long* spawns, uint32_t* pending);
float CoXlTestSeen2(uint32_t i);
float CoXlTestSeen2Of(float side);
int CoXlTestThunkUnwind(uint64_t out[2], uint64_t* stackTop);
uint64_t CoXlTestLayerSnap(uint64_t unit, uint64_t field);
void CoXlTestLayerSpawnInstall(uintptr_t exe);
uintptr_t CoXlTestLayerSpawnDetour(void);
uintptr_t CoXlTestLayerSpawnOriginal(void);
int CoXlTestGridEnter(uint64_t desc);
void CoXlTestGridLeave(int k);
const float* CoXlTestHeightCopy(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define SPAN (2304.0f * 4095.0f / 4096.0f) /* a tile's terrain, sample to sample */
#define AX 3074.65625f                     /* the user's drop tile (session 210f2c17) and its west and south cells */
#define AY -4094.875f
#define BX 1026.21875f
#define CY2 -6143.3125f
#define API 0x3600000u
#define TABLE 0x3601000u

static uint16_t U16(float metres) { return (uint16_t)((metres + 500.0f) / 1000.0f * 65535.0f + 0.5f); }
static uint16_t g_coarse[4], g_tileA[4], g_tileB[4], g_tileC[4];
static float g_levelData[4];
static uint8_t g_levelDesc[0x28];
static uint8_t g_unit[0x60];

/* As the game sends the level's layer heightfield: data, origin (the grid's centre), extent, columns, rows. */
static void LevelDesc(void)
{
    memset(g_levelDesc, 0, sizeof g_levelDesc);
    uint64_t data = (uint64_t)(uintptr_t)g_levelData;
    float origin[2] = {0.0f, 0.0f}, extent[2] = {16384.0f, 16384.0f};
    uint32_t dims[2] = {2, 2};
    memcpy(g_levelDesc, &data, 8); memcpy(g_levelDesc + 8, origin, 8); memcpy(g_levelDesc + 0x14, extent, 8); memcpy(g_levelDesc + 0x1C, dims, 8);
}

/* The descriptor a stamp whose level unit stands at (x, y) gets for its placed units (0 = the level's own). */
static uint64_t Field(float x, float y)
{
    float at[2] = {x, y};
    memcpy(g_unit + 0x50, at, 8);
    return CoXlTestLayerField((uint64_t)(uintptr_t)g_unit, (uint64_t)(uintptr_t)g_levelDesc);
}

/* A placed unit's height at (x, y) from a descriptor, as exe 0xDFA60 reads it (0xE059E..0xE0612: nearest sample, the
 * index compared unsigned with the last column / row). */
static float Read(uint64_t desc, float x, float y)
{
    const uint8_t* d = (const uint8_t*)(uintptr_t)desc;
    uint64_t data; float origin[2], extent[2]; uint32_t cols, rows;
    memcpy(&data, d, 8); memcpy(origin, d + 8, 8); memcpy(extent, d + 0x14, 8); memcpy(&cols, d + 0x1C, 4); memcpy(&rows, d + 0x20, 4);
    float u = ((x - origin[0]) / extent[0] + 0.5f) * (float)cols, v = ((y - origin[1]) / extent[1] + 0.5f) * (float)rows;
    uint32_t c = (uint32_t)(int64_t)u, r = (uint32_t)(int64_t)v;
    if (c > cols - 1) c = cols - 1;
    if (r > rows - 1) r = rows - 1;
    return ((const float*)(uintptr_t)data)[(size_t)(rows - 1 - r) * cols + c];
}

static int Near(float a, float b) { return a > b - 0.05f && a < b + 0.05f; }

/* A stamp's level unit for the layer spawn (exe 0xDBEC0): place +0x50, its SpeedTree groups +0x3D0, resource +0x10
 * whose particle effects are counted at +0x48. */
static uint8_t g_spawnUnit[0x400], g_spawnRes[0x100];
static uint64_t SpawnUnit(float x, float y, uint32_t trees, uint32_t effects)
{
    memset(g_spawnUnit, 0, sizeof g_spawnUnit); memset(g_spawnRes, 0, sizeof g_spawnRes);
    float at[2] = {x, y};
    uint64_t res = (uint64_t)(uintptr_t)g_spawnRes;
    memcpy(g_spawnUnit + 0x50, at, 8); memcpy(g_spawnUnit + 0x3D0, &trees, 4); memcpy(g_spawnUnit + 0x10, &res, 8); memcpy(g_spawnRes + 0x48, &effects, 4);
    return (uint64_t)(uintptr_t)g_spawnUnit;
}

/* Hex bytes written into the fake engine image at rva. */
static void Bytes(uint8_t* exe, uint32_t rva, const char* hex)
{
    for (const char* c = hex; *c;)
    {
        if (*c == ' ') { c++; continue; }
        unsigned v = 0; sscanf(c, "%2x", &v); exe[rva++] = (uint8_t)v; c += 2;
    }
}
static void SpawnPins(uint8_t* exe)
{
    Bytes(exe, 0xDBEC0u, "45 85 C0 0F 84 B4 23 00 00 48 8B C4 4C 89 48 20");
    Bytes(exe, 0xDBEE2u, "48 8D A8 C8 F5 FF FF");
    Bytes(exe, 0xDC47Au, "48 8B 85 58 0A 00 00");
    Bytes(exe, 0xDD871u, "48 8B 95 58 0A 00 00");
    Bytes(exe, 0x20BFD3u, "48 8B 8D C8 0A 00 00 48 89 8D A0 00 00 00");
    Bytes(exe, 0x32E63Au, "E8 81 D8 DA FF 48 83 C4 58 C3");
    Bytes(exe, 0x32E69Au, "E8 21 D8 DA FF 48 83 C4 58 C3");
}
/* Where the call at rva now goes: through a `jmp [rip+0]` stub, its target; else the call's own target. */
static uintptr_t CallGoesTo(const uint8_t* exe, uint32_t rva)
{
    int32_t rel; memcpy(&rel, exe + rva + 1, 4);
    const uint8_t* to = exe + rva + 5 + rel;
    if (exe[rva] != 0xE8) return 0;
    if (to >= exe && to < exe + FAKE_EXE_SIZE) return (uintptr_t)to;
    if (to[0] != 0xFF || to[1] != 0x25 || to[2] || to[3] || to[4] || to[5]) return 0;
    uint64_t target; memcpy(&target, to + 6, 8);
    return (uintptr_t)target;
}

static uint8_t* g_lg;
static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    CoXlTestGame((uintptr_t)g_fake.image);
    CoXlTestWorldLevel((uintptr_t)g_lg, 0);
    { const uint32_t play = 3; memcpy(g_lg + 0x1E4, &play, 4); } /* the level generator's state: finished, in play */
    CoXlTestGenLevel(8.0f);
    for (int k = 0; k < 4; k++) { g_coarse[k] = U16(40.0f); g_tileA[k] = U16(45.0f); g_tileB[k] = U16(30.0f); g_tileC[k] = U16(20.0f); }
    CoXlTestCoarse(g_coarse, 2, 16383.5f);
    LevelDesc();
}

/* The streamer's removal on the fake game: the world interface's destroy (+0x388) and the animation wait (+0x410,
 * the reviewed exe 0x3340A0, here a `ret`). */
static int g_destroys;
static uint64_t g_destroyed;
/* The fake world: its unit list (count +0x230588, array [+0x230590]) as DestroyUnit (exe 0x338960) keeps it. A test's
 * unit id is an address in g_unitMem. */
static uint8_t* g_worldMem;
static uint8_t* g_unitMem;
static uint64_t g_listed[512];
#define U(id) ((uint64_t)(uintptr_t)(g_unitMem + (id)))
static uint32_t ListCount(void) { uint32_t n; memcpy(&n, g_worldMem + 0x230588u, 4); return n; }
static void ListSet(uint32_t n) { memcpy(g_worldMem + 0x230588u, &n, 4); }
static void ListAdd(uint64_t unit) { const uint32_t n = ListCount(); g_listed[n] = unit; ListSet(n + 1); }
static int ListTake(uint64_t unit)
{
    const uint32_t n = ListCount();
    uint64_t* list; /* the world's own array: a case may hang another one in */
    memcpy(&list, g_worldMem + 0x230590u, 8);
    for (uint32_t k = 0; k < n; k++)
        if (list[k] == unit) { list[k] = list[n - 1]; ListSet(n - 1); return 1; }
    return 0;
}
static int g_unlisted; /* destroys of a unit the list did not hold: the engine would free it all the same */
static double g_destroySpin; /* ms each destroy takes: a removal's cost, so a drain's time runs out where a case needs */
static void Spin(double ms)
{
    LARGE_INTEGER hz, t0, t; QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    do QueryPerformanceCounter(&t); while ((double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart < ms);
}
static void FakeDestroy(uint64_t world, uint64_t unit, uint64_t a, uint64_t b)
{
    (void)world; (void)a; (void)b;
    if (g_destroySpin > 0.0) Spin(g_destroySpin);
    g_destroys++; g_destroyed = unit;
    if (!ListTake(unit)) g_unlisted++;
}
static void StreamGame(void)
{
    uint8_t* exe = fake_host_exe();
    DWORD old;
    VirtualProtect(exe + 0x334000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    exe[0x3340A0u] = 0xC3;
    const uintptr_t game = (uintptr_t)g_fake.image, api = game + API, table = game + TABLE, wait = (uintptr_t)exe + 0x3340A0u,
        destroy = (uintptr_t)FakeDestroy;
    memcpy(g_fake.image + 0x3326308u, &api, 8);
    memcpy(g_fake.image + API + 8, &table, 8);
    memcpy(g_fake.image + TABLE + 0x388, &destroy, 8);
    memcpy(g_fake.image + TABLE + 0x410, &wait, 8);
    g_destroys = 0; g_destroyed = 0; g_unlisted = 0;
}

/* The streamer on the fake game with `count` slots, the level's heightfield kept (40 m but where a case sets it). */
static uint8_t g_context[0x100], g_array[256 * 0xE0];
static void Stream(uint32_t count)
{
    if (g_worldMem == NULL)
    {
        g_worldMem = (uint8_t*)VirtualAlloc(NULL, 0x231000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        g_unitMem = (uint8_t*)VirtualAlloc(NULL, 0x100000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    const uint64_t world = (uint64_t)(uintptr_t)g_worldMem, list = (uint64_t)(uintptr_t)g_listed;
    memcpy(g_worldMem + 0x230590u, &list, 8); ListSet(0);
    memcpy(g_context + 0x40, &world, 8);
    memset(g_array, 0, sizeof g_array);
    CoXlTestStreamSetup((uintptr_t)g_fake.image, g_context, g_array, count);
    for (int k = 0; k < 4; k++) g_levelData[k] = 40.0f;
    CoXlTestHeightKept(g_levelData, 2, 2, 16384.0f);
}

/* A streamed stamp at (x, y) with a square footprint of `side` m (record +0xB0, map metres: the setup's half is 0). */
static void Slot(uint32_t i, float x, float y, float side, uint64_t unit, int mission)
{
    float box[4] = {x - side / 2, y - side / 2, x + side / 2, y + side / 2};
    memcpy(g_array + (size_t)i * 0xE0 + 0xB0, box, sizeof box);
    if (unit != 0 && unit < 0x100000) unit = U(unit); /* an id: a unit of the fake world, listed as SpawnUnit lists one */
    if (unit != 0) { const uint64_t world = (uint64_t)(uintptr_t)g_worldMem; memcpy((void*)(uintptr_t)(unit + 0x18), &world, 8); ListAdd(unit); }
    CoXlTestStreamSlot(i, x, y, unit, mission);
}

static void Generator(uint32_t state);
static void InMission(void);
static int g_tileOfB;
/* A mission in play with three streamed stamps, the first and the third on the new ground of a tile that just came in
 * (flagged to go out and come back), as when run grade2/run1 left its mission. */
static int Lines(const char* text);
static void Flagged(void)
{
    CoXlTestMission((uintptr_t)g_lg, 1);
    StreamGame();
    InMission();
    Stream(4); /* the fourth holds no unit */
    const int a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    g_tileOfB = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(g_tileOfB, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);
    Slot(1, BX + 1600.0f, AY, 20.0f, 0x2000, 0);
    Slot(2, BX + 400.0f, AY, 20.0f, 0x3000, 0);
    Generator(3);
    const int leaves = Lines("the level left play");
    CHECK(CoXlTestStreamFlushLater(0) == 1);
    CoXlTestStreamTick(); /* in play, nothing to take out */
    CHECK(g_destroys == 0 && Lines("the level left play") == leaves);
    CHECK(!CoXlTestStreamFlushPending()); /* a change frame's flush runs in its tick, once the lock is released */
    CHECK(CoXlTestStampsReplace(g_tileOfB, BX, AY) == 2 && CoXlTestStreamFlag(0) && CoXlTestStreamFlag(2));
}

static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = g_fake.log; (at = strstr(at, text)) != NULL; at++) n++;
    return n;
}
static void Generator(uint32_t state) { memcpy(g_lg + 0x1E4, &state, 4); }
/* Broad phase layer 4 of the mission world Budgets sets up: [[W+0x410]+0xA0] the broad phase, its vtable's +0x38 the
 * body update 0xD40550, layers [bp+0x10] (`layers` of them), layer 4's leaf count +0x50 and capacity +0x54. */
static void StaticLayer(uint32_t leaves, uint32_t capacity, int32_t layers, uint64_t update)
{
    uint8_t* exe = g_fake.exe;
    uint64_t a = (uint64_t)(uintptr_t)(exe + 0x27E1000u), bp = (uint64_t)(uintptr_t)(exe + 0x27E2000u), vt = (uint64_t)(uintptr_t)(exe + 0x27E3000u);
    uint64_t array = (uint64_t)(uintptr_t)(exe + 0x27E4000u), l4 = (uint64_t)(uintptr_t)(exe + 0x27E5000u);
    update += (uint64_t)(uintptr_t)exe;
    (void)a; memcpy(exe + 0x27E0000u + 0x4C0, &bp, 8); /* the mission world's broad phase, [W+0x4C0] */
    memcpy(exe + 0x27E2000u, &vt, 8); memcpy(exe + 0x27E3000u + 0x38, &update, 8);
    memcpy(exe + 0x27E2000u + 0x10, &array, 8); memcpy(exe + 0x27E2000u + 0x18, &layers, 4);
    memcpy(exe + 0x27E4000u + 4 * 8, &l4, 8);
    memcpy(exe + 0x27E5000u + 0x50, &leaves, 4); memcpy(exe + 0x27E5000u + 0x54, &capacity, 4);
}

/* The mission world's broad phase as the streamed layers read it: W = [exe+0x27BA9F0], [W+0x4C0] the broad phase
 * (vtable +0x38 the update 0xD40550), its `count` layers [bp+0x10] of 0x1C00 bytes, each with its leaves (+0x50), type
 * (+0x1AE0) and pair mask (+0x1AE4) as 0xDB9DB0 gives them, 6..9 as layer 4; W+0x38 the body array. */
static uint8_t g_slWorld[0x600], g_slBp[0x40], g_slVt[0x40], g_slBodies[0x100];
static uint64_t g_slLayers[16];
static uint8_t* g_slLayer;
static void BroadPhase(int32_t count, const uint32_t leaves[10])
{
    static const uint8_t type[10] = {2, 0xB, 2, 1, 1, 4, 1, 1, 1, 1};
    static const uint32_t mask[10] = {0x1D, 0, 9, 5, 1, 0, 1, 1, 1, 1};
    if (g_slLayer == NULL) g_slLayer = (uint8_t*)VirtualAlloc(NULL, 16 * 0x1C00, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memset(g_slLayer, 0, 16 * 0x1C00);
    uint8_t* exe = g_fake.exe;
    const uint64_t w = (uint64_t)(uintptr_t)g_slWorld, bp = (uint64_t)(uintptr_t)g_slBp, vt = (uint64_t)(uintptr_t)g_slVt,
        layers = (uint64_t)(uintptr_t)g_slLayers, bodies = (uint64_t)(uintptr_t)g_slBodies, update = (uint64_t)(uintptr_t)exe + 0xD40550u;
    const uint64_t engine = (uint64_t)(uintptr_t)(g_fake.image + 0x3700000u); /* the set-up's engine, inside the fake game */
    const uint32_t id = 2;                                          /* the main world's physics id: slot 2 */
    memcpy(g_fake.image + 0x3326340u, &engine, 8); memcpy(g_fake.image + 0x3700000u + 0x10F0u, &id, 4);
    memcpy(exe + 0x27BA9F0u, &w, 8);
    memcpy(g_slWorld + 0x4C0, &bp, 8); memcpy(g_slWorld + 0x38, &bodies, 8);
    memcpy(g_slBp, &vt, 8); memcpy(g_slVt + 0x38, &update, 8);
    memcpy(g_slBp + 0x10, &layers, 8); memcpy(g_slBp + 0x18, &count, 4);
    for (int32_t k = 0; k < count && k < 10; k++)
    {
        uint8_t* l = g_slLayer + (size_t)k * 0x1C00;
        g_slLayers[k] = (uint64_t)(uintptr_t)l;
        memcpy(l + 0x50, &leaves[k], 4); l[0x1AE0] = type[k]; memcpy(l + 0x1AE4, &mask[k], 4);
    }
}
static void* WriterW(void) { return g_slWorld + 0x18; }

/* The streamed layers' originals: the config's count (6), a layer's info (marked 0xEE, to tell it ran), the rule
 * (layer 4 for every body but those g_ruleOther names, layer 3) and the body maker (the next id). */
static int g_infoCalls, g_ruleOther;
static uint32_t g_nextId;
static int OrigCount6(void* cfg) { return 6; }
static int OrigCount7(void* cfg) { return 7; }
static void OrigInfo(void* cfg, uint8_t layer, uint8_t* out) { g_infoCalls++; out[0] = 0xEE; }
/* The flag as each hook goes in, by target: the count's hook must find it set. */
static uintptr_t g_flagTarget; static int g_flagAtHook = -1;
static void FlagAtHook(uintptr_t target) { if (target == g_flagTarget) g_flagAtHook = CoXlTestSlLayers(); }
/* The game's set-up making the main world on its thread while the count goes in: [engine+0x10E8] stored. */
static uint8_t* g_layerWorldEngine;
static void MakeWorldMeanwhile(uintptr_t target)
{
    const uint64_t world = 0x5000;
    (void)target;
    memcpy(g_layerWorldEngine + 0x10E8, &world, 8);
}
static void OrigRule(void* cfg, const uint32_t* ids, int n, const uint8_t* bodies, uint8_t* out)
{ for (int i = 0; i < n; i++) out[i] = (int)(ids[i] & 0xFFFFFFu) == g_ruleOther ? 3 : 4; }
static void* OrigCreate(void* writer, uint32_t* id, void* cinfo, void* r9) { *id = g_nextId; return writer; }
static DWORD WINAPI CreateElsewhere(void* p) { uint32_t id = 0; CoXlTestSlCreate(WriterW(), &id); return 0; }

/* The game's state as WorldStreamTick asks it: [game+0x3326340]+0xAC21C, 4 in a mission (and through its unload). */
static void InMission(void)
{
    static uint8_t* object;
    if (object == NULL) object = (uint8_t*)VirtualAlloc(NULL, 0xAD000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    const uint64_t at = (uint64_t)(uintptr_t)object; const uint32_t mission = 4;
    memcpy(g_fake.image + 0x3326340u, &at, 8);
    memcpy(object + 0xAC21Cu, &mission, 4);
}

/* The mission world's physics bodies in use of 1048576 and its navmesh cutters of 1000 (the readers'
 * [exe+0x27BA890+0x160]+0x4C/+0x40 and [exe+0x27C6330+16]+0x20/+0x24). */
static void Budgets(uint32_t bodies, uint32_t cutters)
{
    uint8_t* exe = g_fake.exe;
    uint64_t physics = (uint64_t)(uintptr_t)(exe + 0x27E0000u), pool = (uint64_t)(uintptr_t)(exe + 0x27F0000u);
    uint32_t slots = 1048576u, room = 1000u;
    memcpy(exe + 0x27BA890u + 0x160u, &physics, 8);
    memcpy(exe + 0x27E0000u + 0x40, &slots, 4); memcpy(exe + 0x27E0000u + 0x4C, &bodies, 4);
    memcpy(exe + 0x27C6330u + 16u, &pool, 8);
    memcpy(exe + 0x27F0000u + 0x20, &cutters, 4); memcpy(exe + 0x27F0000u + 0x24, &room, 4);
}

/* The race the spawn guards against: a refill takes the chosen grid out of use before the spawn enters it. */
static int g_refillSlots[2] = {-1, -1};
static void RefillBetween(uint64_t grid)
{
    (void)grid;
    for (int k = 0; k < 2; k++) if (g_refillSlots[k] >= 0) CoXlTestGridReady(g_refillSlots[k], 0);
}

/* A function with the save codes the game's hosts use (a push, an allocation, a saved register and a saved xmm at
 * offsets from the frame), standing for the game function a stub is laid over; UnwindHostBody is past its prologue. */
void UnwindHost(void);
extern const uint8_t UnwindHostBody[];
__asm__(".text\n.p2align 4\n.globl UnwindHost\n.globl UnwindHostBody\nUnwindHost:\n"
        "  .seh_proc UnwindHost\n"
        "  pushq %rbx\n  .seh_pushreg %rbx\n"
        "  subq $0x60, %rsp\n  .seh_stackalloc 0x60\n"
        "  movq %r12, 0x20(%rsp)\n  .seh_savereg %r12, 0x20\n"
        "  movaps %xmm6, 0x30(%rsp)\n  .seh_savexmm %xmm6, 0x30\n"
        "  .seh_endprologue\n"
        "UnwindHostBody:\n  nop\n  nop\n  nop\n  nop\n  nop\n  nop\n  nop\n  nop\n"
        "  movaps 0x30(%rsp), %xmm6\n  movq 0x20(%rsp), %r12\n  addq $0x60, %rsp\n  popq %rbx\n  retq\n"
        "  .seh_endproc\n");

/* A host laid out as the game's: a fragment past the root's code whose data saves xmm6 and chains to the root (a push,
 * 0x60, r12 saved), the root naming a handler when `handler`. Registered as a table of its own. Returns a site in the
 * fragment's body. */
static uintptr_t ChainedHost(int handler)
{
    uint8_t* h = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (h == NULL) return 0;
    memset(h, 0xCC, 0x800);
    static const uint8_t root[12] = {1u | 0u, 0x10, 4, 0, 0x10, 0x04 | (12 << 4), 4, 0, 0x08, 0x02 | (11 << 4), 0x02, 0x00 | (3 << 4)};
    memcpy(h + 0x800, root, sizeof root);
    if (handler) { h[0x800] = 1u | (UNW_FLAG_EHANDLER << 3); uint32_t at = 0x300; memcpy(h + 0x80C, &at, 4); }
    static const uint8_t frag[8] = {1u | (UNW_FLAG_CHAININFO << 3), 0x08, 2, 0, 0x08, 0x08 | (6 << 4), 3, 0};
    memcpy(h + 0x840, frag, sizeof frag);
    RUNTIME_FUNCTION chain = {0x000, 0x100, 0x800};
    memcpy(h + 0x848, &chain, sizeof chain);
    RUNTIME_FUNCTION* table = (RUNTIME_FUNCTION*)(h + 0x900);
    table[0] = (RUNTIME_FUNCTION){0x000, 0x100, 0x800};
    table[1] = (RUNTIME_FUNCTION){0x100, 0x200, 0x840};
    if (!RtlAddFunctionTable(table, 2, (DWORD64)(uintptr_t)h)) return 0;
    return (uintptr_t)h + 0x180;
}

int main(void)
{
    g_lg = (uint8_t*)VirtualAlloc(NULL, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_lg == NULL) { printf("layergrid: no memory\n"); return 1; }

    Start("a stamp by the drop tile alone: its grid holds the tile inside and the coarse ground past it");
    int a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    uint64_t fa = Field(2100.0f, AY);
    CHECK(fa != 0 && Near(Read(fa, 2600.0f, AY), 45.0f) && Near(Read(fa, 1900.0f, AY), 40.0f));
    CHECK(Field(1126.0f, AY) == 0); /* no tile within reach: the level's own heightfield, the coarse ground as drawn */

    Start("a tile comes west of the drop tile: the grids hold its ground, and a stamp there gets its grid");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    int b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    fa = Field(2100.0f, AY); /* nearer the drop tile: its grid, filled again where the new tile lies */
    CHECK(fa != 0 && Near(Read(fa, 1900.0f, AY), 30.0f) && Near(Read(fa, 2600.0f, AY), 45.0f));
    uint64_t fb = Field(1900.0f, AY); /* nearer the new tile: its own grid */
    CHECK(fb != 0 && fb != fa && Near(Read(fb, 1900.0f, AY), 30.0f) && Near(Read(fb, 2300.0f, AY), 45.0f));
    CHECK(Field(BX - 1500.0f, AY) == 0);
    float h = 0.0f;
    CHECK(CoXlTestLayerHeight(fb, 1900.0f, AY, &h) && Near(h, 30.0f));

    Start("while a moving tile builds, stamps still get the drawn ground");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    CoXlTestNearBuilding(1);
    fa = Field(2600.0f, AY);
    CHECK(fa != 0 && Near(Read(fa, 2600.0f, AY), 45.0f));
    h = 0.0f;
    CHECK(CoXlTestLayerHeight((uint64_t)(uintptr_t)g_levelDesc, 2600.0f, AY, &h) && Near(h, 45.0f));
    CoXlTestNearBuilding(0);

    Start("a tile comes in play: its grid fills on a thread, out of use until the game thread finishes it");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTileLater(b, 1, BX, AY);
    CHECK(Field(1900.0f, AY) == Field(2100.0f, AY)); /* the drop tile's grid serves both until the new one is in use */
    {
        int polls = 0;
        while (CoXlTestLayerBusy() && polls < 20000) { Sleep(1); polls++; }
        CHECK(polls < 20000);
    }
    fa = Field(2100.0f, AY);
    CHECK(fa != 0 && Near(Read(fa, 1900.0f, AY), 30.0f) && Near(Read(fa, 2600.0f, AY), 45.0f));
    fb = Field(1900.0f, AY);
    CHECK(fb != 0 && fb != fa && Near(Read(fb, 1900.0f, AY), 30.0f) && Near(Read(fb, 2300.0f, AY), 45.0f));
    CHECK(!CoXlTestLayerBusy());

    Start("the level ends while a tile's fill runs: the level's end waits for it (the next level writes the ground it reads)");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTileLater(b, 1, BX, AY);
    CHECK(CoXlTestLayerBusy());
    CoXlTestNearLevelLeft(0);
    CHECK(!CoXlTestLayerBusy());

    Start("the stream's spawns wait on their own poll: a change nothing else polls is finished by it");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTileLater(b, 1, BX, AY);
    {
        int polls = 0;
        while (CoXlTestStreamLayerBusy() && polls < 20000) { Sleep(1); polls++; }
        CHECK(polls < 20000);
    }
    fb = Field(1900.0f, AY);
    CHECK(fb != 0 && fb != Field(2100.0f, AY) && Near(Read(fb, 1900.0f, AY), 30.0f)); /* the new grid in use */

    Start("two pollers of one finished change: it is finished once (one flip of the grid's descriptor)");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    {
        int once = 1;
        for (int round = 0; round < 12; round++)
        {
            uint32_t before = CoXlTestLayerFills();
            CoXlTestLayerTileLater(b, 1, BX, AY);
            int polls = 0;
            while (!CoXlTestLayerThreadDone() && polls < 20000) { Sleep(1); polls++; }
            g_go = 0;
            HANDLE t[2] = {CreateThread(NULL, 0, Poller, NULL, 0, NULL), CreateThread(NULL, 0, Poller, NULL, 0, NULL)};
            Sleep(2);
            InterlockedExchange(&g_go, 1);
            WaitForMultipleObjects(2, t, TRUE, INFINITE);
            CloseHandle(t[0]); CloseHandle(t[1]);
            once &= CoXlTestLayerFills() == before + 1 && !CoXlTestLayerBusy();
        }
        CHECK(once);
        fb = Field(1900.0f, AY);
        CHECK(fb != 0 && Near(Read(fb, 1900.0f, AY), 30.0f));
    }

    Start("a tile goes: the other grids hold the coarse ground there again, and its own grid is out of use");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(AX, CY2, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, AX, CY2);
    /* Past the drop tile's span: in its band the drawn ground is the higher of the tile and the coarse terrain. */
    fa = Field(AX, AY - 900.0f);
    CHECK(fa != 0 && Near(Read(fa, AX, AY - 1200.0f), 30.0f));
    CoXlTestTileRemove(b);
    CoXlTestLayerTile(b, 0, AX, CY2);
    fa = Field(AX, AY - 900.0f);
    CHECK(fa != 0 && Near(Read(fa, AX, AY - 1200.0f), 40.0f));
    CHECK(Field(AX, CY2) == 0);

    Start("a tile comes: the stamps whose ground moved go out and come back; the rest stay");
    StreamGame();
    Stream(6);
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);         /* on the new tile: the level's 40 m before, its 30 m now */
    Slot(1, BX + 1600.0f, AY, 20.0f, 0x2000, 0);        /* the drop tile's interior: past the new tile's span and reach */
    Slot(2, BX, AY, 20.0f, 0x3000, 1);                  /* a mission's stamp: never streamed out */
    Slot(3, BX - 1400.0f, AY + 300.0f, 20.0f, 0x4000, 0); /* in reach past the span: its grid now holds the coarse 40 m it stood on */
    Slot(4, BX + 1100.0f, AY, 20.0f, 0x5000, 0);        /* in the drop tile's square and the new tile's span: still the drop tile's 45 m */
    Slot(5, BX + 1100.0f, AY, 400.0f, 0x6000, 0);       /* the same, its footprint reaching into the new tile's square */
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 2);
    CHECK(CoXlTestStreamFlag(0) && !CoXlTestStreamFlag(1) && !CoXlTestStreamFlag(2) && !CoXlTestStreamFlag(3) && !CoXlTestStreamFlag(4) &&
        CoXlTestStreamFlag(5));
    CoXlTestStreamScan();
    int live = 0, due = 0; uint64_t unit = 0;
    CHECK(g_destroys == 2);
    CoXlTestStreamSlotState(0, &live, &due, &unit);
    CHECK(!live && due && unit == 0 && !CoXlTestStreamFlag(0));
    for (uint32_t k = 1; k <= 4; k++)
    {
        CoXlTestStreamSlotState(k, &live, &due, &unit);
        CHECK(live && unit == U(0x1000u * (k + 1)));
    }
    CHECK(CoXlTestVisibleWithNone(BX + 900.0f, AY) == CoXlTestVisibleAt(BX + 900.0f, AY) &&
        CoXlTestVisibleWithNone(BX + 1100.0f, AY) == CoXlTestVisibleAt(BX + 1100.0f, AY) &&
        CoXlTestVisibleWithNone(BX - 1100.0f, AY) == CoXlTestVisibleAt(BX - 1100.0f, AY));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a tile comes whose ground is within 0.5 m of what the stamps stood on: they stay");
    StreamGame();
    Stream(2);
    g_tileB[0] = g_tileB[1] = g_tileB[2] = g_tileB[3] = U16(40.3f);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 0 && !CoXlTestStreamFlag(0));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a tile comes 0.7 m off the ground the stamps stood on: they go");
    StreamGame();
    Stream(2);
    g_tileB[0] = g_tileB[1] = g_tileB[2] = g_tileB[3] = U16(40.7f);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 1 && CoXlTestStreamFlag(0));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a stamp that stood on the level's heightfield gets a grid: it goes when the two differ there");
    StreamGame();
    Stream(2);
    g_levelData[2] = 39.0f; /* x < 0, y < 0: column 0, row 0, counted from the far edge: index 2 */
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX - 1400.0f, AY + 300.0f, 20.0f, 0x1000, 0);
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 1 && CoXlTestStreamFlag(0));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a tile goes: the stamps on its ground go, the rest stay");
    StreamGame();
    Stream(4);
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);         /* on it: its 30 m, then the level's 40 m */
    Slot(1, BX - 1400.0f, AY, 20.0f, 0x2000, 0);        /* its grid's coarse 40 m, then the level's 40 m */
    Slot(2, BX + 1100.0f, AY, 20.0f, 0x3000, 0);        /* the drop tile's square */
    CHECK(CoXlTestTileGo(b) == 1);
    CHECK(CoXlTestStreamFlag(0) && !CoXlTestStreamFlag(1) && !CoXlTestStreamFlag(2));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a scan takes the least seen first: past the distance a stamp is drawn to, then the farthest for its size");
    StreamGame();
    Stream(33);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    const float ox = BX - 900.0f;
    Slot(0, BX, AY, 19.0f, 0x1000, 0);                  /* 900 m away, drawn to ~830 m: unseen */
    Slot(1, BX + 100.0f, AY, 52.0f, 0x2000, 0);         /* 1000 m away, drawn to ~1.2 km: seen */
    for (uint32_t k = 2; k < 33; k++) Slot(k, BX, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0); /* small and unseen */
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 33);
    CoXlTestStreamObserver(1, ox, AY);
    CoXlTestStreamScan();
    CoXlTestStreamObserver(0, 0.0f, 0.0f);
    CHECK(g_destroys == 32);
    CoXlTestStreamSlotState(0, &live, &due, &unit);
    CHECK(!live && unit == 0);
    CoXlTestStreamSlotState(1, &live, &due, &unit);
    CHECK(live && unit == U(0x2000) && CoXlTestStreamFlag(1));
    CoXlTestStreamScan();
    CHECK(g_destroys == 33);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a change frame's removals stop once their time is used, each asked again");
    StreamGame();
    Stream(33);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    for (uint32_t k = 0; k < 33; k++) Slot(k, BX, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0);
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 33);
    CoXlTestStreamObserver(1, BX - 900.0f, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && g_destroys == 0); /* the scan decided 32 */
    uint32_t waiting = 0;
    CHECK(CoXlTestStreamDrain(0.0, &waiting) == 1 && waiting == 31 && g_destroys == 1); /* past its time: one at least */
    CHECK(CoXlTestStreamDrainMost(4, 1.0e9, &waiting) == 4 && waiting == 27 && g_destroys == 5);
    uint32_t q = 0;
    while (q < 33 && !CoXlTestStreamQueued(q)) q++;
    CHECK(q < 33);
    CoXlTestStreamFlagClear(q);                           /* its ground is the old one again: it stays */
    CHECK(CoXlTestStreamDrain(1.0e9, &waiting) == 26 && waiting == 0);
    CHECK(waiting == 0 && g_destroys == 31);
    CoXlTestStreamSlotState(q, &live, &due, &unit);
    CHECK(live && unit == U(0x1000u * (q + 1)));
    CHECK(CoXlTestStreamScanFrames(-1) == 1 && g_destroys == 32); /* the next scan: the one left over; q is not flagged */
    CoXlTestStreamSlotState(q, &live, &due, &unit);
    CHECK(live && unit == U(0x1000u * (q + 1)));
    CoXlTestStreamObserver(0, 0.0f, 0.0f);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("removals out of reach wait in the queue once each, however many scans ask");
    StreamGame();
    Stream(10);
    for (uint32_t k = 0; k < 10; k++) { Slot(k, AX + 1000.0f, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && g_destroys == 0);
    CHECK(CoXlTestStreamDrainMost(4, 1.0e9, &waiting) == 4 && waiting == 6); /* all 10 queued at once, the player near */
    CHECK(CoXlTestStreamScanFrames(0) == 0);                                  /* none twice */
    CHECK(CoXlTestStreamDrain(1.0e9, &waiting) == 6 && waiting == 0 && g_destroys == 10);
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    /* The window enters 80-170 stamps a second while a player moves (session f8a9e7ce); a removal pace of 8 a scan held
     * stamps to 2 km. Every expired stamp is queued as the queue has room, the farthest past its radius first. */
    Start("out-of-reach removals: all of them as the queue has room, the farthest first, room left for put-backs");
    StreamGame();
    Stream(200);
    for (uint32_t k = 0; k < 200; k++)
    {
        const uint32_t at = (k * 37u) % 200u; /* not in distance order */
        Slot(k, AX + 1000.0f + 10.0f * (float)at, AY, 2.0f, 0x1000u * (k + 1), 0);
        CoXlTestStreamOutSince(k, 1);
    }
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && g_destroys == 0);
    uint32_t order[256], expiredNow = 0, expiredMost = 0;
    CHECK(CoXlTestStreamQueue(order, 256) == 160); /* 256 less the 96 kept for put-backs and far evictions */
    CoXlTestStreamExpired(&expiredNow, &expiredMost);
    CHECK(expiredNow == 200 && expiredMost == 200);
    int sorted = 1;
    for (uint32_t k = 0; k < 160; k++) if ((order[k] * 37u) % 200u != 199u - k) sorted = 0;
    CHECK(sorted); /* the 160 farthest, farthest first */
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 160); /* full: none twice, none more */
    CHECK(CoXlTestStreamDrain(1.0e9, &waiting) == 64 && waiting == 96 && g_destroys == 64);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 136); /* the 40 nearest join behind */
    CoXlTestStreamExpired(&expiredNow, &expiredMost);
    CHECK(expiredNow == 136 && expiredMost == 200);
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    Start("out-of-reach removals: a far stamp is ranked by its own removal radius");
    StreamGame();
    Stream(2);
    Slot(0, AX + 2000.0f, AY, 2.0f, 0x1000, 0); /* 2000 m, its radius 1900 m: 1.11 squared */
    CoXlTestStreamOut2(0, 1900.0f);
    CoXlTestStreamOutSince(0, 1);
    Slot(1, AX + 1000.0f, AY, 2.0f, 0x2000, 0); /* 1000 m, the near radius 800 m: 1.56 squared */
    CoXlTestStreamOutSince(1, 1);
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 2 && order[0] == 1 && order[1] == 0);
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    Start("the streamer adds and removes bodies in one frame each 250 ms at most; a frame with nothing to do leaves the next free");
    StreamGame();
    Stream(200);
    for (uint32_t k = 0; k < 60; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamObserver(1, AX, AY);
    unsigned long long next = 0;
    CoXlTestStreamChangeAt(0);
    CHECK(CoXlTestStreamChange(5000, 0, &next) == 0 && next == 0 && g_destroys == 0); /* nothing queued: no change frame */
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 60);
    CHECK(CoXlTestStreamChange(5010, 1, &next) == 60 && next == 5260 && g_destroys == 60); /* removals go during a layer fill */
    CHECK(CoXlTestStreamChange(5011, 0, &next) == 0 && next == 5260 && g_destroys == 60); /* the next frames add and remove nothing */
    CHECK(CoXlTestStreamChange(5600, 0, &next) == 0 && next == 5260);

    /* Change frames gave 12 ms/s a budget against a peak spawn demand of 11 ms/s (review of 7cdcd4d): a backlog halves
     * the gap and doubles the time and removals. */
    Start("a backlog of removals or spawns: a change frame every second frame with 128 removals; a layer fill keeps the short gap");
    StreamGame();
    Stream(200);
    for (uint32_t k = 0; k < 200; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamObserver(1, AX, AY);
    CoXlTestStreamChangeAt(0);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 160); /* 160 queued, 200 past their radius */
    CoXlTestStreamTicksAdd();                                        /* a tick in play: never 0 */
    CHECK(CoXlTestStreamChange(5000, 0, &next) == 128 && next == 5000 && CoXlTestStreamNextTick() == CoXlTestStreamTicks() + 2 && g_destroys == 128);
    CHECK(CoXlTestStreamChangeTick() == CoXlTestStreamTicks() && CoXlTestSlChangeMark() == 1); /* the makers' sampler's mark */
    CHECK(CoXlTestStreamChange(5001, 0, &next) == 0 && g_destroys == 128);  /* the same frame */
    CoXlTestStreamTicksAdd();
    CHECK(CoXlTestSlChangeMark() == 2);                                      /* the frame after it */
    CHECK(CoXlTestStreamChange(5002, 0, &next) == 0 && g_destroys == 128);  /* the next frame */
    CoXlTestStreamTicksAdd();
    CHECK(CoXlTestStreamChange(5003, 0, &next) == 32 && next == 5003 && g_destroys == 160); /* the scan's 200 still count */
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 40);
    {
        uint32_t stuck, dry, least, exits = 0, exitChanges = 0; double rebuiltSum;
        CoXlTestStreamFloorMore(&stuck, &dry, &rebuiltSum, &least, &exits, &exitChanges);
        CHECK(exits == 0 && exitChanges == 2);                           /* all 200 ran out before the last scan: none new */
        CHECK(CoXlTestStreamScanFrames(0) == 0);
        CoXlTestStreamFloorMore(&stuck, &dry, &rebuiltSum, &least, &exits, &exitChanges);
        CHECK(exits == 0 && exitChanges == 0);                           /* none new, no change frame since the last scan */
    }
    CoXlTestStreamTicksAdd(); CoXlTestStreamTicksAdd();
    CHECK(CoXlTestStreamChange(5004, 0, &next) == 40 && next == 5254 && CoXlTestStreamNextTick() == 0 && g_destroys == 200); /* no backlog left */
    uint32_t changes = 0, backlogs = 0, drainCuts = 0, spawnCuts = 0, changeRemoved = 0, drains[5];
    CoXlTestStreamChangeStats(&changes, &backlogs, &drainCuts, &spawnCuts, &changeRemoved, drains);
    CHECK(changes == 3 && backlogs == 2 && drainCuts == 0 && spawnCuts == 0 && changeRemoved == 200 && drains[0] == 3);
    CoXlTestStreamNote();
    CHECK(fake_host_logged("streaming change frames: 2 of 3 with a backlog (every 2 frames); budget 3.00 ms a frame, 0 at its floor of 0.5 ms (the quiet frames' longest of 32: "
                           "mean 0.0 ms), 0 of those not caused by the quiet frames, 0 spawning near stamps only, 0 with a due stamp in reach and no layer fill "
                           "(0 locations spawned in them, 0 of them none), the last removal frame removing 8 at least (exits 0 over 0 removal frames); "
                           "change frame cost with its after-change frame estimated: removal 3.00 ms (its 0 samples' mean 0.00), spawn 0.50 ms (its 0 samples' mean 0.00)"));
    CHECK(fake_host_logged("streaming pace: 3 removal frames (0 forced by a 500 ms wait, 100 ms with a backlog), 0 spawn frames; 0 change tries held for a far-grass job, "
                           "0 let through after 500 ms; the quiet frames' longest of 32: mean 0.0 ms without the far-grass and after-change frames, 0.0 ms with them; "
                           "a removal's own time "));
    CHECK(fake_host_logged("streaming keep-up: 40 live stamps past their removal radius at the last scan (+40 since the last note), 0 removals queued (+0); "
                           "in 10 s 200 new exits and 200 removals (the exits' pace 0 a second); the oldest removal forces a frame after 100 ms now"));
    CHECK(fake_host_logged("streaming change work: 200 removals, 66.7 a frame; removals "));
    CHECK(fake_host_logged("0 locations spawned, 0.0 a frame; their units made in 0 flushes, 0.00 ms each (the most 0.00) after an animation wait of 0.00 ms (the most 0.00), 0.000 ms a location and 0.00 ms fixed estimated; "
                           "cut short by the time limit: removals in 0, spawns in 0; by the location count: 0; deferred for a tile step: 0 frames of 0 a tile built in; "
                           "0 due not spawned at the last scan; drained layer 4 3, 6..9 0 0 0 0; spawned into layer 4 0, 6..9 0 0 0 0"));
    CHECK(fake_host_logged("streamed layers off: static leaves layer 4 0, layers 6..9 0 0 0 0 (not read"));
    CoXlTestStreamChangeStats(&changes, &backlogs, &drainCuts, &spawnCuts, &changeRemoved, drains);
    CHECK(changes == 0 && backlogs == 0 && changeRemoved == 0);
    /* a backlog by the queue alone: 100 queued, 100 past their radius */
    StreamGame();
    Stream(100);
    for (uint32_t k = 0; k < 100; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamChangeAt(0);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 100);
    CHECK(CoXlTestStreamChange(7000, 0, &next) == 100 && next == 7000 && CoXlTestStreamNextTick() == CoXlTestStreamTicks() + 2 && g_destroys == 100);
    /* a backlog by due spawns alone: 65 due, past kStreamBacklogDue; 64 are not */
    StreamGame();
    Stream(100);
    for (uint32_t k = 0; k < 2; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamChangeAt(0);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 2);
    for (uint32_t k = 10; k < 75; k++) CoXlTestStreamDue(k, U(0x1000u * (k + 1)));
    CHECK(CoXlTestStreamChange(8000, 0, &next) == 0);                 /* a spawn frame: the fake game spawns nothing; 2 queued gather */
    CHECK(CoXlTestStreamChange(8250, 0, &next) == 2 && next == 8250 && CoXlTestStreamNextTick() == CoXlTestStreamTicks() + 2);
    Stream(100);
    for (uint32_t k = 0; k < 2; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamChangeAt(0);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 2);
    for (uint32_t k = 10; k < 74; k++) CoXlTestStreamDue(k, U(0x1000u * (k + 1)));
    CHECK(CoXlTestStreamChange(9000, 0, &next) == 0);
    CHECK(CoXlTestStreamChange(9250, 0, &next) == 2 && next == 9500 && CoXlTestStreamNextTick() == 0);  /* 64 due: no backlog */
    /* a layer fill holds the spawns: the frame that only removed comes back at the short gap */
    StreamGame();
    Stream(20);
    for (uint32_t k = 0; k < 4; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 4);
    CoXlTestStreamDue(10, U(0x9000));
    CoXlTestStreamChangeAt(0);
    CHECK(CoXlTestStreamChange(6000, 1, &next) == 0 && g_destroys == 0); /* 4 queued gather; no spawn frame in a fill */
    CHECK(CoXlTestStreamChange(6250, 1, &next) == 4 && next == 6250 && CoXlTestStreamNextTick() == CoXlTestStreamTicks() + 2 && g_destroys == 4);
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    /* Issue A's tile build steps and B's change frames never share a frame: the change frame waits while a step ran in
     * this frame or the last (the level update runs before the streamer's tick in the frame, 0xAB519E before 0xAB612D). */
    Start("a change frame waits while a tile build step ran in this frame or the last");
    StreamGame();
    Stream(10);
    for (uint32_t k = 0; k < 4; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 4);
    CoXlTestStreamChangeAt(0);
    CoXlTestStreamTicksAdd();
    CoXlTestMoveStepAt(CoXlTestStreamTicks());       /* a step in this frame */
    CHECK(CoXlTestStreamChange(4000, 0, &next) == 0 && g_destroys == 0 && next == 0 && CoXlTestStreamTileDefers() == 1);
    CoXlTestStreamTicksAdd();                        /* the next frame: the step was the last frame's */
    CHECK(CoXlTestStreamChange(4016, 0, &next) == 0 && g_destroys == 0 && CoXlTestStreamTileDefers() == 2);
    CoXlTestStreamTicksAdd();
    CoXlTestStreamRemoveWait(3700, 0);              /* the oldest waited past the gather */
    CHECK(CoXlTestStreamChange(4032, 0, &next) == 4 && g_destroys == 4 && next == 4282 && CoXlTestStreamTileDefers() == 2);
    CoXlTestMoveStepAt(CoXlTestStreamTicks());
    CHECK(CoXlTestStreamChange(4300, 0, &next) == 0 && CoXlTestStreamTileDefers() == 2); /* nothing to do: not counted */
    CoXlTestMoveStepAt(0);
    {
        /* MoveLevelUpdate's mark: a build frame of 1.5 ms or less is a tile building, over it a heavy step */
        double moveMs = 9; double sum = 0; uint32_t floors = 0, builds = 0;
        CHECK(CoXlTestMoveMarks(&moveMs) == 0 && moveMs == 0.0);
        CoXlTestMoveStepMark(1.5);
        CoXlTestStreamTicksAdd();
        CHECK(CoXlTestMoveMarks(&moveMs) == 2 && moveMs == 1.5);          /* building, no heavy step */
        CoXlTestStreamBudgetStats(&sum, &floors, &builds);
        CHECK(CoXlTestStreamChange(4600, 0, &next) == 0 && CoXlTestStreamTileDefers() == 2); /* nothing queued, not deferred */
        CoXlTestStreamBudgetStats(&sum, &floors, &builds);
        CHECK(builds == 1);
        CoXlTestStreamTicksAdd();
        CHECK(CoXlTestMoveMarks(&moveMs) == 2 && moveMs == 0.0);          /* the next frame: built in the last */
        CoXlTestStreamTicksAdd();
        CHECK(CoXlTestMoveMarks(&moveMs) == 0);
        CoXlTestMoveStepMark(1.6);
        CoXlTestStreamTicksAdd();
        CHECK(CoXlTestMoveMarks(&moveMs) == 3 && moveMs == 1.6);          /* a heavy step */
        CoXlTestStreamTicksAdd();
        CHECK(CoXlTestMoveMarks(&moveMs) == 3 && moveMs == 0.0);
        CoXlTestStreamTicksAdd();
        CHECK(CoXlTestMoveMarks(&moveMs) == 0);
        CoXlTestMoveStepAt(0);
    }
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    /* The flush makes the change frame's units after the lock is released, and its time a location sizes the next. */
    Start("the change frame's flush waits for the lock's release; its time a location is the next frames' estimate");
    StreamGame();
    Stream(2);
    {
        uint32_t flushes = 0; double sum = 0, most = 0, slope = 0, fixed = 0;
        CoXlTestStreamMakeEstimate(0.0);
        CoXlTestStreamFlushCost(4.0, 8, &flushes, &sum, &most, &slope, &fixed);
        CHECK(flushes == 1 && sum == 4.0 && most == 4.0 && slope == 0.5 && fixed == 0.0); /* one flush: its mean a location */
        CoXlTestStreamFlushCost(2.0, 2, &flushes, &sum, &most, &slope, &fixed);
        CHECK(flushes == 2 && sum == 6.0 && most == 4.0);                 /* the line through 8: 4 ms and 2: 2 ms */
        CHECK(slope > 0.3333 && slope < 0.3334 && fixed > 1.3333 && fixed < 1.3334);
        CoXlTestStreamFlushCost(1.0, 0, &flushes, &sum, &most, &slope, &fixed);
        CHECK(flushes == 3 && most == 4.0 && slope > 0.3333 && slope < 0.3334); /* no location: nothing to fit */
        CoXlTestStreamFlushCost(8.0, 2, &flushes, &sum, &most, &slope, &fixed);
        CHECK(flushes == 4 && most == 8.0 && slope > 0.3333 && slope < 0.3334); /* the game's own entries' burst: the slope stays */
        CoXlTestStreamFlushCost(6.0, 16, &flushes, &sum, &most, &slope, &fixed);
        /* the burst flattens the fitted line to 0.052 a location: held at a quarter of the mean a location, 0.698 */
        CHECK(slope > 0.1746 && slope < 0.1747 && fixed > 3.899 && fixed < 3.9);
        CoXlTestStreamMakeEstimate(0.0);
        CoXlTestStreamFlushCost(0.5, 4, &flushes, &sum, &most, &slope, &fixed);
        CoXlTestStreamFlushCost(0.4, 4, &flushes, &sum, &most, &slope, &fixed);
        CHECK(slope > 0.1116 && slope < 0.1117 && fixed == 0.0);          /* the same count twice: no line, the mean */
        CoXlTestStreamMakeEstimate(0.0);
        CoXlTestStreamFlushCost(0.1, 2, &flushes, &sum, &most, &slope, &fixed);
        CoXlTestStreamFlushCost(4.0, 8, &flushes, &sum, &most, &slope, &fixed);
        CHECK(slope == 0.05 && fixed == 0.0);                             /* a line through below 0 ms: not a making, kept */
        CoXlTestStreamMakeEstimate(0.0);
        CoXlTestStreamFlushCost(2.0, 4, &flushes, &sum, &most, &slope, &fixed);
        CoXlTestStreamFlushCost(2.1, 5, &flushes, &sum, &most, &slope, &fixed);
        CHECK(slope > 0.4529 && slope < 0.4530 && fixed == 0.0);          /* 4 and 5 vary too little to fit: the mean */
        CoXlTestStreamMakeEstimate(0.0);
        CoXlTestStreamFlushCost(0.0, 8, &flushes, &sum, &most, &slope, &fixed);
        CoXlTestStreamFlushCost(1.0, 2, &flushes, &sum, &most, &slope, &fixed);
        CHECK(slope > 0.1111 && slope < 0.1112);                          /* a falling line with no slope yet: the mean */
        /* the animation wait spins a while (mov ecx, 1000000; pause; dec ecx; jnz; ret): its time is logged apart */
        static const uint8_t spinWait[] = {0xB9, 0x40, 0x42, 0x0F, 0x00, 0xF3, 0x90, 0xFF, 0xC9, 0x75, 0xFA, 0xC3};
        memcpy(g_fake.exe + 0x3340A0u, spinWait, sizeof spinWait);
        CHECK(CoXlTestStreamFlushLater(8) == 1);
        CoXlTestStreamFlushCost(0.0, 0, &flushes, &sum, &most, &slope, &fixed);
        CHECK(flushes == 14);                                                /* pending: not run under the lock (14: this call) */
        CHECK(CoXlTestStreamFlushAfter() == 0);
        {
            uint32_t h, sm, sq; double qs, waitMs = 0, waitMost = 0;
            CoXlTestStreamFloorStats(&h, &qs, &sm, &sq, &waitMs, &waitMost);
            CHECK(waitMost > 0.05 && waitMost < 2000.0 && waitMs == waitMost); /* the spin's own time, not the clock's */
        }
        g_fake.exe[0x3340A0u] = 0xC3;
        CoXlTestStreamFlushCost(0.0, 0, &flushes, &sum, &most, &slope, &fixed);
        CHECK(flushes == 16);                                                /* the flush after the release, and this */
        CoXlTestStreamMakeEstimate(0.0);
        memcpy(g_fake.image + TABLE + 0x410, &(uint64_t){0}, 8);             /* the animation wait does not read: no flush */
        CHECK(CoXlTestStreamFlushLater(8) == 0);
    }

    /* The governor (run 2c: quiet frames' world update 6.4 ms at 310k bodies, 10.4 at 489k): a second's mean of the
     * quiet frames turns it on over 7 ms and off under 6; between them it keeps its state. Change frames, far-grass
     * frames and frames kept apart do not feed it. */
    Start("the governor: on over 7 ms, off under 6 ms, a second's quiet frames only, 10 of them at least");
    {
        int64_t t = 1000000;
        unsigned long long now = 100000;
        double last = 0;
        uint32_t onTurns = 9, offTurns = 9, held = 9, drained = 9, nearKept = 9;
        int on = 0;
        co_framecost_test_reset(1000000);
        StreamGame();
        Stream(10);
        CoXlTestStreamGovernSet(0);
        /* `n` frames of 10 ms with `world` ms of world update; kind 1 a change frame, 2 a far-grass frame, 3 kept apart */
#define GOV_FRAMES(n, world, kind)                                                                                    \
        for (int f = 0; f < (n); f++)                                                                                  \
        {                                                                                                              \
            co_framecost_test_begin(t, 1, 1);                                                                          \
            on = CoXlTestStreamGovern(now, &last);                                                                     \
            co_framecost_test_world(1, t + 100); co_framecost_test_world(0, t + 100 + (int64_t)((world) * 1000.0));      \
            if ((kind) == 1) co_framecost_bodies(1);                                                                   \
            if ((kind) == 2) co_framecost_grass(1);                                                                    \
            if ((kind) == 3) co_framecost_apart();                                                                     \
            t += 10000; now += 10;                                                                                     \
        }
        GOV_FRAMES(101, 7.0, 0);
        CHECK(!on && last == 7.0);                                       /* 7.0 is not over 7 */
        GOV_FRAMES(99, 7.1, 0);
        CHECK(!on);                                                      /* the second not over yet */
        GOV_FRAMES(1, 7.1, 0);
        CHECK(on && fabs(last - (7.0 + 99 * 7.1) / 100) < 1e-9);        /* a frame of the second before closes in it */
        GOV_FRAMES(100, 6.5, 0);
        CHECK(on && fabs(last - (7.1 + 99 * 6.5) / 100) < 1e-9);        /* between: kept on */
        GOV_FRAMES(200, 6.0, 0);
        CHECK(on && last == 6.0);                                        /* 6.0 is not under 6 */
        GOV_FRAMES(100, 5.9, 0);
        CHECK(!on && fabs(last - (6.0 + 99 * 5.9) / 100) < 1e-9);
        GOV_FRAMES(100, 6.5, 0);
        CHECK(!on);                                                      /* between: kept off */
        GOV_FRAMES(100, 12.0, 1);
        CHECK(!on);                                                      /* change frames: no sample, the mean is not taken */
        GOV_FRAMES(100, 12.0, 2);
        CHECK(!on);                                                      /* far-grass frames */
        GOV_FRAMES(100, 12.0, 3);
        CHECK(!on);                                                      /* frames kept apart */
        /* change frames every other frame: each frame without one is right after one (run 2c: 2.5-4.8 ms over the change
         * frames), no quiet frame, and the governor takes none of them */
        for (int k = 0; k < 300; k++) { GOV_FRAMES(1, 12.0, (k & 1) ? 1 : 0); }
        CHECK(!on);
        /* 9 quiet frames in a second (the rest change frames) are too few to judge by; 10 are enough. Each loop's first
         * frame is right after the last loop's change frame. */
        for (int k = 0; k < 100; k++) { GOV_FRAMES(1, 9.0, k < 10 ? 0 : 1); }
        CHECK(!on);
        for (int k = 0; k < 100; k++) { GOV_FRAMES(1, 9.0, k < 11 ? 0 : 1); }
        CHECK(on && last == 9.0);
        /* 3 s at least between turns: a recovery 1 and 2 s after the turn on is held, the one at 3 s turns it off */
        GOV_FRAMES(100, 5.0, 0);
        CHECK(on && last < 6.0);
        GOV_FRAMES(100, 5.0, 0);
        CHECK(on && last == 5.0);
        GOV_FRAMES(100, 5.0, 0);
        CHECK(!on);
        GOV_FRAMES(200, 9.0, 0);
        CHECK(!on && last == 9.0);                                       /* 1 and 2 s after the turn off: held off */
        GOV_FRAMES(100, 9.0, 0);
        CHECK(on);
#undef GOV_FRAMES
        CoXlTestStreamGovernStats(&onTurns, &offTurns, &held, &drained, &nearKept);
        CHECK(onTurns == 3 && offTurns == 2);
        CoXlTestStreamGovernSet(0);
        co_framecost_test_reset(0);
    }

    /* The governor's floor: no stamp within kStreamOutRadius (800 m) of a player, no mission stamp, no near slot (no far
     * radius) is ever taken; the farthest far ones go first, WINDOW_FAR_EVICT a scan, and no far respawn past 800 m. */
    Start("the governor's near guard: the farthest far stamps past 800 m only, never a near or mission one; far respawns held");
    {
        const float players[4] = {AX, AY, AX + 10000.0f, AY};
        StreamGame();
        Stream(200);
        for (uint32_t k = 0; k < 120; k++)
        {
            /* 0..39 within 800 m of player 1, 40..119 past it, farther with k; 50 a mission stamp; 60 a near slot */
            const float x = k < 40 ? AX + 20.0f * (float)k : AX + 900.0f + 10.0f * (float)k;
            Slot(k, x, AY, 2.0f, 0x1000u * (k + 1), k == 50);
            CoXlTestStreamOut2(k, k == 60 ? 0.0f : 1500.0f);
        }
        Slot(120, AX + 10000.0f + 700.0f, AY, 2.0f, 0x1000u * 121, 0); CoXlTestStreamOut2(120, 1500.0f); /* near player 2 */
        CoXlTestStreamPlayers(2, players);
        CHECK(CoXlTestStreamGovernMay(10) == 0 && CoXlTestStreamGovernMay(39) == 0); /* 200 and 780 m */
        CHECK(CoXlTestStreamGovernMay(40) == 1 && CoXlTestStreamGovernMay(119) == 1);
        CHECK(CoXlTestStreamGovernMay(50) == 0 && CoXlTestStreamGovernMay(60) == 0 && CoXlTestStreamGovernMay(120) == 0);
        CHECK(CoXlTestStreamGovernMay(121) == 0);                        /* not live */
        uint32_t onTurns, offTurns, held, drained, nearKept;
        CoXlTestStreamGovernStats(&onTurns, &offTurns, &held, &drained, &nearKept);
        CHECK(nearKept == 3);                                            /* 10, 39 and 120: within 800 m of a player */
        CoXlTestStreamGovernSet(1);
        CHECK(CoXlTestWindowGovernEvictRange(0, 121) == 64);
        for (uint32_t k = 0; k < 40; k++) CHECK(!CoXlTestStreamQueued(k));
        CHECK(!CoXlTestStreamQueued(50) && !CoXlTestStreamQueued(60) && !CoXlTestStreamQueued(120));
        uint32_t first = 0;
        for (uint32_t k = 40; k < 120; k++) if (CoXlTestStreamQueued(k)) { first = k; break; }
        for (uint32_t k = first; k < 120; k++) CHECK(k == 50 || k == 60 || CoXlTestStreamQueued(k)); /* the farthest */
        CHECK(first > 40);
        CHECK(CoXlTestWindowGovernEvictRange(0, 121) == 0);              /* the queue holds far ones: none more yet */
        CoXlTestStreamGovernStats(&onTurns, &offTurns, &held, &drained, &nearKept);
        CHECK(drained == 64);
        /* the far ring's scan: held only while on and acting */
        const uint32_t heldBefore = held;
        CHECK(CoXlTestWindowGovernHolds() == 0);                         /* on, observing only: scanned */
        CoXlTestStreamGovernStats(&onTurns, &offTurns, &held, &drained, &nearKept);
        CHECK(held == heldBefore);
        CoXlTestStreamGovActs(1);
        CHECK(CoXlTestWindowGovernHolds() == 1);
        CoXlTestStreamGovernStats(&onTurns, &offTurns, &held, &drained, &nearKept);
        CHECK(held == heldBefore + 1 && drained == 64);                  /* the queue still holds far ones: none more */
        CoXlTestStreamGovActs(0);
        /* respawns: past 800 m held while on and acting, near ones not; off or only observing, none held by it */
        CoXlTestRespawnRoom(130, 900.0f * 900.0f);
        CHECK(CoXlTestStreamGovernHeldRespawns() == 0);                  /* on, observing only: the default */
        CoXlTestStreamGovActs(1);
        CHECK(CoXlTestRespawnRoom(130, 900.0f * 900.0f) == 0 && CoXlTestStreamGovernHeldRespawns() == 1);
        CoXlTestRespawnRoom(131, 700.0f * 700.0f);
        CHECK(CoXlTestStreamGovernHeldRespawns() == 1);
        CoXlTestStreamGovernSet(0);
        CoXlTestRespawnRoom(130, 900.0f * 900.0f);
        CHECK(CoXlTestStreamGovernHeldRespawns() == 1);
        CoXlTestStreamGovActs(0);
        CoXlTestStreamPlayers(0, players);
    }

    /* A far stamp queued out waits 2-8 s for its removal (64-256 queued), 90-360 m at 45 m/s: asked again at the drain, a
     * player within 800 m keeps it (the near ring would take it again: a visible pop and a removal and spawn more). */
    Start("a far removal is not done at the drain once a player came within 800 m; the others go");
    {
        const float players[2] = {AX, AY};
        uint32_t left = 9, nearWindow = 9, nearGov = 9; int cutFar = 0, liveFar = 0, dueFar = 0; uint64_t unitFar = 0;
        StreamGame();
        Stream(10);
        for (uint32_t k = 0; k < 4; k++)
        {
            Slot(k, AX + (k == 0 ? 700.0f : k == 1 ? 800.0f : 1000.0f + 100.0f * (float)k), AY, 2.0f, 0x1000u * (k + 1), 0);
            CoXlTestStreamOut2(k, 1500.0f);
            CHECK(CoXlTestStreamDropGovPush(k) && CoXlTestStreamDropKind(k) == 4);
        }
        CoXlTestStreamPlayers(1, players);
        uint32_t w0 = 0, g0 = 0;
        CoXlTestStreamFarNearRemoved(&w0, &g0);
        const uint32_t before = g_destroys, skipped = CoXlTestStreamFarNearSkipped(), evicted = CoXlTestWindowEvictedFar();
        CHECK(CoXlTestStreamDrainLayer(64, 1.0e9, -1, &left, &cutFar) == 2 && g_destroys == before + 2 && left == 0);
        CHECK(CoXlTestStreamFarNearSkipped() == skipped + 2 && CoXlTestWindowEvictedFar() == evicted + 2); /* far slots freed */
        CoXlTestStreamFarNearRemoved(&nearWindow, &nearGov);
        CHECK(nearWindow == w0 && nearGov == g0);                        /* the governor's: none within 800 m */
        CoXlTestStreamSlotState(0, &liveFar, &dueFar, &unitFar); CHECK(liveFar);  /* 700 m: kept */
        CoXlTestStreamSlotState(1, &liveFar, &dueFar, &unitFar); CHECK(liveFar);  /* 800 m: kept */
        CoXlTestStreamSlotState(2, &liveFar, &dueFar, &unitFar); CHECK(!liveFar); /* 1200 m: removed */
        CoXlTestStreamSlotState(3, &liveFar, &dueFar, &unitFar); CHECK(!liveFar);
        CoXlTestStreamPlayers(0, players);
    }

    /* The window's own far eviction over the near budget takes far stamps from 550 m: it makes the room the near
     * stamps need, so the drain removes them however near; the governor's queued past 800 m and met at 700 m stays. */
    Start("the window's far eviction removes at 600-750 m and counts it; the governor's entry met at 700 m is kept");
    {
        float players[2] = {AX, AY};
        uint32_t left = 9, nearWindow = 9, nearGov = 9; int cutFar = 0, liveFar = 0, dueFar = 0; uint64_t unitFar = 0;
        StreamGame();
        Stream(10);
        for (uint32_t k = 0; k < 4; k++) Slot(k, AX + 600.0f + 50.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); /* 600-750 m */
        CoXlTestStreamPlayers(1, players);
        CHECK(CoXlTestWindowEvictFar(0, 4) == 4);
        for (uint32_t k = 0; k < 4; k++) CHECK(CoXlTestStreamDropKind(k) == 3); /* the window's kind */
        uint32_t w0 = 0, g0 = 0;
        CoXlTestStreamFarNearRemoved(&w0, &g0);
        const uint32_t before = g_destroys, skipped = CoXlTestStreamFarNearSkipped();
        CHECK(CoXlTestStreamDrainLayer(64, 1.0e9, -1, &left, &cutFar) == 4 && g_destroys == before + 4 && left == 0);
        CHECK(CoXlTestStreamFarNearSkipped() == skipped);
        CoXlTestStreamFarNearRemoved(&nearWindow, &nearGov);
        CHECK(nearWindow == w0 + 4 && nearGov == g0);
        for (uint32_t k = 0; k < 4; k++) { CoXlTestStreamSlotState(k, &liveFar, &dueFar, &unitFar); CHECK(!liveFar); }
        /* the governor queues one at 1000 m; the player comes to 700 m of it before the drain */
        Slot(5, AX + 1000.0f, AY, 2.0f, 0x6000u, 0);
        CoXlTestStreamOut2(5, 1500.0f);
        CoXlTestStreamGovernSet(1);
        CHECK(CoXlTestWindowGovernEvictRange(5, 6) == 1 && CoXlTestStreamDropKind(5) == 4);
        CoXlTestStreamGovernSet(0);
        players[0] = AX + 300.0f;
        CoXlTestStreamPlayers(1, players);
        CHECK(CoXlTestStreamDrainLayer(64, 1.0e9, -1, &left, &cutFar) == 0 && g_destroys == before + 4 && left == 0);
        CHECK(CoXlTestStreamFarNearSkipped() == skipped + 1 && CoXlTestStreamDropKind(5) == 0);
        CoXlTestStreamSlotState(5, &liveFar, &dueFar, &unitFar); CHECK(liveFar);
        CoXlTestStreamFarNearRemoved(&nearWindow, &nearGov);
        CHECK(nearWindow == w0 + 4 && nearGov == g0);
        CoXlTestStreamNote();
        CHECK(fake_host_logged("the governor's removals not done at the drain, a player within 800 m: 1; far removals done with a player within it: "
                               "the governor's 0, the window's near budget's 4; live stamps within 800 m "));
        CoXlTestStreamPlayers(0, players);
    }

    /* A spawn frame's budget: the frame target less the longest of the last 32 quiet frames, the learned spawn cost and
     * this frame's tile step, so a spawn frame stays near the target. */
    Start("a spawn frame's budget: the target less the quiet frames' longest, the learned spawn cost and the tile step");
    CHECK(CoXlTestStreamBudget(0.0, 3.1, 0.0) == 3.0);                   /* no quiet frames measured yet */
    CHECK(fabs(CoXlTestStreamBudget(11.2, 3.1, 0.0) - 2.4) < 1e-9);
    CHECK(fabs(CoXlTestStreamBudget(11.2, 3.1, 1.0) - 1.4) < 1e-9);       /* a light tile step in the same frame */
    CHECK(CoXlTestStreamBudget(14.8, 3.1, 0.0) == 0.5);                  /* no room: the floor */
    CHECK(CoXlTestStreamBudget(8.0, 1.0, 0.0) == 6.0);                   /* the most */
    /* Removals go in frames of their own (ANALYSIS-r2a §2.3: ~3 ms of step whatever the count): gathered to 8, or the
     * oldest waiting 250 ms, in a frame whose frame before left 4 ms; any frame once the oldest waited 500 ms. */
    Start("removal frames: gathered, in a frame with room, forced after 500 ms; their cost learned apart from spawn frames");
    {
        int64_t t = 1000000;
        double hi = 0, qw = 0, lw = 0, sum = 0, rm = 0, sp = 0; uint32_t floors = 0, builds = 0, rf = 0, forced = 0, sf = 0, gh = 0, gf = 0; int kind = 0;
        co_framecost_test_reset(1000000);
        for (int k = 0; k < 34; k++) { co_framecost_test_begin(t, 1, 1); co_framecost_test_world(1, t + 100); co_framecost_test_world(0, t + 5100); t += k == 3 ? 16500 : 10000; }
        CHECK(co_framecost_recent(&hi, &qw, &lw) == 0 && hi == 16.5 && fabs(qw - 5.0) < 1e-9);
        StreamGame();
        Stream(10);
        for (uint32_t k = 0; k < 4; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
        CoXlTestStreamObserver(1, AX, AY);
        CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 4);
        CoXlTestStreamChangeAt(0);
        CoXlTestStreamRemoveWait(0, 0);
        CoXlTestStreamCost(3.0, 0.5, &rm, &sp, &kind);
        co_framecost_test_begin(t, 1, 1);                                /* the change frame's own: the frame before was 10 ms */
        CHECK(CoXlTestStreamChange(20000, 0, &next) == 0 && g_destroys == 0);  /* 4 queued, none waited: gathering */
        CHECK(CoXlTestStreamChange(20249, 0, &next) == 0 && g_destroys == 0);
        CHECK(CoXlTestStreamChange(20250, 0, &next) == 4 && g_destroys == 4);  /* the oldest waited 250 ms */
        CoXlTestStreamBudgetStats(&sum, &floors, &builds);
        CHECK(fabs(sum - 3.7) < 1e-9 && floors == 0);                    /* 16.7 - 10 (the frame before) - 3.0 (removal cost) */
        CoXlTestStreamPace(&rf, &forced, &sf, &gh, &gf);
        CHECK(rf == 1 && forced == 0 && sf == 0);
        co_framecost_test_world(1, t + 100); co_framecost_test_world(0, t + 8100); t += 12000;
        co_framecost_test_begin(t, 1, 1);                                /* closes it */
        CHECK(co_framecost_recent(&hi, &qw, &lw) == 1 && fabs(lw - 8.0) < 1e-9);
        {
            uint32_t frames[4]; double world[4], grassWorld = 0; uint32_t grass = 0, bodyGrass = 0;
            (void)frames; (void)world; (void)grassWorld; (void)grass; (void)bodyGrass;
        }
        /* the next tick waits for its after-change frame (run 2c: 2.5-4.8 ms over the change frames, taken for quiet) */
        CoXlTestStreamCost(2.0, 0, &rm, &sp, &kind);
        CHECK(kind == 1);
        CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(rm == 2.0 && kind == 1);
        co_framecost_test_world(1, t + 100); co_framecost_test_world(0, t + 6100); t += 10000;
        co_framecost_test_begin(t, 1, 1);                                /* closes the after-change frame */
        /* the tick after it learns from the pair: (8 + 6) - 2 x 5 = 4 ms, a quarter of the way */
        CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - 2.5) < 1e-9 && fabs(sp - 0.5) < 1e-9 && kind == 0);
        CoXlTestStreamCostLearn();                                       /* once */
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - 2.5) < 1e-9);
        /* one sample is held to 4 x the estimate, 1 ms at least and 12 at most */
        CoXlTestStreamCost(0.5, 0, &rm, &sp, &kind); CoXlTestStreamCostKind(1); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - (0.75 * 0.5 + 0.25 * 2.0)) < 1e-9);
        CoXlTestStreamCost(0.1, 0, &rm, &sp, &kind); CoXlTestStreamCostKind(1); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - (0.75 * 0.1 + 0.25 * 1.0)) < 1e-9);
        CoXlTestStreamCost(10.0, 0, &rm, &sp, &kind); CoXlTestStreamCostKind(1); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - (0.75 * 10.0 + 0.25 * 4.0)) < 1e-9);
        /* 12 ms at most whatever the estimate: a 20 ms pair ((25 + 5) - 2 x 5) over an estimate of 10 counts as 12 */
#define COST_PAIR(changeWorld, removed, spawned, afterWorld, how)                                                             co_framecost_test_world(1, t + 100); co_framecost_bodies(1); co_framecost_change(removed, spawned); how;              co_framecost_test_world(0, t + 100 + (int64_t)((changeWorld) * 1000.0)); t += 30000;                                 co_framecost_test_begin(t, 1, 1);                                                                                     co_framecost_test_world(1, t + 100); co_framecost_test_world(0, t + 100 + (int64_t)((afterWorld) * 1000.0)); t += 10000;         co_framecost_test_begin(t, 1, 1)
        COST_PAIR(25.0, 1, 0, 5.0, (void)0);
        CoXlTestStreamCost(10.0, 0, &rm, &sp, &kind); CoXlTestStreamCostKind(1); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - (0.75 * 10.0 + 0.25 * 12.0)) < 1e-9);
        /* a spawn frame's pair goes to the spawn cost only, a removal frame's pair not: (9 + 6) - 10 = 5, held to 2 */
        CoXlTestStreamCost(3.0, 0.5, &rm, &sp, &kind); CoXlTestStreamCostKind(2); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - 3.0) < 1e-9 && fabs(sp - 0.5) < 1e-9 && kind == 0); /* the pair just closed was a removal frame's */
        COST_PAIR(9.0, 0, 1, 6.0, (void)0);
        CoXlTestStreamCostKind(2); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - 3.0) < 1e-9 && fabs(sp - (0.75 * 0.5 + 0.25 * 2.0)) < 1e-9);
        /* a pair a far-grass job was out in, or one kept apart, says nothing of its cost */
        COST_PAIR(20.0, 1, 0, 5.0, co_framecost_grass(1));
        CoXlTestStreamCost(3.0, 0.5, &rm, &sp, &kind); CoXlTestStreamCostKind(1); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - 3.0) < 1e-9);
        COST_PAIR(20.0, 1, 0, 5.0, co_framecost_apart());
        CoXlTestStreamCostKind(1); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - 3.0) < 1e-9);
        co_framecost_test_world(1, t + 100); co_framecost_bodies(1); co_framecost_change(1, 0); co_framecost_test_world(0, t + 20100); t += 30000;
        co_framecost_test_begin(t, 1, 1);
        co_framecost_test_world(1, t + 100); co_framecost_grass(1); co_framecost_test_world(0, t + 5100); t += 10000;
        co_framecost_test_begin(t, 1, 1);                                /* a far-grass job out in the after-change frame */
        CoXlTestStreamCostKind(1); CoXlTestStreamCostLearn();
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(fabs(rm - 3.0) < 1e-9 && kind == 0);
        /* the quiet frames not measured yet (fewer than 32 in this play): no sample, though their world update reads */
        {
            CoFcRecent r;
            co_framecost_test_reset(1000000);
            for (int k = 0; k < 3; k++) { co_framecost_test_begin(t, 1, 1); co_framecost_test_world(1, t + 100); co_framecost_test_world(0, t + 5100); t += 10000; }
            co_framecost_test_begin(t, 1, 1);
            COST_PAIR(9.0, 1, 0, 6.0, (void)0);
            co_framecost_recent_all(&r);
            CHECK(r.quietHi == 0.0 && r.quietWorld == 5.0 && r.lastAfter && r.lastAfterKind == 1 && r.pairClean);
            CoXlTestStreamCostKind(1); CoXlTestStreamCostLearn();
            CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
            CHECK(fabs(rm - 3.0) < 1e-9 && kind == 0);
        }
#undef COST_PAIR
        /* a removal's own time: the drain's over its removals, a quarter of the way, one sample 2 ms at most; the room a
         * chosen removal frame needs is the step cost and 8 removals' own time */
        CHECK(CoXlTestStreamOwn(0.3) == 0.3);
        CoXlTestStreamOwnLearn(4.0, 10);
        CHECK(fabs(CoXlTestStreamOwn(0) - (0.75 * 0.3 + 0.25 * 0.4)) < 1e-9);
        CoXlTestStreamOwn(0.3); CoXlTestStreamOwnLearn(50.0, 10);
        CHECK(fabs(CoXlTestStreamOwn(0) - (0.75 * 0.3 + 0.25 * 2.0)) < 1e-9);
        CoXlTestStreamOwn(0.3); CoXlTestStreamOwnLearn(5.0, 0);
        CHECK(CoXlTestStreamOwn(0) == 0.3);                              /* no removal: nothing learned */
        CHECK(fabs(CoXlTestStreamRemoveRoom() - 5.4) < 1e-9);           /* 3 + 8 x 0.3 */
        /* the choice itself: nothing queued, gathering, a frame without room, the spawn frame's turn, the forced wait */
        CoXlTestStreamRemoveWait(0, 0);
        StreamGame();
        Stream(20);
        for (uint32_t k = 0; k < 12; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
        CoXlTestStreamExitRate(0.0);
        CHECK(CoXlTestStreamRemovePick(1000, 10.0, 0) == 0);             /* none queued */
        CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 12);
        CoXlTestStreamRemoveWait(1000, 0);
        CHECK(CoXlTestStreamRemovePick(1000, 10.0, 0) == 1);             /* 12 queued: past the gather of 8 */
        CHECK(CoXlTestStreamRemovePick(1000, 0.0, 0) == 1);              /* the frame before not measured yet */
        CHECK(CoXlTestStreamRemovePick(1000, 11.29, 0) == 1);
        CHECK(CoXlTestStreamRemovePick(1000, 11.3, 0) == 0);             /* the frame before left under 3 + 8 x 0.3 ms */
        CoXlTestStreamOwn(0.5);
        CHECK(CoXlTestStreamRemovePick(1000, 9.69, 0) == 1 && CoXlTestStreamRemovePick(1000, 9.71, 0) == 0); /* 3 + 8 x 0.5 */
        CoXlTestStreamOwn(0.3);
        CHECK(CoXlTestStreamRemovePick(1499, 15.0, 0) == 0);
        CHECK(CoXlTestStreamRemovePick(1500, 15.0, 0) == 2);             /* the oldest waited 500 ms: forced */
        CoXlTestStreamRemoveWait(1000, 1);
        CHECK(CoXlTestStreamRemovePick(1000, 10.0, 1) == 0);             /* after a removal frame, spawns waiting: their turn */
        CHECK(CoXlTestStreamRemovePick(1000, 10.0, 0) == 1);
        CHECK(CoXlTestStreamRemovePick(1500, 10.0, 1) == 0);             /* forced too: a stalled far-grass job forced every try */
        CoXlTestStreamRemoveWait(1000, 2);
        CHECK(CoXlTestStreamRemovePick(1500, 10.0, 1) == 2);             /* after the spawn frame: forced */
        CoXlTestStreamRemoveWait(0, 0);
        /* a forced frame: the frame before 15 ms (no room), the oldest 500 ms waiting: at least 8 go whatever the time */
        CoXlTestStreamChangeAt(0);
        co_framecost_test_world(1, t + 100); co_framecost_test_world(0, t + 5100); t += 15000;
        co_framecost_test_begin(t, 1, 1);
        CHECK(CoXlTestStreamChange(40000, 0, &next) == 0);               /* the wait starts */
        CHECK(CoXlTestStreamChange(40499, 0, &next) == 0);
        const uint32_t before = g_destroys;
        CHECK(CoXlTestStreamChange(40500, 0, &next) >= 8 && g_destroys - before >= 8);
        CoXlTestStreamPace(&rf, &forced, &sf, &gh, &gf);
        CHECK(rf == 1 && forced == 1);                                   /* since the level's set-up */
        /* a forced frame's minimum is the backlog, not the gather's 8, up to 8 ms of own time (24 at 0.3 ms): 24 of 50
         * taken although each removal costs more than the frame's time; the 26 left keep the busy wait, and the next
         * change try comes in 2 frames, not kStreamChangeGap */
        StreamGame();
        Stream(60);
        for (uint32_t k = 0; k < 50; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
        CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 50);
        CHECK(CoXlTestStreamFloorLeast(0, 0.3) == 24 && CoXlTestStreamRemoveWaitNow() == 100);
        CoXlTestStreamExitRate(0.0);
        CoXlTestStreamChangeAt(0);
        CoXlTestStreamRemoveWait(1, 0);                                  /* waited since the start of the clock: forced */
        g_destroySpin = 0.6;
        CHECK(CoXlTestStreamChange(60000, 0, &next) == 24 && g_destroys == 24 && CoXlTestStreamQueue(order, 256) == 26);
        g_destroySpin = 0.0;
        CHECK(CoXlTestStreamOwn(0) > 0.3 && CoXlTestStreamRemoveWaitNow() == 100);
        CHECK(next == 60000 && CoXlTestStreamNextTick() == CoXlTestStreamTicks() + 2); /* 50 queued: no backlog flag */
        /* the rest in the next forced frame; the next scan finds 2 past their radius: no busy wait */
        CoXlTestStreamOwn(0.3);
        CoXlTestStreamTicksAdd(); CoXlTestStreamTicksAdd();
        CoXlTestStreamRemoveWait(60000, 2);
        CHECK(CoXlTestStreamChange(60100, 0, &next) == 24 && CoXlTestStreamQueue(order, 256) == 2);
        CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamRemoveWaitNow() == 500);
        co_framecost_test_reset(0);
        CoXlTestStreamObserver(0, 0.0f, 0.0f);
    }

    /* A far-grass bake job's frames ran 21 ms against 13 (run 2a): no change frame while one is out, 500 ms at most. */
    Start("no change frame while a far-grass job is out, at most 500 ms; one each 500 ms while a job stalls");
    {
        uint32_t rf = 0, forced = 0, sf = 0, gh = 0, gf = 0;
        StreamGame();
        Stream(20);
        for (uint32_t k = 0; k < 20; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
        CoXlTestStreamObserver(1, AX, AY);
        CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 20);
        CoXlTestStreamChangeAt(0);
        CoXlTestStreamRemoveWait(0, 0);
        CoXlTestFarGrassOut(1);
        CHECK(CoXlTestStreamChange(50000, 0, &next) == 0 && g_destroys == 0);
        CHECK(CoXlTestStreamChange(50499, 0, &next) == 0 && g_destroys == 0);
        CoXlTestStreamPace(&rf, &forced, &sf, &gh, &gf);
        CHECK(gh == 2 && gf == 0);
        CHECK(CoXlTestStreamChange(50500, 0, &next) > 0 && g_destroys > 0);    /* held 500 ms: let through */
        CoXlTestStreamPace(&rf, &forced, &sf, &gh, &gf);
        CHECK(gf == 1);
        const uint32_t d = g_destroys;
        for (uint32_t k = 0; k < 10; k++) { Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x2000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); CHECK(CoXlTestStreamDropPush(k)); }
        CoXlTestStreamChangeAt(0);
        CHECK(CoXlTestStreamChange(50600, 0, &next) == 0 && g_destroys == d); /* the hold starts again */
        CoXlTestFarGrassOut(0);
        CHECK(CoXlTestStreamChange(50601, 0, &next) == 10 && g_destroys == d + 10); /* the job is in: no hold */
        CoXlTestStreamObserver(0, 0.0f, 0.0f);
    }

    /* A change frame rebuilds the layers it touched: the removals of one streamed layer only, the others wait their turn. */
    Start("a drain of one broad phase layer: its stamps alone, the others kept in order; its count limit and its time");
    StreamGame();
    Stream(20);
    for (uint32_t k = 0; k < 20; k++)
    {
        Slot(k, AX + 1000.0f + 4.0f * (float)(19 - k), AY, 2.0f, 0x1000u * (k + 1), 0);
        CoXlTestStreamOutSince(k, 1);
        CoXlTestStreamLayerSet(k, k < 5 ? 6u : k < 15 ? 7u : 0u);
    }
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 20 && order[0] == 0 && order[19] == 19);
    int cut = 0;
    CHECK(CoXlTestStreamDrainLayer(2, 1.0e9, 7, &waiting, &cut) == 2 && waiting == 18 && g_destroys == 2 && cut == 0);
    CoXlTestStreamSlotState(5, &live, &due, &unit); CHECK(!live);
    CoXlTestStreamSlotState(6, &live, &due, &unit); CHECK(!live);
    CoXlTestStreamSlotState(0, &live, &due, &unit); CHECK(live); /* layer 6's: put back, not removed */
    CHECK(CoXlTestStreamDrainLayer(64, 1.0e9, 7, &waiting, &cut) == 8 && waiting == 10 && g_destroys == 10);
    CHECK(CoXlTestStreamQueue(order, 256) == 10 && order[0] == 15 && order[4] == 19 && order[5] == 0 && order[9] == 4); /* in turn */
    CHECK(CoXlTestStreamDrainLayer(64, 1.0e9, 4, &waiting, &cut) == 5 && waiting == 5 && g_destroys == 15); /* layer 4's */
    CHECK(CoXlTestStreamDrainLayer(64, 0.0, 6, &waiting, &cut) == 1 && waiting == 4 && cut == 1); /* past its time: one */
    CHECK(CoXlTestStreamDrainLayer(64, 1.0e9, 8, &waiting, &cut) == 0 && waiting == 4);
    CHECK(CoXlTestStreamDrainLayer(3, 0.0, 6, &waiting, &cut) == 1 && waiting == 3 && cut == 1);
    CHECK(CoXlTestStreamDrainLeast(64, 0.0, 6, 2, &waiting, &cut) == 2 && waiting == 1 && cut == 1); /* past its time: its floor of 2 */
    CHECK(CoXlTestStreamDrainLayer(64, 1.0e9, -1, &waiting, &cut) == 1 && waiting == 0 && g_destroys == 20 && cut == 0);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 0);
    Stream(3);
    for (uint32_t k = 0; k < 3; k++) { Slot(k, AX + 1000.0f, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 3);
    CHECK(CoXlTestStreamDrainLayer(64, 0.0, 4, &waiting, &cut) == 1 && cut == 1);
    CHECK(CoXlTestStreamDrainLayer(1, 0.0, 4, &waiting, &cut) == 1 && cut == 0); /* its count, not its time */
    CHECK(CoXlTestStreamDrainLayer(64, 0.0, 4, &waiting, &cut) == 1 && waiting == 0 && cut == 0); /* the last: nothing left */
    Stream(3);
    for (uint32_t k = 0; k < 3; k++) { Slot(k, AX + 1000.0f, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamLayerSet(1, 7); CoXlTestStreamLayerSet(2, 7);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 3 && order[2] == 0);
    CHECK(CoXlTestStreamDrainLayer(64, 0.0, 4, &waiting, &cut) == 1 && waiting == 2 && cut == 0); /* the others are layer 7's */
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    Start("the change frame's layers: the most queued removals drained, the spawns there unless it is well over the smallest");
    {
        int drain = 0, turn = 0; uint32_t spawn = 0;
        const uint32_t even[5] = {300000, 1000, 2000, 500, 3000}, none[5] = {0, 0, 0, 0, 0};
        CoXlTestSlPick(even, none, 1, &turn, &drain, &spawn);
        CHECK(drain == 8 && spawn == 8 && turn == 0);                /* nothing queued: the smallest */
        const uint32_t q7[5] = {0, 5, 9, 0, 0};
        CoXlTestSlPick(even, q7, 0, &turn, &drain, &spawn);
        CHECK(drain == 7 && spawn == 0 && turn == 0);                /* 2000 over 500 + 6500 / 8: its removals alone */
        CoXlTestSlPick(even, q7, 1, &turn, &drain, &spawn);
        CHECK(drain == 8 && spawn == 8 && turn == 1);                /* spawns due: turns, the smallest's spawns first */
        CoXlTestSlPick(even, q7, 1, &turn, &drain, &spawn);
        CHECK(drain == 7 && spawn == 0 && turn == 0);                /* then layer 7's removals */
        const uint32_t within[5] = {300000, 1000, 1728, 900, 3000};
        CoXlTestSlPick(within, q7, 1, &turn, &drain, &spawn);
        CHECK(drain == 7 && spawn == 7 && turn == 0);                /* 1728 within 900 + 6628 / 8: one layer for both */
        const uint32_t past[5] = {300000, 1000, 1729, 900, 3000};
        CoXlTestSlPick(past, q7, 0, &turn, &drain, &spawn);
        CHECK(drain == 7 && spawn == 0);                             /* 1729 past 900 + 6629 / 8 */
        const uint32_t tie[5] = {9, 5, 9, 0, 0};
        CoXlTestSlPick(even, tie, 0, &turn, &drain, &spawn);
        CHECK(drain == 4 && spawn == 0);                             /* a tie to layer 4, which takes no spawns */
        CoXlTestSlPick(even, tie, 1, &turn, &drain, &spawn);
        CHECK(drain == 8 && spawn == 8 && turn == 1);
        CoXlTestSlPick(even, tie, 1, &turn, &drain, &spawn);
        CHECK(drain == 4 && spawn == 0 && turn == 0);
        const uint32_t small4[5] = {10, 1000, 2000, 500, 3000}, q4[5] = {3, 0, 0, 0, 0};
        CoXlTestSlPick(small4, q4, 0, &turn, &drain, &spawn);
        CHECK(drain == 4 && spawn == 0);
        const uint32_t last[5] = {0, 0, 0, 0, 1};
        CoXlTestSlPick(even, last, 0, &turn, &drain, &spawn);
        CHECK(drain == 9 && spawn == 0);
        const uint32_t first[5] = {0, 2, 1, 1, 1};
        CoXlTestSlPick(even, first, 1, &turn, &drain, &spawn);
        CHECK(drain == 6 && spawn == 6 && turn == 0);                /* 1000 within 500 + 812 */
    }

    Start("the change frame's layers from the queue and the broad phase; none when the hooks are off or it has 6 layers");
    StreamGame();
    Stream(10);
    for (uint32_t k = 0; k < 6; k++) { Slot(k, AX + 1000.0f, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamLayerSet(0, 9); CoXlTestStreamLayerSet(1, 9); CoXlTestStreamLayerSet(2, 6); CoXlTestStreamLayerSet(3, 0);
    CoXlTestStreamLayerSet(4, 9); CoXlTestStreamLayerSet(5, 7);
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 6);
    {
        const uint32_t leaves[10] = {10, 0, 0, 0, 400000, 0, 4000, 9000, 3000, 9500};
        int drain = 0; uint32_t spawn = 0, rebuilt = 0;
        uint32_t read[5] = {0};
        BroadPhase(10, leaves);
        CoXlTestSlReset();
        CoXlTestStreamLayersPick(0, &drain, &spawn, &rebuilt);
        CHECK(drain == -1 && spawn == 0 && rebuilt == 0 && !CoXlTestSlLeaves(read));   /* off, layer 4 not read */
        CoXlTestSlSet(1, (uintptr_t)g_fake.exe, NULL, NULL, NULL, NULL);
        CHECK(CoXlTestSlLeaves(read) && read[0] == 400000 && read[1] == 4000 && read[2] == 9000 && read[3] == 3000 && read[4] == 9500);
        CoXlTestStreamLayersPick(0, &drain, &spawn, &rebuilt);
        CHECK(drain == 9 && spawn == 0 && rebuilt == 9500);             /* three of layer 9's; 9500 over 3000 + 25500 / 8 */
        CoXlTestStreamLayersPick(1, &drain, &spawn, &rebuilt);
        CHECK(drain == 8 && spawn == 8 && rebuilt == 3000);             /* spawns due: the smallest's turn */
        CoXlTestStreamLayersPick(1, &drain, &spawn, &rebuilt);
        CHECK(drain == 9 && spawn == 0 && rebuilt == 9500);
        unsigned long long at = 0;
        uint32_t changes = 0, backlogs = 0, drainCuts = 0, spawnCuts = 0, changeRemoved = 0, drains[5], floors = 0, builds = 0;
        double budget = 0, rm = 0, sp = 0; int kind = 0;
        int64_t t = 1000000;
        co_framecost_test_reset(1000000);
        for (int k = 0; k < 34; k++) { co_framecost_test_begin(t, 1, 1); t += 10000; } /* quiet frames of 10 ms */
        CoXlTestStreamCost(3.0, 0.5, &rm, &sp, &kind);
        CoXlTestStreamChangeAt(0);
        CoXlTestStreamRemoveWait(0, 0);
        CHECK(CoXlTestStreamChange(10000, 0, &at) == 0 && g_destroys == 0);               /* 6 queued, none waited */
        CHECK(CoXlTestStreamChange(10250, 0, &at) == 3 && g_destroys == 3 && at == 10500); /* layer 9's three alone */
        CoXlTestStreamBudgetStats(&budget, &floors, &builds);
        CHECK(fabs(budget - (16.7 - 10.0 - 3.0)) < 1e-9 && floors == 0);  /* the frame before and the removal cost */
        CoXlTestStreamCost(0, 0, &rm, &sp, &kind);
        CHECK(kind == 1 && CoXlTestStreamLeaves() == 9500);             /* for the next tick to learn from */
        co_framecost_test_reset(0);
        CoXlTestStreamSlotState(2, &live, &due, &unit); CHECK(live);
        CHECK(CoXlTestStreamChange(10500, 0, &at) == 1 && g_destroys == 4);                 /* a tie: layer 4's first */
        CoXlTestStreamSlotState(3, &live, &due, &unit); CHECK(!live);
        CHECK(CoXlTestStreamChange(10750, 0, &at) == 1 && g_destroys == 5);                 /* 6 before 7 */
        CoXlTestStreamSlotState(2, &live, &due, &unit); CHECK(!live);
        CoXlTestStreamChangeStats(&changes, &backlogs, &drainCuts, &spawnCuts, &changeRemoved, drains);
        CHECK(changes == 3 && changeRemoved == 5 && drains[0] == 1 && drains[1] == 1 && drains[2] == 0 && drains[4] == 1);
        BroadPhase(6, leaves);
        CoXlTestStreamLayersPick(0, &drain, &spawn, &rebuilt);
        CHECK(drain == -1 && spawn == 0 && !CoXlTestSlLeaves(read));   /* a broad phase made before the hooks */
        BroadPhase(10, leaves);
        memcpy(g_slVt + 0x38, &(uint64_t){(uint64_t)(uintptr_t)g_fake.exe + 0xD40551u}, 8);
        CHECK(!CoXlTestSlLeaves(read));                                /* not the broad phase */
        CoXlTestSlReset();
    }
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    /* A removal frame takes the layer with the most queued and spawns nothing; forced (the oldest waited 500 ms) it
     * removes 8 at least whatever its time. A spawn frame goes to the smallest streamed layer and removes nothing. */
    Start("a removal frame: the most queued layer, 8 at least when forced; a spawn frame: the smallest layer, no removal");
    StreamGame();
    Stream(10);
    for (uint32_t k = 0; k < 6; k++) { Slot(k, AX + 1000.0f, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOutSince(k, 1); }
    CoXlTestStreamLayerSet(0, 9); CoXlTestStreamLayerSet(1, 9); CoXlTestStreamLayerSet(2, 6); CoXlTestStreamLayerSet(3, 0);
    CoXlTestStreamLayerSet(4, 9); CoXlTestStreamLayerSet(5, 7);
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 6);
    CoXlTestStreamDue(6, 0); CoXlTestStreamDue(7, 0);                  /* spawns due */
    CoXlTestStreamDistance(6, 100.0f * 100.0f); CoXlTestStreamDistance(7, 100.0f * 100.0f);
    {
        uint32_t stuck = 9, dry = 9, least = 9, exits = 9, exitChanges = 9; double rebuiltSum = 9;
        CoXlTestStreamFloorMore(&stuck, &dry, &rebuiltSum, &least, &exits, &exitChanges);
        CHECK(exits == 6 && exitChanges == 0 && stuck == 0 && dry == 0 && rebuiltSum == 0.0); /* the scan's 6 new, no change frame before */
    }
    {
        const uint32_t leaves[10] = {10, 0, 0, 0, 1000, 0, 4000, 9000, 3000, 9500};
        unsigned long long at = 0;
        uint32_t changes = 0, backlogs = 0, drainCuts = 0, spawnCuts = 0, changeRemoved = 0, drains[5], floors = 0, builds = 0;
        uint32_t held = 9, sMoved = 9, sQuiet = 9;
        double budget = 0, quietSum = 0, waitMs = 9, waitMost = 9, rm = 0, sp = 0; int kind = 0;
        int64_t t = 1000000;
        BroadPhase(10, leaves);
        CoXlTestSlReset();
        CoXlTestSlSet(1, (uintptr_t)g_fake.exe, NULL, NULL, NULL, NULL);
        co_framecost_test_reset(1000000);
        for (int k = 0; k < 34; k++) { co_framecost_test_begin(t, 1, 1); t += 16500; } /* quiet frames of 16.5 ms: no room */
        CoXlTestStreamCost(3.0, 0.5, &rm, &sp, &kind);
        CoXlTestStreamChangeAt(0);
        CoXlTestStreamRemoveWait(0, 0);
        CHECK(CoXlTestStreamChange(10000, 0, &at) == 0 && g_destroys == 0 && CoXlTestStreamSpawnTried() == 9); /* a spawn frame, layer 8 */
        CHECK(CoXlTestStreamChange(10499, 0, &at) == 0 && g_destroys == 0); /* no room in the frame before, not yet forced */
        g_destroySpin = 0.6;                                             /* each past the frame's time of 0 */
        CHECK(CoXlTestStreamChange(10500, 0, &at) == 3 && g_destroys == 3 && at == 10750 && CoXlTestStreamSpawnTried() == 0);
        g_destroySpin = 0.0;
        {
            uint32_t stuck = 9, dry = 9, least = 9, exits = 9, exitChanges = 9; double rebuiltSum = 9;
            CoXlTestStreamFloorMore(&stuck, &dry, &rebuiltSum, &least, &exits, &exitChanges);
            CHECK(least == 8 && dry == 0 && stuck == 0 && rebuiltSum == 9500.0); /* forced: the gather's 8 at least */
        }
        CoXlTestStreamChangeStats(&changes, &backlogs, &drainCuts, &spawnCuts, &changeRemoved, drains);
        CHECK(changes == 1 && drains[4] == 1);
        CoXlTestStreamBudgetStats(&budget, &floors, &builds);
        CoXlTestStreamFloorStats(&held, &quietSum, &sMoved, &sQuiet, &waitMs, &waitMost);
        CHECK(budget == 0.0 && floors == 1 && held == 0 && quietSum == 16.5); /* 16.7 - 16.5 - 3: none, counted at the floor */
        {
            uint32_t rf = 0, forced = 0, sf = 0, gh = 0, gf = 0;
            CoXlTestStreamPace(&rf, &forced, &sf, &gh, &gf);
            CHECK(rf == 1 && forced == 1 && sf == 0);
        }
        /* room again (32 quiet frames: the change frame's and the one right after it are none): the spawns' turn first,
         * then layer 4 by the tie */
        for (int k = 0; k < 34; k++) { co_framecost_test_begin(t, 1, 1); t += 10100; }
        CHECK(CoXlTestStreamChange(10750, 0, &at) == 0 && g_destroys == 3 && CoXlTestStreamSpawnTried() == 9);
        CHECK(CoXlTestStreamChange(10751, 0, &at) == 1 && g_destroys == 4 && CoXlTestStreamSpawnTried() == 0);
        CoXlTestStreamChangeStats(&changes, &backlogs, &drainCuts, &spawnCuts, &changeRemoved, drains);
        CHECK(changes == 2 && drains[0] == 1 && drains[4] == 1);
        CoXlTestStreamBudgetStats(&budget, &floors, &builds);
        CHECK(fabs(budget - 3.6) < 1e-9 && floors == 1);                 /* 16.7 - 10.1 - 3 */
        /* a spawn frame making nothing leaves the next try free: no change frame counted */
        CoXlTestStreamRemoveWait(11001, 2);
        CHECK(CoXlTestStreamChange(11001, 1, &at) == 0 && g_destroys == 4 && CoXlTestStreamSpawnTried() == 0); /* a layer fill: no spawn frame */
        /* a spawned one past the cursor is no due stamp: the reach looks on to the next, here out of it */
        CoXlTestStreamDistance(6, 100.0f * 100.0f); CoXlTestStreamDistance(7, 600.0f * 600.0f);
        CHECK(CoXlTestStreamDueInReach(500.0f * 500.0f) == 1);
        CoXlTestStreamSpawnedSet(6, 1);
        CHECK(CoXlTestStreamDueInReach(500.0f * 500.0f) == 0 && CoXlTestStreamDueInReach(0.0f) == 1);
        CoXlTestStreamSpawnedSet(6, 0);
        /* layers 6..9 moving after a frame the streamer changed nothing in (a tile destroying units a change frame's
         * flush put there): rebuilds out of the budget, counted as layer 4's are */
        uint32_t sFrames = 0, sLeaves = 0, s4Moved = 0, s4Quiet = 0;
        const uint32_t more7[10] = {10, 0, 0, 0, 1000, 0, 4000, 9100, 3000, 9500}, less9[10] = {10, 0, 0, 0, 1000, 0, 4000, 9100, 3000, 9400};
        CoXlTestStreamStatic(0, 0, &sFrames, &s4Moved, &s4Quiet, &sLeaves);   /* the first read */
        BroadPhase(10, more7);
        CoXlTestStreamStatic(0, 1, &sFrames, &s4Moved, &s4Quiet, &sLeaves);   /* after a frame with no change */
        CoXlTestStreamFloorStats(&held, &quietSum, &sMoved, &sQuiet, &waitMs, &waitMost);
        CHECK(sMoved == 1 && sQuiet == 1);
        BroadPhase(10, less9);
        CoXlTestStreamStatic(0, 0, &sFrames, &s4Moved, &s4Quiet, &sLeaves);   /* after the streamer's change frame */
        CoXlTestStreamFloorStats(&held, &quietSum, &sMoved, &sQuiet, &waitMs, &waitMost);
        CHECK(sMoved == 2 && sQuiet == 1);
        CoXlTestStreamStatic(0, 0, &sFrames, &s4Moved, &s4Quiet, &sLeaves);   /* no move */
        CoXlTestStreamStatic(1, 0, &sFrames, &s4Moved, &s4Quiet, &sLeaves);   /* a frame with no read */
        BroadPhase(10, more7);
        CoXlTestStreamStatic(0, 0, &sFrames, &s4Moved, &s4Quiet, &sLeaves);   /* not from one frame to the next */
        CoXlTestStreamFloorStats(&held, &quietSum, &sMoved, &sQuiet, &waitMs, &waitMost);
        CHECK(sMoved == 2 && sQuiet == 1 && s4Moved == 0);                     /* layer 4 stayed at 1000 */
        const uint32_t four[10] = {10, 0, 0, 0, 1100, 0, 4000, 9100, 3000, 9500};
        BroadPhase(10, four);
        CoXlTestStreamStatic(0, 0, &sFrames, &s4Moved, &s4Quiet, &sLeaves);   /* layer 4 alone moved */
        CoXlTestStreamFloorStats(&held, &quietSum, &sMoved, &sQuiet, &waitMs, &waitMost);
        CHECK(sMoved == 2 && sQuiet == 1);
        CoXlTestStreamNote();
        CHECK(fake_host_logged("layers 6..9 changed in 2 frames, 1 of them after a frame the streamer changed nothing in"));
        CHECK(fake_host_logged("budget 1.80 ms a frame, 1 at its floor of 0.5 ms (the quiet frames' longest of 32: mean 13.3 ms), "
                               "0 of those not caused by the quiet frames, 0 spawning near stamps only, 0 with a due stamp in reach and no layer fill "
                               "(0 locations spawned in them, 0 of them none), the last removal frame removing 8 at least (exits 6 over 0 removal frames); "
                               "change frame cost with its after-change frame estimated: removal 3.00 ms (its 0 samples' mean 0.00), spawn 0.50 ms (its 0 samples' mean 0.00)"));
        CHECK(fake_host_logged("streaming pace: 2 removal frames (1 forced by a 500 ms wait, 100 ms with a backlog), 0 spawn frames; 0 change tries held for a "
                               "far-grass job, 0 let through after 500 ms; the quiet frames' longest of 32: mean 13.3 ms without the far-grass and after-change frames, "
                               "13.3 ms with them; a removal's own time "));
        {
            uint32_t floorDue = 9, floorSpawned = 9; double sampleSum = 9; uint32_t samples = 9;
            CoXlTestStreamFloorDue(&floorDue, &floorSpawned);
            CoXlTestStreamSamples(&sampleSum, &samples);
            CHECK(floorDue == 0 && floorSpawned == 0 && samples == 0 && sampleSum == 0.0); /* since the note */
        }
        CoXlTestStreamFloorStats(&held, &quietSum, &sMoved, &sQuiet, &waitMs, &waitMost);
        CHECK(held == 0 && quietSum == 0.0 && sMoved == 0 && sQuiet == 0); /* since the note */
        co_framecost_test_reset(0);
        CoXlTestSlReset();
    }
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    /* A forced removal frame's minimum is the backlog (queued, or past their radius when more), 8 at least and 8 ms of
     * learned own time at most (24 at most); its wait is 100 ms while the backlog or the exits' pace is past one such
     * frame, so forced frames keep up with 240 a second (run 2c needed 60-154). A floor not caused by the quiet frames
     * is stuck; a moving tile's 0x63 render frame is kept out of the quiet frames. */
    Start("the forced removal frames' minimum and wait, a stuck floor, and the 0x63 render frame kept apart");
    StreamGame();
    Stream(4);
    CoXlTestStreamExitRate(0.0);
    CHECK(CoXlTestStreamFloorLeast(0, 0.3) == 8 && CoXlTestStreamFloorLeast(5, 0) == 8);  /* the gather's 8 at least */
    CHECK(CoXlTestStreamFloorLeast(13, 0) == 13 && CoXlTestStreamFloorLeast(24, 0) == 24); /* the backlog */
    CHECK(CoXlTestStreamRemoveWaitNow() == 500);
    CHECK(CoXlTestStreamFloorLeast(200, 0) == 24 && CoXlTestStreamRemoveWaitNow() == 100); /* 8 / 0.3 = 26: 24 at most; busy */
    CHECK(CoXlTestStreamFloorLeast(200, 0.5) == 16 && CoXlTestStreamFloorLeast(200, 1.6) == 8); /* 8 ms of own time; 8 at least */
    CHECK(CoXlTestStreamFloorLeast(17, 0.5) == 16 && CoXlTestStreamRemoveWaitNow() == 100);
    CHECK(CoXlTestStreamFloorLeast(16, 0.5) == 16 && CoXlTestStreamRemoveWaitNow() == 500);
    CoXlTestStreamFloorLeast(0, 0.3);
    CoXlTestStreamExitRate(48.0);
    CHECK(CoXlTestStreamRemoveWaitNow() == 500);                                          /* 24 exits in 500 ms: one forced frame */
    CoXlTestStreamExitRate(48.5);
    CHECK(CoXlTestStreamRemoveWaitNow() == 100);                                          /* the exits' pace past it */
    CoXlTestStreamExitRate(0.0);
    /* the starting spawn cost (0.5 ms) would have left 1.5 ms: not the quiet frames' doing */
    CHECK(!CoXlTestStreamFloorStuck(16.5, 9500, 0.0) && CoXlTestStreamFloorStuck(10.0, 100000, 0.0));
    CHECK(!CoXlTestStreamFloorStuck(14.0, 100000, 0.8) && !CoXlTestStreamFloorStuck(0.0, 0, 0.0));
    CHECK(CoXlTestStreamFloorStuck(14.69, 100000, 0.0) && !CoXlTestStreamFloorStuck(14.71, 100000, 0.0)); /* 1.5 ms left */
    CoXlTestStreamFloorLeast(0, 0);
    {
        int64_t t = 1000000;
        double hi = 0, qw = 0, lw = 0;
        co_framecost_test_reset(1000000);
        for (int k = 0; k < 34; k++) { co_framecost_test_begin(t, 1, 1); t += 10000; }
        CHECK(!CoXlTestRenderApart(1, 5, 0x62) && !CoXlTestRenderApart(0, 5, 0x63) && !CoXlTestRenderApart(1, 48, 0x63) && !CoXlTestRenderApart(1, -1, 0x63));
        co_framecost_test_begin(t, 1, 1);
        CHECK(CoXlTestRenderApart(1, 5, 0x63));                          /* its push done, the job drains in this frame */
        t += 185000;
        co_framecost_test_begin(t, 1, 1);
        CHECK(co_framecost_recent(&hi, &qw, &lw) == 0 && hi == 10.0);  /* the 185 ms frame is no quiet frame */
        t += 185000;
        co_framecost_test_begin(t, 1, 1);
        CHECK(co_framecost_recent(&hi, &qw, &lw) == 0 && hi == 185.0); /* the same frame not kept apart */
        for (int k = 0; k < 33; k++) { co_framecost_test_begin(t, 1, 1); t += 10000; }
        StreamGame();
        Stream(4);
        co_framecost_test_begin(t, 1, 1);
        CoXlTestStreamTickNear(1, 7, 0x63);                              /* the streamer's tick reads it */
        t += 185000;
        co_framecost_test_begin(t, 1, 1);
        CHECK(co_framecost_recent(&hi, &qw, &lw) == 0 && hi == 10.0);
        co_framecost_test_reset(0);
    }

    /* The exits are the stamps whose out timer ran out since the last scan, counted whatever the removal queue's room:
     * with the queue full (160) its new entries would only echo the removals done, and the minimum would lock. */
    Start("a scan's new exits with the removal queue full; the floor's minimum catches up on the stamps past the design");
    StreamGame();
    Stream(200);
    for (uint32_t k = 0; k < 200; k++)
    {
        Slot(k, AX + 1000.0f + 4.0f * (float)k, AY, 2.0f, 0x1000u * (k + 1), 0);
        CoXlTestStreamOutSince(k, k < 180 ? 1u : 1000u);
    }
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 160);
    {
        uint32_t stuck, dry, least, exits = 9, exitChanges = 9, expiredNow = 0, expiredMost = 0; double rebuiltSum;
        CoXlTestStreamFloorMore(&stuck, &dry, &rebuiltSum, &least, &exits, &exitChanges);
        CHECK(exits == 200 && exitChanges == 0);                          /* the first scan: all 200 */
        CoXlTestStreamScanAt(5500);                                       /* the last scan at 5.5 s: the 20 out since 1 s ran out after it */
        CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueue(order, 256) == 160); /* full: nothing new in it */
        CoXlTestStreamFloorMore(&stuck, &dry, &rebuiltSum, &least, &exits, &exitChanges);
        CoXlTestStreamExpired(&expiredNow, &expiredMost);
        CHECK(exits == 20 && exitChanges == 0 && expiredNow == 200);
        CoXlTestStreamOwn(0.3);
        CHECK(CoXlTestStreamLeastNow() == 24);                            /* 200 past their radius: 24 at most at 0.3 ms each */
        CHECK(CoXlTestStreamRemoveWaitNow() == 100);                      /* a backlog: the busy wait */
        CHECK(CoXlTestStreamScanFrames(0) == 0);
        CoXlTestStreamFloorMore(&stuck, &dry, &rebuiltSum, &least, &exits, &exitChanges);
        CHECK(exits == 0);                                                /* none ran out since */
        /* the exits' pace: 20 ran out in the second since the last scan, a quarter of the way from 0 */
        const ULONGLONG wall = GetTickCount64();
        for (uint32_t k = 180; k < 200; k++) CoXlTestStreamOutSince(k, wall - 5500u);
        CoXlTestStreamExitRate(0.0);
        CoXlTestStreamScanAt(wall - 1000u);
        CHECK(CoXlTestStreamScanFrames(0) == 0);
        CoXlTestStreamFloorMore(&stuck, &dry, &rebuiltSum, &least, &exits, &exitChanges);
        const double pace = CoXlTestStreamExitRate(-1.0);
        CHECK(exits == 20 && pace > 4.5 && pace <= 5.0);
        CoXlTestStreamExitRate(0.0);
    }
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    /* A spawn frame fills the emptiest streamed layer, so the four stay near a quarter each; with the split off its
     * stamps stay in layer 4 (no layer of their own). */
    Start("a spawn frame goes to the smallest streamed layer, to layer 4 with the split off");
    StreamGame();
    Stream(10);
    CoXlTestStreamObserver(1, AX, AY);
    CoXlTestStreamDue(6, 0);
    {
        const uint32_t leaves[10] = {10, 0, 0, 0, 1000, 0, 130000, 130000, 120000, 130000};
        const uint32_t small6[10] = {10, 0, 0, 0, 1000, 0, 1200, 130000, 120000, 130000};
        unsigned long long at = 0;
        int64_t t = 1000000;
        BroadPhase(10, leaves);
        CoXlTestSlReset();
        co_framecost_test_reset(1000000);
        for (int k = 0; k < 34; k++) { co_framecost_test_begin(t, 1, 1); t += 13000; }
        CoXlTestStreamChangeAt(0);
        CoXlTestStreamRemoveWait(0, 0);
        CHECK(CoXlTestStreamChange(10000, 0, &at) == 0 && CoXlTestStreamSpawnTried() == 1); /* off: layer 4 */
        CoXlTestSlSet(1, (uintptr_t)g_fake.exe, NULL, NULL, NULL, NULL);
        CHECK(CoXlTestStreamChange(10001, 0, &at) == 0 && CoXlTestStreamSpawnTried() == 9); /* layer 8's 120,000 */
        BroadPhase(10, small6);
        CHECK(CoXlTestStreamChange(10002, 0, &at) == 0 && CoXlTestStreamSpawnTried() == 7);
        co_framecost_test_reset(0);
        CoXlTestSlReset();
    }
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    /* Layers 6..9 must be layer 4 to every reader: the count only with all four hooks in, their info layer 4's. */
    Start("the streamed layers' count and info: 10 layers, 6..9 as layer 4, only while on and over the reviewed 6");
    CoXlTestSlReset();
    CoXlTestSlSet(1, (uintptr_t)g_fake.exe, (void*)OrigCount6, (void*)OrigInfo, (void*)OrigRule, (void*)OrigCreate);
    CHECK(CoXlTestSlCount() == 10);
    CHECK(CoXlTestSlCountFrom((uintptr_t)g_fake.exe + 0xD3C675u) == 10);
    CHECK(CoXlTestSlCountFrom((uintptr_t)g_fake.exe + 0xD2DC4Au) == 6);  /* the hybrid broad phase: 8 layers inline */
    {
        uint8_t info[12];
        for (uint8_t layer = 6; layer <= 9; layer++)
        {
            uint32_t mask = 0; uint16_t flags = 0xFFFF;
            memset(info, 0xCC, sizeof info);
            g_infoCalls = 0;
            CoXlTestSlInfo(layer, info);
            memcpy(&mask, info + 4, 4); memcpy(&flags, info + 8, 2);
            CHECK(info[0] == 1 && mask == 1 && flags == 0 && g_infoCalls == 0);
        }
        g_infoCalls = 0;
        CoXlTestSlInfo(5, info); CHECK(info[0] == 0xEE && g_infoCalls == 1);
        CoXlTestSlInfo(4, info); CHECK(info[0] == 0xEE && g_infoCalls == 2);
        CoXlTestSlInfo(10, info); CHECK(info[0] == 0xEE && g_infoCalls == 3);
        CoXlTestSlSet(1, (uintptr_t)g_fake.exe, (void*)OrigCount7, NULL, NULL, NULL);
        CHECK(CoXlTestSlCount() == 7);                                 /* not the reviewed config */
        CoXlTestSlSet(0, (uintptr_t)g_fake.exe, (void*)OrigCount6, NULL, NULL, NULL);
        CHECK(CoXlTestSlCount() == 6);
        CoXlTestSlInfo(7, info); CHECK(info[0] == 0xEE && g_infoCalls == 4);
    }
    CoXlTestSlReset();

    Start("bodies made in a change frame on its thread take its layer; the rule sends them there from layer 4 alone");
    StreamGame();
    {
        const uint32_t leaves[10] = {0, 0, 0, 0, 1000, 0, 10, 10, 10, 10};
        uint32_t id = 0; long created = 0, marked = 0, routed = 0;
        uint8_t out[5];
        BroadPhase(10, leaves);
        CoXlTestSlReset();
        CoXlTestSlSet(1, (uintptr_t)g_fake.exe, (void*)OrigCount6, (void*)OrigInfo, (void*)OrigRule, (void*)OrigCreate);
        g_ruleOther = -1;
        const uint32_t ids[5] = {5, 8, 0x01000005u, 7, 0xFFFFFFu};
        g_nextId = 5; CoXlTestSlCreate(WriterW(), &id);              /* outside a frame: none */
        CoXlTestSlRule(ids, 5, g_slBodies, out);
        CHECK(id == 5 && CoXlTestSlLayerOf(5) == 0 && out[0] == 4 && out[2] == 4);
        CoXlTestSlFrame(7);
        g_nextId = 5; CHECK(CoXlTestSlCreate(WriterW(), &id) == WriterW() && CoXlTestSlLayerOf(5) == 7);
        g_nextId = 6; CoXlTestSlCreate(g_slWorld, &id);               /* another writer: not the mission world's */
        CHECK(CoXlTestSlLayerOf(6) == 0);
        g_nextId = 7; CoXlTestSlCreate(WriterW(), &id);
        g_nextId = 9;
        HANDLE t = CreateThread(NULL, 0, CreateElsewhere, NULL, 0, NULL); /* another thread in the frame: none */
        WaitForSingleObject(t, INFINITE); CloseHandle(t);
        CHECK(CoXlTestSlLayerOf(9) == 0 && CoXlTestSlLayerOf(7) == 7);
        CoXlTestSlFrame(0);
        g_nextId = 8; CoXlTestSlCreate(WriterW(), &id);              /* after the frame: none */
        CHECK(CoXlTestSlLayerOf(8) == 0);
        CoXlTestSlCounts(&created, &marked, &routed);
        CHECK(created == 5 && marked == 2 && routed == 0);
        g_ruleOther = 7;                                             /* not layer 4 by the game's rule: kept */
        CoXlTestSlRule(ids, 5, g_slBodies, out);
        CHECK(out[0] == 7 && out[1] == 4 && out[2] == 7 && out[3] == 3 && out[4] == 4);
        CoXlTestSlCounts(&created, &marked, &routed);
        CHECK(routed == 2);
        CoXlTestSlRule(ids, 5, g_slBodies + 0xA0, out);              /* another world's bodies */
        CHECK(out[0] == 4 && out[2] == 4);
        CoXlTestSlRule(ids, 0, g_slBodies, out);
        CoXlTestSlSet(0, (uintptr_t)g_fake.exe, NULL, NULL, NULL, NULL);
        CoXlTestSlRule(ids, 5, g_slBodies, out);
        CHECK(out[0] == 4 && out[2] == 4);                           /* off */
        CoXlTestSlSet(1, (uintptr_t)g_fake.exe, NULL, NULL, NULL, NULL);
        g_nextId = 5; CoXlTestSlCreate(WriterW(), &id);              /* index 5 made again outside a frame */
        CoXlTestSlRule(ids, 5, g_slBodies, out);
        CHECK(out[0] == 4 && out[2] == 4 && CoXlTestSlLayerOf(5) == 0);
        CoXlTestSlFrame(8);
        g_nextId = 5; CoXlTestSlCreate(WriterW(), &id);
        CoXlTestSlFrame(0);
        CoXlTestSlRule(ids, 5, g_slBodies, out);
        CHECK(out[0] == 8);
        CoXlTestSlFrame(5);                                          /* a layer that is not a streamed one: never sent */
        g_nextId = 5; CoXlTestSlCreate(WriterW(), &id);
        CoXlTestSlFrame(0);
        CoXlTestSlRule(ids, 5, g_slBodies, out);
        CHECK(CoXlTestSlLayerOf(5) == 5 && out[0] == 4);
    }
    {
        const uint32_t leaves[10] = {0, 0, 0, 0, 1000, 0, 10, 10, 10, 10};
        uint32_t id = 0; uint8_t out[1];
        const uint32_t ids[1] = {5};
        BroadPhase(6, leaves);                                       /* made before the hooks */
        CoXlTestSlReset();
        CoXlTestSlSet(1, (uintptr_t)g_fake.exe, (void*)OrigCount6, (void*)OrigInfo, (void*)OrigRule, (void*)OrigCreate);
        g_ruleOther = -1;
        CoXlTestSlFrame(6); g_nextId = 5; CoXlTestSlCreate(WriterW(), &id); CoXlTestSlFrame(0);
        CoXlTestSlRule(ids, 1, g_slBodies, out);
        CHECK(CoXlTestSlLayerOf(5) == 6 && out[0] == 4);
        BroadPhase(10, leaves);
        g_slLayer[7 * 0x1C00 + 0x1AE0] = 2;                          /* layer 7 not static */
        CoXlTestSlRule(ids, 1, g_slBodies, out);
        CHECK(out[0] == 4);
        g_slLayer[7 * 0x1C00 + 0x1AE0] = 1; g_slLayer[9 * 0x1C00 + 0x1AE4] = 3; /* layer 9 pairs with layer 1 as well */
        CoXlTestSlRule(ids, 1, g_slBodies, out);
        CHECK(out[0] == 4);
        g_slLayer[9 * 0x1C00 + 0x1AE4] = 1; g_slLayer[4 * 0x1C00 + 0x1AE0] = 2; /* layer 4 not what the info gives */
        CoXlTestSlRule(ids, 1, g_slBodies, out);
        CHECK(out[0] == 4);
        g_slLayer[4 * 0x1C00 + 0x1AE0] = 1;
        CoXlTestSlRule(ids, 1, g_slBodies, out);
        CHECK(out[0] == 6);
        memcpy(g_slVt + 0x38, &(uint64_t){(uint64_t)(uintptr_t)g_fake.exe + 0xD40551u}, 8);
        CoXlTestSlRule(ids, 1, g_slBodies, out);
        CHECK(out[0] == 4);                                          /* not the broad phase */
        CoXlTestSlReset();
        CoXlTestSlSet(1, (uintptr_t)g_fake.exe, (void*)OrigCount6, (void*)OrigInfo, (void*)OrigRule, (void*)OrigCreate);
        CoXlTestSlFrame(6); g_nextId = 5; CoXlTestSlCreate(WriterW(), &id); CoXlTestSlFrame(0);
        CoXlTestSlRule(ids, 1, g_slBodies, out);
        CHECK(out[0] == 4 && CoXlTestSlLayerOf(5) == 0);             /* no broad phase: none made in a frame either */
        CoXlTestSlReset();
    }

    /* Who makes the mission world's bodies outside the change frames (run 2c, ~38k a window): with the layers off the
     * body maker passes through and samples 1 in 64 calls' callers, the note naming the most frequent chains. */
    Start("the bodies' makers: counted with the layers off, 1 in 64 sampled, the most frequent chains named, the table bounded");
    {
        const uint32_t leaves[10] = {0, 0, 0, 0, 1000, 0, 10, 10, 10, 10};
        uint32_t id = 0; long created = 0, marked = 0, routed = 0;
        char text[640];
        StreamGame();
        BroadPhase(10, leaves);
        CoXlTestSlReset();
        CoXlTestSlSet(0, (uintptr_t)g_fake.exe, (void*)OrigCount6, (void*)OrigInfo, (void*)OrigRule, (void*)OrigCreate);
        CoXlTestSlSampleText(text, sizeof text);                      /* a fresh table */
        /* a body made in a change frame (by the streamer's tick: the layers are off), then in the frame after it */
        CoXlTestSlChangeTickSet(CoXlTestStreamTicks());
        CHECK(CoXlTestSlChangeMark() == 1);
        CoXlTestStreamTicksAdd();
        CHECK(CoXlTestSlChangeMark() == 2);
        g_nextId = 4; CHECK(CoXlTestSlCreate(WriterW(), &id) == WriterW() && id == 4);
        CoXlTestStreamTicksAdd();
        CHECK(CoXlTestSlChangeMark() == 0);                           /* two ticks on: another frame */
        CoXlTestSlChangeTickSet(CoXlTestStreamTicks());
        g_nextId = 5; CHECK(CoXlTestSlCreate(WriterW(), &id) == WriterW() && id == 5);
        g_nextId = 6; CoXlTestSlCreate(g_slWorld, &id);               /* another writer: not counted */
        CoXlTestSlCounts(&created, &marked, &routed);
        CHECK(created == 2 && marked == 0 && CoXlTestSlLayerOf(5) == 0);  /* off: counted, never marked */
        CoXlTestSlChangeTickSet(0);
        CHECK(CoXlTestSlChangeMark() == 0);                           /* no change frame yet */
        for (int k = 0; k < 126; k++) CoXlTestSlSampleCall();
        CoXlTestSlSampleText(text, sizeof text);
        CHECK(strncmp(text, "128 bodies made (1 in change frames, 1 in the frames after them, 126 in others), 2 sampled (1 in 64), 0 not kept; ", 114) == 0 &&
              strstr(text, " from ?+0x") != NULL); /* this test's own frames */
        const uintptr_t exe = (uintptr_t)g_fake.exe;
        const uintptr_t a[4] = {exe + 0x20BC85u, exe + 0xDBEC9u, 0x1234u, exe + 0x10u}, b[2] = {exe + 0x2354A0u, exe + 0x2351DDu};
        for (int k = 0; k < 3; k++) CoXlTestSlChain(a, 4, 1, k);
        CoXlTestSlChain(b, 2, 0, 0);
        CoXlTestSlSampleText(text, sizeof text);
        CHECK(strcmp(text, "0 bodies made (0 in change frames, 0 in the frames after them, 0 in others), 4 sampled (1 in 64), 0 not kept; 3 from e+0x20BC85 < "
                           "e+0xDBEC9 < ?+0x1234 < e+0x10 (3 on the game thread, 1 in a change frame, 1 in the frame after); 1 from e+0x2354A0 < e+0x2351DD "
                           "(0 on the game thread, 0 in a change frame, 0 in the frame after)") == 0);
        CoXlTestSlSampleText(text, sizeof text);
        CHECK(strcmp(text, "0 bodies made (0 in change frames, 0 in the frames after them, 0 in others), 0 sampled (1 in 64), 0 not kept") == 0); /* each note starts again */
        for (uintptr_t k = 0; k < 49; k++) { const uintptr_t c[1] = {exe + 0x100u + k}; CoXlTestSlChain(c, 1, 0, 0); }
        CoXlTestSlSampleText(text, sizeof text);
        CHECK(strncmp(text, "0 bodies made (0 in change frames, 0 in the frames after them, 0 in others), 49 sampled (1 in 64), 1 not kept; 1 from e+0x100 ", 126) == 0); /* 48 kept */
        CoXlTestSlReset();
    }

    /* The step's removal path timed (ANALYSIS-r2a §2.3): each thunk passes its four arguments and the original's result
     * through, and its time and calls go to the frame's kind at the streamer's next tick. */
    Start("the step removal timers: the originals' arguments and results passed through, each frame's under its kind");
    {
        typedef uint64_t (*Fn4)(uint64_t, uint64_t, uint64_t, uint64_t);
        for (int slot = 0; slot < 4; slot++) CoXlTestStOrig(slot, slot == 3 ? (void*)StFakeActivate : (void*)StFakeTimed);
        CoXlTestStOn();
        g_stFakeCalls = 0;
        CHECK(((Fn4)CoXlTestStThunk(0))(1, 2, 3, 4) == 1234 && CoXlTestStCalls(0) == 1);
        CHECK(((Fn4)CoXlTestStThunk(1))(5, 6, 7, 8) == 5678 && CoXlTestStCalls(1) == 1);
        CHECK(((Fn4)CoXlTestStThunk(1))(9, 0, 1, 2) == 9012 && CoXlTestStCalls(1) == 2);
        CHECK(((Fn4)CoXlTestStThunk(2))(3, 4, 5, 6) == 3456 && CoXlTestStCalls(2) == 1);
        const long long acts = CoXlTestStCalls(3);
        CHECK(((Fn4)CoXlTestStThunk(3))(7, 8, 9, 1) == 7 * 8 + 9 + 1 && CoXlTestStCalls(3) == acts + 1);
        CHECK(g_stFakeCalls == 5);
        CoXlTestStFrame(3);                                           /* a frame that removed and spawned: a removal frame's */
        CHECK(CoXlTestStKindFrames(1) == 1 && CoXlTestStKindCalls(1, 0) == 1 && CoXlTestStKindCalls(1, 1) == 2 && CoXlTestStKindCalls(1, 2) == 1);
        CHECK(CoXlTestStKindCalls(1, 3) == 1 && CoXlTestStCalls(0) == 0 && CoXlTestStCalls(1) == 0);
        ((Fn4)CoXlTestStThunk(0))(1, 2, 3, 4);
        CoXlTestStFrame(2);                                           /* spawns only */
        CHECK(CoXlTestStKindFrames(2) == 1 && CoXlTestStKindCalls(2, 0) == 1 && CoXlTestStKindCalls(2, 3) == 0);
        CoXlTestStFrame(0);
        CoXlTestStFrame(1);
        CHECK(CoXlTestStKindFrames(0) == 1 && CoXlTestStKindFrames(1) == 2 && CoXlTestStKindCalls(0, 0) == 0);
        CoXlTestStNote();
        CHECK(fake_host_logged("step removal timers in 10 s (removeBodies 0xD0F440, its broad phase remove 0xD41AF0, the renumber 0xD42550 a frame; "
                               "activations 0xDE1690): 2 removal frames: "));
        CHECK(fake_host_logged("1 spawn frames: ") && fake_host_logged("; 1 other frames: 0.00 ms (the most 0.00, 0 calls)"));
        CHECK(CoXlTestStKindFrames(1) == 0 && CoXlTestStKindCalls(1, 1) == 0); /* since the note */
    }

    Start("the change frame makes its spawns' units only when the spawn ring or a destroy list holds any, as DestroyUnit does");
    {
        static uint8_t ring[0x20]; static uint64_t head, tail;
        uint8_t* w = g_worldMem;
        memset(w, 0, 0x171000);
        const uint64_t world = (uint64_t)(uintptr_t)w, r = (uint64_t)(uintptr_t)ring, h = (uint64_t)(uintptr_t)&head, t = (uint64_t)(uintptr_t)&tail;
        memcpy(w + 0x20, &r, 8); memcpy(ring + 8, &h, 8); memcpy(ring + 0x10, &t, 8);
        head = tail = 0x100;
        CHECK(!CoXlTestSlFlushDue(world) && !CoXlTestSlFlushDue(0));
        head = 0x110;
        CHECK(CoXlTestSlFlushDue(world));
        head = 0x100; w[0x170038] = 1;
        CHECK(CoXlTestSlFlushDue(world));
        w[0x170038] = 0; w[0x10030] = 1;
        CHECK(CoXlTestSlFlushDue(world));
        w[0x10030] = 0; memset(ring + 8, 0, 8);
        CHECK(!CoXlTestSlFlushDue(world));                           /* the ring does not read: left to the world's flush */
    }

    /* The count and info go in before the game's set-up (SlEarly), the rule and body maker with the streamer (SlInstall),
     * each pair both or none and only over the reviewed code; the second pair never without the first. */
    Start("the streamed layers' count and info go in first, the rule and body maker after them, only over the reviewed code");
    {
        uint8_t* exe = fake_host_exe();
        const uintptr_t image = (uintptr_t)g_fake.image;
        CoXlTestSlReset();
        g_fake.hookCount = 0;
        /* off by default: no hook at all unless HELLFORGE_XL_STREAM_LAYERS=1 */
        static const char kOff[] = "off by default (HELLFORGE_XL_STREAM_LAYERS=1 turns them on)";
        CoXlTestSlAsk(0);
        const char* off = CoXlTestSlEarly((uintptr_t)exe);
        CHECK(off != NULL && strncmp(off, kOff, sizeof kOff - 1) == 0 && !CoXlTestSlLayers() && g_fake.hookCount == 0);
        off = CoXlTestSlInstall((uintptr_t)exe);
        CHECK(off != NULL && strncmp(off, kOff, sizeof kOff - 1) == 0 && !CoXlTestSlOn() && g_fake.hookCount == 0);
        CoXlTestSlAsk(1);                                            /* as a research build with the switch set */
        const char* first = CoXlTestSlInstall((uintptr_t)exe);       /* the count not in first */
        CHECK(first != NULL && strcmp(first, "the layer count was not hooked before the game's set-up made the main world (its broad phase keeps 6 layers)") == 0);
        CHECK(!CoXlTestSlOn() && g_fake.hookCount == 0);
        g_fake.noMatchExe[0] = 0xDB9EE0u;
        CHECK(CoXlTestSlEarly((uintptr_t)exe) != NULL && !CoXlTestSlLayers() && g_fake.hookCount == 0);
        g_fake.noMatchExe[0] = 0xDB9DB0u;
        CHECK(CoXlTestSlEarly((uintptr_t)exe) != NULL && !CoXlTestSlLayers() && g_fake.hookCount == 0);
        g_fake.noMatchExe[0] = 0xD3C66Cu;                             /* the count's call site in the constructor */
        CHECK(CoXlTestSlEarly((uintptr_t)exe) != NULL && !CoXlTestSlLayers() && g_fake.hookCount == 0);
        g_fake.noMatchExe[0] = 0;
        g_fake.noHook = (uintptr_t)exe + 0xDB9EE0u - image;          /* the count refused: the info passes through */
        CHECK(CoXlTestSlEarly((uintptr_t)exe) != NULL && !CoXlTestSlLayers() && g_fake.hookCount == 1 &&
            fake_host_detour((uintptr_t)exe + 0xDB9DB0u - image) != NULL);
        CoXlTestSlReset();
        g_fake.noHook = 0; g_fake.hookCount = 0;
        CHECK(CoXlTestSlEarly(0) != NULL && !CoXlTestSlLayers());
        g_flagTarget = (uintptr_t)exe + 0xDB9EE0u; g_flagAtHook = -1; g_fake.onHook = FlagAtHook;
        CHECK(CoXlTestSlEarly((uintptr_t)exe) == NULL && CoXlTestSlLayers() && !CoXlTestSlOn() && g_fake.hookCount == 2);
        g_fake.onHook = NULL;
        CHECK(g_flagAtHook == 1);                                    /* set before the count's hook is written */
        CHECK(fake_host_detour((uintptr_t)exe + 0xDB9EE0u - image) != NULL && fake_host_detour((uintptr_t)exe + 0xDB9DB0u - image) != NULL);
        CHECK(g_fake.hooks[0].target == (uintptr_t)exe + 0xDB9DB0u - image); /* the info first: no 10 layers without it */
        CHECK(CoXlTestSlEarly((uintptr_t)exe) == NULL && g_fake.hookCount == 2); /* once */
        /* the main world made with 10 layers, so each refusal below is the pin's or the exe's, not the count's */
        const uint32_t leaves[10] = {0, 0, 0, 0, 1000, 0, 10, 20, 30, 40};
        static const char kNotReviewed[] = "the broad phase's layer rule, the body maker or the spawn flush is not the reviewed code";
        BroadPhase(10, leaves);
        const uint32_t pins[3] = {0xD09080u, 0x12D600u, 0xDB9E60u};
        for (int k = 0; k < 3; k++)
        {
            g_fake.noMatchExe[0] = pins[k];
            const char* pin = CoXlTestSlInstall((uintptr_t)exe);
            CHECK(pin != NULL && strcmp(pin, kNotReviewed) == 0 && !CoXlTestSlOn() && g_fake.hookCount == 2);
        }
        g_fake.noMatchExe[0] = 0;
        const char* other = CoXlTestSlInstall((uintptr_t)exe + 0x1000u); /* another exe than the count's */
        CHECK(other != NULL && strcmp(other, "the exe is not the one the layer count was hooked in") == 0 && !CoXlTestSlOn() && g_fake.hookCount == 2);
        /* the main world made with the game's 6 (the count in, but after it): said, and the rule and body maker stay out */
        BroadPhase(6, leaves);
        const char* six = CoXlTestSlInstall((uintptr_t)exe);
        CHECK(six != NULL && strcmp(six, "the main world's broad phase has 6 layers (made before the layer count was hooked)") == 0);
        CHECK(!CoXlTestSlOn() && g_fake.hookCount == 2);
        CoXlTestStreamLayersInstall();
        CHECK(fake_host_logged("streamed broad phase layers off: the main world's broad phase has 6 layers (made before the layer count was hooked); every static body"));
        CHECK(!CoXlTestSlOn() && g_fake.hookCount == 3 && fake_host_detour((uintptr_t)exe + 0xD09080u - image) != NULL); /* the makers' sampler */
        CHECK(!fake_host_logged("the bodies' makers are not sampled"));
        memset(exe + 0x27BA9F0u, 0, 8);                              /* no main world to read: -1, not on, and not a lost race */
        const char* none = CoXlTestSlInstall((uintptr_t)exe);
        CHECK(none != NULL && strcmp(none, "no main world to read (its broad phase's layer count is unknown)") == 0);
        CHECK(!CoXlTestSlOn() && g_fake.hookCount == 3);
        CoXlTestStreamLayersInstall();
        CHECK(fake_host_logged("streamed broad phase layers off: no main world to read (its broad phase's layer count is unknown); every static body"));
        CHECK(!fake_host_logged("has -1 layers"));
        BroadPhase(10, leaves);
        CoXlTestStreamLayersInstall();
        CHECK(CoXlTestSlOn() && CoXlTestSlLayers() && g_fake.hookCount == 4);         /* the rule; the body maker was in */
        CHECK(fake_host_logged("streamed broad phase layers on: the main world's broad phase has 10 layers, 6..9 as layer 4"));
        CHECK(fake_host_detour((uintptr_t)exe + 0xDB9E60u - image) != NULL && fake_host_detour((uintptr_t)exe + 0xD09080u - image) != NULL);
        CHECK(CoXlTestSlInstall((uintptr_t)exe) == NULL && g_fake.hookCount == 4); /* once */
        CoXlTestSlReset();
    }

    /* The main world is made once, in the game's set-up, and its broad phase reads the layer count then: the count must
     * be hooked before that (the game-loaded event), or the world keeps 6 layers for the process (the combined run). */
    Start("the layer count against the main world's making: in time it has 10 layers, too late it keeps 6 and says so");
    {
        uint8_t* exe = fake_host_exe();
        const uintptr_t image = (uintptr_t)g_fake.image, game = image;
        const uint32_t leaves[10] = {0, 0, 0, 0, 1000, 0, 10, 20, 30, 40};
        uint32_t read[5] = {0};
        uint64_t engineWas = 0;
        const uint64_t none = 0, world = 0x5000, engineAt = (uint64_t)(uintptr_t)(g_fake.image + 0x3700000u);
        uint8_t* engine = g_fake.image + 0x3700000u;                  /* an engine object inside the fake game */
        memcpy(&engineWas, g_fake.image + 0x3326340u, 8);
        memcpy(g_fake.image + 0x3326340u, &engineAt, 8);
        memcpy(engine + 0x10E8, &none, 8);                           /* the set-up has not made the main world */
        g_fake.originals[0].target = (uintptr_t)exe + 0xDB9EE0u; g_fake.originals[0].original = (void*)OrigCount6;
        g_fake.originals[1].target = (uintptr_t)exe + 0xDB9DB0u; g_fake.originals[1].original = (void*)OrigInfo;
        /* in time: hooked at the game-loaded event, then the set-up makes the world, then the streamer goes in */
        CoXlTestSlReset(); g_fake.hookCount = 0;
        CHECK(CoXlTestLayersEarly(game, (uintptr_t)exe) == NULL && CoXlTestSlLayers());
        CHECK(fake_host_logged("streamed broad phase layers: the layer count is hooked before the game's set-up makes the main world"));
        const int made = CoXlTestSlCountFrom((uintptr_t)exe + 0xD3C675u); /* the constructor's read, during the set-up */
        CHECK(made == 10);
        {
            uint8_t info[12] = {0}; uint32_t mask = 0;
            CoXlTestSlInfo(6, info);                                  /* the constructor's info, before the rule goes in */
            memcpy(&mask, info + 4, 4);
            CHECK(!CoXlTestSlOn() && info[0] == 1 && mask == 1);
        }
        BroadPhase(made, leaves);
        memcpy(engine + 0x10E8, &world, 8);
        CHECK(CoXlTestSlInstall((uintptr_t)exe) == NULL && CoXlTestSlOn());
        CHECK(CoXlTestSlLeaves(read) && read[0] == 1000 && read[1] == 10 && read[4] == 40);
        /* too late, as in the run: the world was made before the count was hooked */
        CoXlTestSlReset(); g_fake.hookCount = 0;
        BroadPhase(6, leaves);
        CHECK(CoXlTestLayersEarly(game, (uintptr_t)exe) != NULL && !CoXlTestSlLayers() && g_fake.hookCount == 0);
        CHECK(fake_host_logged("streamed broad phase layers: the main world was made before the layer count could be hooked; every static body stays in layer 4"));
        CHECK(CoXlTestSlInstall((uintptr_t)exe) != NULL && !CoXlTestSlOn() && g_fake.hookCount == 0);
        CHECK(!CoXlTestSlLeaves(read));
        CoXlTestStreamSlNote();
        CHECK(fake_host_logged("(not read: the main world's broad phase has 6 layers, made before the layer count was hooked); in 10 s"));
        BroadPhase(33, leaves);                                      /* 10 or more, but not a count it reads */
        CoXlTestStreamSlNote();
        CHECK(fake_host_logged("(not read: the main world's broad phase has 33 layers, but they do not read)"));
        BroadPhase(10, leaves);                                      /* exactly 10, but the split is off: not "made before" */
        CoXlTestStreamSlNote();
        CHECK(fake_host_logged("(not read: the main world's broad phase has 10 layers, but they do not read)"));
        BroadPhase(6, leaves);
        /* made while the count was hooked: said, the hooks stay in, the 10 s note tells which */
        memcpy(engine + 0x10E8, &none, 8);
        CoXlTestSlReset(); g_fake.hookCount = 0;
        g_layerWorldEngine = engine;
        g_fake.onHook = MakeWorldMeanwhile;
        CHECK(CoXlTestLayersEarly(game, (uintptr_t)exe) != NULL && CoXlTestSlLayers());
        g_fake.onHook = NULL;
        CHECK(fake_host_logged("streamed broad phase layers: the main world was made while the layer count was hooked (the 10 s note shows its layers)"));
        CHECK(!fake_host_logged("hooked (the 10 s note shows its layers); every static body"));
        /* the main world in another slot: its id's record, not slot 2's */
        {
            const uint32_t one = 1, four = 4;
            uint64_t w2 = 0;
            CoXlTestSlSet(1, (uintptr_t)exe, (void*)OrigCount6, (void*)OrigInfo, NULL, NULL);
            BroadPhase(10, leaves);
            memcpy(&w2, exe + 0x27BA9F0u, 8);
            memcpy(g_fake.image + 0x3700000u + 0x10F0u, &one, 4);
            CHECK(!CoXlTestSlLeaves(read));                          /* slot 1 empty */
            memcpy(exe + 0x27BA870u + 0xB0u + 0x20u, &w2, 8);
            CHECK(CoXlTestSlLeaves(read) && read[4] == 40);
            memcpy(exe + 0x27BA870u + 0xB0u + 0x20u, &none, 8);
            memcpy(g_fake.image + 0x3700000u + 0x10F0u, &four, 4);   /* not a slot */
            memcpy(exe + 0x27BA870u + 4u * 0xB0u + 0x20u, &w2, 8);
            CHECK(!CoXlTestSlLeaves(read));
            memcpy(exe + 0x27BA870u + 4u * 0xB0u + 0x20u, &none, 8);
            memcpy(g_fake.image + 0x3326340u, &none, 8);             /* no engine yet */
            CHECK(!CoXlTestSlLeaves(read));
            BroadPhase(10, leaves);
            CHECK(CoXlTestSlLeaves(read));
        }
        /* no main world to read */
        CoXlTestSlReset();
        memcpy(exe + 0x27BA9F0u, &none, 8);
        CoXlTestStreamSlNote();
        CHECK(fake_host_logged("(not read: no main world)"));
        memset(g_fake.originals, 0, sizeof g_fake.originals);
        memcpy(engine + 0x10E8, &none, 8);
        memcpy(g_fake.image + 0x3326340u, &engineWas, 8);
        CoXlTestSlReset(); g_fake.hookCount = 0;
    }

    /* Any static add or remove makes the next step rebuild the whole static layer (exe 0xD3DAA9): leaf moves after a
     * frame the streamer changed nothing in are rebuilds the change frames do not hold. */
    Start("the static layer's leaves: moves counted, apart after a frame with no streamer change; a bad read counts nothing");
    StreamGame();
    Stream(1);
    Budgets(1000, 10);
    /* the main world is the record of the engine's physics id (0xAB1B31): 2, the slot Budgets fills */
    uint64_t sEngineWas = 0;
    const uint64_t sEngine = (uint64_t)(uintptr_t)(g_fake.image + 0x3700000u); const uint32_t sId = 2;
    memcpy(&sEngineWas, g_fake.image + 0x3326340u, 8);
    memcpy(g_fake.image + 0x3326340u, &sEngine, 8); memcpy(g_fake.image + 0x3700000u + 0x10F0u, &sId, 4);
    uint32_t sFrames = 0, sMoved = 0, sQuiet = 0, sLeaves = 0;
    StaticLayer(1000, 0xC0001000u, 6, 0xD40550u); /* the capacity's top bits are flags */
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    CHECK(sFrames == 1 && sMoved == 0 && sQuiet == 0 && sLeaves == 1000);
    StaticLayer(1010, 0x1000u, 6, 0xD40550u);
    CoXlTestStreamStatic(0, 1, &sFrames, &sMoved, &sQuiet, &sLeaves); /* after a frame with no change */
    CHECK(sFrames == 2 && sMoved == 1 && sQuiet == 1 && sLeaves == 1010);
    StaticLayer(1005, 0x1000u, 6, 0xD40550u);
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves); /* after the streamer's change frame */
    CHECK(sFrames == 3 && sMoved == 2 && sQuiet == 1);
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    CHECK(sFrames == 4 && sMoved == 2 && sQuiet == 1);
    CoXlTestStreamStatic(1, 0, &sFrames, &sMoved, &sQuiet, &sLeaves); /* a frame with no read */
    StaticLayer(1020, 0x1000u, 6, 0xD40550u);
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves); /* not from one frame to the next: no move */
    CHECK(sFrames == 5 && sMoved == 2 && sQuiet == 1 && sLeaves == 1020);
    StaticLayer(1030, 0x1000u, 6, 0xD40551u); /* not the broad phase */
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    StaticLayer(1030, 0x1000u, 4, 0xD40550u); /* no layer 4 */
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    StaticLayer(0x1001u, 0xC0001000u, 6, 0xD40550u); /* more leaves than room */
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    CHECK(sFrames == 5 && sMoved == 2 && sLeaves == 1020);
    StaticLayer(0x1000u, 0x1000u, 5, 0xD40550u); /* full, and layer 4 the last */
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    CHECK(sFrames == 6 && sLeaves == 0x1000u);
    /* moves in a tile build step's window: the step's frame and the next; outside them the count stays */
    CoXlTestMoveStepAt(CoXlTestStreamTicks() + 1);
    StaticLayer(0xF00u, 0x1000u, 5, 0xD40550u);
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    CHECK(sMoved == 3 && sQuiet == 2 && CoXlTestStreamStaticTile() == 1);
    StaticLayer(0xE00u, 0x1000u, 5, 0xD40550u);
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    CHECK(sMoved == 4 && sQuiet == 3 && CoXlTestStreamStaticTile() == 2);
    StaticLayer(0xD00u, 0x1000u, 5, 0xD40550u);
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    CHECK(sMoved == 5 && sQuiet == 4 && CoXlTestStreamStaticTile() == 2);
    CoXlTestMoveStepAt(0);
    CoXlTestMoveStepMark(0.2);                                       /* a light build frame: a tile builds all the same */
    StaticLayer(0xC00u, 0x1000u, 5, 0xD40550u);
    CoXlTestStreamStatic(0, 0, &sFrames, &sMoved, &sQuiet, &sLeaves);
    CHECK(sMoved == 6 && CoXlTestStreamStaticTile() == 3);
    CoXlTestMoveStepAt(0);
    {
        int d = 0; uint32_t sp = 9, rb = 0;
        CoXlTestStreamLayersPick(0, &d, &sp, &rb);
        CHECK(d == -1 && sp == 0 && rb == 0xC00u);                   /* split off: the step rebuilds layer 4, its last read */
    }
    memcpy(g_fake.image + 0x3326340u, &sEngineWas, 8);

    Start("far stamps evicted over the budget: no more while the last eviction still drains");
    StreamGame();
    Stream(100);
    for (uint32_t k = 0; k < 100; k++) Slot(k, AX + 3000.0f, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0);
    CoXlTestStreamObserver(1, AX, AY);
    CHECK(CoXlTestWindowEvictFar(0, 100) == 64);
    CHECK(CoXlTestStreamDrainMost(4, 1.0e9, &waiting) == 4 && waiting == 60 && g_destroys == 4);
    CHECK(CoXlTestWindowEvictFar(0, 100) == 0); /* the next scan finds the bodies over budget again before the next change frame */
    for (int f = 0; f < 20 && waiting > 0; f++) CoXlTestStreamDrain(1.0e9, &waiting);
    CHECK(waiting == 0 && g_destroys == 64);
    CHECK(CoXlTestWindowEvictFar(0, 100) == 36); /* drained: the budget is read again and the rest may go */
    CoXlTestStreamObserver(0, 0.0f, 0.0f);

    Start("a level location's new unit waits under the window's budgets");
    StreamGame();
    Stream(2);
    Budgets(500000, 100);
    CHECK(CoXlTestRespawnRoom(0, 100.0f * 100.0f));
    Budgets(900000, 100);
    CHECK(!CoXlTestRespawnRoom(0, 100.0f * 100.0f));                  /* bodies over 80 % */
    Budgets(500000, 900);
    CHECK(!CoXlTestRespawnRoom(0, 100.0f * 100.0f));                  /* navmesh cutters over 80 % */
    Budgets(500000, 100);
    CHECK(!CoXlTestRespawnRoom(0, 1000.0f * 1000.0f));                /* far: the culling pools do not read on the fake game */
    Budgets(500000, 100);
    uint32_t held = CoXlTestSpawnDue(0, 100.0f * 100.0f); /* the passes held so far: the refusals above */
    CHECK(held == 3);
    Budgets(900000, 100);
    CHECK(CoXlTestSpawnDue(0, 100.0f * 100.0f) == held + 1 && CoXlTestSpawnDue(0, 100.0f * 100.0f) == held + 2);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("the research trace reads a grid as the game does and names why a stamp keeps the level's heights");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    int c = CoXlTestTileAddAt(AX, CY2, g_tileC, 2, SPAN); /* south: the drop grid differs north and south */
    CoXlTestLayerTile(c, 1, AX, CY2);
    fa = Field(2100.0f, AY);
    int differ = 0;
    for (float x = 1990.0f; x <= 2110.0f; x += 0.37f) /* across the two tiles' meeting, the trace's read is the game's */
        CHECK(CoXlTestTraceGridAt(a, x, AY + 11.7f) == Read(fa, x, AY + 11.7f));
    for (float y = AY - 1700.0f; y <= AY + 1700.0f; y += 3.1f)
    {
        CHECK(CoXlTestTraceGridAt(a, AX + 5.3f, y) == Read(fa, AX + 5.3f, y));
        differ += !Near(Read(fa, AX + 5.3f, y), Read(fa, AX + 5.3f, 2.0f * AY - y));
    }
    CHECK(differ > 100); /* the grid is not symmetric about its centre, so a flipped row shows */
    int slot = -2;
    float at[2] = {2100.0f, AY};
    memcpy(g_unit + 0x50, at, 8);
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_unit, (uint64_t)(uintptr_t)g_levelDesc, &slot) == 0 && slot == a);
    at[0] = BX - 1500.0f; memcpy(g_unit + 0x50, at, 8);
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_unit, (uint64_t)(uintptr_t)g_levelDesc, &slot) == 5 && slot == -1); /* no grid */
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_unit, 0, &slot) == 1);
    float wide[2] = {4096.0f, 4096.0f};
    memcpy(g_levelDesc + 0x14, wide, 8); /* not the level's extent */
    at[0] = 2100.0f; memcpy(g_unit + 0x50, at, 8);
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_unit, (uint64_t)(uintptr_t)g_levelDesc, &slot) == 3);

    Start("a stamp's layer spawn by a tile hands its grid to every member, SpeedTree and particles included");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    for (int k = 0; k < 4; k++) g_levelData[k] = 52.0f; /* the level's heights: 7 m over the tile, as at the user's drop */
    CoXlTestHeightKept(g_levelData, 2, 2, 16384.0f);
    CoXlTestProbePlayer(1, 2120.0f, AY);
    uint64_t level = (uint64_t)(uintptr_t)g_levelDesc;
    uint64_t given = CoXlTestLayerSpawn(SpawnUnit(2100.0f, AY, 2, 1), 3, level);
    CHECK(given != level && given == Field(2100.0f, AY));
    CHECK(Near(Read(given, 2600.0f, AY), 45.0f) && Near(Read(given, 1900.0f, AY), 40.0f)); /* what an inline read there takes */
    CHECK(fake_host_logged("layer spawns (the first): 1, 1 of them on a tile's grid for every member (1 with SpeedTree, 1 with particle effects); "
        "1 within 300 m of a player, their members 0."));
    CHECK(fake_host_logged(" 0 over 0.5 m; the drawn ground within 16 m of their centres up to "));
    CHECK(!fake_host_logged("layer spawn off the ground"));
    CHECK(CoXlTestLayerSpawn(SpawnUnit(1126.0f, AY, 2, 1), 3, level) == level);  /* no tile within reach: the level's own */
    CHECK(CoXlTestLayerSpawn(SpawnUnit(2100.0f, AY, 0, 0), 3, 0) == 0);          /* no heightfield (0xDAEC8, 0xDB4F3) */
    CHECK(CoXlTestLayerSpawn(SpawnUnit(2100.0f, AY, 0, 0), 3, given) == given);  /* a grid already: kept */
    CHECK(CoXlTestLayerSpawn(SpawnUnit(2100.0f, AY, 0, 0), 0, level) == level);  /* no members: the engine returns at once */
    CHECK(CoXlTestLayerSpawn(SpawnUnit(2600.0f, AY, 3, 0), 3, level) == given);  /* counted: SpeedTree, no particles */
    CoXlTestLayerTile(a, 1, AX, AY);
    CHECK(fake_host_logged("layer spawns (a tile came in): 3, 2 of them on a tile's grid for every member (2 with SpeedTree, 1 with particle effects)"));
    CHECK(fake_host_logged("layer spawns (a tile came in): 0 ran on the game thread, the rest on 1 other thread(s); 0 spawned on the level's heights by a tile"));

    g_case = "a stamp by a tile whose grid is not ready spawns on the level's heights, is listed and logged off the ground";
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN); /* in, its grid not made yet */
    CoXlTestProbePlayer(1, BX + 20.0f, AY);
    CHECK(CoXlTestLayerSpawn(SpawnUnit(BX, AY, 1, 0), 3, level) == level);
    CHECK(fake_host_logged("layer spawn off the ground: the stamp at (1026.2, -4094.9), 20 m from a player: its members read 52.00 (the level's heights)"));
    CoXlTestLayerTile(b, 1, BX, AY);
    CHECK(fake_host_logged("1 spawned on the level's heights by a tile, 0 of them since put back on a grid, 0 kept (not the streamer's to put back), 1 waiting"));

    g_case = "a listed stamp the streamer holds goes out and comes back once a tile's grid covers it";
    StreamGame();
    Stream(1);
    Slot(0, BX, AY, 20.0f, (uint64_t)(uintptr_t)g_spawnUnit, 0);
    long destroys = g_destroys;
    CoXlTestStreamScan();
    int pLive = 0, pDue = 0; uint64_t pUnit = 0;
    CoXlTestStreamSlotState(0, &pLive, &pDue, &pUnit);
    CHECK(g_destroys == destroys + 1 && !pLive && pDue);
    CHECK(CoXlTestSeen2(0) == CoXlTestSeen2Of(20.0f) && CoXlTestSeen2Of(20.0f) > 1.0f); /* ranked by how far its 20 m footprint is seen */
    CoXlTestLayerTile(b, 1, BX, AY);
    CHECK(fake_host_logged("1 spawned on the level's heights by a tile, 1 of them since put back on a grid, 0 kept (not the streamer's to put back), 0 waiting"));

    g_case = "a refill between the grid's choice and the spawn's entry: the level's heights, not counted on a grid, listed once";
    {
        static uint8_t race[0x400];
        float at2[2] = {2100.0f, AY};
        memcpy(race + 0x50, at2, 8);
        const uint64_t unit2 = (uint64_t)(uintptr_t)race;
        long grids0 = 0, spawns0 = 0, grids1 = 0, spawns1 = 0; uint32_t pending0 = 0, pending1 = 0;
        CoXlTestLayerCounts(&grids0, &spawns0, &pending0);
        g_refillSlots[0] = a; g_refillSlots[1] = b; /* every live grid: the second choice finds none */
        CoXlTestLayerChosen(RefillBetween);
        CHECK(CoXlTestLayerSpawn(unit2, 3, level) == level);
        CHECK(CoXlTestLayerSpawn(unit2, 3, level) == level); /* spawned again before its grid came */
        CoXlTestLayerChosen(NULL);
        g_refillSlots[0] = g_refillSlots[1] = -1;
        CoXlTestGridReady(a, 1); CoXlTestGridReady(b, 1);
        CoXlTestLayerCounts(&grids1, &spawns1, &pending1);
        CHECK(spawns1 == spawns0 + 2 && grids1 == grids0 && pending1 == pending0 + 1);
    }

    g_case = "a grid write waits for the spawns reading it and says when one is still reading";
    uint64_t current = Field(2100.0f, AY);
    int k = CoXlTestGridEnter(current);
    CHECK(k == a);
    CoXlTestLayerTile(a, 1, AX, AY);
    CHECK(fake_host_logged("waited for layer spawns reading them, 1 went on with one still reading"));
    CHECK(CoXlTestGridEnter(current) < 0); /* its layout was written again: the old descriptor is not handed on */
    CoXlTestGridLeave(k);
    k = CoXlTestGridEnter(Field(2100.0f, AY));
    CHECK(k == a);
    CoXlTestGridLeave(k);
    CoXlTestLayerTile(b, 0, BX, AY);
    CoXlTestTileRemove(b);

    g_case = "0xDFA60 after the spawn keeps the grid it is given and never keeps it as the level's heightfield";
    int slot2 = -2;
    CHECK(CoXlTestLayerSnap(SpawnUnit(2100.0f, AY, 0, 0), given) == given);
    CHECK(CoXlTestHeightCopy() == g_levelData);
    CHECK(CoXlTestLayerWhy((uint64_t)(uintptr_t)g_spawnUnit, given, &slot2) == 0 && slot2 == a);
    CHECK(CoXlTestLayerSnap(SpawnUnit(2100.0f, AY, 0, 0), level) == Field(2100.0f, AY)); /* a placed-unit call on its own still gets the grid */
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestProbePlayer(0, 0.0f, 0.0f);

    /* The level generator leaves 3 only through 4 (game 0xF70D10), whose update destroys the records' stamp units
     * (0xF742F7), and the mission's unload destroys every other unit once the state is 0 (0xAB3CD9): from the first
     * frame that reads 4 or 0 the streamer holds no unit and no queued removal, and destroys nothing. */
    Start("the level leaves play with stamps flagged for new ground: all is forgotten and nothing is destroyed");
    Flagged();
    Generator(4); /* the unload asked for the teardown */
    CoXlTestStreamTick();
    int goneLive = 9, goneDue = 9; uint64_t goneUnit = 9; uint32_t left = 9, gone = 9, looks = 9; long calls = 9;
    for (uint32_t k = 0; k < 3; k++)
    {
        CoXlTestStreamSlotState(k, &goneLive, &goneDue, &goneUnit);
        CHECK(!goneLive && !goneDue && goneUnit == 0 && !CoXlTestStreamFlag(k) && !CoXlTestStreamQueued(k));
    }
    CHECK(g_destroys == 0 && fake_host_logged("the level left play (generator state 4): streaming stopped; 3 stamp units, 0 queued removals and 2 more flagged for new ground are forgotten"));
    CHECK(fake_host_logged("0 removed in play, 0 given up"));
    Generator(0);
    CoXlTestStreamTick();
    Generator(3); /* never after 0 without a level init; nothing is held to act on all the same */
    CoXlTestStreamTick();
    CHECK(CoXlTestStreamScanFrames(1) == 0 && CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 0 && g_destroys == 0);
    CHECK(Lines("the level left play") == 1 && ListCount() == 3);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("the level leaves play with removals queued: the queue is dropped, and a removal before the next tick destroys nothing");
    Flagged();
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueued(0) && CoXlTestStreamQueued(2)); /* decided, not done */
    Generator(0); /* the unload's reset, before the streamer's next frame */
    CHECK(CoXlTestStreamDrain(1.0e9, &left) == 0 && g_destroys == 0); /* the removal's own test */
    CHECK(CoXlTestStampsReplace(g_tileOfB, BX, AY) == 2 && CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamQueued(0));
    CoXlTestStreamTick();
    CHECK(fake_host_logged("the level left play (generator state 0): streaming stopped; 3 stamp units, 2 queued removals and 0 more flagged for new ground are forgotten"));
    CHECK(!CoXlTestStreamQueued(0) && !CoXlTestStreamQueued(2) && CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 0 && g_destroys == 0);
    for (int f = 0; f < 119; f++) CoXlTestStreamTick();
    CHECK(Lines("frames after level") == 0);
    CoXlTestStreamTick();
    CHECK(Lines("frames after level") == 1 && fake_host_logged("left play: 0 destroy call(s) by the streamer since"));
    for (int f = 0; f < 200; f++) CoXlTestStreamTick();
    CHECK(Lines("frames after level") == 1 && Lines("the level left play") == 1);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("the ship is reached before the streamer saw the generator leave 3: forgotten all the same");
    Flagged();
    CoXlTestMission(0, 0); /* no mission in play */
    CoXlTestStreamTick();
    CoXlTestStreamSlotState(0, &goneLive, &goneDue, &goneUnit);
    CHECK(g_destroys == 0 && !goneLive && goneUnit == 0 && !CoXlTestStreamFlag(0) && Lines("the level left play (generator state 3)") == 1);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);

    Start("a level still generating, or with a second pipeline in play, is held as it is and goes on at 3");
    CoXlTestMission((uintptr_t)g_lg, 1);
    StreamGame();
    InMission();
    Stream(4);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    Slot(0, BX + 500.0f, AY, 20.0f, 0x1000, 0);
    Slot(1, BX + 400.0f, AY, 20.0f, 0x2000, 0);
    Generator(1); /* the generation runs: captured, never yet in play */
    CoXlTestStreamTick();
    Generator(2);
    CoXlTestStreamTick();
    CoXlTestStreamSlotState(0, &goneLive, &goneDue, &goneUnit);
    CHECK(goneLive && goneUnit == U(0x1000) && Lines("the level left play") == 0);
    Generator(3);
    CoXlTestStreamTick();
    CHECK(CoXlTestStampsReplace(b, BX, AY) == 2);
    Generator(5); /* another pipeline: back to 3 after it (game 0xF7451E) */
    CoXlTestStreamTick();
    CoXlTestStreamSlotState(0, &goneLive, &goneDue, &goneUnit);
    CHECK(g_destroys == 0 && goneLive && goneUnit == U(0x1000) && CoXlTestStreamFlag(0) && Lines("the level left play") == 0);
    Generator(3);
    CoXlTestStreamTick();
    CHECK(g_destroys == 2 && g_unlisted == 0 && ListCount() == 0);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("the next level after a leave streams and removes as the first did");
    Flagged();
    CoXlTestStreamBelt(&gone, &looks, &calls);
    const long callsBefore = calls;
    Generator(4);
    CoXlTestStreamTick();
    Generator(3); /* a new level on the same generator object */
    Flagged();
    CoXlTestStreamTick();
    CoXlTestStreamBelt(&gone, &looks, &calls);
    CHECK(g_destroys == 2 && g_unlisted == 0 && gone == 0 && looks == 2 && calls == callsBefore + 2 && ListCount() == 1);
    CoXlTestStreamSlotState(1, &goneLive, &goneDue, &goneUnit);
    CHECK(goneLive && goneUnit == U(0x2000));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    /* DestroyUnit (exe 0x338960) frees its argument whether the world's unit list holds it or not, after running the
     * queued destroys: a unit the engine took, or is about to take, is never passed to it. */
    Start("a stamp unit the world no longer lists is given up without a destroy call");
    Flagged();
    CHECK(ListTake(U(0x1000))); /* the engine destroyed it */
    { const uint64_t held = U(0x1000); memcpy(g_array + 0xA8, &held, 8); } /* the record still names it */
    CoXlTestStreamTick();
    CoXlTestStreamBelt(&gone, &looks, &calls);
    CHECK(g_destroys == 1 && g_destroyed == U(0x3000) && g_unlisted == 0 && gone == 1 && looks == 2);
    CHECK(Lines("slot 0's stamp unit") == 1 && fake_host_logged("is not in the world's unit list any more: the slot is given up with no destroy call"));
    int spawned = 0;
    CoXlTestStreamSlotState(0, &goneLive, &goneDue, &goneUnit);
    CoXlTestStreamSpawned(0, &spawned, &goneDue);
    CHECK(!goneLive && goneUnit == 0 && spawned && !goneDue && !CoXlTestStreamFlag(0));
    { uint64_t at = 9; memcpy(&at, g_array + 0xA8, 8); CHECK(at == 0); }
    CoXlTestStreamObserver(1, BX + 500.0f, AY); /* a player right at it: a slot given up is never due again */
    CoXlTestStreamTick();
    CoXlTestStreamSpawned(0, &spawned, &goneDue);
    CHECK(spawned && !goneDue && g_destroys == 1);
    CoXlTestStreamObserver(0, 0.0f, 0.0f);
    Generator(0);
    CoXlTestStreamTick();
    CHECK(fake_host_logged("1 removed in play, 1 given up (their unit was gone); the world's unit list searched 2 times"));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("a stamp unit whose address is another world's unit now is given up too");
    Flagged();
    { const uint64_t other = 0xABC0; memcpy((void*)(uintptr_t)(U(0x3000) + 0x18), &other, 8); }
    CoXlTestStreamTick();
    CoXlTestStreamBelt(&gone, &looks, &calls);
    CHECK(g_destroys == 1 && g_destroyed == U(0x1000) && gone == 1 && looks == 2 && Lines("the slot is given up with no destroy call") == 1 && ListCount() == 2);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("an empty unit list gives the slots up, each with one search");
    Flagged();
    ListSet(0);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 0);
    CoXlTestStreamBelt(&gone, &looks, &calls);
    CHECK(g_destroys == 0 && gone == 2 && looks == 2);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    /* One look that does not read (a torn count, a fault in the scan) says nothing about the unit: its removal waits
     * at the queue's head for the next frame, and removal goes off for the level only after eight such looks in a row. */
    uint32_t lookBad = 9, lookFails = 9; int lookOff = 9;
    const uint64_t listGood = (uint64_t)(uintptr_t)g_listed, listBad = 0x10;
    Start("a unit list that does not read once: the removal is asked again the next frame and goes then");
    Flagged();
    memcpy(g_worldMem + 0x230590u, &listBad, 8);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 2 && g_destroys == 0);
    CoXlTestStreamBelt(&gone, &looks, &calls);
    CoXlTestStreamLooks(&lookBad, &lookFails, &lookOff);
    CHECK(gone == 0 && lookBad == 1 && lookFails == 1 && !lookOff && !fake_host_logged("removal off"));
    CHECK(CoXlTestStreamQueued(0) && CoXlTestStreamQueued(2));
    memcpy(g_worldMem + 0x230590u, &listGood, 8);
    CHECK(CoXlTestStreamDrain(1.0e9, &left) == 2 && left == 0 && g_destroys == 2 && g_unlisted == 0);
    CoXlTestStreamLooks(&lookBad, &lookFails, &lookOff);
    CHECK(lookBad == 0 && lookFails == 1 && !lookOff);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("a unit list that does not read eight frames in a row turns the removal off and keeps the slots");
    Flagged();
    memcpy(g_worldMem + 0x230590u, &listBad, 8);
    CHECK(CoXlTestStreamScanFrames(0) == 0);
    for (int f = 0; f < 7; f++) CHECK(CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 2);
    CoXlTestStreamLooks(&lookBad, &lookFails, &lookOff);
    CHECK(lookBad == 7 && lookFails == 7 && !lookOff && !fake_host_logged("removal off"));
    memcpy(g_worldMem + 0x230590u, &listGood, 8);
    CHECK(CoXlTestStreamDrain(1.0e9, &left) == 2 && left == 0 && g_destroys == 2); /* a look that reads: the row starts again */
    CoXlTestMission(0, 0);
    Flagged();
    memcpy(g_worldMem + 0x230590u, &listBad, 8);
    CHECK(CoXlTestStreamScanFrames(0) == 0);
    for (int f = 0; f < 7; f++) CHECK(CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 2);
    CHECK(!fake_host_logged("removal off"));
    CHECK(CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 2);
    CoXlTestStreamLooks(&lookBad, &lookFails, &lookOff);
    CoXlTestStreamBelt(&gone, &looks, &calls);
    CHECK(lookOff && lookFails == 8 && gone == 0 && g_destroys == 0 && fake_host_logged("removal off: the world's unit list did not read 8 times in a row"));
    memcpy(g_worldMem + 0x230590u, &listGood, 8);
    CHECK(CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 0 && g_destroys == 0); /* off for the level: the queue empties, nothing goes */
    for (uint32_t k = 0; k < 3; k += 2)
    {
        CoXlTestStreamSlotState(k, &goneLive, &goneDue, &goneUnit);
        CHECK(goneLive && goneUnit == U(0x1000u * (k + 1)));
    }
    Generator(0);
    CoXlTestStreamTick();
    CHECK(fake_host_logged("the world's unit list searched 8 times") && fake_host_logged("units at the last); 8 looks did not read"));
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    /* The search is bounded at 4194304 units: a count past that is taken for a torn or wrong read, never searched. */
    Start("a unit count past the search's bound is no list: nothing is searched or destroyed, and the removal waits");
    {
        static uint64_t* big;
        const uint32_t most = 1u << 22;
        if (big == NULL) big = (uint64_t*)VirtualAlloc(NULL, ((size_t)most + 1) * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        const uint64_t listBig = (uint64_t)(uintptr_t)big;
        Flagged();
        memset(big, 0, ((size_t)most + 1) * 8);
        big[0] = U(0x1000); big[1] = U(0x2000); big[2] = U(0x3000);
        memcpy(g_worldMem + 0x230590u, &listBig, 8);
        ListSet(most + 1);
        CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 2 && g_destroys == 0);
        CoXlTestStreamBelt(&gone, &looks, &calls);
        CoXlTestStreamLooks(&lookBad, &lookFails, &lookOff);
        CHECK(gone == 0 && looks == 0 && lookFails == 1);
        ListSet(most); /* the bound itself: searched */
        CHECK(CoXlTestStreamDrain(1.0e9, &left) == 2 && left == 0 && g_destroys == 2 && g_unlisted == 0 && ListCount() == most - 2);
        memcpy(g_worldMem + 0x230590u, &listGood, 8);
    }
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    /* The generator's state is read through a guarded read: one that fails says neither "in play" nor "left". */
    Start("a generator state that does not read holds the level as it is; only a state that reads ends it");
    Flagged();
    {
        DWORD was = 0;
        VirtualProtect(g_lg, 0x1000, PAGE_NOACCESS, &was);
        CoXlTestStreamTick();
        CoXlTestStreamTick();
        VirtualProtect(g_lg, 0x1000, was, &was);
    }
    CoXlTestStreamSlotState(0, &goneLive, &goneDue, &goneUnit);
    CHECK(g_destroys == 0 && goneLive && goneUnit == U(0x1000) && CoXlTestStreamFlag(0) && Lines("the level left play") == 0);
    CoXlTestStreamTick(); /* it reads 3 again: the level goes on */
    CHECK(g_destroys == 2 && g_unlisted == 0 && Lines("the level left play") == 0);
    Generator(4);
    CoXlTestStreamTick();
    CHECK(Lines("the level left play (generator state 4)") == 1);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    /* The guarded search (proto/safe_read.c) the look uses: through SafeRead in pieces, then in place under the fault
     * handler; the list's second page is not readable. */
    g_case = "a list is searched whole, and one that stops reading is no answer";
    {
        uint8_t* pages = (uint8_t*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        uint64_t* list = (uint64_t*)pages;
        DWORD was;
        for (uint32_t k = 0; k < 512; k++) list[k] = 0x5000 + k;
        VirtualProtect(pages + 0x1000, 0x1000, PAGE_NOACCESS, &was);
        for (int pass = 0; pass < 2; pass++)
        {
            CHECK(co_safe_find((uintptr_t)list, 512, 0x5000) == 1 && co_safe_find((uintptr_t)list, 512, 0x51FF) == 1);
            CHECK(co_safe_find((uintptr_t)list, 511, 0x51FF) == 0 && co_safe_find((uintptr_t)list, 512, 0x5200) == 0);
            CHECK(co_safe_find((uintptr_t)list, 0, 0x5000) == 0 && co_safe_find(0x10, 0, 0x5000) == 0);
            CHECK(co_safe_find((uintptr_t)list, 513, 0x9999) == -1 && co_safe_find((uintptr_t)(list + 500), 600, 0x9999) == -1);
            CHECK(co_safe_find((uintptr_t)(pages + 0x1000), 4, 0) == -1 && co_safe_find(0x10, 4, 0) == -1);
            CHECK(co_safe_find((uintptr_t)list + 4, 8, 0x5001) == -1 && co_safe_find(0x7FFFFFFE0000ull, (size_t)1 << 40, 0) == -1);
            if (pass == 1) CHECK(co_safe_find((uintptr_t)list, 600, 0x5100) == 1); /* in place: found before the page that does not read */
            co_shim_install(); /* the second pass searches in place */
        }
    }

    Start("slots given up count against a frame's removals: each cost a search of the unit list");
    CoXlTestMission((uintptr_t)g_lg, 1);
    StreamGame();
    InMission();
    Stream(8);
    g_tileOfB = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(g_tileOfB, 1, BX, AY);
    for (uint32_t k = 0; k < 6; k++) Slot(k, BX + 300.0f + 40.0f * (float)k, AY, 20.0f, 0x1000u * (k + 1), 0);
    Generator(3);
    CoXlTestStreamTick();
    CHECK(CoXlTestStampsReplace(g_tileOfB, BX, AY) == 6);
    ListSet(0); /* the engine took them all */
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamDrainMost(4, 1.0e9, &left) == 0 && left == 2);
    CoXlTestStreamBelt(&gone, &looks, &calls);
    CHECK(gone == 4 && looks == 4 && g_destroys == 0);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("a slot given up ends the frame's removals once their time is used, as a removal does");
    CoXlTestMission((uintptr_t)g_lg, 1);
    StreamGame();
    InMission();
    Stream(8);
    g_tileOfB = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(g_tileOfB, 1, BX, AY);
    for (uint32_t k = 0; k < 6; k++) Slot(k, BX + 300.0f + 40.0f * (float)k, AY, 20.0f, 0x1000u * (k + 1), 0);
    Generator(3);
    CoXlTestStreamTick();
    CHECK(CoXlTestStampsReplace(g_tileOfB, BX, AY) == 6);
    ListSet(0);
    CHECK(CoXlTestStreamScanFrames(0) == 0 && CoXlTestStreamDrain(0.0, &left) == 0 && left == 5); /* past the frame's time: one at least */
    CoXlTestStreamBelt(&gone, &looks, &calls);
    CHECK(gone == 1 && looks == 1 && g_destroys == 0);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("far stamps given up count as evicted, and one whose look did not read still holds the next eviction back");
    CoXlTestMission((uintptr_t)g_lg, 1);
    StreamGame();
    InMission();
    Stream(100);
    for (uint32_t k = 0; k < 100; k++) Slot(k, AX + 3000.0f, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0);
    Generator(3);
    CoXlTestStreamObserver(1, AX, AY);
    const uint32_t farBefore = CoXlTestWindowEvictedFar();
    CHECK(CoXlTestWindowEvictFar(0, 100) == 64);
    ListSet(0); /* the engine took them */
    CHECK(CoXlTestStreamDrainMost(4, 1.0e9, &left) == 0 && left == 60 && g_destroys == 0 && CoXlTestWindowEvictedFar() == farBefore + 4);
    memcpy(g_worldMem + 0x230590u, &listBad, 8); ListSet(1); /* a list of one that does not read */
    for (int f = 0; f < 3; f++) CHECK(CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 60);
    memcpy(g_worldMem + 0x230590u, &listGood, 8); ListSet(0);
    for (int f = 0; f < 20 && left > 0; f++) CoXlTestStreamDrain(1.0e9, &left);
    CHECK(left == 0 && CoXlTestWindowEvictedFar() == farBefore + 64);
    CHECK(CoXlTestWindowEvictFar(0, 100) == 36); /* the far count came back to 0 with the queue: the rest may go */
    CoXlTestStreamObserver(0, 0.0f, 0.0f);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    /* The governor's far removals count as far ones in the queue's far count, put back with their kind too: the
     * window's eviction is free again once they are gone. */
    Start("a governor's far removal whose look did not read goes back with its kind; the far count comes back to 0");
    CoXlTestMission((uintptr_t)g_lg, 1);
    StreamGame();
    InMission();
    Stream(20);
    for (uint32_t k = 0; k < 20; k++) { Slot(k, AX + 3000.0f, AY + 4.0f * (float)k, 2.0f, 0x1000u * (k + 1), 0); CoXlTestStreamOut2(k, 1500.0f); }
    Generator(3);
    CoXlTestStreamObserver(1, AX, AY);
    for (uint32_t k = 0; k < 10; k++) CHECK(CoXlTestStreamDropGovPush(k));
    CHECK(CoXlTestWindowEvictFar(10, 20) == 0);                     /* the governor's are queued: none more */
    ListSet(0);
    memcpy(g_worldMem + 0x230590u, &listBad, 8); ListSet(1);
    for (int f = 0; f < 3; f++) CHECK(CoXlTestStreamDrain(1.0e9, &left) == 0 && left == 10);
    CHECK(CoXlTestStreamDropKind(0) == 4);                           /* back at the head with its kind */
    memcpy(g_worldMem + 0x230590u, &listGood, 8); ListSet(0);
    for (int f = 0; f < 20 && left > 0; f++) CoXlTestStreamDrain(1.0e9, &left);
    CHECK(left == 0 && CoXlTestWindowEvictFar(10, 20) == 10);        /* the far count came back to 0 */
    CoXlTestStreamObserver(0, 0.0f, 0.0f);
    CoXlTestHeightKept(NULL, 0, 0, 0.0f);
    CoXlTestMission(0, 0);

    Start("the layer spawn's two callers with a heightfield go through it, only on the reviewed code");
    uint8_t* exe = fake_host_exe();
    DWORD old;
    VirtualProtect(exe, FAKE_EXE_SIZE, PAGE_EXECUTE_READWRITE, &old);
    g_fake.strictCode = 1;
    SpawnPins(exe);
    CoXlTestLayerSpawnInstall((uintptr_t)exe);
    CHECK(CoXlTestLayerSpawnOriginal() == (uintptr_t)exe + 0xDBEC0u);
    CHECK(CallGoesTo(exe, 0x32E63Au) == CoXlTestLayerSpawnDetour() && CallGoesTo(exe, 0x32E69Au) == CoXlTestLayerSpawnDetour());
    CHECK(fake_host_logged("a stamp layer by a tile spawns on its grid for every member"));
    exe = fake_host_exe();
    SpawnPins(exe);
    exe[0xDC47Au + 3] = 0x50; /* SpeedTree's descriptor from another slot: not the reviewed code */
    CoXlTestLayerSpawnInstall((uintptr_t)exe);
    CHECK(CoXlTestLayerSpawnOriginal() == 0);
    CHECK(CallGoesTo(exe, 0x32E63Au) == (uintptr_t)exe + 0xDBEC0u && CallGoesTo(exe, 0x32E69Au) == (uintptr_t)exe + 0xDBEC0u);
    CHECK(fake_host_logged("the layer spawn is not the reviewed code"));
    g_fake.strictCode = 0;

    Start("every build logs the layer reads near a player, each one off the ground as drawn, within a line budget");
    a = CoXlTestTileAddAt(AX, AY, g_tileA, 2, SPAN);
    CoXlTestLayerTile(a, 1, AX, AY);
    long reads = 0, off = 0, written = 0;
    CoXlTestProbePlayer(1, AX, AY);
    CoXlTestProbe('H', AX + 10.0f, AY, 45.0f, 0);
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 1 && off == 0 && written == 1);
    CHECK(fake_host_logged("layer probe: ok H 0x0 at (3084.7, -4094.9), 10 m from a player: used 45.00, drawn 45.0"));
    CoXlTestProbe('S', AX + 250.0f, AY, 45.45f, 0); /* under 0.5 m: one of the plain reads, not written */
    CoXlTestProbe('S', AX, AY + 250.0f, 52.0f, 5);  /* the level's heights, 7 m over the tile */
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 3 && off == 1 && written == 2);
    CHECK(fake_host_logged("layer probe: OFF S 0x0 at (3074.7, -3844.9), 250 m from a player: used 52.00, drawn 45.0"));
    CHECK(fake_host_logged("source 0 (no tile's grid)"));
    CoXlTestProbe('S', AX + 350.0f, AY, 99.0f, 0);
    CoXlTestProbe('S', AX + 212.2f, AY + 212.2f, 99.0f, 0); /* 300.1 m on the diagonal: a circle, not a square */
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 3 && off == 1);
    CoXlTestProbePlayer(0, AX, AY - 500.0f); /* no one has landed: the start area */
    CoXlTestProbe('H', AX, AY - 600.0f, 45.0f, 0);
    CoXlTestProbe('H', AX, AY + 400.0f, 45.0f, 0); /* 900 m from it */
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 4 && off == 1);
    b = CoXlTestTileAddAt(BX, AY, g_tileB, 2, SPAN);
    CoXlTestLayerTile(b, 1, BX, AY);
    CHECK(fake_host_logged("layer probe (a tile came in): 4 reads within 300 m of a player, 1 of them over 0.5 m off the ground as drawn (worst 6.99 m); ok 3, no tile's grid 1; 2 of 400 lines written"));
    for (int k = 0; k < 300; k++) CoXlTestProbe('S', AX, AY - 500.0f, 60.0f, 0);
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 304 && off == 301 && written == 153); /* the first 150 off, then the 192nd and the 256th */
    for (int k = 0; k < 19700; k++) CoXlTestProbe('S', AX, AY - 500.0f, 60.0f, 0);
    CoXlTestProbeCounts(&reads, &off, &written);
    CHECK(reads == 20004 && off == 20001 && written == 400); /* past the budget */

    Start("the leaf thunks keep xmm1-xmm5 for the game's callers and pass the return address or the fifth argument on");
    float out[8]; uint64_t r9 = 0, fifth = 0;
    CHECK(CoXlTestThunk(0, out, &r9, &fifth) == 77);
    CHECK(out[0] == 7.0f && out[1] == 1.0f && out[2] == 2.0f && out[3] == 3.0f && out[4] == 4.0f && out[5] == 5.0f);
    CHECK(r9 != 0 && r9 == *(uint64_t*)&out[6]); /* the sampler's caller, for its RVA */
    CHECK(CoXlTestThunk(1, out, &r9, &fifth) == 77);
    CHECK(out[0] == 0.5f && out[1] == 1.0f && out[2] == 2.0f && out[3] == 3.0f && out[4] == 4.0f && out[5] == 5.0f);
    CHECK(r9 == 0x44 && fifth == 0x5555);

    Start("a stub over a game function's body unwinds as that function at the site, and the thunks have unwind data");
    {
        uintptr_t site = (uintptr_t)UnwindHostBody + 4u; /* past its prologue */
        uint64_t u[10] = {0};
        CHECK(CoXlTestUnwind(site, 0, u) == 0 && memcmp(u, u + 5, 5 * sizeof u[0]) == 0 && u[0] >= 0x1000u && u[0] < 0x1400u);
        CHECK(u[3] != 0 && u[4] != 0); /* r12 and xmm6 came off the stack: the save offsets were applied */
        CHECK(CoXlTestUnwind(site, 1, u) == 0 && memcmp(u, u + 5, 5 * sizeof u[0]) == 0);
        CHECK(CoXlTestUnwindWith(site, 0, 1, 0, u) == 0 && memcmp(u, u + 5, 5 * sizeof u[0]) == 0); /* four pushes and 0x80: 0xA0 */
        CHECK(CoXlTestUnwindWith(site, 0, 1, 1, u) == 0 && memcmp(u, u + 5, 5 * sizeof u[0]) == 0);
        uintptr_t chained = ChainedHost(0), guarded = ChainedHost(1);
        CHECK(chained != 0 && guarded != 0);
        if (chained != 0 && guarded != 0)
        {
            CHECK(CoXlTestUnwindWith(chained, 0, 1, 0, u) == 0 && memcmp(u, u + 5, 5 * sizeof u[0]) == 0 && u[3] != 0 && u[4] != 0);
            CHECK(CoXlTestUnwindWith(chained, 0, 0, 0, u) == 0 && memcmp(u, u + 5, 5 * sizeof u[0]) == 0);
            CHECK(CoXlTestUnwindWith(chained, 0, 1, 1, u) == 0 && memcmp(u, u + 5, 5 * sizeof u[0]) == 0); /* 3 bytes in: every game code */
            CHECK(CoXlTestUnwindWith(guarded, 0, 0, 0, u) == -12);                        /* a handler not named: refused */
            CHECK(CoXlTestUnwindWith(guarded, (guarded & ~(uintptr_t)0xFFF) + 0x300, 0, 0, u) == 0 && memcmp(u, u + 5, 5 * sizeof u[0]) == 0);
        }
        uint64_t t[2] = {0}, top = 0;
        CHECK(CoXlTestThunkUnwind(t, &top) == 0 && t[1] == top + 0x98u + 8u && t[0] == 0x2000u + 0x98u / 8u);
    }

    if (g_failed) { printf("layergrid: %d checks failed\n", g_failed); return 1; }
    printf("layergrid: all cases passed\n");
    return 0;
}
