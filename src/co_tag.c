/* The war-table tag: whether the highlighted level-11 operation, or the picked mission, starts as a complex operation,
 * drawn beside the native panel in the panel's own style. Know Your Constellation's method (co_lua.h): the native
 * panel's rectangle and body font read from the game, the banner drawn with the engine's Gui from the lua_pcall hook.
 *
 * The state is the launch's: co_complex_decision on the entry seeds the launch reads, and on the briefing the host's
 * launch-clear prediction (PROOFS.md W-2). Nothing in the game is written. Per frame on any other screen: one read of
 * the UI's screen stack. On the war table or the briefing: the reads below, and Gui calls only when what is shown
 * changes (PROOFS.md W-4). */
#include <stdio.h>
#include <string.h>

#include "co.h"
#include "co_lua.h"
#include "co_tag.h"
#include "xl/co_normal.h"

#define UI_MANAGER 0x347CE28u   /* the UI manager: its screen stack at +0x429C, the count at +0x42B0 (0xADF3A0) */
#define UI_STACK 0x429Cu
#define SCREEN_MAP 15u
#define SCREEN_BRIEFING 14u
#define MENU 0x3326E68u         /* the menu manager: the native panels' event registries */
#define MAP_REGISTRY 25224u     /* one inline subscriber {count, _, owner, kind} */
#define BRIEFING_REGISTRY 25272u
#define MAP_KIND 226u
#define BRIEFING_KIND 229u
#define MAP_FRAME 349072u       /* the owner's operation frame; the briefing owner's description frame */
#define BRIEFING_FRAME 31232u
#define FONT 0x3772268u         /* the active locale's body face; its normal material's hash at [0x37C5478] + 24; its atlas */
#define MATERIAL 0x37C5478u
#define ATLAS 0x3772EE8u
#define BOARD 0xF7280u          /* in the war object: 110 operation records of 0x5C, in use when +0x34 is not 0 */
#define BOARD_SLOTS 110u
#define TABLE 0xF9A10u          /* the mission table: entries of 0x4C, their count at +0xFFC08 */
#define TABLE_COUNT 0xFFC08u
#define TABLE_ENTRIES 0x14Au
#define SELECTED_OPERATION 0x17A2A0u
#define SELECTED_KIND 0x17A298u /* the picked mission: kind (below 0 none), placement, difficulty (0x12D2040) */
#define SELECTED_PLACEMENT 0x17A2A4u
#define SELECTED_DIFFICULTY 0x17A2A8u

/* The game's own readers of what the tag reads, as build 25480438 has them (PROOFS.md W-1). */
static const struct { uint32_t rva; uint8_t size; uint8_t bytes[48]; } kPins[] = {
    {0x12D0CD0u, 34, {0x8B, 0x81, 0xA0, 0xA2, 0x17, 0x00, 0x83, 0xF8, 0x6E, 0x73, 0x14, 0x48, 0x6B, 0xD0, 0x5C, 0x48, 0x8D, 0x81,
        0x80, 0x72, 0x0F, 0x00, 0x48, 0x03, 0xC2, 0x80, 0x78, 0x34, 0x00, 0x75, 0x02, 0x33, 0xC0, 0xC3}},
    {0x12D2040u, 31, {0x83, 0xB9, 0x98, 0xA2, 0x17, 0x00, 0x00, 0x7C, 0x16, 0x8B, 0x91, 0xA4, 0xA2, 0x17, 0x00, 0x85, 0xD2, 0x78,
        0x0C, 0x44, 0x8B, 0x81, 0xA8, 0xA2, 0x17, 0x00, 0xE9, 0x31, 0xFF, 0xFF, 0xFF}},
    {0x12D1F90u, 48, {0x44, 0x8B, 0x91, 0x08, 0xFC, 0x0F, 0x00, 0x45, 0x33, 0xC9, 0x4C, 0x8B, 0xD9, 0x45, 0x85, 0xD2, 0x74, 0x24,
        0x48, 0x8D, 0x81, 0x4C, 0x9A, 0x0F, 0x00, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x39, 0x50, 0xF0, 0x75, 0x05, 0x44,
        0x39, 0x00, 0x74, 0x12, 0x41, 0xFF, 0xC1, 0x48, 0x83, 0xC0}},
    {0xADF3A0u, 48, {0x48, 0x8B, 0x0D, 0x81, 0xDA, 0x99, 0x02, 0x48, 0x81, 0xC1, 0x88, 0x42, 0x00, 0x00, 0x8B, 0x41, 0x28, 0x83,
        0xE8, 0x01, 0x48, 0x98, 0x78, 0x17, 0x48, 0x8D, 0x0C, 0x81, 0x48, 0x83, 0xC1, 0x14, 0x83, 0x39, 0x0E, 0x74, 0x0A, 0x48,
        0x83, 0xE9, 0x04, 0x48, 0x83, 0xE8, 0x01, 0x79, 0xF1, 0xC3}},
};

