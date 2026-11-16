/* Op 0x41's unit resources (proto/xl_free.c, A10): an owned resource's release is pushed as 0x1A8CE0 pushes it, without
 * its fence wait, after the game's own destroy with the record's byte cleared; the resource and its two arrays are freed
 * three frame kicks later from the tick, or after one flagged fence at the world's render teardown; another level's are
 * left; a full list, an unready copy or another slot keep the game's own destroy. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

typedef struct
{
    uint64_t (*unitRec)(uint64_t map, const void* key);
    uint64_t (*resRec)(uint64_t map, const void* key);
    uint64_t (*ctxSlot)(uint64_t dev);
    void (*release)(uint64_t a, uint64_t b);
    void (*pushC)(uint64_t a, uint64_t b);
    void (*pushD)(uint64_t a, uint64_t b);
    uint32_t (*fence)(uint64_t r, uint8_t flag);
    uint8_t (*wait)(uint64_t r, uint32_t id);
    void (*erase)(uint64_t map, const uint32_t* id);
    void (*original)(uint64_t world, uint32_t id, uint8_t erase);
    uint64_t destroySlot;
} Fns;

void CoXlTestMatRes(const void* fns, int ready, void* teardown);
void CoXlTestMatUnitDestroy(uint64_t destroy, uint64_t world, uint64_t unit);
void CoXlTestMatResTick(void);
void CoXlTestMatResTeardown(uint64_t world);
void CoXlTestMatResNote(void);
uint32_t CoXlTestMatResPending(void);
void CoXlTestSetLevels(LONG levels);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define WORLDS 2
static uint8_t* g_world[WORLDS];
static uint8_t g_r[0x1A60], g_dev[16], g_unitRec[24], g_resRec[0x40];
static uint64_t g_devVt[0x20], g_allocVt[16], g_holder;
static uint8_t g_res[4][0x1E0];
static uint64_t g_arrayAlloc[2]; /* objects: their first qword is the vtable */
static char g_calls[1024];
static int g_flagAtDestroy, g_waits, g_fences;
static uint64_t g_freed[16]; static int g_frees;

static void Call(const char* what) { strncat(g_calls, what, sizeof g_calls - strlen(g_calls) - 1); strncat(g_calls, " ", sizeof g_calls - strlen(g_calls) - 1); }
static uint64_t W(int k) { return (uint64_t)(uintptr_t)g_world[k]; }

static uint64_t UnitRec(uint64_t map, const void* key) { (void)key; Call(map == W(0) + 0x2304F8u || map == W(1) + 0x2304F8u ? "unitRec" : "unitRec?"); return (uint64_t)(uintptr_t)g_unitRec; }
static uint64_t ResRec(uint64_t map, const void* key) { Call(map == W(0) + 0x2304D0u && *(const uint32_t*)key == 77 ? "resRec" : "resRec?"); return (uint64_t)(uintptr_t)g_resRec; }
static uint64_t Ctx(uint64_t dev) { Call(dev == (uint64_t)(uintptr_t)g_dev ? "ctx" : "ctx?"); return 0xBA7C4u; }
static void Release(uint64_t res, uint64_t b) { Call(res == *(uint64_t*)g_resRec && b == 0xBA7C4u ? "release" : "release?"); }
static void PushC(uint64_t r, uint64_t b) { Call(r == (uint64_t)(uintptr_t)g_r && b == 0xBA7C4u ? "0xC" : "0xC?"); }
static void PushD(uint64_t r, uint64_t b) { Call(r == (uint64_t)(uintptr_t)g_r && b == 0xBA7C4u ? "0xD" : "0xD?"); }
static uint32_t Fence(uint64_t r, uint8_t flag) { g_fences++; Call(flag == 1 ? "fence1" : "fence0"); return 5; }
static uint8_t Wait(uint64_t r, uint32_t id) { g_waits++; Call(id == 5 ? "wait" : "wait?"); return 1; }
static void Erase(uint64_t map, const uint32_t* id) { Call(map == W(0) + 0x2304D0u && *id == 77 ? "erase" : "erase?"); }
static void Original(uint64_t world, uint32_t id, uint8_t erase) { Call(world == W(0) && id == 77 && erase == 1 ? "original" : "original?"); }
static void Destroy(uint64_t world, uint64_t unit) { g_flagAtDestroy = g_unitRec[0xC]; Call(unit == 0x5150u ? "destroy" : "destroy?"); }
static void Teardown(uint64_t world) { Call(world == W(0) ? "teardown" : "teardown?"); }
static void AllocFree(uint64_t alloc, uint64_t p, uint64_t zero)
{
    char text[48];
    snprintf(text, sizeof text, "free%s", alloc == W(0) + 0x230058u || alloc == W(1) + 0x230058u ? "W" : alloc == (uint64_t)(uintptr_t)&g_arrayAlloc[0] ? "I" : alloc == (uint64_t)(uintptr_t)&g_arrayAlloc[1] ? "V" : "?");
    Call(text);
    if (g_frees < 16) g_freed[g_frees++] = p;
}

