/* A main objective waits for the steps of its own mission on a complex operation: the rule, the note it reads and
 * the arcs' note that makes it, the instance's mission found as the game finds an instance's location, the two stubs
 * run as the game's passes reach them (whose normal path is the game's bytes), the flag level init and the ship set,
 * the install's refusals and rollback, and the war table's lines from a step to its own mission's mains only. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_unlock.h"
#include "xl/co_pages.h"
#include "xl/co_page_sites.h"

void CoXlTestGame(uintptr_t game);
void CoXlTestWorldLevel(uintptr_t level, uintptr_t preview);
void CoXlTestWorldShip(void);
void CoXlTestWorldWrite(float scale, int mission);
void CoXlTestArcUnlockNote(uint64_t lg, uint64_t G, uint32_t o, const uint32_t* index, const uint32_t* id, const uint8_t* block, const uint8_t* blockMission);
void co_pages_test_reset(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* Free places in the image for the game's objects: the world (its level generator at +0xAE288, the preview records
 * from +0xAE2A0), the generator, the unit lookup's table, the location records' units, the objective manager. */
#define WORLD 0x3500000u
#define LEVEL 0x3600000u
#define UNITS 0x3700000u
#define UNIT_RECORDS 0x3800000u
#define MANAGER 0x3900000u
#define NET (MANAGER + 0x2000u)     /* [M+0x68]: the net records, 0x64 each, the state at +0x20 */
#define INSTANCES (MANAGER + 0x3000u) /* [M+0x60] */
#define STUBS 0x3100000u            /* the test's own stubs, within a jump of the sites as the install's page is */
#define FLAG 0x3101800u
#define GEN_PAGE 0x3010000u         /* the world's two value pages, in reach of their readers */
#define LIVE_PAGE 0x3010800u
#define PREVIEWS 0x3000000u
#define GEN 0x100000u
#define LIST 0x1000u
#define UNIT(handle) (0xABC00000ull + (handle))

static uint8_t* g_image;

static void Put64(uint32_t at, uint64_t v) { memcpy(g_image + at, &v, 8); }
static void Put32(uint32_t at, uint32_t v) { memcpy(g_image + at, &v, 4); }
static uint64_t At(uint32_t at) { return (uint64_t)(uintptr_t)(g_image + at); }

static uint64_t TestUnitOf(uint32_t handle) { return handle != 0 && handle < 100 ? UNIT(handle) : 0; }

/* Location l: its unit's handle (0 none), objective id and x/y. */
static void Location(uint32_t l, uint32_t handle, uint32_t id, float x, float y)
{
    const uint32_t loc = GEN + LIST + 0x108u * l, record = UNIT_RECORDS + 0x100u * l;
    Put64(loc + 0x98, handle ? At(record) : 0);
    Put64(record + 0xA8, UNIT(handle));
    memcpy(g_image + loc + 0xA4, &x, 4); memcpy(g_image + loc + 0xA8, &y, 4);
    Put32(loc + 0xD8, id);
    Put32(GEN + 0x11A715Cu, l + 1 > *(uint32_t*)(g_image + GEN + 0x11A715Cu) ? l + 1 : *(uint32_t*)(g_image + GEN + 0x11A715Cu));
}
static void Instance(uint32_t i, uint32_t handle)
{
    const uint32_t entity = MANAGER + 0x1000u + 0x20u * i;
    Put64(MANAGER + 0x100u + 8u * i, At(entity));
    Put32(entity + 8, 0x500u + i); Put32(entity + 0xC, handle);
}

/* The hologram's originals: what the marker source gives, and which slots the draw would line to. */
static uint32_t g_outputs;
static uint64_t g_pos[8];
static uint32_t g_ids[8];
static uint32_t g_drawn;
static float g_alpha;
static uint32_t TestMarkers(uintptr_t lg, uint64_t* pos, const uint32_t* in, uint32_t* out)
{
    memcpy(pos, g_pos, 8ull * g_outputs); memcpy(out, g_ids, 4ull * g_outputs);
    return g_outputs;
}
static uint8_t* g_holo;
#define SLOT(s) (g_holo + 0x886B0u + 0x180u * (s))
static uint64_t TestLines(uintptr_t holo, uintptr_t b, uintptr_t step, uintptr_t d, float alpha)
{
    g_drawn = 0;
    for (uint32_t s = 0; s < *(uint32_t*)(g_holo + 0x8B730u); s++) if (SLOT(s)[0x17C] == 0) g_drawn |= 1u << s;
    g_alpha = alpha;
    return 0;
}

static uint64_t Pos(float x, float y) { uint64_t v; memcpy(&v, &x, 4); memcpy((uint8_t*)&v + 4, &y, 4); return v; }

static void World(int install)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_image = g_fake.image;
    co_unlock_test_reset();
    Put64(0x3326340u, At(WORLD)); Put64(WORLD + 0xAE288u, At(LEVEL)); Put64(LEVEL + 0x2C8u, At(GEN));
    Put64(0x3326308u, At(UNITS)); Put64(UNITS + 0x18u, At(UNITS + 0x100u)); Put64(UNITS + 0x100u + 0x710u, (uint64_t)(uintptr_t)TestUnitOf);
    Put64(MANAGER + 0x50u, At(MANAGER + 0x100u)); Put64(MANAGER + 0x60u, At(INSTANCES)); Put64(MANAGER + 0x68u, At(NET));
    /* two missions: 0 has a main and two steps, 1 a main and a step; a side objective; a main with no note */
    Location(0, 1, 0xA0, 100, 200); Location(1, 2, 0xA1, 110, 210); Location(2, 3, 0xA1, 120, 220);
    Location(3, 4, 0xB0, 300, 400); Location(4, 5, 0xB1, 310, 410); Location(5, 6, 0xC0, 500, 600); Location(6, 7, 0xD0, 700, 800);
    for (uint32_t i = 0; i < 7; i++) Instance(i, i + 1);
    Instance(7, 42); /* a unit no location holds */
    memcpy(g_image + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA);
    memcpy(g_image + CO_UNLOCK_B_SITE, kCoUnlockB, sizeof kCoUnlockB);
    /* the hologram's originals, which the hooks call */
    g_fake.originals[0].target = (uintptr_t)(g_image + CO_UNLOCK_MARKERS); g_fake.originals[0].original = (void*)TestMarkers;
    g_fake.originals[1].target = (uintptr_t)(g_image + CO_UNLOCK_LINES); g_fake.originals[1].original = (void*)TestLines;
    if (install) CHECK(co_unlock_install((uintptr_t)g_image, LIST) == NULL); /* the game's base, as the stubs know it */
}
static void Note(void)
{
    const uint32_t location[6] = {0, 1, 2, 3, 4, 5}, id[6] = {0xA0, 0xA1, 0xA1, 0xB0, 0xB1, 0xC0};
    const uint8_t mission[6] = {0, 0, 0, 1, 1, CO_UNLOCK_NONE};
    co_unlock_note((uintptr_t)At(LEVEL), (uintptr_t)At(GEN), location, id, mission, 6);
}

