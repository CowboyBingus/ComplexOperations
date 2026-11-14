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
#include "co_unwind.h"
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
#include "co_framecost.h"
#include "co_switch.h"
#include "co_steer.h"
#include "co_aibudget.h"
#include "co_cutters.h"
#include "co_navcut.h"
#include "co_forest.h"
#include "co_podstart.h"
#include "co_settle.h"
#include "co_hitch.h"
#include "co_lw_view.h"
#include "co_lw_roads.h"
#include "co_lw_meet.h"
#include "../co_watch.h"

/* The mod's work in the game frame under way (co_hitch.h): the timed parts add to it, CoXlTick hands it on. */
static CoHitchCost g_hitchCost;
static double HitchNow(void)
{
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f);
    return f.QuadPart > 0 ? (double)t.QuadPart * 1000.0 / (double)f.QuadPart : 0.0;
}

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
#include "proto/difficulty_world_roads.c"
#include "proto/difficulty_world_roadbuild.c"
#include "proto/difficulty_world_roadgrade.c"
#include "proto/difficulty_world_roadfield.c"
#include "proto/difficulty_world_roadlevel.c"
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
#include "proto/difficulty_world_fargrass.c"
#include "proto/xl_free.c"
#include "proto/difficulty_world_fargrass_ground.c"
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

/* From each reinforcement pod's start: the frames and the mod's share in them, noted once the watch is over. A frame is
 * the wall-clock time from the previous tick to this one (the game's dt is clamped: a 1528 ms stall read 43 ms), and it
 * holds that tick's work and the level update after it, so it goes with the work timed since the previous tick began. */
static CoHitch g_hitch;
static long g_hitchPods;
static LONG g_hitchSun;
static double g_hitchLast;
static uint64_t g_hitchIdle, g_hitchBusy; /* the system's CPU times as the watch began (GetSystemTimes, 100 ns) */
static uint64_t HitchFiletime(FILETIME t) { return ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime; }
/* The share of the system's CPU time busy since the watch began, the load the frame times were taken under; -1 unread. */
static double HitchLoad(int begin)
{
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return -1.0;
    const uint64_t i = HitchFiletime(idle), all = HitchFiletime(kernel) + HitchFiletime(user); /* kernel time holds the idle time */
    if (begin) { g_hitchIdle = i; g_hitchBusy = all; return 0.0; }
    const uint64_t di = i - g_hitchIdle, da = all - g_hitchBusy;
    return da > 0 && da >= di ? 100.0 * (double)(da - di) / (double)da : -1.0;
}
static void HitchNote(const CoHitch* h, long pod)
{
    char line[1400]; int n = 0;
    for (int k = 0; k < CO_HITCH_KEEP && h->top[k].ms > 0.0f && n < (int)sizeof line - 200; k++)
    {
        const CoHitchFrame* f = &h->top[k];
        n += snprintf(line + n, sizeof line - n, " +%.2f s %.1f ms (mod %.1f: streamer %.1f with %u destroyed, window %.1f with %u in, spawns %.1f, living world %.1f, tiles %.1f; shadows drawn whole %u);",
            (double)f->atMs / 1000.0, (double)f->ms, (double)co_hitch_own(&f->cost), (double)f->cost.stream, (unsigned)f->cost.removed, (double)f->cost.window,
            (unsigned)f->cost.entered, (double)f->cost.spawn, (double)f->cost.living, (double)f->cost.tiles, (unsigned)f->cost.sunWhole);
    }
    Note("complex operations: frames as pod %ld came in (wall clock, the system's CPU %.0f %% busy): %u over %.1f s, mean %.1f ms; over 16.7 ms %u, 25 ms %u, 33 ms %u, 50 ms %u; the mod's share %.0f ms (%.1f %%), %u stamp units destroyed, far shadows drawn whole %u times; the slowest:%s",
        pod, HitchLoad(0), h->frames, (double)h->sum / 1000.0, h->frames ? (double)h->sum / (double)h->frames : 0.0, h->over16, h->over25, h->over33, h->over50,
        (double)h->own, h->sum > 0.0f ? 100.0 * (double)h->own / (double)h->sum : 0.0, h->removed, h->sunWhole, n ? line : " none");
}
static void HitchTick(void)
{
    const double tick = HitchNow(), frame = g_hitchLast > 0.0 ? tick - g_hitchLast : -1.0;
    g_hitchLast = tick;
    CoHitchCost cost = g_hitchCost;
    memset(&g_hitchCost, 0, sizeof g_hitchCost);
    const LONG sun = g_sun.full;
    cost.sunWhole = (uint16_t)(sun - g_hitchSun);
    g_hitchSun = sun;
    co_hitch_level(&g_hitch, g_world.levels);
    const ULONGLONG now = GetTickCount64();
    const long pods = co_podstart_starts();
    if (pods != g_hitchPods) { g_hitchPods = pods; if (co_hitch_arm(&g_hitch, now)) HitchLoad(1); }
    if (co_hitch_frame(&g_hitch, now, (float)frame, &cost)) HitchNote(&g_hitch, pods);
}

/* The frame times line's context: the AI units and the nearest garrison with living spawners to player 1. */
static void CoXlFrameContext(int* ai, float* nest)
{
    *ai = (int)LwAiCount(); *nest = -1;
    if (g_lwWorld.playersAt <= 0) return;
    float best = 3.0e38f;
    for (int i = 0; i < g_lwGarrisonCount; i++)
        if (g_lwGarrisons[i].haveCenter && g_lwGarrisons[i].spawners) { float d = LwDist2(g_lwWorld.players[0], g_lwGarrisons[i].center[0], g_lwGarrisons[i].center[1]); if (d < best) best = d; }
    if (best < 3.0e38f) *nest = sqrtf(best);
}

/* Frames count only in play: game state 4 ([[game + 0x3326340] + 0xAC21C], as WorldStreamInMission; 6 while the
 * mission loads). LwLive is on from the loading screen, about 38 s before play. */
static int CoXlInPlay(void)
{
    uintptr_t world = 0; uint32_t state = 0;
    return LwLive() && g_difficultyGame != 0 && !SafeRead(g_difficultyGame + 0x3326340u, &world, 8) && world != 0 &&
        !SafeRead(world + 0xAC21Cu, &state, 4) && state == 4;
}

/* 0xC05360 is a leaf that never writes xmm0-xmm5, and its callers keep floats there across the call (0xFA85AA reads
 * xmm2, 0xFCCB63 xmm3, 0xFCCE41 xmm5); the C detour may not, so this thunk keeps them and passes the stack argument on. */
uint32_t CoIndexQueryThunk(uint64_t index, uint32_t key, uint32_t mode, uint64_t buffer, uint32_t places);
uint32_t (*g_coIndexQueryClamped)(uint64_t, uint32_t, uint32_t, uint64_t, uint32_t) = IndexQueryClamped;
__asm__(".text\n.p2align 4\n.globl CoIndexQueryThunk\nCoIndexQueryThunk:\n"
        "  .seh_proc CoIndexQueryThunk\n"
        "  subq $0x98, %rsp\n"
        "  .seh_stackalloc 0x98\n  .seh_endprologue\n"
        "  movdqu %xmm0, 0x30(%rsp)\n  movdqu %xmm1, 0x40(%rsp)\n  movdqu %xmm2, 0x50(%rsp)\n"
        "  movdqu %xmm3, 0x60(%rsp)\n  movdqu %xmm4, 0x70(%rsp)\n  movdqu %xmm5, 0x80(%rsp)\n"
        "  movq 0xC0(%rsp), %rax\n  movq %rax, 0x20(%rsp)\n"
        "  callq *g_coIndexQueryClamped(%rip)\n"
        "  movdqu 0x30(%rsp), %xmm0\n  movdqu 0x40(%rsp), %xmm1\n  movdqu 0x50(%rsp), %xmm2\n"
        "  movdqu 0x60(%rsp), %xmm3\n  movdqu 0x70(%rsp), %xmm4\n  movdqu 0x80(%rsp), %xmm5\n"
        "  addq $0x98, %rsp\n  retq\n"
        "  .seh_endproc\n");

/* The game thread's part, in the order the loader's tick called it. */
static void CoXlTick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)user;
    HitchTick();
    co_framecost_context(CoXlFrameContext);
    co_framecost_begin(CoXlInPlay(), (long)g_world.levels);
    StatusWatch(g_difficultyGame); /* the registry's hash, should the game have made the registry only now */
    co_framecost_mark(CO_FC_STATUS);
    WorldStreamTick(dt);
    co_framecost_mark(CO_FC_STREAM);
    SpanRescueTick();
    co_framecost_mark(CO_FC_SPANS);
    NetPatienceTick();
    co_framecost_mark(CO_FC_NET);
    TerrainDiagTick();
    co_framecost_mark(CO_FC_TDIAG);
    TerrainProbeTick();
    co_framecost_mark(CO_FC_TPROBE);
    const double living = HitchNow();
    LivingTick();
    g_hitchCost.living += (float)(HitchNow() - living);
    co_framecost_mark(CO_FC_LIVING);
    DaylightTick();
    co_framecost_mark(CO_FC_DAYLIGHT);
    TacTick();
    co_framecost_mark(CO_FC_TAC);
    co_density_tick(dt);
    co_framecost_mark(CO_FC_DENSITY);
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
    LwInstallTilt(); /* rotation rays under a detailed tile's ground read it above them */
    LwInstallLeanCap(); /* four-corner units' lean on complex levels */
    LwInstallTrees();
    LwInstallZeroBox(); /* absent SpeedTree cells: a switched site, the empty box on complex levels only */
    SunSliceInstall(game); /* the sun's last shadow slice: about the camera on a world over 2048 m, else dynamic */
    if (g_lwRec.insertOrig == NULL) return "the destruction records' room on a complex level could not be guarded";
    if (g_lwMover.passOrig == NULL) return "the navmesh boundary requests on a complex level could not be guarded";
    return NULL;
}

static float PodStartScale(void) { return co_pages_scale(CO_PAGES_LIVE); }

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
    /* refused, a face under more silhouettes than 8192 half-edges hold still crashes the cutter (co_navcut.h) */
    if ((why = co_navcut_install(g_coApi->module_base("helldivers2.exe", NULL))) != NULL) Note("navmesh cuts: %s; a cut is not bounded", why);
    else
    {
#ifdef CO_RESEARCH
        char limits[32] = ""; unsigned edges = 0, segments = 0; /* HELLFORGE_XL_NAVCUT=<edges>,<segments>; no package reads it */
        if (GetEnvironmentVariableA("HELLFORGE_XL_NAVCUT", limits, sizeof limits) > 0 && sscanf(limits, "%u,%u", &edges, &segments) == 2)
        { co_navcut_limits(edges, segments); Note("navmesh cuts: research limits asked for: %u half-edges, %u segments a face", edges, segments); }
#endif
        Note("navmesh cuts: a face takes silhouettes up to %u segments, and a cut over %u half-edges is done again uncut", CO_NAVCUT_BUDGET, CO_NAVCUT_MAX);
    }
    /* refused, a reinforcement pod still starts at the destroyer and crosses the map to its drop point */
    if ((why = co_podstart_install(game, PodStartScale)) != NULL) Note("complex operations: hellpod start: %s", why);
    if ((why = InstallWorld(&d)) != NULL || (why = InstallWorldStreamStep(&d)) != NULL || (why = InstallMapHud(&d)) != NULL) return why;
    /* refused, the game's rule stays: every step on the map holds every main objective */
    if ((why = co_unlock_install(game, g_importantList)) != NULL) Note("complex operations: mission unlock: %s; every step holds every main", why);
    if (g_streamInstallState == 1) SpanRescueInstall(); /* its passes follow the streamer's removals */
    if (LoaderHook("the stamp index query's count (at most its places on a complex level)", game + CO_STAMP_QUERY, (void*)CoIndexQueryThunk, (void**)&g_indexQuery))
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
/* The living world's held place for the player at EvpPlayers index `i`, peer `peer`, read at `at` (x, y). */
int CoXlTestLwSettle(uint32_t i, uint64_t peer, const float* at, uint64_t now, float* use) { return LwSettleAt(i, peer, at, now, use); }
/* The sight probe's reading of a ray's hits (as 0x64D1D0) and of a static hit against the ground. */
int CoXlTestLwSightVerdict(const uint8_t* hits, uint32_t n, uint32_t target, const float* eye, const float* tpos, float nearR, float folR, int* at)
{ return LwSightVerdict(hits, n, target, eye, tpos, nearR, folR, at); }
int CoXlTestLwStaticKind(float z, float drawn, float coarse, int tile) { return LwStaticKind(z, drawn, coarse, tile); }
int CoXlTestLwSenseRule(uint32_t alert, float body, float reach, uint32_t code, int clear) { return LwSenseRule(alert, body, reach, code, clear); }
float CoXlTestLwSightMod(float range, uint32_t op, float value) { return LwSightMod(range, op, value); }
float CoXlTestLwSenseReach(float visibility, float base, uint32_t op, float value) { return LwSenseReach(visibility, base, op, value); }
float CoXlTestLwBodyDist(float d, float radius) { return LwBodyDist(d, radius); }
int CoXlTestLwSenseGate(uint8_t entity, uint64_t tags) { return LwSenseGate(entity, tags); }
int CoXlTestLwPlaceHeld(const float* live, const float* held) { return LwPlaceHeld(live, held); }
int CoXlTestLwSenseNextPlayer(int last, const uint8_t* ok, int n) { return LwSenseNextPlayer(last, ok, n); }
/* The unit checked next against `player` among n candidates (checked at, metres, player, taken). */
int CoXlTestLwSensePick(const uint64_t* checkedAt, const float* d, const uint8_t* player, const uint8_t* taken, int n, int who)
{
    LwSenseCand c[16] = {0};
    for (int i = 0; i < n && i < 16; i++) c[i] = (LwSenseCand){.d = d[i], .checkedAt = checkedAt[i], .player = player[i], .taken = taken[i]};
    return LwSensePick(c, n < 16 ? n : 16, who);
}
/* The notice table of `n` detectors (at most 8), driven by a script: op 'n' notices `det` through `unit` (-1: no slot,
 * -2: waiting, else its notice count), 's' marks one of its units seen (arg: 1 when it left alert 0), 'c' confirms it. */
int CoXlTestLwSenseDet(int reset, int n, char op, uint32_t det, int arg, uint64_t now)
{
    static LwSenseDet t[8];
    if (reset) memset(t, 0, sizeof t);
    LwSenseDet* e = LwSenseDetOf(t, n, det, now, op == 'n');
    if (!e) return -1;
    if (op == 'n') { if (e->pending || now < e->waitAt) return -2; LwSenseNoticed(e, (uint32_t)arg, 0, now); return e->tries; }
    if (op == 's') { e->present = 1; if (arg) e->left = 1; return 0; }
    return LwSenseConfirm(e, now);
}
int CoXlTestLwHostileRule(uint8_t entity, uint8_t on, uint8_t always, int avatar, uint32_t lists, uint32_t faction, uint32_t mask) { return LwHostileRule(entity, on, always, avatar, lists, faction, mask); }
int CoXlTestLwSenseRoute(float awareness) { return LwSenseRoute(awareness); }
int CoXlTestLwSenseLost(uint32_t flags, uint32_t state) { return LwSenseLost(flags, state); }
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
/* The nav section swap on fake game calls, in g_lwSwap's order: unload, load, asyncMode, inFlight, stepDone, wait,
 * commit, step, runnerMake, runnerFree, queued, permit, join. `jobs` and `inl` stand for the two [W+0x4C8] the AI
 * update sets; 0 as when their code is not the reviewed one. */
