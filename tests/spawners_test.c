/* The director's spawner list per mission, run as code: the game's append and a stand-in for the builder's loop go
 * through HellForge's guard and the mod's stubs on the fake host. A normal mission is the game's with HellForge's guard
 * of 128 to the byte, a complex one fills the mod's list of 1024, and the builder follows the list the count is of.
 * The builder's group block is sized for every group its lists can give, by the game's own size code run here. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_spawners.h"
#include "xl/co_spawner_sites.h"
#include "xl/co_normal.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* The registers the game's code has at the two sites, and what it left: r13 and which way it went. */
typedef struct Regs { uint64_t rbx, r13, r14, rdi, r15, r8, r9, r10, r12, rdx, xmm4, outR13, outWay, r11, rsi, outRbx; } Regs;
Regs* g_runRegs;
uint64_t Run(Regs* regs, uintptr_t entry);
void RunBack(void);
/* A frame like the append's (rbp = rsp + 0x100, rsp aligned), the key staged as the game does before the site. */
__asm__(".intel_syntax noprefix\n"
    ".globl Run\nRun:\n"
    "push rbx\npush rbp\npush rdi\npush rsi\npush r12\npush r13\npush r14\npush r15\n"
    "sub rsp, 0x208\nlea rbp, [rsp+0x100]\n"
    "mov [rip+g_runRegs], rcx\nmov rax, rdx\n"
    "mov rbx, [rcx]\nmov r13, [rcx+8]\nmov r14, [rcx+16]\nmov rdi, [rcx+24]\nmov r15, [rcx+32]\n"
    "mov r8, [rcx+40]\nmov r9, [rcx+48]\nmov r10, [rcx+56]\nmov r12, [rcx+64]\nmov rdx, [rcx+72]\nmovq xmm4, [rcx+80]\n"
    "mov r11, [rcx+104]\nmov rsi, [rcx+112]\n"
    "test r14, r14\njz 1f\nmovups xmm0, [r14]\nmov [rsp+0x70], r13\n1:\njmp rax\n"
    ".globl RunBack\nRunBack:\n"
    "mov rcx, [rip+g_runRegs]\nmov [rcx+88], r13\nmov [rcx+96], rax\nmov [rcx+120], rbx\n"
    "add rsp, 0x208\npop r15\npop r14\npop r13\npop r12\npop rsi\npop rdi\npop rbp\npop rbx\nret\n"
    ".att_syntax\n");

#define WORLD 0x2800000u                       /* a free stretch of the image: the manager lies at +0x1FA08 */
#define MANAGER (WORLD + CO_SPAWNER_MANAGER)
#define MANAGER_SIZE 0x8C810u
#define COMMON 0x3116000u                      /* the way back out of the image */
enum { WAY_ADDED = 1, WAY_LOOP_DONE, WAY_EMPTY, WAY_BACK, WAY_LOOP, WAY_DONE };
#define SPANS 8
static uint8_t g_spans[SPANS][0x40];
static const uint32_t kSpans[SPANS] = {CO_SPAWNER_SITE - 8u, CO_SPAWNER_FIRST - 8u, CO_SPAWNER_PLACES - 8u, CO_SPAWNER_GROUPS_SITE - 8u,
    CO_SPAWNER_TASKS - 8u, CO_SPAWNER_SITES - 8u, CO_SPAWNER_ALARMS - 8u, CO_SPAWNER_PATROLS - 8u};

static void Land(uint8_t* image, uint32_t at, uint32_t way)
{
    const int32_t rel = (int32_t)(COMMON - (at + 10u));
    image[at] = 0xB8; memcpy(image + at + 1, &way, 4); image[at + 5] = 0xE9; memcpy(image + at + 6, &rel, 4);
}

