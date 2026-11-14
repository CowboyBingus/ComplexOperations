/* The navmesh face cutter's guard: a segment budget a face, and a cut over the 14-bit limit done again uncut
 * (co_navcut.h). */
#include <string.h>

#include "co_navcut.h"
#include "co_shim.h"

typedef void (*FaceFn)(void* state, void* instance, const void* up, uint8_t flip, uint32_t key, void* vertices);
typedef void (*KeyFn)(void* state, uint32_t key);
typedef uint32_t (*AddFn)(void* state, const void* points, int32_t count, int32_t material);
typedef uint8_t (*StepFn)(void* state);
typedef void (*HeapFn)(void* queue);
typedef void (*ResetFn)(void* state);

/* A thread cuts one face at a time (0xCB1A30 and 0xC24130 run add, silhouettes and extraction in line), so the
 * face's arguments are kept by thread. A worker keeps its slot for the session. */
#define NAVCUT_SLOTS 256
typedef struct Slot
{
    volatile LONG thread;
    void* state; void* instance; void* vertices;
    uint32_t key, adds, own; /* adds: the silhouettes this face took; own: the face's own edges */
    uint8_t flip, redone, partial;
#if defined(CO_RESEARCH) || defined(CO_TEST)
    uint8_t gone;        /* stopped twice: its 0 polygons are the reset's, not a sweep's */
    uint32_t segments;   /* the segments the sweep begins with */
    uint64_t print;      /* the face add's input, hashed */
#endif
    _Alignas(16) uint8_t up[16]; /* the face add reads it with movaps (0x133C695) */
} Slot;

static struct
{
    FaceFn face;
    KeyFn key;
    AddFn add;
    StepFn step;
    HeapFn heap;
    ResetFn reset;
    uint32_t maxEdges, maxSegments;
    volatile LONG64 uncut, hidden, partial, leftOut, againEnded, againNone, againMany, aloneEnded, aloneNone, aloneMany;
    /* one stopped cut's four numbers, and one odd end's three, each in one word: two threads' cuts never mix */
    volatile LONG64 worst, odd;
    volatile LONG largest;
    Slot slots[NAVCUT_SLOTS];
} g_nc = {.maxEdges = CO_NAVCUT_MAX, .maxSegments = CO_NAVCUT_BUDGET};

/* The four entries, the two engine calls made by address, the loop that re-reads the queue after each step, the
 * 14-bit writer and its two signed readers, the segment count the add indexes by, and the extraction's counts. Then
 * the status list's three sites (constructor, first read, link), the add's one answer and its two callers, and one
 * masked writer in each other function that appends half-edges. */
static const struct { uint32_t rva; const char* pin; } kNavCutPins[] = {
    {CO_NAVCUT_FACE, "48 89 5C 24 08 48 89 6C 24 10 56 57 41 54 41 56 41 57 48 83 EC 50"},
    {0x133C67Au, "E8 D1 0D 00 00 8B 84 24 A0 00 00 00 41 83 CF FF 89 9D D0 01 00 00"},
    {CO_NAVCUT_ADD, "40 53 48 83 EC 30 45 8B D0 48 8B DA 4C 8B D9"},
    {CO_NAVCUT_STEP, "40 55 53 56 48 8B EC 48 83 EC 60 83 B9 98 00 00 00 01 48 8B F1"},
    {CO_NAVCUT_HEAP, "40 53 48 83 EC 10 8B 41 08 4C 8B D9 99 2B C2 D1 F8 8B D8 83 F8 01"},
    {CO_NAVCUT_RESET, "33 C0 89 81 D8 00 00 00 89 81 E8 00 00 00 89 81 F8 00 00 00 89 81 08 01 00 00 89 81 CC 01 00 00 48 81 C1 90 00 00 00 E9 C4 F5 FF FF"},
    {0x133CA40u, "8B 41 08 83 F8 01 7C 02 7E 07 C7 41 08 01 00 00 00 C3"},
    {0x133BB30u, "48 8B CF E8 D8 5B 00 00 B9 DC F7 A9 E4 E8 0E 4B 6C FF 83 BF 98 00 00 00 01 75 E5"},
    {0x133BB6Fu, "8B B7 F8 00 00 00 45 33 ED 44 8B B7 E8 00 00 00 44 8B BF 08 01 00 00"},
    {0x13408B8u, "81 E1 FF 3F 00 00 25 02 00 FF FF C1 E1 02 0B C8 41 0F BF C6"},
    {0x1341D3Bu, "C1 E1 10 C1 F9 12 48 0F BF C9 89 44 CA 04"},
    {0x1341F3Cu, "48 C1 E9 20 44 8B C1 C1 E1 10 C1 F9 12"},
    {0x133E460u, "44 0F B7 96 D8 00 00 00"},
    {CO_NAVCUT_KEY, "89 54 24 10 53 48 83 EC 20 48 8B D9 E8 1F 0E 00 00 8B 44 24 38"},
    {0x133D135u, "66 89 83 A0 00 00 00 66 89 83 A2 00 00 00"},
    {0x13405DBu, "44 0F B7 8B A0 00 00 00"},
    {0x13419A3u, "66 89 B3 A0 00 00 00"},
    {0x133BA21u, "E8 EA 22 00 00 B8 01 00 00 00 48 83 C4 30 5B C3"},
    {0xCB20B1u, "E8 EA 98 68 00 03 F7 48 FF C3"},
    {0xC244B9u, "E8 E2 74 71 00 FF C3 48 83 C7 04 48 83 C6 10"},
    {0x134303Bu, "41 0F BF CE 81 E1 FF 3F 00 00 C1 E1 02 83 C9 02"},
    {0x134327Cu, "0F BF C5 25 FF 3F 00 00 C1 E0 02 83 C8 02"},
    {0x1343476u, "81 E1 FF 3F 00 00 C1 E1 02 83 C9 02"},
    {0x13436A0u, "81 E1 FF 3F 00 00 C1 E1 02 83 C9 02"},
};

