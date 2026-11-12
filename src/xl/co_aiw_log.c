/* The nav world ids' log (co_aiw_log.h). Entry hooks, not interface-table swaps: helldivers2.exe calls the destroy
 * directly (0x77E506, 0x789AFA, 0x789B42), which a swap of 0x27CD490's slot would miss. */
#include "co_aiw_log.h"

#ifdef CO_RESEARCH
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>

/* Each pin covers the bytes a log line's meaning rests on: the entries and how the ring, records and slots are read. */
typedef struct { uint32_t rva; const char* hex; } Pin;
static const Pin kPins[] = {
    {CO_AIW_CREATE, "48 8B C4 4C 89 48 20 4C 89 40 18 55 56 41 55 41 57 48 8D A8 E8 FE FF FF 48 81 EC F8 01 00 00"},
    {0x78D3A8u, "48 8D 35 C1 D4 02 02"}, /* lea rsi, records */
    {0x78D3D9u, "8B 0D A5 54 BD 01 8B C1 83 E0 03 FF C1 89 0D 98 54 BD 01 44 8B BC 82 60 D4 90 01"}, /* the pop */
    {0x78DD9Cu, "41 8B C7"},             /* returns the id */
    {CO_AIW_DESTROY, "83 F9 04 0F 83 B2 03 00 00 4C 8B DC 56 48 83 EC 50"},
    {0x78F015u, "48 8D 1D 54 B8 02 02 8B F1 48 69 D6 B0 00 00 00"},
    {0x78F02Fu, "39 44 1A 1C"},          /* acts only on non-zero flags */
    {0x78F345u, "8B 0D AD DE 17 01 8B C1 83 E0 03"},
    {0x78F366u, "89 0D 8C DE 17 01 89 B4 83 60 D4 90 01"}, /* the push */
    {CO_AIW_QUERY, "48 8B C4 44 89 48 20 4C 89 40 18 48 89 50 10 55 56 57 41 56 48 8D A8 A8 FC FF FF"},
    {0x7DAF79u, "48 8D 1D 20 25 13 01 48 C1 E0 07"},
    {0x7DAF99u, "48 8B 1C 18"},
};

typedef uint32_t (*CreateFn)(uint64_t owner, uint64_t rdx, uint64_t settings, uint64_t r9);
typedef void (*DestroyFn)(uint32_t id);
typedef uint32_t (*QueryFn)(uint32_t id, void* query, void* segments, uint32_t room);

static struct
{
    uintptr_t exe, game;
    size_t exeSize, gameSize;
    CreateFn create;
    DestroyFn destroy;
    QueryFn query;
    volatile LONG seq, nullSeen;
    volatile LONG seenHead, seenTail;
    int said;
} g_aiw;

static uint32_t U32(uint32_t rva) { return *(volatile const uint32_t*)(g_aiw.exe + rva); }
static uint64_t U64(uint32_t rva) { return *(volatile const uint64_t*)(g_aiw.exe + rva); }

/* "game.dll+0x..." for the two images; any other module by its file name. */
static void Where(uintptr_t at, char* out, size_t size)
{
    if (g_aiw.game != 0 && at - g_aiw.game < g_aiw.gameSize) { snprintf(out, size, "game.dll+0x%llX", (unsigned long long)(at - g_aiw.game)); return; }
    if (at - g_aiw.exe < g_aiw.exeSize) { snprintf(out, size, "helldivers2.exe+0x%llX", (unsigned long long)(at - g_aiw.exe)); return; }
    HMODULE m = NULL;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &m) &&
        GetModuleFileNameA(m, path, sizeof path) > 0)
    {
        const char* name = strrchr(path, '\\');
        snprintf(out, size, "%s+0x%llX", name != NULL ? name + 1 : path, (unsigned long long)(at - (uintptr_t)m));
        return;
    }
    snprintf(out, size, "0x%llX", (unsigned long long)at);
}

/* The ring, the slots, the record flags and the owners as they are now. Live is the engine's own count: 4 minus the
 * free ids (0x79F6C0 computes free as tail - head). */
static int State(char* out, size_t size)
{
    const uint32_t head = U32(CO_AIW_HEAD), tail = U32(CO_AIW_TAIL), free_ = tail - head;
    int n = snprintf(out, size, "ring head %u tail %u free %u live %d next %u | slots", head, tail, free_, 4 - (int)free_, U32(CO_AIW_RING + 4u * (head & 3u)));
    for (uint32_t i = 0; i < 4 && n > 0 && (size_t)n < size; i++) n += snprintf(out + n, size - n, " %u 0x%llX", i, (unsigned long long)U64(CO_AIW_SLOTS + 0x80u * i));
    if (n > 0 && (size_t)n < size) n += snprintf(out + n, size - n, " | flags");
    for (uint32_t i = 0; i < 4 && n > 0 && (size_t)n < size; i++) n += snprintf(out + n, size - n, " %u 0x%X", i, U32(CO_AIW_RECORDS + 0xB0u * i + 0x1Cu));
    if (n > 0 && (size_t)n < size) n += snprintf(out + n, size - n, " | owners");
    for (uint32_t i = 0; i < 4 && n > 0 && (size_t)n < size; i++) n += snprintf(out + n, size - n, " %u 0x%llX", i, (unsigned long long)U64(CO_AIW_OWNERS + 8u * i));
    return n;
}

static void Line(const char* format, ...)
{
    char text[200], state[640];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    State(state, sizeof state);
    g_coApi->log(g_coMod, "%s | %s", text, state);
}

