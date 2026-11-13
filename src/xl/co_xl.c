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
#include "co_patrols.h"
#include "co_density.h"
#include "co_switch.h"
#include "co_steer.h"

/* Six of the copied files' functions were called from loader code that did not come along (the watch thread, the
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
    co_density_tick(dt);
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

/* The living world: on with the complex part when the game is the reviewed build. Each of its parts acts only while
 * the mission in play is complex (LwLive); a normal mission, the ship and a preview see the game's code. */
static void CoXlLiving(uintptr_t game)
{
    const char* why;
    /* its squads take the place of the mission's director patrols: without the patrols' switch it stays off */
    if (LwEnabled() && (why = co_patrols_install(game)) != NULL) { g_lw.state = -1; Note("living world: off: %s", why); }
    /* without the density guards it runs one budget for the map, and the setup passes stay the game's */
    if (LwEnabled() && (why = co_density_install(game)) != NULL) Note("living world: no density guards: %s", why);
    LwInstallNavConnectNote();
    LwInstallHitNotes();
    LwInstallSmashSpares();
    LwInstallNoise();
    LwInstallDefer();
}

/* The extra large world's guards (proto/living_world_*.c): each acts on a complex level whether or not the living
 * world runs, and is the game's code on any other. The records' room and the boundary requests' list keep the game's
 * memory whole on a complex level, so the part refuses without them; the other two only keep frames short. */
static const char* CoXlGuards(uintptr_t game)
{
    if (g_lw.game == 0) g_lw.game = game; /* the living world sets it once its own pins hold */
    LwInstallRecords();
    LwInstallRender();
    LwInstallMover();
    LwInstallTrees();
    LwInstallZeroBox(); /* absent SpeedTree cells: a switched site, the empty box on complex levels only */
    if (g_lwRec.insertOrig == NULL) return "the destruction records' room on a complex level could not be guarded";
    if (g_lwMover.passOrig == NULL) return "the navmesh boundary requests on a complex level could not be guarded";
    return NULL;
}

/* Every install of the prototype, in the loader's order: what it did when game.dll was loaded, then the level
 * install's check and steps, then the tick. NULL, or why a step refused. The loader ran these at three different
 * moments; the mod runs them once level 11 is installed and the complex part is on. The world, the objective fill and
 * the arcs ask each level whether it is complex; the status volumes' registry is the process's
 * (co_status.c); the weld notes and the tactical map have not been shown to. */
