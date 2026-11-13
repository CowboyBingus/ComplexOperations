/* The war-table tag. W-T1: the state from the decision reader over every input the launch reads. W-T2: the briefing's
 * launch-clear prediction against what the launch detour then does. W-T3: the banner drawn through a real lua51.dll
 * (LuaJIT 2.1, CO_LUA51) with a stand-in stingray: each state's rectangles and texts, ids kept on update, nothing for
 * another level, removal at a screen change, the stack as found, nothing inside a running function. Also the banner's
 * place over the supported resolutions against every place Know Your Constellation's box can take (PROOFS.md W-5). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "co_lua.h"
#include "co_tag.h"
#include "hellforge_net.h"

void co_peers_test_reset(void);
void co_peers_test_message(const void* data, uint32_t bytes);
void co_capacity_test_on(int on);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s (line %d): %s\n", g_case, __LINE__, #what); g_failed++; } } while (0)

#define WAR 0x1000000u
#define SESSION 0x1200000u
#define STATE 0x1400000u
#define UI 0x3600000u
#define OWNER 0x3500000u
#define FONT_HASH 0x1111222233334444ull
#define MATERIAL_HASH 0x5555666677778888ull
#define ATLAS_HASH 0x9999AAAABBBBCCCCull
#define OWN 0x1111222233334444ull

static void Put(uint32_t at, uint64_t value, size_t size) { memcpy(g_fake.image + at, &value, size); }
static uint64_t Ptr(uint32_t rva) { return (uint64_t)(uintptr_t)(g_fake.image + rva); }
static uint32_t Seed(uint32_t i) { uint32_t s; memcpy(&s, g_fake.image + WAR + 0xF9A10u + i * 0x4Cu + 0x34u, 4); return s; }
static void Entry(uint32_t i, uint32_t slot, uint32_t placement, uint32_t seed, uint32_t difficulty)
{
    const uint32_t at = WAR + 0xF9A10u + i * 0x4Cu;
    Put(at + 0x28, slot, 4); Put(at + 0x2C, placement, 4); Put(at + 0x34, seed, 4); Put(at + 0x3C, difficulty, 4);
}
static void Count(uint32_t n) { Put(WAR + 0xFFC08u, n, 4); }
static void Highlight(uint32_t index) { Put(WAR + 0x17A2A0u, index, 4); if (index < 110) Put(WAR + 0xF7280u + index * 0x5Cu + 0x34u, 1, 1); }
static void Pick(int32_t kind, int32_t placement, uint32_t difficulty)
{
    Put(WAR + 0x17A298u, (uint32_t)kind, 4); Put(WAR + 0x17A2A4u, (uint32_t)placement, 4); Put(WAR + 0x17A2A8u, difficulty, 4);
}
static void ScreenTop(uint32_t screen) { Put(UI + 0x429Cu, screen, 4); Put(UI + 0x429Cu + 20u, 1, 4); }

static uintptr_t Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_peers_test_reset();
    co_tag_test_reset();
    co_capacity_test_on(1);
    Put(CO_WAR_OBJECT, Ptr(WAR), 8);
    Put(CO_SESSION_OBJECT, Ptr(SESSION), 8);
    Put(CO_GAME_STATE, Ptr(STATE), 8);
    Put(SESSION + 0xB398u, OWN, 8);
    co_peers_start((uintptr_t)g_fake.image);
    return (uintptr_t)g_fake.image;
}

/* ---- W-T1 --------------------------------------------------------------------------------------------------------- */

static int State(int level11, int marked, int host, int own, int row, int briefing, int clears, int build, int* why)
{
    const CoTagInputs in = {level11, marked, host, own, row, briefing, clears, build};
    return co_tag_state(&in, why);
}