void CoXlTestLwNavSwapFakes(uintptr_t table, void* const* fn, uintptr_t jobs, uintptr_t inl)
{
    g_lwFp.joinTicks = 0;
    memset(&g_lwSwap, 0, sizeof g_lwSwap); memset(&g_lwNavHold, 0, sizeof g_lwNavHold); g_lwNav.watching = 0;
    g_lwSwap.unload = (LwNavUnloadFn)fn[0]; g_lwSwap.load = (LwNavLoadFn)fn[1];
    g_lwSwap.asyncMode = (uint8_t (*)(uintptr_t))fn[2]; g_lwSwap.inFlight = (uint8_t (*)(uintptr_t))fn[3]; g_lwSwap.stepDone = (uint8_t (*)(uintptr_t))fn[4];
    g_lwSwap.wait = (void (*)(uintptr_t))fn[5]; g_lwSwap.commit = (void (*)(uintptr_t, void*, int))fn[6];
    g_lwSwap.step = (uintptr_t (*)(uintptr_t, void*, void*, int))fn[7];
    g_lwSwap.runnerMake = (void (*)(void*))fn[8]; g_lwSwap.runnerFree = (void (*)(void*))fn[9];
    g_lwSwap.queued = (int (*)(uintptr_t))fn[10]; g_lwSwap.permit = (int (*)(int))fn[11]; g_lwSwap.join = (void (*)(int))fn[12];
    g_lwSwap.table = table; g_lwSwap.tasksJobs = jobs; g_lwSwap.tasksInline = inl;
}
/* The hooks as the stubs call them, with rbx (the completion's cell) stored first; a tick's start; the level counter. */
uintptr_t CoXlTestLwNavUnload(uintptr_t w, uint32_t* id, uintptr_t cell) { g_lwSwap.cell = cell; return LwNavUnload(w, id, 0); }
uintptr_t CoXlTestLwNavLoad(uintptr_t w, uint32_t* out, uintptr_t cell) { g_lwSwap.cell = cell; return LwNavLoad(w, out, 0, NULL, 0, NULL); }
void CoXlTestLwNavSwapFrame(void) { LwNavSwapFrame(); }
void CoXlTestLwNavLevel(long level) { g_world.levels = level; }
void CoXlTestLwNavSwapSummary(ULONGLONG now) { LwNavSwapSummary(now); }
int CoXlTestLwNavStepThreads(uintptr_t jobs, uintptr_t inl, uintptr_t runner, uintptr_t tasks, int32_t count)
{ return LwNavStepThreads(jobs, inl, runner, tasks, count); }
void CoXlTestLwNavSwapCounts(uint32_t* swaps, uint32_t* steps, uint32_t* parallel, uint32_t* refused, uint32_t* noLoad)
{ *swaps = g_lwSwap.swaps; *steps = g_lwSwap.steps; *parallel = g_lwSwap.parallel; *refused = g_lwSwap.refused; *noLoad = g_lwSwap.noLoad; }
/* The hold on fake memory: the variable holding the nav world pointers and the list's lock calls; the tick's pop at
 * `now` ms; its counts (held, longest, uses, drains, marked); a marked cell's watch entry (the entries after it). */
void CoXlTestLwNavHold(uintptr_t worlds, void (*enter)(uintptr_t), void (*leave)(uintptr_t))
{ g_lwNavHold.worlds = worlds; g_lwNavHold.enter = enter; g_lwNavHold.leave = leave; }
uintptr_t CoXlTestLwNavPop(uintptr_t list, uint32_t flush, ULONGLONG now) { return LwNavPopAt(list, flush, now); }
int CoXlTestLwNavHoldBusy(void) { return LwNavHoldBusy(); }
int CoXlTestLwCutNote(uintptr_t exe, uintptr_t at, uintptr_t bad, const CONTEXT* c) { return LwCutNote(exe, at, bad, c); }
void CoXlTestLwCutReset(void) { g_lwCutNotes = 0; }
void CoXlTestLwNavRing(uintptr_t ring, volatile uint32_t* next) { g_lwNavHold.ring = ring; g_lwNavHold.ringNext = next; }
void CoXlTestLwNavHoldCounts(uint32_t* v)
{
    v[0] = g_lwNavHold.held; v[1] = g_lwNavHold.longest; v[2] = g_lwNavHold.sumRaise; v[3] = g_lwNavHold.drains; v[4] = g_lwNavHold.marked;
    v[5] = g_lwNavHold.sumBack; v[6] = g_lwNavHold.sumBreaks; v[7] = g_lwNavHold.longNoted[0] + g_lwNavHold.longNoted[1] + g_lwNavHold.longNoted[2] + g_lwNavHold.longNoted[3];
}
int CoXlTestLwNavLeft(int read, uint32_t state) { return LwNavLeftAt(read, state); }
int CoXlTestLwPeerParts(int mission, int host) { return LwPeerParts(mission, host); }
int CoXlTestLwNavWriteStart(uintptr_t at, const uint8_t* call) { return LwNavWriteStart(at, call); }
/* The scheduler's call on a fake scheduler, list variable and nav count; the first builds' budget and the time this
 * tick's have taken; the tick summary's counts (paused ticks, pauses, budget waits, lag ms, queue most, the oldest
 * waiting built cell's ms and ring distance, one call's starts). */
void CoXlTestLwNavSchedFakes(void (*sched)(void), volatile uintptr_t* listVar, volatile uint32_t* navCount, int pauseOff)
{ g_lwNavHold.sched = sched; g_lwNavHold.listVar = listVar; g_lwNavHold.navCount = navCount; g_lwNavHold.pauseOff = (uint8_t)pauseOff; }
void CoXlTestLwNavSched(uint32_t flush, ULONGLONG now) { LwNavSchedAt(flush, now); }
void CoXlTestLwNavDrive(void (*drive)(uintptr_t, float)) { g_lwSwap.drive = drive; }
uint32_t CoXlTestLwNavBounded(void) { return g_lwSwap.bounded; }
void CoXlTestLwNavFirstBudget(uint32_t us, uint32_t spentUs) { g_lwNavHold.budgetUs = us; g_lwNavHold.tickFirstUs += spentUs; }
/* A tick `us` after the one before; the first builds' budget it gives. */
uint32_t CoXlTestLwNavTickGap(uint32_t us) { LwNavTickGap(us); return g_lwNavHold.tickBudgetUs; }
void CoXlTestLwNavTickCounts(uint32_t* v)
{
    v[0] = g_lwNavHold.sum.paused; v[1] = g_lwNavHold.sum.pauses; v[2] = g_lwNavHold.sum.over; v[3] = g_lwNavHold.sum.lag; v[4] = g_lwNavHold.sum.queueMost;
    v[5] = g_lwNavHold.builtAge; v[6] = g_lwNavHold.builtLeft; v[7] = g_lwNavHold.can; v[8] = g_lwNavHold.sum.first; v[9] = g_lwNavHold.sum.built;
    v[10] = g_lwNavHold.sum.ticks; v[11] = g_lwNavHold.sum.mostFirst;
}
/* The window's hook on a fake 0xBD11B0, with its bound; the driver's question for world `w`; the later counts (first
 * builds waiting and the oldest one's ms, the span's most, starts put off, the unsynchronised swaps by where, ticks past
 * the hold's bound, the marks' hold); the level's end drains' time, set when `set` is not negative; the scheduler
 * call's install on a fake exe; a frame of the marks' hold. */
void CoXlTestLwNavWindow(uint8_t (*ask)(uintptr_t, void*), uint32_t ms) { g_lwNavHold.startAsk = ask; g_lwNavHold.windowMs = ms; }
int CoXlTestLwNavStartAsk(uintptr_t w) { return LwNavStartAsk(w, NULL); }
void CoXlTestLwNavMoreCounts(uint32_t* v)
{
    v[0] = g_lwNavHold.firstNow; v[1] = g_lwNavHold.firstAge; v[2] = g_lwNavHold.sum.firstMost; v[3] = (uint32_t)g_lwNavHold.kept;
    v[4] = g_lwSwap.sumUnsync[LW_UNSYNC_TICK]; v[5] = g_lwSwap.sumUnsync[LW_UNSYNC_DUE]; v[6] = g_lwSwap.sumUnsync[LW_UNSYNC_END];
    v[7] = g_lwNavHold.sum.pastBound; v[8] = (uint32_t)g_lwSwap.unsafe; v[9] = g_lwNavHold.sum.marksHeld;
}
/* The span's: the oldest first build's wait, the frames the hold asked and a larger budget ran, path requests' wait
 * for a start and the oldest run in flight (ms). */
void CoXlTestLwNavSpanCounts(uint32_t* v)
{
    v[0] = g_lwNavHold.sum.firstOldest; v[1] = g_lwNavHold.sumOver; v[2] = g_lwNavHold.sum.reqWait; v[3] = g_lwNavHold.sum.runAge;
    v[4] = g_lwNavHold.sum.skips; v[5] = g_lwNavHold.sum.skipped; v[6] = g_lwNavHold.sum.dense; v[7] = g_lwNavHold.sum.dueWaits;
}
double CoXlTestLwNavEndMs(double set) { if (set >= 0) g_lwSwap.endMs = set; return g_lwSwap.endMs; }
void CoXlTestLwNavSchedInstall(uintptr_t exe, uintptr_t stub) { LwInstallNavSched(exe, stub); }
int CoXlTestLwNavMarksHeld(void) { return LwNavMarksHeld(); }
/* An AI join of `ms` on this thread, as the hook counts it. */
void CoXlTestLwFpJoin(double ms) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_lwFp.joinTicks += (int64_t)(ms * (double)f.QuadPart / 1000.0 + 0.5); }
/* One frame's budget choice on the AI world `w` at `now`, as LwAibFrame makes it once the game's value `stock` is
 * known: the lever on or off, requests `lat` s old, the run's age as [w+0x484] has it; the budget it leaves at w+0x47C. */
float CoXlTestLwAibWorld(uintptr_t w, float stock, float lat, int on, ULONGLONG now)
{
    float cur = *(float*)(w + 0x47C);
    g_lwAib.b.world = w; g_lwAib.b.stock = stock; g_lwAib.b.raised = 0; g_lwAib.on = on;
    LwAibJoin(LwFpJoinTake(), 1);
    LwAibSteer(w, cur, lat, *(float*)(w + 0x484), now);
    return *(float*)(w + 0x47C);
}
/* A frame whose AI world did not read, as LwAibFrame leaves it: the joins so far are taken and dropped. */
void CoXlTestLwAibUnread(void) { LwAibJoin(LwFpJoinTake(), 0); }
/* The AI drain as the tests set it, and the joins counted for its frames: count, sum and worst in ms. */
void CoXlTestLwAibDrain(int us, int runMs, int queue) { g_lwAib.drain.us = us; g_lwAib.drain.runMs = runMs; g_lwAib.drain.queue = queue; }
void CoXlTestLwAibDrainJoins(uint32_t* n, double* sum, double* worst) { *n = g_lwAib.drainJoins; *sum = g_lwAib.drainJoinSum; *worst = g_lwAib.drainJoinWorst; }
void CoXlTestLwAibReset(void)
{
    memset(&g_lwAib.b, 0, sizeof g_lwAib.b); g_lwAib.on = 1; g_lwAib.wasDraining = 0;
    g_lwAib.drainJoins = 0; g_lwAib.drainJoinSum = g_lwAib.drainJoinWorst = 0;
    g_lwAib.drain = (CoAiDrain){CO_AIB_DRAIN_US, CO_AIB_DRAIN_RUN_MS, CO_AIB_DRAIN_QUEUE};
}
int CoXlTestLwNavWatch(uint16_t cx, uint16_t cy, uint32_t flags, uint32_t section) { LwNavWatch(cx, cy, flags, section, 1); return g_lwNav.watching; }
/* The AI budget lever's choice for a step: the budget it leaves at H+0x47C from `cur`, with requests `lat` s old. */
float CoXlTestLwAibSteer(float stock, float cur, float lat)
{
    float h[0x130]; memset(h, 0, sizeof h);
    h[0x47C / 4] = cur;
    return CoXlTestLwAibWorld((uintptr_t)h, stock, lat, 1, 0);
}
LONG CoXlTestNoiseHead(void) { return g_lwNoiseHead; }
/* An offline reinforcing squad (index 0) on the way to fight 0 at `t`, one player at `pl`, an extra large disc: the
 * offline walk's arrival and band-edge hold (living_world_intent.c), without the game. */
void CoXlTestLwReinforce(const float* pl, const float* at, const float* t)
{
    memset(&g_lwWorld, 0, sizeof g_lwWorld); memset(g_lwFights, 0, sizeof g_lwFights);
    g_lwWorld.count = 1; g_lwWorld.playersAt = 1; g_lwWorld.rMin = 150.0f; g_lwWorld.radius = 7782.0f;
    memcpy(g_lwWorld.players[0], pl, sizeof(float) * 2);
    LwSquad* q = &g_lwSquads[0]; memset(q, 0, sizeof *q);
    ULONGLONG now = GetTickCount64();
    q->state = LW_OFFLINE; q->intent = LW_REINFORCE; q->speed = 2.0f; q->fight = 0; q->fightBorn = now - 2000; q->intentAt = now - 1000; q->intentStageAt = now - 60000;
    q->pos[0] = at[0]; q->pos[1] = at[1]; q->target[0] = t[0]; q->target[1] = t[1];
    g_lwFights[0].live = 1; g_lwFights[0].born = now - 2000;
}
/* The network vector3 types of a config at `cfg`, scanned once per config as at the first level init (a negative
 * scale forgets it): the list (type, widened or not, fields), then each widened type written for `scale`. The count,
 * the types written in the next byte; -1 when the scan fails. */
int CoXlTestNetVec(uintptr_t cfg, float scale, uint32_t* types, uint8_t* widen, uint32_t* fields, int cap, int* pos, int* jobs)
{
    static NetVec v[NET_VEC_TYPES]; static float was[NET_VEC_TYPES]; static uintptr_t last; static int n, sp, sj;
    int wrote = 0;
    uintptr_t descs = 0;
    if (scale < 0) { last = 0; return 0; }
    if (last != cfg)
    {
        n = NetVecScan(cfg, v, NET_VEC_TYPES, &sp, &sj);
        if (n < 0) return -1;
        last = cfg;
        for (int k = 0; k < n; k++) was[k] = v[k].high;
    }
    *pos = sp; *jobs = sj;
    if (SafeRead(cfg + 0x18, &descs, 8)) return -1;
    for (int k = 0; k < n; k++)
    {
        if (k < cap) { types[k] = v[k].type; widen[k] = v[k].widen; fields[k] = v[k].fields; }
        if (v[k].widen) wrote += NetVecWrite(descs + 24u * v[k].type, &v[k], scale, &was[k]);
    }
    return (n < cap ? n : cap) | wrote << 8;
}
float CoXlTestLwFaceReach(int tilesOn, int keeper, float top, float drawn, float wall) { return LwFaceReach(tilesOn, keeper, top, drawn, wall); }
float CoXlTestLwRequestZ(float top, float face) { return LwRequestZ(top, face); }
/* A square grid of route nodes `step` apart over a disc of `radius`, no home regions. */
static void LwTestGrid(float radius, float step)
{
    static int16_t at[128][128];
    int side = (int)(2 * radius / step) + 1;
    g_lwNodeCount = g_lwRoadNodes = 0; g_lwTerr.built = 0;
    for (int j = 0; j < side && j < 128; j++) for (int i = 0; i < side && i < 128; i++)
    {
        float nx = -radius + step * (float)i, ny = -radius + step * (float)j;
        at[j][i] = -1;
        if (nx * nx + ny * ny > radius * radius || g_lwNodeCount >= LW_NODES_MAX) continue;
        LwNode* n = &g_lwNodes[g_lwNodeCount]; memset(n, 0, sizeof *n); n->x = nx; n->y = ny; at[j][i] = (int16_t)g_lwNodeCount++;
        if (i > 0 && at[j][i - 1] >= 0) LwLink(at[j][i], at[j][i - 1]);
        if (j > 0 && at[j - 1][i] >= 0) LwLink(at[j][i], at[j - 1][i]);
    }
    g_lwWorld.radius = radius;
}
/* On that grid, squad 0 on the node nearest (x, y): `plans` new destinations. Returns the plans that found no route;
 * `beyond` counts destinations over LW_ROAM away, `turned` routes that start with the node it came from (the one
 * before in the last route). */
