/* The navmesh face cutter's guard (co_navcut.h) on a fake exe. The engine's stand-in keeps the cutter state's counts
 * where the game has them and runs the game's order: face add, silhouette adds, heap build, steps until the queue is
 * 1, then the first signed 14-bit read (0x1341BF0), which is where a cut over 8192 half-edges goes wrong.
 * It also keeps the sweep's status where the game has it (first and last segment at +0xA0, +0xA2): only the
 * constructor and the steps write it, a step leaves it holding a segment until the sweep's last event, and the first
 * step after a face add finds an old one as the game's 0x13405DB would. The list's records and the geometry are not
 * modelled: that nothing else outlives a stopped sweep is the static analysis's claim, not this test's. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_navcut.h"
#include "xl/co_shim.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t* g_exe;

typedef struct State { _Alignas(16) uint8_t b[0x200]; } State;
static uint32_t Get(const State* s, uint32_t at) { uint32_t v; memcpy(&v, s->b + at, 4); return v; }
static void Set(State* s, uint32_t at, uint32_t v) { memcpy(s->b + at, &v, 4); }
#define QUEUE_SIZE (CO_NAVCUT_QUEUE + 8)

/* What the stand-ins saw and what they do. A face add resets the state, adds `faceSegments` and queues `faceEvents`;
 * a step takes one event and adds `perStep` half-edges (and vertices and polygons when asked). */
static struct
{
    uint32_t faceCalls, addCalls, stepCalls, heapCalls, resetCalls;
    void* faceState; void* faceInstance; void* faceVertices; const void* faceUp; uint32_t faceKey; uint8_t faceFlip, faceUpBytes[16];
    void* heapQueue;
    uint32_t faceSegments, faceEvents, perStep, vertsPerStep, facesPerStep, segmentsPerStep;
    uint32_t perStepAlone; /* half-edges a step once more than 1 + aloneLater faces were added (0: perStep) */
    uint32_t aloneLater;
    uint32_t unsorted;     /* events queued since the last heap build */
    uint32_t keyCalls, keyKey;
    uint32_t facesOnFirst; /* polygons the first step after a face add makes (the sweep opens them at its first event) */
    uint32_t fresh;        /* a face or key call since the last step: the next step is a sweep's first */
    uint32_t staleStatus;  /* sweeps whose first step found the status of an earlier, stopped sweep */
    uint32_t own;          /* the face's own edges, listed at +0x1D8 as the game's face add does (0x30 bytes each) */
    _Alignas(16) uint8_t ownEdges[8 * 0x30];
} E;

#define STATUS CO_NAVCUT_STATUS
static uint16_t Get16(const State* s, uint32_t at) { uint16_t v; memcpy(&v, s->b + at, 2); return v; }
static void Set16(State* s, uint32_t at, uint16_t v) { memcpy(s->b + at, &v, 2); }
/* The constructor 0x133D080: zero counts, an empty status. */
static void Construct(State* s) { memset(s, 0, sizeof *s); Set16(s, STATUS, 0xFFFFu); Set16(s, STATUS + 2, 0xFFFFu); }

static void EngineReset(void* state)
{
    State* s = (State*)state;
    E.resetCalls++;
    Set(s, CO_NAVCUT_SEGMENTS, 0); Set(s, CO_NAVCUT_FACES, 0); Set(s, CO_NAVCUT_EDGES, 0); Set(s, CO_NAVCUT_VERTICES, 0);
    Set(s, QUEUE_SIZE, 1);
}

static void EngineFace(void* state, void* instance, const void* up, uint8_t flip, uint32_t key, void* vertices)
{
    State* s = (State*)state;
    E.faceCalls++;
    E.faceState = state; E.faceInstance = instance; E.faceVertices = vertices; E.faceUp = up; E.faceKey = key; E.faceFlip = flip;
    memcpy(E.faceUpBytes, up, 16);
    EngineReset(state); E.resetCalls--;
    Set(s, CO_NAVCUT_SEGMENTS, E.faceSegments);
    Set(s, QUEUE_SIZE, 1 + E.faceEvents);
    E.unsorted = E.faceEvents;
    E.fresh = 1;
    const uint8_t* edges = E.ownEdges;
    Set(s, CO_NAVCUT_OWN, E.own); memcpy(s->b + CO_NAVCUT_OWN_EDGES, &edges, sizeof edges);
}

static void EngineKey(void* state, uint32_t key)
{
    E.keyCalls++; E.keyKey = key; E.fresh = 1;
    EngineReset(state); E.resetCalls--;
}

static uint32_t EngineAdd(void* state, const void* points, int32_t count, int32_t material)
{
    State* s = (State*)state;
    E.addCalls++;
    Set(s, CO_NAVCUT_SEGMENTS, Get(s, CO_NAVCUT_SEGMENTS) + (uint32_t)count);
    Set(s, QUEUE_SIZE, Get(s, QUEUE_SIZE) + (uint32_t)count);
    E.unsorted += (uint32_t)count;
    return 1;
}