static void Decisions(void)
{
    int why;
    g_case = "W-T1 the host";
    CHECK(State(1, 1, 1, 1, CO_HOST_ROW_NONE, 0, 0, 0, &why) == CO_TAG_COMPLEX);
    CHECK(State(1, 0, 1, 1, CO_HOST_ROW_NONE, 0, 0, 0, &why) == CO_TAG_STANDARD);
    CHECK(State(1, 1, 1, 0, CO_HOST_ROW_OFF, 0, 0, 0, &why) == CO_TAG_OFF && why == CO_TAG_WHY_PC);
    CHECK(State(1, 0, 1, 0, CO_HOST_ROW_OFF, 0, 0, 0, &why) == CO_TAG_OFF && why == CO_TAG_WHY_PC);
    CHECK(State(1, 1, 1, 0, CO_HOST_ROW_OFF, 0, 0, 1, &why) == CO_TAG_OFF && why == CO_TAG_WHY_BUILD);
    g_case = "W-T1 a client";
    for (int own = 0; own < 2; own++)
        for (int marked = 0; marked < 2; marked++)
        {
            CHECK(State(1, marked, 0, own, CO_HOST_ROW_NONE, 0, 0, 0, &why) == CO_TAG_WAITING && why == CO_TAG_WHY_NONE);
            CHECK(State(1, marked, 0, own, CO_HOST_ROW_OFF, 0, 0, 0, &why) == CO_TAG_OFF && why == CO_TAG_WHY_HOST);
        }
    CHECK(State(1, 1, 0, 0, CO_HOST_ROW_ON, 0, 0, 0, &why) == CO_TAG_OFF && why == CO_TAG_WHY_PC);
    CHECK(State(1, 1, 0, 1, CO_HOST_ROW_ON, 0, 0, 0, &why) == CO_TAG_COMPLEX);
    CHECK(State(1, 0, 0, 1, CO_HOST_ROW_ON, 0, 0, 0, &why) == CO_TAG_STANDARD);
    g_case = "W-T1 another level";
    for (int host = 0; host < 2; host++) CHECK(State(0, 1, host, 1, CO_HOST_ROW_ON, 0, 0, 0, &why) == CO_TAG_NONE);
    g_case = "W-T1 the briefing";
    CHECK(State(1, 1, 1, 1, CO_HOST_ROW_ON, 1, 1, 0, &why) == CO_TAG_CLEARED);
    CHECK(State(1, 1, 1, 1, CO_HOST_ROW_ON, 1, 0, 0, &why) == CO_TAG_COMPLEX);
    CHECK(State(1, 1, 1, 1, CO_HOST_ROW_ON, 1, -1, 0, &why) == CO_TAG_COMPLEX);
    CHECK(State(1, 1, 1, 1, CO_HOST_ROW_ON, 0, 1, 0, &why) == CO_TAG_COMPLEX); /* the war table: no mission is picked */
    CHECK(State(1, 1, 0, 1, CO_HOST_ROW_ON, 1, 1, 0, &why) == CO_TAG_COMPLEX); /* a client's string is the host's */
    CHECK(State(1, 0, 1, 1, CO_HOST_ROW_ON, 1, 1, 0, &why) == CO_TAG_STANDARD);

    /* each state is what co_complex_decision makes of the same inputs */
    g_case = "W-T1 one decision per state";
    for (int host = 0; host < 2; host++)
        for (int own = 0; own < 2; own++)
            for (int row = CO_HOST_ROW_NONE; row <= CO_HOST_ROW_ON; row++)
                for (int marked = 0; marked < 2; marked++)
                {
                    const int d = co_complex_decision(host, own, row, co_seed_marked(0x0184CCF6u, marked));
                    const int s = State(1, marked, host, own, row, 0, 0, 0, &why);
                    CHECK((d == CO_MISSION_COMPLEX) == (s == CO_TAG_COMPLEX));
                    CHECK(d != CO_MISSION_NORMAL || s == CO_TAG_STANDARD || s == CO_TAG_OFF);
                    CHECK(d != CO_MISSION_LEAVE || s == CO_TAG_WAITING || s == CO_TAG_OFF);
                }
}

static void Readers(void)
{
    uint32_t index, entry;
    int level11, marked, found;
    const uintptr_t war = Start("W-T1 the map's reader") + WAR;
    Highlight(0xFFFFFFFFu); Count(4);
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 1 && !found);              /* none highlighted */
    Highlight(200);
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 1 && !found);              /* past the board */
    Put(WAR + 0x17A2A0u, 7, 4);
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 1 && !found);              /* record not in use */
    Highlight(7);
    Count(0);
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 1 && !found);              /* no entries */
    Count(0x14B);
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 0);                        /* a bad count */
    Entry(0, 3, 1, 0x1001, 11); Entry(1, 7, 2, 0x2000, 11); Entry(2, 7, 3, 0x3000, 11); Entry(3, 9, 4, 0x4001, 11);
    Count(4);
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 1 && found && level11 && !marked && index == 7 && entry == 1);
    Entry(2, 7, 3, 0x3001, 11);
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 1 && found && level11 && marked);
    Entry(1, 7, 2, 0x2001, 10); Entry(2, 7, 3, 0x3000, 11);                                              /* mixed levels */
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 1 && level11 && !marked && entry == 2);
    Entry(2, 7, 3, 0x3001, 10);
    CHECK(co_tag_map_entries(war, &index, &entry, &level11, &marked, &found) == 1 && found && !level11);  /* another level */

    g_case = "W-T1 the briefing's reader";
    Entry(0, 3, 5, 0x1000, 11); Entry(1, 7, 6, 0x2000, 11); Entry(2, 7, 6, 0x3001, 11); Entry(3, 9, 6, 0x4001, 10);
    Pick(-1, 6, 11);
    CHECK(co_tag_briefing_entry(war, &index, &entry, &level11, &marked, &found) == 1 && !found);
    Pick(0, -1, 11);
    CHECK(co_tag_briefing_entry(war, &index, &entry, &level11, &marked, &found) == 1 && !found);
    Pick(0, 6, 11);
    CHECK(co_tag_briefing_entry(war, &index, &entry, &level11, &marked, &found) == 1 && found && level11 && !marked && entry == 1 && index == 7);
    Pick(0, 6, 10);
    CHECK(co_tag_briefing_entry(war, &index, &entry, &level11, &marked, &found) == 1 && found && !level11 && entry == 3);
    Pick(0, 8, 11);
    CHECK(co_tag_briefing_entry(war, &index, &entry, &level11, &marked, &found) == 1 && !found);
    Count(0x200);
    CHECK(co_tag_briefing_entry(war, &index, &entry, &level11, &marked, &found) == 0);
}

