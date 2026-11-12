/* Test instrument (HELLFORGE_LW_TASKS=1; KB physics-step-task-graph): a timeline of the Havok task queue during the
 * game world's physics step. The pop (exe 0x8A9A60) frees its queue node through 0x8144B0 before the task runs, so
 * two hooks see every task and its class with no patch inside a function. Command "tasks N" samples N steps. */
#define LW_TL_THREADS 48
#define LW_TL_RING 8192u                    /* 48 x 8192 x 32 bytes = 12.6 MB, fixed: a fight step makes about 6,000 records */
#define LW_TL_STEPS 64
#define LW_TL_DEPTH 8
#define LW_TL_WINDOWS 4                     /* collide waits kept per step: one a sub-step, at most three sub-steps */
#define LW_TL_EVENTS 65536
#define LW_TL_CLASSES 64
#define LW_TL_CHAIN 256
#define LW_TL_NOINLINE __attribute__((noinline))    /* a detour reads its own return address */
#define LW_TL_QUEUE_VT 0x16A1B48u           /* +0x58 0x8AA280, +0x60 0x8AA260, +0x78 0x8AA2D0, +0xB8 0x8AA2F0 */
enum { LW_TL_S_COLLIDE = 1, LW_TL_S_PARTICLES = 2 };   /* own ids for the two calls with one caller each */

typedef struct { uint64_t t0, t1, child; uint32_t cls; uint16_t step; uint8_t flags, depth; } LwTlRec;
typedef struct {
    volatile uint32_t n; uint32_t polls, lost, over, id; int depth, inWait;
    uintptr_t cur[LW_TL_DEPTH]; uint64_t at[LW_TL_DEPTH], child[LW_TL_DEPTH], pollTicks; uint16_t step[LW_TL_DEPTH];
    LwTlRec ring[LW_TL_RING];
} LwTlThread;
typedef struct { uint64_t a, b; } LwTlSpan;
typedef struct {
    uint64_t t0, t1, replay[2], build, wait, waitSpin, particles, particlesWait, avoidWait, dtor;
    LwTlSpan window[LW_TL_WINDOWS]; uint32_t waits, builds, marks; LONG pushes, kicks, kicked;
} LwTlStep;
typedef struct { uint64_t t; uint32_t cls; int8_t d; uint8_t th; } LwTlEvent;
typedef struct { uint32_t cls, runs; uint64_t total, max, game, alone; } LwTlClass;
typedef uintptr_t (*LwTlFn1)(uintptr_t a);
typedef uintptr_t (*LwTlFn2)(uintptr_t a, uintptr_t b);
typedef uintptr_t (*LwTlFn4)(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);
typedef uintptr_t (*LwTlStepFn)(uint32_t world, uintptr_t args);
/* The job start 0xA40F0 as the queue's submit calls it at 0x8AA1F7-0x8AA238: r9d 0, then a dword 3, the count as a
 * dword and a qword 0 on the stack. It copies the argument array (0xA4250-0xA4272), so the array may be a local. */
typedef uintptr_t (*LwTlKickFn)(uintptr_t job, uintptr_t* args, const char* name, uint32_t zero, uint32_t kind, uint32_t count, uintptr_t last);

static struct {
    volatile LONG armed, step, threads, kick, pushes, kicks, kicked;
    int on, total, idle; DWORD game; uintptr_t exe, world; LwTlStep* volatile cur; LwTlKickFn kickFn;
    void *popOrig, *freeOrig, *waitOrig, *stepOrig, *replayOrig, *collideOrig, *particlesOrig, *dtorOrig, *pushOrig, *bpOrig;
    uint64_t tsc; LARGE_INTEGER qpc;
    struct { uint32_t body, unit, hits; } mark[8]; uint32_t marks;
    LwTlStep steps[LW_TL_STEPS];
    LwTlThread th[LW_TL_THREADS];
} g_lwTl;
static LwTlEvent g_lwTlEvents[LW_TL_EVENTS];
/* Task classes (vtable RVAs) that "tasks kick 2" starts jobs for; 0 ends the list. Empty: mode 2 starts none. */
static const uint32_t kLwTlKickClasses[] = { 0x14BE078u, 0 };   /* hknpProcessNarrowPhasePairsTask: new pairs (KB fight-new-pairs-narrow-phase) */
static _Thread_local LwTlThread* t_lwTl;    /* claimed once a thread; 1 = no ring left for this thread */

static LwTlThread* LwTlMine(void)
{
    LwTlThread* t = t_lwTl;
    if (t != NULL) return (uintptr_t)t > 1 ? t : NULL;
    LONG i = InterlockedIncrement(&g_lwTl.threads) - 1;
    t = i >= 0 && i < LW_TL_THREADS ? &g_lwTl.th[i] : (LwTlThread*)(uintptr_t)1;
    t_lwTl = t;
    if ((uintptr_t)t <= 1) return NULL;
    t->id = GetCurrentThreadId();
    return t;
}

/* 0x8A9A60(Q) -> al: 1 when it ran a task. The record is written before the count, and the count is read once, so
 * the print never reads half a record and a late writer after a reset stays inside the ring. */
static LW_TL_NOINLINE uintptr_t LwTlPop(uintptr_t q)
{
    if (!g_lwTl.armed) return ((LwTlFn1)g_lwTl.popOrig)(q);
    LwTlThread* t = LwTlMine();
    if (t == NULL) return ((LwTlFn1)g_lwTl.popOrig)(q);
    if (t->depth < 0 || t->depth >= LW_TL_DEPTH)
    {   /* too deep to record: the free hook must not write into the level above while this pop runs */
        t->over++;
        uintptr_t deep = ((LwTlFn1)g_lwTl.popOrig)(q);
        t->over--;
        return deep;
    }
    int d = t->depth++;
    t->cur[d] = 0; t->child[d] = 0;
    uint64_t a = __builtin_ia32_rdtsc();
    uintptr_t r = ((LwTlFn1)g_lwTl.popOrig)(q);
    uint64_t b = __builtin_ia32_rdtsc();
    t->depth--;
    if ((uint8_t)r == 0 || t->cur[d] == 0)
    {
        if (d == 0) t->pollTicks += b - a;  /* an empty pop: the wait's spin, or a job that ends */
        t->polls++;
        return r;
    }
    uint32_t n = t->n;
    if (n < LW_TL_RING)
    {
        LwTlRec* rec = &t->ring[n];
        rec->t0 = t->at[d]; rec->t1 = b; rec->child = t->child[d]; rec->cls = (uint32_t)t->cur[d]; rec->step = t->step[d];
        rec->flags = (uint8_t)((t->id == g_lwTl.game ? 1 : 0) | (t->inWait ? 2 : 0)); rec->depth = (uint8_t)d;
        t->n = n + 1;
    }
    else t->lost++;
    if (d > 0) t->child[d - 1] += b - t->at[d];   /* a task that waits runs other tasks inside itself */
    return r;
}

