/* The sequential rule per mission: the order test's two stubs run here, at a stand-in of each site's frame (the
 * registers and stack slots the game's loop has there), with the flag off (the game's bytes) and on (another mission's
 * objective is no earlier one), and the install writes both sites or neither. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_unlock.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* A run's registers: in, and as they were at the landing pad. Offsets are the runner's (8 bytes each). */
typedef struct Regs
{
    uint64_t rbx, rbp, rdi, rsi, r12, r13, r14, r15, rcx, rdx, rax, stub, frame30, frame3c, rbp80, rspIn, rspOut;
} Regs;
_Static_assert(sizeof(Regs) == 17 * 8, "the runner's offsets");
Regs* g_runRegs;
uint64_t g_runRsp, g_runFlags, g_runTag, g_probeRsp;
uint8_t g_frameIn[0x100], g_frameOut[0x100];
uint32_t g_calls, g_answer, g_main, g_other;
uint64_t g_manager;

/* order_run(regs): builds the site's frame (0x77 bytes, then [rsp+0x30], [rsp+0x3c] and [rbp-0x80] with rbp = rsp+0x100,
 * as 0x5D2DA0's prologue leaves rbp; rsp 16-aligned), keeps a copy, loads the registers and jumps to the stub. A landing
 * pad puts its tag in r11 and jumps to order_land, which keeps the flags, registers and frame and returns from order_run.
 * order_holds is the stubs' callee: it records its arguments and rsp, then takes what any callee may (rax, rcx, rdx,
 * r8-r11, xmm0-5, the flags, its home space) before it answers g_answer. */
__asm__(
    ".intel_syntax noprefix\n"
    ".text\n"
    ".globl order_run\n"
    "order_run:\n"
    "  push rbx\n  push rbp\n  push rdi\n  push rsi\n  push r12\n  push r13\n  push r14\n  push r15\n"
    "  sub rsp, 0x108\n"
    "  mov qword ptr [rip + g_runRegs], rcx\n"
    "  mov qword ptr [rip + g_runRsp], rsp\n"
    "  mov qword ptr [rcx + 120], rsp\n"
    "  cld\n  mov rdi, rsp\n  mov ecx, 0x100\n  mov al, 0x77\n  rep stosb\n"
    "  mov rcx, qword ptr [rip + g_runRegs]\n"
    "  mov rax, qword ptr [rcx + 96]\n  mov qword ptr [rsp + 0x30], rax\n"
    "  mov eax, dword ptr [rcx + 104]\n  mov dword ptr [rsp + 0x3c], eax\n"
    "  mov rax, qword ptr [rcx + 112]\n  mov qword ptr [rsp + 0x80], rax\n"
    "  mov rsi, rsp\n  lea rdi, [rip + g_frameIn]\n  mov ecx, 0x100\n  rep movsb\n"
    "  mov rcx, qword ptr [rip + g_runRegs]\n"
    "  lea rbp, [rsp + 0x100]\n"
    "  mov rbx, qword ptr [rcx]\n  mov rdi, qword ptr [rcx + 16]\n  mov rsi, qword ptr [rcx + 24]\n"
    "  mov r12, qword ptr [rcx + 32]\n  mov r13, qword ptr [rcx + 40]\n  mov r14, qword ptr [rcx + 48]\n  mov r15, qword ptr [rcx + 56]\n"
    "  mov rdx, qword ptr [rcx + 72]\n  mov rax, qword ptr [rcx + 80]\n  mov r11, qword ptr [rcx + 88]\n  mov rcx, qword ptr [rcx + 64]\n"
    "  jmp r11\n"
    ".globl order_land\n"
    "order_land:\n"
    "  pushfq\n  pop qword ptr [rip + g_runFlags]\n"
    "  mov qword ptr [rip + g_runTag], r11\n"
    "  mov r11, qword ptr [rip + g_runRegs]\n"
    "  mov qword ptr [r11], rbx\n  mov qword ptr [r11 + 8], rbp\n  mov qword ptr [r11 + 16], rdi\n  mov qword ptr [r11 + 24], rsi\n"
    "  mov qword ptr [r11 + 32], r12\n  mov qword ptr [r11 + 40], r13\n  mov qword ptr [r11 + 48], r14\n  mov qword ptr [r11 + 56], r15\n"
    "  mov qword ptr [r11 + 64], rcx\n  mov qword ptr [r11 + 72], rdx\n  mov qword ptr [r11 + 80], rax\n  mov qword ptr [r11 + 128], rsp\n"
    "  mov rsp, qword ptr [rip + g_runRsp]\n"
    "  cld\n  mov rsi, rsp\n  lea rdi, [rip + g_frameOut]\n  mov ecx, 0x100\n  rep movsb\n"
    "  add rsp, 0x108\n"
    "  pop r15\n  pop r14\n  pop r13\n  pop r12\n  pop rsi\n  pop rdi\n  pop rbp\n  pop rbx\n"
    "  ret\n"
    ".globl order_fault\n"
    "order_fault:\n"
    "  mov r11d, 0xFA\n  jmp order_land\n"
    ".globl order_holds\n"
    "order_holds:\n"
    "  mov qword ptr [rip + g_probeRsp], rsp\n"
    "  mov qword ptr [rip + g_manager], rcx\n  mov dword ptr [rip + g_main], edx\n  mov dword ptr [rip + g_other], r8d\n"
    "  inc dword ptr [rip + g_calls]\n"
    "  movabs rax, 0xBADC0FFEE0DDF00D\n"
    "  mov qword ptr [rsp + 8], rax\n  mov qword ptr [rsp + 16], rax\n  mov qword ptr [rsp + 24], rax\n  mov qword ptr [rsp + 32], rax\n"
    "  mov rcx, rax\n  mov rdx, rax\n  mov r8, rax\n  mov r9, rax\n  mov r10, rax\n  mov r11, rax\n"
    "  movq xmm0, rax\n  movq xmm1, rax\n  movq xmm2, rax\n  movq xmm3, rax\n  movq xmm4, rax\n  movq xmm5, rax\n"
    "  push 0x8D5\n  popfq\n" /* CF, PF, AF, ZF, SF and OF set */
    "  mov eax, dword ptr [rip + g_answer]\n"
    "  ret\n"
    ".att_syntax\n");
