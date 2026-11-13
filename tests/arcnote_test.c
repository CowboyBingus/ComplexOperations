/* Where the mission arcs note each objective's mission (proto/difficulty_world_arcs.c ObjectiveArcs): after the moves,
 * so the note holds each objective's final cluster (+0xE0), and on a refused deal too, with the clusters as they were. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_objectives.h"
#include "xl/co_unlock.h"

void CoXlTestArcList(uintptr_t game, uint16_t row, const uint32_t* selected, uint32_t n, const uint32_t* list, uint32_t total);
void CoXlTestObjectiveArcs(uintptr_t level, uint64_t a, uint64_t pass, uint64_t clusters, uint32_t n, float centre, uint32_t placed);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define MISSIONS 0x3773420u
#define OBJECTIVES 0x32EF870u
#define LIVE_EVENT 0x68BFBB59u
#define RADAR 0xF1969B14u
#define MANY 0xFFFFFFFFu
enum { ICBM = 0x100, CODES, POWER, FUEL, M1 = 0x200, M2, M3, O1 = 0x300, O2, O3, O4, O5, O6 };
enum { PLAIN = 50 };

/* The generation block G and its regions (HellForge's, as the capacity reports them), the level, the pass's two
 * records, all in the image's free room. */
#define G 0x3000000u
#define RECORDS_AT 0x1000u
#define CLUSTERS_AT 0x10000u
#define LIST_AT 0x20000u
#define LEVEL 0x3100000u
#define PASS 0x3101000u
#define ARGS 0x3102000u
#define COUNT 0x3103000u
#define SITES 18u
#define CENTRE 4096.0f

static uint8_t* g_image;
static uint32_t g_definitions;
static CoList g_list;

static uint64_t At(uint32_t at) { return (uint64_t)(uintptr_t)(g_image + at); }
static void Put64(uint32_t at, uint64_t v) { memcpy(g_image + at, &v, 8); }
static void Put32(uint32_t at, uint32_t v) { memcpy(g_image + at, &v, 4); }
static uint64_t Get64(uint32_t at) { uint64_t v; memcpy(&v, g_image + at, 8); return v; }

static void Entry(uint32_t r, int k, uint32_t id, float weight, uint32_t cls, uint32_t most)
{
    uint32_t e[6] = {id, 0, 0, cls, 0, most};
    memcpy(&e[1], &weight, 4);
    memcpy(g_image + MISSIONS + r * 0x380u + 0x40u + (uint32_t)k * 0x18u, e, sizeof e);
}
static void Objective(uint32_t id, uint32_t most)
{
    uint8_t* row = g_image + OBJECTIVES + ++g_definitions * 0xA0u;
    memcpy(row, &id, 4); memcpy(row + 0x20, &most, 4);
}

/* A city's Launch ICBM row and HellForge's layout, then the fill's list of three missions (objectives_test.c's first
 * case): block 1 at 0..5, the side objectives at 6..17, blocks 2 and 3 at 18..29. */
