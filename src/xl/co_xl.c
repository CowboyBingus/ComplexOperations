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
#include "co_coarse.h"
#include "co_compare.h"
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
#include "co_unlock.h"
#include "co_spawners.h"
#include "co_patrols.h"
#include "co_density.h"
#include "co_switch.h"
#include "co_steer.h"
#include "co_cutters.h"
#include "../co_watch.h"

/* Six of the copied files' functions were called from loader code that did not come along (the watch thread, the
 * ship's level change, the density rules of a later branch); they are unused until their callers are written. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "proto/xl_free.h"
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
#include "proto/xl_free.c"
#include "proto/living_world.c"
#include "proto/difficulty_tacmap.c"
#include "proto/test_daylight.c"
#include "proto/difficulty_world_memprobe.c"
#include "proto/xl_span_rescue.c"
#include "proto/xl_sun_slice.c"
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
    SpanRescueTick();
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
/* The main world is made once per process, in game.dll's plugin setup (0x4EE160 -> 0xAB0EE0, the only store of
 * [engine+0x10E8] at 0xAB18B7), and the ship and every mission are spawned into it: its pools are sized only if the
 * stubs are in before that. A world already there keeps vanilla pools, which a complex map fills (0x58E14B). */
#define RENDER_ENGINE 0x3326340u     /* game.dll: the setup's engine object, zeroed (0x4EE535) before it is stored (0x4EE564) */
#define RENDER_ENGINE_WORLD 0x10E8u  /* its main world */
static int RenderWorldMade(uintptr_t game)
{
    uint64_t engine = 0, world = 0;
    return g_coApi->read(game + RENDER_ENGINE, &engine, 8) == 0 && engine != 0 &&
        g_coApi->read((uintptr_t)engine + RENDER_ENGINE_WORLD, &world, 8) == 0 && world != 0;
}
const char* CoXlRenderCheck(uintptr_t game, uintptr_t exe)
{
    if (RenderWorldMade(game))
    {
        Note("difficulty: render capacity: the main world was made before the mod could size it; it keeps vanilla pools (262144)");
        return "the main world was made before its culling pools could be enlarged";
    }
    return RenderCapacityPrepare(exe);
}
const char* CoXlRenderWrite(uintptr_t game)
{
    const char* why = RenderCapacityWrite();
    if (why != NULL) return why;
    /* While the setup has not begun, every main world is made through the stubs. Once it has, a world may have read N
     * before the jumps (made stays 0 and +0x10E8 is not set until it returns): only a world the stubs counted proves
     * otherwise. The stubs stay in: putting code back under a thread that may be running it is not safe. */
    if (!co_early_setup_begun(game) || *g_renderCap.made >= 1) return NULL;
    Note("difficulty: render capacity: the game's setup began while the stubs went in and no main world has run them; it may keep vanilla pools (262144)");
    return "the game's setup began while the main world's stubs went in";
}
#ifdef CO_TEST
void CoXlTestRecords(int state) { g_entityRecordsState = state; }
/* The render capacity's state, a fresh process's; its stub page (NULL before the install) and the tile-in note after
 * `made` main worlds. */
void CoXlTestRenderReset(void) { memset(&g_renderCap, 0, sizeof g_renderCap); }
const uint8_t* CoXlTestRenderPage(void) { return g_renderCap.on == 1 ? g_renderCap.page : NULL; }
/* The stubs' counter, as a main world made through them leaves it. */
void CoXlTestRenderMade(LONG made) { if (g_renderCap.page != NULL) *g_renderCap.made = made; }
void CoXlTestRenderNote(LONG made) { if (g_renderCap.on == 1) *g_renderCap.made = made; RenderCapacityNote(); }
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
    SunSliceInstall(game); /* the sun's last shadow slice: about the camera on a world over 2048 m, else dynamic */
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
    /* refused, a forest instance made while the cutter pool was full still crashes the destroy (co_cutters.h) */
    if ((why = co_cutters_install(g_coApi->module_base("helldivers2.exe", NULL))) != NULL) Note("navmesh cutters: %s; a handle of 0 is not left out", why);
    else Note("navmesh cutters: the engine's destroy leaves out handles of 0 (a cutter not made while its pool was full)");
    if ((why = InstallWorld(&d)) != NULL || (why = InstallWorldStreamStep(&d)) != NULL || (why = InstallMapHud(&d)) != NULL) return why;
    /* refused, the game's rule stays: every step on the map holds every main objective */
    if ((why = co_unlock_install(game, g_importantList)) != NULL) Note("complex operations: mission unlock: %s; every step holds every main", why);
    if (g_streamInstallState == 1) SpanRescueInstall(); /* its passes follow the streamer's removals */
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
void CoXlTestRenderCopies(uintptr_t dev, void (*resize)(uintptr_t, uint32_t)) { g_lwRender.resize = (uintptr_t)resize; LwRenderCopiesFree(dev); }
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
/* The band pixels' release (proto/difficulty_world_move.c): tile `slot` live with pixels its texture took at render
 * frame `made`; the drop tile's in g_matband, with a build holding them aside or not. */
