/* The engine's Gui from C for the war-table tag: the stingray.* functions Know Your Constellation draws with, called on
 * the game's main state through lua51.dll's C API. Ported from the Vanilla Plus Megapack's core/engine_gui.c (0BSD);
 * text measuring is left out (the tag's texts are fixed and proven to fit, PROOFS.md W-5).
 *
 * Each function is looked up once per state and kept in the registry; a call pushes it, then its arguments (each
 * Vector2, Vector3, Color and IdString64 made by the engine's own constructor, a lua_pcall each), and lua_pcalls it.
 * Nothing raised in the engine leaves a call: it comes back as CO_GUI_RAISED with the message. The stack is as the call
 * found it.
 *
 * The engine's objects live in one table in the registry under CO_GUI_TABLE_KEY, behind numbers that are never
 * reused: obj (number -> world, GUI, or the material at 0), rev (world -> number), items (GUI -> its rectangles and
 * texts in the order made), guis (GUI -> its world) and last. The table outlives the DLL: the first call of a new
 * start destroys the GUIs an earlier one left (while their world is still listed) and starts a new table. */
#include <stdio.h>
#include <string.h>

#include "co.h"
#include "co_lua.h"

#define TABLE_KEY CO_GUI_TABLE_KEY

enum
{
    F_MAIN_WORLD, F_WORLDS, F_CREATE, F_DESTROY, F_MATERIAL, F_RESOLUTION, F_RECT, F_UPDATE_RECT, F_TEXT, F_UPDATE_TEXT,
    F_SET_SCALAR, F_SET_VECTOR2, F_SET_VECTOR4, F_SET_TEXTURE, F_VECTOR2, F_VECTOR3, F_COLOR, F_FROM_HEX, F_COUNT
};

/* stingray.<table>.<name>, or stingray.<name> without a table (the constructors, which may be callable tables). */
static const struct { const char* table; const char* name; } kFunctions[F_COUNT] = {
    {"Application", "main_world"}, {"Application", "worlds"}, {"World", "create_screen_gui"}, {"World", "destroy_gui"},
    {"Gui", "material"}, {"Gui", "resolution"}, {"Gui", "rect"}, {"Gui", "update_rect"}, {"Gui", "text"},
    {"Gui", "update_text"}, {"Material", "set_scalar"}, {"Material", "set_vector2"}, {"Material", "set_vector4"},
    {"Material", "set_texture"}, {NULL, "Vector2"}, {NULL, "Vector3"}, {NULL, "Color"}, {"IdString64", "from_hex"}};

/* The table's fields, pushed at a call's start in this order above the stack it found (Frame). */
enum { SLOT_TABLE = 1, SLOT_OBJ, SLOT_REV, SLOT_ITEMS, SLOT_GUIS, SLOTS = SLOT_GUIS };

static struct
{
    lua_State* state;         /* the state the functions were looked up in */
    int functions[F_COUNT];   /* their registry references */
    int ready;                /* 1 looked up; -1 one is missing (never ready in this state) */
    char missing[64];
    int table;                /* the objects' table: a registry reference, 0 until the first call */
    int fresh;                /* the next call destroys what the table holds first */
    int last;
} g = {.fresh = 1};

/* One call's context: the API, the state and where the table's fields sit. */
typedef struct Frame { const CoLuaApi* api; lua_State* L; int base; int raised; CoGuiCall* call; } Frame;

#define AT(f, slot) ((f)->base + (slot))

/* ---- looking the functions up ------------------------------------------------------------------------------------ */

/* stingray's function f on top of the stack (the tables it was found in below it): 1, or 0. The caller restores the top. */
static int Find(const CoLuaApi* api, lua_State* L, int f)
{
    int top = api->gettop(L), depth = kFunctions[f].table != NULL ? 3 : 2;
    api->getfield(L, CO_LUA_GLOBALS, "stingray");
    if (api->type(L, -1) == CO_LUA_TTABLE && kFunctions[f].table != NULL) api->getfield(L, -1, kFunctions[f].table);
    if (api->type(L, -1) == CO_LUA_TTABLE) api->getfield(L, -1, kFunctions[f].name);
    return api->gettop(L) == top + depth && api->type(L, -1) != CO_LUA_TNIL;
}