static uint8_t* Reset(const char* name)
{
    /* a stand-in for the loop's 0x66F bytes: record entry r10d's unit at [r9], count on, go to the places */
    static const uint8_t body[] = {0x44, 0x89, 0xD0, 0x48, 0x6B, 0xD8, 0x58, 0x42, 0x8B, 0xB4, 0x2B, 0xE8, 0x2D, 0x08, 0x00, 0x41, 0x89, 0x34, 0x81, 0x41, 0xFF, 0xC2, 0xE9};
    static const uint8_t pad[6] = {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00}; /* the game's own, between the test and the loop */
    const uint8_t common[2] = {0x48, 0xB9}, go[2] = {0xFF, 0xE1}; /* mov rcx, RunBack; jmp rcx */
    const uint64_t back = (uint64_t)(uintptr_t)RunBack;
    DWORD old;
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_spawners_test_reset();
    uint8_t* image = g_fake.image;
    const uint64_t world = (uint64_t)(uintptr_t)image + WORLD;
    const int32_t rel = (int32_t)(CO_SPAWNER_PLACES - (CO_SPAWNER_LOOP + sizeof body + 4u));
    fake_stock_chains(image, 1022.0f / 1024.0f); /* HellForge's level 11, its spawner guard with it */
    memcpy(image + CO_SPAWNER_WORLD, &world, 8);
    memcpy(image + CO_SPAWNER_FIRST + 13u, pad, sizeof pad);
    memcpy(image + CO_SPAWNER_LOOP, body, sizeof body); memcpy(image + CO_SPAWNER_LOOP + sizeof body, &rel, 4);
    Land(image, CO_SPAWNER_DONE, WAY_ADDED); Land(image, CO_SPAWNER_LOOP_DONE, WAY_LOOP_DONE); Land(image, CO_SPAWNER_EMPTY, WAY_EMPTY);
    memcpy(image + COMMON, common, 2); memcpy(image + COMMON + 2, &back, 8); memcpy(image + COMMON + 10, go, 2);
    VirtualProtect(image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old);
    for (int i = 0; i < SPANS; i++) memcpy(g_spans[i], image + kSpans[i], sizeof g_spans[i]);
    return image;
}
static int Unchanged(void)
{
    for (int i = 0; i < SPANS; i++) if (memcmp(g_spans[i], g_fake.image + kSpans[i], sizeof g_spans[i]) != 0) return 0;
    return 1;
}
static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }

/* Mission load: the state change clears the manager, then its enter appends the map's spawners. */
static void Load(uint8_t* image, uint32_t spawners)
{
    memset(image + MANAGER, 0, MANAGER_SIZE);
    for (uint32_t i = 0; i < spawners; i++)
    {
        uint8_t pose[0x40];
        for (int k = 0; k < 0x40; k++) pose[k] = (uint8_t)(i * 7u + (uint32_t)k);
        Regs r = {.rbx = (uint64_t)(uintptr_t)image + MANAGER, .r13 = 0x1111000000000000ull + i, .r14 = (uint64_t)(uintptr_t)pose,
            .rdi = 0x2222000000000000ull + i, .r15 = 0xABCD0000u + i};
        Run(&r, (uintptr_t)image + CO_SPAWNER_SITE);
        if (r.outWay != WAY_ADDED) { CHECK(r.outWay == WAY_ADDED); return; }
    }
}
/* An entry as the game lays it out: key, pose, type, unit (its last four bytes are the stack's). */
static int Entry(const uint8_t* e, uint32_t i)
{
    uint8_t want[0x54];
    const uint64_t key = 0x1111000000000000ull + i, type = 0x2222000000000000ull + i;
    const uint32_t unit = 0xABCD0000u + i;
    memcpy(want, &key, 8);
    for (int k = 0; k < 0x40; k++) want[8 + k] = (uint8_t)(i * 7u + (uint32_t)k);
    memcpy(want + 0x48, &type, 8); memcpy(want + 0x50, &unit, 4);
    return memcmp(e, want, sizeof want) == 0;
}
static int Zero(const uint8_t* at, size_t size) { for (size_t i = 0; i < size; i++) if (at[i] != 0) return 0; return 1; }