static void EngineHeap(void* queue) { E.heapCalls++; E.heapQueue = queue; E.unsorted = 0; }

static uint8_t EngineStep(void* state)
{
    State* s = (State*)state;
    E.stepCalls++;
    const uint32_t per = E.faceCalls > 1 + E.aloneLater && E.perStepAlone ? E.perStepAlone : E.perStep;
    if (E.fresh)
    {
        if (Get16(s, STATUS) != 0xFFFFu || Get16(s, STATUS + 2) != 0xFFFFu) E.staleStatus++;
        Set(s, CO_NAVCUT_FACES, Get(s, CO_NAVCUT_FACES) + E.facesOnFirst);
        E.fresh = 0;
    }
    Set(s, QUEUE_SIZE, Get(s, QUEUE_SIZE) - 1);
    const uint16_t held = Get(s, QUEUE_SIZE) == 1 ? 0xFFFFu : (uint16_t)(E.stepCalls & 0x1FFFu);
    Set16(s, STATUS, held); Set16(s, STATUS + 2, held);
    Set(s, CO_NAVCUT_EDGES, Get(s, CO_NAVCUT_EDGES) + per);
    Set(s, CO_NAVCUT_VERTICES, Get(s, CO_NAVCUT_VERTICES) + E.vertsPerStep);
    Set(s, CO_NAVCUT_FACES, Get(s, CO_NAVCUT_FACES) + E.facesPerStep);
    Set(s, CO_NAVCUT_SEGMENTS, Get(s, CO_NAVCUT_SEGMENTS) + E.segmentsPerStep);
    return 1;
}

static void Hex(uint8_t* at, const char* hex)
{
    for (; *hex; hex++)
    {
        unsigned v = 0;
        if (*hex == ' ') continue;
        sscanf(hex, "%2x", &v);
        *at++ = (uint8_t)v;
        hex++;
    }
}

static void Pins(uint8_t* exe)
{
    Hex(exe + CO_NAVCUT_FACE, "48 89 5C 24 08 48 89 6C 24 10 56 57 41 54 41 56 41 57 48 83 EC 50");
    Hex(exe + 0x133C67Au, "E8 D1 0D 00 00 8B 84 24 A0 00 00 00 41 83 CF FF 89 9D D0 01 00 00");
    Hex(exe + CO_NAVCUT_ADD, "40 53 48 83 EC 30 45 8B D0 48 8B DA 4C 8B D9");
    Hex(exe + CO_NAVCUT_STEP, "40 55 53 56 48 8B EC 48 83 EC 60 83 B9 98 00 00 00 01 48 8B F1");
    Hex(exe + CO_NAVCUT_HEAP, "40 53 48 83 EC 10 8B 41 08 4C 8B D9 99 2B C2 D1 F8 8B D8 83 F8 01");
    Hex(exe + CO_NAVCUT_RESET, "33 C0 89 81 D8 00 00 00 89 81 E8 00 00 00 89 81 F8 00 00 00 89 81 08 01 00 00 89 81 CC 01 00 00 48 81 C1 90 00 00 00 E9 C4 F5 FF FF");
    Hex(exe + 0x133CA40u, "8B 41 08 83 F8 01 7C 02 7E 07 C7 41 08 01 00 00 00 C3");
    Hex(exe + 0x133BB30u, "48 8B CF E8 D8 5B 00 00 B9 DC F7 A9 E4 E8 0E 4B 6C FF 83 BF 98 00 00 00 01 75 E5");
    Hex(exe + 0x133BB6Fu, "8B B7 F8 00 00 00 45 33 ED 44 8B B7 E8 00 00 00 44 8B BF 08 01 00 00");
    Hex(exe + 0x13408B8u, "81 E1 FF 3F 00 00 25 02 00 FF FF C1 E1 02 0B C8 41 0F BF C6");
    Hex(exe + 0x1341D3Bu, "C1 E1 10 C1 F9 12 48 0F BF C9 89 44 CA 04");
    Hex(exe + 0x1341F3Cu, "48 C1 E9 20 44 8B C1 C1 E1 10 C1 F9 12");
    Hex(exe + 0x133E460u, "44 0F B7 96 D8 00 00 00");
    Hex(exe + CO_NAVCUT_KEY, "89 54 24 10 53 48 83 EC 20 48 8B D9 E8 1F 0E 00 00 8B 44 24 38");
    Hex(exe + 0x133D135u, "66 89 83 A0 00 00 00 66 89 83 A2 00 00 00");
    Hex(exe + 0x13405DBu, "44 0F B7 8B A0 00 00 00");
    Hex(exe + 0x13419A3u, "66 89 B3 A0 00 00 00");
    Hex(exe + 0x133BA21u, "E8 EA 22 00 00 B8 01 00 00 00 48 83 C4 30 5B C3");
    Hex(exe + 0xCB20B1u, "E8 EA 98 68 00 03 F7 48 FF C3");
    Hex(exe + 0xC244B9u, "E8 E2 74 71 00 FF C3 48 83 C7 04 48 83 C6 10");
    Hex(exe + 0x134303Bu, "41 0F BF CE 81 E1 FF 3F 00 00 C1 E1 02 83 C9 02");
    Hex(exe + 0x134327Cu, "0F BF C5 25 FF 3F 00 00 C1 E0 02 83 C8 02");
    Hex(exe + 0x1343476u, "81 E1 FF 3F 00 00 C1 E1 02 83 C9 02");
    Hex(exe + 0x13436A0u, "81 E1 FF 3F 00 00 C1 E1 02 83 C9 02");
}