/* 0x8144B0(pool, node), from the pop's call at 0x8A9B39 only: the node still holds the handle (read at 0x8A9B35), the
 * handle its task (+0x48, read at 0x8A9B60) and the task its vtable (0x8A9BB8). The task starts right after. */
static LW_TL_NOINLINE uintptr_t LwTlFree(uintptr_t pool, uintptr_t node)
{
    if (g_lwTl.armed && (uintptr_t)__builtin_return_address(0) == g_lwTl.exe + 0x8A9B3Eu)
    {
        LwTlThread* t = t_lwTl;
        if ((uintptr_t)t > 1 && t->depth > 0 && t->depth <= LW_TL_DEPTH && t->over == 0)
        {
            int d = t->depth - 1;
            uintptr_t h = *(volatile uintptr_t*)(node + 8), task = *(volatile uintptr_t*)(h + 0x48);
            t->cur[d] = task ? *(volatile uintptr_t*)task - g_lwTl.exe : 1;   /* 1 = a handle with no task */
            t->step[d] = (uint16_t)g_lwTl.step;
            t->at[d] = __builtin_ia32_rdtsc();
        }
    }
    return ((LwTlFn2)g_lwTl.freeOrig)(pool, node);
}

/* Game thread, inside a sampled step only: what the step's own calls cost, by return address or own id. */
static void LwTlStepNote(uint32_t site, uint64_t a, uint64_t b, uint64_t spin)
{
    LwTlStep* s = g_lwTl.cur;
    if (s == NULL || GetCurrentThreadId() != g_lwTl.game) return;
    switch (site)
    {
    case 0x79EF6Eu: s->replay[0] += b - a; break;
    case 0x79F133u: s->replay[1] += b - a; break;
    case LW_TL_S_COLLIDE: s->build += b - a; s->builds++; break;
    case LW_TL_S_PARTICLES: s->particles += b - a; break;
    case 0x79EFC3u:
        if (s->waits < LW_TL_WINDOWS) { s->window[s->waits].a = a; s->window[s->waits].b = b; }
        s->waits++; s->wait += b - a; s->waitSpin += spin; break;
    case 0x79F014u: s->particlesWait += b - a; break;
    case 0x79F1E3u: s->avoidWait += b - a; break;
    case 0x79F1F8u: s->dtor += b - a; break;
    default: break;
    }
}

/* 0x8AA260(Q, handle), the queue's vtable +0x60: the step's three waits call it at 0x79EFBF, 0x79F010 and 0x79F1E0. */
static LW_TL_NOINLINE uintptr_t LwTlWaitOne(uintptr_t q, uintptr_t h)
{
    LwTlThread* t = g_lwTl.armed && g_lwTl.cur != NULL && GetCurrentThreadId() == g_lwTl.game ? LwTlMine() : NULL;
    if (t == NULL) return ((LwTlFn2)g_lwTl.waitOrig)(q, h);
    uint32_t site = (uint32_t)((uintptr_t)__builtin_return_address(0) - g_lwTl.exe);
    t->inWait++;
    uint64_t a = __builtin_ia32_rdtsc(), p = t->pollTicks;
    uintptr_t r = ((LwTlFn2)g_lwTl.waitOrig)(q, h);
    t->inWait--;
    LwTlStepNote(site, a, __builtin_ia32_rdtsc(), t->pollTicks - p);
    return r;
}

static LW_TL_NOINLINE uintptr_t LwTlReplay(uintptr_t world, uintptr_t recorder)   /* 0x79E910(u32 world, recorder) */
{
    if (!g_lwTl.armed || g_lwTl.cur == NULL) return ((LwTlFn2)g_lwTl.replayOrig)(world, recorder);
    uint32_t site = (uint32_t)((uintptr_t)__builtin_return_address(0) - g_lwTl.exe);
    uint64_t a = __builtin_ia32_rdtsc();
    uintptr_t r = ((LwTlFn2)g_lwTl.replayOrig)(world, recorder);
    LwTlStepNote(site, a, __builtin_ia32_rdtsc(), 0);
    return r;
}

/* 0xD0CC90(W, step info, graph, queue): builds the collide graph; its one caller is the step, at 0x79EF93. */
static LW_TL_NOINLINE uintptr_t LwTlCollide(uintptr_t w, uintptr_t info, uintptr_t graph, uintptr_t q)
{
    if (!g_lwTl.armed || g_lwTl.cur == NULL) return ((LwTlFn4)g_lwTl.collideOrig)(w, info, graph, q);
    g_lwTl.world = w;
    uint64_t a = __builtin_ia32_rdtsc();
    uintptr_t r = ((LwTlFn4)g_lwTl.collideOrig)(w, info, graph, q);
    LwTlStepNote(LW_TL_S_COLLIDE, a, __builtin_ia32_rdtsc(), 0);
    return r;
}

/* 0xD0CC10(W, step info, graph): three arguments, r9 is left as it came and handed on with them. */
static LW_TL_NOINLINE uintptr_t LwTlParticles(uintptr_t w, uintptr_t info, uintptr_t graph, uintptr_t r9)
{
    if (!g_lwTl.armed || g_lwTl.cur == NULL) return ((LwTlFn4)g_lwTl.particlesOrig)(w, info, graph, r9);
    uint64_t a = __builtin_ia32_rdtsc();
    uintptr_t r = ((LwTlFn4)g_lwTl.particlesOrig)(w, info, graph, r9);
    LwTlStepNote(LW_TL_S_PARTICLES, a, __builtin_ia32_rdtsc(), 0);
    return r;
}