static uint32_t Count(const void* state, uint32_t at)
{
    uint32_t v;
    memcpy(&v, (const uint8_t*)state + at, 4);
    return v;
}

static LONG64 Pack(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    const uint64_t v = (uint64_t)(a & 0xFFFFu) | (uint64_t)(b & 0xFFFFu) << 16 | (uint64_t)(c & 0xFFFFu) << 32 | (uint64_t)(d & 0xFFFFu) << 48;
    return (LONG64)v;
}
static uint32_t Short(uint32_t v) { return v > 0xFFFFu ? 0xFFFFu : v; }
static uint32_t Metres(float v) { return (uint32_t)(int32_t)(v < -32767.0f ? -32767.0f : v > 32767.0f ? 32767.0f : v); }

#if defined(CO_RESEARCH) || defined(CO_TEST)
/* Research only. The redo's claim is that a face added again comes to what the engine makes of that face alone, so
 * the two ends are kept by the face add's whole input and compared. The table is fixed: a face with no free row
 * within 8 of its place is counted and not kept. */
#define NAVCUT_SAME 16384u
static struct
{
    SRWLOCK lock;
    struct { uint64_t print; uint16_t alone, again; } rows[NAVCUT_SAME]; /* polygons + 1; 0 for not seen */
    volatile LONG64 same, differ, notKept, last;
    volatile LONG64 ended[3], stopped[3], steep[3];
    volatile LONG most[3];
} g_study;

static uint64_t Mix(uint64_t h, const void* bytes, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)bytes)[i]) * 1099511628211ull;
    return h;
}

/* Key, flip, up, the frame's tolerance (+0x1D4, 0x133C732) and every own edge's two points and ordinal: what the
 * face add builds the face-alone state from. Another section's face of the same key has other points. */
static uint64_t Print(const void* state, const Slot* s)
{
    const uint8_t* edges;
    const uint32_t n = Count(state, CO_NAVCUT_OWN);
    uint64_t h = 1469598103934665603ull;
    memcpy(&edges, (const uint8_t*)state + CO_NAVCUT_OWN_EDGES, sizeof edges);
    h = Mix(h, &s->key, 4); h = Mix(h, &s->flip, 1); h = Mix(h, s->up, 16);
    h = Mix(h, (const uint8_t*)state + 0x1D4u, 4); h = Mix(h, &n, 4);
    for (uint32_t i = 0; edges != NULL && i < n && i < 4096u; i++) h = Mix(h, edges + (size_t)i * 0x30u, 0x22u);
    return h != 0 ? h : 1;
}