const char* CoXlInstall(uintptr_t game)
{
    DifficultyInstall d = {game};
    const char* why;
    if ((why = CoXlSettings(game)) != NULL) return why;
    InstallDaylight(game);
    CoXlLiving(game);
    if ((why = CoXlGuards(game)) != NULL) return why;
    /* co_steer_install stays out until the empty AI-world slot has a proved cause (DESIGN-AUDIT, d495db9 ruling R1-R4) */
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

#ifdef CO_TEST
/* The mission in play's kind as level init and the ship set it, and the living world's installs, on a fake game. */
void CoXlTestLive(int complex) { g_world.live = complex ? CO_COMPLEX_MAP_SCALE : 1.0f; LwLiveSet(complex); }
/* The living world's pins written into a fake image: the reviewed build's bytes where its code rests on them. */
void CoXlTestLwPins(uint8_t* image)
{
    for (size_t i = 0; i < sizeof kLwPins / sizeof kLwPins[0]; i++)
        for (size_t k = 0; kLwPins[i].hex[2 * k] != 0; k++)
        {
            unsigned v = 0;
            sscanf(kLwPins[i].hex + 2 * k, "%2x", &v);
            image[kLwPins[i].rva + k] = (uint8_t)v;
        }
}
int CoXlTestLiving(uintptr_t game) { if (!LwEnabled()) return 0; CoXlLiving(game); return 1; }
/* The living world's navmesh pins (all in the exe) written into a fake exe, and a complex level's nav tick with no
 * director: 1 when it got past the pins, -1 when they refused. */
void CoXlTestLwNavPins(uint8_t* exe)
{
    for (size_t i = 0; i < sizeof kLwNavPins / sizeof kLwNavPins[0]; i++)
        for (size_t k = 0; kLwNavPins[i].hex[2 * k] != 0; k++)
        {
            unsigned v = 0;
            sscanf(kLwNavPins[i].hex + 2 * k, "%2x", &v);
            exe[kLwNavPins[i].rva + k] = (uint8_t)v;
        }
}
int CoXlTestLwNavTick(uintptr_t game)
{
    g_lw.game = game;
    g_lwNav.pins = 0; g_lwNav.noted = 0;
    if (g_navWorldScale <= 1) return 0; /* not a complex level's grid: the tick does not reach the pins */
    LwNavTick(0);
    return g_lwNav.pins;
}
LONG CoXlTestNoiseHead(void) { return g_lwNoiseHead; }
const char* CoXlTestGuards(uintptr_t game) { return CoXlGuards(game); }
void CoXlTestRenderShrink(uintptr_t map, void (*rehash)(uintptr_t, uint32_t)) { g_lwRender.rehash = (uintptr_t)rehash; LwRenderShrink(map); }
/* The route width's and the scatter set's stubs built at `stub`, and the notes of the guards that act on every level. */
void CoXlTestRouteWidthBuild(uint8_t* stub, uintptr_t flag, uintptr_t back) { RouteWidthBuild(stub, flag, back); }
void CoXlTestScatterSetBuild(uint8_t* stub, uintptr_t count, uintptr_t back) { ScatterSetBuild(stub, count, back); }
void CoXlTestScatterSetNote(volatile LONG* count)
{
    static volatile LONG none[2];
    g_scatOwner.on = 1; g_scatOwner.missRemove = &none[0]; g_scatOwner.missReown = &none[1]; g_scatOwner.outside = count;
    ScatterOwnerNote();
}
void CoXlTestTaskNote(volatile LONG* count) { g_tasksLeftOut = count; GuardNote(); }
void CoXlTestWorldCurrent(float current) { g_world.current = current; }
uint64_t CoXlTestPlaceJob(uint64_t job, uint64_t (*original)(uint64_t, uint64_t, uint64_t, uint64_t)) { g_placeJob = original; return WorldPlaceJob(0, 0, 0, job); }
void CoXlTestHiveLordNote(uint64_t b) { HiveLordNote(b); }
/* Op 0x2B's job regions (proto/difficulty_world.c): how many are made, the op's end on this thread, a job under way. */
int CoXlTestPlaceRegions(void) { int n = 0; for (int k = 0; k < 8; k++) n += g_placeRegions[k] != NULL; return n; }
void CoXlTestPlaceDone(void) { g_placeFree.pending = 1; g_placeFree.thread = GetCurrentThreadId(); WorldPlaceRegionsFree(); }
void CoXlTestPlaceFreeTry(void) { WorldPlaceRegionsFree(); }
void CoXlTestPlaceJobsIn(LONG delta) { InterlockedAdd(&g_placeJobsIn, delta); }
/* A level init's first step for `level`, and a stamp section's hold and cancel test (proto/difficulty_world_terrain.c). */
void CoXlTestLevelInit(uintptr_t level) { LevelInitBegins(); g_worldLevel = level; }
void CoXlTestSectionBegin(void) { SectionBegin(); }
int CoXlTestSectionGone(void) { return SectionGone(); }
void CoXlTestSectionEnd(void) { SectionEnd(); }
/* The destroy queue's hook (proto/difficulty_world_destroy.c) on a fake exe; a mission in play as level init sets it
 * (level 0: the ship, as WorldShip leaves it). */
int CoXlTestDestroyInstall(void) { InstallDestroyQueue(); return g_destroyQueue != NULL; }
void CoXlTestDestroyTick(void) { DestroyTick(); }
LONG CoXlTestDestroyWaiting(void) { return g_destroyWaiting; }
/* Level init and the ship's set-up as the stubs call them, and the near terrain's state they leave or clear
 * (proto/difficulty_world_move.c NearLevelLeft): a complex level's one-mip spawn slot, replay flag, crater hold, tiles. */
void CoXlTestGame(uintptr_t game) { g_world.game = game; }
void CoXlTestWorldLevel(uintptr_t level, uintptr_t preview)
{
    static WorldValues values;
    if (g_world.values == NULL) g_world.values = &values;
    WorldLevel(level, preview);
}
void CoXlTestWorldShip(void) { WorldShip(); }
/* The frame-time levers on a fake exe: the absent cells' switched site (1 in, -1 refused for good, 0 to try again), its
 * path now, and the nav job and vehicle box hooks with what their leave must undo. */
int CoXlTestZeroBox(int reset)
{
    if (reset) { g_lwZb.ready = 0; g_lwZb.refused = 0; }
    LwInstallZeroBox();
    return g_lwZb.ready ? 1 : g_lwZb.refused ? -1 : 0;
}
int CoXlTestZeroBoxPath(void) { return co_switch_path(g_lwZb.exe + LW_ZB_AT); }
void CoXlTestLevers(uintptr_t game) { g_lw.game = game; LwInstallNavJob(); LwInstallVbox(); }
void CoXlTestVboxFrame(void) { LwVboxFrame(); }
int CoXlTestNavJob(DWORD* tid) { if (tid) *tid = g_lwNj.tid; return g_lwNj.installed ? g_lwNj.mode : -1; }
LONG CoXlTestLevels(void) { return g_world.levels; }
/* What op 0x38 (NearOneMipLevel) and op 0x69 (NearOneMipArm) leave once they made the copy and found the slot. */
void CoXlTestOneMipArm(uint64_t tex, uint64_t* renderSlot)
{
    uintptr_t api = 0, table = 0;
    g_oneMip.tex = tex; g_oneMip.level = g_world.levels;
    if (SafeRead(g_world.game + 0x3326308u, &api, 8) || SafeRead(api + 8, &table, 8)) return;
    g_oneMip.original = (void*)(uintptr_t)*(uint64_t*)(table + 0x198);
    g_oneMip.slot = renderSlot; g_oneMip.thread = GetCurrentThreadId(); g_oneMip.armed = 1;
    *(uint64_t*)(table + 0x198) = (uint64_t)(uintptr_t)NearOneMipSpawn;
}
int CoXlTestOneMipArmed(void) { return g_oneMip.armed; }
uint64_t CoXlTestOneMipTex(void) { return g_oneMip.tex; }
uint64_t CoXlTestOneMipSpawn(uint32_t type, uint32_t name) { return NearOneMipSpawn(NULL, NULL, 0, type, name, 0, 0, NULL, NULL, 0); }
void CoXlTestNearActive(LONG on, uint64_t cam) { g_nearActive = on; g_near.cam[0] = cam; }
LONG CoXlTestNearActiveNow(void) { return g_nearActive; }
int CoXlTestCamShift(uint64_t camera) { const float v[3] = {1, 2, 3}; float out[3]; return TerrainNearCamShift(camera, g_world.game + 0xAB0725u, v, out); }
void CoXlTestTileAdd(float cx, float cy, const uint16_t* heights, uint32_t n, float size)
{
    const NearGround ground = {heights, n, size, cx, cy};
    NearTileAdd(NearTileFree(), cx, cy, 0x1234, &ground);
}
int CoXlTestTilesLive(void) { return NearTilesLive(); }
float CoXlTestVisibleAt(float x, float y) { return NearVisibleAt(x, y); }
void CoXlTestHide(uint64_t handle) { g_hide.handle = handle; g_hide.level = g_world.levels; }
uint64_t CoXlTestHideHandle(void) { return g_hide.handle; }
void CoXlTestMoveHold(int on) { MoveHold(on); }
LONG64 CoXlTestMoveHoldSince(void) { return g_moveHoldSince; }
/* A level init's effect on the generator's side alone: the next level, complex (8) or not (1). */
void CoXlTestGenLevel(float current) { InterlockedIncrement(&g_world.levels); g_world.current = current; }
/* The stamp packages a level requested early (EarlyPackages): held as a level that ended before its record 94 leaves them. */
void CoXlTestEarlyHeld(const uint64_t* ids, uint32_t count)
{
    g_early.level = g_world.levels; g_early.state = 1; g_early.count = count;
    memcpy(g_early.list, ids, count * sizeof *ids);
}
int CoXlTestEarlyState(void) { return g_early.state; }
void CoXlTestEarlyStep(uint64_t records, uint64_t index, uint64_t mask) { EarlyPackages(records, 0, index, mask); }
/* What WorldDispatch runs at each op before the level's own (kill heights, water, sea), and what stays to put back. */
void CoXlTestLowered(uint64_t op) { WorldKillHeight(); WorldDryTypes(op); WorldNoSea(op); }
void CoXlTestPending(uint32_t* kill, uint32_t* dry, uint32_t* sea) { *kill = g_killHeight.count; *dry = g_dry.count; *sea = g_sea.count; }
/* The town filters' widening at a complex level's op 0x2B record and the put-back at a level's first op. */
void CoXlTestTownsWiden(uint64_t record) { TownsWidenLevels(record); }
void CoXlTestTownsRestore(void) { TownsRestore(); }
uint32_t CoXlTestTownsPending(void) { return g_townWide.count; }
void CoXlTestMission(uintptr_t level, int complex)
{
    InterlockedIncrement(&g_world.levels);
    g_worldLevelLive = level; g_world.liveLevels = level != 0 ? g_world.levels : 0;
    CoXlTestLive(complex);
}
#endif
