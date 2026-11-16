/* A released tile's terrain destroy split at its fence (proto/xl_free.c): the head (0x1ADC20, 0xC9480, the flagged
 * fence 0x105E40) in the release's frame under the terrain lock; the rest (the game's 0x106030, then the game's destroy
 * with [h+4] = 0xFFFFFF) two frame kicks later, or at a drain; the game's whole destroy for a bounds tree, no threaded
 * renderer, another slot, no render id or a full list; the install on its pins. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestMatRes(const void* fns, int ready, void* teardown);
void CoXlTestMatResTeardown(uint64_t world);
void CoXlTestNearLevelLeft(int ship);
void CoXlTestTerrainSplit(void* enter, void* leave, void* copyFree, void* remove, uint64_t slot, int ready);
int CoXlTestTerrainDestroySplit(uint64_t world, uint64_t h, uint64_t destroy);
void CoXlTestTerrainTailTick(void);
void CoXlTestTerrainTailDrain(uint64_t world);
uint32_t CoXlTestTerrainTailPending(void);
int CoXlTestTerrainSplitInstall(void);
void CoXlTestTerrainSplitLevel(uintptr_t level);
int CoXlTestMoveFreeQueued(int queue, uint64_t tex0, uint64_t terrain, int ticks);
void CoXlTestTerrainTailCheck(uint64_t h, int raised, float cx, float cy, int cellX, int cellY);
void CoXlTestTerrainCheckFn(void* fn);
void CoXlTestGame(uintptr_t game);
void CoXlTestLevelGone(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t g_world[0x230600], g_world2[0x230600], g_rw[0x20], g_r[0x1A60], g_h[0x100], g_h2[0x100];
#define W(p) ((uint64_t)(uintptr_t)(p))

/* The game's calls as stand-ins: what was called, in which order, with what, and the lock's depth at the time. */
static int g_depth, g_seq, g_enters;
static struct { int at, depth, n; uint64_t a, b; uint32_t rid; } g_copy, g_remove, g_fence, g_wait, g_destroy;
static uint64_t g_lockAt;
static uint32_t g_nextId = 7;
static int g_teardowns, g_destroysAtTeardown;
static void Enter(uint64_t cs) { g_lockAt = cs; g_depth++; g_enters++; }
static void Leave(uint64_t cs) { (void)cs; g_depth--; }
static void Note1(void* c, uint64_t a, uint64_t b)
{
    struct { int at, depth, n; uint64_t a, b; uint32_t rid; }* x = c;
    x->at = ++g_seq; x->depth = g_depth; x->n++; x->a = a; x->b = b;
}
static void CopyFree(uint64_t world, uint64_t h) { Note1(&g_copy, world, h); }
static void Remove(uint64_t rw, uint64_t h) { Note1(&g_remove, rw, h); }
static uint32_t Fence(uint64_t r, uint8_t flag) { Note1(&g_fence, r, flag); return g_nextId; }
static uint8_t Wait(uint64_t r, uint32_t id) { Note1(&g_wait, r, id); return 1; }
static void Destroy(uint64_t world, uint64_t h) { Note1(&g_destroy, world, h); g_destroy.rid = *(uint32_t*)(uintptr_t)(h + 4); }
static void Teardown(uint64_t world) { (void)world; g_teardowns++; g_destroysAtTeardown = g_destroy.n; }
static int g_checks; static float g_checkAt[2]; static int g_checkSeq;
static void Check(float cx, float cy, int a, int b) { (void)a; (void)b; g_checks++; g_checkAt[0] = cx; g_checkAt[1] = cy; g_checkSeq = ++g_seq; }
static int g_texFreed, g_texSeq;
static void TexFree(uint64_t tex) { if (tex == 0xBEEF) { g_texFreed++; g_texSeq = ++g_seq; } }

