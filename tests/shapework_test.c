/* The tile's collision shape on a worker (proto/difficulty_world_terrain.c NearShapeBegin / NearShapeEnd): the ready
 * frame held the game thread 136-140 ms in SpawnTerrainWork (exe 0x1AC210). Its steps are the function's own in its
 * order: under the terrain lock the object filled from the entry, the shape build (0x786130) alone as an engine job,
 * then under the lock again the shape into the entry and the quadtree (0x2F2CB0) with the function's 13 arguments,
 * leaf and depth. Only a build in play takes the job (the load keeps work()); a joined shape is finalized, never begun
 * again; a job that never comes back is given up on. Fakes stand at the exe's addresses; the test runs the job. */
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
int CoXlTestShapeFlags(void);
void CoXlTestShapeAge(ULONGLONG ms);
void CoXlTestShapeInside(void);
void CoXlTestShapeReset(void);
void CoXlTestShapeForget(void);
void CoXlTestMoveBuilding(int on);
void CoXlTestNearJobBroken(int broken);
void CoXlTestMatCopySetup(uint64_t tex, uint32_t* pixels);
uint64_t CoXlTestMatReadAsync(uint64_t src, uint64_t alloc, uint64_t dst, uint64_t done);
void CoXlTestMatCopyFinish(void);
long long CoXlTestMatCopyAt(void);
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
static uint8_t g_entry[0x200], g_obj[0x200], g_dims[0x20], g_treeHolder[0x50];
static float g_nodes[6 * 4];
static uint64_t g_heights;
static uint16_t g_hbuf[4096 * 4096];
static struct { uint64_t tree, heights, cols, rows, a5, a6, a7, a8, a9, a10, a11, a12, a13; uint32_t first; int calls; } g_quad;
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
/* 0x2F2CB0's list at tree+0x38: {count, capacity, nodes}; a depth-7 tree adds 21845 nodes of 24 bytes, the root first
 * at the old count, its minimum and maximum height floats at +0 / +4 */
