/* The far-unit animation rate's detours (living_world_animrate.c). Kick, motion step, getter, controller pass,
 * position write and census act only on the game thread; sm_update, dynamic bones and the unit box run on workers
 * and only read what the kick wrote before the kick's tasks were readied (0x555E08). */
static void LwArApplySwitches(void); /* living_world_animrate_cmd.c */
/* Set once a kick and a tick: the first second of a cycle phase is left out of the counters. */
static int LwArCounting(void) { return g_lwAr.counting; }

/* The workers' counters of the last kick, summed on the game thread while no task runs. */
static void LwArSumWorkers(LwArEntry* e)
{
    LwArCounters* c = &g_lwAr.c;
    if (LwArCounting())
    {
        c->updRun += e->updRun; c->updSkip += e->updSkip; c->bonesRun += e->bonesRun; c->bonesSkip += e->bonesSkip;
        c->stale += e->stale; c->boxSkip += e->boxSkip; c->updTicks += e->updTicks; c->bonesTicks += e->bonesTicks; c->evBytes += e->evBytes;
    }
    e->updRun = e->updSkip = e->bonesRun = e->bonesSkip = e->stale = e->boxSkip = 0; e->updTicks = e->bonesTicks = e->evBytes = 0;
}
/* |sum taken - sum given - spread left|, worst axis: 0 within float error when travel is conserved. */
static double LwArConservation(const LwArEntry* e)
{
    double worst = 0;
    for (int a = 0; a < 3; a++) worst = fmax(worst, fabs(e->taken[a] - e->given[a] - (double)e->spread[a]));
    return worst;
}
/* Removed from the table. The LOD flag goes back only through a pointer that still resolves. */
static void LwArDrop(int k, int resolved)
{
    LwArEntry* e = &g_lwAr.e[k];
    LwArSumWorkers(e);
    if (e->pinned && resolved) *(volatile uint8_t*)(e->blender + 0x23A) = 1;
    g_lwAr.c.consWorst = fmax(g_lwAr.c.consWorst, LwArConservation(e));
    g_lwAr.c.spreadWorst = fmax(g_lwAr.c.spreadWorst, sqrt((double)(e->spread[0] * e->spread[0] + e->spread[1] * e->spread[1] + e->spread[2] * e->spread[2])));
    g_lwAr.c.drops++;
    g_lwAr.e[k] = g_lwAr.e[--g_lwAr.count];
}
static void LwArClear(void)
{
    while (g_lwAr.count) { int ok = LwArResolves(&g_lwAr.e[0]); LwArDrop(0, ok); }
    LwArPublishHashes();
}
static void LwArLeave(LwArEntry* e, int reason)
{
    if (e->state != LW_AR_ACTIVE) return;
    e->state = LW_AR_LEAVE; g_lwAr.c.leaves[reason]++;
    if (reason != LW_AR_R_SWITCH && reason != LW_AR_R_GONE) LwArBlock(e->handle);
}
static void LwArRun(LwArEntry* e)
{
    float len = 0;
    if (e->cap < 0.0999f) { len = 2.0f * e->cap; if (len > 0 && e->animAcc / len > g_lwAr.c.capRatioMax) g_lwAr.c.capRatioMax = e->animAcc / len; }
    e->run = 1; e->useDtLast = e->useDt; e->useDt = e->animAcc; e->animAcc = 0;
    e->runKick = g_lwAr.kick; e->pending = 1; e->cap = LwArCap(e->blender); /* no task runs now (1.9) */
}

typedef struct { const LwArCam* cam; const LwArRemote* rv; const LwArMotionMgr* mm; float dt; } LwArKickCtx;
/* Health and alert every kick (a fight starts at once), the other tests every 8th kick, staggered by handle. */
static void LwArGuard(LwArEntry* e, const LwArKickCtx* x)
{
    int r = LW_AR_R_NONE, why = LW_AR_R_TIER;
    int32_t h = LwArHealth(e->id);
    if (h < e->health) r = LW_AR_R_HEALTH;
    else if (LwAlert(e->id)) r = LW_AR_R_ALERT;
    else if (e->stagger == (g_lwAr.kick & 7)) r = LwArNeverSlow(x->mm, e);
    e->health = h;
    if (r == LW_AR_R_NONE && (!LwArInstance(x->mm, e) || !LwArPosition(x->mm, e->motionIndex, e->pos))) r = LW_AR_R_MOTION;
    if (r == LW_AR_R_NONE)
    {
        int t = LwArTierAt(x->cam, x->rv, e->pos, e->tier, &why);
        if (t == LW_AR_S) r = why; else e->tier = (uint8_t)t;
    }
    if (r == LW_AR_R_NONE && e->cap < 2.5f * x->dt) { r = LW_AR_R_CAP; g_lwAr.c.capLow++; }
    if (r != LW_AR_R_NONE) LwArLeave(e, r);
}
/* State labels are the state at the start of the kick: LEAVE runs and becomes LAST; LAST becomes HANDBACK (the
 * getter takes the last delta); HANDBACK runs the carry run and becomes CARRY; CARRY is removed at the next kick. */