int CoXlTestLwPlans(float radius, float step, float x, float y, int plans, int* beyond, int* turned)
{
    LwSquad* q = &g_lwSquads[0]; int failed = 0;
    *beyond = *turned = 0;
    LwTestGrid(radius, step);
    memset(q, 0, sizeof *q);
    q->node = (uint16_t)LwNearestNode(x, y); q->pos[0] = g_lwNodes[q->node].x; q->pos[1] = g_lwNodes[q->node].y;
    for (int k = 0; k < plans; k++)
    {
        float from[2] = {q->pos[0], q->pos[1]};
        LwPlan(q);
        if (q->pathLen == 0) { failed++; continue; }
        int goal = q->path[q->pathLen - 1];
        if (LwDist2(from, g_lwNodes[goal].x, g_lwNodes[goal].y) > LW_ROAM * LW_ROAM) (*beyond)++;
        if (q->prevValid && q->path[0] == q->prevNode) (*turned)++;
        q->prevNode = q->node; q->prevValid = 1; q->node = q->path[0]; q->pos[0] = g_lwNodes[q->node].x; q->pos[1] = g_lwNodes[q->node].y;
        q->pathLen = q->pathAt = 0;
    }
    return failed;
}
/* That grid with a road node beside every grid node within 1000 m of (x, y), joined to it: squad 1 (no road patrol)
 * there plans `plans` times. Returns the plans whose destination is a road node; `failed` those without a route. */
int CoXlTestLwPlansRoads(float radius, float step, float x, float y, int plans, int* failed)
{
    LwSquad* q = &g_lwSquads[1]; int onRoad = 0;
    *failed = 0;
    LwTestGrid(radius, step);
    const int grid = g_lwNodeCount;
    memset(g_lwNodeKind, 0, sizeof g_lwNodeKind);
    for (int i = 0; i < grid && g_lwNodeCount < LW_NODES_MAX; i++)
    {
        const float at[2] = {x, y};
        if (LwDist2(at, g_lwNodes[i].x, g_lwNodes[i].y) > 1000.0f * 1000.0f) continue;
        LwNode* n = &g_lwNodes[g_lwNodeCount]; memset(n, 0, sizeof *n); n->x = g_lwNodes[i].x + 7.0f; n->y = g_lwNodes[i].y + 7.0f;
        g_lwNodeKind[g_lwNodeCount] = CO_LW_ROADS_POINT; LwLink(i, g_lwNodeCount); g_lwNodeCount++; g_lwRoadNodes++;
    }
    memset(q, 0, sizeof *q);
    q->node = (uint16_t)LwNearestNode(x, y); q->pos[0] = x; q->pos[1] = y;
    for (int k = 0; k < plans; k++)
    {
        q->pathLen = q->pathAt = 0;
        LwPlan(q);
        if (q->pathLen == 0) { (*failed)++; continue; }
        if (q->path[q->pathLen - 1] >= grid) onRoad++;
    }
    const int roads = g_lwRoadNodes;
    g_lwNodeCount = g_lwRoadNodes = 0; memset(g_lwNodeKind, 0, sizeof g_lwNodeKind);
    return roads > 0 ? onRoad : -1;
}
/* The same grid with squad 0 standing at `at` (not moved between plans), come from its node's first neighbour. With
 * `home` {x, y, radius} the nodes inside that disc are its home region and the rest another that does not touch it.
 * Returns the plans without a route; `inHome` counts destinations in the home region, `turned` routes that start back
 * to that neighbour; each of the first `notes` routes is noted twice (one line a route). */
int CoXlTestLwPlansAt(float radius, float step, const float* at, const float* home, int plans, int notes, int* inHome, int* turned)
{
    LwSquad* q = &g_lwSquads[0]; int failed = 0;
    *inHome = *turned = 0;
    LwTestGrid(radius, step);
    memset(q, 0, sizeof *q);
    q->node = (uint16_t)LwNearestNode(at[0], at[1]); q->pos[0] = at[0]; q->pos[1] = at[1];
    if (g_lwNodes[q->node].degree) { q->prevNode = g_lwNodes[q->node].next[0]; q->prevValid = 1; }
    if (home)
    {
        memset(g_lwRegionAdj, 0, sizeof g_lwRegionAdj);
        for (int i = 0; i < g_lwNodeCount; i++) g_lwRegion[i] = LwDist2(home, g_lwNodes[i].x, g_lwNodes[i].y) <= home[2] * home[2] ? 1 : 2;
        g_lwTerr.built = 1; q->home1 = 1;
    }
    for (int k = 0; k < plans; k++)
    {
        q->pathLen = q->pathAt = 0;
        LwPlan(q);
        if (q->pathLen == 0) { failed++; continue; }
        if (LwRegionOfNode(q->path[q->pathLen - 1]) == 0) (*inHome)++;
        if (q->prevValid && q->path[0] == q->prevNode) (*turned)++;
        if (k < notes) { LwRouteNote(q); LwRouteNote(q); } /* the second call writes nothing: one line a route */
    }
    g_lwTerr.built = 0;
    return failed;
}
int CoXlTestLwOnlineSteps(int online, uint64_t wideUntil, uint64_t now) { return LwOnlineSteps(online, wideUntil, now); }
/* Squad 0 online with no game behind it: its tick finds the squad entity gone and frees it. 1 when it was ticked, +2
 * when its keeper listing was closed, +4 when it was counted online. */
int CoXlTestLwOnlineStep(uint64_t wideUntil, uint64_t now)
{
    LwSquad* q = &g_lwSquads[0]; int removals = 0, online = 0;
    memset(q, 0, sizeof *q); q->state = LW_ONLINE; q->squad = 0x1234; q->wide = wideUntil != 0; q->wideUntil = wideUntil; q->cap = q->alive = 6;
    if (g_lwWorld.count < 1) g_lwWorld.count = 1;
    LwOnlineStep(q, &removals, &online, now);
    int r = (q->state != LW_ONLINE ? 1 : 0) | (q->wideUntil == 0 ? 2 : 0) | (online == 1 ? 4 : 0);
    memset(q, 0, sizeof *q);
    return r;
}
int CoXlTestLwNoiseSkip(float playerDist, float reach, uint64_t sinceSent, float moved) { return LwNoiseSkip(playerDist, reach, sinceSent, moved); }
/* Squad 0 with a goal sent at 5000: whether a goal can be sent now, and (*after) its goal time then. */
int CoXlTestLwGoalCanSend(int route, int records, uint64_t* after)
{
    LwSquad* q = &g_lwSquads[0];
    memset(q, 0, sizeof *q); q->goalAt = 5000;
    int r = LwGoalCanSend(q, route, records);
    *after = q->goalAt;
    memset(q, 0, sizeof *q);
    return r;
}
int CoXlTestLwOnProp(float top, float drawn, uint32_t owner, float hitZ) { return LwOnProp(top, drawn, owner, hitZ); }
int CoXlTestLwPropTake(void) { return LW_PROP_TAKE; }
void CoXlTestLwSpacedText(char* out, size_t size, int squad, float dist, uint64_t ago) { LwSpacedText(out, size, squad, dist, ago); }
const char* CoXlTestLwFitText(int ends) { return LwFitText(ends); }
int CoXlTestLwSlotHeightOk(float z, float top, float reach) { return LwSlotHeightOk(z, top, reach); }
int CoXlTestMoveKeepWhich(float z, float ground, int listing) { return MoveKeepWhich(z, ground, listing); }
int CoXlTestMoveSettle(uint32_t e, uint64_t until, uint32_t ask, uint64_t now) { if (ask) MoveSettleAsk(ask, until); return MoveSettleListed(e, now); }
/* Fights for the rule's tests: slot f live at (x, y), born `age` ms ago, seen `seenAgo` ms ago, `engaged` units. */
void CoXlTestLwFight(int f, float x, float y, uint32_t age, uint32_t seenAgo, int engaged, uint64_t now)
{
    LwFight* fi = &g_lwFights[f]; memset(fi, 0, sizeof *fi);
    fi->live = 1; fi->pos[0] = x; fi->pos[1] = y; fi->born = now - age; fi->seen = now - seenAgo; fi->engaged = fi->peak = engaged;
}
void CoXlTestLwFightsClear(void) { memset(g_lwFights, 0, sizeof g_lwFights); g_lwWorld.count = 0; }
int CoXlTestLwFightNearest(float x, float y, float within, int skip) { float at[2] = {x, y}; return LwFightNearest(at, within, skip); }
/* Squad i reinforcing fight f (0: a patrol again, its fight cleared; 2: a patrol that still names the fight). */
void CoXlTestLwFightSquad(int i, int f, int reinforcing)
{
    LwSquad* q = &g_lwSquads[i]; memset(q, 0, sizeof *q);
    q->state = LW_OFFLINE; q->intent = reinforcing == 1 ? LW_REINFORCE : LW_PATROL; q->fight = (int8_t)(reinforcing ? f : -1); q->fightBorn = reinforcing ? g_lwFights[f].born : 0;
    if (g_lwWorld.count <= i) g_lwWorld.count = i + 1;
}
int CoXlTestLwFightSends(int f, uint64_t now, uint64_t sentAgo) { g_lwFights[f].sentAt = sentAgo ? now - sentAgo : 0; return LwFightSends(f, now); }
int CoXlTestLwFightHandOver(int f) { int to = LwFightHandOver(f); g_lwFights[f].live = 0; return to; }
int CoXlTestLwFightOfSquad(int i) { return LwFightOf(&g_lwSquads[i]) ? g_lwSquads[i].fight : -1; }
int CoXlTestLwAlertList(uint32_t id, uint32_t lv, float d, int note)
{
    if (note == 2) memset(&g_lwAlertList, 0, sizeof g_lwAlertList);
    if (id) LwAlertListAdd(id, lv, d * d);
    if (note == 1) LwAlertListNote();
    return (int)g_lwAlertList.n;
}
int CoXlTestLwAlertFights(uint32_t lv) { return LwAlertFights(lv); }
void CoXlTestLwPlayerAt(const float* pl) { memcpy(g_lwWorld.players[0], pl, sizeof(float) * 2); }
/* One offline walk step of squad 0: its stage, held flag, position and stage time; the push timer is cleared first. */
void CoXlTestLwWalk(float dt, int* stage, int* held, float* pos, uint64_t* stageAt)
{
    LwSquad* q = &g_lwSquads[0];
    q->pushAt = 0;
    LwWalk(q, dt);
    *stage = q->intentStage; *held = q->held; pos[0] = q->pos[0]; pos[1] = q->pos[1]; *stageAt = q->intentStageAt;
}
int CoXlTestLwIntent(void) { return g_lwSquads[0].intent; }
/* The refusal gates' counts as the 30 s note lists them (then cleared). */
void CoXlTestLwRefuse(const char* why) { LwRefuseCount(why); }
void CoXlTestLwRefuseList(char* out, size_t size) { LwRefuseList(out, size); }
/* The squad a member belongs to: listed, or lost in the last 10 s. */
void CoXlTestLwLostAdd(uint32_t id, int squad) { LwLostAdd(id, squad); }
int CoXlTestLwSquadOf(uint32_t id, uint64_t later, int* dead) { g_lwWorld.count = 0; return LwSquadOfMember(id, GetTickCount64() + later, dead); }
void CoXlTestLwSlot(const float* pos, uint32_t i, uint32_t n, float* to) { LwMemberSlot(pos, i, n, to); }
/* The units the status note gives as the limit: the bubbles' budgets under the density rules, else the effective cap. */
int CoXlTestLwBudget(int density, int eff, int b0, int b1)
{
    g_lwWorld.eff = eff; g_lwDensity.on = density; g_lwDensity.count = 2; g_lwDensity.budget[0] = b0; g_lwDensity.budget[1] = b1;
    int r = LwUnitBudget(); g_lwDensity.on = -1; return r;
}
/* Every build's note of why a waiting event squad is not online: index 0 (reinforcing) with `why`. */
void CoXlTestLwHeldWhy(const char* why) { g_lwSquads[0].intent = LW_REINFORCE; LwHeldWhy(&g_lwSquads[0], why, 160.0f * 160.0f); }
/* Squad 0 online and calm with a goal (fresh: a new stretch sent at `now`; 2: no goal sent, 3: rushing, 4: a patrol
 * left goal-less by its last send, 5: a retaking squad at its garrison likewise), its leader at
 * `at` at `now` ms: the stall check; the 30 s count of calm still squads (the others' count in `other`). */
uint32_t CoXlTestLwStallOther(void) { return g_lwWorld.stalledOther; }
static float g_testStallPath = -1.0f;
uint32_t CoXlTestLwStall(int fresh, const float* at, const float* goal, uint64_t now)
{
    LwSquad* q = &g_lwSquads[0];
    if (fresh) { memset(q, 0, sizeof *q); q->state = LW_ONLINE; q->cap = 9; float g[3] = {goal[0], goal[1], 0}; LwGoalSent(q, g); q->goalAt = now; g_lwWorld.stalled = g_lwWorld.stalledOther = 0; }
    if (fresh == 2) q->goalAt = 0; /* no goal sent */
    if (fresh == 3) q->rushed = 1;
    if (fresh) q->pathLen = 1; /* a route: a goal send plans none here */
    if (fresh == 4 || fresh == 5 || fresh == 8) { q->goalAt = 0; q->jobSet = 1; } /* a goal send that found nothing */
    if (fresh == 8) q->intent = LW_INVESTIGATE; /* on its way, goal-less */
    if (fresh == 5) { q->intent = LW_RETAKE; q->intentStage = 1; }
    if (fresh == 6 || fresh == 7) { q->intent = LW_REINFORCE; q->intentStage = fresh == 6; } /* at its point; on its way */
    LwStallCheckAt(q, NULL, 0, at, now, 0, g_testStallPath);
    return g_lwWorld.stalled;
}
/* What is left of squad 0's path at the next stall checks (under 0: unknown). */
void CoXlTestLwStallPath(float left) { g_testStallPath = left; }
/* Waiting squads' lanes and points: the lane of the rank-th waiting squad; the k-th point at bearing a from a player
 * at the origin with a 150 m band; squad `idx` of two waiting at one point, 21 s there, moved by the hold. */
int CoXlTestLwHoldLane(int rank) { return LwHoldLane(rank); }
/* The lane a newly waiting squad 2 takes when squads 0 and 1 wait in lanes a and b (99: not waiting). */
int CoXlTestLwHoldFree(int a, int b)
{
    memset(g_lwSquads, 0, 3 * sizeof g_lwSquads[0]); g_lwWorld.count = 3;
    for (int i = 0; i < 3; i++) g_lwSquads[i].state = LW_OFFLINE; /* walking squads keep lane 0 in their record: not taken */
    if (a != 99) { g_lwSquads[0].state = LW_OFFLINE; g_lwSquads[0].held = 1; g_lwSquads[0].holdLane = (int8_t)a; }
    if (b != 99) { g_lwSquads[1].state = LW_OFFLINE; g_lwSquads[1].held = 1; g_lwSquads[1].holdLane = (int8_t)b; }
    return LwHoldFreeLane(&g_lwSquads[2]);
}
void CoXlTestLwHoldPoint(float a, int k, float* out) { const float pl[2] = {0, 0}; LwHoldPoint(pl, a, 150.0f, k, out); }
void CoXlTestLwHoldTwo(int idx, float* pos)
{
    LwSquad* q = &g_lwSquads[idx];
    if (idx == 1) { *q = g_lwSquads[0]; g_lwWorld.count = 2; q->holdLane = (int8_t)LwHoldFreeLane(q); }
    q->heldAt -= 21000; q->pushAt = 0;
    LwIntentHoldAt(q, q->target[0], q->target[1], 1);
    pos[0] = q->pos[0]; pos[1] = q->pos[1];
}
/* Squad 0 waiting in `lane` at bearing 1.0, squad 1 at bearing `other` (waiting 0: not, 1: yes, 2: online with the
 * flag still set): squad 0's shift
 * after n dead-end patches (all nine points refused n times); 99 when its bearing is not its lane's plus the shift. */
float CoXlTestLwHoldTurns(int lane, int n, float other, int waiting)
{
    LwSquad* q = &g_lwSquads[0]; LwSquad* o = &g_lwSquads[1];
    g_lwWorld.count = 2; memset(o, 0, sizeof *o); o->state = waiting == 2 ? LW_ONLINE : LW_OFFLINE; o->held = waiting != 0; o->holdA = other;
    q->state = LW_OFFLINE; q->held = 1; q->holdLane = (int8_t)lane; q->holdA = 1.0f; q->holdShift = 0;
    for (int k = 0; k < n; k++) { q->holdTry = 8; LwHoldNext(q); }
    return fabsf(q->holdA - 1.0f - q->holdShift) < 0.0001f ? q->holdShift : 99.0f;
}
/* Squad 0's target moves (the fight's place from the player changes its lane's bearing). */
void CoXlTestLwHoldTarget(float x, float y) { g_lwSquads[0].target[0] = x; g_lwSquads[0].target[1] = y; }
/* After a refused request, squad 0's next point at the edge. */
void CoXlTestLwHoldNext(float* pos) { LwHoldNext(&g_lwSquads[0]); pos[0] = g_lwSquads[0].pos[0]; pos[1] = g_lwSquads[0].pos[1]; }
void CoXlTestLwStallLines(uint32_t n) { g_lwWorld.stallLines = n; }
uint32_t CoXlTestLwStallNoGoal(void) { return g_lwWorld.stallNoGoal; }
/* Squad 0's stall tries: set when `set` >= 0; what they are. */
int CoXlTestLwStallTries(int set) { if (set >= 0) g_lwSquads[0].stallTries = (uint8_t)set; return g_lwSquads[0].stallTries; }
/* A stalled squad's step (LW_STALL_*: 1 left to a brain action, 2 waits for its path request, 3 goal again, 4 goal
 * moved onto the leader's layer, 5 new goal) from its intent, its tries so far and what the recovery read. */
