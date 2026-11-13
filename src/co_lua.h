/* The game's Lua and the engine's Gui from C, for the war-table tag (co_lua.c, co_gui.c, co_tag.c). The method is Know
 * Your Constellation's, ported from the Vanilla Plus Megapack's core (lua.c, engine_gui.c, 0BSD): lua51.dll's C API by
 * export name, the stingray.* Gui functions called through lua_pcall. The mod's objects live under its own registry
 * key, so a Megapack in the same game never shares a table with it. */
#ifndef CO_LUA_H
#define CO_LUA_H

#include <stddef.h>
#include <stdint.h>

typedef struct lua_State lua_State;
typedef int (*CoLuaFunction)(lua_State* L);
typedef struct lua_Debug lua_Debug;
typedef struct CoLuaApi
{
    int (*gettop)(lua_State* L);
    void (*settop)(lua_State* L, int index);
    void (*pushvalue)(lua_State* L, int index);
    int (*type)(lua_State* L, int index);
    double (*tonumber)(lua_State* L, int index);
    int (*toboolean)(lua_State* L, int index);
    const char* (*tolstring)(lua_State* L, int index, size_t* length);
    void (*pushnil)(lua_State* L);
    void (*pushnumber)(lua_State* L, double value);
    void (*pushstring)(lua_State* L, const char* text);
    void (*pushboolean)(lua_State* L, int value);
    void (*createtable)(lua_State* L, int array, int fields);
    void (*getfield)(lua_State* L, int index, const char* key);
    void (*setfield)(lua_State* L, int index, const char* key);
    void (*rawgeti)(lua_State* L, int index, int n);
    void (*rawseti)(lua_State* L, int index, int n);
    size_t (*objlen)(lua_State* L, int index);
    int (*pcall)(lua_State* L, int arguments, int results, int handler);
    int (*ref)(lua_State* L, int table);
    void (*unref)(lua_State* L, int table, int ref);
    void (*pushlstring)(lua_State* L, const char* text, size_t length);
    void (*rawget)(lua_State* L, int index);
    void (*rawset)(lua_State* L, int index);
    int (*pushthread)(lua_State* L);
    int (*getstack)(lua_State* L, int level, lua_Debug* ar);
} CoLuaApi;
#define CO_LUA_REGISTRY (-10000)
#define CO_LUA_GLOBALS (-10002)
#define CO_LUA_TNIL 0
#define CO_LUA_TBOOLEAN 1
#define CO_LUA_TNUMBER 3
#define CO_LUA_TTABLE 5

/* lua51.dll's lua_pcall as LuaJIT 2.1 ships it: the hook goes in only over these bytes. */
#define CO_LUA_PCALL_SHIPPED "48 89 5C 24 08 57 48 83 EC 20 8B 59 08 4C 8B D1 0F B6 7B 7D 40 80 E7 F0"

/* lua51.dll's functions (NULL until every one was found) and the game's main state (NULL until the frame found it). */
const CoLuaApi* co_lua(void);
lua_State* co_lua_state(void);
/* Hooks lua51!lua_pcall once. `frame` then runs once per game frame (after co_lua_tick asked for it), inside the first
 * lua_pcall made on the main thread of a state while no function runs on it, before the engine's own call, with the
 * stack as it found it. NULL when the hook is in, else why not. */
const char* co_lua_install(void (*frame)(lua_State* L));
/* From the game-thread tick: the next eligible lua_pcall runs the frame. */
void co_lua_tick(void);
/* Frames whose work left the stack other than it found it (set back each time); 0 is the expected count. */
uint32_t co_lua_unbalanced(void);

/* ---- the engine's Gui (co_gui.c): one op per call, its arguments and results as Know Your Constellation's ---- */
#define CO_GUI_WORLDS 16
#define CO_GUI_RAISED (-2) /* the engine raised: error holds its message */
enum
{
    CO_GUI_MAIN_WORLD = 1, CO_GUI_LIST_WORLDS, CO_GUI_CREATE, CO_GUI_DESTROY, CO_GUI_MATERIAL, CO_GUI_SET_SCALAR,
    CO_GUI_SET_VECTOR2, CO_GUI_SET_VECTOR4, CO_GUI_SET_TEXTURE, CO_GUI_RESOLUTION, CO_GUI_RECT, CO_GUI_UPDATE_RECT,
    CO_GUI_TEXT, CO_GUI_UPDATE_TEXT
};
typedef struct CoGuiCall
{
    int32_t op, capacity;          /* capacity: of refs, for CO_GUI_LIST_WORLDS */
    double world, gui, item;       /* the objects the call names, or the one it made */
    double x, y, z, w, h;          /* a position (z: the layer) and a size; resolution's results in w, h */
    double size, value;            /* a text size; a scalar */
    uint8_t a, r, g, b;            /* Color(a, r, g, b) */
    uint32_t length;               /* text's bytes */
    const char* text;              /* not NUL-terminated */
    uint64_t id[2];                /* IdString64s: the font or slot, then the material or texture */
    double refs[CO_GUI_WORLDS];    /* the world list's numbers */
    char error[192];
} CoGuiCall;
/* 1 done, 0 the engine gave nothing, CO_GUI_RAISED; the world list: its count, or -1 without one. */
int co_gui_call(CoGuiCall* call);
/* On the frame: 1 once calls can be made (the state found, every function looked up). */
int co_gui_ready(void);
/* The registry table the objects live in: never Know Your Constellation's "vanilla_plus_megapack.engine_gui". */
#define CO_GUI_TABLE_KEY "complex_operations.war_table_tag"

#ifdef CO_TEST
int co_lua_test_state(lua_State* L);
void co_lua_test_reset(void);
int co_lua_test_detour(lua_State* L, int arguments, int results, int handler);
void co_gui_test_reset(void);
#endif

#endif
