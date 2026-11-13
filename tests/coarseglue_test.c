/* The coarse terrain's draw glue (proto/difficulty_world_terrain.c CoarseDraw, CoarseCall, CoarseDrawn, CoarseTake,
 * CoarseRestore, CoarseDone, the clip) on a fake game: the dispatcher acts as case 0x3C with 0xF499B0 does (the
 * first call draws and stays, a call at phase 2 only appends, the generation queue under its ticket lock), the gui
 * triangle call is the api table's slot TerrainTri hooks, and a drain between frames renders every entry. Every
 * tile is drawn once, every triangle rendered once, the queue never holds two entries, a tile that cannot be clipped
 * never shares a frame, a foreign entry stops the batch, and a fault ends the section. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_coarse.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestCoarseSection(const int* list, int count, int columns, int batch, void* dispatch);
int CoXlTestCoarseFrame(uint64_t context, uintptr_t gen);
const CoCoarseBatch* CoXlTestCoarseBatch(int* drawn, int* failed);
const int* CoXlTestCoarseList(void);
int CoXlTestCoarseFree(int k);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define COLUMNS 24
#define TILES 20
#define API 0x3700000u   /* free room in the fake image: the api, its +0xD0 table */
#define TABLE 0x3700100u
#define ARENA_RESET 0x173BAB0u
#define ADD 0x13DD2DCAD2968560ull
#define CAP 0x369E29988294A16Full

typedef void (*TriFn)(uint64_t gui, const float* p0, const float* p1, const float* p2, uint32_t layer, const float* colour,
    uint64_t material, const float* uv0, const float* uv1, const float* uv2);

static struct
{
    uint8_t* image; uint8_t* gen; uint8_t* R; uint8_t* ctx; uint8_t* settings; uint8_t* queue;
    int tile;                         /* the tile the dispatcher draws now */
    int split[TILES * 4], faultTile, foreignTile, clipOffAt;  /* clipOffAt: the clip fails from this call on */
    int frame, calls, frameTiles[64], frameCount, frameUnclipped, aloneBad, touched, twice, queuedMax;
    int gui[4096], guiCount, emits[TILES * 4], renders[TILES * 4];
    volatile LONG resets;
} G;

static int Touch(int a, int b) { return a != b && abs(a % COLUMNS - b % COLUMNS) <= 1 && abs(a / COLUMNS - b / COLUMNS) <= 1; }
static int Clipped(void) { float s; memcpy(&s, G.gen + 0x8758A8u + 0x120u, 4); return s >= 1024.0f; }

static uint64_t Material(uint64_t gui, uint64_t hash) { return hash; }

/* The gui triangle call behind TerrainTri: one emission of the tile the dispatcher draws. */
static void Tri(uint64_t gui, const float* p0, const float* p1, const float* p2, uint32_t layer, const float* colour, uint64_t material,
    const float* uv0, const float* uv1, const float* uv2)
{
    if (G.guiCount < (int)(sizeof G.gui / sizeof G.gui[0])) G.gui[G.guiCount++] = G.tile;
    G.emits[G.tile]++;
}

static void Append(uint64_t context, uint32_t id)
{
    uintptr_t q = (uintptr_t)G.queue;
    LONG64 mine = InterlockedExchangeAdd64((volatile LONG64*)q, 1);
    while (*(volatile LONG64*)(q + 8) != mine) YieldProcessor();
    uint32_t n = *(volatile uint32_t*)(q + 0x10);
    uint8_t* e = (uint8_t*)(q + 0x18 + (uintptr_t)n * CO_COARSE_ENTRY);
    *(volatile uint32_t*)(q + 0x10) = n + 1;
    if (n + 1 > (uint32_t)G.queuedMax) G.queuedMax = (int)n + 1;
    memcpy(e + 0x00, (void*)(uintptr_t)(context + 0x40), 8);
    memcpy(e + 0x08, G.R, 8); memcpy(e + 0x10, G.R + 0x28, 8); memcpy(e + 0x18, G.R + 0x108, 8);
    memset(e + 0x20, 0, 4); e[0x24] = 0; memset(e + 0x38, 0, 2);
    memcpy(e + 0x58, G.settings + 0x74, 16);
    memcpy(e + 0x68, &id, 4);
    InterlockedExchangeAdd64((volatile LONG64*)(q + 8), 1);
}

/* Case 0x3C (0xC16257) around 0xF499B0: phase byte 0, the first call, stays; later calls set it to (pass phase < 2) and
 * move the index on 0. A pass below phase 2 draws (two calls for a split tile), at phase 2 only the append runs. */