static void Fill(void)
{
    static const uint32_t icbm[6] = {ICBM, ICBM, ICBM, CODES, POWER, FUEL};
    uint32_t s[24], n = 0;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_image = g_fake.image;
    g_definitions = 0;
    co_list_test_reset();
    co_unlock_test_reset();
    HfGenerationCapacity* c = &g_fake.capacity;
    c->size = sizeof *c;
    c->locations = 65024u; c->minimap_proxies = 32768u; c->important_locations = 1024u; c->linked_locations = 1024u;
    c->route_nodes = 2304u; c->route_edges = 3456u; c->route_points = 65520u; c->block_bytes = 0x2A00000u; c->moved = 1;
    for (uint32_t i = 0; i <= HF_GEN_ROUTE_POINTS; i++) { c->regions[i].at = 0x11A7280u + 0x1000000u * i; c->regions[i].places = 100u; }
    c->regions[HF_GEN_IMPORTANT].at = RECORDS_AT; c->regions[HF_GEN_CLUSTERS].at = CLUSTERS_AT;
    c->regions[HF_GEN_OBJECTIVE_LIST].at = LIST_AT; c->regions[HF_GEN_OBJECTIVE_LIST].places = 30u;
    g_fake.levelRow = 0x1000;
    co_capacity_init();
    co_capacity_poll(1);
    CHECK(co_gen_at(HF_GEN_CLUSTERS) == CLUSTERS_AT && co_gen_places(HF_GEN_OBJECTIVE_LIST) == 30u);

    for (uint32_t id = ICBM; id <= FUEL; id++) Objective(id, 3);
    Objective(RADAR, 3); Objective(LIVE_EVENT, 3);
    for (uint32_t id = M1; id <= M3; id++) Objective(id, 1);
    for (uint32_t id = O1; id <= O6; id++) Objective(id, 2);
    Entry(PLAIN, 0, ICBM, 1.0f, 0, 3); Entry(PLAIN, 1, CODES, 1.0f, 1, 1); Entry(PLAIN, 2, POWER, 1.0f, 1, 1); Entry(PLAIN, 3, FUEL, 1.0f, 1, 1);
    for (int k = 0; k < 3; k++) Entry(PLAIN, 8 + k, M1 + (uint32_t)k, 1.0f, 2, MANY);
    for (int k = 0; k < 6; k++) Entry(PLAIN, 12 + k, O1 + (uint32_t)k, 1.0f, 3, MANY);
    Entry(PLAIN, 20, RADAR, 1.0f, 3, MANY);
    memcpy(s, icbm, sizeof icbm); n = 6;
    for (uint32_t i = 0; i < 12; i++) s[n++] = O1 + i % 6;
    for (uint32_t i = 0; i < 6; i++) s[n++] = M1 + i % 3;
    const CoListRules rules = {3, 30, 1, 6, 24};
    CHECK(co_list((uintptr_t)g_image, PLAIN, s, n, &rules, &g_list) == NULL && g_list.count == 30 && g_list.blocks == 3);
    CHECK(g_list.block[0] == 1 && g_list.block[17] == 0 && g_list.block[18] == 2 && g_list.block[24] == 3 && g_list.blockMission[2] == 2);
    CoXlTestArcList((uintptr_t)g_image, PLAIN, s, n, g_list.ids, g_list.count);
}

/* The level: the list in its record, one objective record a list primary (location l, list entry Primary(l)), each in
 * its own cluster of 18 round the centre, the missions mixed round the ring. `children`: location 0's mode 1 children
 * in its cluster, more than the deal moves with it. */
