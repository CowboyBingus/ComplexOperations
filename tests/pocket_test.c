/* FixUnreachableAreas on a complex level: the reconnect on a made-up cell graph, the four stubs' bytes and targets and
 * what each does when run for a normal, a HellForge and a complex level, and an install that is all or none. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_chain.h"
#include "xl/co_normal.h"
#include "xl/co_pocket.h"
#include "xl/co_pocket_sites.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define CODE 0x3200000u /* where a built page runs: a free place in the image */
#define FLAG 0x3201F00u
#define NEXT 0x3202000u /* stands for the chain's restart stub */
#define PINS (sizeof kCoPocketPins / sizeof kCoPocketPins[0])

/* ---- a cell graph as the pass has it ------------------------------------------------------------------------------ */

#define CELLS 10u
#define NONE 0xFFFFFFFFu
static uint8_t g_cells[CELLS * 0x110], g_subs[CELLS * 0x20], g_routes[32][0x30], g_typeOpen[0x180], g_typeShut[0x180];
static uint32_t g_ids[CELLS];
static uint64_t g_types[3];
static int g_routeCount, g_diskCalls;

static uint64_t CellAt(uint32_t i) { return (uint64_t)(uintptr_t)(g_cells + i * 0x110); }
static uint8_t* Sub(uint32_t i) { return g_subs + i * 0x20; }
static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static void Put64(uint8_t* at, uint64_t v) { memcpy(at, &v, 8); }
static uint32_t Get32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }

/* The test's disk: a subregion is outside when its +0x10, which no join writes, is set. */
static char InDisk(uint64_t ctx, uint64_t sub) { (void)ctx; g_diskCalls++; return Get32((const uint8_t*)(uintptr_t)sub + 0x10) == 0; }

static void Cell(uint32_t i, uint32_t type, uint32_t id)
{
    Put32(Sub(i), type); Put32(Sub(i) + 4, 0x1000u + i); Put32(Sub(i) + 8, 0x2000u + i); Put32(Sub(i) + 0xC, 0);
    Put64(Sub(i) + 0x18, CellAt(i));
    g_ids[i] = id;
}
static void Link(uint32_t a, uint32_t b)
{
    uint8_t* route = g_routes[g_routeCount++];
    Put64(route + 0x20, CellAt(a)); Put64(route + 0x28, CellAt(b));
    for (int side = 0; side < 2; side++)
    {
        uint8_t* cell = g_cells + (side ? b : a) * 0x110;
        int k = 0;
        while (*(uint64_t*)(cell + 8 + 8 * k) != 0) k++;
        Put64(cell + 8 + 8 * k, (uint64_t)(uintptr_t)route);
    }
}
/* Pocket 0 (component 1), blocked 1 and 2, main 3 (component 0) in a row; a way round over blocked 4, 5 and 6. */
static CoPocketGraph Graph(const char* name, int row, int round)
{
    CoPocketGraph g = {(uint64_t)(uintptr_t)g_cells, (uint64_t)(uintptr_t)g_subs, (uint64_t)(uintptr_t)g_ids, (uint64_t)(uintptr_t)g_types, 0, CELLS, 0, InDisk};
    g_case = name;
    memset(g_cells, 0, sizeof g_cells); memset(g_subs, 0, sizeof g_subs); memset(g_routes, 0, sizeof g_routes);
    memset(g_typeOpen, 0, sizeof g_typeOpen); memset(g_typeShut, 0, sizeof g_typeShut);
    g_typeOpen[0x170] = 0x20; g_typeShut[0x170] = 0x18;
    g_types[0] = (uint64_t)(uintptr_t)g_typeOpen; g_types[1] = (uint64_t)(uintptr_t)g_typeShut; g_types[2] = 0;
    g_routeCount = 0; g_diskCalls = 0;
    Cell(0, 9, 1); Cell(1, 2, NONE); Cell(2, 3, NONE); Cell(3, 7, 0);
    Cell(4, 1, NONE); Cell(5, 6, NONE); Cell(6, 2, NONE);
    Cell(7, 8, 0); Cell(8, 4, NONE); Cell(9, 4, NONE);      /* more of the main area and two blocked cells apart */
    Link(3, 7);
    if (row) { Link(0, 1); Link(1, 2); Link(2, 3); }
    if (round) { Link(0, 4); Link(4, 5); Link(5, 6); Link(6, 3); }
    return g;
}
static uint8_t g_subsBefore[sizeof g_subs];
static uint32_t g_idsBefore[CELLS];
static void Keep(void) { memcpy(g_subsBefore, g_subs, sizeof g_subs); memcpy(g_idsBefore, g_ids, sizeof g_ids); }
static int Kept(void) { return memcmp(g_subsBefore, g_subs, sizeof g_subs) == 0 && memcmp(g_idsBefore, g_ids, sizeof g_ids) == 0; }
/* A path cell: the source's first 16 bytes, its own route cell, the main's id. */
static int Painted(uint32_t i, uint32_t source)
{
    return memcmp(Sub(i), g_subsBefore + source * 0x20, 16) == 0 && memcmp(Sub(i) + 0x10, g_subsBefore + i * 0x20 + 0x10, 16) == 0 && g_ids[i] == 0;
}
static int Untouched(uint32_t i) { return memcmp(Sub(i), g_subsBefore + i * 0x20, 0x20) == 0 && g_ids[i] == g_idsBefore[i]; }

