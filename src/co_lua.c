/* The game's Lua for the war-table tag: lua51.dll's C API by export name, and one hook on lua51!lua_pcall that runs the
 * tag's frame. Ported from the Vanilla Plus Megapack's core/lua.c (Know Your Constellation's route).
 *
 * The frame runs inside a lua_pcall, before the engine's own function, so every Gui call it makes is on the thread that
 * owns the state, between two of the engine's Lua calls. Only a call on the main thread of a state that has stingray's
 * Gui qualifies, and only while no function runs on that thread (lua_getstack level 0 is empty): never inside a Lua
 * function, another pcall or its error handler. The return address is not asked: in a chain with the Megapack's own
 * hook it would name the other hook, not the game.
 *
 * Cost per lua_pcall: one load of the pending flag and a tail call. Once per frame: one lua_getstack, the frame. */
#include <string.h>
#include <windows.h>

#include "co.h"
#include "co_lua.h"

static struct
{
    CoLuaApi api;
    int bound;              /* 1 bound, -1 a function is missing */
    int (*exported)(lua_State* L, int arguments, int results, int handler);
    int (*next)(lua_State* L, int arguments, int results, int handler); /* the hook's original, rewritten by HellForge */
    lua_State* state;
    void (*frame)(lua_State* L);
    volatile LONG pending;
    uint32_t unbalanced;
    const char* refused;
} g;
static uint8_t g_debug[512]; /* a lua_Debug: lua_getstack writes its call index only */

const CoLuaApi* co_lua(void) { return g.bound > 0 ? &g.api : NULL; }
lua_State* co_lua_state(void) { return g.state; }
uint32_t co_lua_unbalanced(void) { return g.unbalanced; }
void co_lua_tick(void) { g.pending = 1; }

/* Our own Gui calls go to the rest of the chain, never back through this hook. */
static int Pcall(lua_State* L, int arguments, int results, int handler)
{
    return (g.next != NULL ? g.next : g.exported)(L, arguments, results, handler);
}

#define BIND(field, name) \
    do { if ((*(FARPROC*)&g.api.field = GetProcAddress(lua, name)) == NULL) missing = name; } while (0)

static int Bind(void)
{
    const char* missing = NULL;
    HMODULE lua = GetModuleHandleA("lua51.dll");
    if (g.bound != 0) return g.bound > 0;
    if (lua == NULL) return 0; /* not loaded yet: asked again */
    BIND(gettop, "lua_gettop");
    BIND(settop, "lua_settop");
    BIND(pushvalue, "lua_pushvalue");
    BIND(type, "lua_type");
    BIND(tonumber, "lua_tonumber");
    BIND(toboolean, "lua_toboolean");
    BIND(tolstring, "lua_tolstring");
    BIND(pushnil, "lua_pushnil");
    BIND(pushnumber, "lua_pushnumber");
    BIND(pushstring, "lua_pushstring");
    BIND(pushboolean, "lua_pushboolean");
    BIND(createtable, "lua_createtable");
    BIND(getfield, "lua_getfield");
    BIND(setfield, "lua_setfield");
    BIND(rawgeti, "lua_rawgeti");
    BIND(rawseti, "lua_rawseti");
    BIND(objlen, "lua_objlen");
    BIND(pcall, "lua_pcall");
    BIND(ref, "luaL_ref");
    BIND(unref, "luaL_unref");
    BIND(pushlstring, "lua_pushlstring");
    BIND(rawget, "lua_rawget");
    BIND(rawset, "lua_rawset");
    BIND(pushthread, "lua_pushthread");
    BIND(getstack, "lua_getstack");
    g.bound = missing == NULL ? 1 : -1;
    if (missing != NULL) { co_log("war-table tag: lua51.dll has no %s; no tag is drawn", missing); return 0; }
    g.exported = g.api.pcall;
    g.api.pcall = Pcall;
    return 1;
}

/* The main thread of a state the game draws its UI from: lua_pushthread answers 1 for a main thread. */
static int GameState(lua_State* L)
{
    const int top = g.api.gettop(L);
    const int main = g.api.pushthread(L);
    g.api.settop(L, top);
    if (!main) return 0;
    g.api.getfield(L, CO_LUA_GLOBALS, "stingray");
    int ui = g.api.type(L, -1) == CO_LUA_TTABLE;
    if (ui) { g.api.getfield(L, -1, "Gui"); ui = g.api.type(L, -1) == CO_LUA_TTABLE; }
    g.api.settop(L, top);
    return ui;
}

__attribute__((noinline)) static void Frame(lua_State* L)
{
    if (g.state != NULL && L != g.state) return;
    if (g.api.getstack(L, 0, (lua_Debug*)g_debug) != 0) return; /* a function runs on this thread */
    if (g.state == NULL)
    {
        if (!GameState(L)) return;
        g.state = L;
        co_log("war-table tag: the game's Lua state found");
    }
    g.pending = 0;
    const int top = g.api.gettop(L);
    g.frame(L);
    if (g.api.gettop(L) != top) { g.unbalanced++; g.api.settop(L, top); }
}

/* lint: per-call. A tail call on every path: the next hook in the chain sees the game's return address. */
static int PcallDetour(lua_State* L, int arguments, int results, int handler)
{
    if (g.pending && L != NULL && g.frame != NULL) Frame(L);
    return g.next(L, arguments, results, handler);
}

const char* co_lua_install(void (*frame)(lua_State* L))
{
    if (g.next != NULL) return NULL;
    if (g.refused != NULL) return g.refused;
    if (!Bind()) return g.refused = "lua51.dll or one of its functions was not found";
    const uintptr_t target = (uintptr_t)g.exported;
    if (g_coApi->size < offsetof(HfApi, code_matches) + sizeof(void*) || !g_coApi->code_matches(target, CO_LUA_PCALL_SHIPPED))
        return g.refused = "lua51.dll's lua_pcall is not the function this mod was made for";
    g.frame = frame;
    if (g_coApi->hook_ex(g_coMod, target, (void*)PcallDetour, (void**)&g.next, 0) != 0)
    {
        g.next = NULL;
        g.frame = NULL;
        return g.refused = "lua51.dll's lua_pcall could not be hooked";
    }
    return NULL;
}

#ifdef CO_TEST
int co_lua_test_state(lua_State* L)
{
    memset(&g, 0, sizeof g);
    if (!Bind()) return 0;
    g.state = L;
    return 1;
}

void co_lua_test_reset(void)
{
    memset(&g, 0, sizeof g);
    co_gui_test_reset();
}

int co_lua_test_detour(lua_State* L, int arguments, int results, int handler) { return PcallDetour(L, arguments, results, handler); }
#endif
