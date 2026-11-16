/* Extra large maps: what the physics step spends on removals (included after difficulty_stream_layers.c). A frame that
 * removes stamps costs ~3 ms more world update whatever the count (ANALYSIS-r2a §2.3); these pass-through thunks time
 * the removal path in the step and count the bodies it wakes, split by the frame's kind (removal, spawn, other):
 *
 * - 0xD0F440 hknpWorld::removeBodies(this, ids, n, mode): rcx/rdx/r8d/r9d spilled at entry (0xD0F443..0xD0F44B); reads
 *   of its frame stop at the r9 home (rbp+0x818 = entry+0x20; rbp = entry-0x7F8): no stack argument; xmm0 is first
 *   written (0xD0F766); returns at 0xD0FC3E.
 * - 0xD41AF0 the broad phase's remove (vtable 0x14B4AB0 +0x30): rcx/rdx/r8d; r9 first written (0xD41B30); no read past
 *   its own frame (pushes 0x38 + 0x60, nothing at rsp+0xC0 and up); xmm0 first written (0xD41BE8).
 * - 0xD42550 the leaf renumber: rcx only; touches no xmm; returns at 0xD4260A.
 * - 0xDE1690 the activation of one body: counted only (lock inc, then a jump to the original), no register but the flags.
 *
 * MSVC may keep values in volatile registers across a call to a leaf it knows (0xC05360, co_xl.c CoIndexQueryThunk), so a
 * timed thunk saves rcx, rdx, r8-r11 and xmm0-5 before its timing call and puts them back for the original, then saves
 * rax, rcx, rdx, r8-r11 and xmm0-5 as the original left them and restores those after its second timing call: the
 * caller sees the registers the original gave it. None of the four branches into its first 7 bytes (the hook's). */

#define ST_REMOVE_RVA 0xD0F440u
#define ST_BPREMOVE_RVA 0xD41AF0u
#define ST_RENUMBER_RVA 0xD42550u
#define ST_ACTIVATE_RVA 0xDE1690u
static const char kStRemovePin[] = "48 8B C4 44 89 48 20 44 89 40 18 48 89 48 08 55 41 54 48 8D A8 08 F8 FF FF";
static const char kStBpRemovePin[] = "48 89 54 24 10 53 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 48 8B F1 49 63 D8";
static const char kStRenumberPin[] = "48 89 5C 24 08 48 89 7C 24 10 48 8B 81 30 01 00 00 45 33 D2";
static const char kStActivatePin[] = "48 83 EC 28 4C 8B 11 41 8B D0 81 E2 FF FF FF 00";

enum { ST_REMOVE, ST_BPREMOVE, ST_RENUMBER, ST_TIMED, ST_ACTIVATE = ST_TIMED, ST_SLOTS };
enum { ST_KINDS = 3 }; /* the frame just closed: 0 no change, 1 a removal, 2 spawns only */

void* g_stOrig0; void* g_stOrig1; void* g_stOrig2; void* g_stOrig3; /* the originals, read by the thunks */
volatile LONG64 g_stActivations;                                       /* the activation thunk's count */
static volatile LONG64 g_stFrameTicks[ST_TIMED], g_stFrameCalls[ST_TIMED];
static struct
{
    int on;
    LONG64 freq;
    LONG64 lastActivations;
    uint32_t frames[ST_KINDS];
    double ms[ST_KINDS][ST_TIMED], most[ST_KINDS][ST_TIMED];
    uint64_t calls[ST_KINDS][ST_SLOTS];
} g_st;