static uint32_t Primary(uint32_t l) { return l < 6 ? l : l + 12; }
static uint32_t ClusterOf(uint32_t l) { return (l * 7u) % SITES; }
static void Level(uint32_t children)
{
    Put32(G + LIST_AT, PLAIN); Put32(G + LIST_AT + 0xD4u, g_list.count);
    memcpy(g_image + G + LIST_AT + 4u, g_list.ids, 4ull * g_list.count);
    g_image[LEVEL + 0xF9u] = 11; Put64(LEVEL + 0x2C8u, At(G));
    Put64(PASS + 0x18u, At(LEVEL));
    Put64(ARGS + 0x18u, At(G + RECORDS_AT)); Put64(ARGS + 0x28u, At(COUNT)); Put32(COUNT, SITES);
    for (uint32_t i = 0; i < SITES; i++)
    {
        const uint32_t cl = G + CLUSTERS_AT + 0xE0u * i;
        const float angle = 6.2831853f * (float)i / (float)SITES, xy[2] = {CENTRE + 2000.0f * cosf(angle), CENTRE + 2000.0f * sinf(angle)};
        memset(g_image + cl, 0, 0xE0);
        memcpy(g_image + cl, xy, 8);
        Put32(cl + 0xD0u, 1u);
    }
    for (uint32_t l = 0; l < SITES; l++)
    {
        const uint32_t rec = G + RECORDS_AT + 0x108u * l, cl = G + CLUSTERS_AT + 0xE0u * ClusterOf(l), k = Primary(l);
        memset(g_image + rec, 0, 0x108);
        Put32(rec + 8u, 0xADC32FAAu); g_image[rec + 0xA2u] = 0xFF; /* no definition: not an extraction */
        Put32(rec + 0xD8u, g_list.ids[k]); g_image[rec + 0xDCu] = g_list.main[k] ? 7 : 8; Put64(rec + 0xE0u, At(cl));
        Put64(cl + 0x10u, At(rec));
    }
    for (uint32_t q = 0; q < children; q++)
    {
        const uint32_t child = G + RECORDS_AT + 0x108u * (SITES + q), cl = G + CLUSTERS_AT + 0xE0u * ClusterOf(0);
        memset(g_image + child, 0, 0x108);
        g_image[child + 0xDCu] = 0xB; Put64(child + 0xF8u, At(G + RECORDS_AT)); g_image[child + 0x100u] = 1;
        Put64(cl + 0x18u + 8u * q, At(child)); Put32(cl + 0xD0u, 2u + q);
    }
}
static uint64_t ClusterAt(uint32_t l) { return Get64(G + RECORDS_AT + 0x108u * l + 0xE0u); }
static uint8_t MissionOf(uint32_t l) { const uint32_t b = g_list.block[Primary(l)]; return b != 0 ? g_list.blockMission[b - 1] : (uint8_t)CO_UNLOCK_NONE; }

int main(void)
{
    g_case = "a dealt level: the note holds each objective's cluster after the moves";
    Fill();
    Level(0);
    CoXlTestObjectiveArcs((uintptr_t)At(LEVEL), At(ARGS), At(PASS), At(G + CLUSTERS_AT), SITES, CENTRE, SITES);
    CHECK(fake_host_logged("mission arcs: row 50"));
    {
        uint32_t moved = 0;
        for (uint32_t l = 0; l < SITES; l++)
        {
            const uint64_t was = At(G + CLUSTERS_AT + 0xE0u * ClusterOf(l)), now = ClusterAt(l);
            const uint32_t id = g_list.ids[Primary(l)];
            moved += now != was;
            CHECK(co_unlock_noted((uintptr_t)At(LEVEL), (uintptr_t)At(G), l, id, now) == MissionOf(l));
            if (now != was) CHECK(co_unlock_noted((uintptr_t)At(LEVEL), (uintptr_t)At(G), l, id, was) == CO_UNLOCK_NONE);
        }
        CHECK(moved > 0);
    }
    CHECK(fake_host_logged("complex operations: mission unlock: 18 objective locations in 3 missions (18 objectives on the map)"));

    g_case = "a refused deal: the note holds each objective's cluster as it was";
    Fill();
    Level(9);
    CoXlTestObjectiveArcs((uintptr_t)At(LEVEL), At(ARGS), At(PASS), At(G + CLUSTERS_AT), SITES, CENTRE, SITES);
    CHECK(fake_host_logged("mission arcs: not dealt (an objective has more than 8 children)"));
    for (uint32_t l = 0; l < SITES; l++)
    {
        const uint64_t was = At(G + CLUSTERS_AT + 0xE0u * ClusterOf(l));
        CHECK(ClusterAt(l) == was && co_unlock_noted((uintptr_t)At(LEVEL), (uintptr_t)At(G), l, g_list.ids[Primary(l)], was) == MissionOf(l));
    }
    CHECK(fake_host_logged("complex operations: mission unlock: 18 objective locations in 3 missions (18 objectives on the map)"));

    if (g_failed == 0) printf("arc note: all checks passed\n");
    return g_failed != 0;
}