/* ---- the state ---------------------------------------------------------------------------------------------------- */

int co_tag_state(const CoTagInputs* in, int* why)
{
    *why = CO_TAG_WHY_NONE;
    if (!in->level11) return CO_TAG_NONE;
    const int decision = co_complex_decision(in->is_host, in->own_on, in->host_row, co_seed_marked(0, in->marked));
    if (decision == CO_MISSION_COMPLEX) return in->briefing && in->is_host && in->clears == 1 ? CO_TAG_CLEARED : CO_TAG_COMPLEX;
    if (decision == CO_MISSION_LEAVE && in->host_row == CO_HOST_ROW_NONE) return CO_TAG_WAITING;
    /* the part is on where the decision is made: the host's own, or both the host's row and this machine's */
    if (decision == CO_MISSION_NORMAL && in->own_on && (in->is_host || in->host_row == CO_HOST_ROW_ON)) return CO_TAG_STANDARD;
    *why = !in->is_host && in->host_row == CO_HOST_ROW_OFF ? CO_TAG_WHY_HOST : in->build ? CO_TAG_WHY_BUILD : CO_TAG_WHY_PC;
    return CO_TAG_OFF;
}

/* ---- the readers -------------------------------------------------------------------------------------------------- */

static uint8_t g_table[TABLE_ENTRIES * 0x4Cu];
static uint32_t U32(const uint8_t* b) { uint32_t v; memcpy(&v, b, 4); return v; }

/* The table's entries, all `count` of them: 0 when the count is past the table or a read failed. */
static int Entries(uintptr_t war, uint32_t* count)
{
    if (g_coApi->read(war + TABLE_COUNT, count, 4) != 0 || *count > TABLE_ENTRIES) return 0;
    return *count == 0 || g_coApi->read(war + TABLE, g_table, (size_t)*count * 0x4Cu) == 0;
}

int co_tag_map_entries(uintptr_t war, uint32_t* index, uint32_t* entry, int* level11, int* marked, int* found)
{
    uint8_t used = 0;
    uint32_t count = 0;
    *found = *level11 = *marked = 0;
    *entry = 0xFFFFFFFFu;
    if (g_coApi->read(war + SELECTED_OPERATION, index, 4) != 0) return 0;
    if (*index >= BOARD_SLOTS) return 1; /* 0xFFFFFFFF: none highlighted */
    if (g_coApi->read(war + BOARD + *index * 0x5Cu + 0x34u, &used, 1) != 0) return 0;
    if (used == 0) return 1;
    if (!Entries(war, &count)) return 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t* e = g_table + i * 0x4Cu;
        if (U32(e + 0x28) != *index) continue;
        *found = 1;
        if (U32(e + 0x3C) != CO_COMPLEX_LEVEL) continue;
        if (!*level11) *entry = i;
        *level11 = 1;
        *marked |= co_seed_is_complex(U32(e + 0x34));
    }
    return 1;
}

