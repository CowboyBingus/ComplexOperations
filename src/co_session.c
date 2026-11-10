/* Eight players: the session's peer lists, first step. The host and client synchronizers lie packed inside the game's
 * session context, every per-peer array with four entries and the next array right behind it. The arrays that will
 * grow to eight stay where they are; this moves the neighbours they would overrun (index flags, party records, ready
 * peers, list 3 with its count, peer values, hostability) to an extension block near the context and gives every code
 * site that addresses them the new distance. Sites that cannot hold a 32-bit distance jump to a stub that can. */
#include <string.h>
#include <windows.h>

#include "co.h"

typedef struct CoSessionArray { uint32_t old, place; } CoSessionArray;
typedef struct CoSessionSite { uint32_t rva; uint8_t at; uint32_t old; uint8_t array, shift; uint8_t pin[8]; } CoSessionSite;
typedef struct CoSessionStub
{
    uint32_t rva;
    uint8_t size, length, at;
    uint32_t old;
    uint8_t array, shift;
    uint8_t jumpAt;   /* a conditional jump in the stub: where its rel32 is, 0 for none */
    uint32_t jumpTo;  /* and the rva it leads to */
    uint8_t replaced[12], code[16];
} CoSessionStub;
#include "co_session_sites.h"

#define CO_SESSION_SITES (sizeof kCoSessionSites / sizeof kCoSessionSites[0])
#define CO_SESSION_STUBS (sizeof kCoSessionStubs / sizeof kCoSessionStubs[0])
#define CO_SESSION_INIT 0x134D740u      /* sets up the session context the global points at */
#define CO_SESSION_GLOBAL 0x347CEF0u
#define CO_SESSION_READY 0xB388u        /* a byte in the context: set once the init has run */
#define CO_EXTENSION 0x1000u
#define CO_STUB_ROOM 0x20u
static const char kSessionInitPin[] = "48 89 5C 24 18 48 89 4C 24 08 55 56 57 48 8B EC 48 83 EC 30 48 8B 35";

