/* The tile's collision shape on a worker (proto/difficulty_world_terrain.c NearShapeBegin / NearShapeEnd): the ready
 * frame held the game thread 136-140 ms in SpawnTerrainWork (exe 0x1AC210). Its steps are the function's own in its
 * order: under the terrain lock the object filled from the entry, the shape build (0x786130) alone as an engine job,
 * then under the lock again the shape into the entry and the quadtree (0x2F2CB0) with the function's leaf and depth.
 * Fakes stand at the exe's addresses; the test runs the job when it chooses. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestShapeExe(uintptr_t exe);
int CoXlTestShapeBegin(uint64_t world, uint64_t handle);
void CoXlTestShapeEnd(void);
int CoXlTestShapeState(void);
void CoXlTestShapeJoin(void);
void CoXlTestShapeLevelLeft(void);
void CoXlTestMoveHold(int on);
LONG64 CoXlTestMoveHoldSince(void);
void CoXlTestNearJobLevelUpdate(uintptr_t level, uintptr_t moving);
int CoXlTestReadyTile(uint64_t context, uint64_t handle, int* state);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t* g_exe;
static int g_depth, g_maxDepth, g_lockedFind, g_lockedQuad, g_shapeLocked, g_works, g_finals;
static uint64_t g_world, g_handle, g_shapeObj, g_shapeWorld;
static uint8_t g_entry[0x200], g_obj[0x200], g_dims[0x20], g_treeHolder[0x40];
static uint64_t g_heights = 0x7700;
static struct { uint64_t tree, heights, cols, rows, a5, a6, a7, a8, a9, depth, a11, a12; int calls; } g_quad;
static struct { void* fn; uint64_t* data; uint32_t queue, priority, count; int calls; } g_submit;

static void Enter(uint64_t lock) { if (lock == g_world + 0x230520u) { g_depth++; if (g_depth > g_maxDepth) g_maxDepth = g_depth; } }
static void Leave(uint64_t lock) { if (lock == g_world + 0x230520u) g_depth--; }
static uint8_t* Find(uint64_t table, uint64_t* key) { g_lockedFind += g_depth == 1; return table == g_world + 0x230550u && *key == g_handle ? g_entry : NULL; }
static uint64_t Hash(uint64_t text, uint64_t length, uint64_t seed)
{
    uint64_t h = 1469598103934665603ull ^ seed;
    for (uint64_t k = 0; k < length; k++) h = (h ^ ((const uint8_t*)(uintptr_t)text)[k]) * 1099511628211ull;
    return h;
}
static uint64_t Shape(uint64_t world, uint64_t obj) { g_shapeWorld = world; g_shapeObj = obj; g_shapeLocked = g_depth; return 0x5A5A0001ull; }
static float Log(float v) { return logf(v); }
static uint64_t Quad(uint64_t tree, uint64_t heights, uint64_t cols, uint64_t rows, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9,
    uint64_t depth, uint64_t a11, uint64_t a12)
{
    g_quad.tree = tree; g_quad.heights = heights; g_quad.cols = cols; g_quad.rows = rows; g_quad.a5 = a5; g_quad.a6 = a6; g_quad.a7 = a7; g_quad.a8 = a8;
    g_quad.a9 = a9; g_quad.depth = depth; g_quad.a11 = a11; g_quad.a12 = a12; g_quad.calls++; g_lockedQuad = g_depth;
    return 0;
}
static uint64_t Submit(void* fn, uint64_t* data, const char* name, uint32_t queue, uint32_t priority, uint32_t count, uint64_t zero)
{
    (void)name; (void)zero;
    g_submit.fn = fn; g_submit.data = data; g_submit.queue = queue; g_submit.priority = priority; g_submit.count = count; g_submit.calls++;
    return 0;
}
static void RunJob(void) { if (g_submit.fn) ((uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t))g_submit.fn)(0, 0, 0, *g_submit.data); g_submit.fn = NULL; }
static char Ready(uint64_t world, uint64_t handle) { (void)world; (void)handle; return 1; }
static void Work(uint64_t world, uint64_t handle) { (void)world; (void)handle; g_works++; }
static char Finalize(uint64_t pass, uint64_t unused, uint64_t context) { (void)pass; (void)unused; (void)context; g_finals++; return 1; }

/* mov rax, imm64; jmp rax */
static void Stub(uint8_t* at, void* fn)
{
    uint64_t to = (uint64_t)(uintptr_t)fn;
    at[0] = 0x48; at[1] = 0xB8; memcpy(at + 2, &to, 8); at[10] = 0xFF; at[11] = 0xE0;
}