static void Reconnect(void)
{
    uint32_t searched = 77;
    CoPocketGraph g = Graph("joined by a path", 1, 0);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, &searched) == 2 && searched == 3);
    CHECK(g_ids[0] == 0 && Untouched(3) && Untouched(7) && Painted(1, 0) && Painted(2, 0) && Get32(Sub(1)) == 9);
    CHECK(memcmp(Sub(0), g_subsBefore, 0x20) == 0 && Untouched(8) && Untouched(4));

    g = Graph("no path", 0, 0);
    Link(0, 1); Link(1, 2); Link(8, 9);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, &searched) == -1 && searched == 3 && Kept());

    g = Graph("the shorter of two paths", 1, 1);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == 2 && Painted(1, 0) && Painted(2, 0) && Untouched(4) && Untouched(5) && Untouched(6));

    g = Graph("a location cell blocks", 1, 0);
    Put64(g_cells + 2 * 0x110 + 0x98, 0x1234);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, &searched) == -1 && searched == 2 && Kept());
    g = Graph("a location cell is gone round", 1, 1);
    Put64(g_cells + 1 * 0x110 + 0x98, 0x1234);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == 3 && Untouched(1) && Untouched(2) && Painted(4, 0) && Painted(5, 0) && Painted(6, 0) && g_ids[0] == 0);

    g = Graph("the pocket's own location cell starts the search", 1, 0);
    Put64(g_cells + 0x98, 0x1234);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == 2 && Painted(1, 0) && Painted(2, 0));

    g = Graph("a mismatched subregion is left out", 1, 0);
    Put64(Sub(1) + 0x18, CellAt(8));
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, &searched) == -1 && searched == 1 && Kept() && g_diskCalls == 0);
    g = Graph("a mismatched subregion is gone round", 1, 1);
    Put64(Sub(2) + 0x18, 0x10);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == 3 && Untouched(1) && Untouched(2) && Painted(5, 0));
    g = Graph("a pocket cell whose subregion names another cell starts nothing", 1, 0);
    Put64(Sub(0) + 0x18, CellAt(8));
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1 && Kept());

    g = Graph("a blocked cell outside the disk carries no path", 1, 0);
    Put32(Sub(1) + 0x10, 1);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1 && Kept());
    g = Graph("an open type outside the disk is a blocked cell", 0, 0);
    Link(0, 1); Link(1, 8); Link(8, 3);
    Put32(Sub(8), 9); Put32(Sub(8) + 0x10, 1);              /* open by its type, outside: blocked and not passable */
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1 && Kept());

    g = Graph("the planet's flag: a type object without bit 5 blocks", 1, 0);
    g.planet = 1;
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == 2);         /* type object 0 has the bit */
    g = Graph("the planet's flag: the main cell is blocked", 1, 0);
    g.planet = 1;
    Put32(Sub(3) + 0xC, 1);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == 3 && Painted(3, 0) && Untouched(7)); /* 3 carries the path to 7 */
    g = Graph("the planet's flag: no type object", 1, 0);
    g.planet = 1;
    Put32(Sub(3) + 0xC, 2); Put32(Sub(7) + 0xC, 2);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1 && Kept());
    g = Graph("no planet's flag: the type objects are not read", 1, 0);
    Put32(Sub(3) + 0xC, 0x7FFFFFFF);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == 2);

    g = Graph("an open cell of another component is not crossed", 0, 0);
    Link(0, 1); Link(1, 8); Link(8, 3);
    Put32(Sub(8), 9); g_ids[8] = 5;
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1 && Kept());

    g = Graph("the pocket's last cell is the source", 1, 0);
    Cell(9, 11, 1); Link(0, 9);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, &searched) == 2 && searched == 4 && Painted(1, 9) && Painted(2, 9) && Get32(Sub(2)) == 11 && g_ids[0] == 0 && g_ids[9] == 0);

    g = Graph("no main area", 1, 0);
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, NONE, NULL) == -1 && co_pocket_reconnect(&g, 1, 1, NULL) == -1 && Kept());
    g.count = 0;
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1);
    g.count = CO_POCKET_CELLS_MAX + 1u;
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1 && Kept());

    g = Graph("routes and arrays that cannot be read", 1, 1);
    Put64(g_cells + 1 * 0x110 + 8 + 8, 0x10);               /* cell 1's second route */
    Put64(g_routes[0] + 0x28, 0x20);                        /* a route's other cell that is none of the cells */
    Keep();
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == 3 && Untouched(1) && Painted(4, 0));
    g.ids = 0x10;
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1);
    g.ids = (uint64_t)(uintptr_t)g_ids; g.subregions = 0x10;
    CHECK(co_pocket_reconnect(&g, 1, 0, NULL) == -1);
}

