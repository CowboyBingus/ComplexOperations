/* The frame-time levers a release ships: each acts on a complex level only, and every other level runs the game's
 * code. The absent SpeedTree cells' switched site runs here for real, through a thunk in the fake exe, so both paths
 * are checked by what they write. Run on the fake game. */
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestLive(int complex);
void CoXlTestGame(uintptr_t game);
void CoXlTestWorldShip(void);
int CoXlTestZeroBox(int reset);
int CoXlTestZeroBoxPath(void);
void CoXlTestLevers(uintptr_t game);
void CoXlTestLwNavSchedInstall(uintptr_t exe, uintptr_t stub);
void CoXlTestVboxFrame(void);
int CoXlTestNavJob(DWORD* tid);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define ZB_SITE 0x21265Du
#define ZB_BACK 0x21267Cu
#define ZB_EMPTY 0x16AA990u
#define ZB_THUNK 0x100000u
#define NJ_TICK 0x7B8A10u
#define NJ_START 0x7B7590u
#define VB_FN 0x2059A0u
#define VB_THUNK 0x134C140u

static const uint8_t kZbPin[31] = {0x0F, 0x57, 0xC0, 0x41, 0x0F, 0x11, 0x46, 0x58, 0x0F, 0x11, 0x45, 0x10, 0x41, 0x0F, 0x28, 0xC1, 0x41, 0x0F,
    0x14, 0xC1, 0xF2, 0x0F, 0x11, 0x45, 0x20, 0xF2, 0x41, 0x0F, 0x11, 0x46, 0x68};

/* exe+ZB_THUNK(cell, locals, xmm9): r14 = cell and rbp = locals as 0x212150 has them, then into the block; its end
 * (ZB_BACK) puts them back and returns. xmm9 is saved, as the Windows ABI wants. */