void CoXlTestBandTile(int slot, uint32_t* pixels, uint32_t made)
{
    g_tiles[slot].live = 1; g_tiles[slot].level = g_world.levels; g_tiles[slot].bandPixels = pixels; g_tiles[slot].band = 0;
    g_tiles[slot].tex[0] = g_tiles[slot].tex[1] = 0; g_tiles[slot].bandFramed = 1; g_tiles[slot].bandFrame = made;
}
uint32_t* CoXlTestBandTilePixels(int slot) { return g_tiles[slot].bandPixels; }
void CoXlTestBandDrop(uint64_t tex, uint32_t* pixels, uint32_t made, int stashed)
{
    g_matband.tex = tex; g_matband.pixels = pixels; g_matband.framed = 1; g_matband.frame = made;
    g_near.state = NEAR_IN; g_move.stashed = stashed;
}
uint32_t* CoXlTestBandDropPixels(void) { return g_matband.pixels; }
void CoXlTestBandTick(void) { MoveBandPixelsTick(); }
int CoXlTestRenderFrame(uint32_t* frame) { return NearRenderFrame(frame); }
void CoXlTestBandLevelGone(void) { g_matband.tex = 0; MoveLevelGone(); }
/* render_world as the near terrain's wrapper sees it (proto/difficulty_world_terrain.c NearRunAt): a tile replaying
 * (`active`) with its two textures, the level's one-mip heightmap, the call from the job consumer or not. */
uint64_t CoXlTestNearRun(int consumer, int active, uint64_t tile0, uint64_t tile1, uint64_t oneMip, void* original)
{
    g_nearRunOriginal = (NearRunFn)original;
    g_near.tex[0] = tile0; g_near.tex[1] = tile1;
    g_oneMip.tex = oneMip; g_oneMip.level = g_world.levels;
    InterlockedExchange(&g_nearActive, active);
    return NearRunAt(consumer, 1, 2, 3, 0, 5, 6, 7, 8);
}
void CoXlTestNearReplaying(int active) { InterlockedExchange(&g_nearActive, active); }
/* The paths the frees rest on: a dispatcher call's start, the band's stash and take, the pins asked again, the render
 * hook's order and its pins. */
void CoXlTestDispatchBegins(uint64_t records, uint64_t index) { WorldDispatchBegins(records, index, 0); }
void CoXlTestBandBuild(uint64_t tex, uint32_t* pixels, uint32_t made) { MoveBandStash(); g_move.stashed = 1; g_matband.tex = tex; g_matband.pixels = pixels; g_matband.framed = 1; g_matband.frame = made; }
void CoXlTestBandTileIn(int slot) { g_tiles[slot].live = 1; g_tiles[slot].level = g_world.levels; MoveTileTakeBand(slot); MoveRestoreDrop(); }
void CoXlTestRenderFramePins(void) { g_renderFramePinned = -1; }
/* The mover (proto/difficulty_world_move.c) on the current level: grid origin (ox, oy), state `state` with the merge
 * up or not, building cell (a, b); its tiles; one player; the release step and the whole level-update tick. */