static uint32_t g_units[1100];
static uint8_t g_group[0x300];
/* The builder's loop from its first test on. Returns how many entries it read; they are in g_units. */
static uint32_t Build(uint8_t* image, Regs* r)
{
    memset(g_units, 0, sizeof g_units); memset(g_group, 0, sizeof g_group);
    Regs regs = {.r13 = (uint64_t)(uintptr_t)image + MANAGER, .r8 = (uint64_t)(uintptr_t)g_group, .r9 = (uint64_t)(uintptr_t)g_units, .rdx = 0x77u};
    Run(&regs, (uintptr_t)image + CO_SPAWNER_FIRST);
    *r = regs;
    uint32_t n = 0;
    while (n < 1100 && g_units[n] != 0) n++;
    return n;
}
/* The builder's size code from the site to its alloc's call, which hands back rdx (the alloc's size) and rbx (the memset's). */
static void Size(uint8_t* image, uint64_t* alloc, uint64_t* zeroed)
{
    static const uint8_t back[6] = {0x48, 0x89, 0xD0, 0x49, 0x89, 0xDD}; /* mov rax, rdx; mov r13, rbx */
    const int32_t rel = (int32_t)(COMMON - (CO_SPAWNER_GROUPS_SITE + 23u + sizeof back + 5u));
    uint8_t call[sizeof back + 5];
    memcpy(call, image + CO_SPAWNER_GROUPS_SITE + 23u, sizeof call);
    memcpy(image + CO_SPAWNER_GROUPS_SITE + 23u, back, sizeof back);
    image[CO_SPAWNER_GROUPS_SITE + 23u + sizeof back] = 0xE9; memcpy(image + CO_SPAWNER_GROUPS_SITE + 23u + sizeof back + 1u, &rel, 4);
    Regs r = {.rbx = 0x5A5A5A5Au, .r8 = 0xFFFFFFFFFFFFFFFFull, .rdx = 0x5A5A5A5Au};
    Run(&r, (uintptr_t)image + CO_SPAWNER_GROUPS_SITE);
    memcpy(image + CO_SPAWNER_GROUPS_SITE + 23u, call, sizeof call);
    *alloc = r.outWay; *zeroed = r.outR13;
}
static int Units(uint32_t n) { for (uint32_t i = 0; i < n; i++) if (g_units[i] != 0xABCD0000u + i) return 0; return 1; }

/* A group's own lists, run as code: the game's bytes copied out (GAME_CODE, then its own way on) beside the mod's stub at
 * the site, from the same block. With room the stub stores what the game stores; full, or with no group, nothing in the
 * block changes but the counter, and the loop goes on as the game's would. */