/* Where the rel32 at `at` of a stub placed at `code` leads, as an offset in the image. */
static uint32_t Leads(const uint8_t* stub, size_t at) { int32_t d; memcpy(&d, stub + at, 4); return (uint32_t)((uintptr_t)stub + at + 4u + (intptr_t)d - (uintptr_t)g_image); }

/* ---- the stubs, run ----------------------------------------------------------------------------------------------- */

/* Every general register, the flags and the xmm registers going into a run, and as the run left them. */
typedef struct Regs
{
    uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15; /* 0x00 .. 0x78 */
    uint64_t flags, landed;                                                                /* 0x80, 0x88 */
    uint8_t xmm[16][16];                                                                   /* 0x90 .. 0x180 */
} Regs;
_Static_assert(sizeof(Regs) == 0x190, "the offsets the run's code uses");
Regs g_unlockIn, g_unlockOut;
uint64_t g_unlockHost, g_unlockEnter;
/* What the stub gave the call (rsp at its entry), how often it called, and the answer the call gives. */
struct { uint64_t rsp, rcx, rdx, r8, calls, answer; } g_unlockCall;

#define LAND_BACK 1u /* 0x5D36BD, after the activation pass's site */
#define LAND_HOLD 2u /* 0x5D3FAF, the waiting pass's main held */
#define LAND_NEXT 3u /* 0x5D3F96, the waiting pass's next step */
void UnlockRun(void);
void UnlockBack(void);
void UnlockHold(void);
void UnlockNext(void);
uint64_t UnlockHolds(void);
/* UnlockRun enters g_unlockEnter with g_unlockIn's registers and flags. Where the site goes on, a landing records every
 * register as the game's next instruction would find it and returns to UnlockRun's caller. UnlockHolds stands in for
 * co_unlock_holds: it records its arguments and its rsp, then does what any callee may (its home space and the volatile
 * registers are its own) before it answers g_unlockCall.answer. */
__asm__(".text\n.p2align 4\n.globl UnlockRun\nUnlockRun:\n"
        "  pushq %rbx\n  pushq %rbp\n  pushq %rdi\n  pushq %rsi\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n  subq $0xA8, %rsp\n"
        "  movdqu %xmm6, 0x00(%rsp)\n  movdqu %xmm7, 0x10(%rsp)\n  movdqu %xmm8, 0x20(%rsp)\n  movdqu %xmm9, 0x30(%rsp)\n  movdqu %xmm10, 0x40(%rsp)\n"
        "  movdqu %xmm11, 0x50(%rsp)\n  movdqu %xmm12, 0x60(%rsp)\n  movdqu %xmm13, 0x70(%rsp)\n  movdqu %xmm14, 0x80(%rsp)\n  movdqu %xmm15, 0x90(%rsp)\n"
        "  movq %rsp, g_unlockHost(%rip)\n"
        "  movdqu g_unlockIn+0x90(%rip), %xmm0\n  movdqu g_unlockIn+0xA0(%rip), %xmm1\n  movdqu g_unlockIn+0xB0(%rip), %xmm2\n  movdqu g_unlockIn+0xC0(%rip), %xmm3\n"
        "  movdqu g_unlockIn+0xD0(%rip), %xmm4\n  movdqu g_unlockIn+0xE0(%rip), %xmm5\n  movdqu g_unlockIn+0xF0(%rip), %xmm6\n  movdqu g_unlockIn+0x100(%rip), %xmm7\n"
        "  movdqu g_unlockIn+0x110(%rip), %xmm8\n  movdqu g_unlockIn+0x120(%rip), %xmm9\n  movdqu g_unlockIn+0x130(%rip), %xmm10\n  movdqu g_unlockIn+0x140(%rip), %xmm11\n"
        "  movdqu g_unlockIn+0x150(%rip), %xmm12\n  movdqu g_unlockIn+0x160(%rip), %xmm13\n  movdqu g_unlockIn+0x170(%rip), %xmm14\n  movdqu g_unlockIn+0x180(%rip), %xmm15\n"
        "  pushq g_unlockIn+0x80(%rip)\n  popfq\n"
        "  movq g_unlockIn+0x00(%rip), %rax\n  movq g_unlockIn+0x08(%rip), %rcx\n  movq g_unlockIn+0x10(%rip), %rdx\n  movq g_unlockIn+0x18(%rip), %rbx\n"
        "  movq g_unlockIn+0x28(%rip), %rbp\n  movq g_unlockIn+0x30(%rip), %rsi\n  movq g_unlockIn+0x38(%rip), %rdi\n  movq g_unlockIn+0x40(%rip), %r8\n"
        "  movq g_unlockIn+0x48(%rip), %r9\n  movq g_unlockIn+0x50(%rip), %r10\n  movq g_unlockIn+0x58(%rip), %r11\n  movq g_unlockIn+0x60(%rip), %r12\n"
        "  movq g_unlockIn+0x68(%rip), %r13\n  movq g_unlockIn+0x70(%rip), %r14\n  movq g_unlockIn+0x78(%rip), %r15\n"
        "  movq g_unlockIn+0x20(%rip), %rsp\n  jmpq *g_unlockEnter(%rip)\n"
        ".p2align 4\n.globl UnlockBack\nUnlockBack:\n  movq $1, g_unlockOut+0x88(%rip)\n  jmp UnlockLand\n"
        ".p2align 4\n.globl UnlockHold\nUnlockHold:\n  movq $2, g_unlockOut+0x88(%rip)\n  jmp UnlockLand\n"
        ".p2align 4\n.globl UnlockNext\nUnlockNext:\n  movq $3, g_unlockOut+0x88(%rip)\n  jmp UnlockLand\n"
        ".p2align 4\nUnlockLand:\n"
        "  movq %rax, g_unlockOut+0x00(%rip)\n  movq %rcx, g_unlockOut+0x08(%rip)\n  movq %rdx, g_unlockOut+0x10(%rip)\n  movq %rbx, g_unlockOut+0x18(%rip)\n"
        "  movq %rsp, g_unlockOut+0x20(%rip)\n  movq %rbp, g_unlockOut+0x28(%rip)\n  movq %rsi, g_unlockOut+0x30(%rip)\n  movq %rdi, g_unlockOut+0x38(%rip)\n"
        "  movq %r8, g_unlockOut+0x40(%rip)\n  movq %r9, g_unlockOut+0x48(%rip)\n  movq %r10, g_unlockOut+0x50(%rip)\n  movq %r11, g_unlockOut+0x58(%rip)\n"
        "  movq %r12, g_unlockOut+0x60(%rip)\n  movq %r13, g_unlockOut+0x68(%rip)\n  movq %r14, g_unlockOut+0x70(%rip)\n  movq %r15, g_unlockOut+0x78(%rip)\n"
        "  movdqu %xmm0, g_unlockOut+0x90(%rip)\n  movdqu %xmm1, g_unlockOut+0xA0(%rip)\n  movdqu %xmm2, g_unlockOut+0xB0(%rip)\n  movdqu %xmm3, g_unlockOut+0xC0(%rip)\n"
        "  movdqu %xmm4, g_unlockOut+0xD0(%rip)\n  movdqu %xmm5, g_unlockOut+0xE0(%rip)\n  movdqu %xmm6, g_unlockOut+0xF0(%rip)\n  movdqu %xmm7, g_unlockOut+0x100(%rip)\n"
        "  movdqu %xmm8, g_unlockOut+0x110(%rip)\n  movdqu %xmm9, g_unlockOut+0x120(%rip)\n  movdqu %xmm10, g_unlockOut+0x130(%rip)\n  movdqu %xmm11, g_unlockOut+0x140(%rip)\n"
        "  movdqu %xmm12, g_unlockOut+0x150(%rip)\n  movdqu %xmm13, g_unlockOut+0x160(%rip)\n  movdqu %xmm14, g_unlockOut+0x170(%rip)\n  movdqu %xmm15, g_unlockOut+0x180(%rip)\n"
        "  pushfq\n  popq g_unlockOut+0x80(%rip)\n"
        "  movq g_unlockHost(%rip), %rsp\n"
        "  movdqu 0x00(%rsp), %xmm6\n  movdqu 0x10(%rsp), %xmm7\n  movdqu 0x20(%rsp), %xmm8\n  movdqu 0x30(%rsp), %xmm9\n  movdqu 0x40(%rsp), %xmm10\n"
        "  movdqu 0x50(%rsp), %xmm11\n  movdqu 0x60(%rsp), %xmm12\n  movdqu 0x70(%rsp), %xmm13\n  movdqu 0x80(%rsp), %xmm14\n  movdqu 0x90(%rsp), %xmm15\n"
        "  addq $0xA8, %rsp\n  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rsi\n  popq %rdi\n  popq %rbp\n  popq %rbx\n  ret\n"
        ".p2align 4\n.globl UnlockHolds\nUnlockHolds:\n"
        "  movq %rsp, g_unlockCall+0x00(%rip)\n  movq %rcx, g_unlockCall+0x08(%rip)\n  movq %rdx, g_unlockCall+0x10(%rip)\n  movq %r8, g_unlockCall+0x18(%rip)\n"
        "  incq g_unlockCall+0x20(%rip)\n"
        "  movabsq $0xBADC0FFEE0DDF00D, %rax\n"
        "  movq %rax, 0x08(%rsp)\n  movq %rax, 0x10(%rsp)\n  movq %rax, 0x18(%rsp)\n  movq %rax, 0x20(%rsp)\n"
        "  movq %rax, %rcx\n  movq %rax, %rdx\n  movq %rax, %r8\n  movq %rax, %r9\n  movq %rax, %r10\n  movq %rax, %r11\n"
        "  movq %rax, %xmm0\n  movq %rax, %xmm1\n  movq %rax, %xmm2\n  movq %rax, %xmm3\n  movq %rax, %xmm4\n  movq %rax, %xmm5\n"
        "  movq g_unlockCall+0x28(%rip), %rax\n  ret\n");