int CoXlTestLwStallStep(int intent, int tries, int action, int busy, int snapOff, int* after)
{
    LwSquad* q = &g_lwSquads[0]; q->intent = (uint8_t)intent; q->stallTries = (uint8_t)tries;
    int r = LwStallStep(q, action, busy, snapOff); *after = q->stallTries; return r;
}
/* LwOnline's checks for squad 0 (held or not) with placement and room answered by the test: how often each ran, and the
 * retry it left (ms from now, 0 = none). */
static int g_testPlaceOk, g_testRoomOk, g_testPlaces, g_testRooms;
static int TestPlace(LwSquad* q, uintptr_t slot, uint32_t cap, float* pos, float* target) { (void)q; (void)slot; (void)cap; (void)pos; (void)target; g_testPlaces++; return g_testPlaceOk; }
static int TestRoom(uint32_t members) { (void)members; g_testRooms++; snprintf(g_lwRoomWhy, sizeof g_lwRoomWhy, "the cap: bubble 70 online + 9 over 72 (1 players)"); return g_testRoomOk; }
int CoXlTestLwGates(int held, int placeOk, int roomOk, int* places, int* rooms, int* retryMs)
{
    LwSquad* q = &g_lwSquads[0]; float pos[3] = {0}, target[3] = {0};
    q->held = (uint8_t)held; q->retry = 0; g_testPlaceOk = placeOk; g_testRoomOk = roomOk; g_testPlaces = g_testRooms = 0;
    g_lwGates.place = TestPlace; g_lwGates.room = TestRoom;
    int r = LwOnlineGates(q, 0, 9, pos, target);
    g_lwGates.place = LwOnlinePlace; g_lwGates.room = LwRoomFor;
    *places = g_testPlaces; *rooms = g_testRooms; *retryMs = q->retry ? (int)(q->retry - GetTickCount64()) : 0;
    return r;
}
/* Whether squad 0's member slots are sight-checked: waiting at the band's edge or not, `waited` ms into its wait. */
int CoXlTestLwSlotsChecked(int held, uint32_t waited) { LwSquad* q = &g_lwSquads[0]; ULONGLONG now = GetTickCount64(); q->held = (uint8_t)held; q->heldFrom = now - waited; return LwSlotsChecked(q, now); }
uint64_t CoXlTestLwHeldFrom(void) { return g_lwSquads[0].heldFrom; }
/* Goals wait for the job's state entry: the squad's state (-1 unread) and the ms since its job was asked for. */
int CoXlTestLwJobSettled(int state, uint32_t waited) { return LwJobSettled(state, waited); }
/* Squad 0 on its way to node 0 at (nodeX, 0), with a goal at (goalX, 0) sent for node `goalNode` (-2: no goal sent):
 * whether the leader at (atX, 0) has reached the node. */
int CoXlTestLwNodeReached(int intent, float nodeX, float goalX, int goalNode, float atX)
{
    LwSquad* q = &g_lwSquads[0]; float g[3] = {goalX, 0, 0}, at[2] = {atX, 0};
    memset(q, 0, sizeof *q); q->state = LW_ONLINE; q->intent = (uint8_t)intent; q->pathLen = 1; g_lwNodes[0].x = nodeX; g_lwNodes[0].y = 0;
    if (goalNode != -2) { LwGoalSent(q, g); q->goalNode = (int16_t)goalNode; }
    return LwNodeReached(q, at);
}
/* A calm stalled squad's recovery (LwStallApply) with the game's calls counted: squad 0 at (atX, 0) with its goal at
 * (goalX, 0); read[] = {ok, action, busy, held, nav record, leader-layer hit}, the snap at (snapX, 0). calls[] = goal
 * sends, path requests, resumes; to[] = the last goal's and the last path request's x. */
static int g_stallCalls[3]; static float g_stallTo[2];
static void StallGoalStub(uint32_t squad, const float* pos) { (void)squad; g_stallCalls[0]++; g_stallTo[0] = pos[0]; }
static void StallRepathStub(void* nav, uint32_t squad, const float* pos) { (void)nav; (void)squad; g_stallCalls[1]++; g_stallTo[1] = pos[0]; }
static void StallResumeStub(void* squads, uint32_t squad) { (void)squads; (void)squad; g_stallCalls[2]++; }
void CoXlTestLwStallApply(int intent, int tries, const int* read, float atX, float goalX, float snapX, int* calls, float* to, float* goalNow)
{
    LwSquad* q = &g_lwSquads[0]; float at[2] = {atX, 0};
    LwStallRead r = {read[0], read[1], read[2], read[3], read[4], read[5], {snapX, 0, 0}};
    q->intent = (uint8_t)intent; q->stallTries = (uint8_t)tries; q->goalTo[0] = goalX; q->goalTo[1] = 0; q->goalAt = 1; q->state = LW_ONLINE;
    memset(g_stallCalls, 0, sizeof g_stallCalls); g_stallTo[0] = g_stallTo[1] = -1;
    g_lw.goal = StallGoalStub; g_lw.repath = StallRepathStub; g_lw.resume = StallResumeStub;
    LwStallApply(q, at, &r);
    memcpy(calls, g_stallCalls, sizeof g_stallCalls); memcpy(to, g_stallTo, sizeof g_stallTo); *goalNow = q->goalTo[0];
}
uint32_t CoXlTestLwStallCount(int what) { return what == 0 ? g_lwWorld.stallResent : what == 1 ? g_lwWorld.stallSnapped : what == 2 ? g_lwWorld.stallNewGoal : what == 3 ? g_lwWorld.stallResumed : what == 4 ? g_lwWorld.stallAction : g_lwWorld.stallBusy; }
int CoXlTestLwStallWaits(int set) { if (set >= 0) g_lwSquads[0].stallWaits = (uint8_t)set; return g_lwSquads[0].stallWaits; }
/* The band-edge wait's clocks moved back by ms: 0 the intent's start, 1 the time at this point; 2: the fight ends. */
void CoXlTestLwHoldAge(int what, uint32_t ms) { LwSquad* q = &g_lwSquads[0]; if (what == 0) { q->intentAt -= ms; g_lwFights[0].born -= ms; q->fightBorn -= ms; } else if (what == 1) q->heldAt -= ms; else if (what == 2) g_lwFights[0].live = 0; else if (what == 4) q->heldFrom -= ms; else { memset(&g_lwNodes[0], 0, sizeof g_lwNodes[0]); g_lwNodeCount = 1; } } /* 3: one route node, for an intent's end; 4: its wait in this intent */
/* Squad 0 online on its way to fight 0 (live, at `fight`), sent `sentAgo` ms ago, with one route node for an end. A
 * fight's target as the test gives it: live at `at`, or dead (at NULL); target 0 puts the game's read back. */
static int g_testTargetLive; static float g_testTargetAt[3];
static int TestTargetLive(uint32_t target, float* pos) { (void)target; if (g_testTargetLive) memcpy(pos, g_testTargetAt, sizeof g_testTargetAt); return g_testTargetLive; }
void CoXlTestLwFightTarget(int f, uint32_t target, const float* at)
{
    g_lwTargetLive = target ? TestTargetLive : LwLiveTarget; g_lwFights[f].target = target; g_testTargetLive = at != NULL;
    if (at) { g_testTargetAt[0] = at[0]; g_testTargetAt[1] = at[1]; g_testTargetAt[2] = 0; }
}
void CoXlTestLwReinforceOn(const float* fight, uint32_t sentAgo, int intent)
{
    const float pl[2] = {0, 0};
    CoXlTestLwReinforce(pl, fight, fight);
    LwSquad* q = &g_lwSquads[0];
    q->state = LW_ONLINE; q->intent = (uint8_t)intent; q->intentAt = GetTickCount64() - sentAgo; q->fightBorn = g_lwFights[0].born;
    memcpy(g_lwFights[0].pos, fight, sizeof(float) * 2);
    memset(&g_lwNodes[0], 0, sizeof g_lwNodes[0]); g_lwNodeCount = 1;
}
/* One online tick of squad 0's reinforcement `later` ms from now, its leader at `at`, `engaged` members engaged: its
 * intent after it; its stage, point and moves. */
int CoXlTestLwReinforceTick(const float* at, int engaged, uint32_t later, int* stage, float* target, int* moves)
{
    LwSquad* q = &g_lwSquads[0]; const uint32_t members[1] = {0};
    LwReinforceTick(q, members, 1, engaged, at, GetTickCount64() + later);
    *stage = q->intentStage; target[0] = q->target[0]; target[1] = q->target[1]; *moves = q->moves;
    return q->intent;
}
/* The limits of squad 0's intent `later` ms from now: bit 0 late online, bit 1 late at the band's edge. */
int CoXlTestLwIntentLate(uint32_t later) { LwSquad* q = &g_lwSquads[0]; ULONGLONG now = GetTickCount64() + later; return LwIntentLate(q, now) | LwHoldLate(q, now) << 1; }
/* Squad 0 goes offline with its intent and comes back: whether its online time starts again. */
int CoXlTestLwIntentOffline(void) { LwSquad* q = &g_lwSquads[0]; q->state = LW_OFFLINE; LwIntentOffline(q); q->state = LW_ONLINE; return q->intentOnAt == 0; }
/* A scan's cluster of `n` units at levels[] and points pts[] (x, y pairs): its centre, its placing units' centre and count. */
int CoXlTestLwCluster(const float* pts, const uint32_t* levels, int n, float* pos, float* hot)
{
    LwCluster c; memset(&c, 0, sizeof c);
    for (int k = 0; k < n; k++) { float p[3] = {pts[2 * k], pts[2 * k + 1], 0}; if (LwAlertFights(levels[k])) LwClusterAdd(&c, p, LwAlertPlaces(levels[k])); }
    pos[0] = c.pos[0]; pos[1] = c.pos[1]; hot[0] = c.hot[0]; hot[1] = c.hot[1];
    return c.n | c.hotN << 8;
}
/* A cluster at (x, y) into the fights at `now`: n units, `hot` of them at a placing level, a call or not, its target.
 * The fight's slot (-1 none) and, for slot `read`, its point, units and last scan seen. */
int CoXlTestLwFightTake(float x, float y, int n, int hot, int call, uint32_t target, uint64_t now, int read, float* pos, int* engaged, uint64_t* seen)
{
    LwCluster c; memset(&c, 0, sizeof c);
    c.pos[0] = c.hot[0] = c.tpos[0] = x; c.pos[1] = c.hot[1] = c.tpos[1] = y; c.n = n; c.hotN = hot; c.call = (uint8_t)call; c.target = target;
    int f = LwFightTake(&c, now);
    pos[0] = g_lwFights[read].pos[0]; pos[1] = g_lwFights[read].pos[1]; *engaged = g_lwFights[read].engaged; *seen = g_lwFights[read].seen;
    return f;
}
int CoXlTestLwTeleportUnder(float hit, float plan, float drawn) { return LwTeleportUnder(hit, plan, drawn); }
void CoXlTestLwStallSet(int rushed, float dist) { g_lwSquads[0].rushed = (uint8_t)rushed; g_lwSquads[0].stallDist = dist; }
/* An intent's end with no patrol goal set: whether the squad still has a goal. */
int CoXlTestLwIntentEndGoal(void) { LwSquad* q = &g_lwSquads[0]; q->goalAt = 5; q->intent = LW_INVESTIGATE; LwIntentEnd(q, "test"); return q->goalAt != 0; }
int CoXlTestLwGoalNode(void) { float g[3] = {1, 2, 3}; g_lwSquads[0].goalNode = 7; LwGoalSent(&g_lwSquads[0], g); return g_lwSquads[0].goalNode; }
/* A refusal counted, then a new mission's clear: the first 30 s list is empty. */
void CoXlTestLwMissionClear(void) { LwMissionClear(); }
const char* CoXlTestGuards(uintptr_t game) { return CoXlGuards(game); }
void CoXlTestRenderShrink(uintptr_t map, void (*rehash)(uintptr_t, uint32_t)) { g_lwRender.rehash = (uintptr_t)rehash; LwRenderShrink(map); }
void CoXlTestRenderCopies(uintptr_t dev, void (*resize)(uintptr_t, uint32_t)) { g_lwRender.resize = (uintptr_t)resize; LwRenderCopiesFree(dev); }
/* The route width's and the scatter set's stubs built at `stub`, and the notes of the guards that act on every level. */
void CoXlTestRouteWidthBuild(uint8_t* stub, uintptr_t flag, uintptr_t back) { RouteWidthBuild(stub, flag, back); }
void CoXlTestTreeClearBuild(uint8_t* stub, uintptr_t count, uintptr_t back) { TreeClearBuild(stub, count, back); }
void CoXlTestLeafClearBuild(uint8_t* stub, uintptr_t count) { LeafClearBuild(stub, count); }
void CoXlTestTreeClearNote(volatile LONG* a, volatile LONG* b, volatile LONG* c) { g_treeSkip[0] = a; g_treeSkip[1] = b; g_treeSkip[2] = c; TreeClearNote(); }
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
/* The tile's names around the replay's own work (proto/difficulty_world_terrain.c NearNamesOpen, NearConsume): the
 * merge's job consumer on `queue` holding `jobs` (the replay's queue or not), a bracket held around it, a light probe
 * capture, far grass's publish, and the reset a finalize asks for, applied to G at the image's 0x3326340. */
