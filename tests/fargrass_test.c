/* Far grass on an extra large level: the bake's planner and view squares agree with the overlay's 16 km mapping; ops
 * 0x4B and 0x67 hand stand-ins to the game and keep the builder's textures; in play a batch publishes them, makes a
 * viewport, camera and environment as op 0x49 does, queues one op 0x4A job per view and then the builder, and takes it
 * all down once the queue has read empty; the overlay's render setting is on only in such a level's play. Run on the
 * fake game. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestWorldCurrent(float current);
void CoXlTestMission(uintptr_t level, int complex);
void CoXlTestWorldShip(void);
int CoXlTestFgBatch(const float (*players)[2], int count, float reach, uint16_t* list, int max);
void CoXlTestFgMark(int i, int j);
void CoXlTestFgView(int i, int j, float* centre, float* rect);
void CoXlTestFgSub(int i, int j, int q, float* box, float* rect);
int CoXlTestFgSubs(void);
uint64_t CoXlTestFgJobCells(float size, float grow);
uint32_t CoXlTestFgJobs(void);
void CoXlTestFgInstall(void* aim, void* generate);
void CoXlTestFgKeep(uint64_t record, uint32_t op);
uint64_t CoXlTestFgTex(int k);
void CoXlTestFgTick(uint64_t level);
int CoXlTestFgBusy(void);
int CoXlTestFgSetting(void);
uint32_t CoXlTestFgBaked(void);
void CoXlTestFgObserver(int on, float x, float y);
int CoXlTestFgBakeView(const uint8_t* view);
void CoXlTestFgWorld(float live);
int CoXlTestScatterUpdate(LONG op, LONG record, uint8_t* view);
int CoXlTestScatterBake(void);
int CoXlTestScatterLayer(uint8_t* view, uint8_t* layer, int* locked);
void CoXlTestScatterHold(uint8_t* layer, int hold);
void CoXlTestScatterCounts(LONG64* bake, LONG64* overlap);
void CoXlTestFgFrames(const float* ms, int n, double* worst, double* mean);
void CoXlTestScatLimits(int bake, int* budget, uint64_t* most);
void CoXlTestFgState(int state);
void CoXlTestFgOut(int on);
void CoXlTestFgNotesReset(void);
void CoXlTestFgLinker(uint64_t record);
int CoXlTestFgViewBaked(int i, int j);
static uint8_t g_linkerRecord[0x19B0];
static uint8_t g_linkerRes[32];
static uint16_t g_linkerSent[1024];
static int g_linkerSends;
static void* FakeFind(uint32_t name) { return name == 0x499EAFD7u ? g_linkerRes : NULL; }
static void FakeUpdate(void* res, const void* data, uint32_t size)
{
    if (res == g_linkerRes && size == sizeof g_linkerSent) { memcpy(g_linkerSent, data, size); g_linkerSends++; }
}
/* The game's linker in op 0x4A's record: zone z, material m -> slot (z + m) % 17. */
static void LinkerRecord(int bad)
{
    uint16_t* e = (uint16_t*)(g_linkerRecord + 8 + 0x1168);
    for (int k = 0; k < 1024; k++) e[k] = (uint16_t)(((k >> 5) + (k & 31)) % 17);
    if (bad) e[5] = 40;
    CoXlTestFgLinker((uint64_t)(uintptr_t)g_linkerRecord);
}
/* Whether the last upload keeps view (i, j), and every entry's slot is the game's. */
static int LinkerKeeps(int i, int j) { return (g_linkerSent[(63 - j) * 16 + (i >> 2)] >> (12 + (i & 3))) & 1; }
static int LinkerSlotsKept(void)
{
    for (int k = 0; k < 1024; k++) if ((g_linkerSent[k] & 0x0FFF) != ((k >> 5) + (k & 31)) % 17) return 0;
    return 1;
}
void CoXlTestScatBakeOver(LONG n);
void CoXlTestFgProfileGuard(void* doc, void* get);
static uint8_t g_docRoot[24], g_docRender[24], g_docKey[24]; /* settings document nodes: type bits 0-2, value at +8 */
static void* FakeDocGet(void* node, const char* key)
{
    if (node == g_docRoot && strcmp(key, "render_settings") == 0) return g_docRender;
    if (node == g_docRender && strcmp(key, "debug_deformation_prevention") == 0) return g_docKey;
    return NULL;
}
static int DocKey(uint32_t head, uint8_t value)
{
    memcpy(g_docKey, &head, 4); g_docKey[8] = value;
    CoXlTestFgProfileGuard(g_docRoot, (void*)FakeDocGet);
    uint32_t now; memcpy(&now, g_docKey, 4);
    return (int)(now << 8 | g_docKey[8]);
}
int CoXlTestFgPaced(double mean, float last, uint32_t* deferred);
void CoXlTestFgRing(int a0, int a1, int b0, int b1);
void CoXlTestMover(float ox, float oy, int state, int merge, int a, int b);
void CoXlTestMoverTick(uintptr_t level, float observerX, float observerY);

static int g_failed;
static volatile LONG g_holding;
static DWORD WINAPI HoldLayer(void* layer)
{
    CoXlTestScatterHold((uint8_t*)layer, 1);
    g_holding = 1;
    Sleep(100);
    CoXlTestScatterHold((uint8_t*)layer, 0);
    return 0;
}
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define API 0x3600000u /* [game+0x3326308]: the engine's interface; its tables at API + 0x1000 * n */
static const uint32_t kTables[] = {0x08, 0x10, 0x18, 0x20, 0xE0};
#define DENSITY 0xD1000ull
#define DENSITY_TMP 0xD2000ull
#define LOOKUP_TMP 0xD3000ull
#define ZONES 0xD4000ull
#define WORLD 0x3000ull
#define VP 0x7100ull
#define CAMERA 0x7200ull
#define ENV 0x7300ull
#define UNIT 0x77u

/* What the fake engine saw. */
static struct
{
    int creates, setTrue, setFalse, published, unpublished, destroyed, vps, vpDestroys, units, unitDestroys, envs, envDestroys, aims, generates;
    uint32_t createFormat[8], createId[8], createW, createH;
    uint32_t publishName[8]; uint64_t publishTex[8];
    uint64_t destroyedTex[8];
    float aimSize, aimK, farZ;
    uint64_t output[2];
    float at[3]; uint32_t moveUnit;
    float rect[4], box[4]; int boxes;
    uint32_t generateHash, generateBehind; uint64_t generateQueue, generateWorld;
    uint32_t vpName; uint64_t unitHash, envHash;
} g_saw;
static uint64_t g_nextStandIn;

