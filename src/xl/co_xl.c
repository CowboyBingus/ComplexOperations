/* The extra large world's prototype, as one unit: the files it was written in (proto/, as they were in HellForge's
 * loader) behind the loader's helper names (co_shim.h) and the stand-ins for the level install it was part of
 * (co_xl_standin.h). The files share static state and rely on the order below, which is the loader's.
 * Every install is reached through CoXlInstall (co_complex_start), but the entity records' hook (CoXlEarly). */
#define CO_XL_UNIT
/* The research calls the loader's Lua bridge exported stay plain functions: the mod's DLL exports none of them. */
#define HF_LUA_API
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tlhelp32.h>
#include "co_shim.h"
#include "co_xl_standin.h"
#include "co_pages.h"
#include "co_normal.h"
#include "co_chain.h"
#include "co_rows.h"
#include "co_objectives.h"
#include "co_nav.h"
#include "co_beside.h"
#include "co_beside_sites.h"
#include "co_stamps.h"
#include "co_stamp_sites.h"
#include "co_weld.h"
#include "co_weld_sites.h"
#include "co_pocket.h"
#include "co_status.h"
#include "co_clusters.h"
#include "co_city.h"
#include "co_locate.h"
#include "co_spawners.h"

/* Seven of the copied files' functions were called from loader code that did not come along (the watch thread, the
 * ship's level change, the density rules of a later branch); they are unused until their callers are written. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "proto/entity_records.c"
#include "proto/difficulty_world_profile.c"
#include "proto/difficulty_world.c"
#include "proto/difficulty_world_plan.c"
#include "proto/difficulty_world_overlap.c"
#include "proto/difficulty_world_poisson.c"
#include "proto/difficulty_world_diag.c"
#include "proto/difficulty_world_terrain.c"
#include "proto/difficulty_world_towns.c"
#include "proto/difficulty_world_gates.c"
#include "proto/difficulty_world_macro.c"
#include "proto/difficulty_status.c"
#include "proto/difficulty_map_hud.c"
#include "proto/difficulty_world_arcs.c"
#include "proto/difficulty_world_watch.c"
#include "proto/difficulty_world_probe.c"
#include "proto/difficulty_stream.c"
#include "proto/difficulty_stream_window.c"
#include "proto/difficulty_world_destroy.c"
#include "proto/difficulty_net_patience.c"
#include "proto/difficulty_world_move.c"
#include "proto/living_world.c"
#include "proto/living_world_mover.c" /* after living_world.c (g_lw); the loader includes it from a later living_world.c */
#include "proto/difficulty_tacmap.c"
#include "proto/test_daylight.c"
#pragma clang diagnostic pop

/* The index query (0xC05360: rcx index, edx key, r8d, r9 buffer, [rsp+0x28] its places) writes at most its places and
 * returns every hit; four callers loop over the count with fixed buffers, and a complex level's dense cells pass them.
 * So a complex level's query returns at most its places, and a tile run's own index answers 0 while it is empty (its
 * count [P+4]; the query stores only to its stack and the buffer). Any other level's query is the game's. */
typedef uint32_t (*IndexQueryFn)(uint64_t, uint32_t, uint32_t, uint64_t, uint32_t);
static IndexQueryFn g_indexQuery;
static uint32_t IndexQueryClamped(uint64_t index, uint32_t key, uint32_t mode, uint64_t buffer, uint32_t places)
{
    if (!co_stamps_complex()) return g_indexQuery(index, key, mode, buffer, places);
    if (g_tileHeaders != NULL)
    {
        uint64_t d = index - 8u - (uint64_t)(uintptr_t)g_tileHeaders;
        if (d < (uint64_t)TILE_RUNNERS_MAX * TILE_HEADER_STRIDE && d % TILE_HEADER_STRIDE == 0 && *(volatile uint32_t*)(g_tileHeaders + d + 4) == 0)
        {
            TileRunner* r = TileRunnerSelf(); /* a runner counts its own: one shared counter from every runner thrashes */
            if (r != NULL) r->emptyQueries++; else InterlockedIncrement64(&g_emptyIndexQueries);
            return 0;
        }
    }
    uint32_t hits = g_indexQuery(index, key, mode, buffer, places);
    return hits < places ? hits : places;
}

/* The game thread's part, in the order the loader's tick called it. */
static void CoXlTick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
    StatusWatch(g_difficultyGame); /* the registry's hash, should the game have made the registry only now */
    WorldStreamTick(dt);
    NetPatienceTick();
    TerrainDiagTick();
    TerrainProbeTick();
    LivingTick();
    DaylightTick();
    TacTick();
}

/* As game.dll is loaded: the records' move must be hooked before the game builds its entity state, and a process moves
 * them once, so it goes in with the engine's room (co_early.c), not with the level's installs. */
void CoXlEarly(uintptr_t game)
{
    static int shim;
    if (!shim++) { co_shim_install(); co_shim_entity_records(&g_entityRecordDisp); }
    EntityRecordsInstall(game);
}
int CoXlRecords(void) { return g_entityRecordsState; }
#ifdef CO_TEST
void CoXlTestRecords(int state) { g_entityRecordsState = state; }
#endif

/* Every install of the prototype, in the loader's order: what it did when game.dll was loaded, then the level
 * install's check and steps, then the tick. NULL, or why a step refused. The loader ran these at three different
 * moments; the mod runs them once level 11 is installed and the complex part is on. The world, the objective fill and
 * the arcs ask each level whether it is complex; the status volumes, weld notes and tactical map have not been shown to. */
const char* CoXlInstall(uintptr_t game)
{
    DifficultyInstall d = {game};
    const char* why;
    if ((why = CoXlSettings(game)) != NULL) return why;
    InstallDaylight(game);
    LwInstallNavConnectNote();
    LwInstallHitNotes();
    LwInstallSmashSpares();
    LwInstallNoise();
    LwInstallDefer();
    if (LwEnabled()) LwInstallMover(); /* the living world's own guard: its callers are the living world's */
    DescInstallNotes();
    if ((why = WorldSitesFailed(game)) != NULL) return why;
    WeldResearchInstall(game);
    if (!ApiCodeMatches(game + CO_STAMP_QUERY, CO_STAMP_QUERY_PIN)) return "the stamp index's query is not the game's code this mod was made for";
    /* the grouped rows' copy counts; refused, a row keeps the objective's name alone */
    if ((why = co_text_install(g_coApi->module_base("helldivers2.exe", NULL), CO_HUD_TEXT_FIRST, 16u * HUD_TEXT_IDS)) != NULL)
        Note("difficulty: HUD: %s; no copy counts", why);
    if ((why = InstallWorld(&d)) != NULL || (why = InstallWorldStreamStep(&d)) != NULL || (why = InstallMapHud(&d)) != NULL) return why;
    if (LoaderHook("the stamp index query's count (at most its places on a complex level)", game + CO_STAMP_QUERY, (void*)IndexQueryClamped, (void**)&g_indexQuery))
        return "the stamp index's query could not be hooked";
    co_nav_ship(WorldShip); /* the ship's navigation step is where play's world goes back to the game's */
    if (g_coApi->on_game_thread(g_coMod, CoXlTick, NULL) == 0) return "the game thread's tick could not be registered";
    return NULL;
}