typedef void (*ZbThunk)(void* cell, void* locals, const float* x9);
static void ZbThunkWrite(uint8_t* exe)
{
    static const uint8_t in[] = {0x41, 0x56, 0x55, 0x48, 0x83, 0xEC, 0x28, 0xF3, 0x44, 0x0F, 0x7F, 0x4C, 0x24, 0x10, 0x49, 0x89, 0xCE, 0x48, 0x89,
        0xD5, 0x45, 0x0F, 0x10, 0x08, 0xE9};
    static const uint8_t out[] = {0xF3, 0x44, 0x0F, 0x6F, 0x4C, 0x24, 0x10, 0x48, 0x83, 0xC4, 0x28, 0x5D, 0x41, 0x5E, 0xC3};
    static const uint8_t empty[16] = {0xFF, 0xFF, 0x7F, 0x7F, 0xFF, 0xFF, 0x7F, 0x7F, 0xFF, 0xFF, 0x7F, 0x7F, 0xFF, 0xFF, 0x7F, 0xFF};
    memcpy(exe + ZB_THUNK, in, sizeof in);
    const int32_t rel = (int32_t)((int64_t)ZB_SITE - (int64_t)(ZB_THUNK + sizeof in + 4u));
    memcpy(exe + ZB_THUNK + sizeof in, &rel, 4);
    memcpy(exe + ZB_SITE, kZbPin, sizeof kZbPin);
    memcpy(exe + ZB_BACK, out, sizeof out);
    memcpy(exe + ZB_EMPTY, empty, sizeof empty);
    DWORD old;
    VirtualProtect(exe + ZB_THUNK, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    VirtualProtect(exe + (ZB_SITE & ~0xFFFu), 0x1000, PAGE_EXECUTE_READWRITE, &old);
}

/* One run of the block on a fresh cell: 1 when it wrote the game's zero box (and the game's locals), 2 when it wrote
 * the type's empty box and left the locals alone, 0 for anything else. The stamp after the box is never touched. */
static int ZbRun(uint8_t* exe)
{
    uint8_t cell[0xE0], locals[0x40];
    const float x9[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    memset(cell, 0x11, sizeof cell);
    memset(locals, 0x22, sizeof locals);
    ((ZbThunk)(exe + ZB_THUNK))(cell, locals, x9);
    float box[6];
    memcpy(box, cell + 0x58, sizeof box);
    int stampKept = 1;
    for (int i = 0x70; i < 0xE0; i++) stampKept &= cell[i] == 0x11;
    for (int i = 0; i < 0x58; i++) stampKept &= cell[i] == 0x11;
    int zero = 1, empty = 1, localsWritten = 1, localsKept = 1;
    for (int k = 0; k < 6; k++) { zero &= box[k] == 0.0f; empty &= box[k] == (k < 3 ? FLT_MAX : -FLT_MAX); }
    for (int i = 0x10; i < 0x28; i++) localsWritten &= locals[i] == 0;
    for (int i = 0; i < 0x40; i++) localsKept &= locals[i] == 0x22;
    if (!stampKept) return 0;
    if (zero && localsWritten) return 1;
    if (empty && localsKept) return 2;
    return 0;
}

static DWORD WINAPI Idle(LPVOID unused) { (void)unused; return 0; }

/* The nav job's tick and start, and the vehicle box query, as the game would call them. */
static uintptr_t g_args[4];
static int g_origCalls;
static uintptr_t OrigFour(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
{
    g_args[0] = a; g_args[1] = b; g_args[2] = c; g_args[3] = d; g_origCalls++;
    return 0xABCDu;
}
typedef uintptr_t (*FourFn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static int g_boxCalls;
static void* OrigBox(void* out, uint32_t unit, uint32_t flag)
{
    g_boxCalls++;
    memset(out, (int)(g_boxCalls & 0xFF), 0x4C);
    return out;
}
typedef void* (*BoxThunk)(void* detour, void* out, uint32_t unit, uint32_t flag);
/* game+VB_THUNK(detour, out, unit, flag): calls the detour from game+0x134C151, so it returns to the motion pass's site. */
static void VbThunkWrite(uint8_t* image)
{
    static const uint8_t t[] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xC8, 0x48, 0x89, 0xD1, 0x4C, 0x89, 0xC2, 0x4D, 0x89, 0xC8, 0x90, 0xFF, 0xD0,
        0x48, 0x83, 0xC4, 0x28, 0xC3};
    memcpy(image + VB_THUNK, t, sizeof t);
    DWORD old;
    VirtualProtect(image + (VB_THUNK & ~0xFFFu), 0x2000, PAGE_EXECUTE_READWRITE, &old);
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    const uintptr_t game = (uintptr_t)g_fake.image, e = (uintptr_t)exe;
    CoXlTestGame(game);
    ZbThunkWrite(exe);

    g_case = "foreign bytes at the absent cells' site";
    exe[ZB_SITE + 3] = 0x90;
    CoXlTestLive(0);
    CHECK(CoXlTestZeroBox(1) == -1);
    CHECK(exe[ZB_SITE] == 0x0F && exe[ZB_SITE + 3] == 0x90); /* refused, not written */
    CHECK(CoXlTestZeroBox(0) == -1 && exe[ZB_SITE] == 0x0F); /* and refused for good */
    exe[ZB_SITE + 3] = kZbPin[3];

    g_case = "the game's block before any install";
    CHECK(ZbRun(exe) == 1);

    g_case = "a thread stopped inside the site";
    {
        HANDLE t = CreateThread(NULL, 0, Idle, NULL, CREATE_SUSPENDED, NULL);
        CONTEXT ctx = {0};
        ctx.ContextFlags = CONTEXT_CONTROL;
        int placed = t != NULL && GetThreadContext(t, &ctx);
        ctx.Rip = e + ZB_SITE + 3u; /* the second instruction: between the old and the new boundaries */
        placed = placed && SetThreadContext(t, &ctx);
        CHECK(placed);
        CHECK(CoXlTestZeroBox(1) == 0); /* not written; tried again later */
        CHECK(memcmp(exe + ZB_SITE, kZbPin, sizeof kZbPin) == 0);
        CHECK(ZbRun(exe) == 1);
        if (t != NULL) { TerminateThread(t, 0); WaitForSingleObject(t, 5000); CloseHandle(t); }
    }

    g_case = "installed on the ship";
    CHECK(CoXlTestZeroBox(0) == 1);
    CHECK(exe[ZB_SITE] == 0xE9);
    for (int i = 5; i < 31; i++) CHECK(exe[ZB_SITE + i] == 0xCC);
    CHECK(CoXlTestZeroBoxPath() == 0);
    CHECK(ZbRun(exe) == 1); /* the game's 31 bytes, run from the switch's page */

    g_case = "a complex level";
    CoXlTestLive(1);
    CHECK(CoXlTestZeroBoxPath() == 1);
    CHECK(ZbRun(exe) == 2);

    g_case = "the ship after it";
    CoXlTestWorldShip();
    CHECK(CoXlTestZeroBoxPath() == 0);
    CHECK(ZbRun(exe) == 1);

    g_case = "a normal level, and a complex one again";
    CoXlTestLive(0);
    CHECK(CoXlTestZeroBoxPath() == 0 && ZbRun(exe) == 1);
    CoXlTestLive(1);
    CHECK(CoXlTestZeroBoxPath() == 1 && ZbRun(exe) == 2);
    CoXlTestLive(0);
    CHECK(CoXlTestZeroBoxPath() == 0 && ZbRun(exe) == 1);

    /* The nav job: its install's reads as the reviewed build has them; its object is not, so the huge-block cache
     * stays out and a complex level runs mode 1 here. */
    g_case = "the nav job and vehicle box hooks";
    const float scale[4] = {2.0f, 2.0f, 0.0f, 0.0f};
    memcpy(exe + 0x16AA6E0u, scale, sizeof scale);
    const uintptr_t heapAt = e + 0x1985330u, heapVt = e + 0x140FD00u, heapAlloc = e + 0x99D750u, heapFree = e + 0x99D7A0u;
    memcpy(exe + 0x1899880u, &heapAt, 8); memcpy(exe + 0x1985330u, &heapVt, 8);
    memcpy(exe + 0x140FD08u, &heapAlloc, 8); memcpy(exe + 0x140FD10u, &heapFree, 8);
    g_fake.originals[0].target = e + NJ_TICK; g_fake.originals[0].original = (void*)OrigFour;
    g_fake.originals[1].target = e + NJ_START; g_fake.originals[1].original = (void*)OrigFour;
    g_fake.originals[2].target = e + VB_FN; g_fake.originals[2].original = (void*)OrigBox;
    VbThunkWrite(g_fake.image);
    CoXlTestLive(0);
    /* The nav hold goes in first and writes its own call over the tick's scheduler call (0x7B8ABA), which the nav job
     * lever pins; the host's check fails on that site from then on, as it does on any rewritten code. */
    static const uint8_t sched[5] = {0xE8, 0x41, 0xF7, 0xFF, 0xFF};
    memcpy(exe + 0x7B8ABAu, sched, sizeof sched);
    CoXlTestLwNavSchedInstall(e, e + 0x7C0000u);
    CHECK(fake_host_logged("living world: nav: the tick's scheduler call goes through the hold (0x7B8ABA)"));
    CHECK(exe[0x7B8ABAu] == 0xE8 && memcmp(exe + 0x7B8ABAu, sched, sizeof sched) != 0);
    g_fake.noMatchExe[0] = 0x7B8ABAu;
    CoXlTestLevers(game);
    CHECK(fake_host_logged("living world: nav job lever installed (mode ") && !fake_host_logged("is not the reviewed code") &&
        !fake_host_logged("nav job lever not installed"));
    FourFn tick = (FourFn)fake_host_detour(e + NJ_TICK - game), start = (FourFn)fake_host_detour(e + NJ_START - game);
    void* box = fake_host_detour(e + VB_FN - game);
    CHECK(tick != NULL && start != NULL && box != NULL);
    if (tick == NULL || start == NULL || box == NULL) { printf("xllevers: not hooked\n"); return 1; }
    DWORD tid = 1;

    g_case = "the nav job on a normal level";
    g_origCalls = 0;
    CHECK(tick(1, 2, 3, 4) == 0xABCDu && g_origCalls == 1 && g_args[0] == 1 && g_args[3] == 4);
    CHECK(start(5, 6, 7, 8) == 0xABCDu && g_origCalls == 2 && g_args[0] == 5 && g_args[2] == 7 && g_args[3] == 8);
    CHECK(CoXlTestNavJob(&tid) == 0 && tid == 0); /* nothing kept: not even the tick's thread */

    g_case = "the nav job on a complex level";
    CoXlTestLive(1);
    CHECK(tick(1, 2, 3, 4) == 0xABCDu && g_args[1] == 2);
    CHECK(CoXlTestNavJob(&tid) == 1 && tid == GetCurrentThreadId());

    g_case = "the nav job after the ship";
    CoXlTestWorldShip(); /* on the tick's thread: mode 0 at once */
    CHECK(CoXlTestNavJob(NULL) == 0);
    g_origCalls = 0;
    CHECK(tick(9, 10, 11, 12) == 0xABCDu && g_origCalls == 1 && g_args[0] == 9 && g_args[3] == 12);
    CHECK(start(13, 14, 15, 16) == 0xABCDu && g_origCalls == 2 && g_args[1] == 14);
    CHECK(CoXlTestNavJob(NULL) == 0);

    g_case = "the vehicle boxes";
    uint8_t out[0x4C];
    BoxThunk call = (BoxThunk)(g_fake.image + VB_THUNK);
    CoXlTestLive(0);
    CoXlTestVboxFrame(); /* a frame of the tick cannot run on a normal level; even if it did, the hook stays the game's */
    g_boxCalls = 0;
    call(box, out, 77, 1); call(box, out, 77, 1);
    CHECK(g_boxCalls == 2 && out[0] == 2);
    CoXlTestLive(1);
    CoXlTestVboxFrame();
    call(box, out, 77, 1);
    memset(out, 0, sizeof out);
    call(box, out, 77, 1);
    CHECK(g_boxCalls == 3 && out[0] == 3); /* the second is the first's copy */
    CoXlTestWorldShip();
    call(box, out, 77, 1);
    CHECK(g_boxCalls == 4 && out[0] == 4); /* left: the game's query again */
    CoXlTestLive(0);
    call(box, out, 77, 1);
    CHECK(g_boxCalls == 5 && out[0] == 5);

    if (g_failed) { printf("xllevers: %d checks failed\n", g_failed); return 1; }
    printf("xllevers: all cases passed\n");
    return 0;
}