void CoXlTestMover(float ox, float oy, int state, int merge, int a, int b)
{
    g_moveInstalled = 1; g_move.on = 1; g_move.level = g_world.levels; g_move.state = state; g_move.merge = merge;
    g_move.origin[0] = ox; g_move.origin[1] = oy; g_move.cell[0] = a; g_move.cell[1] = b; MoveCellCentre(a, b, g_move.centre);
    g_move.players = 0;
}
void CoXlTestMoverTile(int slot, int a, int b, ULONGLONG seen)
{
    memset(&g_tiles[slot], 0, sizeof g_tiles[slot]);
    g_tiles[slot].live = 1; g_tiles[slot].level = g_world.levels; g_tiles[slot].cell[0] = a; g_tiles[slot].cell[1] = b;
    MoveCellCentre(a, b, g_tiles[slot].centre); g_tiles[slot].seen = seen; g_tileHidden[slot].lowered = 0;
}
void CoXlTestMoverPlayer(float x, float y) { g_move.player[g_move.players][0] = x; g_move.player[g_move.players][1] = y; g_move.players++; }
int CoXlTestMoverLive(int slot) { return g_tiles[slot].live && g_tiles[slot].level == g_world.levels; }
ULONGLONG CoXlTestMoverSeen(int slot) { return g_tiles[slot].seen; }
int CoXlTestMoverRelease(ULONGLONG now) { return MoveReleaseTick(now); }
int CoXlTestMoverState(void) { return g_move.state; }
static float TestNoise(void* noise, float x, float y) { return 0.0f; }
void CoXlTestMoverTick(uintptr_t level, float observerX, float observerY)
{
    g_world.live = 8.0f; g_terrainNoise = (TerrainNoiseFn)TestNoise; g_near.level = g_world.levels; g_near.state = NEAR_IN;
    g_streamObserverOn = 1; g_streamObserverAt[0] = observerX; g_streamObserverAt[1] = observerY;
    MoveTick(level);
    g_streamObserverOn = 0;
}
/* rpmalloc's trim after tile events (proto/xl_free.c): its pins asked again, or the reviewed ones taken as passed with
 * the test's trim and statistics; one tile event; one mover frame. */
int CoXlTestTrimPins(void) { g_trim.pins = 0; g_trim.call = g_trim.stats = 0; return MoveTrimPins(); }
uintptr_t CoXlTestTrimCall(void) { return g_trim.call; }
void CoXlTestTrimUse(void (*trim)(uint64_t), void (*stats)(void*))
{
    g_trim.pins = 1; g_trim.call = (uintptr_t)trim; g_trim.stats = (uintptr_t)stats;
    g_trim.level = g_world.levels; g_trim.pending = 0; g_trim.follow = 0; g_trim.events = 0; g_trim.trims = 0;
}
void CoXlTestTrimEvent(const char* why) { MoveTrimEvent(why); }
int CoXlTestTrimTick(void) { return MoveTrimTick(); }
void CoXlTestMoverSet(int state) { g_move.state = state; }
void CoXlTestMoverNear(int state) { g_near.state = state; }
uint32_t CoXlTestMoverFrames(void) { return g_move.frames; }
/* The release's render-fence gate (proto/xl_free.c): the build's world, a tile's terrain handle, the pins asked again. */
void CoXlTestMoverWorld(uint64_t world) { memcpy(g_move.ctx + 0x40, &world, 8); }
void CoXlTestMoverTileHandle(int slot, uint64_t handle) { g_tiles[slot].handle = handle; }
int CoXlTestDrainPins(void) { g_drainPinned = 0; return MoveDrainPins(ApiModuleBase("helldivers2.exe", NULL)); }
/* The map HUD's next zoom level and scale (proto/difficulty_map_hud.c) on a fake game image and map object. */
uint32_t CoXlTestHudZoom(uintptr_t game, uintptr_t map, uint32_t level, float input, float* scale)
{ g_hudGame = game; return (uint32_t)HudZoomTarget(map, level, input, scale); }
void CoXlTestMoverTileIn(int slot, int a, int b)
{ g_move.slot = slot; g_move.cell[0] = a; g_move.cell[1] = b; MoveCellCentre(a, b, g_move.centre); g_near.handle = 0; MoveTileIn(0); g_move.state = MV_IDLE; }
void CoXlTestRenderAfter(uintptr_t state, void (*rehash)(uintptr_t, uint32_t), void (*resize)(uintptr_t, uint32_t))
{ g_lwRender.rehash = (uintptr_t)rehash; g_lwRender.resize = (uintptr_t)resize; LwRenderAfterDispatch(state); }
uintptr_t CoXlTestRenderCopiesPins(uintptr_t exe) { return LwRenderCopiesPins(exe); }
/* A job's end as the recording's hook sees it (return address `ret`), the gate the last dispatch left, the hook itself
 * around `original`, and the job pins. */
