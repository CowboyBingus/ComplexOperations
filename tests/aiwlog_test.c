/* The nav world ids' log (research build only): lines before and after each create and destroy with the id, the
 * caller, the ring and the slots; a ring move between calls; one line for the first empty-slot query; the game's
 * code still runs every time; a pin mismatch refuses with a line. Run on a fake engine that pops and pushes ids. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_aiw_log.h"

#ifndef CO_RESEARCH
int main(void) { printf("aiwlog: a research build's part; nothing to test here\n"); return 0; }
#else
static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t* g_exe;
static uint32_t* U32(uint32_t rva) { return (uint32_t*)(g_exe + rva); }
static uint64_t* U64(uint32_t rva) { return (uint64_t*)(g_exe + rva); }

/* The engine's create and destroy as 0x78D330 and 0x78F000 keep the ring, the records, the owners and the slots. */
static int g_creates, g_destroys, g_queries;
static uint32_t EngineCreate(uint64_t owner, uint64_t rdx, uint64_t settings, uint64_t r9)
{
    g_creates++;
    const uint32_t id = *U32(CO_AIW_RING + 4u * (*U32(CO_AIW_HEAD) & 3u));
    (*U32(CO_AIW_HEAD))++;
    *U64(CO_AIW_OWNERS + 8u * id) = owner;
    *U32(CO_AIW_RECORDS + 0xB0u * id + 0x18u) = id;
    *U32(CO_AIW_RECORDS + 0xB0u * id + 0x1Cu) = (rdx ? 4u : 0u) | (r9 ? 1u : 0u) | (settings ? 2u : 0u);
    if (settings) *U64(CO_AIW_SLOTS + 0x80u * id) = 0xA1000000ull + id;
    return id;
}
static void EngineDestroy(uint32_t id)
{
    g_destroys++;
    if (id >= 4 || *U32(CO_AIW_RECORDS + 0xB0u * id + 0x1Cu) == 0) return;
    if (*U32(CO_AIW_RECORDS + 0xB0u * id + 0x1Cu) & 2u) *U64(CO_AIW_SLOTS + 0x80u * id) = 0;
    memset(g_exe + CO_AIW_RECORDS + 0xB0u * id, 0, 0xB0);
    *U32(CO_AIW_RECORDS + 0xB0u * id + 0x18u) = 0xFFFFFFFFu;
    *U64(CO_AIW_OWNERS + 8u * id) = 0;
    *U32(CO_AIW_RING + 4u * (*U32(CO_AIW_TAIL) & 3u)) = id;
    (*U32(CO_AIW_TAIL))++;
}
static uint32_t EngineQuery(uint32_t id, void* query, void* segments, uint32_t room) { g_queries++; return 7; }

