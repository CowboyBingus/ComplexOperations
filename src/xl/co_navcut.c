/* The navmesh face cutter's guard: a segment budget a face, and a cut over the 14-bit limit done again uncut
 * (co_navcut.h). */
#include <string.h>

#include "co_navcut.h"
#include "co_shim.h"

typedef void (*FaceFn)(void* state, void* instance, const void* up, uint8_t flip, uint32_t key, void* vertices);
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
    uint32_t key; uint8_t flip, redone, partial;
    _Alignas(16) uint8_t up[16]; /* the face add reads it with movaps (0x133C695) */
} Slot;

static struct
{
    FaceFn face;
    AddFn add;
    StepFn step;
    HeapFn heap;
    ResetFn reset;
    uint32_t maxEdges, maxSegments;
    volatile LONG64 uncut, hidden, partial, leftOut;
    volatile LONG largest, worstEdges, worstSegments, worstX, worstY;
    Slot slots[NAVCUT_SLOTS];
} g_nc = {.maxEdges = CO_NAVCUT_MAX, .maxSegments = CO_NAVCUT_BUDGET};

/* The three entries, the two engine calls made by address, the loop that re-reads the queue after each step, the
 * 14-bit writer and its two signed readers, the segment count the add indexes by, and the extraction's counts. */
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
};

static uint32_t Count(const void* state, uint32_t at)
{
    uint32_t v;
    memcpy(&v, (const uint8_t*)state + at, 4);
    return v;
}

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
        s->redone = 0; s->partial = 0;
        memcpy(s->up, up, 16);
    }
    g_nc.face(state, instance, up, flip, key, vertices);
}

/* A silhouette of n points adds n segments (0x133E420: one record a point), and the add indexes them as i16: past
 * the budget the silhouette stays out, and the face is cut by the ones before it. The callers ignore the answer. */
static uint32_t Add(void* state, const void* points, int32_t count, int32_t material)
{
    const uint32_t segments = Count(state, CO_NAVCUT_SEGMENTS), n = count > 0 ? (uint32_t)count : 0;
    if (n > g_nc.maxSegments || segments > g_nc.maxSegments - n)
    {
        Slot* s = SlotOf(0);
        InterlockedIncrement64(&g_nc.leftOut);
        if (s != NULL && s->state == state && !s->partial) { s->partial = 1; InterlockedIncrement64(&g_nc.partial); }
        return 1;
    }
    return g_nc.add(state, points, count, material);
}

/* The sweep only writes the 14-bit indexes; the first signed read is after it (0x1341BF0). So a cut is stopped
 * between two steps, before any read: the face is added again alone and its events put in heap order, and the loop
 * goes on with that. A second stop of one face, or no face to add, leaves the engine's own empty state. */
static void Over(void* state, uint32_t edges, uint32_t segments)
{
    float origin[2];
    Slot* s = SlotOf(0);
    memcpy(origin, (const uint8_t*)state + CO_NAVCUT_ORIGIN, 8);
    InterlockedExchange(&g_nc.worstEdges, (LONG)edges);
    InterlockedExchange(&g_nc.worstSegments, (LONG)segments);
    InterlockedExchange(&g_nc.worstX, (LONG)origin[0]);
    InterlockedExchange(&g_nc.worstY, (LONG)origin[1]);
    if (s != NULL && s->state == state && !s->redone)
    {
        s->redone = 1;
        InterlockedIncrement64(&g_nc.uncut);
        g_nc.face(state, s->instance, s->up, s->flip, s->key, s->vertices);
        g_nc.heap((uint8_t*)state + CO_NAVCUT_QUEUE);
        return;
    }
    InterlockedIncrement64(&g_nc.hidden);
    g_nc.reset(state);
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
    out->worstEdges = (uint32_t)g_nc.worstEdges; out->worstSegments = (uint32_t)g_nc.worstSegments;
    out->worstAt[0] = (float)g_nc.worstX; out->worstAt[1] = (float)g_nc.worstY;
}

void co_navcut_limits(uint32_t edges, uint32_t segments)
{
    g_nc.maxEdges = edges < 16 ? 16 : edges > CO_NAVCUT_MAX ? CO_NAVCUT_MAX : edges;
    g_nc.maxSegments = segments < 16 ? 16 : segments > CO_NAVCUT_MAX ? CO_NAVCUT_MAX : segments;
}

#ifdef CO_TEST
void co_navcut_test_reset(void)
{
    memset((void*)&g_nc, 0, sizeof g_nc);
    g_nc.maxEdges = CO_NAVCUT_MAX; g_nc.maxSegments = CO_NAVCUT_BUDGET;
}
void co_navcut_test_calls(void (*heap)(void* queue), void (*reset)(void* state)) { g_nc.heap = heap; g_nc.reset = reset; }
#endif