/* jmp [rip+0] to a landing, at the game's instruction the site goes on to */
static void Pad(uint32_t rva, void (*to)(void))
{
    static const uint8_t jump[6] = {0xFF, 0x25, 0, 0, 0, 0};
    const uint64_t at = (uint64_t)(uintptr_t)to;
    memcpy(g_image + rva, jump, sizeof jump); memcpy(g_image + rva + sizeof jump, &at, 8);
}

/* The sites hold the game's bytes (World), and runnable landings where they go on; the test's stub room runs too. */
static void Sites(void)
{
    DWORD old;
    CHECK(VirtualProtect(g_image + (CO_UNLOCK_A_SITE & ~0xFFFu), 0x2000u, PAGE_EXECUTE_READWRITE, &old));
    CHECK(VirtualProtect(g_image + STUBS, 0x2000u, PAGE_EXECUTE_READWRITE, &old));
    Pad(CO_UNLOCK_A_BACK, UnlockBack);
    Pad(CO_UNLOCK_B_HOLD, UnlockHold);
    Pad(CO_UNLOCK_B_NEXT, UnlockNext);
    CHECK(memcmp(g_image + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA) == 0 || g_image[CO_UNLOCK_A_SITE] == 0xE9);
    FlushInstructionCache(GetCurrentProcess(), g_image + (CO_UNLOCK_A_SITE & ~0xFFFu), 0x2000u);
}

/* Runs the code at `enter` with `in`'s registers on a 16-aligned stack whose first 0x100 bytes are `frame` (the pass's
 * own slots); 1 when the run left that frame as it was. g_unlockOut holds what the landing found. */
static int Run(const Regs* in, uintptr_t enter, const uint8_t* frame)
{
    uint8_t stack[0x1000] __attribute__((aligned(16)));
    uint8_t* const rsp = stack + 0x800;
    memset(stack, 0xEE, sizeof stack);
    memcpy(rsp, frame, 0x100);
    g_unlockIn = *in;
    g_unlockIn.rsp = (uint64_t)(uintptr_t)rsp;
    memset(&g_unlockOut, 0, sizeof g_unlockOut);
    g_unlockEnter = enter;
    g_unlockCall.rsp = g_unlockCall.rcx = g_unlockCall.rdx = g_unlockCall.r8 = g_unlockCall.calls = 0;
    UnlockRun();
    return memcmp(rsp, frame, 0x100) == 0;
}

/* Two runs that leave the same machine: every register, the arithmetic and direction flags, the xmm registers, the
 * landing. */
static int SameRun(const Regs* a, const Regs* b)
{
    return memcmp(a, b, offsetof(Regs, flags)) == 0 && ((a->flags ^ b->flags) & 0xCD5u) == 0 && a->landed == b->landed &&
        memcmp(a->xmm, b->xmm, sizeof a->xmm) == 0;
}