static Fns g_fns = {UnitRec, ResRec, Ctx, Release, PushC, PushD, Fence, Wait, Erase, Original, (uint64_t)(uintptr_t)Destroy};

static void Res(int k, int arrays)
{
    uint8_t* b = g_res[k];
    memset(b, 0, sizeof g_res[k]);
    if (arrays)
    {
        *(uint32_t*)(b + 0x1C4) = 8; *(uint32_t*)(b + 0x1C0) = 6; *(uint64_t*)(b + 0x1D0) = (uint64_t)(uintptr_t)&g_arrayAlloc[0]; *(uint64_t*)(b + 0x1C8) = 0x1D0Cu;
        *(uint32_t*)(b + 0x1AC) = 4; *(uint32_t*)(b + 0x1A8) = 4; *(uint64_t*)(b + 0x1B8) = (uint64_t)(uintptr_t)&g_arrayAlloc[1]; *(uint64_t*)(b + 0x1B0) = 0x7E7Cu;
    }
}

/* A unit whose record owns resource 77 = g_res[k]; the renderer at kick `kick`. */
static void Unit(int k, int owns, uint32_t kick)
{
    memset(g_unitRec, 0, sizeof g_unitRec);
    *(uint32_t*)(g_unitRec + 8) = 77; g_unitRec[0xC] = (uint8_t)owns;
    *(uint64_t*)g_resRec = (uint64_t)(uintptr_t)g_res[k];
    *(uint32_t*)(g_r + 0x1A50) = kick;
    g_calls[0] = 0; g_flagAtDestroy = -1; g_frees = 0; g_waits = 0; g_fences = 0;
}