static void LwArStep(LwArEntry* e, float dt)
{
    switch (e->state)
    {
    case LW_AR_ACTIVE:
    {
        uint32_t period = e->tier == LW_AR_V ? 2u : (uint32_t)g_lwAr.set.period;
        if (!e->pending && (g_lwAr.kick - e->runKick >= period || e->animAcc + dt > e->cap)) LwArRun(e);
        break;
    }
    case LW_AR_LEAVE: if (!e->pending) { LwArRun(e); e->state = LW_AR_LAST; } break;
    case LW_AR_LAST: e->state = LW_AR_HANDBACK; break;
    case LW_AR_HANDBACK: LwArRun(e); e->pending = 0; e->state = LW_AR_CARRY; break;
    default: break;
    }
}
/* Bone LOD held for tier-H entries off screen at this kick and the one before: the LOD job then ran once with the
 * unit off screen and armed, so the frozen level is the last one (KB 0x563482). Blender step 1 arms it again in a run
 * (0x55EB2B) and the LOD tasks of a kick run before its sm tasks, so it is cleared at every kick. */
static void LwArPinStep(LwArEntry* e, const LwArCam* cam)
{
    int off = cam->state == LW_AR_CAM_VALID && !LwArInCone(cam->pos, cam->fwd, cam->cosHalf, e->pos);
    int want = g_lwAr.set.pin && g_lwAr.pinIn && !g_lwAr.set.dry && e->state == LW_AR_ACTIVE && e->tier == LW_AR_H && off && e->offPrev;
    e->offPrev = (uint8_t)off;
    if (want) { *(volatile uint8_t*)(e->blender + 0x23A) = 0; e->pinned = 1; }
    else if (e->pinned) { *(volatile uint8_t*)(e->blender + 0x23A) = 1; e->pinned = 0; }
}
/* Travel check: the ground path of held tier-H root-motion units against the root travel handed to their steps. */
static int LwArTravelUnit(const LwArEntry* e) { return e->state == LW_AR_ACTIVE && e->tier == LW_AR_H && e->rootMotion; }
static void LwArTravelPath(LwArEntry* e)
{
    int take = LwArTravelUnit(e);
    if (take && e->haveLast && LwArCounting())
        g_lwAr.c.travelDisp += sqrt((double)((e->pos[0] - e->prevPos[0]) * (e->pos[0] - e->prevPos[0]) + (e->pos[1] - e->prevPos[1]) * (e->pos[1] - e->prevPos[1])));
    memcpy(e->prevPos, e->pos, 12); e->haveLast = (uint8_t)take;
}
static void LwArTravelGiven(const LwArEntry* e, const float* out)
{
    if (LwArTravelUnit(e) && LwArCounting()) g_lwAr.c.travelGiven += sqrt((double)(out[0] * out[0] + out[1] * out[1])) * (double)e->scale;
}

