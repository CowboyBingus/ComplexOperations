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
#include "co_nav.h"

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

/* The game thread's part, in the order the loader's tick called it. */
static void CoXlTick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
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
    LwInstallMover();
    DescInstallNotes();
    if ((why = WorldSitesFailed(game)) != NULL) return why;
    WeldResearchInstall(game);
    if ((why = InstallWorld(&d)) != NULL || (why = InstallWorldStreamStep(&d)) != NULL || (why = InstallMapHud(&d)) != NULL) return why;
    co_nav_ship(WorldShip); /* the ship's navigation step is where play's world goes back to the game's */
    if (g_coApi->on_game_thread(g_coMod, CoXlTick, NULL) == 0) return "the game thread's tick could not be registered";
    return NULL;
}
