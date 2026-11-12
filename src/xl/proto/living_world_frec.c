/* Test instrument (HELLFORGE_LW_FREC=1): one record a frame in frames.bin beside modloader.log, so each slow frame
 * can be told apart: main thread work, its Reflex sleep and its frame fence, the render thread's work and its GPU
 * waits, and the engine's own GPU time. Hook points and their return sites: KB render-frame-waits. A frame on the
 * main thread runs from one frame fence (0x106030 called at 0x8A7C4) to the next. Records: python3 frec.py. */
#define LW_FR_BATCH 128 /* about 2 s a write at 60 fps, by the writer thread */
#define LW_FR_BATCHES 4
#define LW_FR_MAGIC 0x31524646u /* "FFR1" */
#define LW_FR_TYPES 0x2D
typedef struct
{
    uint32_t frame, phase; double t;
    float period, sleep, fence, flush, join, pass[8];
    float rPeriod, rBusy, rGpuWait, rDrain, rPresent, rNxMem, gpu;
    uint32_t rFrame; uint16_t units, corpses; uint32_t flushes;
    float pre, update, afterUpdate, renderPrep, kick, tail; /* main thread sections, -1 when a mark is missing */
    float engine, physics, anim, animWait;     /* main thread: 0x1B0B10, 0x79EB10, 0x1B0670, 0x556040 */
    float rWorld, rStartMsg, rPresentMsg, rOther; /* render thread by message type: 0x08, 0x0A, 0x09, the rest */
    uint32_t flags; float instrMain, instrRender;  /* the instruments' own work in this frame: LW_FRF_*, ms */
} LwFrRec;
/* Record flags: log lines written on the main / render thread, a batch handed to the writer, lite mode; bits 8..15
 * hold the "frec mark" byte; 0x20 the render phase timer's phases were on; 0x40 a harness test command ran (an outside capture's window); bit 16 a batch was dropped since the last record. */
enum { LW_FRF_LOGMAIN = 1, LW_FRF_LOGRENDER = 2, LW_FRF_HANDOFF = 4, LW_FRF_LITE = 8, LW_FRF_RMSG = 0x20, LW_FRF_CMD = 0x40, LW_FRF_DROP = 0x10000 };
typedef uintptr_t (*LwFr2Fn)(uintptr_t a, uintptr_t b);
typedef uintptr_t (*LwFr3Fn)(uintptr_t a, uintptr_t b, uintptr_t c);
typedef uintptr_t (*LwFr1Fn)(uintptr_t a);
typedef uintptr_t (*LwFrKickFn)(uintptr_t r, float dt);
typedef uintptr_t (*LwFrEngineFn)(uintptr_t w, float a, float b);
static struct
{
    int on; uintptr_t exe; double toMs; HANDLE file; volatile LONG phase; volatile DWORD mainTid;
    void *sleepOrig, *fenceOrig, *joinOrig, *msgOrig, *gpuOrig, *drainOrig, *afterOrig, *nxOrig, *markOrig, *updEndOrig, *kickOrig, *engineOrig, *physOrig, *animOrig, *animWaitOrig;
    LONGLONG engine, physics, anim, animWait, rType[4];
    LONGLONG mark0, updEnd, mark1, kickIn, kickOut, fenceIn;
    /* main thread, this frame */
    LONGLONG start, sleep, fence, flush, join, passAt[8]; uint32_t flushes, frame;
    /* render thread: the frame being built, then the last finished one (read by the main thread) */
    LONGLONG rStart, rBusy, rGpuWait, rDrain, rPresent, rNxMem, drainEnd; int depth;
    volatile LONG rSeq, rWrite; volatile float rLast[11]; /* rWrite: odd while the render thread writes rLast */
    /* the instruments' own time: main thread this frame, render thread in the frame being built and the last one */
    LONGLONG instrM, instrR; uint32_t flagsM, flagsR; volatile float rInstrLast; volatile LONG rFlagsLast; volatile DWORD renderTid;
    volatile LONG lite, liteAb, mark, flushReq, rmFlushReq, drops, dropNoted, rmsgPhases; ULONGLONG liteAt; double clockNs; void (*volatile more)(void); /* rmsg.bin */
    /* batches: the main thread fills one, the writer thread writes full ones in order */
    LwFrRec batch[LW_FR_BATCHES][LW_FR_BATCH]; volatile LONG full[LW_FR_BATCHES]; uint32_t len[LW_FR_BATCHES], cur, used, wcur; HANDLE wake;
    /* render thread, by message type 0..0x2C (the dispatcher's jump table at 0x113964): this frame, all frames,
     * the frames whose busy time is over 12 ms; noted every 30 s */
    LONGLONG fType[LW_FR_TYPES], allType[LW_FR_TYPES], slowType[LW_FR_TYPES]; uint32_t nType[LW_FR_TYPES], rFrames, rSlow; ULONGLONG typeNoted;
} g_lwFr;