typedef uint64_t (*SessionInitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
static SessionInitFn g_sessionInit;
/* Where each moved array ended in the context, in kCoSessionArrays' order. */
static const uint32_t kOldEnd[] = {0x15AB8u, 0x1634Cu, 0x16300u, 0x16390u, 0x16458u, 0x16498u, 0x161B8u};
static uintptr_t g_game, g_context, g_extension, g_stubs;
static int g_state; /* 0 not moved, 1 moved, -1 left as the game has it */

/* The value a site gets: its own plus the array's move, in the site's units. 0 when it cannot hold it. */
static int NewValue(uint32_t old, uint8_t array, uint8_t shift, uint32_t* value)
{
    int64_t move = (int64_t)(g_extension + kCoSessionArrays[array].place) - (int64_t)(g_context + kCoSessionArrays[array].old);
    if (move & ((1 << shift) - 1)) return 0;
    int64_t v = (int64_t)old + move / (1 << shift);
    *value = (uint32_t)(int32_t)v;
    return v >= INT32_MIN && v <= INT32_MAX;
}

static void Undo(size_t sites, size_t stubs)
{
    for (size_t j = 0; j < stubs; j++) g_coApi->write(g_game + kCoSessionStubs[j].rva, kCoSessionStubs[j].replaced, kCoSessionStubs[j].size);
    for (size_t j = 0; j < sites; j++) g_coApi->write(g_game + kCoSessionSites[j].rva + kCoSessionSites[j].at, &kCoSessionSites[j].old, 4);
}

static void Release(void)
{
    if (g_extension) VirtualFree((void*)g_extension, 0, MEM_RELEASE);
    if (g_stubs) VirtualFree((void*)g_stubs, 0, MEM_RELEASE);
    g_extension = g_stubs = g_context = 0;
}

static void Move(uintptr_t context)
{
    for (size_t i = 0; i < CO_SESSION_SITES; i++)
    {
        const CoSessionSite* s = &kCoSessionSites[i];
        uint8_t bytes[12];
        if (g_coApi->read(g_game + s->rva, bytes, s->at + 4u) != 0 || memcmp(bytes, s->pin, s->at) != 0 || memcmp(bytes + s->at, &s->old, 4) != 0)
        { co_log("session: the code at game+0x%X is not the reviewed one; the session lists stay as they are", s->rva); return; }
    }
    for (size_t i = 0; i < CO_SESSION_STUBS; i++)
    {
        const CoSessionStub* s = &kCoSessionStubs[i];
        uint8_t bytes[12];
        if (g_coApi->read(g_game + s->rva, bytes, s->size) != 0 || memcmp(bytes, s->replaced, s->size) != 0)
        { co_log("session: the code at game+0x%X is not the reviewed one; the session lists stay as they are", s->rva); return; }
    }
    g_context = context;
    g_extension = co_alloc_near(context, CO_EXTENSION);
    g_stubs = co_alloc_near(g_game + 0x1000000u, 0x1000u);
    if (g_extension == 0 || g_stubs == 0)
    { Release(); co_log("session: no free memory near the session context or the game's code; the session lists stay as they are"); return; }
    /* The stubs first, whole and executable, before any jump leads to them. */
    for (size_t i = 0; i < CO_SESSION_STUBS; i++)
    {
        const CoSessionStub* s = &kCoSessionStubs[i];
        uint8_t* stub = (uint8_t*)(g_stubs + i * CO_STUB_ROOM);
        uint32_t value;
        if (!NewValue(s->old, s->array, s->shift, &value))
        { Release(); co_log("session: the extension is out of reach for game+0x%X; the session lists stay as they are", s->rva); return; }
        memcpy(stub, s->code, s->length);
        memcpy(stub + s->at, &value, 4);
        if (s->jumpAt)
        {
            int32_t to = (int32_t)((int64_t)(g_game + s->jumpTo) - (int64_t)((uintptr_t)stub + s->jumpAt + 4u));
            memcpy(stub + s->jumpAt, &to, 4);
        }
        stub[s->length] = 0xE9;
        int32_t back = (int32_t)((int64_t)(g_game + s->rva + s->size) - (int64_t)((uintptr_t)stub + s->length + 5u));
        memcpy(stub + s->length + 1, &back, 4);
    }
    DWORD before;
    if (!VirtualProtect((void*)g_stubs, 0x1000u, PAGE_EXECUTE_READ, &before))
    { Release(); co_log("session: the stubs could not be made executable; the session lists stay as they are"); return; }
    for (size_t i = 0; i < CO_SESSION_SITES; i++)
    {
        const CoSessionSite* s = &kCoSessionSites[i];
        uint32_t value;
        /* The game cannot run on a mix: the sites written so far go back. */
        if (!NewValue(s->old, s->array, s->shift, &value) || g_coApi->write(g_game + s->rva + s->at, &value, 4) != 0)
        { Undo(i, 0); Release(); co_log("session: game+0x%X could not be written; the session lists stay as they are", s->rva); return; }
    }
    for (size_t i = 0; i < CO_SESSION_STUBS; i++)
    {
        const CoSessionStub* s = &kCoSessionStubs[i];
        uint8_t jump[12];
        int32_t to = (int32_t)((int64_t)(g_stubs + i * CO_STUB_ROOM) - (int64_t)(g_game + s->rva + 5u));
        memset(jump, 0x90, sizeof jump);
        jump[0] = 0xE9;
        memcpy(jump + 1, &to, 4);
        if (g_coApi->write(g_game + s->rva, jump, s->size) != 0)
        { Undo(CO_SESSION_SITES, i); Release(); co_log("session: game+0x%X could not be written; the session lists stay as they are", s->rva); return; }
    }
    g_state = 1;
    co_log("session: %u arrays of the session lists moved (%u code sites changed, %u through stubs)", (unsigned)(sizeof kCoSessionArrays / sizeof kCoSessionArrays[0]),
           (unsigned)(CO_SESSION_SITES + CO_SESSION_STUBS), (unsigned)CO_SESSION_STUBS);
}

#ifdef CO_CANARY
/* A check build: the moved arrays' old places hold a pattern; code that still writes there is a site the lists miss. */
static uint8_t g_told[sizeof kOldEnd / sizeof kOldEnd[0]];
void co_session_canary(int fill)
{
    static unsigned checks;
    if (g_state != 1) return;
    if (!fill && (++checks == 1 || checks % 20 == 0))
        co_log("session: CANARY checked %u times; first old place holds %02X", checks, *(uint8_t*)(g_context + kCoSessionArrays[0].old));
    for (size_t i = 0; i < sizeof kOldEnd / sizeof kOldEnd[0]; i++)
    {
        uint8_t* old = (uint8_t*)(g_context + kCoSessionArrays[i].old);
        uint32_t size = kOldEnd[i] - kCoSessionArrays[i].old;
        if (fill) { memset(old, 0xC5, size); g_told[i] = 0; continue; }
        for (uint32_t at = 0; at < size && !g_told[i]; at++)
            if (old[at] != 0xC5)
            {
                g_told[i] = 1;
                co_log("session: CANARY array %u +0x%X (context +0x%X) holds %02X", (unsigned)i, at, kCoSessionArrays[i].old + at, old[at]);
            }
    }
}
#endif

/* What the arrays held goes along, so the move may come at any time before a fifth peer exists. The old places are
 * zeroed: the lists that stay grow into them, and an empty entry is all zeros. */
static void CopyOld(void)
{
    for (size_t i = 0; i < sizeof kOldEnd / sizeof kOldEnd[0]; i++)
    {
        void* old = (void*)(g_context + kCoSessionArrays[i].old);
        memcpy((void*)(g_extension + kCoSessionArrays[i].place), old, kOldEnd[i] - kCoSessionArrays[i].old);
        memset(old, 0, kOldEnd[i] - kCoSessionArrays[i].old);
    }
}

/* Runs in the state constructor's hook and at the session init, whichever comes first with the context's global set.
 * The sites hold distances from one context; another one gets them written again. */
void co_session_state(void)
{
    uintptr_t context = 0;
    if (g_state == -1 || g_game == 0 || g_coApi->read(g_game + CO_SESSION_GLOBAL, &context, sizeof context) != 0 || context == 0) return;
    if (g_state == 1 && context != g_context) { co_counts_undo(); Undo(CO_SESSION_SITES, CO_SESSION_STUBS); Release(); g_state = 0; }
    if (g_state == 0)
    {
        g_state = -1;
        Move(context);
        if (g_state == 1) CopyOld();
#ifdef CO_CANARY
        co_session_canary(1);
#else
        if (g_state == 1) co_counts_apply(g_context, g_extension);
#endif
    }
}

/* An init that has not run yet starts from a context the game has zeroed; the extension is outside it. */
static uint64_t SessionInitDetour(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
    co_session_state();
    if (g_state == 1 && *(uint8_t*)(g_context + CO_SESSION_READY) == 0) memset((void*)g_extension, 0, CO_EXTENSION);
    uint64_t result = g_sessionInit(a1, a2, a3, a4);
    co_counts_session_init();
#ifdef CO_CANARY
    co_session_canary(0);
#endif
    return result;
}

void co_session_install(uintptr_t game)
{
    g_game = game;
    if (g_coApi->code_matches(game + CO_SESSION_INIT, kSessionInitPin) != 1)
    { g_state = -1; co_log("session: this is not the game code the mod was made for (a game update or another mod); the session lists stay as they are"); return; }
    if (g_coApi->hook(g_coMod, game + CO_SESSION_INIT, (void*)SessionInitDetour, (void**)&g_sessionInit) != 0)
    { g_state = -1; co_log("session: the session init could not be hooked; the session lists stay as they are"); }
}

#ifdef CO_TEST
void co_session_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_SESSION_SITES; i++)
    {
        memcpy(image + kCoSessionSites[i].rva, kCoSessionSites[i].pin, kCoSessionSites[i].at);
        memcpy(image + kCoSessionSites[i].rva + kCoSessionSites[i].at, &kCoSessionSites[i].old, 4);
    }
    for (size_t i = 0; i < CO_SESSION_STUBS; i++) memcpy(image + kCoSessionStubs[i].rva, kCoSessionStubs[i].replaced, kCoSessionStubs[i].size);
}

void co_session_test_reset(void)
{
    Release();
    g_game = 0;
    g_state = 0;
}

int co_session_test_state(void) { return g_state; }
uintptr_t co_session_test_extension(void) { return g_extension; }
uintptr_t co_session_test_stubs(void) { return g_stubs; }
unsigned co_session_test_sites(void) { return (unsigned)CO_SESSION_SITES; }
unsigned co_session_test_stub_count(void) { return (unsigned)CO_SESSION_STUBS; }
#endif