int co_tag_briefing_entry(uintptr_t war, uint32_t* index, uint32_t* entry, int* level11, int* marked, int* found)
{
    int32_t kind = -1, placement = -1;
    uint32_t difficulty = 0, count = 0;
    *found = *level11 = *marked = 0;
    *index = *entry = 0xFFFFFFFFu;
    if (g_coApi->read(war + SELECTED_KIND, &kind, 4) != 0 || g_coApi->read(war + SELECTED_PLACEMENT, &placement, 4) != 0 ||
        g_coApi->read(war + SELECTED_DIFFICULTY, &difficulty, 4) != 0)
        return 0;
    if (kind < 0 || placement < 0) return 1;
    if (!Entries(war, &count)) return 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t* e = g_table + i * 0x4Cu;
        if (U32(e + 0x2C) != (uint32_t)placement || U32(e + 0x3C) != difficulty) continue;
        *found = 1; /* the first match, as the launch's own search takes it */
        *index = U32(e + 0x28);
        *entry = i;
        *level11 = difficulty == CO_COMPLEX_LEVEL;
        *marked = co_seed_is_complex(U32(e + 0x34));
        return 1;
    }
    return 1;
}

/* ---- the layout --------------------------------------------------------------------------------------------------- */

int co_tag_place(const CoTagAnchor* a, double width, double height, CoTagBox* out)
{
    const double s = a->scale, h = CO_TAG_HEIGHT * s, side = CO_TAG_SIDE * s;
    /* right of the panel, above its top line: no place Know Your Constellation's box can take reaches up there */
    *out = (CoTagBox){a->x + a->w + side, a->y + a->h + CO_TAG_RAISE * s, a->w, h};
    if (out->x + out->w <= width - side && out->y + out->h <= height - side) return CO_TAG_PLACE_BESIDE;
    /* else past the column Know Your Constellation's box takes beside the panel, level with the panel's top */
    const double top = height - side < a->y + a->h ? height - side : a->y + a->h;
    *out = (CoTagBox){a->x + 2 * (a->w + side), top - h > side ? top - h : side, a->w, h};
    if (out->x + out->w <= width - side && out->y + out->h <= height - side) return CO_TAG_PLACE_COLUMN;
    return CO_TAG_PLACE_NONE;
}

/* The native panel's inks and frame, sampled from a briefing panel (PROOFS.md W-6), and Know Your Constellation's
 * calibrated dark body and slate. */
static const CoTagColour kShade = {204, 0, 0, 0}, kBody = {255, 6, 10, 17}, kSlate = {255, 42, 53, 69};
static const CoTagColour kYellow = {255, 255, 233, 0}, kWhite = {255, 255, 255, 255};
static const CoTagColour kLabel = {255, 181, 217, 233}, kGrey = {255, 203, 204, 206};

const char* co_tag_title(int state)
{
    switch (state)
    {
    case CO_TAG_COMPLEX: return "COMPLEX OPERATION";
    case CO_TAG_STANDARD: return "STANDARD OPERATION";
    case CO_TAG_OFF: return "COMPLEX OPERATIONS OFF";
    case CO_TAG_WAITING: return "WAITING FOR HOST";
    case CO_TAG_CLEARED: return "STARTS AS STANDARD";
    default: return "";
    }
}

const char* co_tag_caption(int state, int why)
{
    static char complex[48];
    if (state == CO_TAG_COMPLEX)
    {
        /* the border's diameter by HellForge's rule for a larger map (co_normal.h), to the nearest kilometre */
        const unsigned km = (unsigned)((2.0 * (389.12 * CO_COMPLEX_MAP_SCALE + 174.0) + 500.0) / 1000.0);
        snprintf(complex, sizeof complex, "%u MISSIONS ON ONE %u KM MAP", CO_COMPLEX_MISSIONS, km);
        return complex;
    }
    if (state == CO_TAG_STANDARD) return "EACH MISSION ON ITS OWN MAP";
    if (state == CO_TAG_WAITING) return "NO ANSWER FROM THE HOST YET";
    if (state == CO_TAG_CLEARED) return "A SQUAD MEMBER CANNOT PLAY IT";
    if (state != CO_TAG_OFF) return "";
    return why == CO_TAG_WHY_HOST ? "OFF ON THE HOST" : why == CO_TAG_WHY_BUILD ? "NOT THIS GAME BUILD" : "OFF ON THIS PC";
}