/* Every register a value of its own; the flags all set (popfq keeps IF). */
static void Distinct(Regs* r)
{
    uint64_t* g = &r->rax;
    for (int i = 0; i < 16; i++) g[i] = 0x1111111111111111ull * (uint64_t)(i + 1) ^ 0x0F0F000000000000ull;
    for (int i = 0; i < 16; i++)
        for (int b = 0; b < 16; b++) r->xmm[i][b] = (uint8_t)(0x10 * i + b + 1);
    r->flags = 0xAD7u;
    r->landed = 0;
}

/* The activation pass at 0x5D36A8: rdx the manager, rbx step j's net record offset, edi j, r13d main k, sil the
 * "held" flag in rsi, whose upper bytes are the manager's the pass loaded there (0x5D35A0, cleared by xor sil,sil);
 * rax the instances, rcx k's instance offset, r12/r15 j's entity and instance offsets, r14b the sequential flag. */
static Regs SiteA(uint32_t k, uint32_t j, uint8_t sil)
{
    Regs r;
    const uint64_t m = At(MANAGER);
    Distinct(&r);
    r.rax = At(INSTANCES); r.rcx = 0x1078ull * k; r.rdx = m; r.rbx = 0x64ull * j;
    r.rsi = (m & ~0xFFull) | sil; r.rdi = j; r.r12 = 8ull * j; r.r13 = k; r.r14 = 1; r.r15 = 0x1078ull * j;
    return r;
}

/* The waiting pass at 0x5D3F8B: rax the manager (just loaded from [rsp+0x30]), ecx step j, rdx and r8 j's net record
 * and instance offsets, r9d the instance count, r10b the "held" flag, r11d main i ([rsp+0x3C]). */
static Regs SiteB(uint32_t i, uint32_t j, uint8_t r10b, uint8_t* frame)
{
    Regs r;
    const uint64_t m = At(MANAGER);
    Distinct(&r);
    r.rax = m; r.rcx = j; r.rdx = 0x64ull * j; r.r8 = 0x1078ull * j; r.r9 = 8; r.r10 = (r.r10 & ~0xFFull) | r10b; r.r11 = i;
    memset(frame, 0x77, 0x100);
    memcpy(frame + 0x30, &m, 8); memcpy(frame + 0x3C, &i, 4);
    return r;
}

static void Step(uint32_t j, uint32_t state) { Put32(NET + 0x64u * j + 0x20u, state); }

/* What the activation pass reads after the site (0x5D36BD on): rdx, the loop's registers and every non-volatile one. */
static void KeptA(const Regs* in, int frameKept)
{
    const Regs* o = &g_unlockOut;
    CHECK(o->landed == LAND_BACK && o->rsp == in->rsp && frameKept);
    CHECK(o->rdx == in->rdx && o->rbx == in->rbx && o->rbp == in->rbp && o->rdi == in->rdi);
    CHECK(o->r12 == in->r12 && o->r13 == in->r13 && o->r14 == in->r14 && o->r15 == in->r15);
    CHECK(memcmp(o->xmm[6], in->xmm[6], 10 * 16) == 0);
}

/* What the waiting pass reads at either way on: the loop's rcx, rdx, r8 to r11, and every non-volatile register. */
static void KeptB(const Regs* in, int frameKept)
{
    const Regs* o = &g_unlockOut;
    CHECK(o->rsp == in->rsp && frameKept);
    CHECK(o->rcx == in->rcx && o->rdx == in->rdx && o->r8 == in->r8 && o->r9 == in->r9 && o->r10 == in->r10 && o->r11 == in->r11);
    CHECK(o->rbx == in->rbx && o->rbp == in->rbp && o->rsi == in->rsi && o->rdi == in->rdi);
    CHECK(o->r12 == in->r12 && o->r13 == in->r13 && o->r14 == in->r14 && o->r15 == in->r15);
    CHECK(memcmp(o->xmm[6], in->xmm[6], 10 * 16) == 0);
}

/* The call the stub made, if it made one: the manager, the main, the step, at a 16-aligned call. */
static void Called(int expected, uint64_t manager, uint32_t main, uint32_t step)
{
    CHECK(g_unlockCall.calls == (uint64_t)expected);
    if (!expected) return;
    CHECK((g_unlockCall.rsp & 0xFu) == 8u); /* rsp + 8 at the callee's entry: 16-aligned at the call */
    CHECK(g_unlockCall.rcx == manager && (uint32_t)g_unlockCall.rdx == main && (uint32_t)g_unlockCall.r8 == step);
}

/* Both stubs with the test's call in place of co_unlock_holds, against the game's bytes run the same way: off, the
 * stub leaves exactly what the game's bytes leave; on, a done step holds nothing and a step not done holds the main by
 * the call's answer, with every register the pass reads after the site as it was. */
static void StubsRun(void)
{
    uint8_t frame[0x100], gameFrame[0x100];
    const uintptr_t game = (uintptr_t)g_image, holds = (uintptr_t)UnlockHolds;
    const uint64_t m = At(MANAGER);
    World(0);
    Sites();
    uint8_t* a = g_image + STUBS;
    uint8_t* b = a + 0x80;
    CHECK(co_unlock_stub_a(a, (uintptr_t)a, game, game + FLAG, holds) == CO_UNLOCK_A_STUB);
    CHECK(co_unlock_stub_b(b, (uintptr_t)b, game, game + FLAG, holds) == CO_UNLOCK_B_STUB);
    FlushInstructionCache(GetCurrentProcess(), a, 0x100);
    const uint32_t k = 3, j = 5, i = 2;
    for (int on = 0; on < 2; on++)
        for (int done = 0; done < 2; done++)
            for (int answer = 0; answer < 2; answer++)
                for (uint8_t held = 0; held < 2; held++)
                {
                    Regs in, game0;
                    int kept, gameKept;
                    g_image[FLAG] = (uint8_t)on;
                    g_unlockCall.answer = (uint64_t)answer;
                    Step(j, done ? 2u : (uint32_t)answer); /* waiting or active: not done either way */

                    g_case = on ? "the activation pass's stub, run: a complex mission" : "the activation pass's stub, run: the game's bytes";
                    memset(frame, 0x5C, sizeof frame);
                    in = SiteA(k, j, held);
                    gameKept = Run(&in, game + CO_UNLOCK_A_SITE, frame);
                    game0 = g_unlockOut;
                    CHECK(game0.landed == LAND_BACK && game0.rsi == (done ? held : 1u) && g_unlockCall.calls == 0 && gameKept); /* the game's rule */
                    kept = Run(&in, (uintptr_t)a, frame);
                    in.rsp = g_unlockIn.rsp;
                    KeptA(&in, kept);
                    if (!on) { CHECK(SameRun(&g_unlockOut, &game0)); Called(0, 0, 0, 0); }
                    else
                    {
                        CHECK(g_unlockOut.rsi == (!done && answer ? 1u : held));
                        Called(!done, m, k, j);
                    }

                    g_case = on ? "the waiting pass's stub, run: a complex mission" : "the waiting pass's stub, run: the game's bytes";
                    in = SiteB(i, j, held, frame);
                    memcpy(gameFrame, frame, sizeof frame);
                    gameKept = Run(&in, game + CO_UNLOCK_B_SITE, gameFrame);
                    game0 = g_unlockOut;
                    CHECK(game0.landed == (done ? LAND_NEXT : LAND_HOLD) && g_unlockCall.calls == 0 && gameKept);
                    kept = Run(&in, (uintptr_t)b, frame);
                    in.rsp = g_unlockIn.rsp;
                    KeptB(&in, kept);
                    if (!on) { CHECK(SameRun(&g_unlockOut, &game0)); Called(0, 0, 0, 0); }
                    else
                    {
                        CHECK(g_unlockOut.landed == (!done && answer ? LAND_HOLD : LAND_NEXT));
                        Called(!done, m, i, j);
                    }
                }
}

