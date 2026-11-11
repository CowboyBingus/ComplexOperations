/* Room in the map generator for an extra large map, and where its arrays lie. HellForge owns the generator's block and
 * level 11; the mod asks for capacity as it starts and reads the layout back once level 11 is installed. It keeps no
 * offset of its own: another mod's request moves every array behind it. */
#include <string.h>

#include <windows.h>

#include "co.h"
#include "xl/co_chain.h"
#include "xl/co_nav.h"
#include "xl/co_normal.h"
#include "xl/co_pages.h"
#include "xl/co_rows.h"
#include "xl/co_stamps.h"
#include "xl/co_weld.h"

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
    uintptr_t game;
    const char* early;         /* why the engine's room did not go in; NULL when it did or was not asked */
    int asked, settled, on;
} g_capacity;
static uint32_t g_commitTest; /* tests: the commit to report instead of the machine's */

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

void co_capacity_game(uintptr_t game) { g_capacity.game = game; }

/* What this process can still commit, in MB: an extra large map was measured to reach CO_COMPLEX_COMMIT_MB. */
static uint32_t CommitFree(void)
{
    MEMORYSTATUSEX m = {.dwLength = sizeof m};
    if (g_commitTest != 0) return g_commitTest;
    if (!GlobalMemoryStatusEx(&m)) return 0;
    return m.ullAvailPageFile >> 20 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)(m.ullAvailPageFile >> 20);
}

/* The mod's own sites: every instruction an install of the complex part changes or hooks is the game's. Reads only. */
static const char* OwnSites(uintptr_t game)
{
    const char* why;
    if ((why = co_pages_check(game)) != NULL || (why = co_hud_check(game)) != NULL || (why = co_mark_check(game)) != NULL ||
        (why = co_launch_check(game)) != NULL || (why = co_rows_check(game)) != NULL ||
        (why = co_join_check(game)) != NULL || (why = co_drain_check(game)) != NULL) return why;
    return NULL;
}

/* The engine lays its pools and its navigation grid out once, seconds after game.dll is loaded, and the mod knows only
 * later whether level 11 is there. So the room goes in on what is known now, and only on a machine that could make a
 * complex operation at all: where one of these fails nothing of the engine's is changed. */
void co_capacity_early(void)
{
    CoComplexNeeds* n = &g_capacity.needs;
    const uintptr_t game = g_capacity.game;
    if (!g_capacity.asked || game == 0 || n->early) return;
    if (!n->capacity_call || !n->regions || n->live_load || !n->capacity_granted) return; /* the poll names the reason */
    if (CommitFree() < CO_COMPLEX_COMMIT_MB) { g_capacity.early = "not enough memory free for an extra large map"; return; }
    if ((g_capacity.early = OwnSites(game)) != NULL || (g_capacity.early = co_stamps_room_check(game)) != NULL) return;
    if ((g_capacity.early = co_early_install(game, g_coApi->module_base("helldivers2.exe", NULL))) != NULL) return;
    CoXlEarly(game);
    co_stamps_room(game); /* refused: the engine's room stays, and the poll finds the stamp index too small for the part */
    n->early = 1;
    co_log("the engine has room for a world of %.0f m: physics bodies and static actors 1048576, a navigation grid of %u x %u cells", (double)CO_EARLY_WORLD, CO_EARLY_CELLS, CO_EARLY_CELLS);
}

/* A grid of 32 x 32 is the mod's to plan for every level and the ship: HellForge's step leaves it alone. This does not
 * wait for a complex operation to be possible; without it a normal level keeps the grid's start-up tiles. */
int co_capacity_normal(void)
{
    const char* why;
    if (!g_capacity.needs.early || g_capacity.game == 0) return 1;
    if ((why = co_nav_install(g_capacity.game)) == NULL) return 1;
    co_log("navigation: %s; every level keeps the grid's start-up tiles (32 m over 1024 m)", why);
    return 0;
}