static void Kick(uint32_t kick) { *(uint32_t*)(g_r + 0x1A50) = kick; g_calls[0] = 0; g_frees = 0; }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    for (int k = 0; k < WORLDS; k++)
    {
        g_world[k] = (uint8_t*)calloc(1, 0x231000);
        *(uint64_t*)(g_world[k] + 0x2301F8u) = (uint64_t)(uintptr_t)&g_holder;
        *(uint64_t*)(g_world[k] + 0x230058u) = (uint64_t)(uintptr_t)g_allocVt;
    }
    g_holder = (uint64_t)(uintptr_t)g_r;
    g_r[0x1480] = 1;
    *(uint64_t*)(g_r + 0x1168) = (uint64_t)(uintptr_t)g_dev;
    *(uint64_t*)g_dev = (uint64_t)(uintptr_t)g_devVt;
    g_devVt[0xF8 / 8] = (uint64_t)(uintptr_t)Ctx;
    g_allocVt[0x40 / 8] = (uint64_t)(uintptr_t)AllocFree;
    g_arrayAlloc[0] = g_arrayAlloc[1] = (uint64_t)(uintptr_t)g_allocVt;
    CoXlTestSetLevels(3);
    const uint64_t destroy = (uint64_t)(uintptr_t)Destroy;

    g_case = "an owned resource: the byte cleared before the game's destroy, then 0x1A8CE0's pushes in its order, no fence, the entry erased";
    CoXlTestMatRes(&g_fns, 1, (void*)Teardown);
    Res(0, 1); Unit(0, 1, 100);
    CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u);
    CHECK(g_flagAtDestroy == 0);
    CHECK(strcmp(g_calls, "unitRec destroy resRec ctx release 0xC 0xD erase ") == 0);
    CHECK(g_fences == 0 && g_waits == 0 && g_frees == 0 && CoXlTestMatResPending() == 1);

    g_case = "freed only at the third frame kick from the push: the index array, the vertex array, then res, each through its own allocator";
    Kick(102);
    CoXlTestMatResTick();
    CHECK(g_frees == 0 && CoXlTestMatResPending() == 1);
    Kick(103);
    CoXlTestMatResTick();
    CHECK(strcmp(g_calls, "freeI freeV freeW ") == 0 && g_frees == 3 && g_freed[0] == 0x1D0Cu && g_freed[1] == 0x7E7Cu && g_freed[2] == (uint64_t)(uintptr_t)g_res[0]);
    CHECK(*(uint32_t*)(g_res[0] + 0x1C4) == 0 && *(uint32_t*)(g_res[0] + 0x1C0) == 0 && *(uint64_t*)(g_res[0] + 0x1C8) == 0 && *(uint32_t*)(g_res[0] + 0x1AC) == 0);
    CHECK(CoXlTestMatResPending() == 0);

    g_case = "the kick count wraps: still three kicks";
    Res(1, 0); Unit(1, 1, 0xFFFFFFFEu);
    CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u);
    Kick(0);
    CoXlTestMatResTick();
    CHECK(g_frees == 0);
    Kick(1);
    CoXlTestMatResTick();
    CHECK(strcmp(g_calls, "freeW ") == 0 && g_freed[0] == (uint64_t)(uintptr_t)g_res[1]); /* no arrays: res alone */

    g_case = "a unit without its own resource: the game's destroy alone, the byte untouched";
    Unit(0, 0, 200);
    CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u);
    CHECK(strcmp(g_calls, "unitRec destroy ") == 0 && g_flagAtDestroy == 0 && CoXlTestMatResPending() == 0);

    g_case = "not ready, or a slot other than the reviewed one: the game's destroy with the byte as it was";
    CoXlTestMatRes(&g_fns, -1, (void*)Teardown);
    Res(0, 1); Unit(0, 1, 300);
    CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u);
    CHECK(strcmp(g_calls, "destroy ") == 0 && g_flagAtDestroy == 1);
    CoXlTestMatRes(&g_fns, 1, (void*)Teardown);
    Unit(0, 1, 300);
    CoXlTestMatUnitDestroy((uint64_t)(uintptr_t)Teardown + 0, W(0), 0x5150u); /* another function in the slot */
    CHECK(strstr(g_calls, "unitRec") == NULL && strstr(g_calls, "release") == NULL);

    g_case = "no render thread, or a device whose +0xF8 slot is not 0x525F40 (neither d3d table): 0x1A8CE0 itself after the game's destroy";
    CoXlTestMatRes(&g_fns, 1, (void*)Teardown);
    Res(0, 1); Unit(0, 1, 400);
    g_r[0x1480] = 0;
    CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u);
    CHECK(strcmp(g_calls, "unitRec destroy original ") == 0 && g_flagAtDestroy == 0 && CoXlTestMatResPending() == 0);
    g_r[0x1480] = 1;
    g_devVt[0xF8 / 8] = (uint64_t)(uintptr_t)Release;
    Unit(0, 1, 400);
    CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u);
    CHECK(strcmp(g_calls, "unitRec destroy original ") == 0);
    g_devVt[0xF8 / 8] = (uint64_t)(uintptr_t)Ctx;

    g_case = "a full list: the 65th goes through 0x1A8CE0; the line counts it";
    CoXlTestMatRes(&g_fns, 1, (void*)Teardown);
    Res(0, 0);
    for (int k = 0; k < 64; k++) { Unit(0, 1, 500); CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u); }
    CHECK(CoXlTestMatResPending() == 64);
    Unit(0, 1, 500);
    CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u);
    CHECK(strcmp(g_calls, "unitRec destroy original ") == 0 && CoXlTestMatResPending() == 64);
    g_fake.log[0] = 0;
    CoXlTestMatResNote();
    CHECK(fake_host_logged("op 0x41's unit resources this build: 64 released without the wait, 0 freed after 3 frame kicks or a teardown, pending max 64 (now 64), "
        "1 by the game's wait (list full or no renderer), 0 teardown drains, 0 left to a gone level"));

    g_case = "the world's render teardown: one flagged fence and its wait, then that world's resources, then the game's teardown";
    Kick(500);
    CoXlTestMatResTeardown(W(0));
    CHECK(g_fences == 1 && g_waits == 1 && g_frees == 16 && CoXlTestMatResPending() == 0);
    CHECK(strncmp(g_calls, "fence1 wait freeW", 17) == 0 && strstr(g_calls, "teardown ") != NULL && strstr(g_calls, "teardown ") > strstr(g_calls, "freeW"));
    g_calls[0] = 0; g_fences = g_waits = 0;
    CoXlTestMatResTeardown(W(0)); /* nothing waits: no fence */
    CHECK(strcmp(g_calls, "teardown ") == 0 && g_fences == 0);

    g_case = "another world's teardown leaves this world's resources";
    CoXlTestMatRes(&g_fns, 1, (void*)Teardown);
    Res(0, 0); Unit(0, 1, 600);
    CoXlTestMatUnitDestroy(destroy, W(0), 0x5150u);
    g_calls[0] = 0;
    CoXlTestMatResTeardown(W(1));
    CHECK(strcmp(g_calls, "teardown? ") == 0 && CoXlTestMatResPending() == 1);

    g_case = "two worlds waiting: a teardown frees its own world's resource only";
    Res(2, 0); Unit(2, 1, 600);
    CoXlTestMatUnitDestroy(destroy, W(1), 0x5150u);
    CHECK(CoXlTestMatResPending() == 2);
    g_calls[0] = 0; g_frees = 0;
    CoXlTestMatResTeardown(W(1));
    CHECK(g_frees == 1 && g_freed[0] == (uint64_t)(uintptr_t)g_res[2] && CoXlTestMatResPending() == 1);

    g_case = "another level's resource is left, never freed (T2)";
    CoXlTestSetLevels(4);
    Kick(700);
    CoXlTestMatResTick();
    CHECK(g_frees == 0 && CoXlTestMatResPending() == 0);
    g_fake.log[0] = 0;
    CoXlTestMatResNote();
    CHECK(fake_host_logged("2 released without the wait, 1 freed") && fake_host_logged("1 teardown drains, 1 left to a gone level"));
    g_fake.log[0] = 0;
    CoXlTestMatResNote(); /* nothing since: no line */
    CHECK(strstr(g_fake.log, "op 0x41's unit resources") == NULL);

    if (g_failed) { printf("matres: %d check(s) failed\n", g_failed); return 1; }
    printf("matres: all checks passed\n");
    return 0;
}