int co_gui_ready(void)
{
    const CoLuaApi* api = co_lua();
    lua_State* L = co_lua_state();
    if (api == NULL || L == NULL) return 0;
    if (g.state == L) return g.ready > 0;
    g.state = L;
    g.ready = 1;
    g.missing[0] = 0;
    for (int f = 0; f < F_COUNT && g.ready > 0; f++)
    {
        int top = api->gettop(L);
        if (!Find(api, L, f))
        {
            g.ready = -1;
            snprintf(g.missing, sizeof g.missing, "stingray.%s%s%s", kFunctions[f].table ? kFunctions[f].table : "",
                kFunctions[f].table ? "." : "", kFunctions[f].name);
        }
        else g.functions[f] = api->ref(L, CO_LUA_REGISTRY);
        api->settop(L, top);
    }
    if (g.ready < 0) co_log("war-table tag: the game's Lua has no %s; no tag is drawn", g.missing);
    return g.ready > 0;
}

static const char* Unavailable(void)
{
    static char why[128];
    if (co_lua() == NULL) return "lua51.dll's functions were not found";
    if (co_lua_state() == NULL) return "the game's Lua has not run yet";
    if (g.state != co_lua_state()) return "the engine's Gui has not been looked up yet";
    if (g.ready > 0) return NULL;
    snprintf(why, sizeof why, "the game's Lua has no %s", g.missing);
    return why;
}

/* ---- the objects' table -------------------------------------------------------------------------------------------- */

static void NewTable(const Frame* f)
{
    const CoLuaApi* api = f->api;
    api->createtable(f->L, 0, 5);
    static const char* const kFields[] = {"obj", "rev", "items", "guis"};
    for (int i = 0; i < 4; i++)
    {
        api->createtable(f->L, 0, 0);
        api->setfield(f->L, -2, kFields[i]);
    }
    api->pushvalue(f->L, -1);
    api->setfield(f->L, CO_LUA_REGISTRY, TABLE_KEY);
    if (g.table != 0) api->unref(f->L, CO_LUA_REGISTRY, g.table);
    g.table = api->ref(f->L, CO_LUA_REGISTRY);
    g.last = 0;
}

/* The table's fields at SLOT_TABLE..SLOT_GUIS above base: the kept table, else the one under TABLE_KEY (an earlier
 * DLL's), else a new one. 0 when what is there is not such a table. */
static int PushSlots(Frame* f)
{
    const CoLuaApi* api = f->api;
    if (g.table != 0) api->rawgeti(f->L, CO_LUA_REGISTRY, g.table);
    else api->getfield(f->L, CO_LUA_REGISTRY, TABLE_KEY);
    if (api->type(f->L, -1) != CO_LUA_TTABLE)
    {
        api->settop(f->L, f->base);
        NewTable(f);
        api->rawgeti(f->L, CO_LUA_REGISTRY, g.table);
    }
    static const char* const kFields[] = {"obj", "rev", "items", "guis"};
    for (int i = 0; i < 4; i++)
    {
        api->getfield(f->L, AT(f, SLOT_TABLE), kFields[i]);
        if (api->type(f->L, -1) != CO_LUA_TTABLE) return 0;
    }
    return 1;
}

static int Last(const Frame* f)
{
    f->api->getfield(f->L, AT(f, SLOT_TABLE), "last");
    int last = f->api->type(f->L, -1) == CO_LUA_TNUMBER ? (int)f->api->tonumber(f->L, -1) : 0;
    f->api->settop(f->L, AT(f, SLOTS));
    return last;
}

/* A new number for the value at index, kept in obj. */
static int Keep(const Frame* f, int index)
{
    const CoLuaApi* api = f->api;
    int n = ++g.last;
    api->pushvalue(f->L, index);
    api->rawseti(f->L, AT(f, SLOT_OBJ), n);
    api->pushnumber(f->L, n);
    api->setfield(f->L, AT(f, SLOT_TABLE), "last");
    return n;
}