/* One entry's kick; 1 when it was removed. */
static int LwArKickEntry(int k, const LwArKickCtx* x)
{
    LwArEntry* e = &g_lwAr.e[k];
    LwArSumWorkers(e);
    int ok = LwArResolves(e);
    if (!ok) g_lwAr.c.leaves[LW_AR_R_GONE]++;
    if (e->state == LW_AR_CARRY || !ok) { LwArDrop(k, ok); return 1; }
    e->animAcc += x->dt;
    if (e->state == LW_AR_ACTIVE && e->enterKick != g_lwAr.kick) LwArGuard(e, x);
    if (e->pending && g_lwAr.kick > e->runKick + 1) { e->pending = 0; g_lwAr.c.staleTakes++; }
    e->run = 0;
    if (g_lwAr.set.dry) { e->run = 1; e->useDt = x->dt; if (e->state != LW_AR_ACTIVE) e->state = LW_AR_CARRY; }
    else LwArStep(e, x->dt);
    LwArPinStep(e, x->cam);
    LwArTravelPath(e);
    return 0;
}
static void LwArKickBody(float dt, int late)
{
    LwArCam cam; LwArRemote rv; LwArMotionMgr mm;
    LwArCamera(&cam); LwArRemotes(&rv);
    int haveMotion = LwArMotionRead(&mm);
    if (haveMotion && g_lwAr.set.period > 0 && !g_lwAr.draining)
    {
        LwArPick pk = {&cam, &rv, &mm, dt, late, g_lwAr.count};
        LwArFindCandidates(&pk);
    }
    LwArKickCtx x = {&cam, &rv, &mm, dt};
    for (int k = 0; k < g_lwAr.count; k++) if (LwArKickEntry(k, &x)) k--;
}

/* ---- DETOUR K: sm_kick exe 0x555BB0, game thread. No animation task of an earlier kick runs at its entry: each kick
 * follows the flush 0x335330 that waits the last one, and the late kick is waited at 0x1B06D8. ---- */
static int LwArKickOurs(uintptr_t aw)
{
    if (!g_lwAr.hooked || !LwArGameThread()) return 0; /* the thread is known from the first living world tick */
    if (!LwArOnGameThread()) { g_lwAr.c.offThread++; return 0; }
    uintptr_t w = LwArGameWorld(), gaw = 0;
    if (!w || !g_lwAr.host || GetTickCount64() - g_lwAr.hostAt > 1000) { if (g_lwAr.count) LwArClear(); return 0; }
    return EvpPtr(w + 0x230050, &gaw) && gaw == aw;
}
static void LwArKick(uintptr_t aw, const uint32_t* deps, uint32_t ndeps, float dt)
{
    if (!LwArKickOurs(aw)) { g_lwAr.kickOrig(aw, deps, ndeps, dt); return; }
    uint64_t t0 = __builtin_ia32_rdtsc();
    uint8_t path = 1;
    SafeRead(LwArGameWorld() + 0x230968, &path, 1); /* 1 on the camera path (0x33404F), 0 on the late one */
    int late = path == 0;
    g_lwAr.counting = GetTickCount64() >= g_lwAr.countFrom;
    g_lwAr.kick++; g_lwAr.c.kicks[late]++; g_lwAr.frameKicks++;
    LwArApplySwitches();
    LwArKickBody(dt, late);
    LwArPublishHashes();
    uint64_t t = __builtin_ia32_rdtsc() - t0;
    if (LwArCounting()) { g_lwAr.c.kTicks += t; g_lwAr.c.kCount++; if (t > g_lwAr.c.kMax) g_lwAr.c.kMax = t; }
    g_lwAr.kickOrig(aw, deps, ndeps, dt);
}

/* ---- DETOURS U and B: sm_update exe 0x5530C0, dynamic bones 0x54DAC0; workers and the game thread ---- */
static LwArEntry* LwArPlayerEntry(uintptr_t p)
{
    int k = LwArByPlayer(p);
    if (k < 0) return NULL;
    LwArEntry* e = &g_lwAr.e[k];
    if (!LwArKeys(e, p)) { e->stale++; return NULL; }
    return e;
}
static void LwArUpdate(uintptr_t p, float dt, uintptr_t out)
{
    LwArEntry* e = LwArPlayerEntry(p);
    if (!e) { g_lwAr.updOrig(p, dt, out); return; }
    if (!e->run) { e->updSkip++; return; }
    uint32_t ev0 = 0, ev1 = 0; /* the event buffer's used bytes (u32 +0, 0x553D4F): events per run */
    int ev = out && !SafeRead(out, &ev0, 4);
    uint64_t t = __builtin_ia32_rdtsc();
    g_lwAr.updOrig(p, g_lwAr.set.dry ? dt : e->useDt, out);
    e->updTicks += __builtin_ia32_rdtsc() - t; e->updRun++;
    if (ev && !SafeRead(out, &ev1, 4) && ev1 >= ev0) e->evBytes += ev1 - ev0;
}
static void LwArBones(uintptr_t p, float dt)
{
    LwArEntry* e = LwArPlayerEntry(p);
    if (!e) { g_lwAr.bonesOrig(p, dt); return; }
    if (!e->run) { e->bonesSkip++; return; }
    uint64_t t = __builtin_ia32_rdtsc();
    g_lwAr.bonesOrig(p, g_lwAr.set.dry ? dt : e->useDt);
    e->bonesTicks += __builtin_ia32_rdtsc() - t; e->bonesRun++;
}