static uint64_t Quad(uint64_t tree, uint64_t heights, uint64_t cols, uint64_t rows, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9,
    uint64_t a10, uint64_t a11, uint64_t a12, uint64_t a13)
{
    g_quad.tree = tree; g_quad.heights = heights; g_quad.cols = cols; g_quad.rows = rows; g_quad.a5 = a5; g_quad.a6 = a6; g_quad.a7 = a7; g_quad.a8 = a8;
    g_quad.a9 = a9; g_quad.a10 = a10; g_quad.a11 = a11; g_quad.a12 = a12; g_quad.a13 = a13; g_quad.calls++; g_lockedQuad = g_depth;
    uint32_t count = 0; memcpy(&count, (void*)(uintptr_t)tree, 4);
    g_quad.first = count;
    if (count < 4) { g_nodes[6 * count] = -3.5f; g_nodes[6 * count + 1] = 812.25f; }
    count += 21845; memcpy((void*)(uintptr_t)tree, &count, 4);
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

static void* g_matBlock;
static void** MatAllocate(void* self, void** out, uint64_t bytes, uint64_t align)
{ (void)self; (void)align; g_matBlock = *out = VirtualAlloc(NULL, (size_t)bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); return out; }

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
    memset(g_treeHolder, 0, sizeof g_treeHolder); memset(g_nodes, 0, sizeof g_nodes);
    uint64_t nodes = (uint64_t)(uintptr_t)g_nodes; memcpy(g_treeHolder + 0x40, &nodes, 8);
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

    for (size_t i = 0; i < 4096u * 4096u; i++) g_hbuf[i] = 1000;
    g_hbuf[12345] = 300; g_hbuf[4096u * 4096u - 7] = 64000;
    g_heights = (uint64_t)(uintptr_t)g_hbuf;
    CoXlTestMoveHold(0);
    CoXlTestMoveBuilding(1); /* a tile builds in play */
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

    g_case = "end: under the lock the shape into the entry, then the quadtree as 0x1AC539..0x1AC585 stacks its 13 arguments";
    g_maxDepth = 0; g_lockedFind = 0;
    { uint32_t had = 3; memcpy(g_treeHolder + 0x38, &had, 4); } /* nodes already in the list: the root goes in after them */
    CoXlTestShapeEnd();
    CHECK(g_depth == 0 && g_maxDepth == 1 && g_lockedFind == 1 && g_lockedQuad == 1);
    { uint64_t s; memcpy(&s, g_entry + 0x18, 8); CHECK(s == 0x5A5A0001ull); }
    CHECK(g_quad.calls == 1 && g_quad.tree == (uint64_t)(uintptr_t)g_treeHolder + 0x38 && g_quad.heights == g_heights);
    CHECK(g_quad.cols == 4096 && g_quad.rows == 4096);
    {
        uint64_t xy0, xy1; memcpy(&xy0, g_entry + 0x24, 8); memcpy(&xy1, g_entry + 0x2C, 8);
        /* rcx, rdx, r8, r9; [rsp+0x20] never written (0 here); +0x28 0; +0x30 / +0x38 the corner; +0x40 the far corner;
         * +0x48 the depth so far 0; +0x50 the depth; +0x58 / +0x60 no min / max out */
        CHECK(g_quad.a5 == 0 && g_quad.a6 == 0 && g_quad.a7 == xy0 && g_quad.a8 == xy0 && g_quad.a9 == xy1);
        CHECK(g_quad.a10 == 0 && g_quad.a11 == (uint64_t)(int64_t)(logf(128.0f) / logf(2.0f)) && g_quad.a11 == 7 && g_quad.a12 == 0 && g_quad.a13 == 0);
    }
    CHECK(CoXlTestShapeState() == 0 && CoXlTestMoveHoldSince() == 0);
    CHECK(fake_host_logged("the tile's collision shape built on a worker"));
    CHECK(g_quad.first == 3 && fake_host_logged("the tile's render quadtree: 21848 nodes (21845 for 4096 x 4096 at depth 7), root -3.50..812.25 m"));
    CHECK((CoXlTestShapeFlags() & 2) != 0); /* its handle kept as done */
    CHECK(fake_host_logged("the tile's heights (scanned on the job): 300..64000 = -495.42..476.58 m; its quadtree root should be -495.67..477.83 m"));

    g_case = "end once: a second end (or a join after it) does nothing";
    CoXlTestShapeEnd();
    CoXlTestShapeJoin();
    CHECK(g_quad.calls == 1);

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
            CHECK(g_quad.rows == rows[k] && g_quad.a11 == (uint64_t)(int64_t)(logf((float)per) / logf(2.0f)) && g_quad.a10 == 0);
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
        CoXlTestShapeForget(); /* a new tile's spawn (NearSpawnTile): the joins above left this handle done */
        int state = 0;
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_submit.calls == 1 && g_finals == 0);
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_finals == 0 && g_submit.calls == 1); /* the job not run yet */
        RunJob();
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0 && g_finals == 1 && g_works == 0 && g_quad.calls == 1);
        CHECK(CoXlTestShapeState() == 0 && (CoXlTestShapeFlags() & 2) == 0);

        static uint8_t level[0x200];
        uint32_t st = 5; memcpy(level + 0x1E4, &st, 4);
        g_case = "the load (no build in play, level state 5): work() as the game has it, no job; its quadtree line the control";
        CoXlTestMoveBuilding(0);
        Entry(4096, 4096);
        { uint32_t n = 21845; memcpy(g_treeHolder + 0x38, &n, 4); g_nodes[0] = -1.25f; g_nodes[1] = 640.5f; }
        g_works = g_finals = 0; g_submit.calls = 0; g_fake.log[0] = 0;
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0 && g_works == 1 && g_finals == 1 && g_submit.calls == 0);
        CHECK(fake_host_logged("the tile's render quadtree (work() on the game thread): 21845 nodes, root -1.25..640.50 m"));
        CHECK(fake_host_logged("the tile's heights (work()'s, scanned on the game thread): 300..64000 = -495.42..476.58 m"));
        CoXlTestNearJobLevelUpdate((uintptr_t)level, 0x1234); /* the load's own level updates: nothing joined, nothing ended */
        CHECK(CoXlTestShapeState() == 0 && g_quad.calls == 0 && g_works == 1);
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0 && g_works == 2 && g_submit.calls == 0); /* never a job */
        CoXlTestMoveBuilding(1);

        g_case = "a join in play (the level leaves play), then the ready poll: finalized from the joined shape, never begun again";
        Entry(4096, 4096);
        g_works = g_finals = 0; g_submit.calls = 0;
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_submit.calls == 1);
        RunJob();
        st = 2; memcpy(level + 0x1E4, &st, 4);
        CoXlTestNearJobLevelUpdate((uintptr_t)level, (uintptr_t)level);
        CHECK(CoXlTestShapeState() == 0 && g_quad.calls == 1 && (CoXlTestShapeFlags() & 2) != 0);
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0 && g_finals == 1 && g_works == 0 && g_submit.calls == 1 && g_quad.calls == 1);
        CHECK((CoXlTestShapeFlags() & 2) == 0);

        g_case = "a new tile's handle forgets a joined shape: a reused handle starts its own";
        Entry(4096, 4096);
        g_submit.calls = 0;
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_submit.calls == 1);
        RunJob();
        CoXlTestShapeJoin();
        CoXlTestShapeForget();
        CHECK((CoXlTestShapeFlags() & 2) == 0);
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_submit.calls == 2);
        RunJob();
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0);

        g_case = "a job that never ran in 30 s: shut out, the tile built by work() here; the late job builds nothing";
        Entry(4096, 4096);
        g_works = g_finals = 0; g_submit.calls = 0; g_shapeObj = 0;
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_submit.calls == 1);
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_works == 0); /* under 30 s it waits */
        CoXlTestShapeAge(30001);
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0 && state == 6 && g_works == 1 && g_finals == 1);
        CHECK((CoXlTestShapeFlags() & 5) == 4 && CoXlTestMoveHoldSince() == 0 && fake_host_logged("(it never ran); no more jobs this session"));
        RunJob();
        CHECK(g_shapeObj == 0 && CoXlTestShapeState() == 0);
        CoXlTestNearJobBroken(0);

        g_case = "a job stuck in the shape's steps: the tile fails, nothing waits for it again, the next tile is built by work()";
        Entry(4096, 4096);
        g_works = g_finals = 0; g_submit.calls = 0;
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1 && g_submit.calls == 1);
        CoXlTestShapeInside();
        CoXlTestShapeAge(30001);
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0 && state == 8 && g_finals == 0 && g_works == 0);
        CHECK((CoXlTestShapeFlags() & 1) != 0 && fake_host_logged("(it is still in the shape's steps)"));
        { ULONGLONG t = GetTickCount64(); CoXlTestShapeJoin(); CHECK(GetTickCount64() - t < 1000 && g_quad.calls == 0); }
        CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle + 1, &state) == 0 && g_works == 1 && g_finals == 1 && g_submit.calls == 1);
        CoXlTestShapeReset(); CoXlTestNearJobBroken(0); g_submit.fn = NULL;

        g_case = "the ready poll moves the spawn's readback copy on a slice each call";
        {
            static void* vt[8]; void* allocator[2] = {vt, NULL}; vt[6] = (void*)MatAllocate;
            uint32_t* pixels = (uint32_t*)VirtualAlloc(NULL, (size_t)4096 * 4096 * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            uint64_t dst = 0; uint8_t done = 0;
            CoXlTestMatCopySetup(0xBA0D, pixels);
            CoXlTestMatReadAsync(0xBA0D, (uint64_t)(uintptr_t)allocator, (uint64_t)(uintptr_t)&dst, (uint64_t)(uintptr_t)&done);
            const long long at = CoXlTestMatCopyAt();
            CHECK(at > 0);
            Entry(4096, 4096);
            CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 1);
            CHECK(CoXlTestMatCopyAt() > at || (CoXlTestMatCopyAt() == -1 && done == 1));
            RunJob();
            CHECK(CoXlTestReadyTile((uint64_t)(uintptr_t)context, g_handle, &state) == 0);
            CoXlTestMatCopyFinish();
            CHECK(done == 1 && dst == (uint64_t)(uintptr_t)g_matBlock);
            VirtualFree(g_matBlock, 0, MEM_RELEASE); VirtualFree(pixels, 0, MEM_RELEASE);
            CoXlTestMatCopySetup(0, NULL);
        }

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
