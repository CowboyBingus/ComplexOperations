/* Room in the map generator for an extra large map, and where its arrays lie. HellForge owns the generator's block and
 * level 11; the mod asks for capacity as it starts and reads the layout back once level 11 is installed. It keeps no
 * offset of its own: another mod's request moves every array behind it. */
#include <string.h>

#include "co.h"

/* What the extra large world's code was written for. Stamps, places and the place set are HellForge's own sizes. */
static const HfGenerationCapacity kWant = {.size = sizeof(HfGenerationCapacity), .locations = CO_GEN_LOCATIONS, .minimap_proxies = 32768u,
    .important_locations = 1024u, .linked_locations = 1024u, .route_nodes = 2304u, .route_edges = 3456u, .route_points = 65520u};
/* The regions the mod's code reads; a HellForge that names none of them leaves them 0. */
static const uint8_t kRead[] = {HF_GEN_LOCATIONS_A, HF_GEN_LOCATIONS_B, HF_GEN_OBJECTIVE_LIST, HF_GEN_CLUSTERS, HF_GEN_IMPORTANT, HF_GEN_LINKED,
    HF_GEN_ROUTE_NODES, HF_GEN_ROUTE_EDGES, HF_GEN_ROUTE_POINTS};

static struct
{
    CoComplexNeeds needs;
    HfGenerationCapacity have; /* as read with `moved`; zero before */
    uint64_t since;            /* the first poll */
    int asked, settled, on;
} g_capacity;

static int Has(size_t end) { return g_coApi != NULL && g_coApi->size >= end; }
#define HAS(call) Has(offsetof(HfApi, call) + sizeof(void*))

static int RegionsNamed(const HfGenerationCapacity* have)
{
    if (have->block_bytes == 0) return 0;
    for (size_t i = 0; i < sizeof kRead; i++)
        if (have->regions[kRead[i]].at == 0 || have->regions[kRead[i]].places == 0) return 0;
    return 1;
}

/* Every capacity asked for is there: another mod can only have raised one. */
static int Granted(const HfGenerationCapacity* have)
{
    return have->locations >= kWant.locations && have->minimap_proxies >= kWant.minimap_proxies &&
        have->important_locations >= kWant.important_locations && have->linked_locations >= kWant.linked_locations &&
        have->route_nodes >= kWant.route_nodes && have->route_edges >= kWant.route_edges && have->route_points >= kWant.route_points;
}

void co_capacity_init(void)
{
    CoComplexNeeds* n = &g_capacity.needs;
    memset(&g_capacity, 0, sizeof g_capacity);
    g_capacity.asked = 1;
    n->capacity_call = HAS(generation_capacity);
    n->live_load = HAS(load_phase) && g_coApi->load_phase(g_coMod) == HF_LOAD_LIVE;
    if (!n->capacity_call) return;
    HfGenerationCapacity have = {.size = sizeof have};
    /* A mod loaded while the game runs is refused whatever it asks: it only reads, so the log names the right reason. */
    n->capacity_granted = g_coApi->generation_capacity(g_coMod, n->live_load ? NULL : &kWant, &have) == 0 && !n->live_load;
    n->regions = RegionsNamed(&have);
}

/* The complex part's needs as this build can check them: the steps that pin stock's stubs, the mod's own sites and the
 * memory are not in it yet, so it is never on; the log says which of the two it is. */
static void Settle(void)
{
    CoComplexNeeds checked = g_capacity.needs;
    checked.stock_stubs = checked.own_sites = 1;
    checked.commit_free_mb = CO_COMPLEX_COMMIT_MB;
    const char* why = co_complex_refusal(&checked);
    g_capacity.settled = 1;
    g_capacity.on = co_complex_refusal(&g_capacity.needs) == NULL;
    if (why != NULL) co_log("complex operations are off: %s", why);
    else if (!g_capacity.on)
        co_log("complex operations are off: this build has the map generator's room (%u locations, a block of 0x%X bytes) and makes none yet",
            g_capacity.have.locations, g_capacity.have.block_bytes);
}

int co_capacity_poll(uint64_t now_ms)
{
    CoComplexNeeds* n = &g_capacity.needs;
    if (g_capacity.settled) return 1;
    if (!g_capacity.asked || !n->capacity_call || !n->regions || n->live_load || !n->capacity_granted) { Settle(); return 1; }
    if (g_capacity.since == 0) g_capacity.since = now_ms != 0 ? now_ms : 1;
    n->level_11 = g_coApi->difficulty_row(CO_COMPLEX_LEVEL) != 0;
    if (!n->level_11)
    {
        if (now_ms - g_capacity.since < CO_GEN_WAIT_MS) return 0;
        Settle();
        return 1;
    }
    HfGenerationCapacity have = {.size = sizeof have};
    g_coApi->generation_capacity(g_coMod, NULL, &have);
    n->capacity_granted = Granted(&have);
    n->regions = RegionsNamed(&have);
    n->moved = have.moved == 1;
    if (n->capacity_granted && n->regions && n->moved) g_capacity.have = have;
    Settle();
    return 1;
}

const CoComplexNeeds* co_capacity_needs(void) { return &g_capacity.needs; }
int co_complex_on(void) { return g_capacity.settled && g_capacity.on; }

uint32_t co_gen_at(int region) { return region >= 0 && region < HF_GEN_REGIONS ? g_capacity.have.regions[region].at : 0; }
uint32_t co_gen_places(int region) { return region >= 0 && region < HF_GEN_REGIONS ? g_capacity.have.regions[region].places : 0; }
uint32_t co_gen_block(void) { return g_capacity.have.block_bytes; }