/* ---- the stubs, run ----------------------------------------------------------------------------------------------- */

/* A stub entered as the pass enters it: the registers it reads, a 16-aligned stack whose +0x78 and +0x64 are the
 * pass's slots, and a way on that returns here. What comes back: rax, rdx, r8, rcx and the registers the pass keeps. */
typedef struct Regs
{
    uint64_t rbp, r12, r13, r14, r15, rbx, rdi, rcx, slot78, slot64, stub;  /* 0x00 .. 0x50 */
    uint64_t rax, rdx, r8, rcxOut, rbxOut, rdiOut, r12Out, r13Out, rbpOut;  /* 0x58 .. 0x98 */
} Regs;
void RunStub(Regs* regs);
__asm__(".text\n.p2align 4\n.globl RunStub\nRunStub:\n"
        "  pushq %rbx\n  pushq %rdi\n  pushq %rsi\n  pushq %rbp\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
        "  subq $0x100, %rsp\n  movq %rcx, %rsi\n"
        "  movq 0x40(%rsi), %rax\n  movq %rax, 0x70(%rsp)\n  movl 0x48(%rsi), %eax\n  movl %eax, 0x5C(%rsp)\n"
        "  movq (%rsi), %rbp\n  movq 0x08(%rsi), %r12\n  movq 0x10(%rsi), %r13\n  movq 0x18(%rsi), %r14\n  movq 0x20(%rsi), %r15\n"
        "  movq 0x28(%rsi), %rbx\n  movq 0x30(%rsi), %rdi\n  movq 0x38(%rsi), %rcx\n  xorl %edx, %edx\n  xorl %r8d, %r8d\n"
        "  callq *0x50(%rsi)\n"
        "  movq %rax, 0x58(%rsi)\n  movq %rdx, 0x60(%rsi)\n  movq %r8, 0x68(%rsi)\n  movq %rcx, 0x70(%rsi)\n"
        "  movq %rbx, 0x78(%rsi)\n  movq %rdi, 0x80(%rsi)\n  movq %r12, 0x88(%rsi)\n  movq %r13, 0x90(%rsi)\n  movq %rbp, 0x98(%rsi)\n"
        "  addq $0x100, %rsp\n"
        "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbp\n  popq %rsi\n  popq %rdi\n  popq %rbx\n  ret\n");

static uint32_t Leads(const uint8_t* image, uint32_t at) { int32_t d; memcpy(&d, image + at, 4); return at + 4u + (uint32_t)d; }
static void Way(uint8_t* image, uint32_t at, uint32_t mark)
{
    static const uint8_t code[6] = {0xB8, 0, 0, 0, 0, 0xC3}; /* mov eax, mark; ret */
    memcpy(image + at, code, sizeof code); memcpy(image + at + 1, &mark, 4);
}

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_chain_test_reset();
    co_pocket_test_reset();
    fake_stock_chains(g_fake.image, 1022.0f / 1024.0f);
    return g_fake.image;
}