static LW_TL_NOINLINE uintptr_t LwTlGraphDtor(uintptr_t graph)   /* 0x9A0C90(G): 22 callers, timed for 0x79F1F3 only */
{
    if (!g_lwTl.armed || g_lwTl.cur == NULL || (uintptr_t)__builtin_return_address(0) != g_lwTl.exe + 0x79F1F8u)
        return ((LwTlFn1)g_lwTl.dtorOrig)(graph);
    uint64_t a = __builtin_ia32_rdtsc();
    uintptr_t r = ((LwTlFn1)g_lwTl.dtorOrig)(graph);
    LwTlStepNote(0x79F1F8u, a, __builtin_ia32_rdtsc(), 0);
    return r;
}

static int LwTlKickClass(uintptr_t h)
{
    uintptr_t task, vt;
    if (kLwTlKickClasses[0] == 0) return 0;
    task = *(volatile uintptr_t*)(h + 0x48);
    if (task == 0) return 0;
    vt = *(volatile uintptr_t*)task - g_lwTl.exe;
    for (int i = 0; i < 8 && kLwTlKickClasses[i] != 0; i++) if (kLwTlKickClasses[i] == vt) return 1;
    return 0;
}

/* 0x8AA320(Q, h) adds [h+0x2C] & 0xFFFFFF nodes and starts no job, so a task released by a finished one is run by
 * the releasing thread and the waiting one. The lever starts jobs for the copies beyond the first, as the submit
 * does (0x8AA1B4-0x8AA238): [Q+0x0C] counts jobs started and not yet begun (taken off at 0x8A9A38) and is kept at
 * or under [Q+0x14] + 1. The submit's own pushes (return 0x8AA150) are left alone: it starts its jobs itself. */
static LW_TL_NOINLINE uintptr_t LwTlPush(uintptr_t q, uintptr_t h)
{
    LONG mode = g_lwTl.kick;
    if (!mode && !g_lwTl.armed) return ((LwTlFn2)g_lwTl.pushOrig)(q, h);
    uint32_t n = *(volatile uint32_t*)(h + 0x2C) & 0xFFFFFFu;
    uintptr_t ret = (uintptr_t)__builtin_return_address(0) - g_lwTl.exe;
    int start = n >= 2 && ret != 0x8AA150u && g_lwTl.kickFn != NULL && (mode == 1 || (mode == 2 && LwTlKickClass(h)));
    uintptr_t r = ((LwTlFn2)g_lwTl.pushOrig)(q, h);   /* h may be run and freed from here on */
    if (n >= 2) InterlockedIncrement(&g_lwTl.pushes);
    if (!start || *(volatile uintptr_t*)q != g_lwTl.exe + LW_TL_QUEUE_VT) return r;
    int most = *(volatile int*)(q + 0x14) + 1;
    if (most < 1 || most > 64) return r;
    int want = (int)(n - 1 < (uint32_t)most ? n - 1 : (uint32_t)most) - (int)*(volatile LONG*)(q + 0x0C);
    if (want <= 0) return r;
    if (want > 64) want = 64;
    uintptr_t args[64];
    for (int i = 0; i < want; i++) args[i] = q;
    InterlockedExchangeAdd((volatile LONG*)(q + 0x0C), want);
    g_lwTl.kickFn(g_lwTl.exe + 0x8A9A20u, args, (const char*)(g_lwTl.exe + 0x16A1A88u), 0, 3, (uint32_t)want, 0);
    InterlockedIncrement(&g_lwTl.kicks); InterlockedExchangeAdd(&g_lwTl.kicked, want);
    return r;
}

/* 0xD40550(bp, ids, n, flags), the broad phase's vtable +0x38: body ids marked dirty. For the direct velocity set's
 * call (0xDD3413) the bodies in layer 1 are kept, since that layer has 38,858 leaves and is rebuilt when one is
 * marked. The body array and the layer field are the function's own reads (0xD4057C-0xD4058C, 0xD406CE). */
static LW_TL_NOINLINE uintptr_t LwTlBpUpdate(uintptr_t bp, uintptr_t ids, uintptr_t n, uintptr_t flags)
{
    LwTlStep* s = g_lwTl.armed ? g_lwTl.cur : NULL;
    if (s != NULL && (uintptr_t)__builtin_return_address(0) == g_lwTl.exe + 0xDD3416u && GetCurrentThreadId() == g_lwTl.game)
    {
        uintptr_t w = 0, set = 0, bodies = 0;
        uint32_t count = (uint32_t)n < 64u ? (uint32_t)n : 64u;
        if (SafeRead(bp + 8, &w, 8) || SafeRead(w + 0x130, &set, 8) || SafeRead(set + 0x10, &bodies, 8)) count = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t id = 0, layer = 0xFFFFFFFFu, unit = 0, k;
            if (SafeRead(ids + 4ull * i, &id, 4) || SafeRead(bodies + 0xA0ull * (id & 0xFFFFFFu) + 0x74, &layer, 4)) continue;
            if (layer == 0xFFFFFFFFu || (layer & 0x1F) != 1) continue;
            s->marks++;
            for (k = 0; k < g_lwTl.marks && g_lwTl.mark[k].body != (id & 0xFFFFFFu); k++) {}
            if (k >= 8) continue;
            if (k == g_lwTl.marks)
            {
                SafeRead(bodies + 0xA0ull * (id & 0xFFFFFFu) + 0x94, &unit, 4);
                g_lwTl.mark[k].body = id & 0xFFFFFFu; g_lwTl.mark[k].unit = unit; g_lwTl.mark[k].hits = 0; g_lwTl.marks++;
            }
            g_lwTl.mark[k].hits++;
        }
    }
    return ((LwTlFn4)g_lwTl.bpOrig)(bp, ids, n, flags);
}

static int LwTlEventOrder(const void* a, const void* b)
{
    const LwTlEvent* x = (const LwTlEvent*)a; const LwTlEvent* y = (const LwTlEvent*)b;
    if (x->t != y->t) return x->t < y->t ? -1 : 1;
    return (int)x->d - (int)y->d;   /* an end before a start: back-to-back tasks do not count as two at once */
}
static int LwTlClassOrder(const void* a, const void* b)
{
    const LwTlClass* x = (const LwTlClass*)a; const LwTlClass* y = (const LwTlClass*)b;
    return x->total == y->total ? 0 : x->total > y->total ? -1 : 1;
}
static LwTlClass* LwTlClassOf(LwTlClass* table, int* count, uint32_t cls)
{
    for (int i = 0; i < *count; i++) if (table[i].cls == cls) return &table[i];
    if (*count >= LW_TL_CLASSES) return &table[LW_TL_CLASSES - 1];   /* the table is full: the last class takes the rest */
    table[*count].cls = cls;
    return &table[(*count)++];
}