static void LwScFrame(float period); static void LwInstallSteamCalls(void); /* living_world_steamcalls.c */
static LONGLONG LwFrNow(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static float LwFrMs(LONGLONG ticks) { return (float)((double)ticks * g_lwFr.toMs); }
static uintptr_t LwFrRet(void* ret) { return (uintptr_t)ret - g_lwFr.exe; }

/* Writer thread: the file writes stay off the game's threads. */
static DWORD WINAPI LwFrWriter(void* unused)
{
    for (;;)
    {
        WaitForSingleObject(g_lwFr.wake, 1000);
        while (g_lwFr.full[g_lwFr.wcur])
        {
            DWORD wrote; uint32_t b = g_lwFr.wcur;
            WriteFile(g_lwFr.file, g_lwFr.batch[b], g_lwFr.len[b] * (DWORD)sizeof(LwFrRec), &wrote, NULL);
            InterlockedExchange(&g_lwFr.full[b], 0); g_lwFr.wcur = (b + 1) % LW_FR_BATCHES;
        }
        if (g_lwFr.more) g_lwFr.more();
    }
    return 0;
}
/* Main thread: hands the filled part of this batch to the writer; when the writer is a whole ring behind it is dropped. */
static void LwFrFlush(void)
{
    if (!g_lwFr.used) return;
    uint32_t next = (g_lwFr.cur + 1) % LW_FR_BATCHES;
    if (g_lwFr.full[next]) { InterlockedIncrement(&g_lwFr.drops); g_lwFr.used = 0; return; }
    g_lwFr.len[g_lwFr.cur] = g_lwFr.used; InterlockedExchange(&g_lwFr.full[g_lwFr.cur], 1); SetEvent(g_lwFr.wake);
    g_lwFr.cur = next; g_lwFr.used = 0; g_lwFr.flagsM |= LW_FRF_HANDOFF;
}
/* A harness test command (hellforge_lw_test) on the main thread: its time and a flag; the Lua around it is not timed. */
static LONGLONG LwFrInstrM(void) { return g_lwFr.instrM; }
static void LwFrCmdTime(LONGLONG t0, LONGLONG before) /* before: instrM at t0, so its log lines are not booked twice */
{
    if (!g_lwFr.on || GetCurrentThreadId() != g_lwFr.mainTid) return;
    g_lwFr.instrM = before + (LwFrNow() - t0); g_lwFr.flagsM |= LW_FRF_CMD;
}
/* Every log line's time, by the thread that wrote it (WriteLog's timer). */
static void LwFrLogTime(LONGLONG ticks)
{
    DWORD tid = GetCurrentThreadId();
    if (tid == g_lwFr.mainTid) { g_lwFr.instrM += ticks; g_lwFr.flagsM |= LW_FRF_LOGMAIN; }
    else if (tid == g_lwFr.renderTid) { g_lwFr.instrR += ticks; g_lwFr.flagsR |= LW_FRF_LOGRENDER; }
}

static void LwFrPasses(LwFrRec* r)
{
    for (int i = 0; i < 8; i++)
    {
        LONGLONG t = g_lwPass[i].ticks, d = t >= g_lwFr.passAt[i] ? t - g_lwFr.passAt[i] : t; /* the 30 s note zeroes it */
        r->pass[i] = g_lwPass[i].orig ? LwFrMs(d) : -1.0f; g_lwFr.passAt[i] = t;
    }
}
static void LwFrSections(LwFrRec* r)
{
    LONGLONG m[7] = {g_lwFr.start, g_lwFr.mark0, g_lwFr.updEnd, g_lwFr.mark1, g_lwFr.kickIn, g_lwFr.kickOut, g_lwFr.fenceIn};
    float* sec = &r->pre; int all = 1;
    for (int i = 0; i < 7; i++) all &= m[i] != 0 && (i == 0 || m[i] >= m[i - 1]);
    for (int i = 0; i < 6; i++) sec[i] = all ? LwFrMs(m[i + 1] - m[i]) : -1.0f;
    if (all) sec[0] -= r->sleep; /* the Reflex sleep is at the top of the frame, before marker 0 */
    g_lwFr.mark0 = g_lwFr.updEnd = g_lwFr.mark1 = g_lwFr.kickIn = g_lwFr.kickOut = g_lwFr.fenceIn = 0;
}

/* "frec lite ab S": lite on and off every S s, switched at a frame end. */
static void LwFrLiteTurn(void)
{
    if (g_lwFr.liteAb && GetTickCount64() - g_lwFr.liteAt >= (ULONGLONG)g_lwFr.liteAb * 1000u)
    { g_lwFr.liteAt = GetTickCount64(); InterlockedExchange(&g_lwFr.lite, !g_lwFr.lite); }
}
/* The last finished render frame, whole: rWrite is odd while the render thread writes it. */
static void LwFrRenderSnap(LwFrRec* r)
{
    float* last = &r->rPeriod;
    for (int tries = 0; tries < 8; tries++)
    {
        LONG w0 = g_lwFr.rWrite; if (w0 & 1) continue;
        for (int i = 0; i < 7; i++) last[i] = g_lwFr.rLast[i];
        for (int i = 0; i < 4; i++) (&r->rWorld)[i] = g_lwFr.rLast[7 + i];
        r->instrRender = g_lwFr.rInstrLast; r->rFrame = (uint32_t)g_lwFr.rSeq;
        if (g_lwFr.rWrite == w0) return;
    }
}

/* Main thread, at the end of a frame's fence wait: the frame's record. */
static void LwFrEndFrame(LONGLONG now)
{
    LwFrRec* r = &g_lwFr.batch[g_lwFr.cur][g_lwFr.used];
    memset(r, 0, sizeof *r);
    r->frame = g_lwFr.frame++; r->phase = (uint32_t)g_lwFr.phase; r->t = (double)now * g_lwFr.toMs / 1000.0;
    r->period = g_lwFr.start ? LwFrMs(now - g_lwFr.start) : 0.0f;
    r->sleep = LwFrMs(g_lwFr.sleep); r->fence = LwFrMs(g_lwFr.fence); r->flush = LwFrMs(g_lwFr.flush); r->join = LwFrMs(g_lwFr.join);
    LwFrPasses(r);
    LwFrRenderSnap(r);
    r->engine = LwFrMs(g_lwFr.engine); r->physics = LwFrMs(g_lwFr.physics); r->anim = LwFrMs(g_lwFr.anim); r->animWait = LwFrMs(g_lwFr.animWait);
    g_lwFr.engine = g_lwFr.physics = g_lwFr.anim = g_lwFr.animWait = 0;
    r->units = (uint16_t)(g_lwWorld.onlineUnits < 0 ? 0 : g_lwWorld.onlineUnits > 65535 ? 65535 : g_lwWorld.onlineUnits);
    r->corpses = (uint16_t)g_lwCorpse.frozenNow; r->flushes = g_lwFr.flushes;
    LwFrSections(r);
    LwScFrame(r->period);
    LONG drops = g_lwFr.drops;
    r->flags = g_lwFr.flagsM | (uint32_t)g_lwFr.rFlagsLast | (g_lwFr.lite ? LW_FRF_LITE : 0) | (g_lwFr.rmsgPhases ? LW_FRF_RMSG : 0) | ((uint32_t)(g_lwFr.mark & 0xFF) << 8) |
        (drops != g_lwFr.dropNoted ? LW_FRF_DROP : 0);
    g_lwFr.dropNoted = drops;
    r->instrMain = LwFrMs(g_lwFr.instrM);
    g_lwFr.start = now; g_lwFr.sleep = g_lwFr.fence = g_lwFr.flush = g_lwFr.join = 0; g_lwFr.flushes = 0;
    g_lwFr.flagsM = 0; g_lwFr.instrM = 0;
    LwFrLiteTurn();
    if (++g_lwFr.used == LW_FR_BATCH || InterlockedExchange(&g_lwFr.flushReq, 0)) LwFrFlush();
    g_lwFr.instrM = LwFrNow() - now; /* this record's own cost lies in the next frame */
}

/* 0x106030(r, index): the frame fence when called from 0x8A7C4, a flush fence from anywhere else. */
static uintptr_t LwFrFence(uintptr_t r, uintptr_t index)
{
    uintptr_t ret = LwFrRet(__builtin_return_address(0)), out;
    LONGLONG a = LwFrNow(); out = ((LwFr2Fn)g_lwFr.fenceOrig)(r, index); LONGLONG b = LwFrNow();
    if (ret == 0x8A7C9u) { g_lwFr.fenceIn = a; g_lwFr.mainTid = GetCurrentThreadId(); g_lwFr.fence += b - a; LwFrEndFrame(b); }
    else if (GetCurrentThreadId() == g_lwFr.mainTid) { g_lwFr.flush += b - a; g_lwFr.flushes++; }
    return out;
}
/* 0xADE70(latency object, u32): the Reflex sleep, called at 0x89AF1 at the top of the frame. */
static uintptr_t LwFrSleep(uintptr_t obj, uintptr_t a)
{
    if (g_lwFr.lite) return ((LwFr2Fn)g_lwFr.sleepOrig)(obj, a);
    LONGLONG t = LwFrNow(); uintptr_t out = ((LwFr2Fn)g_lwFr.sleepOrig)(obj, a);
    if (LwFrRet(__builtin_return_address(0)) == 0x89AF4u) g_lwFr.sleep += LwFrNow() - t;
    return out;
}
/* Main thread section marks: latency markers 0 and 1 (0xADEB0 called at 0x89F6C and 0x8A184 around the simulation),
 * 0x91420 called at 0x89F94 right after the game update call at 0x89F81, and the frame kick 0x10A5C0 at 0x8A73F. */
static uintptr_t LwFrMark(uintptr_t obj, uintptr_t id, uintptr_t frame)
{
    if (g_lwFr.lite) return ((LwFr3Fn)g_lwFr.markOrig)(obj, id, frame);
    uintptr_t ret = LwFrRet(__builtin_return_address(0));
    if (ret == 0x89F6Fu) g_lwFr.mark0 = LwFrNow(); else if (ret == 0x8A187u) g_lwFr.mark1 = LwFrNow();
    return ((LwFr3Fn)g_lwFr.markOrig)(obj, id, frame);
}
static uintptr_t LwFrUpdEnd(uintptr_t a, uintptr_t b)
{
    if (!g_lwFr.lite && LwFrRet(__builtin_return_address(0)) == 0x89F99u) g_lwFr.updEnd = LwFrNow();
    return ((LwFr2Fn)g_lwFr.updEndOrig)(a, b);
}
static uintptr_t LwFrKick(uintptr_t r, float dt)
{
    int ours = !g_lwFr.lite && LwFrRet(__builtin_return_address(0)) == 0x8A744u;
    if (ours) g_lwFr.kickIn = LwFrNow();
    uintptr_t out = ((LwFrKickFn)g_lwFr.kickOrig)(r, dt);
    if (ours) g_lwFr.kickOut = LwFrNow();
    return out;
}
/* Inside the game update, main thread only: engine world update, physics step, animation update and its wait. */
#define LW_FR_TIMED(field, call) if (g_lwFr.lite) return call; LONGLONG t = LwFrNow(); uintptr_t out = call; if (GetCurrentThreadId() == g_lwFr.mainTid) g_lwFr.field += LwFrNow() - t; return out;
static uintptr_t LwFrEngine(uintptr_t w, float a, float b) { LW_FR_TIMED(engine, ((LwFrEngineFn)g_lwFr.engineOrig)(w, a, b)) }
static uintptr_t LwFrPhysics(uintptr_t w, uintptr_t args) { LW_FR_TIMED(physics, ((LwFr2Fn)g_lwFr.physOrig)(w, args)) }
static uintptr_t LwFrAnim(uintptr_t w, float dt) { LW_FR_TIMED(anim, ((LwFrKickFn)g_lwFr.animOrig)(w, dt)) }
static uintptr_t LwFrAnimWait(uintptr_t sm) { LW_FR_TIMED(animWait, ((LwFr1Fn)g_lwFr.animWaitOrig)(sm)) }
/* 0x7D7930(world): the game thread's join of the Havok AI step. */
static uintptr_t LwFrJoin(uintptr_t w)
{
    if (g_lwFr.lite) return ((LwFr1Fn)g_lwFr.joinOrig)(w);
    LONGLONG t = LwFrNow(); uintptr_t out = ((LwFr1Fn)g_lwFr.joinOrig)(w);
    if (GetCurrentThreadId() == g_lwFr.mainTid) g_lwFr.join += LwFrNow() - t;
    return out;
}

/* The message types with the most render time, all frames and the frames over 12 ms busy, ms a frame. */
static void LwFrTypesNote(void)
{
    char all[400] = "", slow[400] = ""; size_t at = 0, st = 0; uint8_t used[LW_FR_TYPES] = {0};
    for (int n = 0; n < 10; n++)
    {
        int best = -1;
        for (int t = 0; t < LW_FR_TYPES; t++) if (!used[t] && g_lwFr.allType[t] && (best < 0 || g_lwFr.allType[t] > g_lwFr.allType[best])) best = t;
        if (best < 0) break;
        used[best] = 1;
        at += (size_t)snprintf(all + at, sizeof all - at, " %02X %.2f (%.1f)", best, LwFrMs(g_lwFr.allType[best]) / g_lwFr.rFrames, (double)g_lwFr.nType[best] / g_lwFr.rFrames);
        if (g_lwFr.rSlow) st += (size_t)snprintf(slow + st, sizeof slow - st, " %02X %.2f", best, LwFrMs(g_lwFr.slowType[best]) / g_lwFr.rSlow);
    }
    Note("living world: test: render messages, ms a frame (calls) over %u frames:%s; in the %u frames over 12 ms busy:%s", g_lwFr.rFrames, all, g_lwFr.rSlow, slow);
    memset(g_lwFr.allType, 0, sizeof g_lwFr.allType); memset(g_lwFr.slowType, 0, sizeof g_lwFr.slowType); memset(g_lwFr.nType, 0, sizeof g_lwFr.nType);
    g_lwFr.rFrames = g_lwFr.rSlow = 0;
}
static void LwFrTypesFrame(float busyMs)
{
    int slow = busyMs > 12.0f;
    for (int t = 0; t < LW_FR_TYPES; t++) { g_lwFr.allType[t] += g_lwFr.fType[t]; if (slow) g_lwFr.slowType[t] += g_lwFr.fType[t]; g_lwFr.fType[t] = 0; }
    g_lwFr.rFrames++; g_lwFr.rSlow += (uint32_t)slow;
    ULONGLONG now = GetTickCount64();
    if (!g_lwFr.typeNoted) g_lwFr.typeNoted = now;
    if (now - g_lwFr.typeNoted >= 30000) { LwFrTypesNote(); g_lwFr.typeNoted = now; }
}

/* Render thread, at a frame-end message: the frame just finished, for the main thread's next record. */
static void LwFrRenderFrameEnd(uintptr_t r, LONGLONG a)
{
    uintptr_t dev = 0; float gpu = -1.0f;
    if (!SafeRead(r + 0x1168, &dev, 8) && dev) SafeRead(dev + 0x1480, &gpu, 4);
    float v[7] = {g_lwFr.rStart ? LwFrMs(a - g_lwFr.rStart) : 0.0f, LwFrMs(g_lwFr.rBusy), LwFrMs(g_lwFr.rGpuWait), LwFrMs(g_lwFr.rDrain),
        LwFrMs(g_lwFr.rPresent), LwFrMs(g_lwFr.rNxMem), gpu * 1000.0f};
    InterlockedIncrement(&g_lwFr.rWrite);
    for (int i = 0; i < 7; i++) g_lwFr.rLast[i] = v[i];
    for (int i = 0; i < 4; i++) { g_lwFr.rLast[7 + i] = LwFrMs(g_lwFr.rType[i]); g_lwFr.rType[i] = 0; }
    g_lwFr.rInstrLast = LwFrMs(g_lwFr.instrR); InterlockedExchange(&g_lwFr.rFlagsLast, (LONG)g_lwFr.flagsR);
    g_lwFr.instrR = 0; g_lwFr.flagsR = 0;
    InterlockedIncrement(&g_lwFr.rSeq); InterlockedIncrement(&g_lwFr.rWrite);
    if (!g_lwFr.lite) LwFrTypesFrame(v[1]);
    g_lwFr.rStart = a; g_lwFr.rBusy = g_lwFr.rGpuWait = g_lwFr.rDrain = g_lwFr.rPresent = g_lwFr.rNxMem = 0;
}
static int LwFrTypeSlot(uint16_t type) { return type == 0x08 ? 0 : type == 0x0A ? 1 : type == 0x09 ? 2 : 3; }

/* Render thread. 0x113010(r, message, payload) handles one message; type 0x0B (word +0xC) ends a frame. */
static uintptr_t LwFrMessage(uintptr_t r, uintptr_t msg, uintptr_t payload)
{
    uint16_t type = *(volatile uint16_t*)(msg + 0xC); LONGLONG a = LwFrNow(); /* the handler's own first read (0x113023) */
    if (!g_lwFr.renderTid) g_lwFr.renderTid = GetCurrentThreadId();
    if (type == 0x0B && g_lwFr.depth == 0)
    {
        LwFrRenderFrameEnd(r, a);
        LONGLONG b = LwFrNow(); g_lwFr.instrR = b - a; a = b; /* the new frame's; holds its note lines already timed */
    }
    if (g_lwFr.lite) return ((LwFr3Fn)g_lwFr.msgOrig)(r, msg, payload);
    g_lwFr.depth++;
    uintptr_t out = ((LwFr3Fn)g_lwFr.msgOrig)(r, msg, payload);
    if (--g_lwFr.depth == 0)
    {
        LONGLONG d = LwFrNow() - a; g_lwFr.rBusy += d; g_lwFr.rType[LwFrTypeSlot(type)] += d;
        if (type < LW_FR_TYPES) { g_lwFr.fType[type] += d; g_lwFr.nType[type]++; }
    }
    return out;
}
/* In present 0x6C5940: the frames-in-flight GPU wait (ret 0x6C5F43), the submit thread's drain before Present1
 * (ret 0x6C601D), the first call after Present1 (ret 0x6C617D) and the NxMem12 frame end (ret 0x6D1053). */
static uintptr_t LwFrGpuWait(uintptr_t fs, uintptr_t target)
{
    if (g_lwFr.lite) return ((LwFr2Fn)g_lwFr.gpuOrig)(fs, target);
    LONGLONG t = LwFrNow(); uintptr_t out = ((LwFr2Fn)g_lwFr.gpuOrig)(fs, target);
    if (LwFrRet(__builtin_return_address(0)) == 0x6C5F43u) g_lwFr.rGpuWait += LwFrNow() - t;
    return out;
}
static uintptr_t LwFrDrain(uintptr_t sub)
{
    if (g_lwFr.lite) { g_lwFr.drainEnd = 0; return ((LwFr1Fn)g_lwFr.drainOrig)(sub); }
    LONGLONG t = LwFrNow(); uintptr_t out = ((LwFr1Fn)g_lwFr.drainOrig)(sub); LONGLONG e = LwFrNow();
    if (LwFrRet(__builtin_return_address(0)) == 0x6C601Du) { g_lwFr.rDrain += e - t; g_lwFr.drainEnd = e; }
    return out;
}
static uintptr_t LwFrAfterPresent(uintptr_t a)
{
    if (!g_lwFr.lite && LwFrRet(__builtin_return_address(0)) == 0x6C617Du && g_lwFr.drainEnd) { g_lwFr.rPresent += LwFrNow() - g_lwFr.drainEnd; g_lwFr.drainEnd = 0; }
    return ((LwFr1Fn)g_lwFr.afterOrig)(a);
}
static uintptr_t LwFrNxMem(uintptr_t m, uintptr_t cl)
{
    if (g_lwFr.lite) return ((LwFr2Fn)g_lwFr.nxOrig)(m, cl);
    LONGLONG t = LwFrNow(); uintptr_t out = ((LwFr2Fn)g_lwFr.nxOrig)(m, cl);
    if (LwFrRet(__builtin_return_address(0)) == 0x6D1053u) g_lwFr.rNxMem += LwFrNow() - t;
    return out;
}

/* Hooked and first in its chain: most detours here test the game's return address, which only the first link sees. */
static int LwFrHook(const char* what, uint32_t rva, const char* pin, void* detour, void** orig)
{
    if (!ApiCodeMatches(g_lwFr.exe + rva, pin) || LoaderHook(what, g_lwFr.exe + rva, detour, orig) || *orig == NULL) return 0;
    AcquireSRWLockShared(&g_hookLock);
    HookTarget* h = HookTargetOf(g_lwFr.exe + rva); int first = h && HookPlace(h, detour) == 1;
    ReleaseSRWLockShared(&g_hookLock);
    if (!first) Note("living world: test: frame recorder: %s is not first in its chain", what);
    return first;
}

static void LwInstallFrec(void)
{
    static int done; char t[4] = ""; LARGE_INTEGER hz;
    if (done++ || GetEnvironmentVariableA("HELLFORGE_LW_FREC", t, sizeof t) == 0 || t[0] != '1') return;
    g_lwFr.exe = (uintptr_t)GetModuleHandleA(NULL); QueryPerformanceFrequency(&hz); g_lwFr.toMs = 1000.0 / (double)hz.QuadPart;
    g_lwFr.file = OpenLog(L"frames.bin");
    if (g_lwFr.file == INVALID_HANDLE_VALUE) { Note("living world: test: frame recorder: frames.bin cannot be opened; off"); return; }
    uint32_t head[4] = {LW_FR_MAGIC, (uint32_t)sizeof(LwFrRec), 2, 0}; DWORD wrote;
    WriteFile(g_lwFr.file, head, sizeof head, &wrote, NULL);
    g_lwFr.wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE w = g_lwFr.wake ? CreateThread(NULL, 0, LwFrWriter, NULL, 0, NULL) : NULL;
    if (!w) { Note("living world: test: frame recorder: no writer thread; off"); return; }
    SetThreadPriority(w, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(w);
    LONGLONG c0 = LwFrNow(); for (int i = 0; i < 20000; i++) (void)LwFrNow();
    g_lwFr.clockNs = LwFrMs(LwFrNow() - c0) * 1e6 / 20000.0;
    int ok = LwFrHook("frame recorder: fence", 0x106030u, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 80 B9 80", (void*)LwFrFence, &g_lwFr.fenceOrig);
    ok += LwFrHook("frame recorder: Reflex sleep", 0xADE70u, "48 83 EC 28 83 79 10 01 75 27 48 8B 41 18 80 78 01 00 74 1D 48 8B 49 08", (void*)LwFrSleep, &g_lwFr.sleepOrig);
    ok += LwFrHook("frame recorder: AI join", 0x7D7930u, "48 89 5C 24 10 48 89 6C 24 18", (void*)LwFrJoin, &g_lwFr.joinOrig);
    ok += LwFrHook("frame recorder: render message", 0x113010u, "48 89 5C 24 08 55 56 57 41 56 41 57 48 8B EC 48 83 EC 60 0F B7 7A 0C 4D", (void*)LwFrMessage, &g_lwFr.msgOrig);
    ok += LwFrHook("frame recorder: GPU wait", 0x6C0590u, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57", (void*)LwFrGpuWait, &g_lwFr.gpuOrig);
    ok += LwFrHook("frame recorder: submit drain", 0x6BEED0u, "40 53 48 83 EC 70 48 8B D9 33 D2 48 8B 49 28 FF 15 13 78 D3 00 33 C0 4C", (void*)LwFrDrain, &g_lwFr.drainOrig);
    ok += LwFrHook("frame recorder: after Present1", 0x6F0360u, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83", (void*)LwFrAfterPresent, &g_lwFr.afterOrig);
    ok += LwFrHook("frame recorder: NxMem12 frame end", 0x6A8200u, "48 89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81", (void*)LwFrNxMem, &g_lwFr.nxOrig);
    ok += LwFrHook("frame recorder: latency marker", 0xADEB0u, "48 81 EC D8 00 00 00 44 8B 49 10 41 83 E9 01 0F", (void*)LwFrMark, &g_lwFr.markOrig);
    ok += LwFrHook("frame recorder: after the game update", 0x91420u, "48 89 5C 24 08 57 48 83 EC 20 8B 41 04 48 8B F9", (void*)LwFrUpdEnd, &g_lwFr.updEndOrig);
    ok += LwFrHook("frame recorder: frame kick", 0x10A5C0u, "48 8B C4 41 57 48 81 EC 80 00 00 00 80 B9 80 14", (void*)LwFrKick, &g_lwFr.kickOrig);
    ok += LwFrHook("frame recorder: engine world update", 0x1B0B10u, "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55", (void*)LwFrEngine, &g_lwFr.engineOrig);
    ok += LwFrHook("frame recorder: physics step", 0x79EB10u, "48 8B C4 89 48 08 55 53 57 41 54 48 8D A8 38 F6", (void*)LwFrPhysics, &g_lwFr.physOrig);
    ok += LwFrHook("frame recorder: animation update", 0x1B0670u, "40 57 48 83 EC 50 FF 81 80 08 23 00 0F 28 C1 F3", (void*)LwFrAnim, &g_lwFr.animOrig);
    ok += LwFrHook("frame recorder: animation wait", 0x556040u, "40 53 57 48 83 EC 38 8B 99 F0 00 00 00 48 8B F9", (void*)LwFrAnimWait, &g_lwFr.animWaitOrig);
    g_lwFr.on = g_lwFr.fenceOrig && g_lwFr.msgOrig; /* the frame and render frame ends */
    if (!g_lwFr.on) { Note("living world: test: frame recorder: verdict OFF, the fence or render message hook is not in (%d of 15 hooks)", ok); return; }
    LwInstallSteamCalls(); g_logTimer = LwFrLogTime;
    Note("living world: test: frame recorder: verdict %s, %d of 15 hooks in, %u-byte records to frames.bin by a writer thread; clock read %.1f ns", ok == 15 ? "ok" : "INCOMPLETE", ok,
        (unsigned)sizeof(LwFrRec), g_lwFr.clockNs);
}
/* "frec N": the scenario's phase, stored in each record; -1 notes the frames buffered and the batches dropped. */
static void LwFrecCmd(int phase)
{
    if (!g_lwFr.on) { Note("living world: test: frame recorder is off (HELLFORGE_LW_FREC=1)"); return; }
    if (phase < 0) { Note("living world: test: frame recorder: %u frames buffered, %ld batches dropped", g_lwFr.used, (long)g_lwFr.drops); return; }
    InterlockedExchange(&g_lwFr.phase, phase);
    Note("living world: test: frame recorder: phase %d from frame %u", phase, g_lwFr.frame);
}
/* From the harness's Lua on the game thread, with or without the living world: "frec N", "frec lite 0/1", "frec lite ab S" (only the
 * frame and render frame ends timed, for the recorder's own cost; ab: on and off every S s), "frec mark M" (0..255, an outside capture's window),
 * "frec flush" (the partial batch to the file at the next frame end). */
static void LwFrecText(const char* a)
{
    int n = 0;
    if (sscanf(a, "frec lite ab %d", &n) == 1)
    { g_lwFr.liteAt = GetTickCount64(); InterlockedExchange(&g_lwFr.liteAb, n > 0 ? n : 0); Note("living world: test: frame recorder: lite every %d s", n); }
    else if (sscanf(a, "frec lite %d", &n) == 1) { InterlockedExchange(&g_lwFr.liteAb, 0); InterlockedExchange(&g_lwFr.lite, n != 0); Note("living world: test: frame recorder: lite %d from frame %u", n != 0, g_lwFr.frame); }
    else if (sscanf(a, "frec mark %d", &n) == 1) { InterlockedExchange(&g_lwFr.mark, n & 0xFF); Note("living world: test: frame recorder: mark %d from frame %u", n & 0xFF, g_lwFr.frame); }
    else if (strcmp(a, "frec flush") == 0) { InterlockedExchange(&g_lwFr.flushReq, 1); InterlockedExchange(&g_lwFr.rmFlushReq, 1); }
    else if (sscanf(a, "frec %d", &n) == 1) LwFrecCmd(n);
    else Note("living world: test: %s: not understood (frec N, frec lite 0/1, frec mark M, frec flush)", a);
}