/* The world at index: its number, the one it had if it was kept before. */
static int KeepWorld(const Frame* f, int index)
{
    const CoLuaApi* api = f->api;
    api->pushvalue(f->L, index);
    api->rawget(f->L, AT(f, SLOT_REV));
    int known = api->type(f->L, -1) == CO_LUA_TNUMBER ? (int)api->tonumber(f->L, -1) : 0;
    api->settop(f->L, -2);
    if (known != 0) return known;
    int n = Keep(f, index);
    api->pushvalue(f->L, index);
    api->pushnumber(f->L, n);
    api->rawset(f->L, AT(f, SLOT_REV));
    return n;
}

/* obj[n] onto the stack (nil when there is none). */
static void PushObject(const Frame* f, double n) { f->api->rawgeti(f->L, AT(f, SLOT_OBJ), (int)n); }

/* The GUI's item k onto the stack (nil when there is none), above the GUI's items table: the item's index. A call's
 * arguments start above it, so the table is not one of them. */
static int PushItem(const Frame* f, double gui, double k)
{
    f->api->rawgeti(f->L, AT(f, SLOT_ITEMS), (int)gui);
    if (f->api->type(f->L, -1) == CO_LUA_TTABLE) f->api->rawgeti(f->L, -1, (int)k);
    else f->api->pushnil(f->L);
    return f->api->gettop(f->L);
}

/* ---- one engine call ---------------------------------------------------------------------------------------------- */

/* The error on top as the call's message (once: the first that raised). */
static void Raised(Frame* f)
{
    if (!f->raised)
    {
        size_t length = 0;
        const char* text = f->api->tolstring(f->L, -1, &length);
        snprintf(f->call->error, sizeof f->call->error, "%.*s", text != NULL ? (int)(length < 191 ? length : 191) : 0,
            text != NULL ? text : "");
        if (f->call->error[0] == 0) snprintf(f->call->error, sizeof f->call->error, "an engine call raised");
    }
    f->raised = 1;
}

static void PushFunction(const Frame* f, int fn) { f->api->rawgeti(f->L, CO_LUA_REGISTRY, g.functions[fn]); }

/* Calls the function arguments + 1 below the top: results onto the stack, or 0 after Raised (the message on top). */
static int Call(Frame* f, int arguments, int results)
{
    if (f->api->pcall(f->L, arguments, results, 0) == 0) return 1;
    Raised(f);
    return 0;
}

/* A constructor's value onto the stack (its message there when it raised: the call is then not made). */
static void Construct(Frame* f, int fn, const double* values, int count)
{
    PushFunction(f, fn);
    for (int i = 0; i < count; i++) f->api->pushnumber(f->L, values[i]);
    (void)Call(f, count, 1);
}

static void Vector2(Frame* f, double x, double y) { Construct(f, F_VECTOR2, (const double[]){x, y}, 2); }
static void Vector3(Frame* f, double x, double y, double z) { Construct(f, F_VECTOR3, (const double[]){x, y, z}, 3); }

static void Colour(Frame* f)
{
    const CoGuiCall* c = f->call;
    Construct(f, F_COLOR, (const double[]){c->a, c->r, c->g, c->b}, 4);
}

static void Id(Frame* f, uint64_t id)
{
    char hex[17];
    snprintf(hex, sizeof hex, "%016llx", (unsigned long long)id);
    PushFunction(f, F_FROM_HEX);
    f->api->pushstring(f->L, hex);
    (void)Call(f, 1, 1);
}

static void Text(const Frame* f) { f->api->pushlstring(f->L, f->call->text != NULL ? f->call->text : "", f->call->length); }

/* The value at index is something (not nil or false). */
static int Some(const Frame* f, int index)
{
    int type = f->api->type(f->L, index);
    return type != CO_LUA_TNIL && !(type == CO_LUA_TBOOLEAN && !f->api->toboolean(f->L, index));
}

