/* Complex Operations: the mod's entry point. */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "co.h"
#include "co_tag.h"
#include "co_watch.h"
#include "xl/co_nav.h"
#include "xl/co_aiw_log.h"

const HfApi* g_coApi;
HfMod* g_coMod;

void co_log(const char* format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (g_coApi != NULL) g_coApi->log(g_coMod, "%s", text);
}

/* HellForge installs level 11 after the game-loaded event, on its own thread: the layout is asked for each game update
 * until it is there. */
static void CapacityTick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)dt; (void)user;
    static int done;
    const uint64_t now = GetTickCount64();
    if (!done && (done = co_capacity_poll(now)) != 0)
    {
        co_capacity_normal(); /* after HellForge's level 11 is in or is known not to come: its steps are chained, not replaced */
        co_complex_start();   /* nothing of the extra large world goes in while it is off */
    }
    co_peers_tick(now);
    co_tag_tick();
    co_watch_tick();
}

static void OnGameLoaded(HfMod* mod, HfEvent event, void* user)
{
    (void)mod; (void)event; (void)user;
    size_t size = 0;
    uintptr_t game = g_coApi->module_base("game.dll", &size);
    if (game == 0) { co_log("game.dll was not found; nothing changed"); return; }
    co_nav_reserve(game, 0);
    co_ui_install(game);
    co_pods_install(game);
    co_counts_install(game);
    co_avatars_install(game);
    co_cards_install(game);
    co_session_install(game);
    co_engine_install(g_coApi->module_base("helldivers2.exe", &size));
#ifdef CO_RESEARCH
    co_aiw_log_install(g_coApi->module_base("helldivers2.exe", NULL), game); /* before any world: every nav id's life */
#endif
    co_capacity_game(game);
    co_capacity_early(); /* this event is the first moment with game.dll's code there, and HellForge's pool sizes are in */
    co_peers_start(game);
    const char* tag = co_tag_install(game);
    if (tag != NULL) co_log("war-table tag: off: %s", tag);
    const char* watch = co_watch_install(game);
    if (watch != NULL) co_log("wartable watch: off: %s", watch);
    if (g_coApi->size < offsetof(HfApi, on_game_thread) + sizeof(void*) || g_coApi->on_game_thread(g_coMod, CapacityTick, NULL) == 0) co_log("complex operations are off: the game thread's callback was refused");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (api == NULL || mod == NULL || api->version < 17u) return 1;
    g_coApi = api;
    g_coMod = mod;
    const char* build = api->build_id();
    if (build == NULL || strcmp(build, CO_GAME_BUILD) != 0)
    {
        co_log("needs game build %s; this is %s", CO_GAME_BUILD, build != NULL ? build : "unknown");
        return 1;
    }
    size_t image = 0;
    const uintptr_t exe = api->module_base("helldivers2.exe", &image);
    /* before the game's heaps exist: they fill the space near the exe that the steps' pages need */
    if (!co_near_exe_reserve(exe, image)) co_log("no room within reach of helldivers2.exe at 0x%llx as the mod loaded", (unsigned long long)exe);
    co_nav_reserve(0, exe);
    if (api->on(mod, HF_EVENT_GAME_LOADED, OnGameLoaded, NULL) != 0) { co_log("the game-loaded event could not be registered"); return 1; }
    co_capacity_init(); /* the game makes the generator's block once: only now is in time */
    return 0;
}