void co_tag_plan(int state, int why, const CoTagBox* b, double s, CoTagPlan* p)
{
    const double i = CO_TAG_INSET * s, k = CO_TAG_BORDER * s, top = b->y + b->h, x = b->x + CO_TAG_PAD * s;
    const double content = top - (CO_TAG_BORDER + CO_TAG_INSET + CO_TAG_EDGE) * s;
    const int loud = state == CO_TAG_COMPLEX;
    const CoTagColour edge = loud ? kYellow : kSlate;
    p->rect[0] = (CoTagRect){b->x, b->y, b->w, b->h, CO_TAG_LAYER_SHADE, kShade};
    p->rect[1] = (CoTagRect){b->x + i, b->y + i, b->w - 2 * i, b->h - 2 * i, CO_TAG_LAYER_BODY, kBody};
    p->rect[2] = (CoTagRect){b->x, b->y, b->w, k, CO_TAG_LAYER_BORDER, edge};
    p->rect[3] = (CoTagRect){b->x, top - k, b->w, k, CO_TAG_LAYER_BORDER, edge};
    p->rect[4] = (CoTagRect){b->x, b->y, k, b->h, CO_TAG_LAYER_BORDER, edge};
    p->rect[5] = (CoTagRect){b->x + b->w - k, b->y, k, b->h, CO_TAG_LAYER_BORDER, edge};
    /* baselines as Know Your Constellation's heading: the title one size below the content top, the caption 1.3 below */
    p->text[0] = (CoTagText){co_tag_title(state), CO_TAG_TITLE * s, x, content - CO_TAG_TITLE * s, CO_TAG_LAYER_TEXT, loud ? kYellow : kLabel};
    p->text[1] = (CoTagText){co_tag_caption(state, why), CO_TAG_CAPTION * s, x, content - (CO_TAG_TITLE * 1.3 + CO_TAG_CAPTION) * s,
        CO_TAG_LAYER_TEXT, loud ? kWhite : kGrey};
}

/* ---- the game side ------------------------------------------------------------------------------------------------- */

/* The font material's slots the native normal-font setup sets (0x104CDF0), as Know Your Constellation sets them. */
static const uint64_t kScalarSlots[4] = {0x8035C26600000000ull, 0x5E8455FE00000000ull, 0x309E778300000000ull, 0x82B803A800000000ull};
#define RANGE_SLOT 0xE13777CE00000000ull
#define SHADOW_SLOT 0x7701209E00000000ull
#define ATLAS_SLOT 0x88BAC99B00000000ull

typedef struct Key { int screen, state, why, refused; uint32_t index; CoTagAnchor a; } Key;

static struct
{
    uintptr_t game;
    double gui, world;
    uint64_t font, material, atlas;
    int items, visible;       /* the GUI's six rectangles and two texts exist; they show */
    double rect[CO_TAG_RECTS], text[2];
    int has;                  /* what was drawn or logged last */
    Key key;
    CoTagBox box;
    int place;
    int drawn, unlogged, stable;
    int errors;
} t;

static uint64_t Pointer(uintptr_t at) { uint64_t p = 0; return g_coApi->read(at, &p, 8) == 0 ? p : 0; }

static void Error(const char* what, const char* detail)
{
    if (t.errors++ < 4) co_log("war-table tag: %s%s%s; no tag until it changes", what, detail[0] ? ": " : "", detail);
}

static int Screen(void)
{
    uint8_t stack[24];
    const uint64_t manager = Pointer(t.game + UI_MANAGER);
    if (manager == 0 || g_coApi->read((uintptr_t)manager + UI_STACK, stack, sizeof stack) != 0) return CO_TAG_SCREEN_NONE;
    const uint32_t n = U32(stack + 20);
    if (n < 1 || n > 5) return CO_TAG_SCREEN_NONE;
    const uint32_t top = U32(stack + 4 * (n - 1));
    return top == SCREEN_MAP ? CO_TAG_SCREEN_MAP : top == SCREEN_BRIEFING ? CO_TAG_SCREEN_BRIEFING : CO_TAG_SCREEN_NONE;
}

static float F32(const uint8_t* b) { float v; memcpy(&v, b, 4); return v; }
static int Out(double v) { return v != v || v < 0 || v > 32768; }