/* The call whose function and arguments are pushed above top: its results, or the result of the call. */
static int Finish(Frame* f, int top, int results)
{
    if (f->raised) return 0;
    return Call(f, f->api->gettop(f->L) - top - 1, results);
}

/* ---- the ops ------------------------------------------------------------------------------------------------------- */

static int MainWorld(Frame* f)
{
    int top = f->api->gettop(f->L);
    PushFunction(f, F_MAIN_WORLD);
    if (!Finish(f, top, 1)) return CO_GUI_RAISED;
    if (!Some(f, -1)) return 0;
    f->call->world = KeepWorld(f, f->api->gettop(f->L));
    return 1;
}

static int Worlds(Frame* f)
{
    int top = f->api->gettop(f->L);
    PushFunction(f, F_WORLDS);
    if (!Finish(f, top, 1)) return CO_GUI_RAISED;
    int list = f->api->gettop(f->L);
    if (f->api->type(f->L, list) != CO_LUA_TTABLE) return -1;
    int count = (int)f->api->objlen(f->L, list), capacity = f->call->capacity < CO_GUI_WORLDS ? f->call->capacity : CO_GUI_WORLDS;
    int n = 0;
    for (int i = 1; i <= count && n < capacity; i++)
    {
        f->api->rawgeti(f->L, list, i);
        f->call->refs[n++] = KeepWorld(f, list + 1);
        f->api->settop(f->L, list);
    }
    return n;
}

static int Create(Frame* f)
{
    const CoLuaApi* api = f->api;
    int top = api->gettop(f->L);
    PushFunction(f, F_CREATE);
    PushObject(f, f->call->world);
    if (!Some(f, -1)) return 0;
    api->pushstring(f->L, "scale");
    api->pushnumber(f->L, 1);
    api->pushnumber(f->L, 1);
    if (!Finish(f, top, 1)) return CO_GUI_RAISED;
    if (!Some(f, -1)) return 0;
    int gui = Keep(f, top + 1);
    api->createtable(f->L, 0, 0);
    api->rawseti(f->L, AT(f, SLOT_ITEMS), gui);
    api->pushnumber(f->L, f->call->world);
    api->rawseti(f->L, AT(f, SLOT_GUIS), gui);
    f->call->gui = gui;
    return 1;
}

/* Forgets GUI n and its items. */
static void Forget(const Frame* f, int n)
{
    const CoLuaApi* api = f->api;
    static const int kSlots[] = {SLOT_ITEMS, SLOT_GUIS, SLOT_OBJ};
    for (int i = 0; i < 3; i++)
    {
        api->pushnil(f->L);
        api->rawseti(f->L, AT(f, kSlots[i]), n);
    }
}

static int Destroy(Frame* f)
{
    int top = f->api->gettop(f->L);
    PushFunction(f, F_DESTROY);
    PushObject(f, f->call->world);
    PushObject(f, f->call->gui);
    int known = Some(f, top + 2) && Some(f, top + 3);
    Forget(f, (int)f->call->gui);
    if (known && !Finish(f, top, 0)) return CO_GUI_RAISED;
    return 1;
}

static int Material(Frame* f)
{
    int top = f->api->gettop(f->L);
    PushFunction(f, F_MATERIAL);
    PushObject(f, f->call->gui);
    if (!Some(f, -1)) return 0;
    Id(f, f->call->id[0]);
    if (!Finish(f, top, 1)) return CO_GUI_RAISED;
    int some = Some(f, -1);
    f->api->rawseti(f->L, AT(f, SLOT_OBJ), 0); /* the material last fetched, nil when the GUI gave none (as the Lua) */
    if (!some) return 0;
    f->call->item = 1;
    return 1;
}

