/* The render thread's frame, split (research and test builds; co_rframe.h for the window and its lines). The game
 * thread's frame fence (0x8A7C4) waits on the render thread's previous frame, so a late render frame shows on the
 * game thread only as fence: this names the render frame's parts.
 *
 * Static basis (exe RVAs, reviewed in the live dump; ANALYSIS-r1 §1):
 * - The render loop 0x113E10 calls the dispatcher 0x113010(r, message, payload) only at 0x113EEE; the type is the word
 *   [message+0xC] (0x113023); 0x0B ends a frame (0x10A950). Between messages the loop polls jobs (0x626900 at 0x113F1D).
 * - Message 7's waits by call site: 0x627160 from 0x1EC4AA (cull) and 0x1E2FA1 (LOD), 0x6097A0 from 0x1E3E64 (LOD),
 *   0x213540 from 0x1E3E2D (SpeedTree view task spin 0x2135E0; five arguments, [rsp+0x20] = r15d), 0x252460 from
 *   0x1EDA81 (the generator join); the dispatch 0x6CECB0 calls 0x6CC130 or 0x6CA5F0 with edx = [rdi+0x5C8] records.
 * - Message 8: 0x1E9540 calls 0x1E84A0 (five arguments, the fifth at entry+0x28 read at 0x1E86AC) at 0x1E959B on the
 *   stream rdx = cmdif+0xB8: nodes from [[rdx+0x10]], next at +0, type the dword at node + word [node+0xE], 1..0x15
 *   (0x1E8510..0x1E852E). The list was swapped out at 0x1085E3 before the message, so nothing else writes it while
 *   it is walked. Then the inline spin 0x1E95A9..0x1E961A: rounds of 0x528210 (ret 0x1E95F8) while dev[0x3D8] != 0.
 * - 0x10BB30(r) spins on the device chain (rounds 0x528210, ret 0x10BB9A); present 0x6C5940: 0x6C0590 from 0x6C5F3E,
 *   0x6BEED0 from 0x6C6018, Present1 at 0x6C60A5 (vtable +0xB0), then 0x6F0360 from 0x6C6178.
 * - Each target's prologue relocates whole (MinHook; 0x6097A0's and 0x213540's jcc rel32 included), no caller reads a
 *   volatile register or xmm0 after the call, and only 0x9548C0 takes a float (xmm3; r9 is first written at 0x954BFE).
 *
 * The render thread's cost is QPC reads and plain stores to its own frame record, a lock-free ring to the game thread,
 * and message 8's walk of at most CO_RF_WALK items; no lock, allocation or log. Everything else is the game thread's. */
#if defined(CO_RESEARCH) || defined(CO_TEST)
#include "../co_rframe.h"

#define XRF_RING 1024u
#define XRF_STACK 0x2000
#define XRF_STACK_FRAMES 16
#define XRF_STALL_POLL_MS 5

enum { XH_JOBWAIT, XH_LODWAIT, XH_TREES, XH_JOIN, XH_DISPA, XH_DISPB, XH_APPLY, XH_CHAIN, XH_GPUWAIT, XH_DRAIN, XH_AFTER, XH_CELLS, XH_MESSAGE, XH_COUNT };