/* ---- DETOURS M and G: motion_step game.dll 0x59FED0, root delta getter exe 0x2029D0; game thread ----
 * M marks the step of an entry; G acts only inside it, for that handle, once (a local step calls the getter once,
 * 0x59F0BA). Readers outside a step (seater, transport, remote step) get the stock getter. */
static void LwArStepSkip(LwArEntry* e, uintptr_t m, uint32_t i, uint32_t handle, float dt)
{
    float* x = (float*)(*(uintptr_t*)(m + 0x48C0) + 0x1Cull * i + 0x10); /* extra velocity, a per-frame push */
    e->motionAcc += dt;
    if (e->motionAcc > g_lwAr.c.motionAccMax) g_lwAr.c.motionAccMax = e->motionAcc;
    int take = (e->pending && g_lwAr.kick > e->runKick) || e->state == LW_AR_CARRY;
    if (!take) { for (int a = 0; a < 3; a++) { e->push[a] += x[a] * dt; x[a] = 0; } g_lwAr.c.stepSkipped++; return; }
    for (int a = 0; a < 3; a++) { x[a] += e->motionAcc > 0 ? e->push[a] / e->motionAcc : 0.0f; e->push[a] = 0; }
    g_lwAr.inStep = 1; g_lwAr.stepHandle = handle;
    g_lwAr.motionOrig(m, i, e->motionAcc);
    g_lwAr.inStep = 0; e->motionAcc = 0; g_lwAr.c.stepTaken++;
}
static void LwArMotion(uintptr_t m, uint32_t i, float dt)
{
    if (!g_lwAr.count) { g_lwAr.motionOrig(m, i, dt); return; }
    if (!LwArOnGameThread()) { g_lwAr.c.motionOff++; g_lwAr.motionOrig(m, i, dt); return; }
    uintptr_t descs = 0, d = 0; uint32_t handle = 0;
    int k = EvpPtr(m + 0x48B8, &descs) && EvpPtr(descs + 8ull * i, &d) && !SafeRead(d + 0xC, &handle, 4) ? LwArByHandle(handle) : -1;
    if (k < 0 || !g_lwAr.set.step || g_lwAr.set.dry)
    {
        g_lwAr.inStep = k >= 0; g_lwAr.stepHandle = handle;
        g_lwAr.motionOrig(m, i, dt); g_lwAr.inStep = 0;
        return;
    }
    LwArStepSkip(&g_lwAr.e[k], m, i, handle, dt);
}
static void LwArTake(LwArEntry* e, uint32_t handle, float dtM)
{
    float d[3];
    g_lwAr.getOrig(d, handle); /* the run that wrote it ended at the flush before this kick */
    for (int a = 0; a < 3; a++) { e->spread[a] += d[a]; e->taken[a] += d[a]; }
    e->spreadT = g_lwAr.set.step ? 0 : e->state == LW_AR_HANDBACK ? 2 * dtM : fmaxf(e->useDt, dtM);
    e->pending = 0; g_lwAr.c.takes++;
}
static float* LwArGetter(float* out, uint32_t handle)
{
    int k;
    if (!g_lwAr.inStep || handle != g_lwAr.stepHandle || !LwArOnGameThread() || (k = LwArByHandle(handle)) < 0) return g_lwAr.getOrig(out, handle);
    g_lwAr.inStep = 0;
    LwArEntry* e = &g_lwAr.e[k];
    if (g_lwAr.set.dry) { float* r = g_lwAr.getOrig(out, handle); LwArTravelGiven(e, out); return r; }
    float dtM = LwArMotionDt();
    if (e->state != LW_AR_CARRY && e->pending && g_lwAr.kick > e->runKick) LwArTake(e, handle, dtM);
    float f = e->state == LW_AR_CARRY || e->spreadT <= dtM ? 1.0f : dtM / e->spreadT;
    for (int a = 0; a < 3; a++) { out[a] = e->spread[a] * f; e->spread[a] -= out[a]; e->given[a] += out[a]; }
    e->spreadT = fmaxf(0.0f, e->spreadT - dtM);
    if (out[0] != 0 || out[1] != 0 || out[2] != 0) e->gaveKick = g_lwAr.kick;
    LwArTravelGiven(e, out);
    g_lwAr.c.handouts++;
    return out;
}