static void Stubs(void)
{
    uint8_t* image = Reset("the page");
    const uintptr_t game = (uintptr_t)image;
    const float key = co_disk_key(), hellforge = 1022.0f / 1024.0f, lower = 0.5f;
    const uint8_t* s = image + CODE;
    DWORD old;
    co_pocket_build(image + CODE, game + CODE, game, game + FLAG, game + NEXT);
    /* the search: the game's compare first, a free cell on at once; the switch, the level's disk, the game's two ways */
    CHECK(memcmp(s, kCoPocketSites[0].bytes, 8) == 0 && s[8] == 0x74 && 10u + s[9] == 37u);
    CHECK(s[10] == 0x80 && s[11] == 0x3D && s[16] == 0 && Leads(image, CODE + 12u) + 1u == FLAG && s[17] == 0x74 && 19u + s[18] == 42u);
    CHECK(s[19] == 0x50 && s[34] == 0x58 && memcmp(s + 30, &key, 4) == 0 && s[35] == 0x75 && 37u + s[36] == 42u);
    CHECK(s[37] == 0xE9 && Leads(image, CODE + 38u) == CO_POCKET_SEARCH_GO && s[42] == 0xE9 && Leads(image, CODE + 43u) == CO_POCKET_SEARCH_SKIP && s[47] == 0xCC);
    CHECK(CO_POCKET_SEARCH_GO == CO_POCKET_SEARCH + 14u && kCoPocketSites[0].on == CO_POCKET_SEARCH_GO);
    /* the list's two sizes: the game's instruction, the level's disk, the larger value, the way on */
    s = image + CODE + CO_POCKET_AT_ALLOC;
    CHECK(memcmp(s, kCoPocketSites[1].bytes, 5) == 0 && memcmp(s + 12, &key, 4) == 0 && s[16] == 0x75 && 18u + s[17] == 23u && s[18] == 0xBA);
    CHECK(Get32(s + 19) == 0x1000u && s[23] == 0xE9 && Leads(image, CODE + CO_POCKET_AT_ALLOC + 24u) == CO_POCKET_ALLOC + 5u && s[28] == 0xCC);
    s = image + CODE + CO_POCKET_AT_CLEAR;
    CHECK(memcmp(s, kCoPocketSites[2].bytes, 6) == 0 && memcmp(s + 13, &key, 4) == 0 && s[17] == 0x75 && 19u + s[18] == 25u && s[19] == 0x41 && s[20] == 0xB8);
    CHECK(Get32(s + 21) == 0x1000u && s[25] == 0xE9 && Leads(image, CODE + CO_POCKET_AT_CLEAR + 26u) == CO_POCKET_CLEAR + 6u && s[30] == 0xCC);
    CHECK(CO_POCKET_LIST_GAME == 0x100u && CO_POCKET_LIST_COMPLEX == 512u * 8u);
    /* the restart: the switch and the disk, the call in a frame of 0x40 with the two stack arguments, the two ways */
    s = image + CODE + CO_POCKET_AT_RESTART;
    CHECK(s[0] == 0x80 && s[1] == 0x3D && s[6] == 0 && Leads(image, CODE + CO_POCKET_AT_RESTART + 2u) + 1u == FLAG && s[7] == 0x74 && 9u + s[8] == 76u);
    CHECK(s[9] == 0x81 && s[10] == 0xB9 && Get32(s + 11) == 0x2D7Cu && memcmp(s + 15, &key, 4) == 0 && s[19] == 0x75 && 21u + s[20] == 76u);
    CHECK(s[24] == 0x51 && s[28] == 0x38 && s[65] == 0x38 && s[66] == 0x59 && s[60] == 0xFF && s[61] == 0xD0);
    { uint64_t to; memcpy(&to, s + 52, 8); CHECK(to == (uint64_t)(uintptr_t)co_pocket_frame); }
    CHECK(s[69] == 0x74 && 71u + s[70] == 76u && s[71] == 0xE9 && Leads(image, CODE + CO_POCKET_AT_RESTART + 72u) == CO_POCKET_GO_ON);
    CHECK(s[76] == 0xE9 && Leads(image, CODE + CO_POCKET_AT_RESTART + 77u) == NEXT && s[81] == 0xCC);

    g_case = "the stubs, run";
    uint8_t* level = (uint8_t*)calloc(1, 0x10000);
    uint8_t settings[0x58] = {0}, frame[0x200] = {0};
    uint64_t ctx[4] = {0, (uint64_t)(uintptr_t)settings, 0, (uint64_t)(uintptr_t)level};
    Regs r;
    CHECK(VirtualProtect(image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old));
    image[CO_POCKET_ALLOC + 5u] = 0xC3; image[CO_POCKET_CLEAR + 6u] = 0xC3;
    Way(image, CO_POCKET_SEARCH_GO, 1); Way(image, CO_POCKET_SEARCH_SKIP, 2);
    Way(image, CO_POCKET_GO_ON, 1); Way(image, NEXT, 2);
    co_pocket_test_in_disk(InDisk);
    for (int pass = 0; pass < 6; pass++)
    {
        const int flag = pass & 1, complex = pass >= 4; /* disks: a lower level's, HellForge's, the mod's */
        memcpy(level + 0x2D7C, pass < 2 ? &lower : pass < 4 ? &hellforge : &key, 4);
        image[FLAG] = (uint8_t)flag;
        /* the list: by the level's disk alone, the same answer at both sites */
        memset(&r, 0, sizeof r); r.r14 = (uint64_t)(uintptr_t)level; r.stub = game + CODE + CO_POCKET_AT_ALLOC;
        RunStub(&r);
        CHECK((uint32_t)r.rdx == (complex ? 0x1000u : 0x100u));
        memset(&r, 0, sizeof r); r.r14 = (uint64_t)(uintptr_t)level; r.stub = game + CODE + CO_POCKET_AT_CLEAR;
        RunStub(&r);
        CHECK((uint32_t)r.r8 == (complex ? 0x1000u : 0x100u));
        /* the search: a free cell goes on in every level; a location cell only in a complex one */
        CoPocketGraph g = Graph("the stubs, run", 1, 0);
        memset(&r, 0, sizeof r); r.rbp = CellAt(0); r.r15 = (uint64_t)(uintptr_t)ctx; r.stub = game + CODE;
        RunStub(&r);
        CHECK((uint32_t)r.rax == 1u && r.rbpOut == CellAt(0));
        Put64(g_cells + 0x98, 0x1234);
        RunStub(&r);
        CHECK((uint32_t)r.rax == (complex && flag ? 1u : 2u));
        Put64(g_cells + 0x98, 0);
        /* the restart: joined and on, only in a complex level; else the chain's stub with the level in rcx */
        memset(frame, 0, sizeof frame);
        uint8_t* rbp = frame + 0x100;
        Put64(rbp - 0x58, g.subregions); Put64(rbp - 0x70, g.ids); Put64(rbp - 0x40, CELLS);
        memset(&r, 0, sizeof r);
        r.rbp = (uint64_t)(uintptr_t)rbp; r.r12 = (uint64_t)(uintptr_t)ctx; r.r13 = g.types; r.rbx = 1; r.rdi = 0;
        r.rcx = (uint64_t)(uintptr_t)level; r.slot78 = g.cells; r.slot64 = 0; r.stub = game + CODE + CO_POCKET_AT_RESTART;
        Keep();
        RunStub(&r);
        if (complex && flag) CHECK((uint32_t)r.rax == 1u && g_ids[0] == 0 && Painted(1, 0) && Painted(2, 0) && fake_host_logged("pocket 1 joined to the main area 0 by 2 blocked cells"));
        else CHECK((uint32_t)r.rax == 2u && Kept() && g_diskCalls == 0 && r.rcxOut == (uint64_t)(uintptr_t)level);
        CHECK(r.rbxOut == 1 && r.rdiOut == 0 && r.r12Out == (uint64_t)(uintptr_t)ctx && r.r13Out == g.types && r.rbpOut == (uint64_t)(uintptr_t)rbp);
    }
    /* a complex level's pocket with no path: the chain's stub, the level still in rcx; the planet's flag is the frame's */
    image[FLAG] = 1;
    for (int planet = 0; planet < 2; planet++)
    {
        CoPocketGraph g = Graph("the stubs, run: not joined", planet, 0);
        uint8_t* rbp = frame + 0x100;
        if (planet) Put32(Sub(3) + 0xC, 2), Put32(Sub(7) + 0xC, 2); /* no type object: blocked, with the flag only */
        Put64(rbp - 0x58, g.subregions); Put64(rbp - 0x70, g.ids); Put64(rbp - 0x40, CELLS);
        memset(&r, 0, sizeof r);
        r.rbp = (uint64_t)(uintptr_t)rbp; r.r12 = (uint64_t)(uintptr_t)ctx; r.r13 = g.types; r.rbx = 1; r.rdi = 0;
        r.rcx = (uint64_t)(uintptr_t)level; r.slot78 = g.cells; r.slot64 = (uint64_t)planet; r.stub = game + CODE + CO_POCKET_AT_RESTART;
        Keep();
        RunStub(&r);
        CHECK((uint32_t)r.rax == 2u && Kept() && r.rcxOut == (uint64_t)(uintptr_t)level && fake_host_logged("has no path to the main area 0"));
    }
    {
        /* a frame that cannot be read, a main area that is none, settings that are not there: the restart as it was */
        CoPocketGraph g = Graph("the frame", 1, 0);
        uint8_t* rbp = frame + 0x100;
        uint8_t stack[0x100] = {0};
        Put64(rbp - 0x58, g.subregions); Put64(rbp - 0x70, g.ids); Put64(rbp - 0x40, CELLS);
        Put64(stack + 0x78, g.cells);
        Keep();
        CHECK(co_pocket_frame((uint8_t*)0x10, rbp, (uint64_t)(uintptr_t)ctx, 1, g.types, 0) == 0 && Kept());
        CHECK(co_pocket_frame(stack, rbp, (uint64_t)(uintptr_t)ctx, 1, g.types, NONE) == 0 && Kept());
        Put64(rbp - 0x40, 0);
        CHECK(co_pocket_frame(stack, rbp, (uint64_t)(uintptr_t)ctx, 1, g.types, 0) == 0 && Kept());
        Put64(rbp - 0x40, CELLS);
        ctx[1] = 0;
        CHECK(co_pocket_frame(stack, rbp, (uint64_t)(uintptr_t)ctx, 1, g.types, 0) == 0 && Kept() && g_diskCalls == 0);
        ctx[1] = (uint64_t)(uintptr_t)settings;
        CHECK(co_pocket_frame(stack, rbp, (uint64_t)(uintptr_t)ctx, 1, g.types, 0) == 1 && g_ids[0] == 0 && Painted(1, 0));
    }
    free(level);
}