void CoXlTestRenderJob(uintptr_t ret, uintptr_t after, uintptr_t job, void (*resize)(uintptr_t, uint32_t))
{ g_lwRender.jobAfter = after; g_lwRender.resize = (uintptr_t)resize; LwRenderJobDone(ret, job); }
void CoXlTestRenderJobHooked(void* original) { g_lwRender.recordOrig = original; }
int CoXlTestRenderJobGate(void) { return g_lwRender.jobGate; }
uint64_t CoXlTestRenderRecord(uint64_t job) { return LwRenderRecord(1, 2, job, 4, 5, 6, 7); }
int CoXlTestRenderJobPins(uintptr_t exe) { return LwRenderJobPins(exe); }
int CoXlTestRenderInstall(void) { LwInstallRender(); return g_lwRender.recordOrig != NULL; }
void* CoXlTestRenderJobsInstall(uintptr_t exe, uintptr_t resize) { g_lwRender.recordOrig = NULL; g_lwRender.resize = resize; LwInstallRenderJobs(exe); return g_lwRender.recordOrig; }
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
/* A level's world written as level init writes it (`mission` 0 a war-table preview), without the rest of level init. */
void CoXlTestWorldWrite(float scale, int mission) { WorldWrite(scale, mission); }
/* The unlock rule's note of a generation (proto/difficulty_world_arcs.c): objective o at record index[o] (its record at
 * rec[o]) with id[o], in block[o] (1-based, 0 none), the block's mission in blockMission. */
void CoXlTestArcUnlockNote(uint64_t lg, uint64_t G, uint32_t o, const uint32_t* index, const uint64_t* rec, const uint32_t* id, const uint8_t* block,
    const uint8_t* blockMission)
{
    static ArcSet s;
    memset(&s, 0, sizeof s);
    s.o = o < ARC_MOST ? o : ARC_MOST;
    memcpy(s.id, id, 4ull * s.o); memcpy(s.block, block, s.o);
    ArcUnlockNote(lg, G, &s, index, rec, blockMission);
}
/* The mission arcs on a fake level: the fill's list as WorldObjectiveFill keeps it, and the cluster pass's call with
 * the first `placed` locations op 0x16's. */
void CoXlTestArcList(uintptr_t game, uint16_t row, const uint32_t* selected, uint32_t n, const uint32_t* list, uint32_t total)
{
    g_difficultyGame = game;
    ArcListKeep(game + DIF_MISSIONS, row, selected, n, list, total);
}
void CoXlTestObjectiveArcs(uintptr_t level, uint64_t a, uint64_t pass, uint64_t clusters, uint32_t n, float centre, uint32_t placed)
{
    LevelKindSet(level, 1);
    memset(g_locationOp, 0, sizeof g_locationOp);
    memset(g_locationOp, 0x16, placed < CO_LOCATIONS_MAX ? placed : CO_LOCATIONS_MAX);
    ObjectiveArcs(a, pass, clusters, n, centre);
}
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
/* The game code a replayed tile step runs under (proto/difficulty_world_terrain.c NearStepPatch): 1 when patched. */
int CoXlTestNearStepPatch(uint8_t op, int on) { return NearStepPatch(op, on); }
/* The layer grids and the stamps put back on new ground (proto/difficulty_world_terrain.c, difficulty_world_move.c,
 * difficulty_stream.c): a tile in a given slot, the coarse ground, a moving tile's build under way, a tile's change, the
 * descriptor and the sample a stamp's layers get, and the streamer's slots around a change. */