/* The native panel's solved rectangle (y up) while it is fully shown, and the body font: 1, or 0 not now. */
static int Anchor(int screen, CoTagAnchor* a)
{
    uint8_t entry[24], widget[164];
    uint32_t tab = 1;
    const uint64_t menu = Pointer(t.game + MENU);
    if (menu == 0 || g_coApi->read((uintptr_t)menu + (screen == CO_TAG_SCREEN_MAP ? MAP_REGISTRY : BRIEFING_REGISTRY), entry, sizeof entry) != 0) return 0;
    if (U32(entry) != 1 || U32(entry + 16) != (screen == CO_TAG_SCREEN_MAP ? MAP_KIND : BRIEFING_KIND)) return 0;
    uint64_t owner;
    memcpy(&owner, entry + 8, 8);
    if (owner < 0x10000u || owner >= 0x800000000000ull) return 0;
    if (screen == CO_TAG_SCREEN_BRIEFING && (g_coApi->read((uintptr_t)owner + 8, &tab, 4) != 0 || tab != 0)) return 0; /* the loadout tab */
    if (g_coApi->read((uintptr_t)owner + (screen == CO_TAG_SCREEN_MAP ? MAP_FRAME : BRIEFING_FRAME), widget, sizeof widget) != 0) return 0;
    const double opacity = F32(widget + 84), sx = F32(widget + 100), sy = F32(widget + 140);
    if ((U32(widget) & 0x10u) == 0 || !(opacity >= 0.995 && opacity <= 1.01)) return 0;
    *a = (CoTagAnchor){F32(widget + 148), F32(widget + 156), F32(widget + 36) * sx, F32(widget + 40) * sy, sx, 0, 0, 0};
    if (Out(a->x) || Out(a->y) || Out(a->w) || Out(a->h) || Out(sx) || sx < 0.3 || sx > 4 || (sx - sy > 0.01 || sy - sx > 0.01) || a->w < 200 || a->h < 40)
        return 0;
    const uint64_t material = Pointer(t.game + MATERIAL);
    a->font = Pointer(t.game + FONT);
    a->atlas = Pointer(t.game + ATLAS);
    a->material = material != 0 ? Pointer((uintptr_t)material + 24) : 0;
    return a->font != 0 && a->material != 0 && a->atlas != 0;
}

static int Call(CoGuiCall* c)
{
    const int r = co_gui_call(c);
    if (r == CO_GUI_RAISED) Error("the engine's Gui raised", c->error);
    return r;
}

static void Destroy(void)
{
    CoGuiCall c = {.op = CO_GUI_LIST_WORLDS, .capacity = CO_GUI_WORLDS};
    if (t.gui != 0)
    {
        int n = Call(&c), listed = 0;
        for (int i = 0; i < n; i++) listed |= c.refs[i] == t.world;
        if (listed) { CoGuiCall d = {.op = CO_GUI_DESTROY, .world = t.world, .gui = t.gui}; Call(&d); }
    }
    t.gui = t.world = 0;
    t.items = t.visible = 0;
}

/* The GUI in the UI world (the first world that is not the main one), its material set as the native normal font's. */
static int Prepare(const CoTagAnchor* a)
{
    CoGuiCall c = {.op = CO_GUI_MAIN_WORLD}, l = {.op = CO_GUI_LIST_WORLDS, .capacity = CO_GUI_WORLDS};
    if (Call(&c) != 1) return 0;
    const int n = Call(&l);
    double target = 0;
    for (int i = 0; i < n && target == 0; i++) if (l.refs[i] != c.world) target = l.refs[i];
    if (target == 0) return 0;
    if (t.gui != 0 && t.world == target && t.font == a->font && t.material == a->material && t.atlas == a->atlas) return 1;
    Destroy();
    CoGuiCall g = {.op = CO_GUI_CREATE, .world = target};
    if (Call(&g) != 1) return 0;
    t.gui = g.gui;
    t.world = target;
    CoGuiCall m = {.op = CO_GUI_MATERIAL, .gui = t.gui, .id = {a->material}};
    if (Call(&m) != 1) return 0;
    for (int i = 0; i < 4; i++) { CoGuiCall v = {.op = CO_GUI_SET_SCALAR, .id = {kScalarSlots[i]}}; if (Call(&v) != 1) return 0; }
    CoGuiCall r = {.op = CO_GUI_SET_VECTOR2, .id = {RANGE_SLOT}, .x = 1, .y = -1}, sh = {.op = CO_GUI_SET_VECTOR4, .id = {SHADOW_SLOT}};
    CoGuiCall at = {.op = CO_GUI_SET_TEXTURE, .id = {ATLAS_SLOT, a->atlas}};
    if (Call(&r) != 1 || Call(&sh) != 1 || Call(&at) != 1) return 0;
    t.font = a->font;
    t.material = a->material;
    t.atlas = a->atlas;
    return 1;
}