/* ---- DETOUR C: controller pass exe 0x556350, game thread. A held player is kept out of the pass by S+0x378 = 0
 * for the call. Only players in the pass's own lists are written (list 1 AW+0x90/+0x98, list 2 AW+0xA8/+0xB0,
 * 0x5565DE, 0x55689F): a unit destroyed after the kick is not listed, so nothing is written into freed memory. The
 * pass waits its own tasks before it returns (0x5577B0), so the restore races with none of them. ---- */
typedef struct { uintptr_t s; uint32_t was; } LwArSaved;
static int LwArCtlList(uint32_t cnt, uintptr_t arr, LwArSaved* saved, int n)
{
    for (uint32_t i = 0; i < cnt && n < LW_AR_MAX; i++)
    {
        uintptr_t p = *(volatile uintptr_t*)(arr + 8ull * i);
        int k = p ? LwArByPlayer(p) : -1;
        if (k < 0) continue;
        LwArEntry* e = &g_lwAr.e[k];
        if (e->run || e->state != LW_AR_ACTIVE || !LwArKeys(e, p)) continue;
        saved[n].s = e->blender; saved[n].was = *(volatile uint32_t*)(e->blender + 0x378);
        *(volatile uint32_t*)(e->blender + 0x378) = 0; n++;
    }
    return n;
}
static int LwArCtlHold(uintptr_t aw, LwArSaved* saved)
{
    int n = LwArCtlList(*(volatile uint32_t*)(aw + 0x90), *(volatile uintptr_t*)(aw + 0x98), saved, 0);
    return LwArCtlList(*(volatile uint32_t*)(aw + 0xA8), *(volatile uintptr_t*)(aw + 0xB0), saved, n);
}
static void LwArCtlPass(uintptr_t aw, float dt, uintptr_t out)
{
    static LwArSaved saved[LW_AR_MAX];
    int n = 0;
    if (g_lwAr.set.ctl && !g_lwAr.set.dry && g_lwAr.count && LwArOnGameThread() && aw == LwArGameAw()) { n = LwArCtlHold(aw, saved); g_lwAr.c.ctlPasses++; }
    g_lwAr.ctlOrig(aw, dt, out);
    for (int i = n - 1; i >= 0; i--) *(volatile uint32_t*)(saved[i].s + 0x378) = saved[i].was; /* reverse: a blender listed twice gets its first value */
    g_lwAr.c.ctlBracketed += (uint32_t)n;
}

/* ---- DETOUR P: unit set local position exe 0x1FC720, game thread. Command 0xC stores the position with no compare
 * and marks the node and the unit dirty (0x12F2E8), so a write equal to the live node only costs the transform. ---- */
static void LwArSetPos(uint32_t handle, uint32_t node, const float* p)
{
    int k;
    if (!g_lwAr.set.post || g_lwAr.set.dry || node != 0 || !LwArOnGameThread() || (k = LwArByHandle(handle)) < 0) { g_lwAr.setOrig(handle, node, p); return; }
    LwArEntry* e = &g_lwAr.e[k];
    uintptr_t nodes = 0; float live[3]; uint32_t h = 0;
    int held = !e->run && e->state == LW_AR_ACTIVE && !e->postOff;
    if (held && !SafeRead(e->unit + 8, &h, 4) && h == handle && EvpPtr(e->unit + 0x60 + 0x20, &nodes) && !SafeRead(nodes + 0x24, live, 12))
    {
        if (e->havePos && memcmp(live, e->lastPos, 12)) { e->postOff = 1; g_lwAr.c.postOther++; } /* another writer moved node 0 */
        else if (!memcmp(live, p, 12)) { g_lwAr.c.posDropped++; return; }
    }
    memcpy(e->lastPos, p, 12); e->havePos = 1; g_lwAr.c.posPassed++;
    g_lwAr.setOrig(handle, node, p);
}