/* Game thread, with armed at 0. Workers may still end a pop begun while armed: the rings are only read here, each up
 * to the count read once. Top-level records cut to the collide waits give the time with 0, 1 and more tasks running. */
static void LwTlPrint(void)
{
    static LwTlClass cls[LW_TL_CLASSES];
    static struct { uint32_t cls; uint64_t ticks; } chain[LW_TL_CHAIN];
    int steps = g_lwTl.step < LW_TL_STEPS ? (int)g_lwTl.step : LW_TL_STEPS, nc = 0, chainN = 0, chainStep = -1;
    int threads = g_lwTl.threads < LW_TL_THREADS ? (int)g_lwTl.threads : LW_TL_THREADS;
    uint32_t counts[LW_TL_THREADS], lost = 0, dropped = 0, ran = 0;
    uint64_t none = 0, one = 0, many = 0, gameRan = 0, tsc = __builtin_ia32_rdtsc();
    LwTlStep sum; LARGE_INTEGER q, f;
    char line[900]; size_t at = 0;
    QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
    double perMs = q.QuadPart > g_lwTl.qpc.QuadPart ? (double)(tsc - g_lwTl.tsc) / ((double)(q.QuadPart - g_lwTl.qpc.QuadPart) * 1000.0 / (double)f.QuadPart) : 0;
    if (steps <= 0 || perMs <= 0) { Note("living world: test: physics tasks: nothing sampled (%d steps)", steps); return; }
    double div = perMs * steps;
    memset(cls, 0, sizeof cls); memset(&sum, 0, sizeof sum);
    for (int s = steps - 1; s >= 0 && chainStep < 0; s--) if (g_lwTl.steps[s].waits > 0) chainStep = s;

    for (int i = 0; i < threads; i++)
    {
        const LwTlThread* t = &g_lwTl.th[i];
        counts[i] = t->n < LW_TL_RING ? t->n : LW_TL_RING; lost += t->lost;
        for (uint32_t k = 0; k < counts[i]; k++)
        {
            const LwTlRec* r = &t->ring[k];
            if (r->step >= steps || r->t0 < g_lwTl.steps[r->step].t0 || r->t1 < r->t0) continue;   /* older than this sampling */
            uint64_t self = r->t1 - r->t0 > r->child ? r->t1 - r->t0 - r->child : 0;
            LwTlClass* c = LwTlClassOf(cls, &nc, r->cls);
            c->runs++; c->total += self; if (self > c->max) c->max = self;
            if (r->flags & 1) c->game += self;
        }
    }

    for (int s = 0; s < steps; s++)
    {
        const LwTlStep* st = &g_lwTl.steps[s];
        sum.t1 += st->t1 - st->t0; sum.replay[0] += st->replay[0]; sum.replay[1] += st->replay[1]; sum.build += st->build;
        sum.wait += st->wait; sum.waitSpin += st->waitSpin; sum.particles += st->particles; sum.particlesWait += st->particlesWait;
        sum.avoidWait += st->avoidWait; sum.dtor += st->dtor; sum.builds += st->builds; sum.marks += st->marks;
        sum.pushes += st->pushes; sum.kicks += st->kicks; sum.kicked += st->kicked;
        for (uint32_t w = 0; w < st->waits && w < LW_TL_WINDOWS; w++)
        {
            uint64_t w0 = st->window[w].a, w1 = st->window[w].b, mask = 0, prev = w0;
            uint32_t ne = 0, act[64]; int na = 0, running = 0;
            for (int i = 0; i < threads; i++)
                for (uint32_t k = 0; k < counts[i]; k++)
                {
                    const LwTlRec* r = &g_lwTl.th[i].ring[k];
                    if (r->step != s || r->depth != 0) continue;
                    uint64_t a = r->t0 > w0 ? r->t0 : w0, b = r->t1 < w1 ? r->t1 : w1;
                    if (b <= a) continue;
                    if (ne + 2 > LW_TL_EVENTS) { dropped++; continue; }
                    g_lwTlEvents[ne++] = (LwTlEvent){ a, r->cls, 1, (uint8_t)i };
                    g_lwTlEvents[ne++] = (LwTlEvent){ b, r->cls, -1, (uint8_t)i };
                    mask |= 1ull << i;
                    if (r->flags & 1) gameRan += b - a;
                }
            qsort(g_lwTlEvents, ne, sizeof g_lwTlEvents[0], LwTlEventOrder);
            for (uint32_t e = 0; e <= ne; e++)
            {
                uint64_t now = e < ne ? g_lwTlEvents[e].t : w1, dt = now > prev ? now - prev : 0;
                if (running == 0) none += dt;
                else if (running > 1) many += dt;
                else
                {
                    uint32_t c = na == 1 ? act[0] : 0;
                    one += dt; LwTlClassOf(cls, &nc, c)->alone += dt;
                    if (s == chainStep && w == 0 && dt > 0)
                    {
                        if (chainN > 0 && chain[chainN - 1].cls == c) chain[chainN - 1].ticks += dt;
                        else if (chainN < LW_TL_CHAIN) { chain[chainN].cls = c; chain[chainN++].ticks = dt; }
                    }
                }
                prev = now;
                if (e == ne) break;
                if (g_lwTlEvents[e].d > 0) { running++; if (na < 64) act[na++] = g_lwTlEvents[e].cls; }
                else
                {
                    running--;
                    for (int k = 0; k < na; k++) if (act[k] == g_lwTlEvents[e].cls) { act[k] = act[--na]; break; }
                }
            }
            for (int i = 0; i < threads; i++) ran += (uint32_t)(mask >> i & 1);
        }
    }

    uintptr_t w = 0, cons = 0; uint32_t nCons = 0, awake = 0, slots = 0;
    int bad = SafeRead(g_lwTl.exe + 0x27BA890u + 2u * 0xB0u, &w, 8) || w == 0;
    bad = bad || SafeRead(w + 0x488, &cons, 8) || SafeRead(cons + 0x20, &nCons, 4) || SafeRead(w + 0xE0, &awake, 4) || SafeRead(w + 0x4C, &slots, 4);
    Note("living world: test: physics tasks: step %.2f ms: replay %.2f, second replay %.2f, build %.2f, wait %.2f (game thread ran tasks %.2f, spun %.2f), "
        "particles %.2f (its wait %.2f), avoidance wait %.2f, graph dtor %.2f; constraints %u, body slots %u, awake %u%s; %d steps, %.2f sub-steps a step",
        sum.t1 / div, sum.replay[0] / div, sum.replay[1] / div, sum.build / div, sum.wait / div, gameRan / div, sum.waitSpin / div,
        (sum.particles + sum.particlesWait) / div, sum.particlesWait / div, sum.avoidWait / div, sum.dtor / div, (unsigned)nCons, (unsigned)slots, (unsigned)awake,
        bad ? " (the world could not be read)" : w != g_lwTl.world ? " (not the world the collide call got)" : "", steps, (double)sum.builds / steps);
    Note("living world: test: physics tasks: wait: no task running %.2f ms, one task %.2f ms, two or more %.2f ms; %.1f threads ran tasks; pushes of 2+ copies %.1f; "
        "lever %ld: %.1f job starts, %.1f jobs a step",
        none / div, one / div, many / div, (double)ran / steps, (double)sum.pushes / steps, g_lwTl.kick, (double)sum.kicks / steps, (double)sum.kicked / steps);
    qsort(cls, (size_t)nc, sizeof cls[0], LwTlClassOrder);
    for (int i = 0; i < nc && i < 24; i++)
    {
        if (cls[i].runs == 0 || cls[i].total / div < 0.001) continue;
        Note("living world: test: physics tasks: class %X runs %.1f, total %.2f ms, max %.2f, game thread %.0f%%, alone %.2f", (unsigned)cls[i].cls,
            (double)cls[i].runs / steps, cls[i].total / div, cls[i].max / perMs, cls[i].total ? 100.0 * cls[i].game / cls[i].total : 0.0, cls[i].alone / div);
    }
    for (int i = 0, shown = 0; i < chainN && shown < 24 && at < sizeof line - 40; i++)
    {
        if (chain[i].ticks / perMs < 0.01) continue;   /* slivers between two tasks hide the chain */
        at += (size_t)snprintf(line + at, sizeof line - at, "%s%X %.2f", shown++ ? " > " : "", (unsigned)chain[i].cls, chain[i].ticks / perMs);
    }
    line[at] = 0;
    Note("living world: test: physics tasks: alone chain (step %d of %d, its first wait, 0.01 ms and longer): %s", chainStep + 1, steps, at ? line : "none");
    at = 0;
    for (uint32_t i = 0; i < g_lwTl.marks && i < 8 && at < sizeof line - 60; i++)
        at += (size_t)snprintf(line + at, sizeof line - at, "%sbody %u unit 0x%08X (%u times)", i ? ", " : "", (unsigned)g_lwTl.mark[i].body, (unsigned)g_lwTl.mark[i].unit, (unsigned)g_lwTl.mark[i].hits);
    line[at] = 0;
    Note("living world: test: physics tasks: layer 1 bodies marked by 0xDD3416: %s; %.1f marks a step", at ? line : "none", (double)sum.marks / steps);
    if (lost || dropped || g_lwTl.threads > LW_TL_THREADS)
        Note("living world: test: physics tasks: incomplete: %u records did not fit their ring, %u did not fit the sweep, %ld threads had no ring",
            (unsigned)lost, (unsigned)dropped, g_lwTl.threads > LW_TL_THREADS ? g_lwTl.threads - LW_TL_THREADS : 0L);
}

