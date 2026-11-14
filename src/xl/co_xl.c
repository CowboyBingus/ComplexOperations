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
#include "co_framecost.h"
#include "co_switch.h"
#include "co_steer.h"
#include "co_cutters.h"
#include "co_forest.h"
#include "co_podstart.h"
#include "co_settle.h"
#include "co_hitch.h"
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
        "  subq $0x98, %rsp\n"
        "  movdqu %xmm0, 0x30(%rsp)\n  movdqu %xmm1, 0x40(%rsp)\n  movdqu %xmm2, 0x50(%rsp)\n"
        "  movdqu %xmm3, 0x60(%rsp)\n  movdqu %xmm4, 0x70(%rsp)\n  movdqu %xmm5, 0x80(%rsp)\n"
        "  movq 0xC0(%rsp), %rax\n  movq %rax, 0x20(%rsp)\n"
        "  callq *g_coIndexQueryClamped(%rip)\n"
        "  movdqu 0x30(%rsp), %xmm0\n  movdqu 0x40(%rsp), %xmm1\n  movdqu 0x50(%rsp), %xmm2\n"
        "  movdqu 0x60(%rsp), %xmm3\n  movdqu 0x70(%rsp), %xmm4\n  movdqu 0x80(%rsp), %xmm5\n"
        "  addq $0x98, %rsp\n  retq\n");

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
void CoXlTestNearNamesClose(int opened) { NearNamesClose(opened); }
uint64_t CoXlTestNearProbe(void* original) { g_nearProbe = (NearProbeFn)original; return NearProbe(1, 2, 3, 4, 5, 6, 7, 8, 9); }
void CoXlTestNearProbeCounts(uint32_t* replay, uint32_t* tile) { *replay = g_probeRun.replay; *tile = g_probeRun.tile; }
void CoXlTestNearProbeClear(void) { memset(&g_probeRun, 0, sizeof g_probeRun); }
LONG CoXlTestNearResetState(void) { return g_nearReset.state; }
void CoXlTestNearReset(LONG state, int pins) { g_nearReset.state = state; g_nearReset.pins = pins; }
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