void CoXlTestNearConsume(uint64_t queue, int replays, void* original)
{
    g_nearConsume = (NearConsumeFn)original; g_nearNames.queue = replays ? queue : queue + 0x100;
    NearConsume(queue);
}
int CoXlTestNearNamesOpen(void) { return NearNamesOpen(); }
void CoXlTestNearTextures(uint64_t tile0, uint64_t tile1) { g_near.tex[0] = tile0; g_near.tex[1] = tile1; }
void CoXlTestNearCraterMaps(uint64_t source, uint64_t offset, uint64_t scorch)
{
    g_nearCraterMaps.tex[0] = source; g_nearCraterMaps.tex[1] = offset; g_nearCraterMaps.tex[2] = scorch; g_nearCraterMaps.level = g_world.levels;
}
void CoXlTestNearCraterMapsEarlier(void) { g_nearCraterMaps.level = g_world.levels - 1; }
int CoXlTestNearCraterMapsMake(int newLevel) { if (newLevel) g_nearCraterMaps.level = -1; return NearCraterMapsMake(); }
void CoXlTestNearAtmosMaps(uint64_t color, uint64_t transmittance)
{
    g_nearAtmosMaps.tex[0] = color; g_nearAtmosMaps.tex[1] = transmittance; g_nearAtmosMaps.level = g_world.levels;
}
int CoXlTestNearAtmosMapsMake(int newLevel)
{
    if (newLevel) g_nearAtmosMaps.level = -1;
    NearAtmosMapsMake();
    return (g_nearAtmosMaps.tex[0] != 0) + (g_nearAtmosMaps.tex[1] != 0);
}
/* The replay's end as NearReplayEnd leaves the bracket's state: its queue forgotten, leaked captures noted. */
void CoXlTestNearReplayEnd(void) { NearReplayEnd(1); }
void CoXlTestNearConsumeQueue(uint64_t queue, void* original) { g_nearConsume = (NearConsumeFn)original; NearConsume(queue); }
uint64_t CoXlTestNearNamesQueue(void) { return g_nearNames.queue; }
uint32_t CoXlTestNearProbeOutside(void) { return g_probeRun.outside; }
void CoXlTestNearNamesClose(int opened) { NearNamesClose(opened); }
uint64_t CoXlTestNearProbe(void* original) { g_nearProbe = (NearProbeFn)original; return NearProbe(1, 2, 3, 4, 5, 6, 7, 8, 9); }
void CoXlTestNearProbeCounts(uint32_t* replay, uint32_t* tile) { *replay = g_probeRun.replay; *tile = g_probeRun.tile; }
void CoXlTestNearProbeClear(void) { memset(&g_probeRun, 0, sizeof g_probeRun); }
LONG CoXlTestNearResetState(void) { return g_nearReset.state; }
void CoXlTestNearReset(LONG state, int pins)
{
    g_nearReset.state = state; g_nearReset.pins = pins; g_nearReset.level = g_world.levels; g_nearReset.bare = 0;
}
void CoXlTestNearResetEarlier(void) { g_nearReset.level = g_world.levels - 1; }
LONG CoXlTestNearNamesSky(void) { return g_nearNames.sky; }
void CoXlTestNearResetDone(LONG done) { g_nearReset.done = done; }
void CoXlTestNearAtmosMapsEarlier(void) { g_nearAtmosMaps.level = g_world.levels - 1; }
void CoXlTestNearResetTick(void) { NearResetTick(); }
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
/* The release and tick hooks let a tile change's grid fills finish in the call, as if they took under a frame. */
static void TestLayerSettle(void) { while (MoveLayerWait()) Sleep(1); }
int CoXlTestMoverRelease(ULONGLONG now) { int r = MoveReleaseTick(now); TestLayerSettle(); return r; }
int CoXlTestMoverState(void) { return g_move.state; }
static float TestNoise(void* noise, float x, float y) { return 0.0f; }
void CoXlTestMoverTick(uintptr_t level, float observerX, float observerY)
{
    g_world.live = 8.0f; g_terrainNoise = (TerrainNoiseFn)TestNoise; g_near.level = g_world.levels; g_near.state = NEAR_IN;
    g_streamObserverOn = 1; g_streamObserverAt[0] = observerX; g_streamObserverAt[1] = observerY;
    MoveTick(level);
    TestLayerSettle();
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
/* The build's wall-clock frames (proto/difficulty_world_move.c MoveWallAdd): one frame of `ms` in mover state `mv`; the
 * longest three and the count over 50 ms as the build's note gives them. */
void CoXlTestMoveWall(double ms, int mv) { MoveWallAdd(ms, mv); }
/* The tile's stamp units as 0x2F left them (`units`, n of them, in the world at the context's +0x40), then one frame's
 * destroys within `budget` ms: 1 once all are gone. */
void CoXlTestStampUnits(uint64_t* units, uint32_t n) { g_move.stUnit = units; g_move.stTemp = g_move.stUnits = n; g_move.stGone = 0; }
int CoXlTestStampUnitsDestroy(double budget) { return MoveStampUnitsDestroy(budget); }
uint32_t CoXlTestStampUnitsGone(void) { return g_move.stGone; }
void CoXlTestMoveWallRead(double* wall, int* mv, uint32_t* slow) { for (int k = 0; k < 3; k++) { wall[k] = g_moveCost.wall[k]; mv[k] = g_moveCost.wallMv[k]; } *slow = g_moveCost.slow; }
void CoXlTestMoveWallClear(void) { memset(&g_moveCost, 0, sizeof g_moveCost); }
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
{ g_move.slot = slot; g_move.cell[0] = a; g_move.cell[1] = b; MoveCellCentre(a, b, g_move.centre); g_near.handle = 0; MoveTileIn(0); TestLayerSettle(); g_move.state = MV_IDLE; }
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
/* The unit table alone, as the install leaves it (0 = not known). */
void CoXlTestDestroyTable(uintptr_t at, void* lock, void* unlock) { g_destroyTableAt = at; g_destroyLock = (DestroyLockFn)lock; g_destroyUnlock = (DestroyLockFn)unlock; }
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
/* The next extra large level's op 0x38 and its first replay's drop, the last level's material map being `matTex`. */
void CoXlTestMapsNextLevel(uint64_t matTex, uint64_t matMark)
{
    g_coarseMat.tex = matTex; g_coarseMat.terrain = g_oneMip.terrain; g_coarseMat.mark = matMark; /* as a spawn with the material map leaves them */
    g_world.levels++;
    NearOneMipLevel();
    NearCoarseMatDrop();
}
void CoXlTestNearActive(LONG on, uint64_t cam) { g_nearActive = on; g_near.cam[0] = cam; }
LONG CoXlTestNearActiveNow(void) { return g_nearActive; }
int CoXlTestCamShift(uint64_t camera) { const float v[3] = {1, 2, 3}; float out[3]; return TerrainNearCamShift(camera, g_world.game + 0xAB0725u, v, out); }
void CoXlTestTileAdd(float cx, float cy, const uint16_t* heights, uint32_t n, float size)
{
    const NearGround ground = {heights, n, size, cx, cy};
    NearTileAdd(NearTileFree(), cx, cy, 0x1234, &ground);
}
int CoXlTestTilesLive(void) { return NearTilesLive(); }
/* The tiles' terrains at a level's end (proto/xl_free.c NearTilesDestroy, NearTilesLeft): slot k's handle and maps (k -1:
 * g_near's and g_matband's, the drop tile's), a build in step `move` with the drop tile's handle stashed (0: none), the
 * near state, and what a slot holds after (k -2: the stashed drop handle). */
void CoXlTestTileReset(void)
{
    for (int k = 0; k < NEAR_TILES_MAX; k++) { g_tiles[k].handle = 0; g_tiles[k].tex[0] = g_tiles[k].tex[1] = g_tiles[k].band = 0; g_tiles[k].bandPixels = NULL; g_tiles[k].live = 0; }
    g_near.handle = 0; g_near.tex[0] = g_near.tex[1] = 0; g_near.state = NEAR_IN; g_near.world = 0; g_matband.tex = 0; g_matband.pixels = NULL;
    g_move.state = MV_IDLE; g_move.stashed = 0; g_move.dropHandle = 0; g_move.dropTex[0] = g_move.dropTex[1] = g_move.dropBand = 0; g_move.dropBandPixels = NULL;
    memset(g_move.freeing, 0, sizeof g_move.freeing); memset(g_nearLeft, 0, sizeof g_nearLeft);
    g_move.stTemp = g_move.stGone = 0; g_move.merge = g_move.acquired = g_move.pkgPending = 0; g_move.cutNoted = 0; g_move.shipSeen = 0; g_move.nodeOld = 0; g_nearStampOffset[0] = g_nearStampOffset[1] = 0.0f;
}
void CoXlTestTileTerrain(int k, uint64_t handle, uint64_t tex0, uint64_t tex1, uint64_t band)
{
    if (k < 0) { g_near.handle = handle; g_near.tex[0] = tex0; g_near.tex[1] = tex1; g_matband.tex = band; return; }
    g_tiles[k].handle = handle; g_tiles[k].tex[0] = tex0; g_tiles[k].tex[1] = tex1; g_tiles[k].band = band;
    g_tiles[k].live = 1; g_tiles[k].level = g_world.levels;
}
void CoXlTestTileBuild(int move, uint64_t dropHandle, uint64_t dropTex, int nearState)
{
    g_move.state = move ? MV_SPAWN : MV_IDLE; g_move.stashed = dropHandle != 0; g_move.dropHandle = dropHandle;
    g_move.dropTex[0] = dropTex; g_near.state = nearState ? NEAR_READY : NEAR_IN;
}
uint64_t CoXlTestTileHandle(int k) { return k == -2 ? g_move.dropHandle : k < 0 ? g_near.handle : g_tiles[k].handle; }
uint64_t CoXlTestTileLeft(int i) { return g_nearLeft[i]; } /* a terrain the last replay found still in the world */
/* A tile build the level's end cuts short (proto/xl_free.c MoveBuildLeft): the level object, the mover's step, whether
 * the merge is up and the package held, the stamp units already destroyed, the level's 0x66 record; then what is left
 * of it (0 units, 1 merge, 2 package held, 3 its release pending, 4 the stamp pass's camera offset on), and the
 * build's own teardown, the merge's start, and the tile's centre in y. */
void CoXlTestBuildCut(uintptr_t level, int state, int merge, int held, uint32_t gone, uint64_t records, uint32_t at66)
{
    g_move.levelPtr = level; g_move.state = state; g_move.cutNoted = 0; g_move.shipSeen = 0; g_move.merge = merge; g_move.acquired = held; g_move.pkgPending = 0; g_move.stGone = gone;
    g_move.stamped = g_move.stTemp != 0; g_near.records = records; g_move.at66 = at66;
    g_nearStampOffset[0] = 0.0f; g_nearStampOffset[1] = g_move.stTemp != 0 ? MOVE_STAMP_OFFSET : 0.0f;
    memset(g_move.ctx, 0, 8); /* no arena to reset */
}
void CoXlTestBuildEnv(uint64_t env) { memcpy(g_move.ctx + 0x98, &env, 8); }
int CoXlTestBuildLeft(int what)
{
    return what == 0 ? (int)(g_move.stTemp - g_move.stGone) : what == 1 ? g_move.merge : what == 2 ? g_move.acquired : what == 3 ? g_move.pkgPending :
        g_nearStampOffset[0] != 0.0f || g_nearStampOffset[1] != 0.0f;
}
int CoXlTestTeardown(void) { return MoveTeardown(0); }
void CoXlTestMergeBegin(void) { MoveMergeBegin(); }
void CoXlTestBuildCentre(float y) { g_move.centre[1] = y; }
uint64_t CoXlTestTileTex(int k) { return k == -2 ? g_move.dropTex[0] : k < 0 ? g_near.tex[0] : g_tiles[k].tex[0]; }
void CoXlTestLevelGone(void) { MoveLevelGone(); }
/* The game code a replayed tile step runs under (proto/difficulty_world_terrain.c NearStepPatch): 1 when patched. */
int CoXlTestNearStepPatch(uint8_t op, int on) { return NearStepPatch(op, on); }
/* A replayed step's record copy, and the cone count over a tile's heights centred on (cx, cy) (NearRecordEdit,
 * NearPeaksCount). */
void CoXlTestNearRecordEdit(uint8_t op, uint8_t* one) { NearRecordEdit(op, one); }
uintptr_t CoXlTestNearNoCells(void) { return (uintptr_t)&kNearNoCells; }
void CoXlTestNearPeaks(const float* heights, float cx, float cy, int* narrow, int* wide, float* top, float* x, float* y)
{
    g_near.centre[0] = cx; g_near.centre[1] = cy;
    NearPeaks p; NearPeaksCount(heights, &p);
    *narrow = p.narrow; *wide = p.wide; *top = p.top; *x = p.topX; *y = p.topY;
}
/* The conform's reset for a new tile (NearConformReset): what it keeps of a tile's state, which it is given. */
void CoXlTestNearConformReset(void* coarse, void* low, void* delta, void* block, long running, int phase, void** kept, long* keptRunning, int* keptPhase)
{
    g_conform.coarse = (uint16_t*)coarse; g_conform.low = (float*)low; g_conform.delta = (float*)delta; g_conform.block = (uint8_t*)block;
    g_conform.planRunning = running; g_conform.phase = phase; g_conform.next = 7; g_conform.blocksCoarse = 9;
    NearConformReset();
    kept[0] = g_conform.coarse; kept[1] = g_conform.low; kept[2] = g_conform.delta; kept[3] = g_conform.block;
    *keptRunning = g_conform.planRunning; *keptPhase = g_conform.phase + (int)g_conform.next + (int)g_conform.blocksCoarse;
}
/* A conform cell's change from its triangles at texel (c, r), and how far they miss H - T (NearCellLerp, NearCellMiss). */
float CoXlTestNearCellLerp(const float* delta, int i, int j, float c, float r) { return NearCellLerp(delta, i, j, c, r); }
float CoXlTestNearCellMiss(const float* ring, const float* delta, int i, int j) { return NearCellMiss(ring, delta, i, j); }
float CoXlTestNearTexelMiss(void) { return NEAR_TEXEL_MISS; }
/* A conform grid node's tile height (NearNodeHeight) on a tile centred on (cx, cy). */
float CoXlTestNearNodeHeight(const float* heights, float cx, float cy, int i, int j)
{
    g_near.centre[0] = cx; g_near.centre[1] = cy;
    return NearNodeHeight(heights, i, j);
}
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
void CoXlTestLayerTileLater(int slot, int in, float cx, float cy) { NearLayerTileChangeLater(slot, in, cx, cy); }
int CoXlTestLayerBusy(void) { return NearLayerBusy(); }
void CoXlTestNearLevelLeft(int ship) { NearLevelLeft(ship); }
/* The stream's own poll of a layer change (proto/difficulty_stream.c), whether the change's thread has ended, the
 * changes finished so far. */
int CoXlTestStreamLayerBusy(void) { return WorldStreamLayerBusy(); }
int CoXlTestLayerThreadDone(void) { HANDLE t = g_layerJob.thread; return t != NULL && WaitForSingleObject(t, 0) == WAIT_OBJECT_0; }
uint32_t CoXlTestLayerFills(void) { return (uint32_t)g_nearLayer.fills; }
uint64_t CoXlTestLayerField(uint64_t unit, uint64_t field) { return NearLayerField(unit, field); }
int CoXlTestLayerHeight(uint64_t desc, float x, float y, float* h) { return NearLayerHeight(desc, x, y, h); }
int CoXlTestLayerWhy(uint64_t unit, uint64_t field, int* slot) { int why; NearLayerFieldWhy(unit, field, slot, &why); return why; }
float CoXlTestTraceGridAt(int slot, float x, float y) { return NearTraceGridAt(slot, x, y); }
/* The layer spawn (exe 0xDBEC0) and 0xDFA60 on the fake game: the descriptor each passes on to the engine's own. */
static uint64_t g_testPassed;
static uint64_t TestLayerSpawnOrig(uint64_t unit, uint64_t list, uint64_t count, uint64_t field) { (void)unit; (void)list; (void)count; g_testPassed = field; return 0x5A; }
static uint64_t TestLayerSnapOrig(uint64_t a, uint64_t b, uint64_t c, uint64_t field, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{ (void)a; (void)b; (void)c; (void)e; (void)f; (void)g; (void)h; g_testPassed = field; return 0x5B; }
uint64_t CoXlTestLayerSpawn(uint64_t unit, uint64_t count, uint64_t field)
{
    LayerSpawnFn keep = g_layerSpawn;
    g_layerSpawn = TestLayerSpawnOrig; g_testPassed = 0xDEAD;
    uint64_t r = WorldLayerSpawn(unit, 0x77, count, field);
    g_layerSpawn = keep;
    return r == 0x5A ? g_testPassed : 0xBAD;
}
uint64_t CoXlTestLayerSnap(uint64_t unit, uint64_t field)
{
    WorldSpawnFn keep = g_layerSnap;
    g_layerSnap = TestLayerSnapOrig; g_testPassed = 0xDEAD;
    uint64_t r = WorldLayerSnap(unit, 1, 2, field, 5, 6, 7, 8);
    g_layerSnap = keep;
    return r == 0x5B ? g_testPassed : 0xBAD;
}
void CoXlTestLayerSpawnInstall(uintptr_t exe) { g_layerSpawn = NULL; WorldLayerSpawnInstall(exe); }
uintptr_t CoXlTestLayerSpawnDetour(void) { return (uintptr_t)WorldLayerSpawn; }
uintptr_t CoXlTestLayerSpawnOriginal(void) { return (uintptr_t)g_layerSpawn; }
int CoXlTestGridEnter(uint64_t desc) { return NearLayerGridEnter(desc); }
void CoXlTestGridLeave(int k) { NearLayerGridLeave(k); }
void CoXlTestLayerChosen(void (*fn)(uint64_t)) { g_coTestLayerChosen = fn; }
void CoXlTestGridReady(int slot, LONG ready) { InterlockedExchange(&g_layerGrid[slot].ready, ready); }
void CoXlTestLayerCounts(long* grids, long* spawns, uint32_t* pending) { *grids = g_layerSpawnNote.grids; *spawns = g_layerSpawnNote.spawns; *pending = g_layerPending.count; }
float CoXlTestSeen2(uint32_t i) { return g_streamSeen2[i]; }
float CoXlTestSeen2Of(float side) { return MoveStampSeen2(side); }
/* Unwinds from inside a stub laid over `site` (pre: before its 0x80 frame, else inside it) and from the site itself with
 * the stub's frame taken off: out[0..4] the stub's Rip, Rsp, Rbx, R12 and Xmm6's low half, out[5..9] the site's. */
static int CoXlTestUnwindFrom(uint64_t rip, uint64_t rsp, uint64_t* out)
{
    DWORD64 base = 0, frame = 0; PVOID data = NULL;
    PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(rip, &base, NULL);
    if (fe == NULL) return -1;
    CONTEXT c; memset(&c, 0, sizeof c);
    c.Rip = rip; c.Rsp = rsp;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, rip, fe, &c, &data, &frame, NULL);
    out[0] = c.Rip; out[1] = c.Rsp; out[2] = c.Rbx; out[3] = c.R12; out[4] = c.Xmm6.Low;
    return 0;
}
/* kind 0: a stub with 0x80 of its own (the smash stub's shape); 1: four pushes and 0x80 (the blast stub's, 0xA0).
 * handler: the one the host's data may name. co_unwind_block's refusal comes back as it minus 10. */
int CoXlTestUnwindWith(uintptr_t site, uintptr_t handler, int kind, int pre, uint64_t out[10])
{
    DWORD64 base = 0;
    if (RtlLookupFunctionEntry(site, &base, NULL) == NULL) return -1;
    uint8_t* block = AllocateNearGame((uintptr_t)base, 0x1000);
    static uint64_t stack[0x400];
    if (block == NULL) return -2;
    memset(block, 0xCC, 0x800);
    static const CoUnwindRange small[2] = {{0, 17, 0, 0, {0}}, {17, 102, 7, 1, {CO_UNWIND_ALLOC(7, 0x80)}}};
    static const CoUnwindRange pushes[2] = {{0, 5, 0, 0, {0}},
        {5, 108, 15, 5, {CO_UNWIND_ALLOC(15, 0x80), CO_UNWIND_PUSH(8, 11), CO_UNWIND_PUSH(6, 10), CO_UNWIND_PUSH(4, 9), CO_UNWIND_PUSH(2, 8)}}};
    int rc = co_unwind_block(block, kind ? pushes : small, 2, site, handler, block + 0x800, 0x800);
    if (rc != 0) return rc - 10;
    for (int i = 0; i < 0x400; i++) stack[i] = 0x1000u + (uint64_t)i;
    uint64_t rsp = (uint64_t)(uintptr_t)&stack[0x100];
    if (CoXlTestUnwindFrom((uint64_t)(uintptr_t)block + (pre ? 3u : 60u), rsp, out)) return -4;
    return CoXlTestUnwindFrom((uint64_t)site, pre ? rsp : rsp + (kind ? 0xA0u : 0x80u), out + 5);
}
int CoXlTestUnwind(uintptr_t site, int pre, uint64_t out[10]) { return CoXlTestUnwindWith(site, 0, 0, pre, out); }
int CoXlTestThunkUnwind(uint64_t out[2], uint64_t* stackTop)
{
    static uint64_t stack[0x100];
    for (int i = 0; i < 0x100; i++) stack[i] = 0x2000u + (uint64_t)i;
    *stackTop = (uint64_t)(uintptr_t)&stack[0];
    uint64_t r[5];
    int k = CoXlTestUnwindFrom((uint64_t)(uintptr_t)CoIndexQueryThunk + 13u, (uint64_t)(uintptr_t)&stack[0], r);
    out[0] = r[0]; out[1] = r[1];
    return k;
}
/* The two leaf thunks against a target that wipes xmm0-xmm5: out gets xmm1-xmm5 after the call, the target's r9 and
 * fifth argument, and the address the runner's call returns to. which 0: the layer sampler's, 1: the index query's. */
uint64_t g_coTestR9, g_coTestFifth;
uint64_t CoXlTestWipe(void);
__asm__(".text\n.p2align 4\nCoXlTestWipe:\n"
        "  movq %r9, g_coTestR9(%rip)\n  movq 0x28(%rsp), %rax\n  movq %rax, g_coTestFifth(%rip)\n"
        "  xorps %xmm0, %xmm0\n  xorps %xmm1, %xmm1\n  xorps %xmm2, %xmm2\n"
        "  xorps %xmm3, %xmm3\n  xorps %xmm4, %xmm4\n  xorps %xmm5, %xmm5\n"
        "  movl $0x40E00000, %eax\n  movd %eax, %xmm0\n  movl $77, %eax\n  retq\n");
uint64_t CoXlTestThunkRunAsm(void* thunk, float* out, uint64_t fifth);
__asm__(".text\n.p2align 4\nCoXlTestThunkRunAsm:\n"
        "  pushq %rbx\n  subq $0x30, %rsp\n  movq %rdx, %rbx\n  movq %r8, 0x20(%rsp)\n  movq %rcx, %rax\n"
        "  movl $0x3F000000, %r10d\n  movd %r10d, %xmm0\n  movl $0x3F800000, %r10d\n  movd %r10d, %xmm1\n"
        "  movl $0x40000000, %r10d\n  movd %r10d, %xmm2\n  movl $0x40400000, %r10d\n  movd %r10d, %xmm3\n"
        "  movl $0x40800000, %r10d\n  movd %r10d, %xmm4\n  movl $0x40A00000, %r10d\n  movd %r10d, %xmm5\n"
        "  movl $0x11, %ecx\n  movl $0x22, %edx\n  movl $0x33, %r8d\n  movl $0x44, %r9d\n"
        "  callq *%rax\n"
        "0:\n  movss %xmm0, 0(%rbx)\n  movss %xmm1, 4(%rbx)\n  movss %xmm2, 8(%rbx)\n  movss %xmm3, 12(%rbx)\n"
        "  movss %xmm4, 16(%rbx)\n  movss %xmm5, 20(%rbx)\n  leaq 0b(%rip), %rcx\n  movq %rcx, 24(%rbx)\n"
        "  addq $0x30, %rsp\n  popq %rbx\n  retq\n");
uint64_t CoXlTestThunk(int which, float* out, uint64_t* r9, uint64_t* fifth)
{
    float (*sample)(uint64_t*, float, float, uintptr_t) = g_coWorldLayerSample;
    uint32_t (*query)(uint64_t, uint32_t, uint32_t, uint64_t, uint32_t) = g_coIndexQueryClamped;
    g_coWorldLayerSample = (float (*)(uint64_t*, float, float, uintptr_t))(void*)CoXlTestWipe;
    g_coIndexQueryClamped = (uint32_t (*)(uint64_t, uint32_t, uint32_t, uint64_t, uint32_t))(void*)CoXlTestWipe;
    g_coTestR9 = g_coTestFifth = 0;
    uint64_t r = CoXlTestThunkRunAsm(which == 0 ? (void*)CoWorldLayerSampleThunk : (void*)CoIndexQueryThunk, out, 0x5555);
    g_coWorldLayerSample = sample; g_coIndexQueryClamped = query;
    *r9 = g_coTestR9; *fifth = g_coTestFifth;
    return r;
}
const float* CoXlTestHeightCopy(void) { return g_heightCopy; }
void CoXlTestProbePlayer(int count, float x, float y)
{
    g_stream.playerCount = count; g_streamPlayerAt[0][0] = x; g_streamPlayerAt[0][1] = y;
    g_stream.captured = 1; g_stream.levels = g_world.levels; g_streamStart[0] = x; g_streamStart[1] = y;
}
void CoXlTestProbe(char kind, float x, float y, float used, int why) { NearLayerTrace(kind, 0, x, y, 0.0f, why == 0, why, used); }
void CoXlTestProbeCounts(long* reads, long* off, long* written) { *reads = g_layerProbe.reads; *off = g_layerProbe.off; *written = g_layerProbe.written; }
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
uint32_t CoXlTestStampsReplace(int slot, float cx, float cy) { return MoveStampsReplace(slot, NULL, cx, cy, "a test tile"); }
/* A tile goes as MoveRelease takes it out: its copy first, then out of the list and its grid, then the stamps. */
uint32_t CoXlTestTileGo(int slot)
{
    NearTile t = g_tiles[slot];
    NearTileRemove(slot);
    NearLayerTileChange(slot, 0, t.centre[0], t.centre[1]);
    return MoveStampsReplace(slot, &t, t.centre[0], t.centre[1], "a test tile went");
}
/* The level's kept layer heightfield (difficulty_world.c WorldLayerSnap): data, columns x rows over the world's extent
 * around (0, 0); NULL: none. */
void CoXlTestHeightKept(float* data, uint32_t cols, uint32_t rows, float extent)
{
    memset(g_heightField, 0, sizeof g_heightField);
    g_heightCopy = data; g_heightLevel = data != NULL ? g_world.levels : -1; g_heightBytes = (uint64_t)cols * rows * 4u;
    uint64_t at = (uint64_t)(uintptr_t)data; float e[2] = {extent, extent}; uint32_t dims[2] = {cols, rows};
    memcpy(g_heightField, &at, 8); memcpy(g_heightField + 0x14, e, 8); memcpy(g_heightField + 0x1C, dims, 8);
}
float CoXlTestVisibleWithNone(float x, float y) { return NearVisibleWith(x, y, -1, NULL); }
void CoXlTestStreamObserver(int on, float x, float y) { g_streamObserverOn = on; g_streamObserverAt[0] = x; g_streamObserverAt[1] = y; }
/* WindowRespawnRoomLocked for slot i with its nearest player sqrt(distance2) away, the gates read afresh. */
int CoXlTestRespawnRoom(uint32_t i, float distance2)
{
    int gates[2] = {-1, -1};
    g_streamDistance[i] = distance2;
    return WindowRespawnRoomLocked(i, gates);
}
static uint8_t TestSpawnNone(uint8_t* pass, uint8_t* record, uint8_t* context) { return 0; }
/* One spawn pass with slot i due for a new stamp unit, its nearest player sqrt(distance2) away: the passes held over
 * budget so far (the fake game has no SpawnUnit, so an unheld one goes no further). */
uint32_t CoXlTestSpawnDue(uint32_t i, float distance2)
{
    WorldStreamSpawnFn spawn = g_streamSpawn;
    uint64_t source = g_heightSource;
    g_streamSpawn = TestSpawnNone; g_heightSource = 1;
    g_streamDue[i] = 1; g_streamSpawned[i] = 0; g_streamLive[i] = 0; g_streamNeedsUnit[i] = 1; g_streamDistance[i] = distance2;
    g_stream.deferred = 1; g_streamOrder[0] = i; g_stream.orderCount = 1; g_stream.orderCursor = 0;
    WorldStreamSpawnDueLocked();
    g_streamSpawn = spawn; g_heightSource = source;
    return g_stream.respawnHeld;
}
int CoXlTestStreamFlag(uint32_t i) { return g_streamReplace[i]; }
void CoXlTestStreamFlagClear(uint32_t i) { g_streamReplace[i] = 0; }
int CoXlTestStreamQueued(uint32_t i) { return g_streamDrop[i]; }
/* One scan; `frames` frames' removals after it (each WorldStreamTick's drain), or all of them for -1. The removals. */
uint32_t CoXlTestStreamScanFrames(int frames)
{
    AcquireSRWLockExclusive(&g_streamLock);
    g_stream.nextScan = 0;
    WorldStreamScanLocked(GetTickCount64());
    uint32_t done = 0;
    for (int f = 0; frames < 0 ? g_streamDropCount > 0 : f < frames; f++) done += WorldStreamDrainLocked(kStreamDropsPerFrame, 1.0e9);
    ReleaseSRWLockExclusive(&g_streamLock);
    return done;
}
void CoXlTestStreamScan(void) { CoXlTestStreamScanFrames(-1); }
/* A frame's removals under `ms`: how many went, and how many wait. */
uint32_t CoXlTestStreamDrain(double ms, uint32_t* waiting)
{
    AcquireSRWLockExclusive(&g_streamLock);
    uint32_t done = WorldStreamDrainLocked(kStreamDropsPerFrame, ms);
    *waiting = g_streamDropCount;
    ReleaseSRWLockExclusive(&g_streamLock);
    return done;
}
/* A frame's WorldStreamTick with its scan due, the slots set up counted as deferred. */
void CoXlTestStreamTick(void)
{
    g_stream.nextScan = 0;
    if (g_stream.deferred == 0) g_stream.deferred = g_stream.count;
    WorldStreamTick(0.016f);
}
/* The look before a removal: slots given up, searches of the world's unit list, the session's destroy calls. */
void CoXlTestStreamBelt(uint32_t* gone, uint32_t* looks, long* destroys) { *gone = g_stream.gone; *looks = g_stream.looks; *destroys = g_streamDestroys; }
/* The looks that did not read: in a row now and in all, and whether removal went off for the level. */
void CoXlTestStreamLooks(uint32_t* bad, uint32_t* fails, int* off) { *bad = g_stream.lookBad; *fails = g_stream.lookFails; *off = g_stream.destroyBad; }
uint32_t CoXlTestWindowEvictedFar(void) { return g_win.evictedFar; }
void CoXlTestStreamOutSince(uint32_t i, ULONGLONG since) { g_streamOutSince[i] = since; }
void co_capacity_test_places(int region, uint32_t places);
/* Slots [base, high) as live far window stamps, then one eviction over the bodies' budget: the far removals queued. */
uint32_t CoXlTestWindowEvictFar(uint32_t base, uint32_t high)
{
    AcquireSRWLockExclusive(&g_streamLock);
    const uint32_t oldBase = g_win.base, oldHigh = g_win.high, before = g_streamDropFar, places = co_gen_places(HF_GEN_LOCATIONS_A);
    co_capacity_test_places(HF_GEN_LOCATIONS_A, high);
    g_win.base = base; g_win.high = high;
    for (uint32_t s = base; s < high; s++) { g_win.handle[s] = 1; g_streamOut2[s] = 1.0e6f; }
    WindowEvictFarLocked();
    const uint32_t queued = g_streamDropFar - before;
    for (uint32_t s = base; s < high; s++) g_win.handle[s] = 0; /* the far slots stay (their removal radius) */
    g_win.base = oldBase; g_win.high = oldHigh; g_win.farHoldUntil = 0;
    co_capacity_test_places(HF_GEN_LOCATIONS_A, places);
    ReleaseSRWLockExclusive(&g_streamLock);
    return queued;
}
void CoXlTestStreamSlotState(uint32_t i, int* live, int* due, uint64_t* unit) { *live = g_streamLive[i]; *due = g_streamDue[i]; *unit = g_streamUnit[i]; }
/* The streamer's own SpawnLevelLayers calls (WorldStreamSpawnDueLocked): `spawn` stands in for the original and `pass`
 * for the captured pass; a due slot waits with its unit; the due slots go in index order. */
void CoXlTestStreamSpawnSetup(void* spawn, uint8_t* pass)
{
    static float height;
    g_streamSpawn = (WorldStreamSpawnFn)spawn; g_stream.pass = pass; g_heightCopy = &height; g_heightSource = 1;
}
void CoXlTestStreamDue(uint32_t i, uint64_t unit)
{
    g_streamUnit[i] = unit; g_streamDeferred[i] = 1; g_streamDue[i] = 1; g_streamLive[i] = 0; g_streamSpawned[i] = 0;
    g_streamNeedsUnit[i] = 0; g_streamDetached[i] = 0;
    uint32_t n = 0;
    for (uint32_t k = 0; k < g_stream.count; k++)
        if (g_streamDeferred[k] && g_streamDue[k] && !g_streamSpawned[k]) g_streamOrder[n++] = k;
    g_stream.orderCount = n; g_stream.orderCursor = 0; g_stream.deferred = n;
}
void CoXlTestStreamSpawnDue(void)
{
    AcquireSRWLockExclusive(&g_streamLock);
    WorldStreamSpawnDueLocked();
    ReleaseSRWLockExclusive(&g_streamLock);
}
void CoXlTestStreamSpawned(uint32_t i, int* spawned, int* due) { *spawned = g_streamSpawned[i]; *due = g_streamDue[i]; }
void CoXlTestStreamTrees(uint32_t* holds, uint32_t* most, uint32_t* cursor) { *holds = g_stream.treeHolds; *most = g_stream.treesMost; *cursor = g_stream.orderCursor; }
int CoXlTestWindowTreesOver(float share) { return WindowTreesOver(share); }
float CoXlTestVisibleAt(float x, float y) { return NearVisibleAt(x, y); }
void CoXlTestHide(uint64_t handle) { g_hide.handle = handle; g_hide.level = g_world.levels; }
uint64_t CoXlTestHideHandle(void) { return g_hide.handle; }
void CoXlTestMoveHold(int on) { MoveHold(on); }
LONG64 CoXlTestMoveHoldSince(void) { return g_moveHoldSince; }
/* A level init's effect on the generator's side alone: the next level, complex (8) or not (1). */
void CoXlTestGenLevel(float current) { InterlockedIncrement(&g_world.levels); g_world.current = current; }
/* The roads' evidence (proto/difficulty_world_roads.c), its parts over raw 0x48 edge records (64 at most) and its
 * lines over a fake generation block. */
static uint32_t RoadsTestEdges(const uint8_t* recs, uint32_t n, RoadEdge* e)
{
    if (n > 64) n = 64;
    for (uint32_t i = 0; i < n; i++) RoadEdgeRead(recs + 0x48ull * i, &e[i]);
    return n;
}
void CoXlTestRoadsWidths(const uint8_t* recs, uint32_t n, char* out, size_t size) { RoadEdge e[64]; n = RoadsTestEdges(recs, n, e); RoadsWidths(e, n, out, size); }
void CoXlTestRoadsPoints(const uint8_t* recs, uint32_t n, uint32_t points, uint32_t counts[8], double km[2])
{
    RoadEdge e[64]; RoadsPoints c;
    n = RoadsTestEdges(recs, n, e);
    RoadsPointsCount(e, n, points, &c);
    counts[0] = c.edges; counts[1] = c.drawable; counts[2] = c.empty; counts[3] = c.emptySkeleton; km[0] = c.drawableKm; km[1] = c.emptyKm;
    counts[4] = c.none; counts[5] = c.one; counts[6] = c.bad; counts[7] = c.demand;
}
/* out: edges with points, their points; m: metres along the points and along the chords */
void CoXlTestRoadsPaths(const uint8_t* recs, uint32_t n, const float* pts, uint32_t points, uint32_t out[2], double m[2])
{
    RoadEdge e[64]; RoadsPaths p;
    n = RoadsTestEdges(recs, n, e);
    RoadsPathsCount(e, n, pts, points, &p);
    out[0] = p.edges; out[1] = p.points; m[0] = p.metres; m[1] = p.chords;
}
/* comp: each edge's part; largest: the largest part's id, its edges and its metres along the points. Returns the parts. */
uint32_t CoXlTestRoadsParts(const uint8_t* recs, uint32_t n, const float* pts, uint32_t points, uint32_t comp[64], uint32_t largest[2], double* metres)
{
    RoadEdge e[64];
    n = RoadsTestEdges(recs, n, e);
    const uint32_t parts = RoadsComponents(e, n, points, comp);
    largest[0] = RoadsLargest(e, n, pts, comp, parts, &largest[1], metres);
    return parts;
}
/* out[7]: how many parts hold the places' nearest paths; part (or NULL): each place's */
void CoXlTestRoadsReach(const float* xy, uint32_t n, const uint8_t* recs, uint32_t edges, const float* pts, uint32_t points, uint32_t* part, float out[8])
{
    RoadEdge e[64]; RoadsReach r; uint32_t comp[64];
    edges = RoadsTestEdges(recs, edges, e);
    RoadsComponents(e, edges, points, comp);
    RoadsReachOf(xy, n, e, edges, pts, points, comp, part, &r);
    out[0] = (float)r.n; out[1] = r.pathMedian; out[2] = r.pathMax; out[3] = r.skelMedian; out[4] = r.skelMax; out[5] = r.lineMedian; out[6] = r.lineMax;
    out[7] = (float)r.components;
}
DWORD CoXlTestTsvLength(char* line, size_t size, int len) { return TsvLength(line, size, len); }
double CoXlTestRoadsDisc(uint32_t world, float fraction, double* radius) { return RoadsDiscKm2(world, fraction, radius); }
/* The locations' ops as the dispatcher records them, and the stamp plan's count, for the level set up last. */
void CoXlTestRoadsLevel(const uint8_t* ops, uint32_t n, uint32_t planned)
{
    memcpy(g_locationOp, ops, n);
    g_plan.level = g_world.levels; g_plan.count = planned;
}
void CoXlTestRoadsCorridor(uintptr_t block, const uint8_t* ops, uint32_t before, uint32_t total) { RoadsCorridorNote(block, ops, before, total); }
void CoXlTestRoadsShape(uint8_t (*fn)(uint64_t, const float*, float, const float*, const float*, float, float*, float*)) { g_roadShape = fn; }
const char* CoXlTestInstallRoadTolerance(uintptr_t game, uint8_t** stub) { const char* why = InstallRoadTolerance(game); *stub = g_roadTolerance != NULL ? (uint8_t*)g_roadTolerance - 0x38 : NULL; return why; }
void CoXlTestRoadsTolerance(int begin) { if (begin) RoadsToleranceBegin(); else RoadsToleranceEnd(); }
/* The mod's added locations from `first` on, and `planned` plan entries at cand (each CoXlTestStampCandidateSize bytes, the location first). */
uint32_t CoXlTestStampCandidateSize(void) { return (uint32_t)sizeof(StampCandidate); }
void CoXlTestRoadsStamps(uint32_t first, void* cand, uint32_t planned)
{
    g_stamps.ringLevel = g_world.levels; g_stamps.first = first; g_stamps.cand = (StampCandidate*)cand;
    g_plan.level = g_world.levels; g_plan.count = planned;
}
/* The slope profile over a height grid (cols x rows cells of `step` m, row 0 at +y): out = metres, over the tank's
 * grade, over the FRV's, wet, steepest grade, worst cross slope, lowest, highest. */
void CoXlTestRoadsSlope(const float* h, uint32_t cols, uint32_t rows, float step, float water, const float* pts, uint32_t n, double* out)
{
    const RoadsGround g = {h, cols, rows, step, water};
    RoadsSlope s; memset(&s, 0, sizeof s);
    RoadsSlopeAlong(&g, pts, n, &s);
    out[0] = s.metres; out[1] = s.overTank; out[2] = s.overFrv; out[3] = s.wet; out[4] = s.grade; out[5] = s.cross; out[6] = s.low; out[7] = s.high;
}
/* The level's height capture and water as the near terrain keeps them, and the note over them. */
void CoXlTestRoadsGround(float* h, uint32_t cols, uint32_t rows, int bodies, float water)
{
    g_near.level = g_world.levels; g_near.global = h; g_near.columns = cols; g_near.rows = rows;
    g_nearWater.level = g_world.levels; g_nearWater.count = bodies; g_nearWater.high = water;
}
void CoXlTestRoadsSlopeNote(uintptr_t block) { RoadsSlopeNote(block); }
void CoXlTestRoadsIndexNote(uint64_t context, const char* when) { RoadsIndexNote(context, when); }
void CoXlTestRoadsPointsWatch(uintptr_t block, uint32_t op) { RoadsPointsWatch(block, op); }
uint8_t CoXlTestLocationOp(uint32_t i) { return g_locationOp[i]; }
float CoXlTestStampSegmentBox(const float* s, const float* lo, const float* hi) { return StampSegmentBox(s, lo, hi); }
void CoXlTestRoadsAfterOp(uintptr_t block, uint64_t context, uint64_t record, uint32_t op, uint32_t before, int counted)
{ RoadsAfterOp(block, context, record, op, before, counted); }
/* The roads' graded profile (proto/difficulty_world_roadgrade.c): links are station pairs, out the GradeStats fields
 * as doubles (steepest, bend, cut, fill, metres, over 5, over 8, moved over 8, over 15). Returns the torn stations. */
uint32_t CoXlTestGradeSolve(const float* ground, const uint8_t* pinned, uint32_t n, const uint32_t* links, const float* gaps, uint32_t count, float slope,
    uint32_t passes, float* target, double out[9])
{
    GradeStation* s = (GradeStation*)calloc(n ? n : 1, sizeof *s);
    if (s == NULL) return n;
    for (uint32_t i = 0; i < n; i++) { s[i].ground = ground[i]; s[i].pinned = pinned != NULL && pinned[i]; s[i].floor = -GRADE_FAR; }
    for (uint32_t k = 0; k < count; k++) GradeLink(s, links[2 * k], links[2 * k + 1], gaps[k]);
    const uint32_t torn = GradeSolve(s, n, slope, passes);
    GradeStats g; GradeMeasure(s, n, &g);
    for (uint32_t i = 0; i < n; i++) target[i] = s[i].target;
    out[0] = g.steepest; out[1] = g.bend; out[2] = g.cut; out[3] = g.fill; out[4] = g.metres; out[5] = g.over5; out[6] = g.over8; out[7] = g.cut8; out[8] = g.cut15;
    free(s);
    return torn;
}
/* The graded field (proto/difficulty_world_roadfield.c). chains: `counts[c]` points each, in `pts`; ends[2c], ends[2c+1]
 * name the shared stations of chain c's ends (a number the test chooses; 0xFFFFFFFF: its own). spur: 1 when the last
 * chain meets the others as a spur does. ground(x, y) and floor are given to every station. Returns the stations. */
static GradeField g_testField;
static float TestGradeGround(void* ground, float x, float y) { return ((float (*)(float, float))ground)(x, y); }
uint32_t CoXlTestGradeField(const float* pts, const uint32_t* counts, const uint32_t* ends, uint32_t chains, int spur, float (*ground)(float, float), float floor,
    float slope, uint32_t passes, float world, uint32_t* torn)
{
    static uint32_t shared[64];
    GradeField* f = &g_testField;
    GradeFieldFree(f);
    memset(shared, 0xFF, sizeof shared);
    for (uint32_t c = 0; c < chains; c++)
    {
        uint32_t own[2] = {GRADE_NONE, GRADE_NONE};
        uint32_t* a = ends[2 * c] < 64 ? &shared[ends[2 * c]] : &own[0];
        uint32_t* b = ends[2 * c + 1] < 64 ? &shared[ends[2 * c + 1]] : &own[1];
        if (spur && c + 1 == chains) own[1] = GradeFieldAttach(f, pts[2 * (counts[c] - 1)], pts[2 * (counts[c] - 1) + 1], f->segs);
        GradeFieldChain(f, pts, counts[c], a, b);
        pts += 2 * (size_t)counts[c];
    }
    for (uint32_t i = 0; i < f->stations; i++) { f->s[i].ground = ground(f->xy[2 * i], f->xy[2 * i + 1]); f->s[i].floor = floor; }
    GradeFieldLinks(f);
    GradeFieldTies(f);
    for (uint32_t i = 0; i < f->stations; i++) f->s[i].cross = GradeFieldCross(f, i, TestGradeGround, (void*)ground);
    *torn = GradeSolve(f->s, f->stations, slope, passes);
    GradeFieldIndex(f, world);
    return f->stations;
}
float CoXlTestGradeBandRamp(float move, float cross) { return GradeBandRamp(move, cross); }
float CoXlTestGradeBandWeight(float u, float ramp, float move, float cross) { return GradeBandWeight(u, ramp, move, cross); }
/* The level's pads for the grading: n circles of (x, y from the world's centre, radius, band, strength, footprint). */
void CoXlTestGradePads(const float* pads, uint32_t n)
{
    g_pads.level = g_world.levels; g_pads.count = 0;
    for (uint32_t i = 0; i < n && i < NEAR_PADS; i++, pads += 6)
        g_pads.pad[g_pads.count++] = (NearPad){.cx = pads[0], .cy = pads[1], .c = 1.0f, .a = pads[2], .ba = pads[3], .k = pads[4], .foot = pads[5] != 0.0f};
}
/* The road's share at a point (corner metres), as the level's heights and a near tile take it; *pinned: a station
 * there keeps its ground. */
float CoXlTestGradeFree(float world, float playable, float x, float y, int* pinned)
{
    g_grade.world = world; g_grade.playable = playable;
    RoadGradePadIndex();
    *pinned = RoadGradePinned(x, y);
    return RoadGradeFree(x, y);
}
float CoXlTestGradeFieldAt(float x, float y, float* z, float* d) { return GradeFieldAt(&g_testField, x, y, z, d); }
float CoXlTestGradeStation(uint32_t i, float* xy, float* band, uint32_t* links)
{ xy[0] = g_testField.xy[2 * i]; xy[1] = g_testField.xy[2 * i + 1]; *band = g_testField.band[i]; *links = g_testField.s[i].links; return g_testField.s[i].target; }
uint32_t CoXlTestGradeFieldPieces(uint32_t* unlinked, uint32_t* capped) { *unlinked = g_testField.unlinked; *capped = g_testField.capped; return g_testField.segs; }
/* A road's slopes on a surface: out = metres, over 5, 8, 12, 18, rise, steepest, 95th percentile, cross. */
void CoXlTestGradeRoad(float (*height)(void*, float, float), const float* pts, uint32_t n, double out[9])
{
    static GradeRoad g;
    memset(&g, 0, sizeof g);
    GradeRoadAlong(height, NULL, pts, n, &g);
    out[0] = g.metres; for (int k = 0; k < 4; k++) out[1 + k] = g.over[k];
    out[5] = g.rise; out[6] = g.steepest; out[7] = GradeRoad95(&g); out[8] = g.cross;
}
/* The level's draw over the test field: the playable radius, then every cell through `tri`. Returns the triangles. */
uint32_t CoXlTestGradeDraw(float world, float playable, GradeTriFn tri, float px, uint32_t most, uint32_t* cells)
{
    GradeFieldFree(&g_grade.f);
    g_grade.f = g_testField; /* lent: the test's callback reads the field through g_testField meanwhile */
    g_grade.world = world; g_grade.playable = playable; g_grade.next = 0; g_grade.drawn = 0;
    RoadGradePadIndex();
    RoadGradeCells();
    static const float colour[4] = {255.0f, 0.0f, 0.0f, 0.0f};
    uint32_t given = 0, batch;
    while ((batch = RoadGradeBatch(tri, 1, 2, colour, px, most)) != 0) given += batch;
    *cells = g_grade.drawn;
    memset(&g_grade.f, 0, sizeof g_grade.f);
    return given;
}
uint32_t CoXlTestGradeTies(uint32_t* untied) { *untied = g_testField.untied; return g_testField.ties; }
/* A near tile's texel on the test field: the level's height there, sharpened as the tile takes it. */
float CoXlTestGradeSharpen(float world, float playable, float wx, float wy, float coarse)
{
    g_grade.f = g_testField; /* lent, as for the draw */
    g_grade.world = world; g_grade.playable = playable; g_grade.ready = 1; g_grade.off = 0; g_grade.level = g_world.levels;
    RoadGradePadIndex();
    const float h = RoadGradeSharpen(wx, wy, coarse);
    memset(&g_grade.f, 0, sizeof g_grade.f); g_grade.ready = 0; g_grade.level = -1;
    return h;
}
/* The merge's first op on a level whose roads were graded (level) or not (-1): 1 when the roads will be graded again. */
int CoXlTestGradeRestart(int graded)
{
    g_grade.level = graded ? g_world.levels : -1; g_grade.phase = graded ? 2 : 0; g_grade.ready = graded;
    RoadGradeBegin();
    const int again = graded && g_grade.level == -1 && g_grade.phase == 0 && !g_grade.ready;
    g_grade.level = -1; g_grade.phase = 0; g_grade.ready = 0;
    return again;
}
/* The capped profile (GradeSolveCapped) on links as CoXlTestGradeSolve takes them; earth NULL: no cap anywhere.
 * out: the stations GradeOverCap counts past `by`, the furthest's metres past its cap, its index, the metres of road. */
uint32_t CoXlTestGradeCapped(const float* ground, const uint8_t* pinned, const float* earth, uint32_t n, const uint32_t* links, const float* gaps, uint32_t count,
    float soft, float hard, uint32_t passes, float by, float* target, double out[4])
{
    GradeStation* s = (GradeStation*)calloc(n ? n : 1, sizeof *s);
    if (s == NULL) return n;
    for (uint32_t i = 0; i < n; i++)
    { s[i].ground = ground[i]; s[i].pinned = pinned != NULL && pinned[i]; s[i].floor = -GRADE_FAR; s[i].earth = earth != NULL ? earth[i] : GRADE_FAR; }
    for (uint32_t k = 0; k < count; k++) GradeLink(s, links[2 * k], links[2 * k + 1], gaps[k]);
    const uint32_t torn = GradeSolveCapped(s, n, soft, hard, passes);
    float worst = 0.0f; uint32_t at = 0; double metres = 0.0;
    out[0] = GradeOverCap(s, n, by, &worst, &at, &metres); out[1] = worst; out[2] = at; out[3] = metres;
    for (uint32_t i = 0; i < n; i++) target[i] = s[i].target;
    free(s);
    return torn;
}
/* n flatten zones (op 0x3E) after the pads CoXlTestGradePads gave, as TerrainNearPads keeps them: boxes of x0, y0,
 * x1, y1 from the world's centre. */
void CoXlTestGradeZones(const float* boxes, uint32_t n)
{
    for (uint32_t i = 0; i < n && g_pads.count < NEAR_PADS; i++, boxes += 4)
        g_pads.pad[g_pads.count++] = (NearPad){.cx = (boxes[0] + boxes[2]) / 2, .cy = (boxes[1] + boxes[3]) / 2, .c = 1.0f, .a = (boxes[2] - boxes[0]) / 2,
            .b = (boxes[3] - boxes[1]) / 2, .ba = 16.0f, .bb = 16.0f, .k = 1.0f, .rect = 1, .zone = 1};
}
/* The level's own solve (RoadGradeSolve) on one road of n points: ground(x, y) stands for the capture, the pins and
 * caps come from the pads and the rim. The stations stay in the test field. Returns the torn stations, -1 when not
 * solved; out: the pinned stations, those past the cap, the furthest's metres past it, its x and y, the metres. */
static float (*g_testGradeGround)(float, float);
static uint64_t TestGradeSample(void* descriptor, float* uv, void* out, uint64_t unused)
{ (void)descriptor; (void)unused; *(float*)out = g_testGradeGround(uv[0] * g_grade.world, uv[1] * g_grade.world); return 0; }
int CoXlTestGradeLevel(const float* pts, uint32_t n, float (*ground)(float, float), float world, float playable, double out[6])
{
    uint32_t first = GRADE_NONE, last = GRADE_NONE;
    GradeFieldFree(&g_testField);
    GradeFieldChain(&g_testField, pts, n, &first, &last);
    GradeFieldFree(&g_grade.f);
    g_grade.f = g_testField; /* lent, and taken back with what the solve made */
    g_grade.world = world; g_grade.playable = playable; g_grade.sample = TestGradeSample; g_grade.capture = 1; g_testGradeGround = ground;
    RoadGradePadIndex();
    const int solved = RoadGradeSolve();
    g_testField = g_grade.f;
    memset(&g_grade.f, 0, sizeof g_grade.f); g_grade.sample = NULL; g_grade.capture = 0;
    out[0] = g_grade.pinned; out[1] = g_grade.deep; out[2] = g_grade.deepest; out[3] = g_grade.deepAt[0]; out[4] = g_grade.deepAt[1]; out[5] = g_grade.deepMetres;
    return solved ? (int)g_grade.torn : -1;
}
/* A station of the test field after CoXlTestGradeLevel: its cap; *pinned: it keeps its ground. */
float CoXlTestGradeStationCap(uint32_t i, int* pinned) { *pinned = g_testField.s[i].pinned; return g_testField.s[i].earth; }
/* What the grading adds at a point (RoadGradeMove) on the field and the ground CoXlTestGradeLevel left. */
float CoXlTestGradeMove(float x, float y)
{
    g_grade.f = g_testField; /* lent, as for the draw */
    g_grade.sample = TestGradeSample; g_grade.capture = 1;
    RoadGradePadIndex();
    const float move = RoadGradeMove(NULL, x, y);
    memset(&g_grade.f, 0, sizeof g_grade.f); g_grade.sample = NULL; g_grade.capture = 0;
    return move;
}
/* A road's shoulders on a surface: out = the steepest span (degrees), its x and y, the sharpest crease, its x and y. */
void CoXlTestGradeShoulder(float (*height)(void*, float, float), const float* pts, uint32_t n, double out[6])
{
    static GradeRoad g;
    memset(&g, 0, sizeof g);
    GradeRoadAlong(height, NULL, pts, n, &g);
    out[0] = g.side; out[1] = g.sideAt[0]; out[2] = g.sideAt[1]; out[3] = g.crease; out[4] = g.creaseAt[0]; out[5] = g.creaseAt[1];
}
/* 1 inside a stamp's footprint (corner metres); *open: RoadGradeOpen there over a surface that is 7 m everywhere. */
static float TestGradeSeven(void* ctx, float x, float y) { (void)ctx; (void)x; (void)y; return 7.0f; }
int CoXlTestGradeInPlace(float world, float playable, float x, float y, float* open)
{
    g_grade.world = world; g_grade.playable = playable; g_grade.under = TestGradeSeven;
    RoadGradePadIndex();
    *open = RoadGradeOpen(NULL, x, y);
    g_grade.under = NULL;
    return RoadGradeInPlace(x, y);
}
/* The roads' build (proto/difficulty_world_roadbuild.c): the point spacing, its stub and the arterial choice. */
uint32_t CoXlTestRoadsLeftOut(const uint8_t* recs, uint32_t n, const uint8_t* taken, uint32_t used, uint32_t capacity, float spacing, uint32_t* total)
{ RoadEdge e[64]; n = RoadsTestEdges(recs, n, e); return RoadsSpacingLeftOut(e, n, taken, used, capacity, spacing, total); }
float CoXlTestRoadsSpacing(const uint8_t* recs, uint32_t n, const uint8_t* taken, uint32_t used, uint32_t capacity, uint32_t* total, uint32_t* left)
{ RoadEdge e[64]; n = RoadsTestEdges(recs, n, e); return RoadsSpacingChoose(e, n, taken, used, capacity, total, left); }
const char* CoXlTestInstallRoadSpacing(uintptr_t game, uint8_t** stub) { const char* why = InstallRoadSpacing(game); *stub = (uint8_t*)g_roadSpacing.key - 0x30; return why; }
void CoXlTestRoadSpacingBuild(uint8_t* stub, uintptr_t constant, uintptr_t back) { RoadSpacingBuild(stub, constant, back); }
void CoXlTestRoadSpacingAt(uint8_t* stub) { g_roadSpacing.key = (volatile uint64_t*)(stub + 0x30); g_roadSpacing.spacing = (volatile float*)(stub + 0x38); g_roadSpacing.level = -1; }
void CoXlTestRoadsSpacingBegin(uintptr_t block, uint64_t record) { RoadsSpacingBegin(block, record); }
void CoXlTestRoadsSpacingEnd(void) { RoadsSpacingEnd(); }
uint32_t CoXlTestRoadsArterials(const uint8_t* recs, uint32_t edges, const uint32_t* ends, const float* nodeXY, uint32_t nodes, const float* termXY, uint32_t terms,
    uint8_t* chosen, uint32_t* unreached)
{
    RoadEdge e[64];
    edges = RoadsTestEdges(recs, edges, e);
    const RoadsGraph g = {e, ends, nodeXY, edges, nodes};
    return RoadsArterials(&g, termXY, terms, chosen, unreached);
}
void CoXlTestRoadsArterialsApply(uintptr_t block, const uint8_t* ops, uint32_t total) { RoadsArterialsApply(block, ops, total); }
void CoXlTestRoadsArterialsPoints(uintptr_t block) { RoadsArterialsPoints(block); }
void CoXlTestRoadsSpursBuild(uintptr_t block, uint64_t context, void (*capsule)(uint64_t, const float*, uint32_t)) { g_roadCapsule = capsule; RoadsSpursBuild(block, context); }
void CoXlTestRoadsSpurTerminals(const float* xy, const float* gap, uint32_t terms) { RoadsSpurTerminals(xy, gap, terms); }
uint32_t CoXlTestRoadsSpurs(uint32_t* reserve, const float** table) { *reserve = RoadsSpurReserve(); *table = (const float*)g_spurs.spur; return g_spurs.level == g_world.levels ? g_spurs.count : 0; }
int CoXlTestRoadsArterial(uint32_t edge) { return g_arterials.level == g_world.levels && (g_arterials.bits[edge >> 3] >> (edge & 7)) & 1; }
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
static int g_scatterTestCalled, g_scatterTestBake;
static int g_scatterTestLocked; /* the layer's lock was held while the engine's update ran */
static void ScatterTestEngine(void* a, uint8_t* view, uint32_t c, uint8_t* layer, void* e, void* f, void* g, void* h)
{
    g_scatterTestCalled = 1; g_scatterTestBake = t_tdScatBake;
    SRWLOCK* lock = &g_tdScatLock[((uintptr_t)layer / 0x238u) & (TD_SCAT_LOCKS - 1)];
    g_scatterTestLocked = layer != NULL && !TryAcquireSRWLockExclusive(lock);
    if (layer != NULL && !g_scatterTestLocked) ReleaseSRWLockExclusive(lock);
}
/* The update of `layer` for `view`; 1 when the engine's ran, and whether it ran under the layer's lock. */
int CoXlTestScatterLayer(uint8_t* view, uint8_t* layer, int* locked)
{
    g_tdScatterUpdate = ScatterTestEngine; g_scatterTestCalled = 0; g_scatterTestLocked = 0;
    TdScatterUpdate(NULL, view, 0, layer, NULL, NULL, NULL, NULL);
    *locked = g_scatterTestLocked;
    return g_scatterTestCalled;
}
void CoXlTestScatterHold(uint8_t* layer, int hold)
{
    SRWLOCK* lock = &g_tdScatLock[((uintptr_t)layer / 0x238u) & (TD_SCAT_LOCKS - 1)];
    if (hold) AcquireSRWLockExclusive(lock); else ReleaseSRWLockExclusive(lock);
}
/* The bake's updates and the updates that waited for their layer, since the start. */
void CoXlTestScatterCounts(LONG64* bake, LONG64* overlap) { *bake = g_tdScatBakeUpdates; *overlap = g_tdScatOverlap; }
/* The frame times before a batch: `n` level updates `ms` apart in play, then a batch's start; the worst and the mean. */
void CoXlTestFgFrames(const float* ms, int n, double* worst, double* mean)
{
    g_fgFrames.filled = g_fgFrames.at = 0;
    for (int k = 0; k < n; k++) { g_fgFrames.ring[g_fgFrames.at] = ms[k]; g_fgFrames.at = (g_fgFrames.at + 1) % FG_RING; if (g_fgFrames.filled < FG_RING) g_fgFrames.filled++; }
    FgFramesStart();
    *worst = g_fgFrames.before[0]; *mean = g_fgFrames.before[1];
}
int CoXlTestScatterBake(void) { return g_scatterTestBake; } /* the far-grass bake mark the engine's update ran under */
void CoXlTestScatLimits(int bake, int* budget, uint64_t* most) { NearScatLimits(bake, budget, most); }
void CoXlTestScatClear(void) { memset(g_scatCache, 0, sizeof g_scatCache); }
void CoXlTestScatStore(float size, float half, int32_t ix, int32_t iy, float lo, float hi)
{
    uint32_t s, h; memcpy(&s, &size, 4); memcpy(&h, &half, 4);
    NearScatStore(s, h, ix, iy, lo, hi);
}
int CoXlTestScatFind(float size, float half, int32_t ix, int32_t iy, float* lo, float* hi)
{
    uint32_t s, h; memcpy(&s, &size, 4); memcpy(&h, &half, 4);
    return NearScatFind(s, h, ix, iy, lo, hi);
}
uint32_t CoXlTestScatSlot(float size, float half, int32_t ix, int32_t iy)
{
    uint32_t s, h; memcpy(&s, &size, 4); memcpy(&h, &half, 4);
    return NearScatSlot(s, h, ix, iy);
}
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
/* The frame-time gate (CoXlInPlay) with the game at `game` and the living world's `live`. */
int CoXlTestInPlay(uintptr_t game, float live) { g_difficultyGame = game; g_world.live = live; return CoXlInPlay(); }
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
/* Far grass on extra large maps (proto/difficulty_world_fargrass.c): the planner, a view's square and rectangle, the
 * kept textures at ops 0x4B / 0x67, the bake's level-update tick with the game's two functions given, the bake view's
 * mark in the scatter update. A player is the streamer's research observer. */
int CoXlTestFgBatch(const float (*players)[2], int count, float reach, uint16_t* list, int max) { return FgBatch(&g_fg.map, players, count, reach, list, max); }
void CoXlTestFgMark(int i, int j) { FgMark(&g_fg.map, i, j); }
void CoXlTestFgView(int i, int j, float* centre, float* rect) { FgCentre(i, j, centre); FgRect(i, j, rect); }
void CoXlTestFgInstall(void* aim, void* generate) { g_fg.installed = 1; g_fg.aim = (FgAimFn)aim; g_fg.generate = (FgGenerateFn)generate; }
void CoXlTestFgKeep(uint64_t record, uint32_t op) { FarGrassKeep(record, op); }
uint64_t CoXlTestFgTex(int k) { return g_fg.tex[k]; }
void CoXlTestFgTick(uint64_t level) { FarGrassTick(level); }
/* The far-grass ground heights' job: a coarse grid over the world and, with tiles 1, one tile at (tx, ty). */
void CoXlTestFgGroundJob(const uint16_t* coarse, uint32_t cn, float csize, const uint16_t* tile, uint32_t tn, float tsize, float tx, float ty, int tiles)
{
    FgGroundJob* j = &g_fgGround.job;
    memset(j, 0, sizeof *j);
    j->coarse.v = coarse; j->coarse.n = cn; j->coarse.size = csize;
    j->tiles[0].ground.v = tile; j->tiles[0].ground.n = tn; j->tiles[0].ground.size = tsize; j->tiles[0].ground.cx = tx; j->tiles[0].ground.cy = ty;
    j->tiles[0].centre[0] = tx; j->tiles[0].centre[1] = ty; j->count = tiles;
}
float CoXlTestFgGroundAt(float x, float y) { return FgGroundAt(&g_fgGround.job, x, y); }
void CoXlTestFgGroundRows(uint16_t* out, int r0, int r1) { g_fgGround.job.out = out; FgGroundRows(&g_fgGround.job, r0, r1); }
int CoXlTestFgBusy(void) { return FarGrassBusy(); }
int CoXlTestFgSetting(void) { return g_fg.setting; }
uint32_t CoXlTestFgBaked(void) { return g_fg.baked; }
void CoXlTestFgObserver(int on, float x, float y) { g_streamObserverOn = on; g_streamObserverAt[0] = x; g_streamObserverAt[1] = y; }
int CoXlTestFgBakeView(const uint8_t* view) { return TdScatterBakeView(view); }
void CoXlTestFgWorld(float live) { g_world.live = live; } /* 8 on an extra large level in play (16384 m) */
void CoXlTestFgState(int state) { g_fg.state = state; }
/* The profile guard on a settings document, with `get` as the document's operator[]. */
void CoXlTestFgProfileGuard(void* doc, void* get) { g_fgDocGet = (FgDocGetFn)get; FgProfileGuard(doc, "a test save"); }
void CoXlTestScatBakeOver(LONG n) { g_scatBakeOver = n; } /* a bake view's layer over the node limit */
int CoXlTestFgViewBaked(int i, int j) { return FgBaked(&g_fg.map, i, j); }
void CoXlTestFgLinker(uint64_t record) { FarGrassLinker(record); } /* op 0x4A's record on an extra large level */
void CoXlTestFgNotesReset(void) { g_fg.settingNotes = 0; g_fg.clearNoted = 0; } /* a new session's log */
void CoXlTestFgOut(int on) { g_fg.published = on; } /* a job out, with the names published */
int CoXlTestFgPaced(double mean, float last, uint32_t* deferred)
{
    g_fgFrames.filled = mean > 0.0 ? FG_RING : 0; g_fgFrames.lastMs = last;
    for (int k = 0; k < FG_RING; k++) g_fgFrames.ring[k] = (float)mean;
    int r = FgPaced();
    *deferred = g_fg.deferred;
    return r;
}
void CoXlTestFgRing(int a0, int a1, int b0, int b1) { g_ring.a0 = a0; g_ring.a1 = a1; g_ring.b0 = b0; g_ring.b1 = b1; } /* the mover's cells */
#endif

#ifdef CO_RESEARCH
/* A research build only: the harness's Lua finds the living world's test commands (teleport, frec) by this name. */
__declspec(dllexport) int co_research_lw_test(const char* cmd) { return hellforge_lw_test(cmd); }
#endif