/* Every need, then one line when the part is off. What a complex operation lacks in this build, so that it is never on
 * (CO_COMPLEX_COMPLETE):
 * - the generator's lists on a 16384 m world, each beside HellForge's own step: the city footprint list (0x8000) and
 *   the city location reserve (0xF994A5), the pocket search and reconnect, the clusters' divisor;
 * - a mission's lists: spawners 1024, map markers 64, the status volumes' unit registry (its two more uses of the
 *   game's list, 0x13CC3F6 and 0x13CC03F, and a caller for its install);
 * - an operation's three missions past 30 objectives: the objective list of 48, the result rows, a partner mission
 *   per extra mission;
 * - a client's way out of a level init it should not be in (it gets a normal level and a log line). */
static void Settle(void)
{
    CoComplexNeeds* n = &g_capacity.needs;
    const char* site = NULL;
    const char* why = co_complex_refusal(n);
    g_capacity.settled = 1;
    /* the capacity needs alone say "not this HellForge"; the rest is asked only of a game that can have the part */
    const int capacity = n->capacity_call && n->regions && !n->live_load && n->capacity_granted && n->level_11 && n->moved;
    if (capacity && g_capacity.game != 0 && n->early)
    {
        /* hooked or moved: the game builds its entity state on its own time, and a move that fails then turns the part off */
        n->records = CoXlRecords() > 0;
        n->stock_stubs = co_radii_check(g_capacity.game) == NULL && co_chain_check(g_capacity.game, NULL) == NULL &&
            co_nav_check(g_capacity.game, NULL) == NULL && co_stamps_check(g_capacity.game) == NULL &&
            co_weld_check(g_capacity.game) == NULL;
        n->own_sites = (site = OwnSites(g_capacity.game)) == NULL;
        n->commit_free_mb = CommitFree();
        why = co_complex_refusal(n);
    }
    else if (capacity && g_capacity.game == 0) why = NULL; /* no game to ask: the line below says what this build is */
    g_capacity.on = CO_COMPLEX_COMPLETE && capacity && g_capacity.game != 0 && why == NULL;
    if (why != NULL && capacity && !n->early && g_capacity.early != NULL) co_log("complex operations are off: %s (%s)", why, g_capacity.early);
    else if (why != NULL && site != NULL && n->stock_stubs) co_log("complex operations are off: %s (%s)", why, site);
    else if (why != NULL && n->stock_stubs && n->own_sites && n->records && capacity && g_capacity.game != 0)
        co_log("complex operations are off: %s (%u MB can be committed, %u are needed)", why, n->commit_free_mb, CO_COMPLEX_COMMIT_MB);
    else if (why != NULL) co_log("complex operations are off: %s", why);
    else if (!g_capacity.on)
        co_log("complex operations are off: this build has the map generator's room (%u locations, a block of 0x%X bytes) and makes none yet",
            g_capacity.have.locations, g_capacity.have.block_bytes);
}

int co_complex_start(void)
{
    const char* why;
    if (!co_complex_on()) return 0;
    if ((why = co_rows_install(g_capacity.game)) == NULL && (why = co_drain_install(g_capacity.game)) == NULL &&
        (why = co_join_install(g_capacity.game)) == NULL && (why = co_nav_install(g_capacity.game)) == NULL &&
        (why = co_stamps_install(g_capacity.game)) == NULL && (why = co_weld_install(g_capacity.game)) == NULL &&
        (why = CoXlInstall(g_capacity.game)) == NULL && (why = co_mark_install(g_capacity.game)) == NULL) why = co_launch_install(g_capacity.game);
    if (why == NULL) return 1;
    g_capacity.on = 0; /* what did go in runs the game's own code on every level: nothing is complex without the mark */
    co_log("complex operations are off: %s", why);
    return 0;
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
int co_complex_on(void) { return g_capacity.settled && g_capacity.on && CoXlRecords() >= 0; }

uint32_t co_gen_at(int region) { return region >= 0 && region < HF_GEN_REGIONS ? g_capacity.have.regions[region].at : 0; }
uint32_t co_gen_places(int region) { return region >= 0 && region < HF_GEN_REGIONS ? g_capacity.have.regions[region].places : 0; }
uint32_t co_gen_block(void) { return g_capacity.have.block_bytes; }

#ifdef CO_TEST
void co_capacity_test_commit(uint32_t mb) { g_commitTest = mb; }
/* A machine whose complex part is on or off, for the parts that only ask. */
void co_capacity_test_on(int on) { g_capacity.settled = 1; g_capacity.on = on; }
#endif