static void Reset(void)
{
    memset(&g_copy, 0, sizeof g_copy); memset(&g_remove, 0, sizeof g_remove); memset(&g_fence, 0, sizeof g_fence);
    memset(&g_wait, 0, sizeof g_wait); memset(&g_destroy, 0, sizeof g_destroy);
    g_seq = 0; g_depth = 0; g_enters = 0; g_lockAt = 0;
}
static void Kick(uint32_t k) { memcpy(g_r + 0x1A50, &k, 4); }
static void World(uint8_t* world, int threaded, uint32_t kick)
{
    const uint64_t rw = W(g_rw), r = W(g_r);
    memcpy(world + 0x2301F8u, &rw, 8); memcpy(g_rw, &r, 8);
    g_r[0x1480] = (uint8_t)threaded;
    Kick(kick);
}
static void Terrain(uint8_t* h, uint32_t rid, uint64_t cow)
{
    memset(h, 0, 0x100);
    memcpy(h + 4, &rid, 4); memcpy(h + 0x40, &cow, 8);
}
static void Split(int ready)
{
    CoXlTestTerrainSplit((void*)Enter, (void*)Leave, (void*)CopyFree, (void*)Remove, W(Destroy), ready);
    Reset();
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    uint64_t fns[11] = {0};
    fns[6] = W(Fence); fns[7] = W(Wait); /* MatResFns: .fence, .wait */
    CoXlTestMatRes(fns, 1, (void*)Teardown);
    CoXlTestTerrainSplitLevel(0);

    g_case = "the head: the CPU copy, the remove and the flagged fence in the terrain lock, in the game's order; no wait, no destroy";
    Split(1);
    World(g_world, 1, 40);
    Terrain(g_h, 0x1234, 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1);
    CHECK(g_lockAt == W(g_world) + 0x230520u && g_depth == 0 && g_enters == 1);
    CHECK(g_copy.n == 1 && g_copy.a == W(g_world) && g_copy.b == W(g_h) && g_copy.depth == 1);
    CHECK(g_remove.n == 1 && g_remove.a == W(g_rw) && g_remove.b == W(g_h) && g_remove.depth == 1 && g_remove.at > g_copy.at);
    CHECK(g_fence.n == 1 && g_fence.a == W(g_r) && g_fence.b == 1 && g_fence.depth == 1 && g_fence.at > g_remove.at);
    CHECK(g_wait.n == 0 && g_destroy.n == 0 && CoXlTestTerrainTailPending() == 1);
    CHECK(*(uint32_t*)(g_h + 4) == 0x1234); /* the render id stays until the rest */

    g_case = "the rest: not before the second frame kick; then the game's wait on the same fence, and its destroy past the fence in the lock";
    Kick(41);
    CoXlTestTerrainTailTick();
    CHECK(g_wait.n == 0 && g_destroy.n == 0 && CoXlTestTerrainTailPending() == 1);
    Kick(42);
    g_fake.log[0] = 0;
    CoXlTestTerrainTailTick();
    CHECK(g_wait.n == 1 && g_wait.a == W(g_r) && g_wait.b == 7 && g_wait.depth == 0);
    CHECK(g_destroy.n == 1 && g_destroy.a == W(g_world) && g_destroy.b == W(g_h) && g_destroy.depth == 1 && g_destroy.at > g_wait.at);
    CHECK(g_destroy.rid == 0xFFFFFFu && g_depth == 0 && g_enters == 2 && CoXlTestTerrainTailPending() == 0);
    CHECK(fake_host_logged("difficulty: moving tiles: a released terrain's destroy split at its fence (exe 0x1ADFA9): the head "));
    CHECK(fake_host_logged("2 frame kick(s) and 2 poll(s) later; 0 pending, 1 split, 1 completed, 0 drained, 0 left, 0 by the game's wait (last: none)"));
    g_fake.log[0] = 0;
    Kick(50);
    CoXlTestTerrainTailTick();
    CHECK(g_destroy.n == 1 && g_fake.log[0] == 0);

    g_case = "the kick count wraps: still two kicks";
    Split(1);
    World(g_world, 1, 0xFFFFFFFFu);
    Terrain(g_h, 0x55, 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1);
    Kick(0);
    CoXlTestTerrainTailTick();
    CHECK(g_destroy.n == 0);
    Kick(1);
    CoXlTestTerrainTailTick();
    CHECK(g_destroy.n == 1 && g_wait.n == 1);

    g_case = "the game's whole destroy: a bounds tree, no render thread, another slot; no call, no lock";
    Split(1);
    World(g_world, 1, 60);
    Terrain(g_h, 0x55, 0x9999);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 0);
    Terrain(g_h, 0x55, 0);
    World(g_world, 0, 60);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 0);
    World(g_world, 1, 60);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Wait)) == 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), 0) == 0);
    CHECK(g_enters == 0 && g_copy.n == 0 && g_remove.n == 0 && g_fence.n == 0 && CoXlTestTerrainTailPending() == 0);
    Split(0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 0 && g_enters == 0);

    g_case = "no render id: the game's destroy has no fence; the head only looked, in the lock";
    Split(1);
    Terrain(g_h, 0xFFFFFFu, 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 0);
    CHECK(g_enters == 1 && g_depth == 0 && g_copy.n == 0 && g_remove.n == 0 && g_fence.n == 0 && CoXlTestTerrainTailPending() == 0);

    g_case = "a full list: the 9th is the game's; the line counts it";
    Split(1);
    World(g_world, 1, 70);
    static uint8_t hs[9][0x100];
    for (int k = 0; k < 9; k++) Terrain(hs[k], 0x100u + (uint32_t)k, 0);
    for (int k = 0; k < 8; k++) CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(hs[k]), W(Destroy)) == 1);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(hs[8]), W(Destroy)) == 0 && CoXlTestTerrainTailPending() == 8 && g_fence.n == 8);
    Kick(72);
    g_fake.log[0] = 0;
    CoXlTestTerrainTailTick();
    CHECK(g_destroy.n == 8 && g_destroy.b == W(hs[7]) && CoXlTestTerrainTailPending() == 0);
    CHECK(fake_host_logged("1 by the game's wait (last: the list full)"));
    CHECK(fake_host_logged("later; 7 pending, 8 split, 1 completed") && fake_host_logged("later; 0 pending, 8 split, 8 completed"));

    g_case = "the generator's job runs: the rests wait, polled";
    Split(1);
    static uint8_t level[0xF720], job[16];
    const uint64_t jobAt = W(job);
    memcpy(level + 0xF718, &jobAt, 8); job[8] = 1;
    CoXlTestTerrainSplitLevel((uintptr_t)level);
    World(g_world, 1, 80);
    Terrain(g_h, 0x77, 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1);
    Kick(90);
    CoXlTestTerrainTailTick();
    CHECK(g_destroy.n == 0 && g_wait.n == 0 && CoXlTestTerrainTailPending() == 1);
    job[8] = 0;
    g_fake.log[0] = 0;
    CoXlTestTerrainTailTick();
    CHECK(g_destroy.n == 1 && fake_host_logged("10 frame kick(s) and 2 poll(s) later"));
    CoXlTestTerrainSplitLevel(0);

    g_case = "the world's teardown (exe 0x1A6E00): its world's fences waited before the game's teardown, their terrains left in the map; another world's kept";
    Split(1);
    World(g_world, 1, 100);
    Terrain(g_h, 0x88, 0);
    Terrain(g_h2, 0x89, 0);
    memcpy(g_world2 + 0x2301F8u, g_world + 0x2301F8u, 8);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1 && CoXlTestTerrainDestroySplit(W(g_world2), W(g_h2), W(Destroy)) == 1);
    g_teardowns = 0;
    g_fake.log[0] = 0;
    CoXlTestMatResTeardown(W(g_world));
    CHECK(g_teardowns == 1 && g_destroysAtTeardown == 0 && g_destroy.n == 0 && g_wait.n == 1 && g_wait.b == 7 && g_enters == 2);
    CHECK(*(uint32_t*)(g_h + 4) == 0x88 && CoXlTestTerrainTailPending() == 1);
    CHECK(fake_host_logged("0 frame kick(s) and 0 poll(s) later, drained at the world's teardown (fence only: the terrain left in the map); 1 pending, 2 split, 0 completed, 0 drained, 1 left"));

    g_case = "a level's end (NearTilesDestroy) while the generator's job runs leaves the rests; once it has left, does every one, whatever its kicks";
    job[8] = 1;
    CoXlTestTerrainSplitLevel((uintptr_t)level);
    CoXlTestNearLevelLeft(0);
    CHECK(g_destroy.n == 0 && g_wait.n == 1 && CoXlTestTerrainTailPending() == 1);
    job[8] = 0;
    CoXlTestNearLevelLeft(0);
    CoXlTestTerrainSplitLevel(0);
    CHECK(g_destroy.n == 1 && g_destroy.b == W(g_h2) && g_destroy.a == W(g_world2) && CoXlTestTerrainTailPending() == 0);
    CHECK(fake_host_logged("drained at a level init"));

    g_case = "a released tile's textures wait for its own terrain's rest, no other";
    Split(1);
    World(g_world, 1, 110);
    Terrain(g_h, 0x99, 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1);
    CHECK(CoXlTestMoveFreeQueued(1, 0, W(g_h), 10) == 1);
    CHECK(CoXlTestMoveFreeQueued(1, 0, W(g_h2), 3) == 1 && CoXlTestMoveFreeQueued(0, 0, 0, 1) == 0); /* another terrain's: 4 frames */
    CHECK(CoXlTestMoveFreeQueued(1, 0, 0, 4) == 0);
    CHECK(CoXlTestMoveFreeQueued(1, 0, W(g_h), 10) == 1);
    CoXlTestTerrainTailDrain(0);
    CHECK(CoXlTestMoveFreeQueued(0, 0, 0, 3) == 1 && CoXlTestMoveFreeQueued(0, 0, 0, 1) == 0);

    g_case = "the release's ground check waits for the rest (the tile's body answers until then); unsplit, it runs at once";
    Split(1);
    CoXlTestTerrainCheckFn((void*)Check);
    g_checks = 0;
    World(g_world, 1, 120);
    Terrain(g_h, 0x9A, 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1);
    CoXlTestTerrainTailCheck(W(g_h), 1, 11.0f, 12.0f, 1, 2);
    CHECK(g_checks == 0);
    Kick(122);
    CoXlTestTerrainTailTick();
    CHECK(g_checks == 1 && g_checkAt[0] == 11.0f && g_checkAt[1] == 12.0f && g_checkSeq > g_destroy.at);
    Terrain(g_h, 0x9B, 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1);
    CoXlTestTerrainTailCheck(W(g_h), 0, 0, 0, 0, 0); /* nothing raised: no check */
    CoXlTestTerrainTailCheck(0, 1, 5.0f, 6.0f, 0, 0); /* a destroy not split */
    CHECK(g_checks == 2 && g_checkAt[0] == 5.0f);
    Kick(124);
    CoXlTestTerrainTailTick();
    CHECK(g_checks == 2 && CoXlTestTerrainTailPending() == 0);
    CoXlTestTerrainCheckFn(NULL);

    g_case = "a bounds tree made in the window: the rest is not run, loudly; the terrain stays";
    Split(1);
    World(g_world, 1, 130);
    Terrain(g_h, 0x9C, 0);
    CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1);
    const uint64_t tree = 0x7777;
    memcpy(g_h + 0x40, &tree, 8);
    Kick(132);
    g_fake.log[0] = 0;
    CoXlTestTerrainTailTick();
    CHECK(g_destroy.n == 0 && g_wait.n == 1 && g_depth == 0 && *(uint32_t*)(g_h + 4) == 0x9C && CoXlTestTerrainTailPending() == 0);
    CHECK(fake_host_logged("WARNING: a released terrain (0x") && fake_host_logged("(NOT run: a bounds tree); 0 pending, 1 split, 0 completed, 0 drained, 1 left"));

    g_case = "a level's start (MoveLevelGone): the rests before their textures";
    {
        static uint8_t api[0x20], t10[0x200];
        const uint64_t a = W(api), t = W(t10), f = W(TexFree);
        memcpy(g_fake.image + 0x3326308u, &a, 8); memcpy(api + 0x10, &t, 8); memcpy(t10 + 0x198, &f, 8);
        CoXlTestGame((uintptr_t)g_fake.image);
        Split(1);
        World(g_world, 1, 140);
        Terrain(g_h, 0x9D, 0);
        CHECK(CoXlTestTerrainDestroySplit(W(g_world), W(g_h), W(Destroy)) == 1);
        job[8] = 1; /* the generator's job runs: no rest now (the terrain lock is its) */
        CoXlTestTerrainSplitLevel((uintptr_t)level);
        CoXlTestLevelGone();
        CHECK(g_destroy.n == 0 && CoXlTestTerrainTailPending() == 1);
        job[8] = 0;
        CoXlTestTerrainSplitLevel(0);
        CHECK(CoXlTestMoveFreeQueued(1, 0xBEEF, W(g_h), 1) == 1);
        g_texFreed = 0;
        CoXlTestLevelGone();
        CHECK(g_destroy.n == 1 && g_texFreed == 1 && g_destroy.at < g_texSeq && CoXlTestTerrainTailPending() == 0);
        CHECK(fake_host_logged("drained at a level's start"));
        memset(g_fake.image + 0x3326308u, 0, 8);
    }

    g_case = "the install: every pin of the destroy, its CPU copy free, the remove, the bounds test and the thunk; op 0x41's fence copy first";
    {
        static const struct { uint32_t at; uint32_t len; } pins[] = {{0x1ADEA0u, 0x2B1u}, {0x1ADC20u, 0xA7u}, {0xC9480u, 0x9Fu}, {0x2F4D40u, 0x23u}, {0x336AD0u, 5u}};
        uint64_t lock[2] = {W(Enter), W(Leave)};
        memcpy(exe + 0x13F66B8u, lock, 16);
        g_fake.log[0] = 0;
        CHECK(CoXlTestTerrainSplitInstall() == 1);
        CHECK(fake_host_logged("difficulty: moving tiles: a released terrain's destroy is split at its fence (exe 0x1ADFA9): its rest runs 2 frame kicks later, without the wait"));
        for (size_t i = 0; i < sizeof pins / sizeof pins[0]; i++)
        {
            g_fake.noMatchExe[0] = pins[i].at;
            g_fake.log[0] = 0;
            CHECK(CoXlTestTerrainSplitInstall() == -1);
            char line[160];
            snprintf(line, sizeof line, "a released terrain's destroy keeps its fence wait (exe+0x%X is not the reviewed code)", pins[i].at);
            CHECK(fake_host_logged(line));
        }
        g_fake.noMatchExe[0] = 0;
        memset(exe + 0x13F66C0u, 0, 8);
        CHECK(CoXlTestTerrainSplitInstall() == -1 && fake_host_logged("(the terrain lock's calls do not read)"));
        memcpy(exe + 0x13F66B8u, lock, 16);
        CoXlTestMatRes(fns, -1, (void*)Teardown);
        CHECK(CoXlTestTerrainSplitInstall() == -1 && fake_host_logged("(op 0x41's fence copy is off)"));
        CoXlTestMatRes(fns, 1, (void*)Teardown);
        CHECK(CoXlTestTerrainSplitInstall() == 1);
    }

    if (g_failed) { printf("terrainsplit: %d check(s) failed\n", g_failed); return 1; }
    printf("terrainsplit: all checks passed\n");
    return 0;
}