static void Entry(uint32_t cols, uint32_t rows)
{
    memset(g_entry, 0, sizeof g_entry); memset(g_obj, 0, sizeof g_obj);
    uint64_t obj = (uint64_t)(uintptr_t)g_obj, dims = (uint64_t)(uintptr_t)g_dims, tree = (uint64_t)(uintptr_t)g_treeHolder;
    memcpy(g_entry + 0x10, &obj, 8); memcpy(g_entry + 0x38, &dims, 8); memcpy(g_entry + 0x00, &tree, 8);
    memcpy(g_dims + 0x14, &cols, 4); memcpy(g_dims + 0x18, &rows, 4);
    const float corner[4] = {1296.0f, -1152.0f, -500.0f, 500.0f};
    memcpy(g_entry + 0x24, corner, 16);
    memcpy(g_obj + 0x40, &g_heights, 8);
    for (int k = 0x60; k < 0x168; k++) g_obj[k] = (uint8_t)(k * 7 + 3);
    memset(&g_quad, 0, sizeof g_quad);
}

static int Leaf(uint32_t rows)
{
    uint32_t leaf = rows;
    if (rows >= 32)
    {
        if ((rows & (rows - 1)) == 0) leaf = 32;
        else if (!(rows & 1)) do { if (leaf <= 32) break; leaf >>= 1; } while (!(leaf & 1));
    }
    return (int)leaf;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* image = g_fake.image;
    CoXlTestGame((uintptr_t)image);
    g_exe = fake_host_exe();
    CoXlTestShapeExe((uintptr_t)g_exe);
    static uint8_t world[0x231000];
    g_world = (uint64_t)(uintptr_t)world; g_handle = 0xABCD0001ull;
    /* the exe: IAT lock entries, code stubs, strings, the log's constant, the physics world list */
    void (*enter)(uint64_t) = Enter, (*leave)(uint64_t) = Leave;
    memcpy(g_exe + 0x13F66B8u, &enter, 8); memcpy(g_exe + 0x13F66C0u, &leave, 8);
    Stub(g_exe + 0x1B86C0u, (void*)Find); Stub(g_exe + 0x5D5840u, (void*)Hash); Stub(g_exe + 0x786130u, (void*)Shape);
    Stub(g_exe + 0x12CCB10u, (void*)Log); Stub(g_exe + 0x2F2CB0u, (void*)Quad);
    DWORD old = 0;
    const uint32_t code[] = {0x1B8000u, 0x5D5000u, 0x786000u, 0x12CC000u, 0x2F2000u};
    for (int k = 0; k < 5; k++) VirtualProtect(g_exe + code[k], 0x2000, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_exe + 0x167193Cu, "static", 6); memcpy(g_exe + 0x16719A8u, "terrain", 7);
    const float two = 2.0f; memcpy(g_exe + 0x16A9AE0u, &two, 4);
    uint64_t list[4] = {0x1111, g_world, 0, 0}; memcpy(g_exe + 0x27BAB30u, list, sizeof list);
    /* the game: the job submit [[game+0x33263E8]+0x88] */
    static uint8_t jobs[0x100];
    uint64_t jobsAt = (uint64_t)(uintptr_t)jobs; memcpy(image + 0x33263E8u, &jobsAt, 8);
    uint64_t (*submit)(void*, uint64_t*, const char*, uint32_t, uint32_t, uint32_t, uint64_t) = Submit; memcpy(jobs + 0x88, &submit, 8);

    CoXlTestMoveHold(0);
    g_case = "begin: under the lock the object filled from the entry as 0x1AC343..0x1AC4C1 does; the shape a job on channel 10";
    Entry(4096, 4096);
    CHECK(CoXlTestShapeBegin(g_world, g_handle) == 1);
    CHECK(g_depth == 0 && g_maxDepth == 1 && g_lockedFind == 1);
    CHECK(memcmp(g_obj + 0x168, g_entry + 0x24, 16) == 0);
    { uint32_t v; memcpy(&v, g_obj + 0x17C, 4); CHECK(v == 1); memcpy(&v, g_obj + 0x178, 4); CHECK(v == 0x80);
      memcpy(&v, g_obj + 0x50, 4); CHECK(v == 4096); memcpy(&v, g_obj + 0x54, 4); CHECK(v == 4096);
      memcpy(&v, g_obj + 0x58, 4); CHECK(v == (uint32_t)(Hash((uint64_t)(uintptr_t)"static", 6, 0) >> 32));
      memcpy(&v, g_obj + 0x5C, 4); CHECK(v == (uint32_t)(Hash((uint64_t)(uintptr_t)"terrain", 7, 0) >> 32)); }
    CHECK(memcmp(g_entry + 0x58, g_obj + 0x60, 0x108) == 0);
    CHECK(g_submit.calls == 1 && g_submit.queue == 1 && g_submit.priority == 3 && g_submit.count == 1);
    CHECK(CoXlTestShapeState() == 3); /* on, running */
    CHECK(g_quad.calls == 0);
    CHECK(CoXlTestMoveHoldSince() != 0); /* craters held while the shape reads the heights */

    g_case = "the job: the shape built off the lock from the world and the object";
    RunJob();
    CHECK(g_shapeWorld == g_world && g_shapeObj == (uint64_t)(uintptr_t)g_obj && g_shapeLocked == 0);
    CHECK(CoXlTestShapeState() == 1);

    g_case = "end: under the lock the shape into the entry, then the quadtree with the function's arguments";
    g_maxDepth = 0; g_lockedFind = 0;
    CoXlTestShapeEnd();
    CHECK(g_depth == 0 && g_maxDepth == 1 && g_lockedFind == 1 && g_lockedQuad == 1);
    { uint64_t s; memcpy(&s, g_entry + 0x18, 8); CHECK(s == 0x5A5A0001ull); }
    CHECK(g_quad.calls == 1 && g_quad.tree == (uint64_t)(uintptr_t)g_treeHolder + 0x38 && g_quad.heights == g_heights);
    CHECK(g_quad.cols == 4096 && g_quad.rows == 4096 && g_quad.a5 == 0 && g_quad.a9 == 0 && g_quad.a11 == 0 && g_quad.a12 == 0);
    { uint64_t xy0, xy1; memcpy(&xy0, g_entry + 0x24, 8); memcpy(&xy1, g_entry + 0x2C, 8); CHECK(g_quad.a6 == xy0 && g_quad.a7 == xy0 && g_quad.a8 == xy1); }
    CHECK(g_quad.depth == (uint64_t)(int64_t)(logf(128.0f) / logf(2.0f)));
    CHECK(CoXlTestShapeState() == 0 && CoXlTestMoveHoldSince() == 0);
    CHECK(fake_host_logged("the tile's collision shape built on a worker"));

    g_case = "the leaf from the rows (0x1AC4DA..0x1AC50A): under 32 as is, a power of two 32, odd as is, even halved to odd or 32";
    {
        const uint32_t rows[] = {20, 4096, 2048, 33, 96, 100, 4000};
        for (int k = 0; k < 7; k++)
        {
            Entry(4096, rows[k]);
            CHECK(CoXlTestShapeBegin(g_world, g_handle) == 1);
            RunJob();
            CoXlTestShapeEnd();
            const int per = 4096 / Leaf(rows[k]);
            CHECK(g_quad.rows == rows[k] && g_quad.depth == (uint64_t)(int64_t)(logf((float)per) / logf(2.0f)));
        }
        CHECK(Leaf(96) == 24 && Leaf(100) == 25 && Leaf(4000) == 125 && Leaf(33) == 33 && Leaf(20) == 20);
    }

    g_case = "a world not in the physics list: no shape, no job; the quadtree still built (0x1AC2FF)";
    {
        uint64_t none[4] = {1, 2, 3, 4}; memcpy(g_exe + 0x27BAB30u, none, sizeof none);
        Entry(4096, 4096);
        g_submit.calls = 0;
        CHECK(CoXlTestShapeBegin(g_world, g_handle) == 1 && g_submit.calls == 0 && CoXlTestShapeState() == 1);
        { uint32_t v; memcpy(&v, g_obj + 0x17C, 4); CHECK(v == 0); }
        CoXlTestShapeEnd();
        uint64_t s; memcpy(&s, g_entry + 0x18, 8); CHECK(s == 0 && g_quad.calls == 1);
        memcpy(g_exe + 0x27BAB30u, list, sizeof list);
    }

    g_case = "the level's end with the job out: it is waited for, then the end's steps";
    Entry(4096, 4096);
    CHECK(CoXlTestShapeBegin(g_world, g_handle) == 1);
    RunJob();
    CoXlTestShapeJoin();
    CHECK(CoXlTestShapeState() == 0 && g_quad.calls == 1);
    CoXlTestShapeJoin(); /* none: nothing */
    CHECK(g_quad.calls == 1);

    g_case = "the level update's entry: the build's level in play keeps the job; off state 3 it is joined";
    {
        static uint8_t level[0x200];
        uint32_t st = 3; memcpy(level + 0x1E4, &st, 4);
        Entry(4096, 4096);
        CHECK(CoXlTestShapeBegin(g_world, g_handle) == 1);
        CoXlTestNearJobLevelUpdate((uintptr_t)level, (uintptr_t)level);
        CHECK(CoXlTestShapeState() == 3);
        RunJob();
        st = 2; memcpy(level + 0x1E4, &st, 4);
        CoXlTestNearJobLevelUpdate((uintptr_t)level, (uintptr_t)level);
        CHECK(CoXlTestShapeState() == 0 && g_quad.calls == 1);
    }

    g_case = "a level init or the ship: the job joined before the tiles go";
    Entry(4096, 4096);
    CHECK(CoXlTestShapeBegin(g_world, g_handle) == 1);
    RunJob();
    CoXlTestShapeLevelLeft();
    CHECK(CoXlTestShapeState() == 0 && g_quad.calls == 1);

    g_case = "the ready poll: the job first, frames that wait, then the end and finalize; work() never called";
    {
        static uint8_t api[0x20], table[0x200], context[0x100];
        uint64_t apiAt = (uint64_t)(uintptr_t)api, tableAt = (uint64_t)(uintptr_t)table;
        memcpy(image + 0x3326308u, &apiAt, 8); memcpy(api + 8, &tableAt, 8);
        char (*ready)(uint64_t, uint64_t) = Ready; void (*work)(uint64_t, uint64_t) = Work;
        memcpy(table + 0x1A0, &ready, 8); memcpy(table + 0x1A8, &work, 8);
        memcpy(context + 0x40, &g_world, 8);
        Stub(image + 0xF451E0u, (void*)Finalize);
        VirtualProtect(image + 0xF45000u, 0x1000, PAGE_EXECUTE_READWRITE, &old);
        Entry(4096, 4096);
        g_works = g_finals = 0; g_submit.calls = 0;
        int state = 0;
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_submit.calls == 1 && g_finals == 0);
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_finals == 0 && g_submit.calls == 1); /* the job not run yet */
        RunJob();
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0 && g_finals == 1 && g_works == 0 && g_quad.calls == 1);
        CHECK(CoXlTestShapeState() == 0);

        g_case = "SpawnTerrainWork not the reviewed code: work() on the game thread as before, no job";
        g_fake.strictCode = 1; /* the host compares the bytes: the fake exe holds none of them */
        Entry(4096, 4096);
        g_works = g_finals = 0; g_submit.calls = 0;
        CoXlTestShapeExe((uintptr_t)g_exe); /* asks the pins again */
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0 && g_works == 1 && g_finals == 1 && g_submit.calls == 0);
        CHECK(fake_host_logged("SpawnTerrainWork is not the reviewed code"));
        g_fake.strictCode = 0;
    }

    if (g_failed) { printf("shapework: %d check(s) failed\n", g_failed); return 1; }
    printf("shapework: all checks passed\n");
    return 0;
}