/* The plan's items: made the first time, updated after (the ids stay). */
static int Items(const CoTagPlan* p, const CoTagAnchor* a)
{
    for (int i = 0; i < CO_TAG_RECTS; i++)
    {
        const CoTagRect* r = &p->rect[i];
        CoGuiCall c = {.op = t.items ? CO_GUI_UPDATE_RECT : CO_GUI_RECT, .gui = t.gui, .item = t.rect[i], .x = r->x, .y = r->y, .z = r->z,
            .w = r->w, .h = r->h, .a = r->c.a, .r = r->c.r, .g = r->c.g, .b = r->c.b};
        if (Call(&c) != 1) return 0;
        if (!t.items) t.rect[i] = c.item;
    }
    for (int i = 0; i < 2; i++)
    {
        const CoTagText* x = &p->text[i];
        CoGuiCall c = {.op = t.items ? CO_GUI_UPDATE_TEXT : CO_GUI_TEXT, .gui = t.gui, .item = t.text[i], .x = x->x, .y = x->y, .z = x->z,
            .size = x->size, .a = x->c.a, .r = x->c.r, .g = x->c.g, .b = x->c.b, .text = x->text, .length = (uint32_t)strlen(x->text),
            .id = {a->font, a->material}};
        if (Call(&c) != 1) return 0;
        if (!t.items) t.text[i] = c.item;
    }
    t.items = t.visible = 1;
    return 1;
}

/* Every item made empty, the GUI kept for the next banner on this screen. */
static void Hide(void)
{
    if (!t.items || !t.visible) return;
    for (int i = 0; i < CO_TAG_RECTS; i++) { CoGuiCall c = {.op = CO_GUI_UPDATE_RECT, .gui = t.gui, .item = t.rect[i]}; Call(&c); }
    for (int i = 0; i < 2; i++)
    {
        CoGuiCall c = {.op = CO_GUI_UPDATE_TEXT, .gui = t.gui, .item = t.text[i], .text = "", .id = {t.font, t.material}, .size = CO_TAG_CAPTION};
        Call(&c);
    }
    t.visible = 0;
}

static const char* kStates[] = {"none", "complex", "standard", "off", "waiting for the host", "complex, cleared at launch"};
static const char* kWhy[] = {"", " (host)", " (this PC)", " (game build)"};

static void Log(const Key* k, int drawn)
{
    const char* screen = k->screen == CO_TAG_SCREEN_MAP ? "map, operation" : "briefing, operation";
    if (k->state == CO_TAG_NONE) { co_log("war-table tag: %s %d: not level 11, no banner", screen, (int)k->index); return; }
    if (!drawn) { co_log("war-table tag: %s %d: %s%s, no place for the banner", screen, (int)k->index, kStates[k->state], kWhy[k->why]); return; }
    co_log("war-table tag: %s %d: %s%s, banner x %.1f y %.1f w %.1f h %.1f (%s; panel x %.1f y %.1f w %.1f h %.1f, scale %.4f)",
        screen, (int)k->index, kStates[k->state], kWhy[k->why], t.box.x, t.box.y, t.box.w, t.box.h,
        t.place == CO_TAG_PLACE_BESIDE ? "beside the panel" : "second column", k->a.x, k->a.y, k->a.w, k->a.h, k->a.scale);
}