typedef uint32_t (*CreateFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef void (*DestroyFn)(uint32_t);
typedef uint32_t (*QueryFn)(uint32_t, void*, void*, uint32_t);

static void* Detour(uint32_t rva) { return fake_host_detour((uintptr_t)g_exe + rva - (uintptr_t)g_fake.image); }

static void Fresh(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_exe = fake_host_exe();
    for (uint32_t i = 0; i < 4; i++) *U32(CO_AIW_RING + 4u * i) = i;
    *U32(CO_AIW_TAIL) = 4; /* four free ids, as at startup */
    co_aiw_log_test_reset();
    g_creates = g_destroys = g_queries = 0;
}

int main(void)
{
    Fresh();
    g_fake.originals[0].target = (uintptr_t)g_exe + CO_AIW_CREATE; g_fake.originals[0].original = (void*)EngineCreate;
    g_fake.originals[1].target = (uintptr_t)g_exe + CO_AIW_DESTROY; g_fake.originals[1].original = (void*)EngineDestroy;
    g_fake.originals[2].target = (uintptr_t)g_exe + CO_AIW_QUERY; g_fake.originals[2].original = (void*)EngineQuery;

    g_case = "the install";
    CHECK(co_aiw_log_install((uintptr_t)g_exe, (uintptr_t)g_fake.image) == NULL);
    CHECK(fake_host_logged("aiw log: on: world create exe+0x78D330, world destroy exe+0x78F000 and the steering query exe+0x7DAF10 hooked at entry | ring head 0 tail 4 free 4 live 0 next 0"));
    CreateFn create = (CreateFn)Detour(CO_AIW_CREATE);
    DestroyFn destroy = (DestroyFn)Detour(CO_AIW_DESTROY);
    QueryFn query = (QueryFn)Detour(CO_AIW_QUERY);
    CHECK(create != NULL && destroy != NULL && query != NULL);
    if (create == NULL || destroy == NULL || query == NULL) { printf("aiwlog: %d failed\n", g_failed); return 1; }

    g_case = "a create: the id handed out, the caller and the state before and after";
    g_fake.log[0] = 0;
    CHECK(create(0x5000, 1, 1, 1) == 0 && g_creates == 1);
    CHECK(fake_host_logged("aiw #1 create before: from aiwlog_test.exe+0x"));
    CHECK(fake_host_logged("owner 0x5000 rdx 1 settings 1 r9 1 | ring head 0 tail 4 free 4 live 0 next 0 | slots 0 0x0 1 0x0 2 0x0 3 0x0 | flags 0 0x0"));
    CHECK(fake_host_logged("aiw #1 create after: id 0 | ring head 1 tail 4 free 3 live 1 next 1 | slots 0 0xA1000000 1 0x0 2 0x0 3 0x0 | flags 0 0x7 1 0x0 2 0x0 3 0x0 | owners 0 0x5000 1 0x0"));

    g_case = "a preview's create: no AI world";
    CHECK(create(0x6000, 0, 0, 1) == 1);
    CHECK(fake_host_logged("aiw #2 create after: id 1 | ring head 2 tail 4 free 2 live 2 next 2 | slots 0 0xA1000000 1 0x0"));

    g_case = "a destroy: the id given back and the slot emptied";
    g_fake.log[0] = 0;
    destroy(0);
    CHECK(g_destroys == 1);
    CHECK(fake_host_logged("aiw #3 destroy before: id 0 from aiwlog_test.exe+0x"));
    CHECK(fake_host_logged("aiw #3 destroy after: id 0 | ring head 2 tail 5 free 3 live 1 next 2 | slots 0 0x0 1 0x0 2 0x0 3 0x0 | flags 0 0x0 1 0x1"));
    CHECK(!fake_host_logged("the ring moved"));

    g_case = "a stale destroy of a free id: logged, the game's code no-ops";
    destroy(0);
    CHECK(g_destroys == 2 && fake_host_logged("aiw #4 destroy after: id 0 | ring head 2 tail 5 free 3 live 1"));

    g_case = "another ring user between two calls";
    g_fake.log[0] = 0;
    (*U32(CO_AIW_HEAD))++;
    destroy(1);
    CHECK(fake_host_logged("aiw: the ring moved outside create and destroy: head 2 -> 3, tail 5 -> 5 (seen before #5)"));

    g_case = "the first empty-slot query: one line, the game's query still runs";
    g_fake.log[0] = 0;
    uint8_t in[0x60], out[0x40];
    CHECK(query(1, in, out, 0x40) == 7 && g_queries == 1);
    CHECK(fake_host_logged("aiw: the steering query asked nav id 1 with an empty AI-world slot, from aiwlog_test.exe+0x"));
    CHECK(!fake_host_logged("(the boundary job)"));
    CHECK(query(0, in, out, 0x40) == 7 && g_queries == 2);
    CHECK(!fake_host_logged("nav id 0 with an empty"));

    g_case = "a second install keeps the first and says nothing";
    g_fake.log[0] = 0;
    CHECK(co_aiw_log_install((uintptr_t)g_exe, (uintptr_t)g_fake.image) == NULL && g_fake.log[0] == 0);

    g_case = "a filled slot asks nothing of the log";
    Fresh();
    g_fake.originals[2].target = (uintptr_t)g_exe + CO_AIW_QUERY; g_fake.originals[2].original = (void*)EngineQuery;
    CHECK(co_aiw_log_install((uintptr_t)g_exe, 0) == NULL);
    query = (QueryFn)Detour(CO_AIW_QUERY);
    *U64(CO_AIW_SLOTS + 0x80u * 2u) = 0x1234;
    g_fake.log[0] = 0;
    CHECK(query != NULL && query(2, in, out, 0x40) == 7 && g_fake.log[0] == 0);

    g_case = "another engine: refused with the address, nothing hooked";
    Fresh();
    g_fake.noMatch[0] = (uintptr_t)g_exe + 0x78F366u - (uintptr_t)g_fake.image;
    CHECK(co_aiw_log_install((uintptr_t)g_exe, (uintptr_t)g_fake.image) != NULL && g_fake.hookCount == 0);
    CHECK(fake_host_logged("aiw log: off: the engine's code at exe+0x78F366 is not the code this log was made for"));
    Fresh();
    CHECK(co_aiw_log_install(0, 0) != NULL && fake_host_logged("aiw log: off: helldivers2.exe was not found"));

    g_case = "a hook refused";
    Fresh();
    g_fake.noHook = (uintptr_t)g_exe + CO_AIW_CREATE - (uintptr_t)g_fake.image;
    CHECK(co_aiw_log_install((uintptr_t)g_exe, 0) != NULL && fake_host_logged("aiw log: off: the world create could not be hooked"));

    if (g_failed) { printf("aiwlog: %d failed\n", g_failed); return 1; }
    printf("aiwlog: all passed\n");
    return 0;
}
#endif