static uint64_t Dispatch(uint64_t record, uint64_t context, uint64_t index, uint64_t d, uint64_t phase, uint64_t f, uint64_t g, uint64_t h)
{
    uint8_t* rec = (uint8_t*)(uintptr_t)record;
    uint8_t* byte = (uint8_t*)(uintptr_t)phase;
    int tile; memcpy(&tile, rec + 0x18F0, 4);
    uint32_t pass; memcpy(&pass, rec + 8 + 0x108, 4);
    int first = *byte == 0;
    G.calls++;
    if (tile == G.faultTile) *(volatile int*)(uintptr_t)8 = 1;
    if (pass < 2)
    {
        G.tile = tile;
        TriFn tri; memcpy(&tri, G.image + TABLE, 8);
        float px = 4096.0f / 16384.0f, cx = ((tile % COLUMNS) * 704.0f + 352.0f) * px, cy = ((tile / COLUMNS) * 704.0f + 352.0f) * px;
        float a[3] = {cx - 20, cy - 20, 5}, b[3] = {cx + 20, cy - 20, 5}, c[3] = {cx, cy + 20, 5}, colour[4] = {255, 0, 0, 0};
        int before = G.emits[tile];
        tri(0x5000, a, b, c, pass == 0 ? 0 : 0x64, colour, Material(0x5000, ADD), NULL, NULL, NULL);
        tri(0x5000, a, b, c, 0x64, colour, Material(0x5000, CAP), NULL, NULL, NULL);
        if (G.emits[tile] > before)
        {
            for (int i = 0; i < G.frameCount; i++) if (Touch(G.frameTiles[i], tile)) G.touched++;
            if ((!Clipped() && G.frameCount > 0) || G.frameUnclipped) G.aloneBad++;
            if (!Clipped()) G.frameUnclipped = 1;
            if (G.frameCount < 64) G.frameTiles[G.frameCount++] = tile;
        }
        pass = G.split[tile] && pass == 0 ? 1 : 2;
        memcpy(rec + 8 + 0x108, &pass, 4);
    }
    Append(context, tile == G.foreignTile ? 7u : 0xFFFFFFFFu);
    if (G.clipOffAt > 0 && G.calls == G.clipOffAt) memset(G.gen + 0x8758A8u + 0x120u, 0, 4);
    if (first) *byte = 1;
    else if (pass >= 2) { *byte = 0; (*(volatile uint32_t*)(uintptr_t)index)++; }
    else *byte = 1;
    return 0;
}

/* The render callback's drain (0xAB0590): every entry renders the gui put there since the last render. */
static void Drain(void)
{
    uint32_t n; memcpy(&n, G.queue + 0x10, 4);
    if (n > 1 && G.guiCount > 0) G.twice++;
    if (n > 0) { for (int i = 0; i < G.guiCount; i++) G.renders[G.gui[i]] += (int)n; G.guiCount = 0; }
    memset(G.queue + 0x10, 0, 4);
    G.frameCount = 0; G.frameUnclipped = 0; G.frame++;
}