void order_run(Regs* regs);
void order_land(void);
void order_holds(void);
void order_fault(void);

/* A stub that loses a register the game reads faults as the game would: the run ends at order_fault, a failed check
 * rather than a dead test. */
static volatile LONG g_running;
static LONG CALLBACK Fault(EXCEPTION_POINTERS* e)
{
    if (!g_running || e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    g_running = 0;
    e->ContextRecord->Rip = (DWORD64)(uintptr_t)order_fault;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void Go(Regs* r) { g_running = 1; order_run(r); g_running = 0; }

static uint8_t* g_image;
#define STUBS 0x3100000u
#define FLAG 0x3101800u

/* mov r11d, tag; jmp to a jump to order_land: 11 bytes, as the waiting pass's two targets lie 14 apart */
#define TO_LAND 0x5D4100u
static void Pad(uint32_t at, uint32_t tag)
{
    uint8_t* p = g_image + at;
    const int32_t rel = (int32_t)(TO_LAND - (at + 11u));
    p[0] = 0x41; p[1] = 0xBB; memcpy(p + 2, &tag, 4);
    p[6] = 0xE9; memcpy(p + 7, &rel, 4);
}
static void ToLand(void)
{
    uint8_t* p = g_image + TO_LAND;
    const uint64_t land = (uint64_t)(uintptr_t)order_land;
    p[0] = 0xFF; p[1] = 0x25; memset(p + 2, 0, 4); memcpy(p + 6, &land, 8);
}

static void Fresh(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_image = g_fake.image;
    co_unlock_test_reset();
    DWORD old;
    VirtualProtect(g_image + 0x5D3000u, 0x2000, PAGE_EXECUTE_READWRITE, &old);
    VirtualProtect(g_image + STUBS, 0x2000, PAGE_EXECUTE_READWRITE, &old);
    ToLand();
    Pad(CO_UNLOCK_C_NEXT, 0xC1); Pad(CO_UNLOCK_C_SKIP, 0xC2); Pad(CO_UNLOCK_D_NEXT, 0xD1); Pad(CO_UNLOCK_D_SKIP, 0xD2);
    g_calls = 0;
}

static void Base(Regs* r, uint64_t manager)
{
    memset(r, 0, sizeof *r);
    r->rbx = 0x1111; r->rdi = 7; r->rsi = 0x5151; r->r12 = 0x1212; r->r13 = 3; r->r14 = 0x1414; r->r15 = 0x1515;
    r->rcx = 0xC0C0; r->rdx = 0xD0D0; r->rax = 0xA0A0; r->frame30 = manager; r->frame3c = 5; r->rbp80 = 0x80808080;
}

/* Every non-volatile register, rsp and the frame as they were: the pass reads them after either way on. */
static int Kept(const Regs* in, const Regs* out)
{
    return out->rbx == in->rbx && out->rdi == in->rdi && out->rsi == in->rsi && out->r12 == in->r12 && out->r13 == in->r13 &&
        out->r14 == in->r14 && out->r15 == in->r15 && out->rbp == out->rspIn + 0x100 && out->rspOut == out->rspIn &&
        memcmp(g_frameIn, g_frameOut, sizeof g_frameIn) == 0;
}
/* What the dump's pass reads of the volatile registers after each way on. C: rax and rdx at 0x5D365C (0x5D3660,
 * 0x5D3664), none at 0x5D3686. D: rax and CF at 0x5D3EA7 (0x5D3EA9, the jae), rcx at both (0x5D3E84, 0x5D3EE7). */
static int LiveC(const Regs* out, uint64_t manager) { return g_runTag != 0xC1 || (out->rax == manager && out->rdx == 0x80808080); }
static int LiveD(const Regs* out, uint64_t manager, uint64_t net, int earlier)
{
    return out->rcx == manager && (g_runTag != 0xD1 || (out->rax == net && (int)(g_runFlags & 1) == earlier));
}

int main(void)
{
    static uint8_t net[0x64 * 16]; /* the manager's net records, +0x3C each one's order */
    static uint8_t manager[0x100];
    const uint64_t m = (uint64_t)(uintptr_t)manager, netAt = (uint64_t)(uintptr_t)net;
    memcpy(manager + 0x68, &netAt, 8);
    for (uint32_t i = 0; i < 16; i++) { const uint32_t order = 10 + i; memcpy(net + 0x64 * i + 0x3C, &order, 4); }
    Regs in, r;
    AddVectoredExceptionHandler(1, Fault);

    g_case = "the activation pass's order test, a normal mission";
    Fresh();
    uint8_t* c = g_image + STUBS;
    uint8_t* d = g_image + STUBS + 0x100;
    const uintptr_t game = (uintptr_t)g_image, flag = (uintptr_t)(g_image + FLAG), holds = (uintptr_t)order_holds;
    CHECK(co_unlock_stub_c(c, (uintptr_t)c, game, flag, holds) == CO_UNLOCK_C_STUB);
    CHECK(co_unlock_stub_d(d, (uintptr_t)d, game, flag, holds) == CO_UNLOCK_D_STUB);
    Base(&in, m); in.stub = (uint64_t)(uintptr_t)c;
    r = in; g_answer = 0; Go(&r);
    CHECK(g_runTag == 0xC1 && g_calls == 0 && LiveC(&r, m) && Kept(&in, &r)); /* the game's two loads */

    g_case = "the activation pass's order test, the same mission";
    g_image[FLAG] = 1;
    r = in; g_answer = 1; Go(&r);
    CHECK(g_runTag == 0xC1 && g_calls == 1 && g_manager == m && g_main == 3 && g_other == 7 && LiveC(&r, m) && Kept(&in, &r));
    CHECK((g_probeRsp & 15) == 8); /* a call from a 16-aligned rsp */

    g_case = "the activation pass's order test, another mission";
    r = in; g_answer = 0; Go(&r);
    CHECK(g_runTag == 0xC2 && g_calls == 2 && Kept(&in, &r)); /* as a group mismatch: 0x5D3686 loads what it reads */

    g_case = "the waiting pass's order test, a normal mission";
    g_image[FLAG] = 0;
    Base(&in, m); in.stub = (uint64_t)(uintptr_t)d; in.rcx = m; in.rbx = 0x64 * 2; in.r14 = 14; /* the other's order 12 < 14 */
    r = in; g_calls = 0; Go(&r);
    CHECK(g_runTag == 0xD1 && g_calls == 0 && LiveD(&r, m, netAt, 1) && Kept(&in, &r));
    in.r14 = 11; /* 12 is not earlier than 11 */
    r = in; Go(&r);
    CHECK(g_runTag == 0xD1 && LiveD(&r, m, netAt, 0) && Kept(&in, &r));

    g_case = "the waiting pass's order test, the same mission";
    g_image[FLAG] = 1; in.r14 = 14;
    r = in; g_answer = 1; Go(&r);
    CHECK(g_runTag == 0xD1 && g_calls == 1 && g_manager == m && g_main == 5 && g_other == 7 && LiveD(&r, m, netAt, 1) && Kept(&in, &r));
    CHECK((g_probeRsp & 15) == 8);
    in.r14 = 11;
    r = in; Go(&r);
    CHECK(g_runTag == 0xD1 && g_calls == 2 && LiveD(&r, m, netAt, 0) && Kept(&in, &r));

    g_case = "the waiting pass's order test, another mission";
    in.r14 = 14;
    r = in; g_answer = 0; g_calls = 0; Go(&r);
    CHECK(g_runTag == 0xD2 && g_calls == 1 && LiveD(&r, m, netAt, 1) && Kept(&in, &r)); /* the loop's top reads the manager in rcx */

    g_case = "the install writes both order tests";
    Fresh();
    memcpy(g_image + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA); memcpy(g_image + CO_UNLOCK_B_SITE, kCoUnlockB, sizeof kCoUnlockB);
    memcpy(g_image + CO_UNLOCK_C_SITE, kCoUnlockC, sizeof kCoUnlockC); memcpy(g_image + CO_UNLOCK_D_SITE, kCoUnlockD, sizeof kCoUnlockD);
    CHECK(co_unlock_install(game, 0x1000) == NULL);
    {
        const uint8_t* page = co_unlock_test_page();
        int32_t rel;
        CHECK(page != NULL && g_image[CO_UNLOCK_C_SITE] == 0xE9 && g_image[CO_UNLOCK_C_SITE + 8] == 0x90 && g_image[CO_UNLOCK_D_SITE] == 0xE9 && g_image[CO_UNLOCK_D_SITE + 8] == 0x90);
        memcpy(&rel, g_image + CO_UNLOCK_C_SITE + 1, 4); CHECK(page != NULL && g_image + CO_UNLOCK_C_SITE + 5 + rel == page + 0x100);
        memcpy(&rel, g_image + CO_UNLOCK_D_SITE + 1, 4); CHECK(page != NULL && g_image + CO_UNLOCK_D_SITE + 5 + rel == page + 0x180);
        CHECK(page != NULL && memcmp(page + 0x100 + 9, kCoUnlockC, 9) == 0 && memcmp(page + 0x180 + 9, kCoUnlockD, 9) == 0);
    }

    g_case = "another's code at an order test: neither is written, the steps' rule stands";
    Fresh();
    memcpy(g_image + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA); memcpy(g_image + CO_UNLOCK_B_SITE, kCoUnlockB, sizeof kCoUnlockB);
    memcpy(g_image + CO_UNLOCK_C_SITE, kCoUnlockC, sizeof kCoUnlockC); memcpy(g_image + CO_UNLOCK_D_SITE, kCoUnlockD, 8);
    CHECK(co_unlock_install(game, 0x1000) == NULL && g_image[CO_UNLOCK_A_SITE] == 0xE9);
    CHECK(memcmp(g_image + CO_UNLOCK_C_SITE, kCoUnlockC, sizeof kCoUnlockC) == 0 && fake_host_logged("every mission's earlier ones"));

    g_case = "the second order test's write fails: the first is put back";
    Fresh();
    memcpy(g_image + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA); memcpy(g_image + CO_UNLOCK_B_SITE, kCoUnlockB, sizeof kCoUnlockB);
    memcpy(g_image + CO_UNLOCK_C_SITE, kCoUnlockC, sizeof kCoUnlockC); memcpy(g_image + CO_UNLOCK_D_SITE, kCoUnlockD, sizeof kCoUnlockD);
    g_fake.failWriteAt = 3; /* A, B, C, then D */
    CHECK(co_unlock_install(game, 0x1000) == NULL && g_image[CO_UNLOCK_B_SITE] == 0xE9);
    CHECK(memcmp(g_image + CO_UNLOCK_C_SITE, kCoUnlockC, sizeof kCoUnlockC) == 0 && memcmp(g_image + CO_UNLOCK_D_SITE, kCoUnlockD, sizeof kCoUnlockD) == 0);
    CHECK(fake_host_logged("the waiting pass's order test could not be written"));

    if (g_failed == 0) printf("unlock order: all checks passed\n");
    return g_failed != 0;
}