/* ---- W-T2 --------------------------------------------------------------------------------------------------------- */

typedef uint64_t (*LaunchFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static const uint8_t kLaunch[11] = {0x40, 0x53, 0x55, 0x56, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00};

static void Prediction(void)
{
    static const uint32_t kRoles[] = {HF_SESSION_HOST, HF_SESSION_SOLO, HF_SESSION_JOINER};
    int cases = 0;
    for (int r = 0; r < 3; r++)
        for (int own = 0; own < 2; own++)
            for (int over = 0; over < 2; over++)
                for (int peer = 0; peer < 4; peer++) /* alone; a peer that said on; one that said off; one that said nothing */
                {
                    const uintptr_t game = Start("W-T2 the prediction is the launch");
                    memcpy(g_fake.image + CO_LAUNCH_STRING, kLaunch, sizeof kLaunch);
                    CHECK(co_launch_install(game) == NULL);
                    const LaunchFn launch = (LaunchFn)fake_host_detour(CO_LAUNCH_STRING);
                    g_fake.role = HF_SESSION_HOST;
                    if (peer == 1 || peer == 2)
                    {
                        CoCapability c = {.protocol = CO_SHARE_PROTOCOL, .on = peer == 1, .peer = 77};
                        co_peers_test_message(&c, sizeof c);
                    }
                    if (peer != 0) Put(SESSION + 0x159B0u + 0x20u + 2 * 0x38u, 77, 8);
                    Put(SESSION + 0x159B0u + 0x20u, OWN, 8);
                    Put(STATE + 0xB0u, over ? 'x' : 0, 1);
                    co_capacity_test_on(own);
                    g_fake.role = kRoles[r];
                    Count(2); Entry(0, 4, 6, 0x5001, 11); Entry(1, 4, 7, 0x6001, 11);
                    const int predicted = co_launch_would_clear();
                    launch(0, 0, 3, 6, 11);
                    const int cleared = (Seed(0) & 1u) == 0;
                    CHECK(cleared == (predicted == 1));
                    CHECK(Seed(1) == 0x6001);
                    CHECK(kRoles[r] != HF_SESSION_JOINER || predicted == -1);
                    CHECK(kRoles[r] == HF_SESSION_JOINER || predicted == co_launch_clears(co_peers_reports(),
                        (const uint64_t[4]){OWN, 0, peer ? 77 : 0, 0}, 4, OWN, own, over));
                    cases++;
                }
    CHECK(cases == 48);
}

/* ---- W-T3 --------------------------------------------------------------------------------------------------------- */

static const char kStingray[] =
    "E = {log = {}, created = {}, destroyed = {}, calls = 0}\n"
    "local function note(name) E.calls = E.calls + 1 if E.raise == name then error('boom from ' .. name) end end\n"
    "main, ui = {name = 'main'}, {name = 'ui'}\n"
    "E.worlds = {main, ui}\n"
    "E.res = {1280, 720}\n"
    "local V2 = setmetatable({}, {__call = function(_, x, y) return {x, y, kind = 'v2'} end})\n"
    "local V3 = setmetatable({}, {__call = function(_, x, y, z) return {x, y, z, kind = 'v3'} end})\n"
    "local function kind(v) return type(v) == 'table' and v.kind end\n"
    "local function given(name, n, ...) local got = select('#', ...)\n"
    "  assert(got == n, name .. ' was given ' .. got .. ' arguments, not ' .. n) end\n"
    "local function gui(g) assert(type(g) == 'table' and g.world and not g.dead, 'not a live GUI') end\n"
    "local function box(p, s, c) assert(kind(p) == 'v3' and kind(s) == 'v2' and kind(c) == 'color', 'not a box') end\n"
    "stingray = {Vector2 = V2, Vector3 = V3,\n"
    "  Color = function(a, r, g, b) return {a, r, g, b, kind = 'color'} end,\n"
    "  IdString64 = {from_hex = function(h) assert(#h == 16, 'hex ' .. h) return 'id:' .. h end},\n"
    "  Application = {main_world = function() note('main_world') return main end,\n"
    "    worlds = function() note('worlds') return E.worlds end},\n"
    "  World = {create_screen_gui = function(...) given('create_screen_gui', 4, ...) local w, mode, a, b = ... note('create')\n"
    "      local g = {world = w, mode = mode, n = 0, items = {}} E.created[#E.created + 1] = g return g end,\n"
    "    destroy_gui = function(...) given('destroy_gui', 2, ...) local w, g = ... note('destroy') g.dead = true\n"
    "      E.destroyed[#E.destroyed + 1] = g end},\n"
    "  Gui = {material = function(...) given('material', 2, ...) local g, id = ... note('material') gui(g) E.material = id\n"
    "      return {id = id} end,\n"
    "    resolution = function() note('resolution') return E.res[1], E.res[2] end,\n"
    "    rect = function(...) given('rect', 4, ...) local g, p, s, c = ... note('rect') gui(g) box(p, s, c)\n"
    "      g.n = g.n + 1 g.items[g.n] = {kind = 'rect', p = p, s = s, c = c} return g.n end,\n"
    "    update_rect = function(...) given('update_rect', 5, ...) local g, id, p, s, c = ... note('update_rect') gui(g)\n"
    "      box(p, s, c) assert(g.items[id] and g.items[id].kind == 'rect', 'not a rectangle of the GUI')\n"
    "      g.items[id] = {kind = 'rect', p = p, s = s, c = c} end,\n"
    "    text = function(...) given('text', 7, ...) local g, t, font, size, mat, p, c = ... note('text') gui(g)\n"
    "      g.n = g.n + 1 g.items[g.n] = {kind = 'text', t = t, font = font, size = size, mat = mat, p = p, c = c} return g.n end,\n"
    "    update_text = function(...) given('update_text', 8, ...) local g, id, t, font, size, mat, p, c = ... note('update_text')\n"
    "      gui(g) assert(g.items[id] and g.items[id].kind == 'text', 'not a text of the GUI')\n"
    "      g.items[id] = {kind = 'text', t = t, font = font, size = size, mat = mat, p = p, c = c} end},\n"
    "  Material = {set_scalar = function(...) given('set_scalar', 3, ...) note('set_scalar') end,\n"
    "    set_vector2 = function(...) given('set_vector2', 3, ...) note('set_vector2') end,\n"
    "    set_vector4 = function(...) given('set_vector4', 3, ...) note('set_vector4') end,\n"
    "    set_texture = function(...) given('set_texture', 3, ...) local m, slot, t = ... note('set_texture') E.atlas = t end}}\n"
    "function E.near(a, b) return math.abs(a - b) < 1e-3 end\n"
    "function E.rect(g, id, x, y, w, h, z, a, r, gg, b) local i = g.items[id]\n"
    "  if not i or i.kind ~= 'rect' then return 'item ' .. id .. ' is no rectangle' end\n"
    "  if not (E.near(i.p[1], x) and E.near(i.p[2], y) and E.near(i.p[3], z) and E.near(i.s[1], w) and E.near(i.s[2], h)) then\n"
    "    return string.format('rect %d at %.3f %.3f %.3f size %.3f %.3f', id, i.p[1], i.p[2], i.p[3], i.s[1], i.s[2]) end\n"
    "  if i.c[1] ~= a or i.c[2] ~= r or i.c[3] ~= gg or i.c[4] ~= b then return 'rect ' .. id .. ' colour' end end\n"
    "function E.text(g, id, t, size, x, y, z, a, r, gg, b) local i = g.items[id]\n"
    "  if not i or i.kind ~= 'text' then return 'item ' .. id .. ' is no text' end\n"
    "  if i.t ~= t then return 'text ' .. id .. ' is ' .. tostring(i.t) end\n"
    "  if not (E.near(i.size, size) and E.near(i.p[1], x) and E.near(i.p[2], y) and E.near(i.p[3], z)) then return 'text place' end\n"
    "  if i.c[1] ~= a or i.c[2] ~= r or i.c[3] ~= gg or i.c[4] ~= b then return 'text ' .. id .. ' colour' end\n"
    "  if i.font ~= 'id:1111222233334444' or i.mat ~= 'id:5555666677778888' then return 'not the native font' end end\n";

static HMODULE g_lua51;
static lua_State* (*g_newState)(void);
static void (*g_openLibs)(lua_State* L);
static void (*g_close)(lua_State* L);
static int (*g_loadString)(lua_State* L, const char* chunk);
static int (*g_pcall)(lua_State* L, int arguments, int results, int handler);
static int (*g_getTop)(lua_State* L);
static void (*g_setTop)(lua_State* L, int index);
static int (*g_type)(lua_State* L, int index);
static const char* (*g_toString)(lua_State* L, int index, size_t* length);
static void (*g_pushClosure)(lua_State* L, int (*fn)(lua_State*), int n);
#define FIND(field, name) (*(FARPROC*)&field = GetProcAddress(g_lua51, name)) != NULL

static int LuaLoad(void)
{
    const char* path = getenv("CO_LUA51");
    if (g_lua51 != NULL) return 1;
    if (path == NULL || *path == 0 || (g_lua51 = LoadLibraryA(path)) == NULL) return 0;
    return FIND(g_newState, "luaL_newstate") && FIND(g_openLibs, "luaL_openlibs") && FIND(g_close, "lua_close") &&
        FIND(g_loadString, "luaL_loadstring") && FIND(g_pcall, "lua_pcall") && FIND(g_getTop, "lua_gettop") &&
        FIND(g_setTop, "lua_settop") && FIND(g_type, "lua_type") && FIND(g_toString, "lua_tolstring") &&
        FIND(g_pushClosure, "lua_pushcclosure");
}

/* A chunk run: NULL, or its error or the text it returned. */
static const char* Run(lua_State* L, const char* chunk)
{
    static char message[1024];
    const int top = g_getTop(L);
    const char* result = NULL;
    if (g_loadString(L, chunk) != 0 || g_pcall(L, 0, 1, 0) != 0 || g_type(L, -1) == 4)
    {
        const char* text = g_toString(L, -1, NULL);
        snprintf(message, sizeof message, "%s", text != NULL ? text : "(not a text)");
        result = message;
    }
    g_setTop(L, top);
    return result;
}
#define CHECK_LUA(L, chunk) do { const char* e_ = Run((L), (chunk)); if (e_ != NULL) { printf("FAILED %s (line %d): %s\n", g_case, __LINE__, e_); g_failed++; } } while (0)

static int Lines(const char* text)
{
    int n = 0;
    for (const char* at = strstr(g_fake.log, text); at != NULL; at = strstr(at + 1, text)) n++;
    return n;
}

static int g_balanced = 1;
/* One game frame: the tick, then the engine's lua_pcall of a function through the hook; the stack as it was after. */
static void Frame(lua_State* L)
{
    co_tag_tick();
    const int top = g_getTop(L);
    g_loadString(L, "return");
    co_lua_test_detour(L, 0, 0, 0);
    if (g_getTop(L) != top) g_balanced = 0;
    g_setTop(L, top);
}

static double Calls(lua_State* L)
{
    const int top = g_getTop(L);
    double calls = -1;
    if (g_loadString(L, "return E.calls") == 0 && g_pcall(L, 0, 1, 0) == 0) calls = atof(g_toString(L, -1, NULL));
    g_setTop(L, top);
    return calls;
}

/* The frame asked from inside a C function that Lua runs: lua_getstack sees it, so nothing is drawn there. */
static int Inside(lua_State* L)
{
    co_tag_tick();
    g_loadString(L, "return");
    co_lua_test_detour(L, 0, 0, 0);
    return 0;
}

/* The anchor of a 1280x720 screen: the native panel 533 units wide at x 54, its top 168 units below the screen's. */
static const CoTagAnchor kAnchor = {36.0, 360.4166666, 355.3333333, 247.5833333, 2.0 / 3.0, FONT_HASH, MATERIAL_HASH, ATLAS_HASH};

static void Anchor(uint32_t registry, uint32_t kind, uint32_t frame)
{
    const uint32_t widget = OWNER + frame;
    Put(FAKE_REGISTRY + registry, 1, 4); Put(FAKE_REGISTRY + registry + 8, Ptr(OWNER), 8); Put(FAKE_REGISTRY + registry + 16, kind, 4);
    Put(widget, 0x10, 4);
    const float w = 533.0f, h = 371.375f, s = 2.0f / 3.0f, x = 36.0f, y = (float)kAnchor.y, one = 1.0f;
    memcpy(g_fake.image + widget + 36, &w, 4); memcpy(g_fake.image + widget + 40, &h, 4); memcpy(g_fake.image + widget + 84, &one, 4);
    memcpy(g_fake.image + widget + 100, &s, 4); memcpy(g_fake.image + widget + 140, &s, 4);
    memcpy(g_fake.image + widget + 148, &x, 4); memcpy(g_fake.image + widget + 156, &y, 4);
}

static uintptr_t Scene(const char* name)
{
    const uintptr_t game = Start(name);
    co_tag_test_pins(g_fake.image);
    Put(0x347CE28u, Ptr(UI), 8);
    Put(0x3772268u, FONT_HASH, 8); Put(0x37C5478u, Ptr(0x3700000u), 8); Put(0x3700000u + 24u, MATERIAL_HASH, 8); Put(0x3772EE8u, ATLAS_HASH, 8);
    Anchor(25224u, 226u, 349072u);
    Anchor(25272u, 229u, 31232u);
    g_fake.originals[0].target = (uintptr_t)GetProcAddress(g_lua51, "lua_pcall");
    g_fake.originals[0].original = (void*)GetProcAddress(g_lua51, "lua_pcall");
    g_fake.role = HF_SESSION_HOST;
    Count(3); Entry(0, 7, 6, 0x5001, 11); Entry(1, 7, 7, 0x6001, 11); Entry(2, 8, 8, 0x7001, 10);
    Highlight(7); Highlight(8); Put(WAR + 0x17A2A0u, 7, 4);
    ScreenTop(15);
    return game;
}

/* The drawn GUI's items are the plan for this state at this place. */
static void Drawn(lua_State* L, int state, int why, int gui)
{
    CoTagBox box;
    CoTagPlan plan;
    char chunk[4096];
    size_t used = 0;
    const int place = co_tag_place(&kAnchor, 1280, 720, &box);
    co_tag_plan(state, why, &box, kAnchor.scale, &plan);
    CHECK(place == CO_TAG_PLACE_BESIDE && co_tag_test_shown()->state == state && co_tag_test_shown()->visible);
    used += (size_t)snprintf(chunk + used, sizeof chunk - used, "local g = E.created[%d] if not g then return 'no GUI' end local e\n", gui);
    for (int i = 0; i < CO_TAG_RECTS; i++)
    {
        const CoTagRect* r = &plan.rect[i];
        used += (size_t)snprintf(chunk + used, sizeof chunk - used, "e = E.rect(g, %d, %.6f, %.6f, %.6f, %.6f, %.6f, %u, %u, %u, %u) if e then return e end\n",
            i + 1, r->x, r->y, r->w, r->h, r->z, r->c.a, r->c.r, r->c.g, r->c.b);
    }
    for (int i = 0; i < 2; i++)
    {
        const CoTagText* x = &plan.text[i];
        used += (size_t)snprintf(chunk + used, sizeof chunk - used, "e = E.text(g, %d, '%s', %.6f, %.6f, %.6f, %.6f, %u, %u, %u, %u) if e then return e end\n",
            CO_TAG_RECTS + i + 1, x->text, x->size, x->x, x->y, x->z, x->c.a, x->c.r, x->c.g, x->c.b);
    }
    snprintf(chunk + used, sizeof chunk - used, "if g.n ~= 8 or g.world ~= ui or g.mode ~= 'scale' then return 'items ' .. g.n end");
    CHECK_LUA(L, chunk);
}

static void Banner(void)
{
    if (!LuaLoad()) { printf("FAILED W-T3: no lua51.dll (CO_LUA51)\n"); g_failed++; return; }
    lua_State* L = g_newState();
    g_openLibs(L);
    const uintptr_t game = Scene("W-T3 the complex banner");
    CHECK_LUA(L, kStingray);
    CHECK(co_tag_install(game) == NULL && g_fake.hookCount == 1);
    Frame(L);
    CHECK(fake_host_logged("war-table tag: the game's Lua state found"));
    Drawn(L, CO_TAG_COMPLEX, CO_TAG_WHY_NONE, 1);
    CHECK_LUA(L, "local g = E.created[1] if g.items[7].t ~= 'COMPLEX OPERATION' or g.items[8].t ~= '3 MISSIONS ON ONE 16 KM MAP' then return 'texts' end");
    CHECK_LUA(L, "if E.atlas ~= 'id:9999aaaabbbbcccc' or E.material ~= 'id:5555666677778888' then return 'the native font material' end");
    for (int i = 0; i < CO_TAG_SETTLE; i++) Frame(L);
    CHECK(fake_host_logged("war-table tag: map, operation 7: complex, banner x 399.3 y 612.0 w 355.3 h 47.9 (beside the panel; panel x 36.0 y 360.4 w 355.3 h 247.6, scale 0.6667)"));

    g_case = "W-T3 an unchanged frame makes no Gui call";
    const double calls = Calls(L);
    Frame(L); Frame(L);
    CHECK(Calls(L) == calls);

    g_case = "W-T3 the standard banner reuses the items";
    Entry(0, 7, 6, 0x5000, 11); Entry(1, 7, 7, 0x6000, 11);
    Frame(L);
    Drawn(L, CO_TAG_STANDARD, CO_TAG_WHY_NONE, 1);
    CHECK_LUA(L, "if #E.created ~= 1 or #E.destroyed ~= 0 then return 'a second GUI' end");

    g_case = "W-T3 the part off, and a client waiting";
    co_capacity_test_on(0);
    Frame(L);
    Drawn(L, CO_TAG_OFF, CO_TAG_WHY_PC, 1);
    co_capacity_test_on(1);
    g_fake.role = HF_SESSION_JOINER;
    Frame(L);
    Drawn(L, CO_TAG_WAITING, CO_TAG_WHY_NONE, 1);
    g_fake.role = HF_SESSION_HOST;

    g_case = "W-T3 another level: nothing shows";
    Put(WAR + 0x17A2A0u, 8, 4);
    Frame(L);
    CHECK(co_tag_test_shown()->state == CO_TAG_NONE && !co_tag_test_shown()->visible && co_tag_test_shown()->gui != 0);
    CHECK_LUA(L, "local g = E.created[1] for i = 1, 6 do if g.items[i].s[1] ~= 0 or g.items[i].s[2] ~= 0 then return 'rect ' .. i end end\n"
                 "for i = 7, 8 do if g.items[i].t ~= '' then return 'text ' .. i end end");
    for (int i = 0; i < CO_TAG_SETTLE; i++) Frame(L);
    CHECK(fake_host_logged("war-table tag: map, operation 8: not level 11, no banner"));

    g_case = "W-T3 the briefing: complex, then cleared for an unknown peer";
    Put(WAR + 0x17A2A0u, 7, 4);
    Entry(0, 7, 6, 0x5001, 11); Entry(1, 7, 7, 0x6001, 11);
    Pick(0, 7, 11);
    ScreenTop(14);
    Frame(L);
    Drawn(L, CO_TAG_COMPLEX, CO_TAG_WHY_NONE, 1);
    Put(SESSION + 0x159B0u + 0x20u, OWN, 8); Put(SESSION + 0x159B0u + 0x20u + 0x38u, 99, 8);
    Frame(L);
    Drawn(L, CO_TAG_CLEARED, CO_TAG_WHY_NONE, 1);
    CHECK_LUA(L, "if E.created[1].items[7].t ~= 'STARTS AS STANDARD' then return 'cleared' end");
    Put(SESSION + 0x159B0u + 0x20u + 0x38u, 0, 8);

    g_case = "W-T3 nothing inside a running function";
    Entry(1, 7, 7, 0x6000, 11);
    const double before = Calls(L);
    g_pushClosure(L, Inside, 0);
    CHECK(g_pcall(L, 0, 0, 0) == 0);
    CHECK(Calls(L) == before && co_tag_test_shown()->state == CO_TAG_CLEARED);
    Frame(L);
    CHECK(co_tag_test_shown()->state == CO_TAG_STANDARD);

    g_case = "W-T3 the engine raises";
    CHECK_LUA(L, "E.raise = 'update_rect'");
    Entry(1, 7, 7, 0x6001, 11);
    Frame(L);
    CHECK(fake_host_logged("war-table tag: the engine's Gui raised: ") && fake_host_logged("boom from update_rect"));
    CHECK(co_tag_test_shown()->gui == 0);
    CHECK_LUA(L, "E.raise = nil");
    Frame(L);
    CHECK(co_tag_test_shown()->gui == 0); /* no tag until it changes */
    Entry(1, 7, 7, 0x6000, 11);
    Frame(L);
    CHECK(co_tag_test_shown()->visible && co_tag_test_shown()->gui != 0 && co_tag_test_shown()->state == CO_TAG_STANDARD);

    g_case = "W-T3 a refused read: nothing shows, one line";
    Count(0x200);
    Frame(L); Frame(L); Frame(L);
    CHECK(!co_tag_test_shown()->visible && Lines("the mission table's count is past its entries") == 1);
    Count(3);
    Frame(L);
    CHECK(co_tag_test_shown()->visible);

    g_case = "W-T3 a screen change removes the GUI";
    ScreenTop(3);
    Frame(L);
    CHECK(co_tag_test_shown()->gui == 0 && fake_host_logged("war-table tag: removed (the screen changed)"));
    CHECK_LUA(L, "local n = #E.created if #E.destroyed < 1 or E.destroyed[#E.destroyed] ~= E.created[n] then return 'not destroyed' end");

    g_case = "W-T3 a GUI an earlier start left is destroyed";
    ScreenTop(15);
    Put(WAR + 0x17A2A0u, 7, 4);
    Frame(L);
    CHECK(co_tag_test_shown()->gui != 0);
    co_lua_test_reset();
    co_tag_test_reset();
    CHECK(co_tag_install(game) == NULL);
    Frame(L);
    CHECK(fake_host_logged("war-table tag: 1 GUI(s) an earlier start left were destroyed"));
    CHECK_LUA(L, "local live = 0 for _, g in ipairs(E.created) do if not g.dead then live = live + 1 end end if live ~= 1 then return live .. ' live GUIs' end");

    g_case = "W-T3 the stack as found, every frame";
    CHECK(g_balanced && co_lua_unbalanced() == 0);
    CHECK_LUA(L, "if type(debug.getregistry()['complex_operations.war_table_tag']) ~= 'table' then return 'no table under the mod key' end\n"
                 "if debug.getregistry()['vanilla_plus_megapack.engine_gui'] ~= nil then return 'the Megapack key is used' end");
    co_lua_test_reset();
    g_close(L);
}

/* ---- W-5: the place over the supported resolutions -------------------------------------------------------------- */

static int Apart(double ax, double ay, double aw, double ah, const CoTagBox* b)
{
    return b->x >= ax + aw || b->x + b->w <= ax || b->y >= ay + ah || b->y + b->h <= ay;
}

static void Places(void)
{
    static const double kSizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {2560, 1080}, {3440, 1440}};
    g_case = "W-5 the place";
    int beside = 0, column = 0;
    for (size_t r = 0; r < sizeof kSizes / sizeof kSizes[0]; r++)
        for (double below = 0; below <= 1080; below += 4)  /* the panel's top, in units below the screen's top */
            for (double tall = 40; tall <= 700; tall += 60)
            {
                const double W = kSizes[r][0], H = kSizes[r][1], s = fmin(W / 1920, H / 1080);
                const CoTagAnchor a = {(W - fmin(W, H * 16 / 9)) / 2 + 54 * s, H - below * s - tall * s, 533 * s, tall * s, s, 1, 1, 1};
                if (a.y < 0) continue;
                CoTagBox b;
                const int place = co_tag_place(&a, W, H, &b);
                CHECK(place != CO_TAG_PLACE_NONE);
                if (place == CO_TAG_PLACE_NONE) continue;
                beside += place == CO_TAG_PLACE_BESIDE;
                column += place == CO_TAG_PLACE_COLUMN;
                CHECK(place != CO_TAG_PLACE_BESIDE || below >= CO_TAG_HEIGHT + CO_TAG_RAISE + CO_TAG_SIDE - 1e-9);
                CHECK(b.x >= 0 && b.y >= 0 && b.x + b.w <= W && b.y + b.h <= H);
                CHECK(Apart(a.x, a.y, a.w, a.h, &b));                                           /* the native panel */
                CHECK(Apart(a.x, 0, a.w, a.y + 3 * s, &b));                                     /* KYC below it, any height */
                const double side = a.x + a.w + 12 * s + a.w <= W - 12 * s ? a.x + a.w + 12 * s : a.x - a.w - 12 * s;
                const double top = fmin(H - 12 * s, a.y + a.h);
                CHECK(Apart(side, 0, a.w, top, &b));                                            /* KYC beside it, any height */
                CHECK(fabs(b.w - a.w) < 1e-9 && fabs(b.h - CO_TAG_HEIGHT * s) < 1e-9);
            }
    CHECK(beside > 0 && column > 0);
    /* the measured panels: the briefing 168 units below the top, the war table's operation panel 132 */
    for (size_t r = 0; r < sizeof kSizes / sizeof kSizes[0]; r++)
        for (int k = 0; k < 2; k++)
        {
            const double W = kSizes[r][0], H = kSizes[r][1], s = fmin(W / 1920, H / 1080), below = k ? 132 : 168;
            const CoTagAnchor a = {(W - fmin(W, H * 16 / 9)) / 2 + 54 * s, H - below * s - 300 * s, 533 * s, 300 * s, s, 1, 1, 1};
            CoTagBox b;
            CHECK(co_tag_place(&a, W, H, &b) == CO_TAG_PLACE_BESIDE);
        }
    g_case = "W-5 the texts";
    CHECK(strcmp(co_tag_caption(CO_TAG_COMPLEX, 0), "3 MISSIONS ON ONE 16 KM MAP") == 0);
    static const int kStates[] = {CO_TAG_COMPLEX, CO_TAG_STANDARD, CO_TAG_OFF, CO_TAG_WAITING, CO_TAG_CLEARED};
    for (size_t i = 0; i < sizeof kStates / sizeof kStates[0]; i++)
        for (int why = 0; why < 4; why++)
        {
            const char* texts[2] = {co_tag_title(kStates[i]), co_tag_caption(kStates[i], why)};
            /* a glyph at most one text size wide still leaves the side padding free */
            CHECK(strlen(texts[0]) * CO_TAG_TITLE <= 533 - 2 * CO_TAG_PAD && strlen(texts[1]) * CO_TAG_CAPTION <= 533 - 2 * CO_TAG_PAD);
            for (int t = 0; t < 2; t++)
                for (const char* c = texts[t]; *c; c++) CHECK((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ' ');
        }
}

int main(void)
{
    Decisions();
    Readers();
    Prediction();
    Banner();
    Places();
    if (g_failed) { printf("tag: %d checks failed\n", g_failed); return 1; }
    printf("tag: all cases passed\n");
    return 0;
}