static void Same(const Slot* s, uint32_t faces, int again)
{
    const uint16_t now = (uint16_t)(faces >= 0xFFFEu ? 0xFFFFu : faces + 1);
    uint16_t other = 0;
    int kept = 0;
    AcquireSRWLockExclusive(&g_study.lock);
    for (uint32_t i = 0; i < 8 && !kept; i++)
    {
        const uint32_t at = (uint32_t)(s->print + i) & (NAVCUT_SAME - 1);
        if (g_study.rows[at].print != s->print && g_study.rows[at].print != 0) continue;
        g_study.rows[at].print = s->print;
        if (again) { g_study.rows[at].again = now; other = g_study.rows[at].alone; }
        else { g_study.rows[at].alone = now; other = g_study.rows[at].again; }
        kept = 1;
    }
    ReleaseSRWLockExclusive(&g_study.lock);
    if (!kept) { InterlockedIncrement64(&g_study.notKept); return; }
    if (other == 0) return;
    if (other == now) { InterlockedIncrement64(&g_study.same); return; }
    InterlockedIncrement64(&g_study.differ);
    InterlockedExchange64(&g_study.last, again ? Pack((uint32_t)other - 1, faces, s->own, 0) : Pack(faces, (uint32_t)other - 1, s->own, 0));
}

/* The budget's ground: half-edges a segment of whole cuts, by the segments they began with. */
static void Whole(const Slot* s, uint32_t edges, int stopped)
{
    const int band = s->segments >= 2048u ? 2 : s->segments >= 1024u ? 1 : s->segments >= 512u ? 0 : -1;
    if (band < 0) return;
    if (stopped) { InterlockedIncrement64(&g_study.stopped[band]); return; }
    const LONG rate = (LONG)((uint64_t)edges * 100u / s->segments);
    InterlockedIncrement64(&g_study.ended[band]);
    if (rate > 229) InterlockedIncrement64(&g_study.steep[band]);
    for (LONG seen = g_study.most[band]; seen < rate;)
    {
        const LONG was = InterlockedCompareExchange(&g_study.most[band], rate, seen);
        if (was == seen) break;
        seen = was;
    }
}
#endif

static Slot* SlotOf(int claim)
{
    const LONG me = (LONG)GetCurrentThreadId();
    for (int i = 0; i < NAVCUT_SLOTS; i++)
    {
        const LONG owner = g_nc.slots[i].thread;
        if (owner == me) return &g_nc.slots[i];
        if (owner == 0)
        {
            if (!claim) return NULL;
            if (InterlockedCompareExchange(&g_nc.slots[i].thread, me, 0) == 0) return &g_nc.slots[i];
        }
    }
    return NULL;
}

static void Face(void* state, void* instance, const void* up, uint8_t flip, uint32_t key, void* vertices)
{
    Slot* s = SlotOf(1);
    if (s != NULL)
    {
        s->state = state; s->instance = instance; s->vertices = vertices; s->key = key; s->flip = flip;
        s->redone = 0; s->partial = 0; s->adds = 0;
        memcpy(s->up, up, 16);
    }
    g_nc.face(state, instance, up, flip, key, vertices);
    if (s == NULL) return;
    s->own = Count(state, CO_NAVCUT_OWN);
#if defined(CO_RESEARCH) || defined(CO_TEST)
    s->gone = 0; s->segments = Count(state, CO_NAVCUT_SEGMENTS); s->print = Print(state, s);
#endif
}

/* A cut that starts with no face (0x133C620) must not find the face this thread cut before: the callers' states are
 * stack frames, so the address comes back. */
static void Key(void* state, uint32_t key)
{
    Slot* s = SlotOf(0);
    if (s != NULL) s->state = NULL;
    g_nc.key(state, key);
}

/* A silhouette of n points adds n segments (0x133E420: one record a point), and the add indexes them as i16: past
 * the budget the silhouette stays out, and the face is cut by the ones before it. The callers ignore the answer. */
static uint32_t Add(void* state, const void* points, int32_t count, int32_t material)
{
    const uint32_t segments = Count(state, CO_NAVCUT_SEGMENTS), n = count > 0 ? (uint32_t)count : 0;
    Slot* s = SlotOf(0);
    if (n > g_nc.maxSegments || segments > g_nc.maxSegments - n)
    {
        InterlockedIncrement64(&g_nc.leftOut);
        if (s != NULL && s->state == state && !s->partial) { s->partial = 1; InterlockedIncrement64(&g_nc.partial); }
        return 1;
    }
    if (s != NULL && s->state == state)
    {
        s->adds++;
#if defined(CO_RESEARCH) || defined(CO_TEST)
        s->segments += n;
#endif
    }
    return g_nc.add(state, points, count, material);
}