static int g_tiles[TILES];
static void Setup(const char* name, int batch)
{
    static int made;
    g_case = name;
    if (!made)
    {
        g_coApi = fake_host_reset(CO_GAME_BUILD);
        g_coMod = (HfMod*)&g_fake;
        G.image = g_fake.image;
        CoXlTestGame((uintptr_t)G.image);
        uint64_t api = (uint64_t)(uintptr_t)(G.image + API), table = (uint64_t)(uintptr_t)(G.image + TABLE);
        memcpy(G.image + 0x3326308u, &api, 8);
        memcpy(G.image + API + 0xD0, &table, 8);
        TriFn tri = Tri; memcpy(G.image + TABLE, &tri, 8);
        uint64_t (*mat)(uint64_t, uint64_t) = Material; memcpy(G.image + TABLE + 0x230, &mat, 8);
        static const uint8_t reset[] = {0xFF, 0x01, 0xC3}; /* inc dword [rcx]; ret: the arena reset, counted */
        memcpy(G.image + ARENA_RESET, reset, sizeof reset);
        DWORD old; VirtualProtect(G.image + ARENA_RESET, sizeof reset, PAGE_EXECUTE_READWRITE, &old);
        G.gen = VirtualAlloc(NULL, 0x8758A8u + 0x1000u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        G.R = calloc(1, 0x200); G.ctx = calloc(1, 0x100); G.settings = calloc(1, 0x100);
        G.queue = calloc(1, 0x18 + CO_COARSE_ENTRIES * CO_COARSE_ENTRY);
        made = 1;
    }
    uintptr_t R = (uintptr_t)G.R, gui = 0x5000, world = 0xABCD00, camera = 0xCA000, generator = 0x6E0000, r108 = 0x108108;
    uintptr_t settings = (uintptr_t)G.settings, queue = (uintptr_t)G.queue, arena = (uintptr_t)&G.resets;
    memcpy(G.gen, &R, 8);
    memcpy(G.R, &camera, 8); memcpy(G.R + 0x28, &generator, 8); memcpy(G.R + 0x108, &r108, 8); memcpy(G.R + 0x110, &gui, 8);
    float samples = 4096.0f; memcpy(G.gen + 0x8758A8u + 0x120u, &samples, 4);
    memcpy(G.ctx + 0x00, &arena, 8); memcpy(G.ctx + 0x08, &settings, 8); memcpy(G.ctx + 0x40, &world, 8); memcpy(G.ctx + 0x50, &queue, 8);
    for (int i = 0; i < 16; i++) G.settings[0x74 + i] = (uint8_t)(0x40 + i);
    memset(G.queue, 0, 0x18 + CO_COARSE_ENTRIES * CO_COARSE_ENTRY);
    memset(G.split, 0, sizeof G.split); memset(G.emits, 0, sizeof G.emits); memset(G.renders, 0, sizeof G.renders);
    G.faultTile = G.foreignTile = -1; G.clipOffAt = 0;
    G.frame = G.calls = G.frameCount = G.frameUnclipped = G.aloneBad = G.touched = G.twice = G.queuedMax = G.guiCount = 0;
    G.resets = 0;
    /* rows 0 and 1, columns 0..9: steps 0..11, two tiles a step from step 2 on */
    for (int k = 0; k < TILES; k++) g_tiles[k] = (k / 10) * COLUMNS + k % 10;
    CoXlTestCoarseSection(g_tiles, TILES, COLUMNS, batch, (void*)Dispatch);
}

static int Run(int frames)
{
    int r = 1;
    for (int f = 0; f < frames && r > 0; f++) { r = CoXlTestCoarseFrame((uint64_t)(uintptr_t)G.ctx, (uintptr_t)G.gen); Drain(); }
    return r;
}

static void Once(void)
{
    int rendered = 1, freed = 1;
    for (int k = 0; k < TILES; k++)
    {
        int t = g_tiles[k];
        if (G.emits[t] == 0 || G.renders[t] != G.emits[t]) rendered = 0;
        if (!CoXlTestCoarseFree(k)) freed = 0;
    }
    CHECK(rendered);
    CHECK(freed);
    CHECK(G.twice == 0 && G.queuedMax == 1 && G.touched == 0);
}

int main(void)
{
    int drawn, failed;
    const CoCoarseBatch* b;

    Setup("one tile a frame", 1);
    CHECK(Run(1000) == 0);
    b = CoXlTestCoarseBatch(&drawn, &failed);
    Once();
    CHECK(drawn == TILES && failed == 0 && b->frames == 2 * TILES && b->taken == 0 && G.calls == 2 * TILES && G.resets == TILES);
    CHECK(memcmp(CoXlTestCoarseList(), g_tiles, sizeof g_tiles) == 0); /* the tile order */

    Setup("four a frame", CO_COARSE_BATCH);
    CHECK(Run(1000) == 0);
    b = CoXlTestCoarseBatch(&drawn, &failed);
    Once();
    /* steps 0 and 1 one tile, 2..9 two, 10 and 11 one: 12 groups, the next group's drawing calls in each moving frame */
    CHECK(drawn == TILES && failed == 0 && b->groups == 12 && b->frames == 13 && b->sharedFrames == 11 && G.resets == 12);
    CHECK(b->refused == 0 && b->unclipped == 0 && b->restored == 0 && b->tilesFrameMax == 2);
    printf("four a frame: %ld draw frames, %ld groups, %ld entries taken off\n", b->frames, b->groups, b->taken);

    Setup("a first call that did not finish", CO_COARSE_BATCH);
    G.split[g_tiles[4]] = 1; G.split[g_tiles[15]] = 1;
    CHECK(Run(1000) == 0);
    b = CoXlTestCoarseBatch(&drawn, &failed);
    Once();
    CHECK(drawn == TILES && failed == 0);

    /* after every call of the first frames: between two calls of one frame too */
    for (int at = 1; at < 12; at++)
    {
        Setup("the clip fails inside the section", CO_COARSE_BATCH);
        G.clipOffAt = at;
        CHECK(Run(1000) == 0);
        b = CoXlTestCoarseBatch(&drawn, &failed);
        Once();
        CHECK(drawn == TILES && G.aloneBad == 0 && b->batch == 1 && b->unclipped >= 1);
    }

    Setup("an entry of another form", CO_COARSE_BATCH);
    G.foreignTile = g_tiles[3];
    CHECK(Run(1000) == 0);
    b = CoXlTestCoarseBatch(&drawn, &failed);
    Once();
    CHECK(drawn == TILES && b->refused == 1 && b->batch == 1);

    for (int at = 2; at < TILES; at += 5)
    {
        Setup("a draw that faults", CO_COARSE_BATCH);
        G.faultTile = g_tiles[at];
        CHECK(Run(1000) == -1);
        b = CoXlTestCoarseBatch(&drawn, &failed);
        int rendered = 1;
        for (int t = 0; t < TILES * 4; t++) if (G.renders[t] != G.emits[t]) rendered = 0;
        CHECK(rendered && G.twice == 0 && G.queuedMax == 1 && failed == 1 && b->refused == 0 && drawn < TILES);
    }

    if (g_failed) { printf("coarseglue: %d checks failed\n", g_failed); return 1; }
    printf("coarseglue: all checks passed\n");
    return 0;
}