/* 0x79EB10(u32 world, args): the physics step; world 2 is the game's. A call that builds no collide graph (the world
 * not stepped this frame) is not a sample; 2000 such calls in a row end the sampling. */
static LW_TL_NOINLINE uintptr_t LwTlStepHook(uint32_t world, uintptr_t args)
{
    LONG i = g_lwTl.step;
    if (world != 2 || g_lwTl.armed <= 0 || g_lwTl.cur != NULL) return ((LwTlStepFn)g_lwTl.stepOrig)(world, args);
    if (i < 0 || i >= LW_TL_STEPS || i >= g_lwTl.total) { InterlockedExchange(&g_lwTl.armed, 0); return ((LwTlStepFn)g_lwTl.stepOrig)(world, args); }
    LwTlStep* s = &g_lwTl.steps[i];
    memset(s, 0, sizeof *s);
    g_lwTl.game = GetCurrentThreadId();
    LONG pushes = g_lwTl.pushes, kicks = g_lwTl.kicks, kicked = g_lwTl.kicked;
    s->t0 = __builtin_ia32_rdtsc();
    g_lwTl.cur = s;
    uintptr_t r = ((LwTlStepFn)g_lwTl.stepOrig)(world, args);
    g_lwTl.cur = NULL;
    s->t1 = __builtin_ia32_rdtsc();
    s->pushes = g_lwTl.pushes - pushes; s->kicks = g_lwTl.kicks - kicks; s->kicked = g_lwTl.kicked - kicked;
    LONG left = g_lwTl.armed;
    if (left <= 0) return r;                                    /* "tasks off" came during the step */
    if (s->builds == 0)
    {
        if (++g_lwTl.idle < 2000) return r;
        InterlockedExchange(&g_lwTl.armed, 0);
        Note("living world: test: physics tasks: the world was not stepped in 2000 calls; sampling ended after %ld steps", i);
        if (i > 0) LwTlPrint();
        return r;
    }
    g_lwTl.idle = 0;
    InterlockedExchange(&g_lwTl.step, i + 1);
    InterlockedExchange(&g_lwTl.armed, left - 1);
    if (left - 1 == 0) LwTlPrint();
    return r;
}

/* The lever's facts, checked before it is switched on: the submit's push returns to 0x8AA150, the submit's job start
 * passes (job, array, name, 0, 3, count, 0) through [[exe+0x1B140D0]+0x88], and the job takes its start off Q+0x0C. */
