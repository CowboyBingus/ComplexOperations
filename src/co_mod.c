/* Complex Operations: the mod's entry point. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "co.h"

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

static void OnGameLoaded(HfMod* mod, HfEvent event, void* user)
{
    (void)mod; (void)event; (void)user;
    size_t size = 0;
    uintptr_t game = g_coApi->module_base("game.dll", &size);
    if (game == 0) { co_log("game.dll was not found; nothing changed"); return; }
    co_pods_install(game);
    co_session_install(game);
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
    if (api->on(mod, HF_EVENT_GAME_LOADED, OnGameLoaded, NULL) != 0) { co_log("the game-loaded event could not be registered"); return 1; }
    return 0;
}
