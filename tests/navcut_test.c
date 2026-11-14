/* The navmesh face cutter's guard (co_navcut.h) on a fake exe. The engine's stand-in keeps the cutter state's counts
 * where the game has them and runs the game's order: face add, silhouette adds, heap build, steps until the queue is
 * 1, then the first signed 14-bit read (0x1341BF0), which is where a cut over 8192 half-edges goes wrong. */
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
} E;

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
    Set(s, QUEUE_SIZE, Get(s, QUEUE_SIZE) - 1);
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
    memset(&E, 0, sizeof E);
    co_navcut_test_calls(EngineHeap, EngineReset);
}

typedef void (*FaceFn)(void*, void*, const void*, uint8_t, uint32_t, void*);
typedef uint32_t (*AddFn)(void*, const void*, int32_t, int32_t);
typedef uint8_t (*StepFn)(void*);
static void* Detour(uint32_t rva) { return fake_host_detour((uintptr_t)g_exe + rva - (uintptr_t)g_fake.image); }

/* The cut as 0x133BA40 runs it once the face and the silhouettes are in. Returns 1 when the state that reaches the
 * first signed read fits 14 bits and the events were in heap order at every step; 0 when not, or when the loop
 * does not end. */
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
    return ordered && Get(s, CO_NAVCUT_EDGES) <= 8192 && Get(s, CO_NAVCUT_VERTICES) <= 8192 && Get(s, CO_NAVCUT_FACES) <= 8192;
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
        memset(&s, 0, sizeof s);
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
        memset(&s, 0, sizeof s);
        E.faceSegments = 4; E.faceEvents = 4; E.perStep = 2;
        face(&s, &g_instance, kUp, 0, 1, &g_vertices);
        CHECK(add(&s, points, (int32_t)CO_NAVCUT_BUDGET - 4 - 8, 0) == 1);
        CHECK(add(&s, points, 9, 0) == 1 && E.addCalls == 1);       /* 3576 + 9 > 3584: left out, the answer still 1 */
        CHECK(add(&s, points, 8, 0) == 1 && E.addCalls == 2);       /* 3576 + 8 = the budget: in */
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
        memset(&s, 0, sizeof s);
        const float origin[2] = {2440.0f, 3259.0f};
        E.faceSegments = 11; E.faceEvents = 11; E.perStep = 2; E.perStepAlone = 2;
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
    }

    Reset("exactly 8192 half-edges is a whole cut");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    face = (FaceFn)Detour(CO_NAVCUT_FACE); add = (AddFn)Detour(CO_NAVCUT_ADD);
    if (face != NULL && add != NULL)
    {
        memset(&s, 0, sizeof s);
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
        memset(&s, 0, sizeof s);
        E.faceSegments = 3; E.faceEvents = 3; E.perStep = 5000; E.perStepAlone = 5000;
        face(&s, &g_instance, kUp, 0, 9, &g_vertices);
        CHECK(Sweep(&s) == 1);
        CHECK(E.faceCalls == 2 && E.resetCalls == 1 && Get(&s, CO_NAVCUT_EDGES) == 0 && Get(&s, QUEUE_SIZE) == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 1 && seen.hidden == 1);
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
        memset(&s, 0, sizeof s); memset(&other, 0, sizeof other);
        E.faceSegments = 3; E.faceEvents = 3; E.perStep = 5000;
        face(&other, &g_instance, kUp, 0, 9, &g_vertices);
        Set(&s, QUEUE_SIZE, 4);
        CHECK(Sweep(&s) == 1 && E.faceCalls == 1 && E.resetCalls == 1);
        co_navcut_seen(&seen);
        CHECK(seen.uncut == 0 && seen.hidden == 1);
    }

    Reset("no face added on this thread at all: reset");
    CHECK(co_navcut_install((uintptr_t)g_exe) == NULL);
    co_navcut_limits(CO_NAVCUT_MAX, CO_NAVCUT_MAX);
    {
        memset(&s, 0, sizeof s);
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
            memset(&s, 0, sizeof s);
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
        memset(&s, 0, sizeof s);
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
        memset(&s, 0, sizeof s);
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

    static const uint32_t kPinned[] = {CO_NAVCUT_FACE + 3, 0x133C67Au + 1, CO_NAVCUT_ADD + 2, CO_NAVCUT_STEP + 12, CO_NAVCUT_HEAP + 7,
        CO_NAVCUT_RESET + 4, 0x133CA40u + 12, 0x133BB30u + 20, 0x133BB6Fu + 2, 0x13408B8u + 2, 0x1341D3Bu + 5, 0x1341F3Cu + 12, 0x133E460u + 4};
    for (size_t i = 0; i < sizeof kPinned / sizeof kPinned[0]; i++)
    {
        Reset("a changed byte in the cutter's code: nothing hooked");
        g_exe[kPinned[i]] ^= 0xFF;
        CHECK(co_navcut_install((uintptr_t)g_exe) != NULL);
        CHECK(Detour(CO_NAVCUT_FACE) == NULL && Detour(CO_NAVCUT_ADD) == NULL && Detour(CO_NAVCUT_STEP) == NULL);
    }

    Reset("the face add's hook refused: nothing else hooked");
    g_fake.noHook = (uintptr_t)g_exe + CO_NAVCUT_FACE - (uintptr_t)g_fake.image;
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