/* ---- the install -------------------------------------------------------------------------------------------------- */

static void Install(void)
{
    uint8_t* image = Reset("the check");
    uintptr_t game = (uintptr_t)image;
    const CoChain* chains = co_chain_sites();
    uint8_t* before = (uint8_t*)malloc(FAKE_IMAGE_SIZE);
    CHECK(chains[10].site == CO_POCKET_RESTART);
    CHECK(co_pocket_check(game) == NULL && g_fake.writes == 0);
    {
        g_coApi = fake_host_reset(CO_GAME_BUILD);
        CHECK(co_pocket_check((uintptr_t)g_fake.image) != NULL); /* another game */
    }
    for (int i = 0; i < 3; i++)
    {
        image = Reset("a site is another's");
        image[kCoPocketSites[i].rva + kCoPocketSites[i].length - 1u] ^= 1;
        CHECK(co_pocket_check((uintptr_t)image) != NULL);
        CHECK(co_chain_install((uintptr_t)image, (uintptr_t)image + FLAG) == NULL);
        CHECK(co_pocket_install((uintptr_t)image, (uintptr_t)image + FLAG) != NULL && g_fake.writes == CO_CHAINS && co_pocket_test_page() == 0);
    }
    for (size_t i = 0; i < PINS; i++)
    {
        image = Reset("the code around is another");
        image[kCoPocketPins[i].rva + kCoPocketPins[i].length - 1u] ^= 1;
        CHECK(co_pocket_check((uintptr_t)image) != NULL && g_fake.writes == 0);
    }

    image = Reset("before the chain");
    game = (uintptr_t)image;
    memcpy(before, image, FAKE_IMAGE_SIZE);
    CHECK(co_pocket_install(game, game + FLAG) != NULL && g_fake.writes == 0 && memcmp(before, image, FAKE_IMAGE_SIZE) == 0);

    image = Reset("the install");
    game = (uintptr_t)image;
    CHECK(co_chain_install(game, game + FLAG) == NULL && g_fake.writes == CO_CHAINS);
    const uintptr_t slot = co_chain_slot(10);
    CHECK(slot == co_chain_test_page() + 10u * CO_CHAIN_SLOT && co_chain_slot(CO_CHAINS) == 0 && co_chain_slot(-1) == 0);
    memcpy(before, image, FAKE_IMAGE_SIZE);
    CHECK(co_pocket_install(game, game + FLAG) == NULL && g_fake.writes == CO_CHAINS + 4 && co_pocket_test_page() != 0);
    const uintptr_t page = co_pocket_test_page();
    static const uint32_t at[3] = {CO_POCKET_AT_SEARCH, CO_POCKET_AT_ALLOC, CO_POCKET_AT_CLEAR};
    for (int i = 0; i < 3; i++)
    {
        const CoPocketSite* s = &kCoPocketSites[i];
        int32_t rel; memcpy(&rel, image + s->rva + 1u, 4);
        CHECK(image[s->rva] == 0xE9 && game + s->rva + 5u + (intptr_t)rel == page + at[i] && s->on == s->rva + s->length);
        for (unsigned k = 5; k < s->length; k++) CHECK(image[s->rva + k] == 0x90);
        CHECK(memcmp((const uint8_t*)page + at[i], s->bytes, i == 0 ? 8 : s->length) == 0); /* each begins with the game's instruction */
    }
    {
        int32_t rel; memcpy(&rel, image + CO_POCKET_RESTART + 1u, 4);
        const uint8_t* restart = (const uint8_t*)page + CO_POCKET_AT_RESTART;
        CHECK(image[CO_POCKET_RESTART] == 0xE9 && image[CO_POCKET_RESTART + 5u] == 0x90 && game + CO_POCKET_RESTART + 5u + (intptr_t)rel == (uintptr_t)restart);
        memcpy(&rel, restart + 77, 4);
        CHECK((uintptr_t)restart + 81u + (intptr_t)rel == slot);  /* every level it joins nothing in: the chain's stub */
        memcpy(&rel, restart + 72, 4);
        CHECK((uintptr_t)restart + 76u + (intptr_t)rel == game + CO_POCKET_GO_ON);
    }
    /* nothing else of the image, and nothing of the chain's page or HellForge's stub */
    for (int i = 0; i < 3; i++) memcpy(before + kCoPocketSites[i].rva, image + kCoPocketSites[i].rva, kCoPocketSites[i].length);
    memcpy(before + CO_POCKET_RESTART + 1u, image + CO_POCKET_RESTART + 1u, 4);
    CHECK(memcmp(before, image, FAKE_IMAGE_SIZE) == 0);
    CHECK(co_pocket_install(game, game + FLAG) == NULL && g_fake.writes == CO_CHAINS + 4); /* once */

    for (int fail = 0; fail < 4; fail++)
    {
        image = Reset("a step that cannot be written");
        game = (uintptr_t)image;
        CHECK(co_chain_install(game, game + FLAG) == NULL);
        memcpy(before, image, FAKE_IMAGE_SIZE);
        g_fake.failWriteAt = CO_CHAINS + fail;
        CHECK(co_pocket_install(game, game + FLAG) != NULL && co_pocket_test_page() == 0);
        CHECK(memcmp(before, image, FAKE_IMAGE_SIZE) == 0); /* the game's bytes and the chain's jump again */
    }
    image = Reset("the restart is not the chain's");
    game = (uintptr_t)image;
    CHECK(co_chain_install(game, game + FLAG) == NULL);
    image[CO_POCKET_RESTART + 1u] ^= 0x40;
    memcpy(before, image, FAKE_IMAGE_SIZE);
    CHECK(co_pocket_install(game, game + FLAG) != NULL && g_fake.writes == CO_CHAINS && memcmp(before, image, FAKE_IMAGE_SIZE) == 0);
    image = Reset("the restart's jump is shorter");
    game = (uintptr_t)image;
    CHECK(co_chain_install(game, game + FLAG) == NULL);
    image[CO_POCKET_RESTART + 5u] = 0xCC;
    CHECK(co_pocket_install(game, game + FLAG) != NULL && g_fake.writes == CO_CHAINS && co_pocket_test_page() == 0);
    free(before);
}

int main(void)
{
    Reconnect();
    Stubs();
    Install();
    if (g_failed) { printf("pocket: %d checks failed\n", g_failed); return 1; }
    printf("pocket: all cases passed\n");
    return 0;
}