static uint64_t FakeCreate(uint32_t w, uint32_t h, uint32_t mips, uint32_t format, uint32_t id, uint32_t flags)
{
    (void)mips; (void)flags;
    if (g_saw.creates < 8) { g_saw.createFormat[g_saw.creates] = format; g_saw.createId[g_saw.creates] = id; }
    g_saw.createW = w; g_saw.createH = h; g_saw.creates++;
    return g_nextStandIn++;
}
static void FakeDestroy(uint64_t tex) { if (g_saw.destroyed < 8) g_saw.destroyedTex[g_saw.destroyed] = tex; g_saw.destroyed++; }
static void FakePublish(uint32_t name, uint64_t tex) { if (g_saw.published < 8) { g_saw.publishName[g_saw.published] = name; g_saw.publishTex[g_saw.published] = tex; } g_saw.published++; }
static void FakeUnpublish(uint32_t name) { (void)name; g_saw.unpublished++; }
static void FakeSetting(const char* name, const char* value)
{
    if (strcmp(name, "debug_deformation_prevention") != 0) return;
    if (strcmp(value, "true") == 0) g_saw.setTrue++;
    if (strcmp(value, "false") == 0) g_saw.setFalse++;
}
static uint64_t FakeVp(uint64_t world, uint32_t name) { (void)world; g_saw.vpName = name; g_saw.vps++; return VP; }
static void FakeVpDestroy(uint64_t world, uint64_t vp) { if (world == WORLD && vp == VP) g_saw.vpDestroys++; }
static uint32_t FakeSpawn(uint64_t world, uint64_t hash, const float* pose) { (void)world; (void)pose; g_saw.unitHash = hash; g_saw.units++; return UNIT; }
static void FakeUnitDestroy(uint64_t world, uint32_t unit) { if (world == WORLD && unit == UNIT) g_saw.unitDestroys++; }
static uint64_t FakeCamera(uint32_t unit, uint32_t index) { return unit == UNIT && index == 0 ? CAMERA : 0; }
static uint64_t FakeEnv(uint64_t world, uint64_t hash) { (void)world; g_saw.envHash = hash; g_saw.envs++; return ENV; }
static void FakeEnvDestroy(uint64_t world, uint64_t env) { if (world == WORLD && env == ENV) g_saw.envDestroys++; }
static void FakeFar(uint64_t camera, float farZ) { if (camera == CAMERA) g_saw.farZ = farZ; }
static void FakeOutput(uint64_t vp, uint64_t tex) { g_saw.output[0] = vp; g_saw.output[1] = tex; }
static void FakeMove(uint64_t camera, uint32_t unit, const float* xyz) { if (camera == CAMERA) { memcpy(g_saw.at, xyz, 12); g_saw.moveUnit = unit; } }
static void FakeUnitUpdate(uint64_t world, uint32_t unit) { (void)world; (void)unit; }
static void FakeBox(uint64_t camera, float l, float r, float b, float t)
{
    if (camera == CAMERA) { g_saw.box[0] = l; g_saw.box[1] = r; g_saw.box[2] = b; g_saw.box[3] = t; g_saw.boxes++; }
}
static void FakeRect(uint64_t vp, float x, float y, float w, float h) { if (vp == VP) { g_saw.rect[0] = x; g_saw.rect[1] = y; g_saw.rect[2] = w; g_saw.rect[3] = h; } }
static void FakeAim(uint64_t world, uint32_t unit, uint64_t camera, float size, float k)
{
    if (world == WORLD && unit == UNIT && camera == CAMERA) { g_saw.aimSize = size; g_saw.aimK = k; g_saw.aims++; }
}
static void FakeGenerate(uint64_t queue, uint64_t world, uint32_t hash, uint64_t unused, uint32_t n1, const void* p1, uint32_t n2, const void* p2,
    const void* p3, uint32_t last)
{
    (void)unused; (void)p1; (void)p2; (void)p3;
    if (n1 == 0 && n2 == 0 && last == 0) { g_saw.generateHash = hash; g_saw.generateQueue = queue; g_saw.generateWorld = world; g_saw.generates++; }
    uint32_t* count = (uint32_t*)(uintptr_t)(queue + 0x10);
    g_saw.generateBehind += *count; /* jobs still queued: the builder would read a view not yet drawn */
    (*count)++; /* the builder's job is on the queue until the render callback drains it */
}

/* The two overlay libraries as loaded: their GPU parts, each pixel stage a DXBC container at the edit's offset. */
static uint8_t g_lib[3][0xCE70]; /* [2]: what the engine hands for a name it has not loaded (the type's default) */
static int g_libLoaded[2];
static const uint64_t kLibName[2] = {0x2FA016B4649B8927ull, 0xECF7F262F57585A0ull};
static void Libraries(int edited)
{
    static const uint32_t at[2][2] = {{0x1FB8, 0x7DE0}, {0x1CC8, 0x44F8}}, size[2] = {0x4F14, 0x1C20}, run[2] = {0x4B18, 0x1840};
    static const uint8_t stock[16] = {0x32, 0, 0, 0x0F, 0xC2, 0, 0x10, 0, 0, 0, 0, 0, 0x06, 0x04, 0x10, 0};
    static const uint8_t nops[16] = {0x3A, 0, 0, 1, 0x3A, 0, 0, 1, 0x3A, 0, 0, 1, 0x3A, 0, 0, 1};
    static const uint8_t scale[8] = {0, 0, 0x80, 0x38, 0, 0, 0x80, 0xB8}, scale0[8] = {0, 0, 0, 0x3A, 0, 0, 0, 0xBA};
    memset(g_lib, 0, sizeof g_lib);
    for (int k = 0; k < 2; k++)
    {
        g_libLoaded[k] = 1;
        for (int st = 0; st < 2; st++)
        {
            uint8_t* b = g_lib[k] + at[k][st];
            memcpy(b, "DXBC", 4); memcpy(b + 24, &size[k], 4);
            memcpy(b + run[k], edited ? nops : stock, 16);
            if (k == 0) { memcpy(b + 0x1818, edited ? scale : scale0, 8); memcpy(b + 0x1894, edited ? scale : scale0, 8); }
        }
    }
    memcpy(g_lib[2], g_lib[1], sizeof g_lib[1]); /* a default that would pass for library 1 */
}
static uint8_t FakeCanGet(uint64_t type, uint64_t name)
{
    for (int k = 0; k < 2; k++) if (type == 0xE5EE32A477239A93ull && name == kLibName[k]) return (uint8_t)g_libLoaded[k];
    return 0;
}
static uint64_t FakeGet(uint64_t name, uint64_t type)
{
    for (int k = 0; k < 2; k++) if (type == 0xE5EE32A477239A93ull && name == kLibName[k]) return (uint64_t)(uintptr_t)g_lib[g_libLoaded[k] ? k : 2];
    return 0;
}