/* Any other ring user (game.dll has none; the exe's 0x79F6D0, 0x79F770 and 0x79F7F0 have no reference in the dump)
 * shows as a move between two logged calls. Two threads' calls at once can show one too. */
static void RingCheck(LONG seq)
{
    const LONG head = (LONG)U32(CO_AIW_HEAD), tail = (LONG)U32(CO_AIW_TAIL);
    const LONG wasHead = InterlockedExchange(&g_aiw.seenHead, head), wasTail = InterlockedExchange(&g_aiw.seenTail, tail);
    if (wasHead != head || wasTail != tail)
        g_coApi->log(g_coMod, "aiw: the ring moved outside create and destroy: head %ld -> %ld, tail %ld -> %ld (seen before #%ld)", wasHead, head, wasTail, tail, seq);
}
static void RingSeen(void) { InterlockedExchange(&g_aiw.seenHead, (LONG)U32(CO_AIW_HEAD)); InterlockedExchange(&g_aiw.seenTail, (LONG)U32(CO_AIW_TAIL)); }

/* Lines before and after the call: a crash inside it still leaves the first on disk (the loader writes each line). */
static uint32_t Create(uint64_t owner, uint64_t rdx, uint64_t settings, uint64_t r9)
{
    char from[96];
    const LONG seq = InterlockedIncrement(&g_aiw.seq);
    Where((uintptr_t)__builtin_return_address(0), from, sizeof from);
    RingCheck(seq);
    Line("aiw #%ld create before: from %s tid %lu owner 0x%llX rdx %d settings %d r9 %d", seq, from, GetCurrentThreadId(), (unsigned long long)owner, rdx != 0, settings != 0, r9 != 0);
    const uint32_t id = g_aiw.create(owner, rdx, settings, r9);
    Line("aiw #%ld create after: id %u", seq, id);
    RingSeen();
    return id;
}

static void Destroy(uint32_t id)
{
    char from[96];
    const LONG seq = InterlockedIncrement(&g_aiw.seq);
    Where((uintptr_t)__builtin_return_address(0), from, sizeof from);
    RingCheck(seq);
    Line("aiw #%ld destroy before: id %u from %s tid %lu", seq, id, from, GetCurrentThreadId());
    g_aiw.destroy(id);
    Line("aiw #%ld destroy after: id %u", seq, id);
    RingSeen();
}

/* The query has no bound or null test (0x7DAF6A..0x7DAFB4); the game's code still runs after the line. */
static uint32_t Query(uint32_t id, void* query, void* segments, uint32_t room)
{
    if ((id >= 4 || U64(CO_AIW_SLOTS + 0x80u * id) == 0) && InterlockedCompareExchange(&g_aiw.nullSeen, 1, 0) == 0)
    {
        char from[96];
        const uintptr_t at = (uintptr_t)__builtin_return_address(0);
        Where(at, from, sizeof from);
        Line("aiw: the steering query asked nav id %u with an empty AI-world slot, from %s tid %lu%s", id, from, GetCurrentThreadId(),
             g_aiw.game != 0 && at == g_aiw.game + 0x134A58Eu ? " (the boundary job)" : "");
    }
    return g_aiw.query(id, query, segments, room);
}

static const char* Install(uintptr_t exe, uintptr_t game)
{
    if (g_aiw.create != NULL) return NULL;
    if (exe == 0) return "helldivers2.exe was not found";
    for (size_t i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
        if (g_coApi->code_matches(exe + kPins[i].rva, kPins[i].hex) != 1)
        {
            static char why[96];
            snprintf(why, sizeof why, "the engine's code at exe+0x%X is not the code this log was made for", kPins[i].rva);
            return why;
        }
    g_aiw.exe = exe;
    g_aiw.game = game;
    g_coApi->module_base("helldivers2.exe", &g_aiw.exeSize);
    if (game != 0) g_coApi->module_base("game.dll", &g_aiw.gameSize);
    RingSeen();
    /* the query first: its line is only worth having with the create and destroy lines before it */
    if (g_coApi->hook(g_coMod, exe + CO_AIW_QUERY, (void*)Query, (void**)&g_aiw.query) != 0) { g_aiw.query = NULL; return "the steering query could not be hooked"; }
    if (g_coApi->hook(g_coMod, exe + CO_AIW_DESTROY, (void*)Destroy, (void**)&g_aiw.destroy) != 0) { g_aiw.destroy = NULL; return "the world destroy could not be hooked"; }
    if (g_coApi->hook(g_coMod, exe + CO_AIW_CREATE, (void*)Create, (void**)&g_aiw.create) != 0) { g_aiw.create = NULL; return "the world create could not be hooked"; }
    return NULL;
}

const char* co_aiw_log_install(uintptr_t exe, uintptr_t game)
{
    const char* why = Install(exe, game);
    if (why != NULL) co_log("aiw log: off: %s", why);
    else if (!g_aiw.said++) Line("aiw log: on: world create exe+0x%X, world destroy exe+0x%X and the steering query exe+0x%X hooked at entry", CO_AIW_CREATE, CO_AIW_DESTROY, CO_AIW_QUERY);
    return why;
}

#ifdef CO_TEST
void co_aiw_log_test_reset(void) { memset((void*)&g_aiw, 0, sizeof g_aiw); }
#endif
#else
typedef int co_aiw_log_research_only; /* a normal build compiles none of it */
#endif