int CoXlTestTileAddAt(float cx, float cy, const uint16_t* heights, uint32_t n, float size)
{
    const NearGround ground = {heights, n, size, cx, cy};
    return NearTileAdd(NearTileFree(), cx, cy, 0x1234, &ground);
}
void CoXlTestTileRemove(int slot) { NearTileRemove(slot); }
void CoXlTestCoarse(const uint16_t* heights, uint32_t n, float size) { const NearGround g = {heights, n, size, 0.0f, 0.0f}; g_nearCoarseGround = g; }
void CoXlTestNearBuilding(int building) { g_near.level = g_world.levels; g_near.state = building ? NEAR_START : NEAR_IN; }
void CoXlTestLayerTile(int slot, int in, float cx, float cy) { NearLayerTileChange(slot, in, cx, cy); }
uint64_t CoXlTestLayerField(uint64_t unit, uint64_t field) { return NearLayerField(unit, field); }
int CoXlTestLayerHeight(uint64_t desc, float x, float y, float* h) { return NearLayerHeight(desc, x, y, h); }
void CoXlTestStreamSetup(uintptr_t game, uint8_t* context, uint8_t* array, uint32_t count)
{
    AcquireSRWLockExclusive(&g_streamLock);
    WorldStreamClearLocked();
    g_streamInstallState = 1; g_stream.captured = 1; g_stream.generatorDone = 1; g_stream.level = g_worldLevel; g_stream.levels = g_world.levels;
    g_stream.game = game; g_stream.context = context; g_stream.array = array; g_stream.count = count;
    memset(g_streamMission, 0, sizeof g_streamMission); memset(g_streamUnit, 0, sizeof g_streamUnit); memset(g_streamReplace, 0, sizeof g_streamReplace);
    ReleaseSRWLockExclusive(&g_streamLock);
}
void CoXlTestStreamSlot(uint32_t i, float x, float y, uint64_t unit, int mission)
{
    g_streamAt[i][0] = x; g_streamAt[i][1] = y; g_streamUnit[i] = unit; g_streamMission[i] = (uint8_t)mission;
    g_streamDeferred[i] = 1; g_streamLive[i] = 1; g_streamPoseGood[i] = 1; g_streamNeedsUnit[i] = 0; g_streamSpawned[i] = 1; g_streamResource[i] = 0x51;
}
uint32_t CoXlTestStampsReplace(float cx, float cy) { return MoveStampsReplace(cx, cy, "a test tile"); }
int CoXlTestStreamFlag(uint32_t i) { return g_streamReplace[i]; }
void CoXlTestStreamScan(void)
{
    AcquireSRWLockExclusive(&g_streamLock);
    g_stream.nextScan = 0;
    WorldStreamScanLocked(GetTickCount64());
    ReleaseSRWLockExclusive(&g_streamLock);
}
void CoXlTestStreamSlotState(uint32_t i, int* live, int* due, uint64_t* unit) { *live = g_streamLive[i]; *due = g_streamDue[i]; *unit = g_streamUnit[i]; }
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
/* The engine's scatter update hook (proto/difficulty_world_diag.c) for `view` while record `record` (op `op`) runs:
 * 1 when it called the engine's update, 0 when it left the view out. */
static int g_scatterTestCalled;
static void ScatterTestEngine(void* a, uint8_t* view, uint32_t c, uint8_t* layer, void* e, void* f, void* g, void* h) { g_scatterTestCalled = 1; }
int CoXlTestScatterUpdate(LONG op, LONG record, uint8_t* view)
{
    g_genOp = op; g_genRecord = record; g_tdScatCameraPinned = 1;
    g_tdScatterUpdate = ScatterTestEngine; g_scatterTestCalled = 0;
    TdScatterUpdate(NULL, view, 0, NULL, NULL, NULL, NULL, NULL);
    return g_scatterTestCalled;
}
/* The moving tile's merge window: 0 open, 1 close, 3 reset, as the gate's own calls; 2 one level update's tick
 * (MoveScatterTick) with the mover's render queue holding `queued` entries (-1: no queue to read); 4 a build kicked
 * (MoveMergeBegin) and failed (MoveFail). */
void CoXlTestScatterMerge(int action, LONG64 queued)
{
    static uint8_t queue[0x20];
    if (action == 0) TdScatterMergeOpen();
    else if (action == 1) TdScatterMergeClose();
    else if (action == 2)
    {
        uint64_t at = queued < 0 ? 0 : (uint64_t)(uintptr_t)queue;
        uint32_t n = (uint32_t)queued;
        memcpy(queue + 0x10, &n, 4); memcpy(g_move.ctx + 0x50, &at, 8);
        MoveScatterTick();
    }
    else if (action == 4) { MoveMergeBegin(); MoveFail("a test's failure"); }
    else TdScatterMergeReset();
}
/* The same with the op and record as the last dispatch, the ship or a level init left them. */
int CoXlTestScatterUpdateAsIs(uint8_t* view)
{
    g_tdScatterUpdate = ScatterTestEngine; g_scatterTestCalled = 0;
    TdScatterUpdate(NULL, view, 0, NULL, NULL, NULL, NULL, NULL);
    return g_scatterTestCalled;
}
/* The coarse terrain's draw (CoarseDraw and its callbacks) over `count` tiles the test has built: every slot ready,
 * the dispatcher the test's, no runners. The list is sorted by step as CoarseStart sorts it when batch > 1. */
