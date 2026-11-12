/* The steering-wall query on an empty AI world slot (co_steer.h). */
#include <string.h>

#include <windows.h>

#include "co_steer.h"

/* The query's start, and its slot load: lea rbx, [rip -> 0x190D4A0]; shl rax, 7 (the id x 0x80); mov rbx, [rax+rbx]. */
static const char kQueryPin[] = "48 8B C4 44 89 48 20 4C 89 40 18 48 89 50 10 55 56 57 41 56 48 8D A8 A8 FC FF FF";
static const char kSlotPin[] = "48 8D 1D 20 25 13 01 48 C1 E0 07";
static const char kSlotLoadPin[] = "48 8B 1C 18";
#define SLOT_LEA 0x7DAF79u
#define SLOT_LOAD 0x7DAF99u

typedef uint32_t (*QueryFn)(uint32_t id, void* query, void* segments, uint32_t room);
static QueryFn g_query;
static uintptr_t g_exe;
static volatile LONG g_refused;

/* No segments, as the query answers for a world that has none (0x7DB10D..0x7DB112); the boundary job stores the count
 * and its readers loop over it (game 0x134A590, 0x134CB93). A slot emptied after this test still reaches the game. */
static uint32_t Query(uint32_t id, void* query, void* segments, uint32_t room)
{
    uintptr_t world = 0;
    if (id < CO_STEER_SLOT_COUNT) memcpy(&world, (const void*)(g_exe + CO_STEER_SLOTS + 0x80u * (uintptr_t)id), 8);
    if (world == 0)
    {
        if (InterlockedIncrement(&g_refused) == 1) co_log("complex operations: the steering query asked nav id %u, which has no AI world; it gets no segments", id);
        return 0;
    }
    return g_query(id, query, segments, room);
}

const char* co_steer_install(uintptr_t exe)
{
    if (g_query != NULL) return NULL;
    if (exe == 0 || g_coApi->code_matches(exe + CO_STEER_QUERY, kQueryPin) != 1 || g_coApi->code_matches(exe + SLOT_LEA, kSlotPin) != 1 ||
        g_coApi->code_matches(exe + SLOT_LOAD, kSlotLoadPin) != 1)
        return "the engine's steering query is not the code this mod was made for";
    g_exe = exe;
    if (g_coApi->hook(g_coMod, exe + CO_STEER_QUERY, (void*)Query, (void**)&g_query) != 0) { g_query = NULL; return "the engine's steering query could not be hooked"; }
    return NULL;
}

#ifdef CO_TEST
void co_steer_test_reset(void) { g_query = NULL; g_exe = 0; g_refused = 0; }
#endif