/* The sweep only writes the 14-bit indexes; the first signed read is after it (0x1341BF0). So a cut is stopped
 * between two steps, before any read: the face is added again alone and its events put in heap order, and the loop
 * goes on with that. A second stop of one face, or no face to add, leaves the engine's own empty state.
 * A sweep stopped half way still lists its segments in the status, which neither the reset nor the face add
 * clears: the next sweep's first event (0x13405DB) would walk them. It is emptied as the constructor does. */
static void Over(void* state, uint32_t edges, uint32_t segments)
{
    static const uint16_t kNone[2] = {0xFFFFu, 0xFFFFu};
    float origin[2];
    Slot* s = SlotOf(0);
    memcpy((uint8_t*)state + CO_NAVCUT_STATUS, kNone, sizeof kNone);
    memcpy(origin, (const uint8_t*)state + CO_NAVCUT_ORIGIN, 8);
    InterlockedExchange64(&g_nc.worst, Pack(Short(edges), Short(segments), Metres(origin[0]), Metres(origin[1])));
    if (s != NULL && s->state == state && !s->redone)
    {
        s->redone = 1;
#if defined(CO_RESEARCH) || defined(CO_TEST)
        Whole(s, edges, 1);
#endif
        InterlockedIncrement64(&g_nc.uncut);
        g_nc.face(state, s->instance, s->up, s->flip, s->key, s->vertices);
        g_nc.heap((uint8_t*)state + CO_NAVCUT_QUEUE);
        return;
    }
#if defined(CO_RESEARCH) || defined(CO_TEST)
    if (s != NULL && s->state == state) s->gone = 1;
#endif
    InterlockedIncrement64(&g_nc.hidden);
    g_nc.reset(state);
}

/* The sweep is over (the loop 0x133BB42 leaves at a queue of 1): the face's arguments are not kept past it, and
 * what a face alone came to is counted. */
static void Ended(void* state)
{
    Slot* s = SlotOf(0);
    if (s == NULL || s->state != state) return;
    const uint32_t faces = Count(state, CO_NAVCUT_FACES);
    s->state = NULL;
    if (s->redone)
    {
        InterlockedIncrement64(&g_nc.againEnded);
        if (faces != 1)
        {
            InterlockedIncrement64(faces == 0 ? &g_nc.againNone : &g_nc.againMany);
            InterlockedExchange64(&g_nc.odd, Pack(Short(faces), Short(Count(state, CO_NAVCUT_EDGES)), Short(s->own), 0));
        }
    }
    else if (s->adds == 0)
    {
        InterlockedIncrement64(&g_nc.aloneEnded);
        if (faces != 1) InterlockedIncrement64(faces == 0 ? &g_nc.aloneNone : &g_nc.aloneMany);
    }
#if defined(CO_RESEARCH) || defined(CO_TEST)
    if (s->redone ? !s->gone : s->adds == 0) Same(s, faces, s->redone);
    if (!s->redone && s->adds != 0) Whole(s, Count(state, CO_NAVCUT_EDGES), 0);
#endif
}

static uint8_t Step(void* state)
{
    const uint8_t answer = g_nc.step(state);
    const uint32_t edges = Count(state, CO_NAVCUT_EDGES), segments = Count(state, CO_NAVCUT_SEGMENTS);
    LONG seen = g_nc.largest;
    while ((uint32_t)seen < edges)
    {
        const LONG was = InterlockedCompareExchange(&g_nc.largest, (LONG)edges, seen);
        if (was == seen) break;
        seen = was;
    }
    if (edges > g_nc.maxEdges || segments > CO_NAVCUT_MAX || Count(state, CO_NAVCUT_FACES) > CO_NAVCUT_MAX ||
        Count(state, CO_NAVCUT_VERTICES) > CO_NAVCUT_MAX) Over(state, edges, segments);
    if (Count(state, CO_NAVCUT_QUEUE + 8) == 1) Ended(state);
    return answer;
}

