/* The far-unit animation rate's switches, hooks and notes (living_world_animrate.c). Commands store wanted values
 * only; the kick detour applies them at its entry, where no animation task runs. */
typedef struct { uint8_t dll; uint32_t rva; const char* bytes; } LwArPin;
static const LwArPin g_lwArCorePins[] = {
    {0, 0x555BB0u, "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57"},
    {0, 0x5530C0u, "48 8B C4 4C 89 40 18 55 41 54 48 8D A8 28 FF FF FF 48 81 EC C8 01 00 00"},
    {0, 0x54DAC0u, "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57"},
    {1, 0x59FED0u, "40 55 53 57 41 54 41 56 41 57 48 8D AC 24 58 FF FF FF 48 81 EC A8 01 00"},
    {0, 0x2029D0u, "48 89 5C 24 08 57 48 81 EC E0 00 00 00 48 8B F9 8B CA E8 D9 AE E9 FF 48"},
    {0, 0x556350u, "48 8B C4 48 89 58 10 4C 89 40 18 48 89 48 08 55 56 57 41 54 41 55 41 56"},
    {0, 0x5545AAu, "48 8B 0C F1 E8 0D EB FF FF"}, {0, 0x5545BAu, "48 8B 0C F1 E8 FD 94 FF FF"},
    {0, 0x33407Eu, "E8 2D 1B 22 00"}, {0, 0x1B06C7u, "E8 E4 54 3A 00"}, {0, 0x1B06B8u, "48 8B 89 50 00 23 00"},
    {0, 0x33404Fu, "C6 83 68 09 23 00 01"}, {0, 0x1B0840u, "C6 87 68 09 23 00 00"},
    {0, 0x55637Eu, "4C 8B E9"},
    {0, 0x5565DEu, "41 39 BD 90 00 00 00 0F 86 B4 02 00 00 0F 1F 44 00 00 4D 8B 85 98 00 00 00 41 8B F6 49 8B 04 F0 48 8B 48 30"},
    {0, 0x55689Fu, "41 83 BD A8 00 00 00 00 44 8B F7 0F 86 C4 02 00 00 4D 8B 85 B0 00 00 00 41 8B F6 49 8B 04 F0 48 8B 48 30"},
    {0, 0x556602u, "48 83 B9 90 03 00 00 00 0F 84 7F 02 00 00 83 B9 78 03 00 00"}, {0, 0x5568C2u, "48 83 B9 90 03 00 00 00"},
    {0, 0x2BD9A0u, "48 8B 81 68 01 00 00 C3"}, {0, 0x2BD9C0u, "48 8B 81 78 01 00 00 C3"}, {0, 0x202A02u, "83 B8 80 02 00 00 00 0F 85"},
    {0, 0x4EF0B4u, "48 6B C1 58 48 03 87 98 00 00 00 83 38 01"},
    {0, 0x4EF372u, "41 8B 41 30 45 89 61 50 4D 03 D6 41 FF C4 41 89 42 08 41 0F B6 41 34 41 88 42 55"}, {0, 0x5AEA2Du, "40 38 75 55"},
    {1, 0x59F0A0u, "48 8B 87 B8 48 00 00 4C 8B 81 F0 01 00 00 48 8B"}, {1, 0x59F0B0u, "0C F0 8B 51 0C 48 8D 4C 24 20 41 FF D0"},
    {1, 0x59F478u, "3B 9F 90 48 00 00 73 07 E8 4B 0A 00 00"}, {1, 0x59FF3Du, "80 7C 08 20 00"},
    {1, 0x59F0D6u, "48 8B 87 C8 48 00 00 F3 0F 10 54 08 74"}, /* integ+0x74 scales the getter's xy (travel check) */
    {1, 0x5711B9u, "48 8B 05 88 51 DB 02 8D 4A 0C 44 3B C1 44 0F 43 C1 F3 0F 10 58 04"},
    {1, 0x4A52D3u, "45 8B 4A 20 45 33 C0 41 8B 5A 28"}, {1, 0x4A5344u, "48 69 C8 AC 00 00 00 49 8B 42 38 83 3C 01 00"},
    {1, 0x5A0D83u, "80 7C 3B 0D 00"}, {1, 0x5A0D8Au, "0F B6 4C 3B 0F"}, {1, 0x5A0E58u, "F3 0F 10 4C 3B 10"}, {1, 0x59FE70u, "42 80 BC 02 80 00 00 00"},
};
static const LwArPin g_lwArPostPins[] = {
    {0, 0x1FC720u, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49 8B F0 8B FA 8B D9 E8 85"}, {1, 0x59FD5Cu, "4C 8B 48 18 41 FF 51 38 EB 28"},
    {0, 0x12F2E8u, "49 8B 51 50 41 8D 48 01 44 8B C1 B8 01 00 00 00 49 C1 E8 05 83 E1 1F D3 E0 42 09 04 82 49 8B 41 50 83 08 01"},
    {0, 0x2BD870u, "48 8D 41 60 C3"},
};
static const LwArPin g_lwArPinPins[] = {
    {0, 0x2C4AE0u, "48 8B C4 48 89 58 10 48 89 70 18 55 57 41 56 48 8D A8 98 FD FF FF 48 81"}, {0, 0x5633A6u, "41 80 BE 3A 02 00 00 00"},
    {0, 0x5631A5u, "48 8B 99 10 02 00 00"}, {0, 0x5631C4u, "FF 90 38 02 00 00"}, {0, 0x5631D6u, "F3 0F 10 6D 00 F3 0F 10 55 0C 0F 2F EA"},
    {0, 0x55EB2Bu, "C6 81 3A 02 00 00 01"},
};
static const LwArPin g_lwArCensusPins[] = {
    {0, 0x2DF170u, "4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57"}, {0, 0x1B0D83u, "48 8D 8F 28 02 23 00"},
    {0, 0x1B0D96u, "E8 D5 E3 12 00"}, {0, 0x2BD870u, "48 8D 41 60 C3"}, {0, 0x2BD9E0u, "48 8B 81 D0 01 00 00 C3"},
    {0, 0x12F2E8u, "49 8B 51 50 41 8D 48 01 44 8B C1 B8 01 00 00 00 49 C1 E8 05 83 E1 1F D3 E0 42 09 04 82 49 8B 41 50 83 08 01"},
    {0, 0x1B0863u, "48 8B AF 68 08 23 00"}, {0, 0x1B087Fu, "39 85 80 00 00 00"}, {0, 0x1B0890u, "48 8B 85 88 00 00 00"},
};
#define LW_AR_PINS(a) (a), sizeof(a) / sizeof((a)[0])

static int LwArPinsOk(const LwArPin* p, size_t n, const char* what)
{
    if (strcmp(g_build, "25480438")) { Note("living world: animrate %s: not build 25480438; not hooked", what); return 0; }
    for (size_t i = 0; i < n; i++)
        if (!ApiCodeMatches((p[i].dll ? g_lw.game : g_lwAr.exe) + p[i].rva, p[i].bytes))
        { Note("living world: animrate %s: %s+0x%X is not the reviewed code; not hooked", what, p[i].dll ? "game.dll" : "exe", p[i].rva); return 0; }
    return 1;
}
static int LwArHook(const char* what, int dll, uint32_t rva, void* detour, void** orig)
{
    return !LoaderHook(what, (dll ? g_lw.game : g_lwAr.exe) + rva, detour, orig) && *orig != NULL;
}
/* The kick, both worker detours, motion step, getter and controller pass, or nothing: without the getter a held unit
 * would get the same root delta every frame between runs and travel N times too far. */
static int LwArInstallCore(void)
{
    static int tried;
    if (g_lwAr.hooked || tried++) return g_lwAr.hooked;
    if (!LwArPinsOk(LW_AR_PINS(g_lwArCorePins), "core")) return 0;
    int ok = LwArHook("animrate: root delta getter", 0, 0x2029D0u, (void*)LwArGetter, (void**)&g_lwAr.getOrig) &&
        LwArHook("animrate: motion step", 1, 0x59FED0u, (void*)LwArMotion, (void**)&g_lwAr.motionOrig) &&
        LwArHook("animrate: sm_update", 0, 0x5530C0u, (void*)LwArUpdate, (void**)&g_lwAr.updOrig) &&
        LwArHook("animrate: dynamic bones", 0, 0x54DAC0u, (void*)LwArBones, (void**)&g_lwAr.bonesOrig) &&
        LwArHook("animrate: controller pass", 0, 0x556350u, (void*)LwArCtlPass, (void**)&g_lwAr.ctlOrig) &&
        LwArHook("animrate: sm_kick", 0, 0x555BB0u, (void*)LwArKick, (void**)&g_lwAr.kickOrig);
    g_lwAr.hooked = ok; /* the kick goes in last: until it is in, no entry exists and every other detour passes through */
    Note("living world: animrate: %s", ok ? "hooked (kick, sm_update, dynamic bones, motion step, getter, controller pass)" : "a hook failed; off");
    return ok;
}
static int LwArFirst(uintptr_t target, void* detour)
{
    AcquireSRWLockShared(&g_hookLock);
    HookTarget* h = HookTargetOf(target);
    int first = h != NULL && HookPlace(h, detour) == 1;
    ReleaseSRWLockShared(&g_hookLock);
    return first;
}
static int LwArInstallGroup(int* in, const LwArPin* pins, size_t n, const char* what, uint32_t rva, void* detour, void** orig)
{
    static int tried[3];
    int slot = in == &g_lwAr.postIn ? 0 : in == &g_lwAr.pinIn ? 1 : 2;
    if (*in || tried[slot]++) return *in;
    *in = LwArPinsOk(pins, n, what) && LwArHook(what, 0, rva, detour, orig);
    Note("living world: animrate %s: %s", what, *in ? "hooked" : "not hooked; stays off");
    return *in;
}
static int LwArWaitsOn(void) { char t[4] = ""; return GetEnvironmentVariableA("HELLFORGE_LW_WAITS", t, sizeof t) > 0 && t[0] == '1'; }

/* ---- switches ---- */
static void LwArLeaveAll(void) { for (int k = 0; k < g_lwAr.count; k++) LwArLeave(&g_lwAr.e[k], LW_AR_R_SWITCH); }
/* At the kick's entry. A change of dry or step lets every entry leave under the mode it entered with, then switches. */
static void LwArApplySwitches(void)
{
    LwArSet* w = &g_lwAr.want; LwArSet* s = &g_lwAr.set;
    if (!memcmp(w, s, sizeof *w)) return;
    int mode = w->dry != s->dry || w->step != s->step;
    memset(g_lwAr.block, 0, sizeof g_lwAr.block);
    if (mode || w->period == 0) LwArLeaveAll();
    g_lwAr.draining = mode && g_lwAr.count > 0;
    LwArSet next = *w;
    if (g_lwAr.draining) { next.dry = s->dry; next.step = s->step; }
    *s = next;
    if (!g_lwAr.draining)
        Note("living world: animrate: period %d, dry %d, step %d, post %d, ctl %d, pin %d, census %d, near %d m, px %d %d (kick %u)", s->period, s->dry,
            s->step, s->post, s->ctl, s->pin, s->census, s->nearM, s->pxA, s->pxB, g_lwAr.kick);
}
static void LwArSetPeriod(int n)
{
    if (n != 0 && n != 2 && n != 3 && n != 4) { Note("living world: animrate: the period is 0, 2, 3 or 4"); return; }
    if (n && !LwArInstallCore()) { Note("living world: animrate: not hooked; stays off"); return; }
    g_lwAr.want.period = n;
    if (n) g_lwAr.lastPeriod = n;
    Note("living world: animrate %d asked; the next kick applies it", n);
}
/* Parts that need their own hook: refused when it is not in. */
static int LwArPartReady(const int* to, int v)
{
    if (v == 0) return 1;
    if (to == &g_lwAr.want.post) return LwArInstallGroup(&g_lwAr.postIn, LW_AR_PINS(g_lwArPostPins), "post", 0x1FC720u, (void*)LwArSetPos, (void**)&g_lwAr.setOrig);
    if (to == &g_lwAr.want.census) return LwArInstallGroup(&g_lwAr.censusIn, LW_AR_PINS(g_lwArCensusPins), "census", 0x2DF170u, (void*)LwArCensus, (void**)&g_lwAr.sgOrig);
    if (to != &g_lwAr.want.pin) return 1;
    if (LwArWaitsOn()) { Note("living world: animrate pin: refused while HELLFORGE_LW_WAITS=1 (it hooks exe 0x2C4AE0 too)"); return 0; }
    if (!LwArInstallGroup(&g_lwAr.pinIn, LW_AR_PINS(g_lwArPinPins), "pin", 0x2C4AE0u, (void*)LwArBox, (void**)&g_lwAr.boxOrig)) return 0;
    g_lwAr.pinFirst = LwArFirst(g_lwAr.exe + 0x2C4AE0u, (void*)LwArBox);
    return 1;
}
static void LwArCmd(const char* s)
{
    static const struct { const char* fmt; int* to; int lo, hi; } kSwitches[] = {
        {"animrate dry %d", &g_lwAr.want.dry, 0, 1}, {"animrate step %d", &g_lwAr.want.step, 0, 1}, {"animrate post %d", &g_lwAr.want.post, 0, 1},
        {"animrate ctl %d", &g_lwAr.want.ctl, 0, 1}, {"animrate pin %d", &g_lwAr.want.pin, 0, 1}, {"animrate census %d", &g_lwAr.want.census, 0, 1},
        {"animrate near %d", &g_lwAr.want.nearM, 10, 1000}, {"animrate cycle %d", &g_lwAr.wantCycle, 0, 3600},
    };
    int a = 0, b = 0;
    for (size_t i = 0; i < sizeof kSwitches / sizeof kSwitches[0]; i++)
    {
        if (sscanf(s, kSwitches[i].fmt, &a) != 1) continue;
        if (a < kSwitches[i].lo || a > kSwitches[i].hi) Note("living world: animrate: %s takes %d..%d", kSwitches[i].fmt, kSwitches[i].lo, kSwitches[i].hi);
        else if (LwArPartReady(kSwitches[i].to, a)) { *kSwitches[i].to = a; Note("living world: animrate: \"%s\" asked; the next kick applies it", s); }
        return;
    }
    if (sscanf(s, "animrate px %d %d", &a, &b) == 2)
    {
        if (a > b && b >= 1 && a <= 1440) { g_lwAr.want.pxA = a; g_lwAr.want.pxB = b; Note("living world: animrate: px %d %d asked", a, b); }
        else Note("living world: animrate px A B: 1440 >= A > B >= 1");
    }
    else if (sscanf(s, "animrate %d", &a) == 1) LwArSetPeriod(a);
    else Note("living world: animrate: not understood: %s", s);
}

/* ---- notes ---- */
static double LwArPct(uint32_t a, uint32_t b) { return b ? 100.0 * (double)a / (double)b : 0.0; }
static void LwArNoteEntries(void)
{
    const LwArCounters* c = &g_lwAr.c; char line[512]; int at = 0; uint32_t v = 0, h = 0, leaving = 0, postOff = 0; double cons = c->consWorst;
    for (int k = 0; k < g_lwAr.count; k++)
    {
        const LwArEntry* e = &g_lwAr.e[k];
        if (e->state != LW_AR_ACTIVE) leaving++; else if (e->tier == LW_AR_V) v++; else h++;
        postOff += e->postOff; cons = fmax(cons, LwArConservation(e));
    }
    for (int r = 0; r < LW_AR_R_COUNT && at < (int)sizeof line - 32; r++)
        if (c->leaves[r]) at += snprintf(line + at, sizeof line - (size_t)at, " %s %u", g_lwArReason[r], c->leaves[r]);
    Note("living world: animrate entries: %d now (V %u, H %u, leaving %u); %u entered, %u dropped, %u blocked; leaves:%s", g_lwAr.count, v, h, leaving,
        c->enters, c->drops, c->blocked, at ? line : " none");
    Note("living world: animrate getter: %u takes, %u stale pendings, %u hand-outs; conservation worst %.3g m; largest spread at removal %.3f m; "
        "travel path / root travel x scale %.3f (%.1f m / %.1f m); cap: %u under 2.5 dt, shortest looping clip %.3f s, largest summed dt / clip %.2f",
        c->takes, c->staleTakes, c->handouts, cons, c->spreadWorst, c->travelGiven > 0 ? c->travelDisp / c->travelGiven : 0.0, c->travelDisp,
        c->travelGiven, c->capLow, c->capShortest < 1e8f ? (double)c->capShortest : 0.0, (double)c->capRatioMax);
    Note("living world: animrate parts: motion step off-thread %u, step mode skipped %u taken %u (largest dt %.3f s); position writes dropped %u "
        "passed %u, other writers %u, entries with post off %u; controller pass %u players held in %u passes; boxes skipped %u (first in chain %d); "
        "census %u frames: held moving dirty %u of %u (%.0f%%), held idle dirty %u of %u (%.0f%%)", c->motionOff, c->stepSkipped, c->stepTaken,
        (double)c->motionAccMax, c->posDropped, c->posPassed, c->postOther, postOff, c->ctlBracketed, c->ctlPasses, c->boxSkip, g_lwAr.pinFirst,
        c->census, c->censusDirty[1], c->censusAll[1], LwArPct(c->censusDirty[1], c->censusAll[1]), c->censusDirty[0], c->censusAll[0],
        LwArPct(c->censusDirty[0], c->censusAll[0]));
}
static void LwArNote(const char* why, double tscPerMs, ULONGLONG ms)
{
    const LwArCounters* c = &g_lwAr.c;
    double frames = (double)(c->kickFrames[0] + c->kickFrames[1] + c->kickFrames[2]), runs = (double)(c->updRun ? c->updRun : 1);
    if (frames < 1) frames = 1;
    if (g_lwAr.pinIn) g_lwAr.pinFirst = LwArFirst(g_lwAr.exe + 0x2C4AE0u, (void*)LwArBox);
    Note("living world: animrate (%s, %.1f s, period %d, dry %d, step %d, post %d, ctl %d, pin %d, phase %u): host %d; kicks camera %u, late %u; "
        "frames with 0/1/2+ kicks %u/%u/%u; kick detour %.1f us average, %.1f us at most; camera unreadable %u, no forward %u; remote capped %u; "
        "off the game thread %u", why, (double)ms / 1000.0, g_lwAr.set.period, g_lwAr.set.dry, g_lwAr.set.step, g_lwAr.set.post, g_lwAr.set.ctl,
        g_lwAr.set.pin, g_lwAr.phase, g_lwAr.host, c->kicks[0], c->kicks[1], c->kickFrames[0], c->kickFrames[1], c->kickFrames[2],
        c->kCount ? 1000.0 * (double)c->kTicks / c->kCount / tscPerMs : 0.0, 1000.0 * (double)c->kMax / tscPerMs, c->camNone, c->camNoFwd,
        c->remoteCapped, c->offThread);
    Note("living world: animrate workers: sm_update for entries run %u, skipped %u, %.3f ms a frame; dynamic bones run %u, skipped %u, %.3f ms a "
        "frame; stale keys %u; events %.2f records a run (%.1f a frame)", c->updRun, c->updSkip, (double)c->updTicks / tscPerMs / frames, c->bonesRun,
        c->bonesSkip, (double)c->bonesTicks / tscPerMs / frames, c->stale, (double)c->evBytes / 16.0 / runs, (double)c->evBytes / 16.0 / frames);
    LwArNoteEntries();
}
/* Every 10 s and at each phase end; the rdtsc rate is taken against the performance counter over the same span. */
static void LwArNoteNow(const char* why)
{
    static uint64_t tsc0; static LARGE_INTEGER q0;
    LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
    uint64_t tsc = __builtin_ia32_rdtsc();
    double ms = q0.QuadPart ? 1000.0 * (double)(q.QuadPart - q0.QuadPart) / (double)f.QuadPart : 0.0;
    if (ms > 100.0 && (g_lwAr.hooked || g_lwAr.count)) LwArNote(why, (double)(tsc - tsc0) / ms, (ULONGLONG)ms);
    tsc0 = tsc; q0 = q;
    memset(&g_lwAr.c, 0, sizeof g_lwAr.c); g_lwAr.c.capShortest = 1e9f;
}
/* "cycle S": the last non-zero period and 0 by turns every S s; the frame recorder's phase is odd while on. The first
 * second of a phase is left out of the counters. */
static void LwArCycle(ULONGLONG now)
{
    if (g_lwAr.wantCycle == 1) g_lwAr.wantCycle = 2; /* the first second of a phase is not counted */
    if (g_lwAr.wantCycle != g_lwAr.cycle)
    {
        g_lwAr.cycle = g_lwAr.lastPeriod ? g_lwAr.wantCycle : 0;
        if (g_lwAr.wantCycle && !g_lwAr.lastPeriod) { Note("living world: animrate cycle: set a period first"); g_lwAr.wantCycle = 0; }
        if (!g_lwAr.cycle) { g_lwAr.want.period = g_lwAr.lastPeriod; return; }
        g_lwAr.cycleAt = now - (ULONGLONG)g_lwAr.cycle * 1000; g_lwAr.phase = 0;
    }
    if (!g_lwAr.cycle || now - g_lwAr.cycleAt < (ULONGLONG)g_lwAr.cycle * 1000) return;
    LwArNoteNow("phase end");
    g_lwAr.cycleAt = now; g_lwAr.phase++; g_lwAr.countFrom = now + 1000;
    g_lwAr.want.period = (g_lwAr.phase & 1) ? g_lwAr.lastPeriod : 0;
    if (g_lwFr.on) InterlockedExchange(&g_lwFr.phase, (LONG)g_lwAr.phase);
    Note("living world: animrate cycle: phase %u, period %d", g_lwAr.phase, g_lwAr.want.period);
}
/* Game thread, once a tick (LwTickScans): the game thread's id, the host flag, kicks a frame, the cycle and notes. */
static void LwArFrame(void)
{
    static ULONGLONG notedAt;
    ULONGLONG now = GetTickCount64(); int host = 0;
    if (!g_lwAr.tid) g_lwAr.tid = GetCurrentThreadId();
    g_lwAr.counting = now >= g_lwAr.countFrom;
    g_lwAr.host = ActMission(g_lw.game, &host, NULL) && host; g_lwAr.hostAt = now;
    if (g_lwAr.hooked && LwArCounting()) g_lwAr.c.kickFrames[g_lwAr.frameKicks < 2 ? g_lwAr.frameKicks : 2]++;
    g_lwAr.frameKicks = 0;
    LwArCycle(now);
    if (now - notedAt >= 10000) { notedAt = now; LwArNoteNow("10 s"); }
}
static void LwInstallAnimRate(void)
{
    static const char* const kEnv[][2] = {{"HELLFORGE_LW_ANIMRATE_DRY", "dry"}, {"HELLFORGE_LW_ANIMRATE_STEP", "step"},
        {"HELLFORGE_LW_ANIMRATE_POST", "post"}, {"HELLFORGE_LW_ANIMRATE_CTL", "ctl"}, {"HELLFORGE_LW_ANIMRATE_PIN", "pin"},
        {"HELLFORGE_LW_ANIMRATE_CENSUS", "census"}, {"HELLFORGE_LW_ANIMRATE", ""}};
    static int done;
    if (done++) return;
    g_lwAr.exe = (uintptr_t)GetModuleHandleA(NULL);
    g_lwAr.want.nearM = 60; g_lwAr.want.pxA = 32; g_lwAr.want.pxB = 12; g_lwAr.set = g_lwAr.want; g_lwAr.c.capShortest = 1e9f;
    LwArPublishHashes();
    for (size_t i = 0; i < sizeof kEnv / sizeof kEnv[0]; i++)
    {
        char t[16] = "", cmd[64];
        if (GetEnvironmentVariableA(kEnv[i][0], t, sizeof t) == 0 || atoi(t) == 0) continue;
        snprintf(cmd, sizeof cmd, "animrate %s%s%d", kEnv[i][1], kEnv[i][1][0] ? " " : "", atoi(t));
        LwArCmd(cmd);
    }
}