#define GAME_CODE 0x2A00000u
static uint8_t g_block[4 * 0x300];
static uint8_t g_before[sizeof g_block], g_game[sizeof g_block];
static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
/* The game's first `size` bytes of `code`, then a jump to `back`, at GAME_CODE. */
static uintptr_t GameCopy(uint8_t* image, const uint8_t* code, size_t size, uint32_t back)
{
    const int32_t rel = (int32_t)(back - (GAME_CODE + size + 5u));
    memcpy(image + GAME_CODE, code, size); image[GAME_CODE + size] = 0xE9; memcpy(image + GAME_CODE + size + 1, &rel, 4);
    return (uintptr_t)image + GAME_CODE;
}
/* Both runs from the block as it is: 1 when the game's copy and the site leave the same block, ways and rbx. */
static int AsGame(uintptr_t gameEntry, uintptr_t site, const Regs* r, Regs* out)
{
    Regs a = *r, b = *r;
    memcpy(g_before, g_block, sizeof g_block);
    Run(&a, gameEntry);
    memcpy(g_game, g_block, sizeof g_block);
    memcpy(g_block, g_before, sizeof g_block);
    Run(&b, site);
    *out = b;
    return memcmp(g_game, g_block, sizeof g_block) == 0 && a.outWay == b.outWay && a.outRbx == b.outRbx && memcmp(g_game, g_before, sizeof g_block) != 0;
}
/* One run at the site that must leave the block as it was; the counter and the way it went are the caller's to check. */
static int Untouched(uintptr_t site, Regs* r)
{
    memcpy(g_before, g_block, sizeof g_block);
    Run(r, site);
    return memcmp(g_before, g_block, sizeof g_block) == 0;
}
static uint8_t g_managerKept[MANAGER_SIZE];
static void GroupLists(uint8_t* image)
{
    const uintptr_t game = (uintptr_t)image;
    uint8_t* manager = image + MANAGER;
    uint8_t* group = g_block + 0x300;
    uint8_t keep[10];
    Regs r, out;
    memcpy(keep, image + CO_SPAWNER_TASKS_BACK, sizeof keep);
    Land(image, CO_SPAWNER_TASKS_BACK, WAY_BACK);
    Land(image, 0x94AC00u, WAY_LOOP); Land(image, CO_SPAWNER_SITES_NEXT + 13u, WAY_DONE);
    Land(image, 0x94B1D0u, WAY_LOOP); Land(image, CO_SPAWNER_ALARMS + 55u, WAY_DONE);
    Land(image, 0x94B2A0u, WAY_LOOP); Land(image, CO_SPAWNER_PATROLS + 52u, WAY_DONE);
    memcpy(g_managerKept, manager, MANAGER_SIZE);
    memset(manager, 0, MANAGER_SIZE);
    const volatile long* tasks = co_spawners_skipped(CO_SPAWNER_CELL_TASKS), *sites = co_spawners_skipped(CO_SPAWNER_CELL_SITES);
    const volatile long* alarms = co_spawners_skipped(CO_SPAWNER_CELL_ALARMS), *patrols = co_spawners_skipped(CO_SPAWNER_CELL_PATROLS);
    CHECK(tasks != NULL && sites != NULL && alarms != NULL && patrols != NULL && co_spawners_skipped(CO_SPAWNER_CELL_FULL) == NULL);
    if (tasks == NULL || sites == NULL || alarms == NULL || patrols == NULL) return;

    g_case = "a group's tasks";
    memset(g_block, 0xA5, sizeof g_block);
    Put32(group + CO_SPAWNER_TASK_COUNT, 95);
    r = (Regs){.rbx = (uint64_t)(uintptr_t)group, .r8 = 0x1234567890ull, .rsi = 0x3F1};
    CHECK(AsGame(GameCopy(image, kCoSpawnerTasks, 19, CO_SPAWNER_TASKS_BACK), game + CO_SPAWNER_TASKS, &r, &out) && out.outWay == WAY_BACK);
    CHECK(U32(group + CO_SPAWNER_TASK_COUNT) == 96u && U32(group + 8 + 95 * 4) == 0x3F1u && *(uint64_t*)group == 0x1234567890ull && *tasks == 0);
    r.rsi = 0x3F2;
    CHECK(Untouched(game + CO_SPAWNER_TASKS, &r) && r.outWay == WAY_BACK && *tasks == 1); /* the 97th: the key is the group's already */
    Put32(group + CO_SPAWNER_TASK_COUNT, 0); memset(group, 0, 8);
    CHECK(AsGame(GameCopy(image, kCoSpawnerTasks, 19, CO_SPAWNER_TASKS_BACK), game + CO_SPAWNER_TASKS, &r, &out) && U32(group + 8) == 0x3F2u && *tasks == 1);

    g_case = "a group's invasion sites";
    memset(g_block, 0xA5, sizeof g_block);
    Put32(group + CO_SPAWNER_PLACE_COUNT, 15); Put32(manager + 0x86FA0u, 3);
    r = (Regs){.r8 = (uint64_t)(uintptr_t)group, .xmm4 = 0x4142434445464748ull, .rdx = 0x51525354u, .r13 = (uint64_t)(uintptr_t)manager, .r10 = 2};
    CHECK(AsGame(GameCopy(image, kCoSpawnerPlaces, 33, CO_SPAWNER_SITES_NEXT), game + CO_SPAWNER_SITES, &r, &out) && out.outWay == WAY_LOOP && *sites == 0);
    CHECK(U32(group + CO_SPAWNER_PLACE_COUNT) == 16u && *(uint64_t*)(group + 0x18C + 15 * 12) == 0x4142434445464748ull && U32(group + 0x18C + 15 * 12 + 8) == 0x51525354u);
    r.r10 = 3;
    CHECK(Untouched(game + CO_SPAWNER_SITES, &r) && r.outWay == WAY_DONE && *sites == 1); /* the 17th */

    g_case = "a group's alarms";
    memset(g_block, 0xA5, sizeof g_block);
    for (uint32_t i = 0; i < 4; i++) Put32(manager + 0x86FA8u + 16u * i, 0xA1A10000u + i);
    Put32(manager + 0x877A8u, 4);
    Put32(group + CO_SPAWNER_ALARM_COUNT, 31);
    r = (Regs){.rdi = (uint64_t)(uintptr_t)g_block, .rdx = 1, .rbx = 2, .r11 = 4, .r13 = (uint64_t)(uintptr_t)manager};
    CHECK(AsGame(GameCopy(image, kCoSpawnerAlarms, 42, CO_SPAWNER_ALARMS_NEXT), game + CO_SPAWNER_ALARMS, &r, &out) && out.outWay == WAY_LOOP && out.outRbx == 3u);
    CHECK(U32(group + CO_SPAWNER_ALARM_COUNT) == 32u && U32(group + 0x250 + 31 * 4) == 0xA1A10002u && *alarms == 0);
    r.rbx = 3; r.r11 = 6;
    CHECK(Untouched(game + CO_SPAWNER_ALARMS, &r) && r.outWay == WAY_DONE && r.outRbx == 4u && *alarms == 1); /* the 33rd */
    r = (Regs){.rdi = (uint64_t)(uintptr_t)g_block, .rdx = 0xFFFFFFFFu, .rbx = 0, .r11 = 0, .r13 = (uint64_t)(uintptr_t)manager};
    CHECK(Untouched(game + CO_SPAWNER_ALARMS, &r) && r.outWay == WAY_LOOP && r.outRbx == 1u && *alarms == 2); /* no group */

    g_case = "a group's patrol spawner";
    memset(g_block, 0xA5, sizeof g_block);
    for (uint32_t i = 0; i < 3; i++) Put32(manager + 0x330u + 16u * i, 0xB2B20000u + i);
    Put32(manager + 0x730u, 3);
    Put32(group + CO_SPAWNER_PATROL_COUNT, 0);
    r = (Regs){.rdi = (uint64_t)(uintptr_t)g_block, .rdx = 1, .rbx = 1, .r10 = 2u * (1u + 0x33u), .r13 = (uint64_t)(uintptr_t)manager};
    CHECK(AsGame(GameCopy(image, kCoSpawnerPatrols, 39, CO_SPAWNER_PATROLS_NEXT), game + CO_SPAWNER_PATROLS, &r, &out) && out.outWay == WAY_LOOP && out.outRbx == 2u);
    CHECK(U32(group + CO_SPAWNER_PATROL_COUNT) == 1u && U32(group + 0x2D4) == 0xB2B20001u && *patrols == 0);
    r.rbx = 2; r.r10 = 2u * (2u + 0x33u);
    CHECK(Untouched(game + CO_SPAWNER_PATROLS, &r) && r.outWay == WAY_DONE && r.outRbx == 3u && *patrols == 1); /* the second */
    r = (Regs){.rdi = (uint64_t)(uintptr_t)g_block, .rdx = 0xFFFFFFFFu, .rbx = 0, .r10 = 2u * 0x33u, .r13 = (uint64_t)(uintptr_t)manager};
    CHECK(Untouched(game + CO_SPAWNER_PATROLS, &r) && r.outWay == WAY_LOOP && r.outRbx == 1u && *patrols == 2); /* no group */

    memcpy(image + CO_SPAWNER_TASKS_BACK, keep, sizeof keep);
    memcpy(manager, g_managerKept, MANAGER_SIZE);
}