/* What the screen shows now: 1 with the key, 0 when nothing is to be drawn. */
static int Look(int screen, Key* k)
{
    uint32_t entry = 0;
    int level11 = 0, marked = 0, found = 0;
    const uint64_t war = Pointer(t.game + CO_WAR_OBJECT);
    memset(k, 0, sizeof *k);
    k->screen = screen;
    if (war == 0) return 0;
    const int read = screen == CO_TAG_SCREEN_MAP ? co_tag_map_entries((uintptr_t)war, &k->index, &entry, &level11, &marked, &found)
                                                 : co_tag_briefing_entry((uintptr_t)war, &k->index, &entry, &level11, &marked, &found);
    if (!read) { k->refused = 1; return 0; }
    if (!found) return 0;
    const int host = co_peers_is_host();
    CoTagInputs in = {level11, marked, host, co_complex_on(), co_peers_host_row(), screen == CO_TAG_SCREEN_BRIEFING, 0,
        co_complex_refused_build(co_capacity_needs())};
    if (in.briefing && level11 && host) in.clears = co_launch_would_clear();
    k->state = co_tag_state(&in, &k->why);
    return k->state == CO_TAG_NONE || Anchor(screen, &k->a);
}

/* Once per frame, inside the lua_pcall hook (co_lua.c). A state is logged once it has held for CO_TAG_SETTLE frames, so
 * a panel that slides in logs where it came to rest. lint: per-frame */
static void Frame(lua_State* L)
{
    (void)L;
    const int screen = Screen();
    Key k;
    if (screen == CO_TAG_SCREEN_NONE)
    {
        if (t.gui != 0) { Destroy(); co_log("war-table tag: removed (the screen changed)"); }
        t.has = 0;
        return;
    }
    const int shown = Look(screen, &k);
    if (t.has && memcmp(&k, &t.key, sizeof k) == 0)
    {
        if (t.unlogged && ++t.stable >= CO_TAG_SETTLE) { Log(&t.key, t.drawn); t.unlogged = 0; }
        return;
    }
    t.key = k;
    t.has = 1;
    t.stable = t.drawn = 0;
    t.unlogged = shown;
    if (k.refused) Error("the mission table's count is past its entries, or it could not be read", "");
    if (!shown || k.state == CO_TAG_NONE) { Hide(); return; }
    CoGuiCall size = {.op = CO_GUI_RESOLUTION};
    CoTagPlan plan;
    if (!co_gui_ready() || !Prepare(&k.a) || Call(&size) != 1) { Hide(); t.unlogged = 0; return; }
    t.place = co_tag_place(&k.a, size.w, size.h, &t.box);
    if (t.place == CO_TAG_PLACE_NONE) { Hide(); return; }
    co_tag_plan(k.state, k.why, &t.box, k.a.scale, &plan);
    if (!Items(&plan, &k.a)) { Destroy(); t.unlogged = 0; return; }
    t.drawn = 1;
}

void co_tag_tick(void) { if (t.game != 0) co_lua_tick(); }

const char* co_tag_install(uintptr_t game)
{
    uint8_t now[48];
    for (size_t i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
        if (g_coApi->read_code(game + kPins[i].rva, now, kPins[i].size) != 0 || memcmp(now, kPins[i].bytes, kPins[i].size) != 0)
            return "a reader of the war table is not the game's code this mod was made for";
    memset(&t, 0, sizeof t);
    const char* why = co_lua_install(Frame);
    if (why != NULL) return why;
    t.game = game;
    return NULL;
}

#ifdef CO_TEST
void co_tag_test_reset(void) { memset(&t, 0, sizeof t); }
void co_tag_test_pins(uint8_t* image)
{
    for (size_t i = 0; i < sizeof kPins / sizeof kPins[0]; i++) memcpy(image + kPins[i].rva, kPins[i].bytes, kPins[i].size);
}
const CoTagShown* co_tag_test_shown(void)
{
    static CoTagShown s;
    s = (CoTagShown){t.key.state, t.key.why, t.key.screen, t.place, t.visible, t.gui, t.world, t.box, t.key.index};
    return &s;
}
#endif