const char* co_navcut_install(uintptr_t exe)
{
    if (g_nc.step != NULL) return NULL;
    if (exe == 0) return "the engine's image is not known";
    for (size_t i = 0; i < sizeof kNavCutPins / sizeof kNavCutPins[0]; i++)
        if (co_exe_code_matches(exe + kNavCutPins[i].rva, kNavCutPins[i].pin) != 1)
            return "the engine's navmesh face cutter is not the code this mod was made for";
    if (g_nc.heap == NULL) g_nc.heap = (HeapFn)(exe + CO_NAVCUT_HEAP);
    if (g_nc.reset == NULL) g_nc.reset = (ResetFn)(exe + CO_NAVCUT_RESET);
    /* the step last: with it alone a stopped cut is reset (the face cut away), which is still in range */
    if (co_loader_hook("navmesh cuts: the face's arguments are kept", exe + CO_NAVCUT_FACE, (void*)Face, (void**)&g_nc.face) || g_nc.face == NULL)
    { g_nc.face = NULL; return "the engine's navmesh face add could not be hooked"; }
    if (co_loader_hook("navmesh cuts: a cut with no face forgets the last one", exe + CO_NAVCUT_KEY, (void*)Key, (void**)&g_nc.key) || g_nc.key == NULL)
    { g_nc.key = NULL; return "the engine's navmesh cut start without a face could not be hooked"; }
    if (co_loader_hook("navmesh cuts: a face's segment budget", exe + CO_NAVCUT_ADD, (void*)Add, (void**)&g_nc.add) || g_nc.add == NULL)
    { g_nc.add = NULL; return "the engine's navmesh silhouette add could not be hooked"; }
    if (co_loader_hook("navmesh cuts: a cut over the edge limit is done again uncut", exe + CO_NAVCUT_STEP, (void*)Step, (void**)&g_nc.step) ||
        g_nc.step == NULL)
    { g_nc.step = NULL; return "the engine's navmesh sweep step could not be hooked"; }
    return NULL;
}

void co_navcut_seen(CoNavCutSeen* out)
{
    out->uncut = (uint64_t)g_nc.uncut; out->hidden = (uint64_t)g_nc.hidden;
    out->partial = (uint64_t)g_nc.partial; out->leftOut = (uint64_t)g_nc.leftOut;
    out->largest = (uint32_t)g_nc.largest;
    const uint64_t worst = (uint64_t)g_nc.worst, odd = (uint64_t)g_nc.odd;
    out->worstEdges = (uint32_t)(worst & 0xFFFFu); out->worstSegments = (uint32_t)(worst >> 16 & 0xFFFFu);
    out->worstAt[0] = (float)(int16_t)(worst >> 32 & 0xFFFFu); out->worstAt[1] = (float)(int16_t)(worst >> 48);
    out->againEnded = (uint64_t)g_nc.againEnded; out->againNone = (uint64_t)g_nc.againNone; out->againMany = (uint64_t)g_nc.againMany;
    out->aloneEnded = (uint64_t)g_nc.aloneEnded; out->aloneNone = (uint64_t)g_nc.aloneNone; out->aloneMany = (uint64_t)g_nc.aloneMany;
    out->oddFaces = (uint32_t)(odd & 0xFFFFu); out->oddEdges = (uint32_t)(odd >> 16 & 0xFFFFu); out->oddOwn = (uint32_t)(odd >> 32 & 0xFFFFu);
}

#if defined(CO_RESEARCH) || defined(CO_TEST)
void co_navcut_limits(uint32_t edges, uint32_t segments)
{
    g_nc.maxEdges = edges < 16 ? 16 : edges > CO_NAVCUT_MAX ? CO_NAVCUT_MAX : edges;
    g_nc.maxSegments = segments < 16 ? 16 : segments > CO_NAVCUT_MAX ? CO_NAVCUT_MAX : segments;
}

void co_navcut_study(CoNavCutStudy* out)
{
    const uint64_t last = (uint64_t)g_study.last;
    out->same = (uint64_t)g_study.same; out->differ = (uint64_t)g_study.differ; out->notKept = (uint64_t)g_study.notKept;
    out->differAlone = (uint32_t)(last & 0xFFFFu); out->differAgain = (uint32_t)(last >> 16 & 0xFFFFu); out->differOwn = (uint32_t)(last >> 32 & 0xFFFFu);
    for (int i = 0; i < 3; i++)
    {
        out->ended[i] = (uint64_t)g_study.ended[i]; out->stopped[i] = (uint64_t)g_study.stopped[i];
        out->steep[i] = (uint64_t)g_study.steep[i]; out->most[i] = (uint32_t)g_study.most[i];
    }
}
#endif

#ifdef CO_TEST
void co_navcut_test_reset(void)
{
    memset((void*)&g_nc, 0, sizeof g_nc);
    memset((void*)&g_study, 0, sizeof g_study);
    g_nc.maxEdges = CO_NAVCUT_MAX; g_nc.maxSegments = CO_NAVCUT_BUDGET;
}
void co_navcut_test_calls(void (*heap)(void* queue), void (*reset)(void* state)) { g_nc.heap = heap; g_nc.reset = reset; }
#endif