int main(void)
{
    static uint8_t gameList[0x54], gameGroup[sizeof g_group]; /* an entry less its last four bytes, which are the stack's */
    uint8_t* image = Reset("HellForge's level 11 alone");
    uintptr_t game = (uintptr_t)image;
    Regs r;
    CHECK(co_level_limit(CO_LIMIT_SPAWNERS, 0) == 128u && co_level_limit(CO_LIMIT_SPAWNERS, 1) == 1024u && U32(kCoSpawnerGuard + 7) == 128u);
    CHECK(co_spawners_check(game) == NULL && g_fake.writes == 0 && co_spawners_places_full() == NULL);
    co_spawners_mission(1); /* nothing before the install */
    /* what the game does with HellForge's guard and no mod: 128 of 130, two counted, the loop over the 128 */
    Load(image, 130);
    CHECK(U32(image + MANAGER + CO_SPAWNER_COUNT) == 128u && U32(image + FAKE_STOCK_SPAWNERS + 0x40u) == 2u);
    for (uint32_t i = 0; i < 128; i++) CHECK(Entry(image + MANAGER + CO_SPAWNER_ENTRIES + i * CO_SPAWNER_ENTRY, i));
    memcpy(gameList, image + MANAGER + CO_SPAWNER_ENTRIES, sizeof gameList);
    CHECK(Build(image, &r) == 128u && Units(128) && r.outWay == WAY_LOOP_DONE && r.outR13 == game + MANAGER);
    memcpy(gameGroup, g_group, sizeof gameGroup); /* the game's 128 unchecked stores into a group's 16 places */

    g_case = "the mod beside it";
    const char* why = co_spawners_install(game);
    if (why != NULL) printf("install: %s (%d writes)\n", why, g_fake.writes);
    CHECK(why == NULL && g_fake.writes == 9 && g_fake.hookCount == 0);
    uint8_t* page = co_spawners_test_page();
    CHECK(page != NULL && co_spawners_places_full() != NULL);
    if (page == NULL) { printf("spawners: no page\n"); return 1; }
    const uint8_t* cells = page + CO_SPAWNER_CELLS; const uint8_t* list = page + CO_SPAWNER_LIST;
    const volatile long* placesFull = co_spawners_places_full();
    {
        const struct { uint32_t rva, size, stub; } sites[8] = {{CO_SPAWNER_SITE, 6, CO_SPAWNER_STUB_APPEND}, {CO_SPAWNER_FIRST, 13, CO_SPAWNER_STUB_FIRST},
            {CO_SPAWNER_NEXT, 13, CO_SPAWNER_STUB_NEXT}, {CO_SPAWNER_PLACES, 33, CO_SPAWNER_STUB_PLACES}, {CO_SPAWNER_TASKS, 19, CO_SPAWNER_STUB_TASKS},
            {CO_SPAWNER_SITES, 33, CO_SPAWNER_STUB_SITES}, {CO_SPAWNER_ALARMS, 23, CO_SPAWNER_STUB_ALARMS}, {CO_SPAWNER_PATROLS, 20, CO_SPAWNER_STUB_PATROLS}};
        for (int i = 0; i < 8; i++)
        {
            CHECK(image[sites[i].rva] == 0xE9 && Leads(image + sites[i].rva + 1) == (uintptr_t)page + sites[i].stub);
            for (uint32_t k = 5; k < sites[i].size; k++) CHECK(image[sites[i].rva + k] == 0x90);
        }
        /* the append's stub: the count's load first, and one jump to HellForge's stub, which is as it was */
        CHECK(memcmp(page, kCoSpawnerPin, 6) == 0 && page[0x1F] == 0xE9 && Leads(page + 0x20) == game + FAKE_STOCK_SPAWNERS);
        CHECK(memcmp(image + FAKE_STOCK_SPAWNERS, kCoSpawnerGuard, 14) == 0 && Leads(image + FAKE_STOCK_SPAWNERS + 14) == game + CO_SPAWNER_ADD);
        CHECK(*(const uint64_t*)(cells + CO_SPAWNER_CELL_BASED) == (uint64_t)(uintptr_t)list - CO_SPAWNER_ENTRIES);
        CHECK(cells[CO_SPAWNER_CELL_FLAG] == 0 && cells[CO_SPAWNER_CELL_OWN] == 0);
        /* the group block: mov r8d, 3136 over the capacity's load and its doubling, the size code after it the game's */
        const uint32_t groups = 2048u + 1024u + 64u;
        uint64_t alloc = 0, zeroed = 0;
        CHECK(co_spawners_groups() == groups && CO_SPAWNER_GROUP_TASKS == 2048u && CO_SPAWNER_GROUP_INVASIONS == 64u);
        CHECK(co_spawners_groups() >= CO_SPAWNER_GROUP_TASKS + co_level_limit(CO_LIMIT_SPAWNERS, 0) + CO_SPAWNER_GROUP_INVASIONS);
        CHECK(image[CO_SPAWNER_GROUPS_SITE] == 0x41 && image[CO_SPAWNER_GROUPS_SITE + 1] == 0xB8 && U32(image + CO_SPAWNER_GROUPS_SITE + 2) == groups);
        CHECK(memcmp(image + CO_SPAWNER_GROUPS_SITE + 6, kCoSpawnerGroupsPin + 6, sizeof kCoSpawnerGroupsPin - 6) == 0);
        CHECK(memcmp(image + CO_SPAWNER_GROUPS_SITE - 8, g_spans[3], 8) == 0);
        Size(image, &alloc, &zeroed);
        CHECK(alloc == (uint64_t)groups * CO_SPAWNER_GROUP_SIZE && zeroed == alloc && alloc == 0x24C000u);
        CHECK(fake_host_logged("complex operations: GuardForce's group block holds 3136 groups on every level (2352 KB of scratch at mission load)"));
        CHECK(fake_host_logged("a GuardForce group keeps at most 96 tasks, 16 positions, 32 alarms and 1 patrol spawner on every level"));
        /* what the game's code behind each site reads is the game's, past the bytes replaced */
        CHECK(memcmp(image + CO_SPAWNER_TASKS + 19, kCoSpawnerTasks + 19, 4) == 0 && memcmp(image + CO_SPAWNER_SITES_NEXT, kCoSpawnerSitesNext, 13) == 0);
        CHECK(memcmp(image + CO_SPAWNER_ALARMS_ADD, kCoSpawnerAlarms + 23, 32) == 0 && memcmp(image + CO_SPAWNER_PATROLS_ADD, kCoSpawnerPatrols + 20, 32) == 0);
    }
    GroupLists(image);
    const int writes = g_fake.writes;
    CHECK(co_spawners_install(game) == NULL && g_fake.writes == writes); /* once */
    /* the count of the mission loaded before the install is the manager's list's: the loop is the game's */
    CHECK(Build(image, &r) == 128u && Units(128) && r.outWay == WAY_LOOP_DONE && r.outR13 == game + MANAGER && memcmp(g_group, gameGroup, sizeof g_group) == 0);

    g_case = "a normal mission";
    co_spawners_mission(0);
    Load(image, 130);
    CHECK(U32(image + MANAGER + CO_SPAWNER_COUNT) == 128u && U32(image + FAKE_STOCK_SPAWNERS + 0x40u) == 4u); /* HellForge's guard, its counter */
    for (uint32_t i = 0; i < 128; i++) CHECK(Entry(image + MANAGER + CO_SPAWNER_ENTRIES + i * CO_SPAWNER_ENTRY, i));
    CHECK(Zero(list, 1024u * CO_SPAWNER_ENTRY));
    CHECK(U32(cells + CO_SPAWNER_CELL_LEFT) == 0u && cells[CO_SPAWNER_CELL_OWN] == 0);
    CHECK(Build(image, &r) == 128u && Units(128) && r.outWay == WAY_LOOP_DONE && r.outR13 == game + MANAGER);
    CHECK(memcmp(g_group, gameGroup, sizeof g_group) == 0 && *placesFull == 0); /* the places too: no test the game has not */

    g_case = "a complex mission";
    co_spawners_mission(1); /* its level init, then the state change */
    Load(image, 1030);
    CHECK(U32(image + MANAGER + CO_SPAWNER_COUNT) == 1024u && U32(cells + CO_SPAWNER_CELL_LEFT) == 6u && U32(image + FAKE_STOCK_SPAWNERS + 0x40u) == 4u);
    CHECK(cells[CO_SPAWNER_CELL_OWN] == 1 && Zero(image + MANAGER + CO_SPAWNER_ENTRIES, 128u * CO_SPAWNER_ENTRY)); /* the manager's list is not written */
    for (uint32_t i = 0; i < 1024; i++) CHECK(Entry(list + i * CO_SPAWNER_ENTRY, i));
    CHECK(memcmp(list, gameList, 0x54) == 0); /* an entry is the game's own */
    CHECK(Build(image, &r) == 1024u && Units(1024) && r.outWay == WAY_LOOP_DONE && r.outR13 == game + MANAGER);
    /* a group takes 16 positions; the rest are counted, and the count behind the places is whole */
    CHECK(U32(g_group + 0x24C) == 16u && *placesFull == 1024 - 16 && U32(g_group + 0x18C + 15 * 12 + 8) == 0x77u && Zero(g_group + 0x250, 0xB0));
    /* on the ship, and when the next level is set up, the count is still this mission's and so is the list */
    co_spawners_mission(0);
    CHECK(fake_host_logged("the mission's spawner list held 1024 of 1024; so far 6 left out past it and 1008 positions past a group's 16 places"));
    CHECK(Build(image, &r) == 1024u && Units(1024) && r.outR13 == game + MANAGER);
    g_fake.log[0] = 0;
    co_spawners_mission(0);
    CHECK(g_fake.log[0] == 0); /* said once */

    g_case = "a normal mission after a complex one";
    memset(image + MANAGER, 0, MANAGER_SIZE);
    CHECK(Build(image, &r) == 0u && r.outWay == WAY_EMPTY && r.outR13 == game + MANAGER); /* cleared: no spawner yet */
    Load(image, 5);
    CHECK(U32(image + MANAGER + CO_SPAWNER_COUNT) == 5u && cells[CO_SPAWNER_CELL_OWN] == 0 && Entry(image + MANAGER + CO_SPAWNER_ENTRIES + 4 * CO_SPAWNER_ENTRY, 4));
    CHECK(Entry(list + 5 * CO_SPAWNER_ENTRY, 5) && U32(cells + CO_SPAWNER_CELL_LEFT) == 6u); /* the mod's list is left as it was */
    CHECK(Build(image, &r) == 5u && Units(5) && r.outWay == WAY_LOOP_DONE && r.outR13 == game + MANAGER && U32(g_group + 0x24C) == 5u);
    /* the next mission's level init comes while this count stands: nothing changes until the manager is cleared */
    co_spawners_mission(1);
    CHECK(Build(image, &r) == 5u && Units(5) && cells[CO_SPAWNER_CELL_OWN] == 0);
    {
        uint8_t pose[0x40] = {0};
        Regs a = {.rbx = (uint64_t)game + MANAGER, .r13 = 9, .r14 = (uint64_t)(uintptr_t)pose, .rdi = 9, .r15 = 9};
        Run(&a, game + CO_SPAWNER_SITE);
        CHECK(a.outWay == WAY_ADDED && U32(image + MANAGER + CO_SPAWNER_COUNT) == 6u && U32(image + MANAGER + CO_SPAWNER_ENTRIES + 5 * CO_SPAWNER_ENTRY + 0x50) == 9u);
    }
    Load(image, 200);
    CHECK(cells[CO_SPAWNER_CELL_OWN] == 1 && U32(image + MANAGER + CO_SPAWNER_COUNT) == 200u && Build(image, &r) == 200u && Units(200) && r.outR13 == game + MANAGER);

    /* refusals: nothing is written */
    {
        const uint32_t other[] = {FAKE_STOCK_SPAWNERS + 7u, FAKE_STOCK_SPAWNERS + 12u, FAKE_STOCK_SPAWNERS + 14u, FAKE_STOCK_SPAWNERS + 19u, FAKE_STOCK_SPAWNERS + 26u,
            CO_SPAWNER_SITE, CO_SPAWNER_SITE + 5u, CO_SPAWNER_ADD + 11u, CO_SPAWNER_ADD + 120u, CO_SPAWNER_BASE_LOAD + 3u, CO_SPAWNER_BASE_ADD + 3u,
            CO_SPAWNER_FIRST + 3u, CO_SPAWNER_FIRST + 9u, CO_SPAWNER_NEXT + 1u, CO_SPAWNER_NEXT + 12u, CO_SPAWNER_PLACES + 3u, CO_SPAWNER_PLACES + 32u,
            CO_SPAWNER_GROUPS_SITE, CO_SPAWNER_GROUPS_SITE + 5u, CO_SPAWNER_GROUPS_SITE + 11u, CO_SPAWNER_GROUPS_SITE + 24u, CO_SPAWNER_GROUPS_SITE + 48u,
            CO_SPAWNER_TASKS + 1u, CO_SPAWNER_TASKS + 18u, CO_SPAWNER_TASKS + 22u, CO_SPAWNER_SITES + 3u, CO_SPAWNER_SITES + 32u, CO_SPAWNER_SITES_NEXT + 3u,
            CO_SPAWNER_SITES_NEXT + 12u, CO_SPAWNER_ALARMS, CO_SPAWNER_ALARMS + 12u, CO_SPAWNER_ALARMS + 25u, CO_SPAWNER_ALARMS + 54u, CO_SPAWNER_PATROLS,
            CO_SPAWNER_PATROLS + 12u, CO_SPAWNER_PATROLS + 22u, CO_SPAWNER_PATROLS + 51u};
        for (size_t i = 0; i < sizeof other / sizeof other[0]; i++)
        {
            image = Reset("a site or HellForge's guard is another");
            image[other[i]] ^= 0x04;
            for (int k = 0; k < SPANS; k++) memcpy(g_spans[k], image + kSpans[k], sizeof g_spans[k]);
            CHECK(co_spawners_check((uintptr_t)image) != NULL && co_spawners_install((uintptr_t)image) != NULL && Unchanged() && g_fake.writes == 0 && co_spawners_test_page() == NULL);
        }
        image = Reset("no guard of HellForge's");
        memcpy(image + CO_SPAWNER_SITE, kCoSpawnerPin, 6);
        memcpy(g_spans[0], image + kSpans[0], sizeof g_spans[0]);
        CHECK(co_spawners_check((uintptr_t)image) != NULL && strstr(co_spawners_check((uintptr_t)image), "not in place") != NULL);
        CHECK(co_spawners_install((uintptr_t)image) != NULL && Unchanged() && g_fake.writes == 0);
    }
    for (int fail = 0; fail < 9; fail++)
    {
        image = Reset("a write is refused");
        g_fake.failWriteAt = fail;
        CHECK(co_spawners_install((uintptr_t)image) != NULL && Unchanged() && co_spawners_test_page() == NULL && co_spawners_places_full() == NULL &&
            co_spawners_skipped(CO_SPAWNER_CELL_ALARMS) == NULL);
        CHECK(!fake_host_logged("group block holds"));
        /* and the game with HellForge's guard runs as before */
        Load(image, 130);
        CHECK(U32(image + MANAGER + CO_SPAWNER_COUNT) == 128u && Build(image, &r) == 128u && Units(128) && r.outR13 == (uintptr_t)image + MANAGER);
    }

    if (g_failed) { printf("spawners: %d checks failed\n", g_failed); return 1; }
    printf("spawners: all cases passed\n");
    return 0;
}