/* The page the install made, entered through the sites' jumps, with the mod's own call: each pair asked by the note. */
static void InstalledRun(void)
{
    uint8_t frame[0x100];
    Regs in;
    g_case = "the installed stubs, run with co_unlock_holds";
    World(1);
    Note();
    Sites();
    const uintptr_t game = (uintptr_t)g_image;
    /* mission 0: main 0, steps 1 (active) and 2 (done); mission 1: main 3, step 4 (active); 5 a side objective */
    Step(1, 0); Step(2, 2); Step(4, 1); Step(5, 0);
    static const struct { uint32_t main, step; int on; uint8_t heldA; uint32_t landB; } kPairs[] = {
        {0, 1, 1, 1, LAND_HOLD}, {0, 4, 1, 0, LAND_NEXT}, {0, 2, 1, 0, LAND_NEXT}, {3, 4, 1, 1, LAND_HOLD}, {3, 1, 1, 0, LAND_NEXT},
        {3, 5, 1, 1, LAND_HOLD}, /* a step of no mission: the game's answer */
        {0, 4, 0, 1, LAND_HOLD}, {3, 1, 0, 1, LAND_HOLD}, {3, 2, 0, 0, LAND_NEXT}, /* a normal mission: the game's bytes */
    };
    for (size_t p = 0; p < sizeof kPairs / sizeof kPairs[0]; p++)
    {
        const int before = g_failed;
        co_unlock_live(kPairs[p].on);
        memset(frame, 0x5C, sizeof frame);
        in = SiteA(kPairs[p].main, kPairs[p].step, 0);
        int kept = Run(&in, game + CO_UNLOCK_A_SITE, frame);
        in.rsp = g_unlockIn.rsp;
        KeptA(&in, kept);
        CHECK(g_unlockOut.rsi == kPairs[p].heldA);
        in = SiteB(kPairs[p].main, kPairs[p].step, 0, frame);
        kept = Run(&in, game + CO_UNLOCK_B_SITE, frame);
        in.rsp = g_unlockIn.rsp;
        KeptB(&in, kept);
        CHECK(g_unlockOut.landed == kPairs[p].landB);
        if (g_failed != before) printf("  (pair %u: main %u, step %u, on %d)\n", (unsigned)p, kPairs[p].main, kPairs[p].step, kPairs[p].on);
    }
    co_unlock_live(0);
}

/* ---- what writes the note and the flag ----------------------------------------------------------------------------- */

/* The arcs' note: an objective's mission is its block's, none without a block or past the blocks; its record index and
 * id go with it. A level whose block is not the clusters' one gets no note. */
static void ArcNote(void)
{
    g_case = "the arcs' note";
    World(0);
    const uintptr_t lg = (uintptr_t)At(LEVEL), gen = (uintptr_t)At(GEN);
    static const uint32_t index[7] = {9, 3, 7, 12, 20, 5, 30}, id[7] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    static const uint8_t block[7] = {1, 2, 3, 0, 16, 17, 2}, expected[7] = {0, 1, CO_UNLOCK_NONE, CO_UNLOCK_NONE, 2, CO_UNLOCK_NONE, 1};
    uint8_t blockMission[16];
    memset(blockMission, CO_UNLOCK_NONE, sizeof blockMission);
    blockMission[0] = 0; blockMission[1] = 1; blockMission[15] = 2; /* block 3 knows no mission */
    CoXlTestArcUnlockNote(lg, gen, 7, index, id, block, blockMission);
    for (uint32_t o = 0; o < 7; o++) CHECK(co_unlock_noted(lg, gen, index[o], id[o]) == expected[o]);
    CHECK(co_unlock_noted(lg, gen, 0, id[0]) == CO_UNLOCK_NONE && co_unlock_noted(lg, gen, index[0], id[1]) == CO_UNLOCK_NONE);
    CHECK(fake_host_logged("complex operations: mission unlock: 4 objective locations in 3 missions (7 objectives on the map)"));
    co_unlock_forget();
    CoXlTestArcUnlockNote(lg, gen + 0x10000u, 7, index, id, block, blockMission);
    for (uint32_t o = 0; o < 7; o++) CHECK(co_unlock_noted(lg, gen + 0x10000u, index[o], id[o]) == CO_UNLOCK_NONE && co_unlock_noted(lg, gen, index[o], id[o]) == CO_UNLOCK_NONE);
    CHECK(fake_host_logged("complex operations: mission unlock: the level's block is not the clusters' one; every step holds every main"));
}

static uint32_t Target(uint32_t rva, uint8_t length) { int32_t d; memcpy(&d, g_image + rva + length - 4u, 4); return rva + length + (uint32_t)d; }

