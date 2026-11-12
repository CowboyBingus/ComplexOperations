/* The steering-wall query on an empty AI world slot: no segments and one log line with the id; a slot with a world,
 * the engine's query with the same arguments. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_steer.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

typedef uint32_t (*QueryFn)(uint32_t id, void* query, void* segments, uint32_t room);
static uint32_t g_calls, g_id, g_room;
static void *g_in, *g_out;
static uint32_t EngineQuery(uint32_t id, void* query, void* segments, uint32_t room)
{
    g_calls++; g_id = id; g_in = query; g_out = segments; g_room = room;
    return 5;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    const uintptr_t target = (uintptr_t)exe + CO_STEER_QUERY;
    g_fake.originals[0].target = target; g_fake.originals[0].original = (void*)EngineQuery;
    co_steer_test_reset();

    g_case = "the install";
    CHECK(co_steer_install((uintptr_t)exe) == NULL);
    QueryFn query = (QueryFn)fake_host_detour(target - (uintptr_t)g_fake.image);
    CHECK(query != NULL);
    if (query == NULL) { printf("steer: %d failed\n", g_failed); return 1; }

    uint8_t in[0x60], out[0x40 * 0x20];
    g_case = "slot 0 empty: no segments, logged once with the id";
    CHECK(query(0, in, out, 0x40) == 0 && g_calls == 0);
    CHECK(fake_host_logged("nav id 0, which has no AI world"));
    CHECK(query(0, in, out, 0x40) == 0 && g_calls == 0);

    g_case = "an id past the table";
    CHECK(query(4, in, out, 0x40) == 0 && query(0xFFFFFFFFu, in, out, 0x40) == 0 && g_calls == 0);

    g_case = "slot 2 holds a world: the engine's query";
    const uint64_t world = 0x12345678ull;
    memcpy(exe + CO_STEER_SLOTS + 2u * 0x80u, &world, 8);
    CHECK(query(2, in, out, 0x40) == 5 && g_calls == 1 && g_id == 2 && g_in == in && g_out == out && g_room == 0x40);

    g_case = "a second install keeps the first";
    CHECK(co_steer_install((uintptr_t)exe) == NULL);

    g_case = "another engine";
    co_steer_test_reset();
    CHECK(co_steer_install(0) != NULL);

    if (g_failed) { printf("steer: %d failed\n", g_failed); return 1; }
    printf("steer: all passed\n");
    return 0;
}