static void Slot(uint32_t table, uint32_t slot, void* fn)
{
    uintptr_t f = (uintptr_t)fn;
    memcpy(g_fake.image + API + 0x1000u * (table == 0xE0 ? 5 : table / 8) + slot, &f, 8);
}

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    const uintptr_t game = (uintptr_t)g_fake.image, api = game + API;
    memcpy(g_fake.image + 0x3326308u, &api, 8);
    for (size_t k = 0; k < sizeof kTables / sizeof kTables[0]; k++)
    {
        uintptr_t t = api + 0x1000u * (kTables[k] == 0xE0 ? 5 : kTables[k] / 8);
        memcpy(g_fake.image + API + kTables[k], &t, 8);
    }
    Slot(0x10, 0x170, (void*)FakeCreate); Slot(0x10, 0x190, (void*)FakeDestroy); Slot(0x10, 0x210, (void*)FakePublish);
    Slot(0x10, 0x218, (void*)FakeUnpublish); Slot(0x10, 0xD8, (void*)FakeSetting); Slot(0x10, 0x80, (void*)FakeVp);
    Slot(0x10, 0x88, (void*)FakeVpDestroy); Slot(0x08, 0x08, (void*)FakeSpawn); Slot(0x08, 0x60, (void*)FakeUnitDestroy);
    Slot(0x08, 0x100, (void*)FakeUnitUpdate); Slot(0x08, 0x468, (void*)FakeEnv); Slot(0x08, 0x478, (void*)FakeEnvDestroy);
    Slot(0x18, 0x6B0, (void*)FakeCamera); Slot(0x20, 0x20, (void*)FakeMove); Slot(0x20, 0x70, (void*)FakeFar);
    Slot(0xE0, 0x0, (void*)FakeRect); Slot(0x20, 0x98, (void*)FakeBox); Slot(0xE0, 0x10, (void*)FakeOutput);
    Slot(0x10, 0x388, (void*)FakeCanGet); Slot(0x10, 0x3F8, (void*)FakeGet); Slot(0x10, 0x158, (void*)FakeFind); Slot(0x10, 0x208, (void*)FakeUpdate);
    g_linkerSends = 0; memset(g_linkerSent, 0, sizeof g_linkerSent);
    Libraries(1);
    CoXlTestGame(game);
    CoXlTestFgInstall((void*)FakeAim, (void*)FakeGenerate);
    CoXlTestFgObserver(0, 0, 0);
    memset(&g_saw, 0, sizeof g_saw);
    g_nextStandIn = 0x5100;
}

/* Op 0x4B's record: +8 its data, whose +0x90 says what op 0x49 made; op 0x67's: +8 a block whose +0 is the merge R. */
static uint8_t g_rec4B[0x19B0], g_rec67[0x19B0], g_data[0x100], g_block[0x10], g_merge[0x100];
static void Records(int32_t made)
{
    uint64_t d = (uint64_t)(uintptr_t)g_data, b = (uint64_t)(uintptr_t)g_block, r = (uint64_t)(uintptr_t)g_merge;
    uint64_t density = DENSITY, tmp = DENSITY_TMP, lookup = LOOKUP_TMP, zones = ZONES;
    memset(g_data, 0, sizeof g_data); memset(g_merge, 0, sizeof g_merge);
    memcpy(g_rec4B + 8, &d, 8); memcpy(g_data + 0x90, &made, 4);
    memcpy(g_data + 0x38, &density, 8); memcpy(g_data + 0x40, &tmp, 8); memcpy(g_data + 0x48, &lookup, 8);
    memcpy(g_rec67 + 8, &b, 8); memcpy(g_block, &r, 8); memcpy(g_merge + 0xB8, &zones, 8);
}
static uint64_t At(const uint8_t* p, size_t at) { uint64_t v; memcpy(&v, p + at, 8); return v; }

/* The level in play: +0x1E4 its state, +0x2A8 its records' block, whose context (+0x19B008) holds the world (+0x40),
 * the generator's queue (+0x50) and the settings (+8, of which +0x74 goes into each job). */
static uint8_t* g_level;
static uint8_t* g_queue;
#define CTX 0x19B008u
static void Level(void)
{
    memset(g_level, 0, 0x1000);
    uint32_t state = 3; uint64_t block = (uint64_t)(uintptr_t)g_level + 0x1000, world = WORLD, queue = (uint64_t)(uintptr_t)g_queue,
        settings = (uint64_t)(uintptr_t)g_level + 0x800;
    memcpy(g_level + 0x1E4, &state, 4); memcpy(g_level + 0x2A8, &block, 8);
    uint8_t* ctx = g_level + 0x1000 + CTX;
    memcpy(ctx + 0x40, &world, 8); memcpy(ctx + 0x50, &queue, 8); memcpy(ctx + 8, &settings, 8);
    memset(g_level + 0x800 + 0x74, 0xAB, 16);
    memset(g_queue, 0, 0x4000);
}
static uint32_t Queued(void) { uint32_t n; memcpy(&n, g_queue + 0x10, 4); return n; }
static void Drain(void) { memset(g_queue + 0x10, 0, 4); }

/* A complex mission whose load kept the textures, in play. */
static void Kept(void)
{
    CoXlTestWorldCurrent(8.0f);
    CoXlTestMission((uintptr_t)g_level, 1);
    CoXlTestFgWorld(8.0f);
    Records(2);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec4B, 0x4B);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec67, 0x67);
    Level();
}

static int Near(float a, float b) { return fabsf(a - b) < 1e-4f; }