void CoXlTestCoarseSection(const int* list, int count, int columns, int batch, void* dispatch)
{
    g_worldDispatch = (WorldDispatchFn)dispatch;
    memset(g_coarse.slots, 0, sizeof g_coarse.slots);
    g_coarse.count = count < COARSE_SLOTS_MAX ? count : COARSE_SLOTS_MAX;
    memcpy(g_coarse.list, list, (size_t)g_coarse.count * sizeof *list);
    if (batch > 1) co_coarse_order(g_coarse.list, g_coarse.count, columns);
    g_coarse.slotCount = g_coarse.count;
    for (int k = 0; k < g_coarse.count; k++)
    {
        CoarseSlot* s = &g_coarse.slots[k];
        s->state = STAMP_SLOT_BUILT; s->ok = 1; s->k = k; s->t = g_coarse.list[k];
        memcpy(s->record + 0x18F0, &s->t, 4); /* past the pass: which tile, for the test's dispatcher */
        g_coarse.slotOf[k] = k;
    }
    g_coarse.nextTile = g_coarse.count; g_coarse.inflight = 0; g_coarse.pending = 0; g_coarse.submit = 0; g_coarse.started = 0;
    g_coarse.haveEntry = 0; g_coarse.waitSince = 0; g_coarse.frameTris = 0; g_coarse.frameTrisAll = 0;
    g_terrain.columns = columns; g_terrain.next = 0; g_terrain.drawn = 0; g_terrain.failed = 0; g_terrain.frames = 0;
    co_coarse_begin(&g_coarse.batch, columns, batch, TERRAIN_DRAW_CALLS);
    g_coarse.on = 1;
}
int CoXlTestCoarseFrame(uint64_t context, uintptr_t gen)
{
    LARGE_INTEGER hz, t0;
    QueryPerformanceFrequency(&hz); QueryPerformanceCounter(&t0);
    g_terrain.frames++;
    return CoarseDraw(context, 0, 0, gen, t0, hz);
}
const CoCoarseBatch* CoXlTestCoarseBatch(int* drawn, int* failed)
{
    *drawn = g_terrain.drawn; *failed = g_terrain.failed;
    return &g_coarse.batch;
}
const int* CoXlTestCoarseList(void) { return g_coarse.list; }
int CoXlTestCoarseFree(int k) { return g_coarse.slotOf[k] < 0 && g_coarse.slots[k].state == STAMP_SLOT_FREE; }
/* The coarse compare on (no reference), its texel table checked against the exe as CcOn does; then one image. */
int CoXlTestCompareOn(void) { g_cc.on = 1; g_cc.reference[0] = 0; return CcTable(); }
void CoXlTestCompareImage(const char* label, uintptr_t descriptor) { CcDescriptor(label, descriptor); }
void CoXlTestComparePass(uint32_t op, uintptr_t level) { g_worldLevel = level; CcPass(op); g_worldLevel = 0; }
/* The tactical map (proto/difficulty_tacmap.c): the install's game, the game thread's tick, HellForge's id for the
 * screen, the waypoint the screen queued for the tick, and a session that has not asked for the screen yet. */
void CoXlTestTacGame(uintptr_t game) { g_difficultyGame = game; }
void CoXlTestTacTick(void) { TacTick(); }
int CoXlTestTacScreen(void) { return g_tac.screenId; }
int CoXlTestTacPing(float* x, float* y, int* remove)
{
    *x = g_tac.pingAt[0]; *y = g_tac.pingAt[1]; *remove = g_tac.pingRemove;
    return g_tac.pingPending;
}
void CoXlTestTacRestart(void) { g_tac.started = 0; g_tac.screenId = 0; }
/* The tests have no helldivers2.exe unit registry, so TacSquad finds no avatar and no own slot: the ping's tick for
 * own slot `slot`, and avatar i's waypoint as the snapshot takes it (1 with x, y), go to the readers directly. */
void CoXlTestTacPingAs(int slot)
{
    float size = 0, playable = 0;
    TacTickPing(g_difficultyGame, TacLevel(g_difficultyGame, &size, &playable), slot);
}
int CoXlTestTacWaypoint(uint32_t i, float* x, float* y)
{
    uintptr_t m;
    TacPlayer p = {0};
    if (!EvpPtr(g_difficultyGame + TAC_AVATARS, &m)) return -1;
    TacAvatarState(g_difficultyGame, m, i, 0, 0, &p);
    *x = p.wx; *y = p.wy;
    return p.waypoint;
}
#endif