/* Material.set_<what>(the material, IdString64(id[0]), the value the op takes). */
static int SetMaterial(Frame* f)
{
    const CoGuiCall* c = f->call;
    int top = f->api->gettop(f->L);
    int fn = c->op == CO_GUI_SET_SCALAR ? F_SET_SCALAR : c->op == CO_GUI_SET_VECTOR2 ? F_SET_VECTOR2
        : c->op == CO_GUI_SET_VECTOR4 ? F_SET_VECTOR4 : F_SET_TEXTURE;
    PushFunction(f, fn);
    PushObject(f, 0);
    Id(f, c->id[0]);
    if (fn == F_SET_SCALAR) f->api->pushnumber(f->L, c->value);
    else if (fn == F_SET_VECTOR2) Vector2(f, c->x, c->y);
    else if (fn == F_SET_VECTOR4) Colour(f);
    else Id(f, c->id[1]);
    return Finish(f, top, 0) ? 1 : CO_GUI_RAISED;
}

static int Resolution(Frame* f)
{
    int top = f->api->gettop(f->L);
    PushFunction(f, F_RESOLUTION);
    if (!Finish(f, top, 2)) return CO_GUI_RAISED;
    if (f->api->type(f->L, top + 1) != CO_LUA_TNUMBER || f->api->type(f->L, top + 2) != CO_LUA_TNUMBER) return 0;
    f->call->w = f->api->tonumber(f->L, top + 1);
    f->call->h = f->api->tonumber(f->L, top + 2);
    return 1;
}

/* The value on top, kept as the GUI's next item: its number in call->item. */
static int Add(Frame* f)
{
    const CoLuaApi* api = f->api;
    if (!Some(f, -1)) return 0;
    int value = api->gettop(f->L);
    api->rawgeti(f->L, AT(f, SLOT_ITEMS), (int)f->call->gui);
    if (api->type(f->L, -1) != CO_LUA_TTABLE) return 0;
    int k = (int)api->objlen(f->L, -1) + 1;
    api->pushvalue(f->L, value);
    api->rawseti(f->L, -2, k);
    f->call->item = k;
    return 1;
}

/* Vector3(x, y, z), Vector2(w, h), Color(a, r, g, b): a rectangle's place, size and colour. */
static void Box(Frame* f)
{
    const CoGuiCall* c = f->call;
    Vector3(f, c->x, c->y, c->z);
    Vector2(f, c->w, c->h);
    Colour(f);
}

/* text, IdString64(font), size, IdString64(material), Vector3(x, y, z), Color(a, r, g, b): a label. */
static void Label(Frame* f)
{
    const CoGuiCall* c = f->call;
    Text(f);
    Id(f, c->id[0]);
    f->api->pushnumber(f->L, c->size);
    Id(f, c->id[1]);
    Vector3(f, c->x, c->y, c->z);
    Colour(f);
}

/* Gui.rect or Gui.text (made: a new item), or Gui.update_rect or update_text (an item the GUI has). */
static int Primitive(Frame* f)
{
    const CoGuiCall* c = f->call;
    int rect = c->op == CO_GUI_RECT || c->op == CO_GUI_UPDATE_RECT, made = c->op == CO_GUI_RECT || c->op == CO_GUI_TEXT;
    int item = made ? 0 : PushItem(f, c->gui, c->item);
    if (!made && !Some(f, item)) return 0;
    int top = f->api->gettop(f->L);
    PushFunction(f, rect ? (made ? F_RECT : F_UPDATE_RECT) : (made ? F_TEXT : F_UPDATE_TEXT));
    PushObject(f, c->gui);
    if (!Some(f, -1)) return 0;
    if (!made) f->api->pushvalue(f->L, item);
    if (rect) Box(f);
    else Label(f);
    if (!Finish(f, top, made ? 1 : 0)) return CO_GUI_RAISED;
    return made ? Add(f) : 1;
}

static int Op(Frame* f)
{
    switch (f->call->op)
    {
    case CO_GUI_MAIN_WORLD: return MainWorld(f);
    case CO_GUI_LIST_WORLDS: return Worlds(f);
    case CO_GUI_CREATE: return Create(f);
    case CO_GUI_DESTROY: return Destroy(f);
    case CO_GUI_MATERIAL: return Material(f);
    case CO_GUI_SET_SCALAR: case CO_GUI_SET_VECTOR2: case CO_GUI_SET_VECTOR4: case CO_GUI_SET_TEXTURE: return SetMaterial(f);
    case CO_GUI_RESOLUTION: return Resolution(f);
    case CO_GUI_RECT: case CO_GUI_UPDATE_RECT: case CO_GUI_TEXT: case CO_GUI_UPDATE_TEXT: return Primitive(f);
    default:
        snprintf(f->call->error, sizeof f->call->error, "unknown op %d", (int)f->call->op);
        return CO_GUI_RAISED;
    }
}

