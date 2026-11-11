/* The map generator's room: what the mod asks HellForge for, each way the answer turns the complex part off with its
 * own log line, and the layout the mod's code reads, which is HellForge's and nothing before the block has moved. */
#include <stdio.h>
#include <string.h>

#include "harness/fake_host.h"

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* A layout no build of HellForge has, so a place the mod kept for itself would show. */
static void Give(uint32_t moved)
{
    HfGenerationCapacity* c = &g_fake.capacity;
    memset(c, 0, sizeof *c);
    c->size = sizeof *c;
    c->locations = 65024u; c->minimap_proxies = 32768u; c->mission_stamps = 1024u; c->places = 512u; c->place_set = 256u;
    c->important_locations = 1024u; c->linked_locations = 1024u; c->route_nodes = 2304u; c->route_edges = 3456u; c->route_points = 65520u;
    c->block_bytes = 0x2A00000u; c->moved = moved;
    for (uint32_t i = 0; i <= HF_GEN_ROUTE_POINTS; i++) { c->regions[i].at = 0x1200000u + 0x10010u * i; c->regions[i].places = 100u + i; }
    c->regions[HF_GEN_LOCATIONS_A].places = c->regions[HF_GEN_LOCATIONS_B].places = 65024u;
    c->regions[HF_GEN_OBJECTIVE_LIST].places = 30u;
}

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    Give(0);
}

static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = g_fake.log; (at = strstr(at, text)) != NULL; at += strlen(text)) n++;
    return n;
}

int main(void)
{
    Start("the request");
    co_capacity_init();
    CHECK(g_fake.wanted.size == sizeof(HfGenerationCapacity) && g_fake.wanted.locations == CO_GEN_LOCATIONS && g_fake.wanted.minimap_proxies == 32768u);
    CHECK(g_fake.wanted.important_locations == 1024u && g_fake.wanted.linked_locations == 1024u && g_fake.wanted.route_nodes == 2304u);
    CHECK(g_fake.wanted.route_edges == 3456u && g_fake.wanted.route_points == 65520u);
    CHECK(g_fake.wanted.mission_stamps == 0 && g_fake.wanted.places == 0 && g_fake.wanted.place_set == 0); /* HellForge's own */
    const CoComplexNeeds* n = co_capacity_needs();
    CHECK(n->capacity_call && n->regions && !n->live_load && n->capacity_granted && !n->level_11 && !n->moved);
    CHECK(!co_complex_on() && co_gen_at(HF_GEN_LOCATIONS_B) == 0 && co_gen_places(HF_GEN_LOCATIONS_A) == 0 && co_gen_block() == 0);

    /* Level 11 is installed after the game loaded: nothing is decided, read or logged while it is not there. */
    g_case = "waiting for level 11";
    CHECK(co_capacity_poll(1000) == 0 && co_capacity_poll(1000 + CO_GEN_WAIT_MS - 1) == 0);
    CHECK(g_fake.log[0] == 0 && co_gen_at(HF_GEN_CLUSTERS) == 0);
    g_fake.levelRow = 0x1000; Give(1);
    CHECK(co_capacity_poll(30000) == 1);
    n = co_capacity_needs();
    CHECK(n->level_11 && n->moved && n->regions && n->capacity_granted && !n->stock_stubs && !n->own_sites);
    /* The layout is the one HellForge reports, region by region. */
    for (int i = 0; i <= HF_GEN_ROUTE_POINTS; i++) CHECK(co_gen_at(i) == 0x1200000u + 0x10010u * (uint32_t)i);
    CHECK(co_gen_places(HF_GEN_LOCATIONS_A) == 65024u && co_gen_places(HF_GEN_OBJECTIVE_LIST) == 30u && co_gen_places(HF_GEN_IMPORTANT) == 100u + HF_GEN_IMPORTANT);
    CHECK(co_gen_block() == 0x2A00000u && co_gen_at(HF_GEN_REGIONS) == 0 && co_gen_at(-1) == 0);
    /* This build pins neither stock's stubs nor its own sites: the complex part is off, and the log says so once. */
    CHECK(!co_complex_on() && Lines("complex operations are off") == 1 && fake_host_logged("makes none yet"));
    CHECK(co_capacity_poll(40000) == 1 && Lines("complex operations are off") == 1);

    Start("a HellForge without the call");
    ((HfApi*)g_coApi)->size = offsetof(HfApi, generation_capacity);
    co_capacity_init();
    CHECK(!co_capacity_needs()->capacity_call && g_fake.wanted.size == 0);
    CHECK(co_capacity_poll(1) == 1 && !co_complex_on() && fake_host_logged("has no generation_capacity") && Lines("complex operations are off") == 1);

    Start("a HellForge that names no regions");
    g_fake.capacity.size = offsetof(HfGenerationCapacity, regions);
    co_capacity_init();
    CHECK(co_capacity_needs()->capacity_call && !co_capacity_needs()->regions);
    g_fake.levelRow = 0x1000; g_fake.capacity.moved = 1;
    CHECK(co_capacity_poll(1) == 1 && !co_complex_on() && fake_host_logged("does not report the map generator's layout") && co_gen_at(HF_GEN_LOCATIONS_B) == 0);

    Start("loaded while the game ran");
    g_fake.loadPhase = HF_LOAD_LIVE;
    co_capacity_init();
    CHECK(co_capacity_needs()->live_load && g_fake.wanted.size == 0); /* no request: it would be refused */
    CHECK(co_capacity_poll(1) == 1 && !co_complex_on() && fake_host_logged("restart the game"));

    Start("the request refused");
    g_fake.capacityRefused = 1;
    co_capacity_init();
    CHECK(!co_capacity_needs()->capacity_granted);
    g_fake.levelRow = 0x1000; g_fake.capacity.moved = 1;
    CHECK(co_capacity_poll(1) == 1 && !co_complex_on() && fake_host_logged("refused the map generator's capacity") && co_gen_block() == 0);

    Start("no level 11");
    co_capacity_init();
    CHECK(co_capacity_poll(5000) == 0 && co_capacity_poll(5000 + CO_GEN_WAIT_MS) == 1);
    CHECK(!co_capacity_needs()->level_11 && !co_complex_on() && fake_host_logged("difficulty 11 is not installed") && co_gen_at(HF_GEN_CLUSTERS) == 0);
    g_fake.levelRow = 0x1000; g_fake.capacity.moved = 1; /* settled is settled: no second line, no late switch */
    CHECK(co_capacity_poll(5000 + 2 * CO_GEN_WAIT_MS) == 1 && Lines("complex operations are off") == 1 && co_gen_at(HF_GEN_CLUSTERS) == 0);

    Start("the generator kept its own arrays");
    co_capacity_init();
    g_fake.levelRow = 0x1000;
    CHECK(co_capacity_poll(1) == 1 && co_capacity_needs()->level_11 && !co_capacity_needs()->moved);
    CHECK(!co_complex_on() && fake_host_logged("kept its own arrays") && co_gen_at(HF_GEN_LOCATIONS_A) == 0 && co_gen_block() == 0);

    Start("less room than asked for");
    co_capacity_init();
    g_fake.levelRow = 0x1000; Give(1); g_fake.capacity.locations = 8192u;
    CHECK(co_capacity_poll(1) == 1 && !co_capacity_needs()->capacity_granted && !co_complex_on() && co_gen_at(HF_GEN_LOCATIONS_A) == 0);

    printf("%s\n", g_failed ? "capacity: failures" : "capacity: all cases passed");
    return g_failed != 0;
}