/* ---- DETOUR X: the unit box exe 0x2C4AE0 (vt+0x238), workers. Only the bone LOD job's call (return 0x5631CA) of a
 * pinned entry is skipped: min > max sends the job to 0x563254 (0x5631D6..0x563205), which reads no other field. ---- */
static uintptr_t LwArBox(uintptr_t u, uint8_t* out, uint8_t flag)
{
    int k;
    if (!g_lwAr.pinFirst || (uintptr_t)__builtin_return_address(0) != g_lwAr.exe + LW_AR_BOX_RET || (k = LwArByUnit(u)) < 0 ||
        !g_lwAr.e[k].pinned || *(volatile uint32_t*)(u + 8) != g_lwAr.e[k].handle) return g_lwAr.boxOrig(u, out, flag);
    static const float minMax[6] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    memcpy(out + 0x40, minMax, sizeof minMax);
    g_lwAr.e[k].boxSkip++;
    return (uintptr_t)out;
}

/* ---- DETOUR Z: update_scene_graphs exe 0x2DF170 from the engine world update (rcx W+0x230228), game thread. At
 * its entry the unit's dirty word 0 holds every node write of the frame so far (0x596600 clears it at its end). ---- */
static void LwArDirtyCensus(void)
{
    for (int k = 0; k < g_lwAr.count; k++)
    {
        LwArEntry* e = &g_lwAr.e[k];
        uintptr_t words = 0; uint32_t w0 = 0;
        if (e->run || e->state != LW_AR_ACTIVE || !LwArResolves(e) || !EvpPtr(e->unit + 0xB0, &words) || SafeRead(words, &w0, 4)) continue;
        int moving = e->gaveKick == g_lwAr.kick;
        g_lwAr.c.censusAll[moving]++;
        g_lwAr.c.censusDirty[moving] += w0 & 1;
    }
    g_lwAr.c.census++;
}
/* Every 30 s: the blenders by owner (KB world-update-exe-scene: BW = [W+0x230868], count +0x80, S* [+0x88]; the unit
 * [S+0x210], 0x5631A5; its link parent [u+0x1D0], 0x2BD9E0). */
static int LwArOwnerKind(uintptr_t u)
{
    uintptr_t vt = 0, at = u; uint32_t h = 0;
    if (SafeRead(u, &vt, 8) || SafeRead(u + 8, &h, 4)) return 3;
    if (LwArByUnit(u) >= 0) return 0;
    if (LwArHelldiver(h)) return 2;
    for (int depth = 0; depth < 4 && vt == g_lwAr.exe + LW_AR_UNIT_VT; depth++)
    {
        if (!EvpPtr(at + 0x1D0, &at) || SafeRead(at, &vt, 8)) break;
        if (LwArByUnit(at) >= 0) return 1;
    }
    return 3;
}
static void LwArBlenderCensus(void)
{
    static const char* const kind[4] = {"entry", "child of an entry", "Helldiver", "other"};
    uintptr_t w = LwArGameWorld(), bw = 0, arr = 0; uint32_t n = 0, by[4][2] = {{0}};
    if (!w || !EvpPtr(w + 0x230868, &bw) || SafeRead(bw + 0x80, &n, 4) || !EvpPtr(bw + 0x88, &arr)) return;
    for (uint32_t i = 0; i < n && i < 8192; i++)
    {
        uintptr_t s = 0, u = 0, vt = 0, p = 0;
        if (!EvpPtr(arr + 8ull * i, &s) || !EvpPtr(s + 0x210, &u)) continue;
        int withPlayer = !SafeRead(u, &vt, 8) && vt == g_lwAr.exe + LW_AR_UNIT_VT && EvpPtr(u + 0x178, &p);
        by[LwArOwnerKind(u)][withPlayer]++;
    }
    for (int k = 0; k < 4; k++)
        Note("living world: animrate census: %u blenders, %s: %u with a player, %u without", n, kind[k], by[k][1], by[k][0]);
}
static void LwArCensus(uintptr_t a, uintptr_t b, uint32_t n, uintptr_t d)
{
    static ULONGLONG blendersAt;
    uintptr_t w = LwArGameWorld();
    if (g_lwAr.set.census && w && a == w + 0x230228 && LwArOnGameThread())
    {
        LwArDirtyCensus();
        if (GetTickCount64() - blendersAt >= 30000) { blendersAt = GetTickCount64(); LwArBlenderCensus(); }
    }
    g_lwAr.sgOrig(a, b, n, d);
}