static void Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_navcut_test_reset();
    g_exe = fake_host_exe();
    g_fake.strictCode = 1;
    Pins(g_exe);
    g_fake.originals[0].target = (uintptr_t)g_exe + CO_NAVCUT_FACE; g_fake.originals[0].original = (void*)EngineFace;
    g_fake.originals[1].target = (uintptr_t)g_exe + CO_NAVCUT_ADD; g_fake.originals[1].original = (void*)EngineAdd;
    g_fake.originals[2].target = (uintptr_t)g_exe + CO_NAVCUT_STEP; g_fake.originals[2].original = (void*)EngineStep;
    g_fake.originals[3].target = (uintptr_t)g_exe + CO_NAVCUT_KEY; g_fake.originals[3].original = (void*)EngineKey;
    memset(&E, 0, sizeof E);
    co_navcut_test_calls(EngineHeap, EngineReset);
}

typedef void (*FaceFn)(void*, void*, const void*, uint8_t, uint32_t, void*);
typedef uint32_t (*AddFn)(void*, const void*, int32_t, int32_t);
typedef uint8_t (*StepFn)(void*);
typedef void (*KeyFn)(void*, uint32_t);
static void* Detour(uint32_t rva) { return fake_host_detour((uintptr_t)g_exe + rva - (uintptr_t)g_fake.image); }

/* The cut as 0x133BA40 runs it once the face and the silhouettes are in. Returns 1 when the state that reaches the
 * first signed read fits 14 bits, the events were in heap order at every step and no sweep began on an old status;
 * 0 when not, or when the loop does not end. */
static int Sweep(State* s)
{
    StepFn step = (StepFn)Detour(CO_NAVCUT_STEP);
    int ordered = 1;
    EngineHeap(s->b + CO_NAVCUT_QUEUE);
    for (uint32_t turns = 0; Get(s, QUEUE_SIZE) != 1; turns++)
    {
        if (turns > 200000) return 0;
        if (E.unsorted) ordered = 0;
        step(s);
    }
    return ordered && E.staleStatus == 0 && Get(s, CO_NAVCUT_EDGES) <= 8192 && Get(s, CO_NAVCUT_VERTICES) <= 8192 && Get(s, CO_NAVCUT_FACES) <= 8192;
}