/* Each hook's target and prologue, and the call sites (or code) its reading rests on. */
static const struct { uint32_t rva; const char* name; const char* pin; uint32_t site; const char* sitePin; uint32_t site2; const char* site2Pin; } kXrfHooks[XH_COUNT] = {
    {0x627160u, "job wait 0x627160", "48 89 5C 24 08 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8B EC",
     0x1EC4AAu, "E8 B1 AC 43 00 48 8D 5B 04 49 83 EF 01 75 D7", 0x1E2FA1u, "E8 BA 41 44 00"},
    {0x6097A0u, "LOD wait 0x6097A0", "83 FA FF 0F 84 54 01 00 00 56 57 41 57 48 83 EC 20 48 89 5C 24 40 48 8B", 0x1E3E64u, "E8 37 59 42 00", 0, NULL},
    {0x213540u, "SpeedTree view wait 0x213540", "83 FA FF 0F 84 66 01 00 00 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 30",
     0x1E3E21u, "44 89 7C 24 20 48 8B 89 B0 01 00 00 E8 0E F7 02 00", 0x213607u, "8B 7C 24 60"},
    {0x252460u, "generator join 0x252460", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20", 0x1EDA81u, "E8 DA 49 06 00", 0, NULL},
    {0x6CC130u, "dispatch 0x6CC130", "44 88 4C 24 20 44 89 44 24 18 89 54 24 10 48 89 4C 24 08 55 53 56 57 41",
     0x6CED3Du, "44 0F B6 CB 41 8B D7 48 8B CF E8 E4 D3 FF FF", 0x6CECD5u, "44 8B BF C8 05 00 00"},
    {0x6CA5F0u, "dispatch 0x6CA5F0", "44 89 44 24 18 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56",
     0x6CED4Eu, "44 0F B6 CB 41 8B D7 48 8B CF E8 93 B8 FF FF", 0x6CECD5u, "44 8B BF C8 05 00 00"},
    {0x1E84A0u, "message 8 apply 0x1E84A0", "48 8B C4 4C 89 40 18 48 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48",
     0x1E8510u, "48 8B 42 10 4C 8B 30 4D 85 F6 0F 84 3D 0F 00 00 4C 89 72 10 41 0F B7 7E 0E 49 03 FE 8B 07 FF C8", 0x1E959Bu, "E8 00 EF FF FF"},
    {0x10BB30u, "device-chain spin 0x10BB30", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B B9 68", 0x10BB95u, "E8 76 C6 41 00 84 C0", 0, NULL},
    {0x6C0590u, "GPU wait 0x6C0590", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57", 0x6C5F3Eu, "E8 4D A6 FF FF", 0, NULL},
    {0x6BEED0u, "submit drain 0x6BEED0", "40 53 48 83 EC 70 48 8B D9 33 D2 48 8B 49 28 FF 15 13 78 D3 00 33 C0 4C",
     0x6C6018u, "E8 B3 8E FF FF", 0x6C60A5u, "FF 90 B0 00 00 00"},
    {0x6F0360u, "after Present1 0x6F0360", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83",
     0x6C6178u, "E8 E3 A1 02 00", 0x6C60A5u, "FF 90 B0 00 00 00"},
    {0x9548C0u, "SpeedTree cells 0x9548C0", "48 8B C4 55 53 57 48 8D 68 A1 48 81 EC D0 00 00 00", 0x212560u, "E8 5B 23 74 00",
     0x954CF2u, "4C 89 B3 90 00 00 00"},
    {0x113010u, "render messages 0x113010", "48 89 5C 24 08 55 56 57 41 56 41 57 48 8B EC 48 83 EC 60 0F B7 7A 0C 4D", 0x113EEEu, "E8 1D F1 FF FF", 0, NULL},
};

typedef uint64_t (*Xrf4Fn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*Xrf5Fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*XrfCellFn)(uint64_t, uint64_t, uint64_t, float);

static struct
{
    uintptr_t exe; size_t exeSize; int64_t freq; double clockNs;
    void* orig[XH_COUNT];
    char in[600], refused[600];
    const char* off;                 /* the whole instrument refused: why */
    volatile DWORD renderTid; const NT_TIB* renderTib;
    DWORD gameTid; int started; uint32_t timed, noted;
    /* the render thread's own */
    int depth; CoRfFrame f; int64_t applyEnd, spinLast, drainEnd; int spinOpen; uint32_t lostW;
    volatile LONG64 frameAt; volatile LONG frameSeq;
    CoRfFrame ring[XRF_RING]; volatile uint32_t ringW, ringR;
    /* any thread */
    volatile LONG64 views, cells; volatile LONG cellsMost;
    /* the game thread's */
    uint32_t lostR; ULONGLONG poolsAt; int pools; uint32_t shown[5], parked[5], shownMost[5], parkedMost[5];
    /* the stall watcher's samples, handed to the game thread */
    volatile LONG stallTaken, stallSeq; int stallOn; DWORD stallErr;
    struct { volatile LONG ready; uint64_t rip; double ms; uint32_t len; uint8_t stack[XRF_STACK]; } stall[CO_RF_STALL_SAMPLES];
    int asked;
#ifdef CO_TEST
    uint64_t testRw;                 /* the main world's render world for the pools, instead of the capacity stub's */
    int64_t testNow;                 /* the clock, when set */
#endif
} g_xrf;

static int64_t XrfNow(void)
{
#ifdef CO_TEST
    if (g_xrf.testNow != 0) return g_xrf.testNow;
#endif
    LARGE_INTEGER n; QueryPerformanceCounter(&n); return n.QuadPart;
}
static int XrfMine(void) { return g_xrfOn && g_xrf.renderTid != 0 && GetCurrentThreadId() == g_xrf.renderTid; }

/* The part a timed hook's call counts under, by its return address: -1 none, -2 an address outside the exe (another
 * mod's hook runs first, so the game's return address is not seen). */
static int XrfPartFor(int hook, uintptr_t ret)
{
    if (hook == XH_APPLY) return CO_RF_APPLY;
    if (hook == XH_CHAIN) return CO_RF_CHAIN;
    if (ret < g_xrf.exe || ret - g_xrf.exe >= g_xrf.exeSize) return -2;
    const uintptr_t rva = ret - g_xrf.exe;
    switch (hook)
    {
    case XH_JOBWAIT: return rva == 0x1EC4AFu ? CO_RF_CULL : rva == 0x1E2FA6u ? CO_RF_LODA : -1;
    case XH_LODWAIT: return rva == 0x1E3E69u ? CO_RF_LODB : -1;
    case XH_TREES: return rva == 0x1E3E32u ? CO_RF_TREES : -1;
    case XH_JOIN: return rva == 0x1EDA86u ? CO_RF_JOIN : -1;
    case XH_DISPA: return rva == 0x6CED4Cu ? CO_RF_DISPATCH : -1;
    case XH_DISPB: return rva == 0x6CED5Du ? CO_RF_DISPATCH : -1;
    case XH_GPUWAIT: return rva == 0x6C5F43u ? CO_RF_GPUWAIT : -1;
    case XH_DRAIN: return rva == 0x6C601Du ? CO_RF_DRAIN : -1;
    default: return -1;
    }
}
static void XrfAdd(int part, int64_t t0, int64_t t1) { g_xrf.f.t[part] += t1 - t0; g_xrf.f.clocks += 2; }

/* Message 8's inline spin: from 0x1E84A0's return at 0x1E95A0 to the end of its last round, closed before the next
 * apply, at each message's end and at the frame's end. */
static void XrfSpinClose(void)
{
    if (!g_xrf.spinOpen) return;
    g_xrf.spinOpen = 0;
    if (g_xrf.spinLast > g_xrf.applyEnd) g_xrf.f.t[CO_RF_SPIN8] += g_xrf.spinLast - g_xrf.applyEnd;
}
static void XrfApplied(uintptr_t ret, int64_t t1)
{
    if (ret - g_xrf.exe != 0x1E95A0u) return;
    g_xrf.applyEnd = t1; g_xrf.spinLast = 0; g_xrf.spinOpen = 1;
}
static void XrfRoundAt(uintptr_t ret, int64_t now)
{
    if (ret - g_xrf.exe != 0x1E95F8u || !g_xrf.spinOpen) return;
    g_xrf.spinLast = now; g_xrf.f.rounds8++; g_xrf.f.clocks++;
}
/* From LwRhNext, after 0x528210 returns: a round of message 8's spin ends now. */
static void XrfRound(uintptr_t ret)
{
    if (XrfMine() && g_xrf.spinOpen) XrfRoundAt(ret, XrfNow());
}

/* The object messages 0x1E84A0 is about to apply, by type: the first CO_RF_WALK of them. */
static void XrfWalk(uint64_t stream)
{
    uintptr_t p = *(volatile uintptr_t*)(uintptr_t)(stream + 0x10);
    uint32_t n = 0;
    while (p != 0 && n < CO_RF_WALK)
    {
        const uintptr_t node = *(volatile uintptr_t*)p;
        if (node == 0) break;
        const uint32_t type = *(volatile uint32_t*)(node + *(volatile uint16_t*)(node + 0xE));
        g_xrf.f.types[type < CO_RF_TYPES ? type : 0]++;
        p = node; n++;
    }
    g_xrf.f.walked = (uint16_t)(g_xrf.f.walked + n); g_xrf.f.applies++;
    if (n == CO_RF_WALK && p != 0 && *(volatile uintptr_t*)p != 0) g_xrf.f.applyCut++;
}

/* The render thread's record into the ring; a full ring drops it (counted). */
static void XrfPush(const CoRfFrame* f)
{
    const uint32_t w = g_xrf.ringW;
    if (w - __atomic_load_n(&g_xrf.ringR, __ATOMIC_ACQUIRE) >= XRF_RING) { g_xrf.lostW++; return; }
    memcpy(&g_xrf.ring[w % XRF_RING], f, sizeof *f);
    __atomic_store_n(&g_xrf.ringW, w + 1, __ATOMIC_RELEASE);
}

/* At message 0x0B's entry: the frame since the last one ends. [[r+0x1168]+0x1480] is the device's last GPU frame time
 * in seconds (0x6C5940's timer pair; which frame it belongs to is inferred, not proven). */
static void XrfFrameEnd(uint64_t r, int64_t now)
{
    XrfSpinClose();
    if (g_xrf.frameAt != 0)
    {
        uint64_t dev = 0; float gpu = -1.0f;
        if (r == 0 || SafeRead((uintptr_t)r + 0x1168, &dev, 8) || dev == 0 || SafeRead((uintptr_t)dev + 0x1480, &gpu, 4) || !(gpu >= 0.0f && gpu < 10.0f)) gpu = -1.0f;
        g_xrf.f.period = now - g_xrf.frameAt;
        g_xrf.f.gpuMs = gpu >= 0.0f ? gpu * 1000.0f : -1.0f;
        XrfPush(&g_xrf.f);
    }
    memset(&g_xrf.f, 0, sizeof g_xrf.f);
    InterlockedExchange64(&g_xrf.frameAt, now);
    InterlockedIncrement(&g_xrf.frameSeq);
}
static int XrfSlot(uint16_t type)
{
    switch (type)
    {
    case 0x07: return CO_RF_M07; case 0x08: return CO_RF_M08; case 0x09: return CO_RF_M09; case 0x0A: return CO_RF_M0A;
    case 0x0B: return CO_RF_M0B; case 0x0E: return CO_RF_M0E; case 0x10: return CO_RF_M10; default: return CO_RF_MOTHER;
    }
}
static void XrfMessageDone(uint16_t type, int64_t a, int64_t b)
{
    XrfSpinClose();
    g_xrf.f.t[XrfSlot(type)] += b - a;
    g_xrf.f.messages++; g_xrf.f.clocks += 2;
}

/* The dispatcher, from the stranded spans' hook or the instrument's own. Only the render loop calls it (0x113EEE);
 * a nested call is timed by the outer one. */
static uint64_t XrfMessage(uint64_t r, uint64_t message, uint64_t payload, XrfMessageFn orig)
{
    if (!g_xrfOn) return orig(r, message, payload);
    if (g_xrf.renderTid == 0) { g_xrf.renderTib = (const NT_TIB*)NtCurrentTeb(); g_xrf.renderTid = GetCurrentThreadId(); }
    else if (GetCurrentThreadId() != g_xrf.renderTid) return orig(r, message, payload);
    if (g_xrf.depth != 0) return orig(r, message, payload);
    const uint16_t type = *(volatile uint16_t*)(uintptr_t)(message + 0xC);
    int64_t a = XrfNow();
    if (type == 0x0B) { XrfFrameEnd(r, a); a = XrfNow(); g_xrf.f.clocks++; }
    g_xrf.depth++;
    const uint64_t out = orig(r, message, payload);
    g_xrf.depth--;
    XrfMessageDone(type, a, XrfNow());
    return out;
}
static uint64_t XrfOwnMessage(uint64_t r, uint64_t message, uint64_t payload) { return XrfMessage(r, message, payload, (XrfMessageFn)g_xrf.orig[XH_MESSAGE]); }

#define XRF_RET() ((uintptr_t)__builtin_return_address(0))
/* A timed call: on the render thread, from the call site its part names. */
#define XRF_TIMED(hook, call)                                                \
    do {                                                                     \
        if (!XrfMine()) return call;                                         \
        const int part = XrfPartFor(hook, XRF_RET());                        \
        if (part < 0) { if (part == -2) g_xrf.f.foreign++; return call; }    \
        const int64_t t0 = XrfNow(); const uint64_t out = call;              \
        XrfAdd(part, t0, XrfNow()); return out;                              \
    } while (0)

static uint64_t XrfJobWait(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { XRF_TIMED(XH_JOBWAIT, ((Xrf4Fn)g_xrf.orig[XH_JOBWAIT])(a, b, c, d)); }
static uint64_t XrfLodWait(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { XRF_TIMED(XH_LODWAIT, ((Xrf4Fn)g_xrf.orig[XH_LODWAIT])(a, b, c, d)); }
static uint64_t XrfTrees(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e) { XRF_TIMED(XH_TREES, ((Xrf5Fn)g_xrf.orig[XH_TREES])(a, b, c, d, e)); }
static uint64_t XrfJoin(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { XRF_TIMED(XH_JOIN, ((Xrf4Fn)g_xrf.orig[XH_JOIN])(a, b, c, d)); }
static uint64_t XrfChain(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { XRF_TIMED(XH_CHAIN, ((Xrf4Fn)g_xrf.orig[XH_CHAIN])(a, b, c, d)); }
static uint64_t XrfGpuWait(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { XRF_TIMED(XH_GPUWAIT, ((Xrf4Fn)g_xrf.orig[XH_GPUWAIT])(a, b, c, d)); }

/* A dispatch's records (edx = [rdi+0x5C8], 0x6CECD5..0x6CED58). */
static void XrfRecords(uint32_t n)
{
    g_xrf.f.dispatches++; g_xrf.f.records += n;
    if (n > g_xrf.f.recordsMost) g_xrf.f.recordsMost = n;
}
static uint64_t XrfDispatch(int hook, uintptr_t ret, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    const Xrf4Fn orig = (Xrf4Fn)g_xrf.orig[hook];
    if (!XrfMine()) return orig(a, b, c, d);
    const int part = XrfPartFor(hook, ret);
    if (part < 0) { if (part == -2) g_xrf.f.foreign++; return orig(a, b, c, d); }
    XrfRecords((uint32_t)b);
    const int64_t t0 = XrfNow(); const uint64_t out = orig(a, b, c, d);
    XrfAdd(part, t0, XrfNow());
    return out;
}
static uint64_t XrfDispatchA(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { return XrfDispatch(XH_DISPA, XRF_RET(), a, b, c, d); }
static uint64_t XrfDispatchB(uint64_t a, uint64_t b, uint64_t c, uint64_t d) { return XrfDispatch(XH_DISPB, XRF_RET(), a, b, c, d); }

static uint64_t XrfApply(uint64_t rw, uint64_t stream, uint64_t c, uint64_t d, uint64_t e)
{
    const Xrf5Fn orig = (Xrf5Fn)g_xrf.orig[XH_APPLY];
    if (!XrfMine()) return orig(rw, stream, c, d, e);
    const uintptr_t ret = XRF_RET();
    XrfSpinClose();
    XrfWalk(stream);
    const int64_t t0 = XrfNow(); const uint64_t out = orig(rw, stream, c, d, e); const int64_t t1 = XrfNow();
    XrfAdd(CO_RF_APPLY, t0, t1);
    XrfApplied(ret, t1);
    return out;
}

/* Present: the drain's end opens Present1, the call at 0x6C6178 closes it. */
static void XrfDrained(int64_t t1) { g_xrf.drainEnd = t1; }
static void XrfAfterAt(uintptr_t ret, int64_t now)
{
    if (ret - g_xrf.exe != 0x6C617Du || g_xrf.drainEnd == 0) return;
    g_xrf.f.t[CO_RF_PRESENT1] += now - g_xrf.drainEnd; g_xrf.f.clocks++;
    g_xrf.drainEnd = 0;
}
static uint64_t XrfDrain(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    const Xrf4Fn orig = (Xrf4Fn)g_xrf.orig[XH_DRAIN];
    if (!XrfMine()) return orig(a, b, c, d);
    const int part = XrfPartFor(XH_DRAIN, XRF_RET());
    if (part < 0) { if (part == -2) g_xrf.f.foreign++; return orig(a, b, c, d); }
    const int64_t t0 = XrfNow(); const uint64_t out = orig(a, b, c, d); const int64_t t1 = XrfNow();
    XrfAdd(part, t0, t1);
    XrfDrained(t1);
    return out;
}
static uint64_t XrfAfter(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    if (XrfMine() && g_xrf.drainEnd != 0) XrfAfterAt(XRF_RET(), XrfNow());
    return ((Xrf4Fn)g_xrf.orig[XH_AFTER])(a, b, c, d);
}

/* A SpeedTree view's cells (q[owner+0x90], stored at 0x954CF2), on whichever thread runs the view task. */
static void XrfCellsAt(uintptr_t ret, uint64_t owner)
{
    if (!g_xrfOn || ret - g_xrf.exe != 0x212565u) return;
    const uint64_t n = *(volatile uint64_t*)(uintptr_t)(owner + 0x90);
    InterlockedIncrement64(&g_xrf.views); InterlockedAdd64(&g_xrf.cells, (LONG64)n);
    const LONG v = n > 0x7FFFFFFFull ? 0x7FFFFFFF : (LONG)n;
    for (LONG m = g_xrf.cellsMost; v > m; m = g_xrf.cellsMost)
        if (InterlockedCompareExchange(&g_xrf.cellsMost, v, m) == m) break;
}
static uint64_t XrfCells(uint64_t owner, uint64_t view, uint64_t a3, float a4)
{
    const uint64_t out = ((XrfCellFn)g_xrf.orig[XH_CELLS])(owner, view, a3, a4);
    XrfCellsAt(XRF_RET(), owner);
    return out;
}

/* The frame loop's fence wait, from the op 0x41 setup's hook on 0x106030 (its 0x8A7C9 return): the game thread's only. */
static void XrfFence(int64_t in, int64_t out)
{
    if (g_xrfOn && g_xrf.started && GetCurrentThreadId() == g_xrf.gameTid) co_rframe_fence(out - in);
}

static void* const kXrfDetours[XH_COUNT] = {
    (void*)XrfJobWait, (void*)XrfLodWait, (void*)XrfTrees, (void*)XrfJoin, (void*)XrfDispatchA, (void*)XrfDispatchB, (void*)XrfApply,
    (void*)XrfChain, (void*)XrfGpuWait, (void*)XrfDrain, (void*)XrfAfter, (void*)XrfCells, (void*)XrfOwnMessage};
/* The parts each hook times (bits of CO_RF_*). */
static uint32_t XrfPartsOf(int hook)
{
    switch (hook)
    {
    case XH_JOBWAIT: return 1u << CO_RF_CULL | 1u << CO_RF_LODA;
    case XH_LODWAIT: return 1u << CO_RF_LODB;
    case XH_TREES: return 1u << CO_RF_TREES;
    case XH_JOIN: return 1u << CO_RF_JOIN;
    case XH_DISPA: case XH_DISPB: return 1u << CO_RF_DISPATCH;
    case XH_APPLY: return 1u << CO_RF_APPLY;
    case XH_CHAIN: return 1u << CO_RF_CHAIN;
    case XH_GPUWAIT: return 1u << CO_RF_GPUWAIT;
    case XH_DRAIN: return 1u << CO_RF_DRAIN;
    default: return 0;
    }
}
/* What is timed now: the hooks in, and the parts that rest on another's hook (messages, message 8's rounds). */
static uint32_t XrfTimed(void)
{
    uint32_t t = 0;
    for (int h = 0; h < XH_COUNT; h++) if (g_xrf.orig[h] != NULL) t |= XrfPartsOf(h);
    if (g_xrf.orig[XH_DISPA] == NULL || g_xrf.orig[XH_DISPB] == NULL) t &= ~(1u << CO_RF_DISPATCH);
    if (g_xrf.orig[XH_DRAIN] != NULL && g_xrf.orig[XH_AFTER] != NULL) t |= 1u << CO_RF_PRESENT1;
    if (g_xrf.orig[XH_APPLY] != NULL && g_lwRh.orig != NULL) t |= 1u << CO_RF_SPIN8;
    if (g_xrfViaSr || g_xrf.orig[XH_MESSAGE] != NULL) t |= (1u << (CO_RF_MOTHER + 1)) - 1u;
    return t;
}

static void XrfStallStart(void);
static void RenderFrameInstall(void)
{
    char t[4] = "";
    if (g_xrf.asked++) return;
    g_xrf.exe = ApiModuleBase("helldivers2.exe", NULL);
    g_xrf.exeSize = MoveStallImage(g_xrf.exe);
#ifdef CO_TEST
    if (g_xrf.exeSize == 0) g_xrf.exeSize = 0x2800000u; /* the fake host's image has no headers */
#endif
    if (GetEnvironmentVariableA("HELLFORGE_XL_RFRAME", t, sizeof t) > 0 && t[0] == '0') { g_xrf.off = "off (HELLFORGE_XL_RFRAME=0)"; return; }
    if (GetEnvironmentVariableA("HELLFORGE_LW_FREC", t, sizeof t) > 0 && t[0] == '1') { g_xrf.off = "off: the frame recorder (HELLFORGE_LW_FREC=1) hooks the same code"; return; }
    if (g_xrf.exe == 0 || g_xrf.exeSize == 0) { g_xrf.off = "off: no helldivers2.exe"; return; }
    LARGE_INTEGER hz; QueryPerformanceFrequency(&hz); g_xrf.freq = hz.QuadPart;
    const int64_t c0 = XrfNow();
    for (int i = 0; i < 2000; i++) (void)XrfNow();
    g_xrf.clockNs = (double)(XrfNow() - c0) * 1e9 / (double)g_xrf.freq / 2000.0;
    InterlockedExchange(&g_xrfViaSr, g_sr.render != NULL);
    size_t in = 0, out = 0;
    for (int h = 0; h < XH_COUNT; h++)
    {
        if (h == XH_MESSAGE && g_xrfViaSr) continue; /* the stranded spans' hook calls in */
        const uintptr_t x = g_xrf.exe;
        const char* why = NULL;
        if (ApiCodeMatches(x + kXrfHooks[h].rva, kXrfHooks[h].pin) != 1) why = "prologue";
        else if (ApiCodeMatches(x + kXrfHooks[h].site, kXrfHooks[h].sitePin) != 1 ||
                 (kXrfHooks[h].site2Pin != NULL && ApiCodeMatches(x + kXrfHooks[h].site2, kXrfHooks[h].site2Pin) != 1)) why = "call site";
        else if (LoaderHook(kXrfHooks[h].name, x + kXrfHooks[h].rva, kXrfDetours[h], &g_xrf.orig[h]) || g_xrf.orig[h] == NULL) why = "hook";
        if (why != NULL)
        {
            g_xrf.orig[h] = NULL;
            out += (size_t)snprintf(g_xrf.refused + out, sizeof g_xrf.refused - out, "%s%s (%s)", out ? ", " : "", kXrfHooks[h].name, why);
        }
        else in += (size_t)snprintf(g_xrf.in + in, sizeof g_xrf.in - in, "%s%s", in ? ", " : "", kXrfHooks[h].name);
        if (in >= sizeof g_xrf.in) in = sizeof g_xrf.in - 1;
        if (out >= sizeof g_xrf.refused) out = sizeof g_xrf.refused - 1;
    }
    InterlockedExchange(&g_xrfOn, 1);
    XrfStallStart();
}

/* ---- the game thread ---- */

/* The main world's culling sets 0..4: CSO_k = [rw+0x268+8k], shown = [CSO+0x198] (the SoA at CSO+0x50, its count +0x148:
 * 0x5679B0 / 0x5677F0 from 0x58E6A0), parked = [CSO+0x1A0] (0x58E7F4..0x58E833). */
static int XrfPools(uint32_t* shown, uint32_t* parked)
{
    uint64_t rw = 0;
#ifdef CO_TEST
    rw = g_xrf.testRw;
#endif
    if (rw == 0 && (g_renderCap.on != 1 || g_renderCap.scenePtr == NULL || (rw = *g_renderCap.scenePtr) == 0)) return 0;
    for (int k = 0; k < 5; k++)
    {
        uint64_t cso = 0;
        if (SafeRead((uintptr_t)rw + 0x268 + 8u * (unsigned)k, &cso, 8) || cso == 0 || SafeRead((uintptr_t)cso + 0x198, &shown[k], 4) ||
            SafeRead((uintptr_t)cso + 0x1A0, &parked[k], 4)) return 0;
    }
    return 1;
}
static void XrfPoolsSample(void)
{
    uint32_t s[5], p[5];
    if (!XrfPools(s, p)) return;
    g_xrf.pools = 1;
    for (int k = 0; k < 5; k++)
    {
        g_xrf.shown[k] = s[k]; g_xrf.parked[k] = p[k];
        if (s[k] > g_xrf.shownMost[k]) g_xrf.shownMost[k] = s[k];
        if (p[k] > g_xrf.parkedMost[k]) g_xrf.parkedMost[k] = p[k];
    }
}

/* The far window stamps (g_streamOut2 > 0: entered past WINDOW_IN) by their nearest player, in the streamer's world
 * x, y (the eviction's own measure); -1 when the streamer holds its lock. */
static int XrfStamps(uint32_t band[5], uint32_t* farCount, uint32_t* live)
{
    if (!TryAcquireSRWLockShared(&g_streamLock)) return -1;
    int ok = 0;
    if (g_win.on && g_stream.playerCount > 0)
    {
        ok = 1;
        for (uint32_t s = g_win.base; s < g_win.high && s < kStreamMaxLocations; s++) /* the arrays' bound; high <= DIF_LOCATIONS */
        {
            if (!(g_streamOut2[s] > 0.0f)) continue;
            float best = 3.4e38f;
            for (int p = 0; p < g_stream.playerCount; p++)
            {
                const float dx = g_streamAt[s][0] - g_streamPlayerAt[p][0], dy = g_streamAt[s][1] - g_streamPlayerAt[p][1];
                if (dx * dx + dy * dy < best) best = dx * dx + dy * dy;
            }
            const float d = sqrtf(best);
            band[d < 550.0f ? 0 : d < 800.0f ? 1 : d < 1100.0f ? 2 : d < 1500.0f ? 3 : 4]++;
            (*farCount)++;
            if (g_streamLive[s]) (*live)++;
        }
    }
    ReleaseSRWLockShared(&g_streamLock);
    return ok;
}

static void XrfDrainRing(void)
{
    uint32_t r = g_xrf.ringR;
    const uint32_t w = __atomic_load_n(&g_xrf.ringW, __ATOMIC_ACQUIRE);
    for (; r != w; r++) co_rframe_frame(&g_xrf.ring[r % XRF_RING]);
    __atomic_store_n(&g_xrf.ringR, r, __ATOMIC_RELEASE);
}

static void XrfInstallLine(void)
{
    const uint32_t key = g_xrf.timed | (uint32_t)(g_matWait.frameAt != 0) << 31 | 1u << 30; /* never 0: the first tick notes */
    if (key == g_xrf.noted) return;
    g_xrf.noted = key;
    Note("render frame instrument (research): hooks in: %s; refused: %s; through other hooks: render messages %s, the frame fence (0x8A7C4) %s, "
        "message 8's spin rounds (0x528210 at 0x1E95F8) %s; clock read %.0f ns; stall samples %s",
        g_xrf.in[0] ? g_xrf.in : "none", g_xrf.refused[0] ? g_xrf.refused : "none",
        g_xrfViaSr ? "yes (the stranded spans' 0x113010 hook)" : g_xrf.orig[XH_MESSAGE] != NULL ? "yes (its own 0x113010 hook)" : "NO",
        g_matWait.frameAt != 0 ? "yes (op 0x41's 0x106030 hook)" : "not yet", g_lwRh.orig != NULL ? "yes (the render help hook)" : "NO",
        g_xrf.clockNs, g_xrf.stallOn ? "on (a render frame over 25 ms, once a frame, 3 a window)" : "off");
}

static void XrfStallNotes(void);
static void RenderFrameTick(void)
{
    static int offNoted;
    if (!g_xrfOn)
    {
        if (g_xrf.off != NULL && !offNoted++) Note("render frame instrument (research): %s", g_xrf.off);
        return;
    }
    const int64_t now = XrfNow();
    if (!g_xrf.started)
    {
        g_xrf.gameTid = GetCurrentThreadId();
        g_xrf.timed = XrfTimed();
        co_rframe_reset(g_xrf.freq, g_xrf.clockNs, g_xrf.timed, now);
        g_xrf.started = 1;
    }
    const uint32_t timed = XrfTimed();
    if (timed != g_xrf.timed) { g_xrf.timed = timed; co_rframe_timed(timed); }
    XrfInstallLine();
    XrfDrainRing();
    const ULONGLONG tick = GetTickCount64();
    if (tick - g_xrf.poolsAt >= 1000) { g_xrf.poolsAt = tick; XrfPoolsSample(); }
    if (co_rframe_due(now))
    {
        CoRfCounts c;
        memset(&c, 0, sizeof c);
        c.views = (uint64_t)InterlockedExchange64(&g_xrf.views, 0); c.cells = (uint64_t)InterlockedExchange64(&g_xrf.cells, 0);
        c.cellsMost = (uint32_t)InterlockedExchange(&g_xrf.cellsMost, 0);
        const uint32_t lost = g_xrf.lostW; c.lost = lost - g_xrf.lostR; g_xrf.lostR = lost;
        c.pools = g_xrf.pools;
        memcpy(c.shown, g_xrf.shown, sizeof c.shown); memcpy(c.parked, g_xrf.parked, sizeof c.parked);
        memcpy(c.shownMost, g_xrf.shownMost, sizeof c.shownMost); memcpy(c.parkedMost, g_xrf.parkedMost, sizeof c.parkedMost);
        c.stamps = XrfStamps(c.band, &c.farCount, &c.farLive);
        co_rframe_close(now, &c);
        g_xrf.pools = 0;
        memset(g_xrf.shownMost, 0, sizeof g_xrf.shownMost); memset(g_xrf.parkedMost, 0, sizeof g_xrf.parkedMost);
        InterlockedExchange(&g_xrf.stallTaken, 0);
    }
    XrfStallNotes();
}

/* ---- the stall sampler ---- */

/* The game thread's reading of a sample: where the render thread was, its call sites, the first known wait. */
static void XrfStallNote(int k)
{
    const uintptr_t exe = g_xrf.exe, game = g_world.game;
    const size_t exeSize = g_xrf.exeSize, gameSize = MoveStallImage(game);
    const uint64_t rip = g_xrf.stall[k].rip;
    char where[64], trail[400], waitText[64];
    uint64_t off = 0;
    size_t n = 0;
    int frames = 0;
    MoveStallWhere((uintptr_t)rip, exe, exeSize, game, gameSize, where, sizeof where, &off);
    const uintptr_t fn = rip >= exe && rip - exe < exeSize ? MoveStallFunction((uintptr_t)rip) : 0;
    const char* wait = rip >= exe && rip - exe < exeSize ? co_rframe_wait_name((uint32_t)(rip - exe)) : NULL;
    if (wait == NULL) wait = MoveStallWait((uintptr_t)rip, exe, exeSize, game, gameSize, waitText, sizeof waitText);
    trail[0] = 0;
    for (uint32_t i = 0; i + 8 <= g_xrf.stall[k].len && frames < XRF_STACK_FRAMES; i += 8)
    {
        uint64_t v;
        memcpy(&v, g_xrf.stall[k].stack + i, 8);
        if (!(v >= exe && v - exe < exeSize) || !MoveStallCallBefore((uintptr_t)v)) continue;
        const char* name = co_rframe_wait_name((uint32_t)(v - exe));
        if (wait == NULL) wait = name != NULL ? name : MoveStallWait((uintptr_t)v, exe, exeSize, game, gameSize, waitText, sizeof waitText);
        n += (size_t)snprintf(trail + n, sizeof trail - n, "%sexe+0x%llX", frames ? ", " : "", (unsigned long long)(v - exe));
        if (n >= sizeof trail) n = sizeof trail - 1;
        frames++;
    }
    char function[40] = "not found";
    if (fn != 0) snprintf(function, sizeof function, "exe+0x%llX", (unsigned long long)(fn - exe));
    Note("render stall sample: the render thread %.1f ms into its frame at %s+0x%llX (function %s); wait: %s; stack (%u bytes): %s",
        g_xrf.stall[k].ms, where, (unsigned long long)off, function, wait != NULL ? wait : "none named", g_xrf.stall[k].len, frames ? trail : "none found");
}
static void XrfStallNotes(void)
{
    for (int k = 0; k < CO_RF_STALL_SAMPLES; k++)
        if (__atomic_load_n(&g_xrf.stall[k].ready, __ATOMIC_ACQUIRE))
        {
            XrfStallNote(k);
            __atomic_store_n(&g_xrf.stall[k].ready, 0, __ATOMIC_RELEASE);
        }
}

/* 1: the frame under way since `at` (QPC) is past CO_RF_STALL_MS and not yet sampled, and the window has room. */
static int XrfStallDue(int64_t at, int64_t now, LONG seq)
{
    return at != 0 && seq != g_xrf.stallSeq && g_xrf.stallTaken < CO_RF_STALL_SAMPLES && g_xrf.freq > 0 &&
        (double)(now - at) * 1000.0 / (double)g_xrf.freq > CO_RF_STALL_MS;
}

#if defined(CO_RESEARCH) && !defined(CO_TEST) /* a test thread is never suspended */
/* The watcher. Between SuspendThread and ResumeThread it only reads the context and copies the stack (no lock the
 * render thread may hold: no allocation, log or lookup); the game thread reads the copy. */
static DWORD WINAPI XrfStallWatch(void* unused)
{
    (void)unused;
    HANDLE thread = NULL;
    for (;;)
    {
        Sleep(XRF_STALL_POLL_MS);
        if (g_xrf.renderTid == 0 || g_xrf.renderTib == NULL) continue;
        if (thread == NULL && (thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_xrf.renderTid)) == NULL)
        { g_xrf.stallErr = GetLastError(); g_xrf.stallOn = 0; return 0; }
        const LONG seq = g_xrf.frameSeq;
        MemoryBarrier();
        const int64_t at = g_xrf.frameAt, now = XrfNow();
        if (!XrfStallDue(at, now, seq)) continue;
        int k = 0;
        while (k < CO_RF_STALL_SAMPLES && g_xrf.stall[k].ready) k++;
        if (k == CO_RF_STALL_SAMPLES) continue;
        g_xrf.stallSeq = seq;
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        size_t len = 0;
        if (SuspendThread(thread) == (DWORD)-1) continue;
        if (GetThreadContext(thread, &c) && g_xrf.frameSeq == seq)
        {
            const uintptr_t lo = (uintptr_t)g_xrf.renderTib->StackLimit, hi = (uintptr_t)g_xrf.renderTib->StackBase;
            if (c.Rsp >= lo && c.Rsp < hi)
            {
                len = hi - c.Rsp < XRF_STACK ? (size_t)(hi - c.Rsp) : XRF_STACK;
                memcpy(g_xrf.stall[k].stack, (const void*)(uintptr_t)c.Rsp, len);
            }
        }
        const int64_t taken = XrfNow();
        ResumeThread(thread);
        if (len == 0) continue;
        g_xrf.stall[k].rip = c.Rip; g_xrf.stall[k].len = (uint32_t)len;
        g_xrf.stall[k].ms = (double)(taken - at) * 1000.0 / (double)g_xrf.freq;
        InterlockedIncrement(&g_xrf.stallTaken);
        __atomic_store_n(&g_xrf.stall[k].ready, 1, __ATOMIC_RELEASE);
    }
}
static void XrfStallStart(void)
{
    char v[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_XL_RFRAME_STALL", v, sizeof v) > 0 && v[0] == '0') return;
    HANDLE t = CreateThread(NULL, 0, XrfStallWatch, NULL, 0, NULL);
    g_xrf.stallOn = t != NULL;
    if (t != NULL) CloseHandle(t);
}
#else
static void XrfStallStart(void) {}
#endif

#ifdef CO_TEST
/* The instrument on a test clock (ticks of `freq` a second), this thread as the render and the game thread. */
void CoXlTestXrfReset(uintptr_t exe, int64_t freq, int64_t now)
{
    memset(&g_xrf, 0, sizeof g_xrf);
    g_xrf.exe = exe; g_xrf.exeSize = 0x2800000u; g_xrf.freq = freq; g_xrf.clockNs = 50.0; g_xrf.testNow = now; g_xrf.asked = 1;
    g_xrf.renderTid = GetCurrentThreadId(); g_xrf.renderTib = (const NT_TIB*)NtCurrentTeb();
    InterlockedExchange(&g_xrfOn, 1); InterlockedExchange(&g_xrfViaSr, 1);
    for (int h = 0; h < XH_COUNT; h++) if (h != XH_MESSAGE) g_xrf.orig[h] = (void*)CoXlTestXrfReset; /* every hook in */
}
void CoXlTestXrfClock(int64_t now) { g_xrf.testNow = now; }
void CoXlTestXrfHookOut(int hook) { g_xrf.orig[hook] = NULL; }
void CoXlTestXrfThread(DWORD render) { g_xrf.renderTid = render; }
int CoXlTestXrfPartFor(int hook, uintptr_t ret) { return XrfPartFor(hook, ret); }
void CoXlTestXrfAdd(int part, int64_t t0, int64_t t1) { XrfAdd(part, t0, t1); }
void CoXlTestXrfFrameEnd(uint64_t r) { XrfFrameEnd(r, XrfNow()); }
void CoXlTestXrfMessageDone(uint16_t type, int64_t a, int64_t b) { XrfMessageDone(type, a, b); }
uint64_t CoXlTestXrfMessage(uint64_t r, uint64_t message, uint64_t payload, uint64_t (*orig)(uint64_t, uint64_t, uint64_t))
{ return XrfMessage(r, message, payload, (XrfMessageFn)orig); }
void CoXlTestXrfWalk(uint64_t stream) { XrfWalk(stream); }
void CoXlTestXrfApplied(uintptr_t ret, int64_t t1) { XrfApplied(ret, t1); }
void CoXlTestXrfRoundAt(uintptr_t ret, int64_t now) { XrfRoundAt(ret, now); }
void CoXlTestXrfSpinClose(void) { XrfSpinClose(); }
void CoXlTestXrfRecords(uint32_t n) { XrfRecords(n); }
void CoXlTestXrfDrained(int64_t t1) { XrfDrained(t1); }
void CoXlTestXrfAfterAt(uintptr_t ret, int64_t now) { XrfAfterAt(ret, now); }
void CoXlTestXrfCellsAt(uintptr_t ret, uint64_t owner) { XrfCellsAt(ret, owner); }
void CoXlTestXrfFence(int64_t in, int64_t out) { XrfFence(in, out); }
const CoRfFrame* CoXlTestXrfCurrent(void) { return &g_xrf.f; }
uint32_t CoXlTestXrfRingFill(void) { return g_xrf.ringW - g_xrf.ringR; }
uint32_t CoXlTestXrfLost(void) { return g_xrf.lostW; }
void CoXlTestXrfTick(void) { RenderFrameTick(); }
void CoXlTestXrfPoolsRw(uint64_t rw) { g_xrf.testRw = rw; g_xrf.poolsAt = 0; }
uint32_t CoXlTestXrfTimed(void) { return XrfTimed(); }
int CoXlTestXrfStallDue(int64_t at, int64_t now, LONG seq) { return XrfStallDue(at, now, seq); }
/* A watcher's sample as it hands it over: noted at the next tick. */
void CoXlTestXrfStall(int k, uint64_t rip, double ms, const void* stack, uint32_t len)
{
    g_xrf.stall[k].rip = rip; g_xrf.stall[k].ms = ms; g_xrf.stall[k].len = len;
    memcpy(g_xrf.stall[k].stack, stack, len);
    __atomic_store_n(&g_xrf.stall[k].ready, 1, __ATOMIC_RELEASE);
}
/* The install on the fake exe: on (1) or not, and its hooks in and refused. */
int CoXlTestXrfInstall(const char** in, const char** refused, const char** off)
{
    memset(&g_xrf, 0, sizeof g_xrf);
    InterlockedExchange(&g_xrfOn, 0);
    RenderFrameInstall();
    *in = g_xrf.in; *refused = g_xrf.refused; *off = g_xrf.off;
    return g_xrfOn;
}
void* CoXlTestXrfOrig(int hook) { return g_xrf.orig[hook]; }
/* The streamer's window as the far bands read it: slots base.. with their x, y, removal radius squared and live flag. */
void CoXlTestXrfStampsSet(int on, uint32_t base, uint32_t n, const float (*at)[2], const float* out2, const uint8_t* live, int players, const float (*playerAt)[2])
{
    g_win.on = on; g_win.base = base; g_win.high = base + n; g_stream.playerCount = players;
    for (uint32_t i = 0; i < n; i++)
    { g_streamAt[base + i][0] = at[i][0]; g_streamAt[base + i][1] = at[i][1]; g_streamOut2[base + i] = out2[i]; g_streamLive[base + i] = live[i]; }
    for (int p = 0; p < players; p++) { g_streamPlayerAt[p][0] = playerAt[p][0]; g_streamPlayerAt[p][1] = playerAt[p][1]; }
}
#endif
#else
static void RenderFrameInstall(void) {}
static void RenderFrameTick(void) {}
#endif