int64_t StNow(void);
void StAdd(uint32_t slot, int64_t start);
int64_t StNow(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
void StAdd(uint32_t slot, int64_t start)
{
    if (slot >= ST_TIMED) return;
    InterlockedExchangeAdd64(&g_stFrameTicks[slot], StNow() - start);
    InterlockedIncrement64(&g_stFrameCalls[slot]);
}

/* A timed thunk: 0xC8 of frame (entry rsp is 8 mod 16, so calls from it are aligned), pre-call registers at 0x20..0x58
 * and 0x60..0xBF, the start at 0xC0; the original's home space is 0x00..0x1F. */
#define ST_THUNK(name, slot, orig)                                                                                         \
    __asm__(".text\n.p2align 4\n.globl " #name "\n" #name ":\n"                                                          \
            "  .seh_proc " #name "\n  subq $0xC8, %rsp\n  .seh_stackalloc 0xC8\n  .seh_endprologue\n"                      \
            "  movq %rcx, 0x20(%rsp)\n  movq %rdx, 0x28(%rsp)\n  movq %r8, 0x30(%rsp)\n  movq %r9, 0x38(%rsp)\n"         \
            "  movq %r10, 0x40(%rsp)\n  movq %r11, 0x48(%rsp)\n"                                                         \
            "  movdqu %xmm0, 0x60(%rsp)\n  movdqu %xmm1, 0x70(%rsp)\n  movdqu %xmm2, 0x80(%rsp)\n"                        \
            "  movdqu %xmm3, 0x90(%rsp)\n  movdqu %xmm4, 0xA0(%rsp)\n  movdqu %xmm5, 0xB0(%rsp)\n"                        \
            "  callq StNow\n  movq %rax, 0xC0(%rsp)\n"                                                                    \
            "  movq 0x20(%rsp), %rcx\n  movq 0x28(%rsp), %rdx\n  movq 0x30(%rsp), %r8\n  movq 0x38(%rsp), %r9\n"         \
            "  movq 0x40(%rsp), %r10\n  movq 0x48(%rsp), %r11\n"                                                         \
            "  movdqu 0x60(%rsp), %xmm0\n  movdqu 0x70(%rsp), %xmm1\n  movdqu 0x80(%rsp), %xmm2\n"                        \
            "  movdqu 0x90(%rsp), %xmm3\n  movdqu 0xA0(%rsp), %xmm4\n  movdqu 0xB0(%rsp), %xmm5\n"                        \
            "  callq *" #orig "(%rip)\n"                                                                                  \
            "  movq %rax, 0x20(%rsp)\n  movq %rcx, 0x28(%rsp)\n  movq %rdx, 0x30(%rsp)\n  movq %r8, 0x38(%rsp)\n"         \
            "  movq %r9, 0x40(%rsp)\n  movq %r10, 0x48(%rsp)\n  movq %r11, 0x50(%rsp)\n"                                 \
            "  movdqu %xmm0, 0x60(%rsp)\n  movdqu %xmm1, 0x70(%rsp)\n  movdqu %xmm2, 0x80(%rsp)\n"                        \
            "  movdqu %xmm3, 0x90(%rsp)\n  movdqu %xmm4, 0xA0(%rsp)\n  movdqu %xmm5, 0xB0(%rsp)\n"                        \
            "  movl $" #slot ", %ecx\n  movq 0xC0(%rsp), %rdx\n  callq StAdd\n"                                           \
            "  movq 0x20(%rsp), %rax\n  movq 0x28(%rsp), %rcx\n  movq 0x30(%rsp), %rdx\n  movq 0x38(%rsp), %r8\n"         \
            "  movq 0x40(%rsp), %r9\n  movq 0x48(%rsp), %r10\n  movq 0x50(%rsp), %r11\n"                                  \
            "  movdqu 0x60(%rsp), %xmm0\n  movdqu 0x70(%rsp), %xmm1\n  movdqu 0x80(%rsp), %xmm2\n"                        \
            "  movdqu 0x90(%rsp), %xmm3\n  movdqu 0xA0(%rsp), %xmm4\n  movdqu 0xB0(%rsp), %xmm5\n"                        \
            "  addq $0xC8, %rsp\n  retq\n  .seh_endproc\n")
void StThunkRemove(void);
void StThunkBpRemove(void);
void StThunkRenumber(void);
void StThunkActivate(void);
ST_THUNK(StThunkRemove, 0, g_stOrig0);
ST_THUNK(StThunkBpRemove, 1, g_stOrig1);
ST_THUNK(StThunkRenumber, 2, g_stOrig2);
__asm__(".text\n.p2align 4\n.globl StThunkActivate\nStThunkActivate:\n"
        "  lock incq g_stActivations(%rip)\n  jmpq *g_stOrig3(%rip)\n");

/* The four hooks, all or none counted on: a refused pin or hook leaves the ones in passing through (they only time). */
static const char* StInstall(uintptr_t exe)
{
    char t[4] = "";
    if (g_st.on) return NULL;
    if (GetEnvironmentVariableA("HELLFORGE_XL_STEP_TIMERS", t, sizeof t) > 0 && t[0] == '0') return "off (HELLFORGE_XL_STEP_TIMERS=0)";
    if (exe == 0) return "the exe is not loaded";
    if (!ApiCodeMatches(exe + ST_REMOVE_RVA, kStRemovePin) || !ApiCodeMatches(exe + ST_BPREMOVE_RVA, kStBpRemovePin) ||
        !ApiCodeMatches(exe + ST_RENUMBER_RVA, kStRenumberPin) || !ApiCodeMatches(exe + ST_ACTIVATE_RVA, kStActivatePin))
        return "a removal path function is not the reviewed code";
    if (LoaderHook("extra large maps: step timer, removeBodies", exe + ST_REMOVE_RVA, (void*)StThunkRemove, &g_stOrig0) || g_stOrig0 == NULL ||
        LoaderHook("extra large maps: step timer, broad phase remove", exe + ST_BPREMOVE_RVA, (void*)StThunkBpRemove, &g_stOrig1) || g_stOrig1 == NULL ||
        LoaderHook("extra large maps: step timer, renumber", exe + ST_RENUMBER_RVA, (void*)StThunkRenumber, &g_stOrig2) || g_stOrig2 == NULL ||
        LoaderHook("extra large maps: step counter, activations", exe + ST_ACTIVATE_RVA, (void*)StThunkActivate, &g_stOrig3) || g_stOrig3 == NULL)
        return "a hook was refused";
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_st.freq = f.QuadPart;
    g_stActivations = 0; g_st.lastActivations = 0;
    g_st.on = 1;
    return NULL;
}

/* Each streamer tick: the step since the last tick (the frame just closed's world update) under that frame's kind. */
static void StFrame(int kind)
{
    if (!g_st.on || g_st.freq <= 0) return;
    const int k = kind == 1 || kind == 3 ? 1 : kind == 2 ? 2 : 0;
    g_st.frames[k]++;
    for (int s = 0; s < ST_TIMED; s++)
    {
        const double ms = (double)InterlockedExchange64(&g_stFrameTicks[s], 0) * 1000.0 / (double)g_st.freq;
        g_st.ms[k][s] += ms; if (ms > g_st.most[k][s]) g_st.most[k][s] = ms;
        g_st.calls[k][s] += (uint64_t)InterlockedExchange64(&g_stFrameCalls[s], 0);
    }
    const LONG64 a = g_stActivations;
    g_st.calls[k][ST_ACTIVATE] += (uint64_t)(a - g_st.lastActivations);
    g_st.lastActivations = a;
}

static void StNote(void)
{
    if (!g_st.on) return;
    static const char* const kKind[ST_KINDS] = {"other frames", "removal frames", "spawn frames"};
    char part[ST_KINDS][200];
    for (int k = 0; k < ST_KINDS; k++)
    {
        const double n = g_st.frames[k] ? (double)g_st.frames[k] : 1.0;
        snprintf(part[k], sizeof part[k], "%u %s: %.2f ms (the most %.2f, %llu calls), %.2f ms (the most %.2f), %.2f ms (the most %.2f), %.1f activations a frame",
            g_st.frames[k], kKind[k], g_st.ms[k][ST_REMOVE] / n, g_st.most[k][ST_REMOVE], (unsigned long long)g_st.calls[k][ST_REMOVE],
            g_st.ms[k][ST_BPREMOVE] / n, g_st.most[k][ST_BPREMOVE], g_st.ms[k][ST_RENUMBER] / n, g_st.most[k][ST_RENUMBER], (double)g_st.calls[k][ST_ACTIVATE] / n);
    }
    Note("difficulty: extra large maps: step removal timers in 10 s (removeBodies 0xD0F440, its broad phase remove 0xD41AF0, the renumber 0xD42550 a frame; "
        "activations 0xDE1690): %s; %s; %s", part[1], part[2], part[0]);
    memset(g_st.frames, 0, sizeof g_st.frames); memset(g_st.ms, 0, sizeof g_st.ms); memset(g_st.most, 0, sizeof g_st.most); memset(g_st.calls, 0, sizeof g_st.calls);
}