static _Alignas(16) const uint8_t kUp[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static int g_instance, g_vertices;

static struct { State state; int ok; uint32_t key, edges; } g_other;
static DWORD WINAPI OtherThread(void* unused)
{
    FaceFn face = (FaceFn)Detour(CO_NAVCUT_FACE);
    face(&g_other.state, &g_other, kUp, 0, 0x777u, &g_other.key);
    return 0;
}

int main(void)
{
    State s;
    CoNavCutSeen seen;
    int points[4];

    Reset("a cut under the limits passes as it is");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    FaceFn face = (FaceFn)Detour(CO_NAVCUT_FACE);
    AddFn add = (AddFn)Detour(CO_NAVCUT_ADD);
    CHECK(face != NULL && add != NULL && Detour(CO_NAVCUT_STEP) != NULL);
    if (face != NULL && add != NULL && Detour(CO_NAVCUT_STEP) != NULL)
    {
        Construct(&s);
        E.faceSegments = 5; E.faceEvents = 5; E.perStep = 4;
        face(&s, &g_instance, kUp, 1, 0x1234u, &g_vertices);
        CHECK(E.faceCalls == 1 && E.faceState == &s && E.faceInstance == &g_instance && E.faceVertices == &g_vertices && E.faceKey == 0x1234u &&
            E.faceFlip == 1 && E.faceUp == kUp);
        for (int i = 0; i < 100; i++) CHECK(add(&s, points, 8, 3) == 1);
        CHECK(E.addCalls == 100 && Get(&s, CO_NAVCUT_SEGMENTS) == 805);
        CHECK(Sweep(&s) == 1);
        CHECK(E.stepCalls == 805 && Get(&s, CO_NAVCUT_EDGES) == 3220 && E.faceCalls == 1 && E.heapCalls == 1 && E.resetCalls == 0);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 0 && seen.hidden == 0 && seen.partial == 0 && seen.leftOut == 0 && seen.largest == 3220);
    }

    Reset("the segment budget: a silhouette that would pass it stays out, one that reaches it goes in");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        Construct(&s);
        E.faceSegments = 4; E.faceEvents = 4; E.perStep = 2;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices);
        CHECK(add(&s, points, (int32_t)CO_NAVCUT_BUDGET - 4 - 8, 0) == 1);
        CHECK(add(&s, points, 9, 0) == 1 && E.addCalls == 1);       /* 9 would pass the budget: left out, the answer still 1 */
        CHECK(add(&s, points, 8, 0) == 1 && E.addCalls == 2);       /* 8 reach the budget: in */
        CHECK(Get(&s, CO_NAVCUT_SEGMENTS) == CO_NAVCUT_BUDGET);
        CHECK(add(&s, points, 1, 0) == 1 && add(&s, points, 6, 0) == 1 && E.addCalls == 2);
        CHECK(add(&s, points, 0x7FFFFFFF, 0) == 1 && E.addCalls == 2); /* no wrap of segments + count */
        CHECK(add(&s, points, -5, 0) == 1 && E.addCalls == 3);         /* the engine's own business */
        co_navcut_seen(&seen);
        CHECK(seen.partial == 1 && seen.leftOut == 4);
        CHECK(Sweep(&s) == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 0 && seen.hidden == 0);
        E.faceSegments = 4; /* the next face on this thread counts anew */
        face(&s, &g_instance, kUp, 0, 2, &g_vertices);
        CHECK(add(&s, points, (int32_t)CO_NAVCUT_BUDGET, 0) == 1);
        co_navcut_seen(&seen);
        CHECK(seen.partial == 2 && seen.leftOut == 5);
    }

    Reset("run 4's cut: 5599 segments' worth of half-edges stops at the limit and the face is done again alone");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX); /* the budget out of the way: the step's guard alone */
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        Construct(&s);
        const float origin[2] = {2440.0f, 3259.0f};
        E.faceSegments = 11; E.faceEvents = 11; E.perStep = 2; E.perStepAlone = 2; E.facesOnFirst = 1; E.own = 7;
        face(&s, &g_instance, kUp, 1, 0x00B502D4u, &g_vertices);
        memcpy(s.b + CO_NAVCUT_ORIGIN, origin, 8);
        for (int i = 0; i < 635; i++) add(&s, points, 8, 1);
        add(&s, points, 508, 1);
        CHECK(Get(&s, CO_NAVCUT_SEGMENTS) == 5599);
        memset(&E.faceState, 0, sizeof E.faceState); E.faceUp = NULL; memset(E.faceUpBytes, 0, 16);
        CHECK(Sweep(&s) == 1);
        /* stopped after step 4097 (8194 half-edges), then the face's 11 events */
        CHECK(E.faceCalls == 2 && E.stepCalls == 4097 + 11 && Get(&s, CO_NAVCUT_EDGES) == 22 && Get(&s, CO_NAVCUT_SEGMENTS) == 11);
        CHECK(E.faceState == &s && E.faceInstance == &g_instance && E.faceVertices == &g_vertices && E.faceKey == 0x00B502D4u && E.faceFlip == 1);
        CHECK(memcmp(E.faceUpBytes, kUp, 16) == 0 && ((uintptr_t)E.faceUp & 15) == 0);
        CHECK(E.heapCalls == 2 && E.heapQueue == s.b + CO_NAVCUT_QUEUE && E.resetCalls == 0);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 1 && seen.hidden == 0 && seen.largest == 8194 && seen.worstEdges == 8194 && seen.worstSegments == 5599);
        CHECK(seen.worstAt[0] == 2440.0f && seen.worstAt[1] == 3259.0f);
        /* the stopped sweep held a segment in the status; the face's own sweep began on an empty one and left 1 polygon */
        CHECK(E.staleStatus == 0 && Get16(&s, STATUS) == 0xFFFFu && Get16(&s, STATUS + 2) == 0xFFFFu && Get(&s, CO_NAVCUT_FACES) == 1);
        CHECK(seen.againEnded == 1 && seen.againNone == 0 && seen.againMany == 0 && seen.aloneEnded == 0 && seen.aloneNone == 0 && seen.aloneMany == 0);
        /* the same face cut again and ending as 2 polygons is told apart */
        E.facesOnFirst = 2;
        face(&s, &g_instance, kUp, 1, 0x00B502D4u, &g_vertices);
        add(&s, points, 5000, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 2 && seen.againEnded == 2 && seen.againMany == 1 && seen.againNone == 0);
        CHECK(seen.oddFaces == 2 && seen.oddEdges == 22 && seen.oddOwn == 7);
        /* faces with no silhouette are the engine's own measure; one that took a silhouette and fitted is neither */
        face(&s, &g_instance, kUp, 1, 1, &g_vertices);
        CHECK(Sweep(&s) == 1);
        E.facesOnFirst = 1;
        face(&s, &g_instance, kUp, 1, 2, &g_vertices);
        CHECK(Sweep(&s) == 1);
        face(&s, &g_instance, kUp, 1, 3, &g_vertices);
        add(&s, points, 8, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 2 && seen.againEnded == 2 && seen.againMany == 1 && seen.aloneEnded == 2 && seen.aloneMany == 1 && seen.aloneNone == 0);
        /* a face alone that ends with no polygon is its own count, in both pairs */
        E.facesOnFirst = 0;
        face(&s, &g_instance, kUp, 1, 4, &g_vertices);
        CHECK(Sweep(&s) == 1);
        face(&s, &g_instance, kUp, 1, 5, &g_vertices);
        add(&s, points, 5000, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_seen(&seen);
        CHECK(seen.aloneEnded == 3 && seen.aloneMany == 1 && seen.aloneNone == 1 && seen.againEnded == 3 && seen.againMany == 1 && seen.againNone == 1);
        CHECK(seen.oddFaces == 0 && seen.oddEdges == 22 && seen.oddOwn == 7);
        /* a stop west and south of the origin keeps its signs, and its four numbers are one cut's */
        const float west[2] = {-3771.0f, -2740.0f};
        E.facesOnFirst = 1;
        face(&s, &g_instance, kUp, 1, 6, &g_vertices);
        memcpy(s.b + CO_NAVCUT_ORIGIN, west, 8);
        add(&s, points, 4200, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_seen(&seen);
        CHECK(seen.worstEdges == 8194 && seen.worstSegments == 4211 && seen.worstAt[0] == -3771.0f && seen.worstAt[1] == -2740.0f);
    }

    Reset("exactly 8192 half-edges is a whole cut");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        Construct(&s);
        E.faceSegments = 4; E.faceEvents = 4; E.perStep = 2;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices);
        add(&s, points, 4092, 0);
        CHECK(Sweep(&s) == 1 && Get(&s, CO_NAVCUT_EDGES) == 8192 && E.faceCalls == 1 && E.stepCalls == 4096);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 0 && seen.hidden == 0 && seen.largest == 8192);
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* one more event: stopped */
        add(&s, points, 4093, 0);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 3);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 1);
    }

    Reset("a face that passes the limit alone is reset, and the loop ends");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        Construct(&s);
        E.faceSegments = 3; E.faceEvents = 3; E.perStep = 5000; E.perStepAlone = 5000;
        face(&s, &g_instance, kUp, 0, 9, &g_vertices);
        CHECK(Sweep(&s) == 1);
        CHECK(E.faceCalls == 2 && E.resetCalls == 1 && Get(&s, CO_NAVCUT_EDGES) == 0 && Get(&s, QUEUE_SIZE) == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 1 && seen.hidden == 1 && seen.againEnded == 1 && seen.againNone == 1 && seen.againMany == 0 && seen.oddFaces == 0);
        CHECK(Get16(&s, STATUS) == 0xFFFFu && Get16(&s, STATUS + 2) == 0xFFFFu); /* the second stop empties it too */
        E.perStepAlone = 1; E.aloneLater = 2; /* the next face has its own second chance */
        face(&s, &g_instance, kUp, 0, 10, &g_vertices);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 4 && E.resetCalls == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 2 && seen.hidden == 1);
    }

    Reset("a state whose face this thread did not add is reset, never given another state's face");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE);
    if (face != NULL)
    {
        State other;
        Construct(&s); Construct(&other);
        E.faceSegments = 3; E.faceEvents = 3; E.perStep = 5000;
        face(&other, &g_instance, kUp, 0, 9, &g_vertices);
        Set(&s, QUEUE_SIZE, 4);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 1 && E.resetCalls == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 0 && seen.hidden == 1);
    }

    Reset("a cut that starts with no face is never given the face cut before in the same state");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE);
    KeyFn key = (KeyFn)Detour(CO_NAVCUT_KEY);
    CHECK(key != NULL);
    if (face != NULL && key != NULL)
    {
        Construct(&s);
        E.faceSegments = 3; E.faceEvents = 3; E.perStep = 1;
        face(&s, &g_instance, kUp, 0, 9, &g_vertices);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 1);
        /* the sweep ended: the face is forgotten even when the next cut's start is not seen */
        EngineReset(&s); E.fresh = 1;
        Set(&s, QUEUE_SIZE, 4); E.perStep = 5000;
        CHECK(Sweep(&s) == 1 && E.faceCalls == 1 && E.resetCalls == 2);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 0 && seen.hidden == 1);
        /* a face whose sweep never ran, then the start with no face (0x133C620) and silhouettes over the limit */
        E.perStep = 1;
        face(&s, &g_instance, kUp, 0, 10, &g_vertices);
        key(&s, 0x55u);
        CHECK(E.keyCalls == 1 && E.keyKey == 0x55u && Get(&s, QUEUE_SIZE) == 1);
        Set(&s, QUEUE_SIZE, 4); E.perStep = 5000;
        CHECK(Sweep(&s) == 1 && E.faceCalls == 2 && E.resetCalls == 3);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 0 && seen.hidden == 2 && seen.againEnded == 0);
    }

    Reset("no face added on this thread at all: reset");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    {
        Construct(&s);
        E.perStep = 9000;
        Set(&s, QUEUE_SIZE, 3);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 0 && E.resetCalls == 1);
    }

    Reset("vertices, polygons or segments over 8192 stop a cut too");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE);
    if (face != NULL)
    {
        for (int which = 0; which < 3; which++)
        {
            Construct(&s);
            memset(&E, 0, sizeof E);
            E.faceSegments = 3; E.faceEvents = 3; E.perStep = 1; E.perStepAlone = 1;
            face(&s, &g_instance, kUp, 0, 9, &g_vertices);
            if (which == 0) E.vertsPerStep = 8193;
            if (which == 1) E.facesPerStep = 8193;
            if (which == 2) E.segmentsPerStep = 8190;
            StepFn step = (StepFn)Detour(CO_NAVCUT_STEP);
            step(&s);
            E.vertsPerStep = E.facesPerStep = E.segmentsPerStep = 0;
            CHECK(E.faceCalls == 2 && Get(&s, CO_NAVCUT_VERTICES) == 0 && Get(&s, CO_NAVCUT_FACES) == 0 && Get(&s, CO_NAVCUT_SEGMENTS) == 3);
            CHECK(Sweep(&s) == 1);
        }
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 3);
    }

    Reset("each thread's face is its own");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE);
    if (face != NULL)
    {
        Construct(&s);
        E.faceSegments = 3; E.faceEvents = 3; E.perStep = 5000; E.perStepAlone = 1; E.aloneLater = 1;
        face(&s, &g_instance, kUp, 1, 0x42u, &g_vertices);
        HANDLE t = CreateThread(NULL, 0, OtherThread, NULL, 0, NULL);
        CHECK(t != NULL);
        if (t != NULL) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
        CHECK(E.faceCalls == 2 && E.faceKey == 0x777u);
        Set(&s, QUEUE_SIZE, 4);
        CHECK(Sweep(&s) == 1);
        CHECK(E.faceCalls == 3 && E.faceKey == 0x42u && E.faceState == &s && E.faceInstance == &g_instance && E.faceFlip == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 1 && seen.hidden == 0);
    }

    Reset("a research build's lower limits, clamped");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        co_navcut_limits(64, 40);
        Construct(&s);
        E.faceSegments = 4; E.faceEvents = 4; E.perStep = 2; E.perStepAlone = 2;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices);
        CHECK(add(&s, points, 36, 0) == 1 && add(&s, points, 1, 0) == 1 && E.addCalls == 1);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 2); /* 40 events x 2 > 64 */
        co_navcut_limits(1, 1); /* not under 16 */
        E.faceSegments = 0; E.faceEvents = 0;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices);
        CHECK(add(&s, points, 16, 0) == 1 && E.addCalls == 2 && add(&s, points, 1, 0) == 1 && E.addCalls == 2);
        co_navcut_limits(1u << 20, 1u << 20); /* not over 8192 */
        E.faceEvents = 1;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices);
        CHECK(add(&s, points, 8192, 0) == 1 && E.addCalls == 3 && add(&s, points, 1, 0) == 1 && E.addCalls == 3);
        E.perStep = 1; E.perStepAlone = 1;
        CHECK(Sweep(&s) == 1 && E.faceCalls == 5 && Get(&s, CO_NAVCUT_EDGES) == 1); /* 8193 half-edges: stopped */
    }

    Reset("the release budget holds a cut of 4 half-edges a segment under the limit");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        Construct(&s);
        E.faceSegments = 4; E.faceEvents = 4; E.perStep = 4;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices);
        for (int i = 0; i < 1400; i++) add(&s, points, 4, 0); /* run 4's 5599 segments and more, four a silhouette */
        co_navcut_seen(&seen);
        CHECK(Get(&s, CO_NAVCUT_SEGMENTS) == CO_NAVCUT_BUDGET && seen.partial == 1 && seen.leftOut == 1400 - (CO_NAVCUT_BUDGET - 4) / 4);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 1 && Get(&s, CO_NAVCUT_EDGES) <= CO_NAVCUT_MAX);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 0 && seen.hidden == 0 && seen.largest == 4 * CO_NAVCUT_BUDGET);
    }

    Reset("research: one face alone and done again is compared by its own edges, not by its key");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        CoNavCutStudy study;
        Construct(&s);
        E.faceSegments = 5; E.faceEvents = 5; E.perStep = 2; E.perStepAlone = 2; E.facesOnFirst = 3; E.own = 5;
        memset(E.ownEdges, 0x11, sizeof E.ownEdges);
        face(&s, &g_instance, kUp, 0, 0x424u, &g_vertices); /* alone: 3 polygons */
        CHECK(Sweep(&s) == 1);
        co_navcut_study(&study);
        CHECK(study.same == 0 && study.differ == 0);
        face(&s, &g_instance, kUp, 0, 0x424u, &g_vertices); /* the same face stopped and done again: 3 polygons */
        add(&s, points, 5000, 1);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 3);
        co_navcut_study(&study);
        CHECK(study.same == 1 && study.differ == 0);
        E.ownEdges[0x30 + 0x21] ^= 1; /* the same key in another section: other points, so another face */
        E.facesOnFirst = 2;
        face(&s, &g_instance, kUp, 0, 0x424u, &g_vertices);
        add(&s, points, 5000, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_study(&study);
        CHECK(study.same == 1 && study.differ == 0);
        face(&s, &g_instance, kUp, 0, 0x424u, &g_vertices); /* that face alone after its redo: 2 and 2 */
        CHECK(Sweep(&s) == 1);
        co_navcut_study(&study);
        CHECK(study.same == 2 && study.differ == 0);
        E.ownEdges[0x30 + 0x21] ^= 1; /* the first face again, done again as 2 where alone it was 3 */
        face(&s, &g_instance, kUp, 0, 0x424u, &g_vertices);
        add(&s, points, 5000, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_study(&study);
        CHECK(study.same == 2 && study.differ == 1 && study.differAlone == 3 && study.differAgain == 2 && study.differOwn == 5);
        E.ownEdges[0x22] ^= 1; /* a byte the face add does not write is not part of the face */
        E.facesOnFirst = 3;
        face(&s, &g_instance, kUp, 0, 0x424u, &g_vertices);
        add(&s, points, 5000, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_study(&study);
        CHECK(study.same == 3 && study.differ == 1);
        E.ownEdges[0x22] ^= 1;
        /* a face stopped twice has the reset's 0 polygons, which is no sweep's end: not compared, and the next is */
        E.facesOnFirst = 0;
        face(&s, &g_instance, kUp, 0, 0x425u, &g_vertices);
        CHECK(Sweep(&s) == 1);
        E.perStepAlone = 5000; E.aloneLater = E.faceCalls;
        face(&s, &g_instance, kUp, 0, 0x425u, &g_vertices);
        add(&s, points, 5000, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_seen(&seen);
        co_navcut_study(&study);
        CHECK(seen.hidden == 1 && study.same == 3 && study.differ == 1);
        E.perStepAlone = 2; E.aloneLater = 0;
        face(&s, &g_instance, kUp, 0, 0x425u, &g_vertices);
        add(&s, points, 5000, 1);
        CHECK(Sweep(&s) == 1);
        co_navcut_study(&study);
        CHECK(study.same == 4 && study.differ == 1 && study.notKept == 0);
        /* more faces than rows: the rest are counted, none takes another's row */
        E.facesOnFirst = 1;
        for (uint32_t k = 0; k < 1280000; k++) { face(&s, &g_instance, kUp, 0, 0x10000u + k, &g_vertices); Sweep(&s); }
        co_navcut_study(&study);
        CHECK(study.notKept >= 1280000 - 1048576 && study.notKept < 1280000 - 524288 && study.same == 4 && study.differ == 1);
        /* a flight's faces alone leave the table open: a cut done again after them still finds its row */
        Reset("research: the table holds a flight's faces alone and the cuts done again after them");
        CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
        co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
        Construct(&s);
        E.faceSegments = 5; E.faceEvents = 5; E.perStep = 2; E.perStepAlone = 2; E.facesOnFirst = 1; E.own = 5;
        for (uint32_t k = 0; k < 150000; k++) { face(&s, &g_instance, kUp, 0, 0x10000u + k, &g_vertices); Sweep(&s); }
        co_navcut_study(&study);
        CHECK(study.notKept == 0 && study.same == 0 && study.differ == 0);
        for (uint32_t k = 0; k < 150000; k += 15) { face(&s, &g_instance, kUp, 0, 0x10000u + k, &g_vertices); add(&s, points, 5000, 1); Sweep(&s); }
        co_navcut_study(&study);
        CHECK(study.notKept == 0 && study.same == 10000 && study.differ == 0);
    }

    Reset("research: whole cuts by the segments they began with");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        CoNavCutStudy study;
        Construct(&s);
        E.faceSegments = 11; E.faceEvents = 11; E.perStep = 5;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* 511 segments: under every band */
        add(&s, points, 500, 0);
        CHECK(Sweep(&s) == 1);
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* 512 segments at 5 a segment */
        add(&s, points, 501, 0);
        CHECK(Sweep(&s) == 1);
        E.perStep = 1; E.faceSegments = 599; E.faceEvents = 1373;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* 600 segments, 1374 half-edges: 2.29, not over it */
        add(&s, points, 1, 0);
        CHECK(Sweep(&s) == 1 && Get(&s, CO_NAVCUT_EDGES) == 1374);
        E.perStep = 3; E.faceSegments = 11; E.faceEvents = 11;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* 1024 segments at 3 */
        add(&s, points, 1013, 0);
        CHECK(Sweep(&s) == 1);
        E.perStep = 2;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* 2047 segments at 2: still the middle band */
        add(&s, points, 2036, 0);
        CHECK(Sweep(&s) == 1);
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* 2048 at 2 */
        add(&s, points, 2037, 0);
        CHECK(Sweep(&s) == 1);
        E.perStep = 5;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* 3000 at 5: stopped, so no rate */
        add(&s, points, 2989, 0);
        CHECK(Sweep(&s) == 1);
        E.faceSegments = 600; E.faceEvents = 600;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices); /* a face alone, of 600 edges even, is no cut by silhouettes */
        CHECK(Sweep(&s) == 1);
        co_navcut_study(&study);
        CHECK(study.ended[0] == 2 && study.ended[1] == 2 && study.ended[2] == 1);
        CHECK(study.stopped[0] == 0 && study.stopped[1] == 0 && study.stopped[2] == 1);
        CHECK(study.steep[0] == 1 && study.steep[1] == 1 && study.steep[2] == 0);
        CHECK(study.most[0] == 500 && study.most[1] == 300 && study.most[2] == 200);
    }

    static const uint32_t kPinned[] = {CO_NAVCUT_FACE + 3, 0x133C67Au + 1, CO_NAVCUT_ADD + 2, CO_NAVCUT_STEP + 12, CO_NAVCUT_HEAP + 7,
        CO_NAVCUT_RESET + 4, 0x133CA40u + 12, 0x133BB30u + 20, 0x133BB6Fu + 2, 0x13408B8u + 2, 0x1341D3Bu + 5, 0x1341F3Cu + 12, 0x133E460u + 4,
        CO_NAVCUT_KEY + 13, 0x133D135u + 10, 0x13405DBu + 4, 0x13419A3u + 3, 0x133BA21u + 6, 0xCB20B1u + 5, 0xC244B9u + 6, 0x134303Bu + 6,
        0x134327Cu + 4, 0x1343476u + 2, 0x13436A0u + 2};
    for (size_t i = 0; i < sizeof kPinned / sizeof kPinned[0]; i++)
    {
        Reset("a changed byte in the cutter's code: nothing hooked");
        g_exe[kPinned[i]] ^= 0xFF;
        CHECK(co_navcut_install((uintptr_t)g_exe) != NULL);
        CHECK(Detour(CO_NAVCUT_FACE) == NULL && Detour(CO_NAVCUT_KEY) == NULL && Detour(CO_NAVCUT_ADD) == NULL && Detour(CO_NAVCUT_STEP) == NULL);
    }

    Reset("the face add's hook refused: nothing else hooked");
    g_fake.noHook = (uintptr_t)g_exe + CO_NAVCUT_FACE - (uintptr_t)g_fake.image;
    CHECK(co_navcut_install((uintptr_t)g_exe) != NULL);
    CHECK(Detour(CO_NAVCUT_KEY) == NULL && Detour(CO_NAVCUT_ADD) == NULL && Detour(CO_NAVCUT_STEP) == NULL);

    Reset("the hook of the start with no face refused: no add or step hook");
    g_fake.noHook = (uintptr_t)g_exe + CO_NAVCUT_KEY - (uintptr_t)g_fake.image;
    CHECK(co_navcut_install((uintptr_t)g_exe) != NULL);
    CHECK(Detour(CO_NAVCUT_ADD) == NULL && Detour(CO_NAVCUT_STEP) == NULL);

    Reset("the silhouette add's hook refused: no step hook");
    g_fake.noHook = (uintptr_t)g_exe + CO_NAVCUT_ADD - (uintptr_t)g_fake.image;
    CHECK(co_navcut_install((uintptr_t)g_exe) != NULL);
    CHECK(Detour(CO_NAVCUT_STEP) == NULL);

    Reset("the step's hook refused: said so");
    g_fake.noHook = (uintptr_t)g_exe + CO_NAVCUT_STEP - (uintptr_t)g_fake.image;
    CHECK(co_navcut_install((uintptr_t)g_exe) != NULL);

    Reset("no engine image");
    CHECK(co_navcut_install(0) != NULL);

    if (g_failed) { printf("navcut: %d check(s) failed\n", g_failed); return 1; }
    printf("navcut: all checks passed\n");
    return 0;
}