/* ---- what an earlier start left -------------------------------------------------------------------------------------- */

/* The listed worlds as a set (world -> true) on top: 1, or 0 when there is no list (nothing is destroyed then). */
static int PushListed(Frame* f)
{
    const CoLuaApi* api = f->api;
    int top = api->gettop(f->L);
    PushFunction(f, F_WORLDS);
    if (!Call(f, 0, 1) || api->type(f->L, -1) != CO_LUA_TTABLE) { api->settop(f->L, top); return 0; }
    int list = api->gettop(f->L), count = (int)api->objlen(f->L, list);
    api->createtable(f->L, 0, count);
    for (int i = 1; i <= count; i++)
    {
        api->rawgeti(f->L, list, i);
        api->pushboolean(f->L, 1);
        api->rawset(f->L, list + 1);
    }
    return 1;
}

/* GUI n of the table in the slots, destroyed while its world is in the set at listed: 1 when it was. */
static int DestroyLeft(Frame* f, int n, int listed)
{
    const CoLuaApi* api = f->api;
    int top = api->gettop(f->L);
    api->rawgeti(f->L, AT(f, SLOT_GUIS), n);
    if (api->type(f->L, -1) != CO_LUA_TNUMBER) { api->settop(f->L, top); return 0; }
    double world = api->tonumber(f->L, -1);
    PushFunction(f, F_DESTROY);
    PushObject(f, world);
    PushObject(f, n);
    api->pushvalue(f->L, top + 3);
    api->rawget(f->L, listed);
    int alive = Some(f, -1) && Some(f, top + 4);
    api->settop(f->L, top + 4);
    int destroyed = alive && Call(f, 2, 0);
    api->settop(f->L, top);
    return destroyed;
}

/* The first call of this DLL: the GUIs the table holds destroyed (while their worlds live), a new table. */
static void Sweep(Frame* f)
{
    g.fresh = 0;
    int last = Last(f), destroyed = 0;
    if (last > 0 && PushListed(f))
    {
        int listed = f->api->gettop(f->L);
        for (int n = 1; n <= last; n++) destroyed += DestroyLeft(f, n, listed);
    }
    f->raised = 0;
    f->call->error[0] = 0;
    f->api->settop(f->L, f->base);
    NewTable(f);
    f->api->settop(f->L, f->base);
    if (destroyed > 0) co_log("war-table tag: %d GUI(s) an earlier start left were destroyed", destroyed);
}

/* The sweep at the first call: the stack as it was. */
static void Settle(Frame* f)
{
    if (!g.fresh) return;
    if (PushSlots(f)) Sweep(f);
    else
    {
        f->api->settop(f->L, f->base);
        NewTable(f);
        g.fresh = 0;
    }
    f->api->settop(f->L, f->base);
}

/* lint: per-frame */
int co_gui_call(CoGuiCall* call)
{
    call->error[0] = 0;
    if (!co_gui_ready())
    {
        snprintf(call->error, sizeof call->error, "%s", Unavailable() ? Unavailable() : "no game Lua");
        return CO_GUI_RAISED;
    }
    Frame f = {co_lua(), co_lua_state(), 0, 0, call};
    f.base = f.api->gettop(f.L);
    Settle(&f);
    int result = CO_GUI_RAISED;
    if (PushSlots(&f)) result = Op(&f);
    else snprintf(call->error, sizeof call->error, "the engine Gui's table in the Lua registry is not one");
    f.api->settop(f.L, f.base);
    return result;
}

#ifdef CO_TEST
void co_gui_test_reset(void)
{
    memset(&g, 0, sizeof g);
    g.fresh = 1;
}
#endif