/* The world's value pages over the game's readers, as the world's install points them. */
static void Pages(void)
{
    co_pages_test_reset();
    for (size_t i = 0; i < sizeof kCoWorldReaders / sizeof kCoWorldReaders[0]; i++) memcpy(g_image + kCoWorldReaders[i].rva, kCoWorldReaders[i].bytes, kCoWorldReaders[i].length);
    for (int i = 0; i < 5; i++) memcpy(g_image + kCoWorldConstants[i], &kCoWorldConstantBits[i], 4);
    for (size_t i = 0; i < sizeof kCoPageReaders / sizeof kCoPageReaders[0]; i++)
    {
        memcpy(g_image + kCoPageReaders[i].rva, kCoPageReaders[i].bytes, kCoPageReaders[i].length);
        memcpy(g_image + Target(kCoPageReaders[i].rva, kCoPageReaders[i].length), &kCoPageReaders[i].value, 4);
    }
    for (size_t i = 0; i < sizeof kCoPageImmediates / sizeof kCoPageImmediates[0]; i++)
        memcpy(g_image + kCoPageImmediates[i].rva, kCoPageImmediates[i].bytes, kCoPageImmediates[i].at + 4u);
    CHECK(co_pages_install((uintptr_t)g_image, (CoGenPage*)(g_image + GEN_PAGE), (CoLivePage*)(g_image + LIVE_PAGE)) == NULL);
}

/* The stubs' flag follows play's world: on for a complex mission in play (a preview generated meanwhile leaves it), off
 * on the ship and for a normal mission. Every level init forgets the last generation's note. */
static void Wiring(void)
{
    g_case = "the flag and the note through level init and the ship";
    World(1);
    CoXlTestGame((uintptr_t)g_image);
    Pages();
    const uint8_t* page = co_unlock_test_page();
    const uintptr_t lg = (uintptr_t)At(LEVEL), gen = (uintptr_t)At(GEN);
    CHECK(page != NULL && page[0x1000] == 0);
    if (page == NULL) return;
    CoXlTestWorldWrite(8.0f, 1);
    CHECK(page[0x1000] == 1);
    CoXlTestWorldWrite(1.0f, 0); /* a war table's preview of a normal mission while the complex one plays */
    CHECK(page[0x1000] == 1);
    CoXlTestWorldShip();
    CHECK(page[0x1000] == 0);
    CoXlTestWorldWrite(8.0f, 0); /* a complex mission's preview on the ship */
    CHECK(page[0x1000] == 0);
    CoXlTestWorldWrite(8.0f, 1);
    CHECK(page[0x1000] == 1);
    CoXlTestWorldWrite(1.0f, 1);
    CHECK(page[0x1000] == 0);
    CoXlTestWorldWrite(8.0f, 1);
    Note();
    CHECK(co_unlock_noted(lg, gen, 1, 0xA1) == 0);
    CoXlTestWorldLevel(lg, 0); /* the next mission's level init: a normal one here */
    CHECK(page[0x1000] == 0 && co_unlock_noted(lg, gen, 1, 0xA1) == CO_UNLOCK_NONE);
    Note();
    CoXlTestWorldLevel(lg, 0x44); /* a war-table preview's level init forgets it too */
    CHECK(co_unlock_noted(lg, gen, 1, 0xA1) == CO_UNLOCK_NONE);
    co_pages_test_reset();
}

/* ---- the install's refusals ---------------------------------------------------------------------------------------- */

/* The page a write of the jump at site A led to: the first bytes of write `n`. */
static uint8_t* JumpedTo(int n)
{
    int32_t d;
    if (n >= g_fake.wroteCount || g_fake.wroteHead[n][0] != 0xE9) return NULL;
    memcpy(&d, g_fake.wroteHead[n] + 1, 4);
    return (uint8_t*)(g_fake.wrote[n] + 5 + (intptr_t)d);
}
static int Freed(const void* at)
{
    MEMORY_BASIC_INFORMATION m;
    return at != NULL && VirtualQuery(at, &m, sizeof m) != 0 && m.State == MEM_FREE;
}
static int SitesAsGame(void)
{
    return memcmp(g_image + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA) == 0 && memcmp(g_image + CO_UNLOCK_B_SITE, kCoUnlockB, sizeof kCoUnlockB) == 0;
}

static void Refusals(void)
{
    const uintptr_t game = (uintptr_t)g_image;
    uint8_t* page;
    g_case = "the install: another's code at a site or the location lookup";
    World(0);
    g_image[CO_UNLOCK_A_SITE + 20] = 0xCC;
    CHECK(co_unlock_install(game, LIST) != NULL && g_fake.wroteCount == 0 && co_unlock_test_page() == NULL);
    World(0);
    g_fake.noMatch[0] = 0x5D2F0Cu; /* the unit lookup the mission is found by */
    CHECK(co_unlock_install(game, LIST) != NULL && g_fake.wroteCount == 0 && co_unlock_test_page() == NULL && SitesAsGame());

    g_case = "the install: site A's write refused";
    World(0);
    g_fake.failWriteAt = 0;
    CHECK(co_unlock_install(game, LIST) != NULL && co_unlock_test_page() == NULL && g_fake.wroteCount == 1 && SitesAsGame());
    page = JumpedTo(0);
    CHECK(page != NULL && Freed(page));
    CHECK(fake_host_detour(CO_UNLOCK_MARKERS) == NULL && fake_host_detour(CO_UNLOCK_LINES) == NULL);

    g_case = "the install: site B's write refused, site A put back";
    World(0);
    g_fake.failWriteAt = 1;
    CHECK(co_unlock_install(game, LIST) != NULL && co_unlock_test_page() == NULL);
    CHECK(g_fake.wroteCount == 3 && g_fake.wrote[2] == game + CO_UNLOCK_A_SITE && SitesAsGame());
    page = JumpedTo(0);
    CHECK(page != NULL && Freed(page)); /* nothing jumps to it any more */
    CHECK(fake_host_detour(CO_UNLOCK_MARKERS) == NULL && fake_host_detour(CO_UNLOCK_LINES) == NULL);
    co_unlock_live(1); /* nothing installed: nothing written */
    g_fake.failWriteAt = -1;
    CHECK(co_unlock_install(game, LIST) == NULL && co_unlock_test_page() != NULL); /* and a later install may go in */

    g_case = "the install: site B's write refused, site A not put back";
    World(0);
    g_fake.failWriteAt = 1; g_fake.failWrites = 2;
    CHECK(co_unlock_install(game, LIST) != NULL && co_unlock_test_page() == NULL && g_fake.wroteCount == 3);
    page = JumpedTo(0);
    CHECK(page != NULL && !Freed(page) && g_image[CO_UNLOCK_A_SITE] == 0xE9); /* the game still jumps to it: kept */
    CHECK(page != NULL && memcmp(page + 9, kCoUnlockA, sizeof kCoUnlockA) == 0 && page[0x1000] == 0); /* and it runs the game's bytes */

    static const struct { uint32_t rva; const char* why; } kPins[] = {
        {CO_UNLOCK_MARKERS, "complex operations: mission unlock: the war table's marker source is not the game's code this mod was made for; the war table draws every step's lines"},
        {CO_UNLOCK_LINES, "complex operations: mission unlock: the war table's line draw is not the game's code this mod was made for; the war table draws every step's lines"},
        {0x14325A8u, "complex operations: mission unlock: the war table's line draw is not the game's code this mod was made for; the war table draws every step's lines"},
        {0x142CBDBu, "complex operations: mission unlock: the war table's shown preview is not the game's code this mod was made for; the war table draws every step's lines"},
    };
    for (size_t p = 0; p < sizeof kPins / sizeof kPins[0]; p++)
    {
        g_case = "the install: a hologram pin refused";
        World(0);
        g_fake.noMatch[0] = kPins[p].rva;
        CHECK(co_unlock_install(game, LIST) == NULL && co_unlock_test_page() != NULL && g_image[CO_UNLOCK_A_SITE] == 0xE9 && g_image[CO_UNLOCK_B_SITE] == 0xE9);
        CHECK(fake_host_logged(kPins[p].why) && g_fake.hookCount == 0);
    }

    g_case = "the install: the marker source's hook refused";
    World(0);
    g_fake.noHook = CO_UNLOCK_MARKERS;
    CHECK(co_unlock_install(game, LIST) == NULL && co_unlock_test_page() != NULL && g_image[CO_UNLOCK_A_SITE] == 0xE9 && g_image[CO_UNLOCK_B_SITE] == 0xE9);
    CHECK(fake_host_logged("hook refused: the war table's marker source (each marker's mission)"));
    CHECK(fake_host_logged("complex operations: mission unlock: the war table draws every step's lines"));
    CHECK(fake_host_detour(CO_UNLOCK_LINES) == NULL); /* no lines without the markers' missions */

    g_case = "the install: the line draw's hook refused";
    World(0);
    g_fake.noHook = CO_UNLOCK_LINES;
    CHECK(co_unlock_install(game, LIST) == NULL && co_unlock_test_page() != NULL);
    CHECK(fake_host_logged("hook refused: the war table's step lines (its own mission's mains)"));
    CHECK(fake_host_logged("complex operations: mission unlock: the war table draws every step's lines"));

    g_case = "the install: every hook in";
    World(0);
    CHECK(co_unlock_install(game, LIST) == NULL && fake_host_detour(CO_UNLOCK_MARKERS) != NULL && fake_host_detour(CO_UNLOCK_LINES) != NULL);
    CHECK(!fake_host_logged("draws every step's lines") && !fake_host_logged("hook refused"));
}