int main(void)
{
    g_level = (uint8_t*)VirtualAlloc(NULL, 0x1000 + CTX + 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_queue = (uint8_t*)VirtualAlloc(NULL, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_level == NULL || g_queue == NULL) { printf("fargrass: no memory\n"); return 1; }

    g_case = "a view's square and rectangle are where the overlay reads them";
    {
        float c[2], r[4];
        CoXlTestFgView(0, 0, c, r);
        CHECK(Near(c[0], -8064.0f) && Near(c[1], -8064.0f) && Near(r[0], 0.0f) && Near(r[1], 63.0f / 64) && Near(r[2], 1.0f / 64) && Near(r[3], 1.0f / 64));
        CoXlTestFgView(63, 0, c, r);
        CHECK(Near(c[0], 8064.0f) && Near(c[1], -8064.0f) && Near(r[0], 63.0f / 64) && Near(r[1], 63.0f / 64));
        CoXlTestFgView(5, 60, c, r);
        CHECK(Near(c[0], -8192.0f + 5.5f * 256) && Near(c[1], -8192.0f + 60.5f * 256) && Near(r[0], 5.0f / 64) && Near(r[1], 3.0f / 64));
        /* every point's texel in the overlay (u = x / 16384 + 0.5, v = 0.5 - y / 16384, 4096 texels) is in its view's rectangle */
        int wrong = 0;
        for (float y = -8191.0f; y < 8192.0f; y += 97.3f)
            for (float x = -8191.0f; x < 8192.0f; x += 101.7f)
            {
                int i = (int)floorf((x + 8192.0f) / 256.0f), j = (int)floorf((y + 8192.0f) / 256.0f);
                CoXlTestFgView(i, j, c, r);
                float u = (x / 16384.0f + 0.5f) * 4096.0f, v = (0.5f - y / 16384.0f) * 4096.0f;
                if (u < r[0] * 4096.0f || u >= (r[0] + r[2]) * 4096.0f || v < r[1] * 4096.0f || v >= (r[1] + r[3]) * 4096.0f) wrong++;
                if (fabsf(x - c[0]) > 128.0f || fabsf(y - c[1]) > 128.0f) wrong++;
            }
        CHECK(wrong == 0);
    }

    g_case = "a view's jobs: their rectangles tile the view's without overlap, each box's ground in its own rectangle";
    {
        CHECK(CoXlTestFgSubs() == 8);
        int wrong = 0;
        static const int views[4][2] = {{0, 0}, {63, 0}, {5, 60}, {31, 33}};
        for (int v = 0; v < 4; v++)
        {
            const int i = views[v][0], j = views[v][1];
            float c[2], r[4];
            CoXlTestFgView(i, j, c, r);
            static uint8_t cover[64][64]; /* the view's 64 x 64 texels */
            memset(cover, 0, sizeof cover);
            for (int q = 0; q < 8; q++)
            {
                float b[4], s[4];
                CoXlTestFgSub(i, j, q, b, s);
                /* 64 x 128 m boxes inside the view's square, at whole texels of the 4096 target */
                wrong += !(b[1] - b[0] == 64.0f && b[3] - b[2] == 128.0f && b[0] >= -128.0f && b[1] <= 128.0f && b[2] >= -128.0f && b[3] <= 128.0f);
                wrong += !(s[2] * 4096.0f == 16.0f && s[3] * 4096.0f == 32.0f);
                const float x0 = s[0] * 4096.0f - r[0] * 4096.0f, y0 = s[1] * 4096.0f - r[1] * 4096.0f;
                wrong += x0 != floorf(x0) || y0 != floorf(y0);
                for (int ty = 0; ty < 32; ty++)
                    for (int tx = 0; tx < 16; tx++)
                    {
                        const int cx = (int)x0 + tx, cy = (int)y0 + ty;
                        if (cx < 0 || cx >= 64 || cy < 0 || cy >= 64) { wrong++; continue; }
                        cover[cy][cx]++;
                    }
                /* the ortho box maps x east, y north onto the rectangle with the top row north (clip +y up): a texel's
                 * centre from the box must be the overlay's texel (u = x / 16384 + 0.5, v = 0.5 - y / 16384) */
                for (int ty = 0; ty < 32; ty += 7)
                    for (int tx = 0; tx < 16; tx += 5)
                    {
                        const float wx = c[0] + b[0] + (tx + 0.5f) * 4.0f, wy = c[1] + b[3] - (ty + 0.5f) * 4.0f;
                        const float u = (wx / 16384.0f + 0.5f) * 4096.0f, vv = (0.5f - wy / 16384.0f) * 4096.0f;
                        wrong += (int)floorf(u) != (int)(s[0] * 4096.0f) + tx || (int)floorf(vv) != (int)(s[1] * 4096.0f) + ty;
                    }
            }
            for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) wrong += cover[y][x] != 1;
        }
        CHECK(wrong == 0);
        /* a 16 m layer with an 8 m pad: 18 x 18 cells for the whole square, 6 x 10 for a job */
        CHECK(CoXlTestFgJobCells(16.0f, 8.0f) == 60 && CoXlTestFgJobCells(16.0f, 4.0f) == 60 && CoXlTestFgJobCells(4.0f, 2.0f) == 18 * 34);
    }

    Start("the planner: unbaked views near a player, nearest first");
    {
        uint16_t list[64];
        const float one[1][2] = {{100.0f, -300.0f}};
        int n = CoXlTestFgBatch(one, 1, 768.0f, list, 64);
        /* views whose square is within 768 m of the player, counted here on their own */
        int want = 0;
        for (int j = 0; j < 64; j++)
            for (int i = 0; i < 64; i++)
            {
                float cx = -8192.0f + (i + 0.5f) * 256, cy = -8192.0f + (j + 0.5f) * 256;
                float dx = fmaxf(0, fabsf(100.0f - cx) - 128), dy = fmaxf(0, fabsf(-300.0f - cy) - 128);
                want += sqrtf(dx * dx + dy * dy) <= 768.0f;
            }
        CHECK(n == want && n > 4 && n <= 64);
        CHECK(list[0] == (uint16_t)(30 * 64 + 32)); /* the view the player stands in: x 100 in column 32, y -300 in row 30 */
        float last = -1; int sorted = 1;
        for (int k = 0; k < n; k++)
        {
            int i = list[k] % 64, j = list[k] / 64;
            float cx = -8192.0f + (i + 0.5f) * 256, cy = -8192.0f + (j + 0.5f) * 256;
            float dx = fmaxf(0, fabsf(100.0f - cx) - 128), dy = fmaxf(0, fabsf(-300.0f - cy) - 128), d = sqrtf(dx * dx + dy * dy);
            sorted &= d >= last; last = d;
        }
        CHECK(sorted);
        CoXlTestFgMark(32, 30);
        CHECK(CoXlTestFgBatch(one, 1, 768.0f, list, 64) == n - 1 && list[0] != (uint16_t)(30 * 64 + 32));
        /* a short list keeps the nearest */
        uint16_t few[3];
        CHECK(CoXlTestFgBatch(one, 1, 1536.0f, few, 3) == 3 && few[0] == list[0]);
        const float away[2][2] = {{NAN, 0.0f}, {20000.0f, 20000.0f}};
        CHECK(CoXlTestFgBatch(away, 2, 768.0f, list, 64) == 0);
        const float two[2][2] = {{-8000.0f, -8000.0f}, {8000.0f, 8000.0f}};
        CHECK(CoXlTestFgBatch(two, 2, 10.0f, list, 64) == 2); /* each player's own view, at the world's corners */
    }

    Start("ops 0x4B and 0x67 on a complex level keep the builder's textures and hand the game stand-ins");
    CoXlTestWorldCurrent(8.0f);
    CoXlTestMission(0, 0);
    Records(2);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec4B, 0x4B);
    CHECK(CoXlTestFgTex(0) == DENSITY && CoXlTestFgTex(1) == DENSITY_TMP && CoXlTestFgTex(2) == LOOKUP_TMP && CoXlTestFgTex(3) == 0);
    CHECK(g_saw.creates == 3 && g_saw.createW == 1 && g_saw.createH == 1);
    CHECK(g_saw.createFormat[0] == 0x1A && g_saw.createFormat[1] == 0x1A && g_saw.createFormat[2] == 0x16);
    CHECK(At(g_data, 0x38) == 0x5100 && At(g_data, 0x40) == 0x5101 && At(g_data, 0x48) == 0x5102);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec67, 0x67);
    CHECK(CoXlTestFgTex(3) == ZONES && At(g_merge, 0xB8) == 0x5103 && g_saw.createFormat[3] == 0x2A && g_saw.createId[3] == 0x1524FE5Au);
    CoXlTestWorldShip();
    CHECK(g_saw.destroyed == 4 && g_saw.destroyedTex[0] == DENSITY && g_saw.destroyedTex[3] == ZONES && CoXlTestFgTex(0) == 0);

    Start("op 0x4B when op 0x49 made nothing, and a vanilla-size level");
    CoXlTestWorldCurrent(8.0f);
    CoXlTestMission(0, 0);
    Records(0);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec4B, 0x4B);
    CHECK(g_saw.creates == 0 && CoXlTestFgTex(0) == 0 && At(g_data, 0x38) == DENSITY);
    CoXlTestWorldCurrent(1.0f);
    CoXlTestMission(0, 0);
    Records(2);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec4B, 0x4B);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec67, 0x67);
    CHECK(g_saw.creates == 0 && CoXlTestFgTex(0) == 0 && CoXlTestFgTex(3) == 0 && At(g_data, 0x38) == DENSITY && At(g_merge, 0xB8) == ZONES);

    Start("a batch in play: the views around a player, the builder, then everything taken down");
    Kept();
    CoXlTestFgObserver(1, 100.0f, -300.0f);
    CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(g_saw.setTrue == 1 && !CoXlTestFgBusy());
    for (int k = 0; k < 40 && !CoXlTestFgBusy(); k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(CoXlTestFgBusy());
    CHECK(g_saw.published == 4 && g_saw.publishName[0] == 0x3AEAC078u && g_saw.publishTex[0] == DENSITY && g_saw.publishName[3] == 0x2FFE957Du &&
        g_saw.publishTex[3] == ZONES);
    CHECK(g_saw.vps == 1 && g_saw.vpName == 0x5A038527u && g_saw.units == 1 && g_saw.unitHash == 0x465F4895F3DC98D1ull && g_saw.envs == 1 &&
        g_saw.envHash == 0x9F09B0185B47408Full);
    CHECK(g_saw.aims == 1 && Near(g_saw.aimSize, 2048.0f) && Near(g_saw.aimK, 8.0f) && Near(g_saw.farZ, 1002.0f));
    CHECK(g_saw.output[0] == VP && g_saw.output[1] == DENSITY);
    int jobs = (int)CoXlTestFgJobs(), entriesOk = 1, rectsOk = 1, ticks = 0, busyWhileQueued = 1, freeBetween = 0; /* the first job is out */
    CHECK(jobs == 1 && g_saw.boxes == 1);
    /* the first job's own rectangle and box (checked in the loop for the rest) */
    {
        int i = (int)floorf((g_saw.at[0] + 8192.0f) / 256.0f), j = (int)floorf((g_saw.at[1] + 8192.0f) / 256.0f);
        float b[4], r[4];
        CoXlTestFgSub(i, j, 0, b, r);
        CHECK(Near(g_saw.rect[0], r[0]) && Near(g_saw.rect[1], r[1]) && memcmp(g_saw.box, b, sizeof b) == 0);
    }
    while (!(g_saw.generates == 1 && !CoXlTestFgBusy()) && ticks < 4000)
    {
        uint32_t before = Queued();
        CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
        ticks++;
        if (Queued() > before && g_saw.generates == 0)
        {
            const uint8_t* e = g_queue + 0x18 + (size_t)before * 0x1D0;
            uint32_t flags, id;
            memcpy(&flags, e + 0x20, 4); memcpy(&id, e + 0x68, 4);
            entriesOk &= Queued() == 1 && At(e, 0) == WORLD && At(e, 8) == CAMERA && At(e, 0x10) == VP && At(e, 0x18) == ENV && flags == 0x10400u &&
                id == 0xFFFFFFFFu && e[0x58] == 0xAB && e[0x67] == 0xAB;
            int i = (int)floorf((g_saw.at[0] + 8192.0f) / 256.0f), j = (int)floorf((g_saw.at[1] + 8192.0f) / 256.0f);
            float b[4], r[4];
            CoXlTestFgSub(i, j, jobs % 8, b, r); /* a view's eight jobs in order, the camera over its centre */
            rectsOk &= Near(g_saw.rect[0], r[0]) && Near(g_saw.rect[1], r[1]) && Near(g_saw.rect[2], r[2]) && Near(g_saw.rect[3], r[3]) &&
                g_saw.boxes == jobs + 1 && memcmp(g_saw.box, b, sizeof b) == 0 && Near(g_saw.at[0], -8192.0f + (i + 0.5f) * 256) &&
                Near(g_saw.at[1], -8192.0f + (j + 0.5f) * 256) && Near(g_saw.at[2], 501.0f) && g_saw.moveUnit == UNIT;
            jobs++;
        }
        if (Queued() != 0 && !CoXlTestFgBusy()) busyWhileQueued = 0;
        if (Queued() == 0 && !CoXlTestFgBusy() && g_saw.generates == 0) freeBetween++; /* a moving tile may build between jobs */
        if (ticks % 3 == 0 && Queued() == before) Drain(); /* the render callback, not every frame, never with a job just queued */
    }
    const int views = jobs / 8;
    CHECK(jobs % 8 == 0 && views == 8 && (uint32_t)views == CoXlTestFgBaked() && (uint32_t)jobs == CoXlTestFgJobs() && entriesOk && rectsOk && busyWhileQueued &&
        freeBetween >= jobs - 1);
    CHECK(g_saw.generates == 1 && g_saw.generateBehind == 0 && g_saw.generateHash == 0xC4933EE1u && g_saw.generateQueue == (uint64_t)(uintptr_t)g_queue &&
        g_saw.generateWorld == WORLD);
    /* the names go out with each job and come back once it has run: jobs + the builder */
    CHECK(!CoXlTestFgBusy() && g_saw.published == 4 * (jobs + 1) && g_saw.unpublished == 4 * (jobs + 1) && g_saw.vpDestroys == 1 &&
        g_saw.unitDestroys == 1 && g_saw.envDestroys == 1);
    CHECK(g_saw.setTrue >= 2); /* set again while the batch ran */
    /* a full batch: the next is picked at the next update, not 30 later */
    CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(g_saw.vps == 2);
    /* batches go on until the ground within 768 m of the player is baked, then stop while the player stays */
    for (int k = 0; k < 20000; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    {
        int close = 0, closeBaked = 0;
        for (int j = 0; j < 64; j++)
            for (int i = 0; i < 64; i++)
            {
                float cx = -8192.0f + (i + 0.5f) * 256, cy = -8192.0f + (j + 0.5f) * 256;
                float dx = fmaxf(0, fabsf(100.0f - cx) - 128), dy = fmaxf(0, fabsf(-300.0f - cy) - 128);
                if (sqrtf(dx * dx + dy * dy) <= 768.0f) { close++; closeBaked += CoXlTestFgViewBaked(i, j); }
            }
        CHECK(close > 8 && closeBaked == close && !CoXlTestFgBusy() && CoXlTestFgJobs() == 8 * CoXlTestFgBaked());
    }
    const int vps = g_saw.vps, builds = g_saw.generates;
    for (int k = 0; k < 200; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    CHECK(g_saw.vps == vps && g_saw.generates == builds && vps >= 2);
    /* the builder's job still queued when the batch waits to end: the names stay */
    CoXlTestFgObserver(1, 4000.0f, 4000.0f);
    const uint32_t jobsBefore = CoXlTestFgJobs();
    const int published = g_saw.unpublished;
    for (int k = 0; k < 1000 && g_saw.generates < builds + 1; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    const int u = published + 4 * (int)(CoXlTestFgJobs() - jobsBefore);
    CHECK(g_saw.generates == builds + 1 && CoXlTestFgBusy() && g_saw.unpublished == u);
    for (int k = 0; k < 10; k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(CoXlTestFgBusy() && g_saw.unpublished == u);
    Drain();
    for (int k = 0; k < 2; k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(CoXlTestFgBusy());
    CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(!CoXlTestFgBusy() && g_saw.unpublished == u + 4);
    CoXlTestWorldShip();
    CHECK(CoXlTestFgSetting() == 0 && g_saw.setFalse == 1);

    Start("crater cuts: each build keeps every view not baked since the last one, with the game's own slots");
    Kept();
    LinkerRecord(0);
    CoXlTestFgObserver(1, 100.0f, -300.0f);
    for (int k = 0; k < 4000 && g_saw.generates < 1; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    {
        CHECK(g_saw.generates == 1 && g_linkerSends == 1 && LinkerSlotsKept());
        int keptOk = 1, kept = 0;
        for (int j = 0; j < 64; j++)
            for (int i = 0; i < 64; i++) { kept += LinkerKeeps(i, j); keptOk &= LinkerKeeps(i, j) != CoXlTestFgViewBaked(i, j); }
        CHECK(keptOk && kept == 4096 - (int)CoXlTestFgBaked() && fake_host_logged("the scatter lookup builder is the game's own"));
        const uint32_t first = CoXlTestFgBaked();
        for (int k = 0; k < 40; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
        CoXlTestFgObserver(1, 4000.0f, 4000.0f);
        for (int k = 0; k < 4000 && g_saw.generates < 2; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
        kept = 0;
        for (int j = 0; j < 64; j++) for (int i = 0; i < 64; i++) kept += LinkerKeeps(i, j);
        /* the second build rewrites its own views only: the first batch's are kept now */
        CHECK(g_saw.generates == 2 && g_linkerSends == 2 && LinkerSlotsKept() && kept == 4096 - (int)(CoXlTestFgBaked() - first) &&
            LinkerKeeps((int)floorf((100.0f + 8192.0f) / 256.0f), (int)floorf((-300.0f + 8192.0f) / 256.0f)));
    }
    for (int k = 0; k < 20; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    CoXlTestWorldShip();
    CHECK(g_linkerSends == 3 && LinkerSlotsKept() && !LinkerKeeps(0, 0)); /* the game's entries back at the ship */
    CoXlTestFgObserver(0, 0.0f, 0.0f);

    Start("crater cuts: no upload without the level's own linker (not read, or not slot indices)");
    Kept();
    LinkerRecord(0);
    CoXlTestFgObserver(1, 100.0f, -300.0f);
    for (int k = 0; k < 4000 && g_saw.generates < 1; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    for (int k = 0; k < 20; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    CHECK(g_saw.generates == 1 && g_linkerSends == 1);
    /* the next level's op 0x4A gives no slot indices: its own upload stands, the last level's entries are not sent */
    CoXlTestMission((uintptr_t)g_level, 1);
    LinkerRecord(1);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec4B, 0x4B);
    CoXlTestFgKeep((uint64_t)(uintptr_t)g_rec67, 0x67);
    CHECK(g_linkerSends == 1);
    CoXlTestWorldShip();
    CHECK(g_linkerSends == 1);
    CoXlTestFgObserver(0, 0.0f, 0.0f);

    Start("crater cuts: a level whose op 0x4A gave no slot indices gets no upload");
    Kept();
    LinkerRecord(1);
    CoXlTestFgObserver(1, 100.0f, -300.0f);
    for (int k = 0; k < 4000 && g_saw.generates < 1; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    CHECK(g_saw.generates == 1 && g_linkerSends == 0 && fake_host_logged("crater cuts are not kept by this build: op 0x4A's linker was not read this level"));
    CoXlTestWorldShip();
    CHECK(g_linkerSends == 0);
    CoXlTestFgObserver(0, 0.0f, 0.0f);

    Start("a bake view's layer over the node limit: the views so far are built, then no more bakes this level");
    Kept();
    LinkerRecord(0);
    CoXlTestFgObserver(1, 100.0f, -300.0f);
    for (int k = 0; k < 40 && !CoXlTestFgBusy(); k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(CoXlTestFgBusy());
    CoXlTestScatBakeOver(1);
    for (int k = 0; k < 400 && !(g_saw.generates == 1 && !CoXlTestFgBusy()); k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    CHECK(g_saw.generates == 1 && CoXlTestFgBaked() == 1);
    {
        int kept = 0;
        for (int j = 0; j < 64; j++) for (int i = 0; i < 64; i++) kept += LinkerKeeps(i, j);
        CHECK(g_linkerSends == 1 && kept == 4096); /* the over-limit view's texels are kept too */
    }
    CoXlTestFgObserver(1, 4000.0f, 4000.0f);
    for (int k = 0; k < 400; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
    CHECK(g_saw.generates == 1 && g_saw.vps == 1);
    CoXlTestWorldShip();
    CoXlTestFgObserver(0, 0.0f, 0.0f);

    Start("the overlay's setting: only an extra large level in play with the textures kept");
    CoXlTestWorldCurrent(1.0f);
    CoXlTestMission((uintptr_t)g_level, 0);
    Level();
    for (int k = 0; k < 100; k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(g_saw.setTrue == 0 && g_saw.setFalse == 0 && g_saw.vps == 0);
    Kept();
    for (int k = 0; k < 130; k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(g_saw.setTrue == 3 && CoXlTestFgSetting() == 1); /* once, then every 60 level updates */
    CoXlTestFgTick(0); /* another level's update: the overlay's variant is the game's there */
    CHECK(g_saw.setFalse == 1 && CoXlTestFgSetting() == 0);

    Start("a batch's next view waits while a moving tile builds, and outside play");
    {
        Kept();
        CoXlTestFgObserver(1, 100.0f, -300.0f);
        for (int k = 0; k < 40 && !CoXlTestFgBusy(); k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
        CHECK(CoXlTestFgBusy() && Queued() == 1);
        Drain();
        for (int k = 0; k < 3; k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
        CHECK(!CoXlTestFgBusy() && CoXlTestFgBaked() == 1); /* the first view ran; the names are back */
        CoXlTestMover(0, 0, 1, 0, 0, 0); /* a tile build starts */
        for (int k = 0; k < 50; k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
        CHECK(!CoXlTestFgBusy() && Queued() == 0 && CoXlTestFgBaked() == 1);
        CoXlTestMover(0, 0, 0, 0, 0, 0);
        uint32_t three = 2; memcpy(g_level + 0x1E4, &three, 4); /* the level leaves play (state 2) */
        for (int k = 0; k < 50; k++) CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
        CHECK(Queued() == 0 && CoXlTestFgBaked() == 1 && g_saw.setFalse >= 1 && CoXlTestFgSetting() == 0);
        three = 3; memcpy(g_level + 0x1E4, &three, 4);
        CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
        CHECK(CoXlTestFgBusy() && Queued() == 1 && CoXlTestFgBaked() == 1 && CoXlTestFgJobs() == 2 && CoXlTestFgSetting() == 1); /* the view's next job */
        CoXlTestWorldShip();
    }

    static const char* const kRefusals[] = {"the overlay variants are the game's own (no build-data): their debug tint is still in",
        "an overlay variant is not loaded", "the overlay variants are the game's own (no build-data): they map 2048 m",
        "an overlay variant's stage is not where the edit put it"};
    for (int r = 0; r < 4; r++)
    {
        Start("no setting and no bakes unless the edited overlay variants are loaded");
        Kept();
        if (r == 0) Libraries(0); /* a DLL copied without build-data: the game's own variants */
        if (r == 1) g_libLoaded[1] = 0;
        if (r == 2) memset(g_lib[0] + 0x7DE0 + 0x1894, 0, 8); /* the tint gone, the 2048 m mapping kept */
        if (r == 3) memset(g_lib[1] + 0x44F8 + 24, 0, 4);
        CoXlTestFgObserver(1, 100.0f, -300.0f);
        for (int k = 0; k < 200; k++) { Drain(); CoXlTestFgTick((uint64_t)(uintptr_t)g_level); }
        CHECK(g_saw.setTrue == 0 && CoXlTestFgSetting() == 0 && g_saw.vps == 0 && CoXlTestFgBaked() == 0);
        char line[200];
        snprintf(line, sizeof line, "far grass: off, debug_deformation_prevention stays false: %s", kRefusals[r]);
        CHECK(fake_host_logged(line));
        CoXlTestWorldShip();
        CHECK(g_saw.setFalse == 1);
    }
    Start("the edited variants loaded: the setting goes on, each change logged");
    CoXlTestFgNotesReset();
    Kept();
    CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CHECK(g_saw.setTrue == 1 && fake_host_logged("the edited overlay variants are loaded") &&
        fake_host_logged("debug_deformation_prevention true (in play on an extra large level)"));
    CoXlTestFgTick(0);
    CHECK(g_saw.setFalse == 1 && fake_host_logged("debug_deformation_prevention false (not in play on an extra large level)"));
    CoXlTestWorldShip();
    CHECK(g_saw.setFalse == 2 && fake_host_logged("debug_deformation_prevention false (a level init or the ship)"));
    Start("a profile save or the renderer's fill never finds debug_deformation_prevention true in the settings document");
    CHECK(DocKey(0x101u, 1) == (0x101 << 8)); /* a true bool: false, its other bits kept */
    CHECK(fake_host_logged("debug_deformation_prevention was true before a test save; it is false now"));
    CHECK(DocKey(0x1u, 0) == (0x1 << 8) && DocKey(0x0u, 0) == (0x1 << 8)); /* false stays; a missing key becomes false */
    CHECK(fake_host_logged("was missing before a test save"));
    CHECK(DocKey(0x2u, 7) == (0x2 << 8 | 7) && DocKey(0x6u, 1) == (0x6 << 8 | 1)); /* not a bool: left */
    Start("a clear that changes nothing is set but not logged: the line cap lasts a long session");
    CoXlTestWorldShip();
    CHECK(g_saw.setFalse == 1 && !fake_host_logged("debug_deformation_prevention"));
    Kept();
    CoXlTestFgTick((uint64_t)(uintptr_t)g_level);
    CoXlTestWorldShip();
    CHECK(g_saw.setTrue == 1 && g_saw.setFalse == 2 && fake_host_logged("debug_deformation_prevention false (the level is left)"));

    Start("a moving tile's build waits while a batch runs");
    {
        static const char kStart[] = "the level's context did not read; no tile"; /* how far a build start gets on the fake game */
        CoXlTestMission((uintptr_t)g_level, 1);
        Level();
        CoXlTestMover(0, 0, 0, 0, 0, 0);
        CoXlTestFgRing(-2, 2, -2, 2);
        CoXlTestFgState(2); /* a view's job is out */
        CoXlTestFgOut(1);
        CoXlTestMoverTick((uintptr_t)g_level, 2048.4f, 0.0f);
        CHECK(!fake_host_logged(kStart));
        CoXlTestFgState(1); /* between views: the tile builds */
        CoXlTestFgOut(0);
        CoXlTestMoverTick((uintptr_t)g_level, 2048.4f, 0.0f);
        CHECK(fake_host_logged(kStart));
        CoXlTestFgState(0);
        CoXlTestFgRing(0, 0, 0, 0);
    }

    g_case = "the bake's view in the scatter update";
    {
        uint8_t view[0x300] = {0};
        float n = 1.0f, f = 1002.0f; uint32_t ortho = 0, persp = 1;
        memcpy(view + 0x20, &n, 4); memcpy(view + 0x24, &f, 4); memcpy(view + 0x28, &ortho, 4);
        CoXlTestMission(0, 1);
        CoXlTestFgWorld(8.0f);
        CHECK(CoXlTestFgBakeView(view) == 1);
        memcpy(view + 0x28, &persp, 4);
        CHECK(CoXlTestFgBakeView(view) == 0);
        memcpy(view + 0x28, &ortho, 4); f = 1001.0f; memcpy(view + 0x24, &f, 4);
        CHECK(CoXlTestFgBakeView(view) == 0); /* the load-time cameras the scatter cuts look for */
        f = 1002.0f; memcpy(view + 0x24, &f, 4);
        CoXlTestMission(0, 0);
        CHECK(CoXlTestFgBakeView(view) == 0);
    }

    g_case = "the scatter update for a bake view gives every node its ground";
    {
        uint8_t view[0x300] = {0};
        float n = 1.0f, f = 1002.0f, aspect = 1.0f;
        memcpy(view + 0x20, &n, 4); memcpy(view + 0x24, &f, 4); memcpy(view + 0x278, &aspect, 4);
        CoXlTestWorldCurrent(8.0f);
        CoXlTestMission(0, 1);
        CoXlTestFgWorld(8.0f);
        CHECK(CoXlTestScatterUpdate(0x22, 500, view) == 1 && CoXlTestScatterBake() == 1); /* not cut: run with the mark */
        f = 400.0f; memcpy(view + 0x24, &f, 4);
        CHECK(CoXlTestScatterUpdate(0x22, 500, view) == 1 && CoXlTestScatterBake() == 0);
        int budget = 0; uint64_t most = 0;
        CoXlTestScatLimits(1, &budget, &most);
        CHECK(budget == 4096 && most == 4096);
        CoXlTestScatLimits(0, &budget, &most);
        CHECK(budget == 256 && most == 1024);
    }

    g_case = "a layer's scatter update runs under the layer's lock in play, and a wait is counted";
    {
        static uint8_t layers[4 * 0x238];
        uint8_t bake[0x300] = {0}, other[0x300] = {0};
        float n = 1.0f, f = 1002.0f, aspect = 1.0f, near2 = 0.1f, far2 = 400.0f;
        memcpy(bake + 0x20, &n, 4); memcpy(bake + 0x24, &f, 4); memcpy(bake + 0x278, &aspect, 4);
        memcpy(other + 0x20, &near2, 4); memcpy(other + 0x24, &far2, 4);
        CoXlTestWorldCurrent(8.0f);
        CoXlTestMission(0, 1);
        CoXlTestFgWorld(8.0f);
        LONG64 b0, o0, b1, o1;
        int locked = 0;
        CoXlTestScatterCounts(&b0, &o0);
        CHECK(CoXlTestScatterLayer(bake, layers + 0x238, &locked) == 1 && locked == 1);
        CHECK(CoXlTestScatterLayer(other, layers + 2 * 0x238, &locked) == 1 && locked == 1);
        CoXlTestScatterCounts(&b1, &o1);
        CHECK(b1 - b0 == 1 && o1 == o0); /* free locks: no wait */
        HANDLE t = CreateThread(NULL, 0, HoldLayer, layers + 0x238, 0, NULL); /* another job holds layer 1 for 100 ms */
        while (!g_holding) Sleep(1);
        ULONGLONG t0 = GetTickCount64();
        CHECK(CoXlTestScatterLayer(other, layers + 0x238, &locked) == 1 && locked == 1);
        CHECK(GetTickCount64() - t0 >= 50); /* it waited for the holder */
        WaitForSingleObject(t, INFINITE); CloseHandle(t);
        CoXlTestScatterCounts(&b0, &o0);
        CHECK(b0 == b1 && o0 - o1 == 1);
        CoXlTestFgWorld(1.0f); /* a normal level: no lock */
        CHECK(CoXlTestScatterLayer(other, layers + 0x238, &locked) == 1 && locked == 0);
        CoXlTestFgWorld(8.0f);
    }

    g_case = "a job goes out only after a frame as quick as the recent ones without a job, never by force";
    {
        uint32_t deferred = 0;
        CHECK(CoXlTestFgPaced(10.0, 12.0f, &deferred) == 1 && deferred == 0); /* 12.5 + 1 ms allowed */
        CHECK(CoXlTestFgPaced(10.0, 13.4f, &deferred) == 1);
        CHECK(CoXlTestFgPaced(10.0, 14.0f, &deferred) == 0 && deferred == 1); /* past 12.5 + 1 ms: held */
        CHECK(CoXlTestFgPaced(10.0, 12.0f, &deferred) == 1 && deferred == 0);
        int out = 0;
        for (int k = 0; k < 300; k++) out += CoXlTestFgPaced(10.0, 30.0f, &deferred);
        CHECK(out == 0 && deferred == 300);
        CHECK(CoXlTestFgPaced(24.0, 30.0f, &deferred) == 1 && deferred == 0); /* the load stayed: the mean follows */
        CHECK(CoXlTestFgPaced(0.0, 30.0f, &deferred) == 1); /* no frames before: no pacing */
    }

    g_case = "the frames before a batch: the worst and the mean of the last 128";
    {
        float ms[200];
        double worst = 0, mean = 0;
        for (int k = 0; k < 200; k++) ms[k] = 10.0f;
        ms[10] = 90.0f; /* pushed out of the ring by the 128 after it */
        ms[150] = 40.0f;
        CoXlTestFgFrames(ms, 200, &worst, &mean);
        CHECK(fabs(worst - 40.0) < 1e-6 && fabs(mean - (127 * 10.0 + 40.0) / 128.0) < 1e-6);
        CoXlTestFgFrames(ms, 3, &worst, &mean);
        CHECK(fabs(worst - 10.0) < 1e-6 && fabs(mean - 10.0) < 1e-6);
        CoXlTestFgFrames(ms, 0, &worst, &mean);
        CHECK(worst == 0 && mean == 0);
    }

    if (g_failed) { printf("fargrass: %d check(s) failed\n", g_failed); return 1; }
    printf("fargrass: all checks passed\n");
    return 0;
}