static int LwTlKickReady(void)
{
    uintptr_t exe = g_lwTl.exe, api = 0, fn = 0; char name[22] = "";
    if (!ApiCodeMatches(exe + 0x8AA14Bu, "E8 D0 01 00 00 49 03 FD") ||
        !ApiCodeMatches(exe + 0x8AA219u, "C7 44 24 20 03 00 00 00 48 8B CB 45 33 C9") ||
        !ApiCodeMatches(exe + 0x8AA22Au, "48 8B 05 9F 9E 26 01 48 8D 0D E8 F7 FF FF FF 90 88 00 00 00") ||
        !ApiCodeMatches(exe + 0x8A9A2Bu, "49 8B D9 E8 ED CA F6 FF B8 FF FF FF FF F0 0F C1 43 0C") ||
        !ApiCodeMatches(exe + 0x8AA2F0u, "8B 41 14 FF C0 C3") ||
        !ApiCodeMatches(exe + 0xA40F0u, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 44 89 4C 24 20")) return 0;
    if (SafeRead(exe + 0x1B140D0u, &api, 8) || SafeRead(api + 0x88, &fn, 8) || fn != exe + 0xA40F0u) return 0;
    if (SafeRead(exe + 0x16A1A88u, name, sizeof name) || memcmp(name, "havok_task_queue_task", sizeof name) != 0) return 0;
    g_lwTl.kickFn = (LwTlKickFn)fn;
    return 1;
}

/* "tasks N" (1..64 steps), "tasks kick 0|1|2", "tasks off". The rings are emptied only here and only when nothing
 * is armed; a pop still ending from the sampling before cannot leave its ring, and its record is older than the
 * new first step, so the print passes it by. */
static void LwTasksCmd(const char* args)
{
    int n = 0;
    if (!g_lwTl.on) { Note("living world: test: physics tasks: not installed (HELLFORGE_LW_TASKS is not 1, or the code is not the reviewed one)"); return; }
    if (sscanf(args, "kick %d", &n) == 1)
    {
        if (n < 0 || n > 2) n = 0;
        if (n != 0 && g_lwTl.kickFn == NULL && !LwTlKickReady()) { Note("living world: test: physics tasks: the job start is not the reviewed code; the lever stays off"); return; }
        LONG was = InterlockedExchange(&g_lwTl.kick, n);
        Note("living world: test: physics tasks: job starts on release %d (was %ld; %ld starts of %ld jobs since the last change)", n, was,
            InterlockedExchange(&g_lwTl.kicks, 0), InterlockedExchange(&g_lwTl.kicked, 0));
        return;
    }
    if (strncmp(args, "off", 3) == 0)
    {
        LONG was = InterlockedExchange(&g_lwTl.armed, 0);
        Note("living world: test: physics tasks: sampling off (%ld steps were left, %ld sampled; the lever stays %ld)", was, g_lwTl.step, g_lwTl.kick);
        if (was > 0 && g_lwTl.step > 0 && g_lwTl.cur == NULL) LwTlPrint();
        return;
    }
    if (sscanf(args, "%d", &n) != 1 || n < 1 || n > LW_TL_STEPS) { Note("living world: test: physics tasks: tasks N (1..%d), tasks kick 0|1|2, tasks off", LW_TL_STEPS); return; }
    if (g_lwTl.armed > 0 || g_lwTl.cur != NULL) { Note("living world: test: physics tasks: still sampling (%ld steps left)", g_lwTl.armed); return; }
    for (int i = 0; i < LW_TL_THREADS; i++) { g_lwTl.th[i].n = 0; g_lwTl.th[i].lost = 0; }
    g_lwTl.marks = 0; g_lwTl.idle = 0; g_lwTl.total = n;
    g_lwTl.tsc = __builtin_ia32_rdtsc(); QueryPerformanceCounter(&g_lwTl.qpc);
    InterlockedExchange(&g_lwTl.step, 0);
    InterlockedExchange(&g_lwTl.armed, n);
    Note("living world: test: physics tasks: sampling %d steps (lever %ld)", n, g_lwTl.kick);
}

/* Every hooked entry and every call site whose return address the detours compare is pinned first; one that differs
 * leaves all of it out. Off, each detour costs a load and a branch (the push: two loads). */

/* Census of the new-pairs task (KB fight-new-pairs-narrow-phase): 0xE44800(task object, i) hands out one 16-byte item
 * a call; the item's entry X = [item] + 0x20 + u16 [item+8] has its kind at X+0x0B (0 new cache, 1 and 2 grid
 * deferrals, 3 reactivated) and its cache c = [X] + 0x20 + u16 [X+8] the cache type at c+8 (2 convex-convex,
 * 4 convex-composite, 5 composite-composite). The time from one claim to the thread's next is charged to the item. */
typedef uintptr_t (*LwPcClaimFn)(uintptr_t o, uintptr_t i);
static struct { void* orig; volatile LONG n[4][16]; volatile LONG64 ticks[4][16]; volatile LONG64 most[16]; uint64_t at; } g_lwPc;
static _Thread_local struct { uint64_t t; int kind, type, owner; } t_lwPc;
static struct
{
    volatile uintptr_t bodies; volatile uint32_t count; uint32_t dead[2][160]; volatile LONG deadN[2], deadAt;
    volatile LONG n[3][5][3], keyframed[3][5]; volatile LONG64 ticks[3][5];
} g_lwPo;
static void LwPcCharge(uint64_t now)
{
    if (t_lwPc.t == 0) return;
    LONG64 d = (LONG64)(now - t_lwPc.t);
    InterlockedIncrement(&g_lwPc.n[t_lwPc.kind][t_lwPc.type]); InterlockedExchangeAdd64(&g_lwPc.ticks[t_lwPc.kind][t_lwPc.type], d);
    if (t_lwPc.owner >= 0) InterlockedExchangeAdd64(&g_lwPo.ticks[t_lwPc.owner >> 3][t_lwPc.owner & 7], d);
    LONG64 m = g_lwPc.most[t_lwPc.type];
    while (d > m) { LONG64 was = InterlockedCompareExchange64(&g_lwPc.most[t_lwPc.type], d, m); if (was == m) break; m = was; }
    t_lwPc.t = 0;
}
/* Whose body a deferred cache (kind 1, types 6, 4, 2) holds (KB fight-new-pairs-narrow-phase, "Who owns the deferred
 * pairs' bodies"): its dynamic side (c+0 holds the static one when c+0xF bit 2 is clear), the body's unit at +0x94, and
 * that unit against the corpse service's ring and disposal list, copied on the game thread once a second. */
/* corpses by unit slot parity: "corpse freeze 2" freezes the even half only */
static const char* const kLwPoOwner[5] = {"corpse even", "other unit", "no unit", "not read", "corpse odd"};
static int LwPcOwner(uintptr_t c, int t)
{
    uint32_t ida = 0, idb = 0, flags = 0, check = 0, unit = 0, actor = 0; uint8_t f = 0;
    uintptr_t base = g_lwPo.bodies; int owner = 3, part = 0;
    if (!SafeRead(c, &idb, 4) && !SafeRead(c + 4, &ida, 4) && !SafeRead(c + 0x0F, &f, 1) && base)
    {
        uint32_t id = (f & 4) ? idb : ida, idx = id & 0xFFFFFFu;
        uintptr_t body = base + 0xA0u * (uintptr_t)idx;
        if (idx != 0xFFFFFFu && idx < g_lwPo.count && !SafeRead(body + 0x44, &flags, 4) && (flags & 3) && !SafeRead(body + 0x70, &check, 4) && check == id &&
            !SafeRead(body + 0x90, &actor, 4) && !SafeRead(body + 0x94, &unit, 4))
        {
            owner = unit == 0 ? 2 : 1;
            if (unit)
            {
                LONG b = g_lwPo.deadAt & 1, n = g_lwPo.deadN[b];
                for (LONG k = 0; k < n; k++) if (g_lwPo.dead[b][k] == unit) { owner = (unit & 1u) ? 4 : 0; break; }
            }
            part = (actor & 0x02000000u) ? 2 : ((actor >> 28) & 3u) == 3u ? 1 : 0; /* the ragdoll's own body, a bone, other actors */
            if (flags & 4) InterlockedIncrement(&g_lwPo.keyframed[t][owner]);
        }
    }
    InterlockedIncrement(&g_lwPo.n[t][owner][part]);
    return t * 8 + owner;
}
/* Game thread: the world's body array every frame, the dead units once a second. */
static void LwPoFrame(void)
{
    static ULONGLONG at; uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL), world = 0, bodies = 0, svc = 0; uint32_t count = 0;
    /* world 2's physics world (exe 0x784F12) and its bodies, 0xA0 bytes each (0xCE5A66-0xCE5A85) */
    if (!SafeRead(exe + 0x27BA9F0u, &world, 8) && world && !SafeRead(world + 0x38, &bodies, 8) && !SafeRead(world + 0x40, &count, 4)) { g_lwPo.count = count; g_lwPo.bodies = bodies; }
    else g_lwPo.bodies = 0;
    ULONGLONG now = GetTickCount64();
    if (now - at < 1000 || SafeRead(g_lw.game + 0x346D500u, &svc, 8) || svc == 0) return;
    at = now;
    LONG b = (g_lwPo.deadAt & 1) ^ 1, n = 0; uint32_t disposal = 0, unit = 0, state = 0;
    for (int i = 0; i < 32; i++)
        if (!SafeRead(svc + 0x18 + 0x40u * (uintptr_t)i, &unit, 4) && !SafeRead(svc + 0x20 + 0x40u * (uintptr_t)i, &state, 4) && unit && (state == 1 || state == 2)) g_lwPo.dead[b][n++] = unit;
    if (!SafeRead(svc + 0x818, &disposal, 4))
        for (uint32_t i = 0; i < disposal && i < 128 && n < 160; i++) if (!SafeRead(svc + 0x820 + 0x40u * (uintptr_t)i, &unit, 4) && unit) g_lwPo.dead[b][n++] = unit;
    g_lwPo.deadN[b] = n; InterlockedExchange(&g_lwPo.deadAt, g_lwPo.deadAt + 1);
}
static void LwPoNote(double tscPerMs)
{
    static const char* const types[3] = {"6", "4", "2"};
    char line[900]; int used = 0;
    for (int t = 0; t < 3; t++)
    {
        used += snprintf(line + used, sizeof line - (size_t)used, " type %s:", types[t]);
        for (int o = 0; o < 5 && used < 860; o++)
        {
            LONG a = InterlockedExchange(&g_lwPo.n[t][o][0], 0), b = InterlockedExchange(&g_lwPo.n[t][o][1], 0), r = InterlockedExchange(&g_lwPo.n[t][o][2], 0);
            LONG k = InterlockedExchange(&g_lwPo.keyframed[t][o], 0); double ms = (double)InterlockedExchange64(&g_lwPo.ticks[t][o], 0) / tscPerMs;
            if (a + b + r) used += snprintf(line + used, sizeof line - (size_t)used, " %s %ld, %.1f ms (bones %ld, ragdoll %ld, other %ld, keyframed %ld)", kLwPoOwner[o], a + b + r, ms, b, r, a, k);
        }
        if (used < 880) line[used++] = ';';
    }
    line[used < (int)sizeof line ? used : (int)sizeof line - 1] = 0;
    Note("living world: test: deferred pairs by owner (30 s, %ld dead units listed):%s", g_lwPo.deadN[g_lwPo.deadAt & 1], line);
}
static LW_TL_NOINLINE uintptr_t LwPcClaim(uintptr_t o, uintptr_t i)
{
    uintptr_t it = ((LwPcClaimFn)g_lwPc.orig)(o, i);
    uint64_t now = __builtin_ia32_rdtsc();
    LwPcCharge(now);
    if (it == 0) return it;
    uintptr_t blk = 0, cblk = 0; uint16_t off = 0, coff = 0; uint8_t kind = 0, type = 0;
    if (SafeRead(it, &blk, 8) || SafeRead(it + 8, &off, 2)) return it;
    uintptr_t x = blk + 0x20 + off;
    if (SafeRead(x + 0x0B, &kind, 1) || SafeRead(x, &cblk, 8) || SafeRead(x + 8, &coff, 2) || SafeRead(cblk + 0x20 + coff + 8, &type, 1)) return it;
    t_lwPc.t = now; t_lwPc.kind = kind < 4 ? kind : 3; t_lwPc.type = type & 15;
    t_lwPc.owner = kind == 1 && (type == 2 || type == 4 || type == 6) ? LwPcOwner(cblk + 0x20 + coff, type == 6 ? 0 : type == 4 ? 1 : 2) : -1;
    return it;
}
static void LwPcNote(double tscPerMs);
/* Game thread, once a frame: every 30 s the census, its clock calibrated by QueryPerformanceCounter over the window. */
static void LwPcFrame(void)
{
    static uint64_t tsc0; static LARGE_INTEGER q0; static ULONGLONG at;
    if (!g_lwTl.on) return;
    LwPoFrame();
    ULONGLONG now = GetTickCount64();
    if (at == 0) { at = now; tsc0 = __builtin_ia32_rdtsc(); QueryPerformanceCounter(&q0); return; }
    if (now - at < 30000) return;
    uint64_t tsc = __builtin_ia32_rdtsc(); LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
    double ms = (double)(q.QuadPart - q0.QuadPart) * 1000.0 / (double)f.QuadPart;
    if (ms > 0) { LwPcNote((double)(tsc - tsc0) / ms); LwPoNote((double)(tsc - tsc0) / ms); }
    at = now; tsc0 = tsc; q0 = q;
}
static void LwPcNote(double tscPerMs)
{
    char line[900]; int used = 0; LONG total = 0;
    for (int k = 0; k < 4; k++)
        for (int t = 0; t < 16; t++)
        {
            LONG n = InterlockedExchange(&g_lwPc.n[k][t], 0); LONG64 ticks = InterlockedExchange64(&g_lwPc.ticks[k][t], 0);
            total += n;
            if (n && used < 820) used += snprintf(line + used, sizeof line - (size_t)used, " kind %d type %d: %ld, %.2f ms;", k, t, n, (double)ticks / tscPerMs);
        }
    char most[300]; int mu = 0;
    for (int t = 0; t < 16; t++) { LONG64 m = InterlockedExchange64(&g_lwPc.most[t], 0); if (m && mu < 260) mu += snprintf(most + mu, sizeof most - (size_t)mu, " %d %.2f", t, (double)m / tscPerMs); }
    Note("living world: test: new pairs (30 s): %ld items;%s largest item by type (ms):%s", total, used ? line : " none;", mu ? most : " none");
}

static void LwInstallTasks(void)
{
    static int done;
    static const struct { uint32_t rva; const char* bytes; } pins[] = {
        { 0x8A9A60u, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 56 41 57" },
        { 0x8144B0u, "48 89 5C 24 08 48 89 7C 24 10 44 8B 41 0C 48 8B C2" },
        { 0x8AA260u, "48 89 54 24 10 48 83 EC 28 48 8B 01 48 8D 54 24 38" },
        { 0x79EB10u, "48 8B C4 89 48 08 55 53 57 41 54 48 8D A8 38 F6 FF FF" },
        { 0x79E910u, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57" },
        { 0xD0CC90u, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49 8B D9" },
        { 0xD0CC10u, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40" },
        { 0x9A0C90u, "48 89 5C 24 18 57 48 83 EC 20 48 8B D9 48 89 6C 24 30" },
        { 0x8AA320u, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 48 81 EC 40 01 00 00" },
        { 0xD40550u, "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8B" },
        { 0x8A9B35u, "4C 8B 72 08 E8 72 A9 F6 FF" },            /* the pop: handle from the node, then the free */
        { 0x8AA340u, "8B 42 2C 48 8B FA 25 FF FF FF 00" },      /* the push: its node count */
        { 0x79EF69u, "E8 A2 F9 FF FF" }, { 0x79F12Eu, "E8 DD F7 FF FF" },   /* the two replays */
        { 0x79EF93u, "E8 F8 DC 56 00" }, { 0x79EFE4u, "E8 27 DC 56 00" },   /* collide and particles builds */
        { 0x79EFBFu, "41 FF 50 60" }, { 0x79F010u, "41 FF 50 60" }, { 0x79F1E0u, "FF 50 60" },   /* the three waits */
        { 0x79F1F3u, "E8 98 1A 20 00" },                        /* the graph's destructor */
        { 0xDD3413u, "FF 50 38" },                              /* the velocity set's broad phase update */
        { 0xD4057Cu, "48 8B 46 08 33 FF 44 8B C7 4C 8B 90 30 01 00 00 4D 8B 6A 10" }, { 0xD406CEu, "45 8B 43 74" },
    };
    static const struct { const char* what; uint32_t rva; void* detour; void** orig; } hooks[] = {
        { "living world: task timeline: pop", 0x8A9A60u, (void*)LwTlPop, &g_lwTl.popOrig },
        { "living world: task timeline: node free", 0x8144B0u, (void*)LwTlFree, &g_lwTl.freeOrig },
        { "living world: task timeline: wait", 0x8AA260u, (void*)LwTlWaitOne, &g_lwTl.waitOrig },
        { "living world: task timeline: replay", 0x79E910u, (void*)LwTlReplay, &g_lwTl.replayOrig },
        { "living world: task timeline: collide build", 0xD0CC90u, (void*)LwTlCollide, &g_lwTl.collideOrig },
        { "living world: task timeline: particles build", 0xD0CC10u, (void*)LwTlParticles, &g_lwTl.particlesOrig },
        { "living world: task timeline: graph destructor", 0x9A0C90u, (void*)LwTlGraphDtor, &g_lwTl.dtorOrig },
        { "living world: task timeline: push", 0x8AA320u, (void*)LwTlPush, &g_lwTl.pushOrig },
        { "living world: task timeline: broad phase update", 0xD40550u, (void*)LwTlBpUpdate, &g_lwTl.bpOrig },
        { "living world: task timeline: step", 0x79EB10u, (void*)LwTlStepHook, &g_lwTl.stepOrig },
        { "living world: task timeline: new-pairs census", 0xE44800u, (void*)LwPcClaim, &g_lwPc.orig },
    };
    char t[4] = "";
    if (done++) return;
    if (GetEnvironmentVariableA("HELLFORGE_LW_TASKS", t, sizeof t) == 0 || t[0] != '1') return;
    g_lwTl.exe = (uintptr_t)GetModuleHandleA(NULL);
    for (size_t i = 0; i < sizeof pins / sizeof pins[0]; i++)
        if (!ApiCodeMatches(g_lwTl.exe + pins[i].rva, pins[i].bytes))
        { Note("living world: test: physics tasks: exe+0x%X is not the reviewed code; nothing is hooked", (unsigned)pins[i].rva); return; }
    for (size_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        if (LoaderHook(hooks[i].what, g_lwTl.exe + hooks[i].rva, hooks[i].detour, hooks[i].orig) || *hooks[i].orig == NULL)
        { Note("living world: test: physics tasks: %s could not be hooked; the %d hooks before it stay and pass every call on", hooks[i].what, (int)i); return; }
    g_lwTl.on = 1;
    Note("living world: test: physics tasks: installed (tasks N, tasks kick 0|1|2, tasks off)");
}