int main(void)
{
    g_case = "the rule";
    CHECK(co_unlock_rule(0, 0) && co_unlock_rule(2, 2) && !co_unlock_rule(0, 1) && !co_unlock_rule(1, 0));
    CHECK(co_unlock_rule(CO_UNLOCK_NONE, 1) && co_unlock_rule(1, CO_UNLOCK_NONE) && co_unlock_rule(CO_UNLOCK_NONE, CO_UNLOCK_NONE));

    g_case = "the note";
    World(0);
    Note();
    const uintptr_t lg = (uintptr_t)At(LEVEL), gen = (uintptr_t)At(GEN);
    CHECK(co_unlock_noted(lg, gen, 1, 0xA1) == 0 && co_unlock_noted(lg, gen, 4, 0xB1) == 1);
    CHECK(co_unlock_noted(lg, gen, 4, 0xB0) == CO_UNLOCK_NONE);   /* another objective at the record now */
    CHECK(co_unlock_noted(lg, gen, 5, 0xC0) == CO_UNLOCK_NONE);   /* a side objective: no mission */
    CHECK(co_unlock_noted(lg + 8, gen, 1, 0xA1) == CO_UNLOCK_NONE && co_unlock_noted(lg, gen + 8, 1, 0xA1) == CO_UNLOCK_NONE);
    co_unlock_forget();
    CHECK(co_unlock_noted(lg, gen, 1, 0xA1) == CO_UNLOCK_NONE);

    g_case = "a main waits for its own mission's steps";
    World(1);
    Note();
    const uintptr_t m = (uintptr_t)At(MANAGER);
    CHECK(co_unlock_holds(m, 0, 1) == 1 && co_unlock_holds(m, 0, 2) == 1 && co_unlock_holds(m, 3, 4) == 1);
    CHECK(co_unlock_holds(m, 0, 4) == 0 && co_unlock_holds(m, 3, 1) == 0 && co_unlock_holds(m, 3, 2) == 0);
    /* the game's answer where a mission is not known: a side objective, a location with no note, a unit with none */
    CHECK(co_unlock_holds(m, 0, 5) == 1 && co_unlock_holds(m, 6, 1) == 1 && co_unlock_holds(m, 3, 7) == 1 && co_unlock_holds(m, 7, 4) == 1);
    Put32(GEN + LIST + 0x108u * 4 + 0xD8, 0xEE); /* the record holds another objective now: asked again after a new note */
    Note();
    CHECK(co_unlock_holds(m, 0, 4) == 1);
    co_unlock_forget();
    CHECK(co_unlock_holds(m, 3, 1) == 1); /* no note: every step holds every main */
    Put32(GEN + LIST + 0x108u * 4 + 0xD8, 0xB1);
    Note();
    Put64(LEVEL + 0x2C8u, At(GEN + 0x10000u)); /* another generation block than the noted one */
    CHECK(co_unlock_holds(m, 3, 1) == 1);

    g_case = "the activation pass's stub";
    World(0);
    uint8_t* a = g_image + 0x3100000u; /* within a jump of the sites, as the install's page is */
    const uintptr_t flag = (uintptr_t)(g_image + 0x3001140u), holds = (uintptr_t)co_unlock_holds, game = (uintptr_t)g_image;
    CHECK(co_unlock_stub_a(a, (uintptr_t)a, game, flag, holds) == CO_UNLOCK_A_STUB);
    CHECK(a[0] == 0x80 && a[1] == 0x3D && Leads(a, 2) + 1u == 0x3001140u && a[7] == 0x75 && a[8] == 26);
    CHECK(memcmp(a + 9, kCoUnlockA, sizeof kCoUnlockA) == 0 && a[30] == 0xE9 && Leads(a, 31) == CO_UNLOCK_A_BACK); /* a normal mission: the game's */
    CHECK(memcmp(a + 35, kCoUnlockA, 8) == 0 && memcmp(a + 43, kCoUnlockA + 8, 5) == 0 && a[48] == 0x0F && a[49] == 0x84 && Leads(a, 50) == CO_UNLOCK_A_BACK);
    CHECK(a[54] == 0x52 && a[84] == 0x5A && memcmp(a + 70, &holds, 8) == 0 && a[87] == 0x0F && a[88] == 0x84 && Leads(a, 89) == CO_UNLOCK_A_BACK);
    CHECK(a[93] == 0xBE && a[94] == 1 && a[98] == 0xE9 && Leads(a, 99) == CO_UNLOCK_A_BACK);

    g_case = "the waiting pass's stub";
    uint8_t* b = a + CO_UNLOCK_A_STUB;
    CHECK(co_unlock_stub_b(b, (uintptr_t)b, game, flag, holds) == CO_UNLOCK_B_STUB);
    CHECK(b[0] == 0x80 && Leads(b, 2) + 1u == 0x3001140u && b[7] == 0x75 && b[8] == 20);
    CHECK(memcmp(b + 9, kCoUnlockB, 9) == 0 && b[18] == 0x0F && b[19] == 0x85 && Leads(b, 20) == CO_UNLOCK_B_HOLD && b[24] == 0xE9 && Leads(b, 25) == CO_UNLOCK_B_NEXT);
    CHECK(CO_UNLOCK_B_SITE + sizeof kCoUnlockB + kCoUnlockB[10] == CO_UNLOCK_B_HOLD); /* the game's jne leads where the stub's does */
    CHECK(memcmp(b + 29, kCoUnlockB, 9) == 0 && b[38] == 0x0F && b[39] == 0x84 && Leads(b, 40) == CO_UNLOCK_B_NEXT && memcmp(b + 74, &holds, 8) == 0);
    CHECK(b[64] == 0x48 && b[68] == 0x80 && b[100] == 0x0F && b[101] == 0x85 && Leads(b, 102) == CO_UNLOCK_B_HOLD && b[106] == 0xE9 && Leads(b, 107) == CO_UNLOCK_B_NEXT);

    g_case = "the install";
    World(0);
    CHECK(co_unlock_install(game, 0) != NULL && g_fake.wroteCount == 0);
    CHECK(co_unlock_install(game, LIST) == NULL && co_unlock_test_page() != NULL);
    {
        const uint8_t* page = co_unlock_test_page();
        int32_t d;
        CHECK(g_image[CO_UNLOCK_A_SITE] == 0xE9 && g_image[CO_UNLOCK_A_SITE + 20] == 0x90 && g_image[CO_UNLOCK_B_SITE] == 0xE9 && g_image[CO_UNLOCK_B_SITE + 10] == 0x90);
        memcpy(&d, g_image + CO_UNLOCK_A_SITE + 1, 4); CHECK(g_image + CO_UNLOCK_A_SITE + 5 + d == page);
        memcpy(&d, g_image + CO_UNLOCK_B_SITE + 1, 4); CHECK(g_image + CO_UNLOCK_B_SITE + 5 + d == page + 0x80);
        CHECK(page[0x1000] == 0 && memcmp(page + 9, kCoUnlockA, sizeof kCoUnlockA) == 0);
        co_unlock_live(1); CHECK(page[0x1000] == 1);
        co_unlock_live(0); CHECK(page[0x1000] == 0); /* a normal mission and the ship: the game's bytes run */
    }
    CHECK(fake_host_detour(CO_UNLOCK_MARKERS) != NULL && fake_host_detour(CO_UNLOCK_LINES) != NULL);
    {
        World(0);
        memset(g_image + CO_UNLOCK_B_SITE, 0xCC, 1); /* another's code at the waiting pass: nothing is written */
        CHECK(co_unlock_install(game, LIST) != NULL && g_fake.wroteCount == 0 && co_unlock_test_page() == NULL);
    }

    StubsRun();
    InstalledRun();
    ArcNote();
    Wiring();
    Refusals();

    g_case = "the war table's lines";
    World(1);
    Note();
    /* the shown preview is record 0: [R] 3, its ids at R+0x2234 */
    const uint32_t record = WORLD + 0xAE2A0u;
    Put64(0x347CEE8u, At(PREVIEWS)); Put32(PREVIEWS + 0x4168CCu, 0); Put32(record, 3);
    /* a descriptor objective with no place, mission 0's main and step, mission 1's main and step */
    g_outputs = 5;
    g_pos[0] = 0; g_pos[1] = Pos(100, 200); g_pos[2] = Pos(110, 210); g_pos[3] = Pos(300, 400); g_pos[4] = Pos(310, 410);
    for (uint32_t k = 0; k < 5; k++) g_ids[k] = 0x900u + k;
    uint64_t pos[8];
    CHECK(co_unlock_test_markers(lg, pos, NULL, (uint32_t*)(g_image + record + 0x2234u)) == 5);
    g_holo = calloc(1, 0x8B740u);
    *(uint32_t*)(g_holo + 0x8B730u) = 5;
    SLOT(1)[0x17C] = 1; /* the game's own no-line slot stays one */
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(2), 0, 0.5f);
    CHECK(g_drawn == 0x05u && g_alpha == 0.5f);              /* mission 0's step: not to mission 1's main and step */
    CHECK(SLOT(3)[0x17C] == 0 && SLOT(4)[0x17C] == 0 && SLOT(1)[0x17C] == 1);
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(4), 0, 0.5f);
    CHECK(g_drawn == 0x19u);                                 /* mission 1's step: slots 0, 3, 4 */
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(0), 0, 0.5f);
    CHECK(g_drawn == 0x1Du);                                 /* a slot of no mission hides nothing */
    *(uint32_t*)(g_image + record + 0x2234u + 12) = 0x777;   /* slot 3 is another marker now */
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(2), 0, 0.5f);
    CHECK(g_drawn == 0x0Du);
    Put32(record, 2);                                        /* not a shown record: the game's lines */
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(2), 0, 0.5f);
    CHECK(g_drawn == 0x1Du);
    Put32(record, 3);
    co_unlock_forget();                                      /* a generation with no note */
    CHECK(co_unlock_test_markers(lg, pos, NULL, (uint32_t*)(g_image + record + 0x2234u)) == 5);
    co_unlock_test_lines((uintptr_t)g_holo, 0, (uintptr_t)SLOT(2), 0, 0.5f);
    CHECK(g_drawn == 0x1Du);
    free(g_holo);

    g_case = "which slots a step skips";
    {
        const uint8_t mission[6] = {0, 0, 1, 1, CO_UNLOCK_NONE, 2};
        CHECK(co_unlock_hide(mission, 6, 0) == 0x2Cu && co_unlock_hide(mission, 6, 2) == 0x23u && co_unlock_hide(mission, 6, 4) == 0);
        CHECK(co_unlock_hide(mission, 6, 6) == 0 && co_unlock_hide(mission, 33, 0) == 0);
    }

    if (g_failed == 0) printf("unlock: all checks passed\n");
    return g_failed != 0;
}
