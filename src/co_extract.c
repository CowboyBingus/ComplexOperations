/* Extraction for eight: the Pelican's seats 8 and 9, and the boosters of all eight players. The caves are in
 * scripts/sites/extract_sites.py; co_counts.c writes them with the counts and calls in here.
 *
 * Seats 8 and 9 attach to two scene-graph nodes the game's Pelican unit does not have; the mod's recipe data adds them.
 * A missing node makes node() -1, which the attach code stores unchecked, so the seats exist only once the mod has seen
 * both nodes on a Pelican's own unit, through the engine's has_node, the function the game itself asks before attaching
 * (0x119FDAD). Until then the caves hand out the game's six passenger seats. A peer's message can still seat someone in
 * 8 or 9, so each attach asks again for its own unit (co_seat_node). */
#include <string.h>

#include "co.h"

/* The seat collections, as their per-frame pass 0x633E80 reads them: +0x10 the count, +0x38 the owner's entity record
 * by collection (+8 its id), +0x48 records of 0x64 with the seat type first. */
#define CO_SEAT_COLLECTIONS 0x3326D88u
#define CO_COLLECTION_COUNT 0x10u
#define CO_COLLECTION_OWNERS 0x38u
#define CO_COLLECTION_RECORDS 0x48u
#define CO_COLLECTION_RECORD 0x64u
#define CO_PELICAN_TYPE 40u
/* An entity id's record (+0xC its unit): the way 0x119F910 finds the vehicle whose node 0x119FC60 attaches to. */
#define CO_ENTITY_RECORD 0xFD9D40u
#define CO_ENTITY_UNIT 0xCu
static const char kEntityPin[] = "48 83 EC 08 3B 0D 02 9F 4A 02 0F 84 9B 00 00 00";
/* The event handler 0x1191520 that NL3 and NL6 sit in: esi the seat, rdi the vehicle's entity record (0x119F910 finds it
 * from the seater's, and the attach 0x119FC60 reads its unit at +0xC). */
#define CO_SEAT_EVENTS 0x1191531u
static const char kSeatEventsPin[] = "41 8B F0 8B EA E8 05 88 E4 FF 48 8B C8 48 8B D8 E8 CA E3 00 00 48 8B F8";
/* The engine's unit functions, from its script table [game+0x3326308]+0x18: has_node searches the unit's scene graph
 * (+0x10 count, +0x40 name hashes) as node() does for the attach; alive and "is of resource" check the unit first,
 * because has_node does not. */
#define CO_SCRIPT_API 0x3326308u
#define CO_UNIT_API 0x18u
#define CO_HAS_NODE 0x6D0u
#define CO_ALIVE 0x720u
#define CO_IS_RESOURCE 0x730u
static const char kHasNodePin[] = "40 53 48 83 EC 20 8B DA E8 23 F3 E9 FF 48 8B C8 4C 8B 00 41 FF 90 E8 00 00 00 33 C9 44 8B 40 10";
static const char kAlivePin[] = "48 83 EC 28 E8 07 EA E9 FF 48 85 C0 0F 95 C0 48 83 C4 28 C3";
static const char kIsPin[] = "40 53 48 83 EC 20 48 8B DA E8 A2 E9 E9 FF 48 8B 48 30 33 C0 48 39 19 0F 94 C0";
#define CO_GUNSHIP 0xEF3A4136B21592CBull  /* content/fac_helldivers/vehicles/shuttle_gunship/shuttle_gunship */
#define CO_NODE_LEFT_4 0xFC07BFB6u        /* attach_passenger_left_4 */
#define CO_NODE_RIGHT_4 0x27393B02u       /* attach_passenger_right_4 */
#define CO_NODE_LEFT_3 0xDD1461AEu        /* attach_passenger_left_3, seat 2's: the game attaches to it on every type-40 unit */
#define CO_NODE_RIGHT_3 0x6AD680F5u       /* attach_passenger_right_3, seat 5's */

/* The booster update 0x8568A0: loadout entries L = [0x347CE50] (count +0x2D200, 0x1690 each; +0x28 clear and +0x2C set
 * for a player's, booster id +0x168) and the booster table (21 of 0x38: id, status effect +4, package +0x34). */
#define CO_LOADOUTS 0x347CE50u
#define CO_LOADOUT_COUNT 0x2D200u
#define CO_LOADOUT 0x1690u
#define CO_BOOSTER_TABLE 0x32FDA90u
#define CO_BOOSTER 0x38u
#define CO_BOOSTERS 0x15u
#define CO_BOOSTERS_NOW 0x14u               /* in B: the four indexes loaded last */
#define CO_PACKAGES 0x347CEB0u
#define CO_ENGINE 0x3326340u
#define CO_ENGINE_MODE 0xAC21Cu             /* 4 in a mission */
#define CO_PLAYERS_GLOBAL 0x3326468u
#define CO_NO_PLAYER 0x3483C34u
#define CO_EFFECTS 0x37CB600u
/* The callees, as 0x8568A0 calls them. co_counts.c first checks their bytes and the body's (kCoBoosterPins). */
#define CO_FIND_PACKAGE 0x12689C0u
#define CO_LOAD 0x12FE350u
#define CO_UNLOAD 0x12FE4E0u
#define CO_HAS_EFFECT 0x670E30u
#define CO_ADD_EFFECT 0x66F190u
#define CO_REMOVE_EFFECT 0x66F4C0u

typedef uintptr_t (*EntityFn)(uint32_t);
typedef uint8_t (*HasNodeFn)(uint32_t, uint32_t);
typedef uint8_t (*AliveFn)(uint32_t);
typedef uint8_t (*IsFn)(uint32_t, uint64_t);
typedef uint64_t* (*FindPackageFn)(uint64_t*, uint32_t);
typedef void (*PackageFn)(uintptr_t, uint64_t*, uint32_t);
typedef uint8_t (*HasEffectFn)(const uint32_t*, uint32_t, uint32_t);
typedef void (*AddEffectFn)(const uint32_t*, uint32_t, void*, uint32_t);
typedef void (*RemoveEffectFn)(const uint32_t*, uint32_t, uint32_t);

static uintptr_t g_game;
static int g_seats;                  /* 0 not decided yet, 1 eight passenger seats, -1 six */
static int g_toldOther, g_toldNode;  /* logged: a type-40 owner that is no gunship; a seat 8 or 9 without its node */
static EntityFn g_entity;
static AliveFn g_alive;
static HasNodeFn g_hasNode;
static struct
{
    FindPackageFn find;
    PackageFn load, unload;
    HasEffectFn has;
    AddEffectFn add;
    RemoveEffectFn remove;
} g_fn;
static uint32_t g_extra[4], g_lastFour[4];   /* boosters 5 to 8, and the four the mod wrote to B last */

uint32_t co_extract_seats_ready(uintptr_t game)
{
    g_game = game;
    g_seats = g_toldOther = g_toldNode = 0;
    g_alive = NULL;
    g_hasNode = NULL;
    if (g_coApi->code_matches(game + CO_ENTITY_RECORD, kEntityPin) != 1) return CO_ENTITY_RECORD;
    if (g_coApi->code_matches(game + CO_SEAT_EVENTS, kSeatEventsPin) != 1) return CO_SEAT_EVENTS;
    g_entity = (EntityFn)(game + CO_ENTITY_RECORD);
    return 0;
}

void co_extract_boosters_ready(uintptr_t game)
{
    g_game = game;
    g_fn.find = (FindPackageFn)(game + CO_FIND_PACKAGE);
    g_fn.load = (PackageFn)(game + CO_LOAD);
    g_fn.unload = (PackageFn)(game + CO_UNLOAD);
    g_fn.has = (HasEffectFn)(game + CO_HAS_EFFECT);
    g_fn.add = (AddEffectFn)(game + CO_ADD_EFFECT);
    g_fn.remove = (RemoveEffectFn)(game + CO_REMOVE_EFFECT);
    memset(g_extra, 0, sizeof g_extra);
    memset(g_lastFour, 0, sizeof g_lastFour);
}

static void Decided(int seats, const char* line)
{
    g_seats = seats;
    co_log("extraction: %s", line);
}

/* The first gunship with a live unit decides, until the next apply: the recipe data is the same for every gunship of
 * the session. Owners of type 40 that are not a gunship are skipped, as the game skips unit 0 before alive (0x119FED2).
 * With none yet the game's six seats stay and the next call looks again; a count of eight only ever grows to ten. */
int co_seats_on(void)
{
    if (g_seats != 0) return g_seats > 0;
    const uintptr_t api = *(const uintptr_t*)(g_game + CO_SCRIPT_API);
    const uintptr_t collections = *(const uintptr_t*)(g_game + CO_SEAT_COLLECTIONS);
    if (api == 0 || collections == 0 || g_entity == NULL) return 0;
    void* const* unitApi = *(void* const* const*)(api + CO_UNIT_API);
    if (unitApi == NULL) return 0;
    const uintptr_t hasNode = (uintptr_t)unitApi[CO_HAS_NODE / 8], alive = (uintptr_t)unitApi[CO_ALIVE / 8], is = (uintptr_t)unitApi[CO_IS_RESOURCE / 8];
    if (hasNode == 0 || alive == 0 || is == 0 || g_coApi->code_matches(hasNode, kHasNodePin) != 1 ||
        g_coApi->code_matches(alive, kAlivePin) != 1 || g_coApi->code_matches(is, kIsPin) != 1)
    {
        Decided(-1, "the engine's unit functions are not the reviewed ones; the Pelican keeps six passenger seats");
        return 0;
    }
    const uint32_t count = *(const uint32_t*)(collections + CO_COLLECTION_COUNT);
    const uint8_t* records = *(const uint8_t* const*)(collections + CO_COLLECTION_RECORDS);
    const uintptr_t* owners = *(const uintptr_t* const*)(collections + CO_COLLECTION_OWNERS);
    for (uint32_t i = 0; records != NULL && owners != NULL && i < count; i++)
    {
        if (*(const uint32_t*)(records + i * CO_COLLECTION_RECORD) != CO_PELICAN_TYPE || owners[i] == 0) continue;
        const uintptr_t entity = g_entity(*(const uint32_t*)(owners[i] + 8u));
        if (entity == 0) continue;
        const uint32_t unit = *(const uint32_t*)(entity + CO_ENTITY_UNIT);
        if (unit == 0 || !((AliveFn)alive)(unit)) continue;
        if (!((IsFn)is)(unit, CO_GUNSHIP))
        {
            if (!g_toldOther) co_log("extraction: a unit other than shuttle_gunship has seat type 40; the mod looks at the next one");
            g_toldOther = 1;
            continue;
        }
        g_alive = (AliveFn)alive;
        g_hasNode = (HasNodeFn)hasNode;
        if (!g_hasNode(unit, CO_NODE_LEFT_4) || !g_hasNode(unit, CO_NODE_RIGHT_4))
            Decided(-1, "the Pelican's unit has no attach_passenger_left_4 and right_4 nodes (its recipe data is missing); six passenger seats");
        else Decided(1, "the Pelican's unit has the nodes attach_passenger_left_4 and right_4: eight passenger seats");
        return g_seats > 0;
    }
    return 0;
}

/* The node seat 8 or 9 attaches to on the vehicle with this entity record: its own only with the seats on and on this
 * unit, else left_3's or right_3's, which the game's seats 2 and 5 attach to on every type-40 unit. */
uint32_t co_seat_node(uintptr_t entity, uint32_t seat)
{
    const uint32_t own = seat == 8 ? CO_NODE_LEFT_4 : CO_NODE_RIGHT_4;
    if (co_seats_on() && entity != 0)
    {
        const uint32_t unit = *(const uint32_t*)(entity + CO_ENTITY_UNIT);
        if (unit != 0 && g_alive(unit) && g_hasNode(unit, own)) return own;
    }
    if (!g_toldNode) co_log("extraction: seat %u has no node of its own here; its passenger sits at the third seat's node", seat);
    g_toldNode = 1;
    return seat == 8 ? CO_NODE_LEFT_3 : CO_NODE_RIGHT_3;
}

static const uint8_t* Booster(uint32_t index) { return (const uint8_t*)(g_game + CO_BOOSTER_TABLE + index * CO_BOOSTER); }

/* The player a status effect goes to, as the game picks it: the local player, or "none". */
static const uint32_t* Player(void)
{
    const uintptr_t players = *(const uintptr_t*)(g_game + CO_PLAYERS_GLOBAL);
    if (*(const uint32_t*)(players + 0x88u) != 0) return (const uint32_t*)(*(const uintptr_t*)(players + 0xE8u) + 8u);
    return (const uint32_t*)(g_game + CO_NO_PLAYER);
}

/* Index 0x15 is the game's "not found": it reads that entry's package too, so this does. */
static void Change(uint32_t index, int load)
{
    uint64_t package = 0;
    g_fn.find(&package, *(const uint32_t*)(Booster(index) + 0x34u));
    if (package != 0) (load ? g_fn.load : g_fn.unload)(*(const uintptr_t*)(g_game + CO_PACKAGES), &package, 1);
    if (*(const uint32_t*)(*(const uintptr_t*)(g_game + CO_ENGINE) + CO_ENGINE_MODE) != 4 || index >= CO_BOOSTERS) return;
    const uint32_t effect = *(const uint32_t*)(Booster(index) + 4u);
    if (effect == 0) return;
    const uint32_t* player = Player();
    const uint8_t has = g_fn.has(player, *player, effect);
    if (load && !has)
    {
        uint8_t record[0x30];
        const uint32_t value = *(const uint32_t*)(*(const uintptr_t*)(g_game + CO_EFFECTS + effect * 8u) + 0x50u);
        memset(record, 0, sizeof record);
        memcpy(record, &effect, 4);
        memcpy(record + 4, &value, 4);
        record[9] = 1;
        g_fn.add(player, *player, record, 1);
    }
    else if (!load && has) g_fn.remove(player, *player, effect);
}

static int Holds(const uint32_t* list, uint32_t width, uint32_t value)
{
    for (uint32_t i = 0; i < width; i++)
        if (list[i] == value) return 1;
    return 0;
}

/* 0x8568A0 over eight. With four players or fewer and no booster beyond them it runs four wide, exactly as the game:
 * eight wide, the empty slots' index 0 would count as present. B keeps four; the other four stay here, and a B the
 * world has reset since the last write clears them. */
void co_boosters_update(uintptr_t b)
{
    uint32_t now[8] = {0}, old[8], collected = 0;
    const uintptr_t loadouts = *(const uintptr_t*)(g_game + CO_LOADOUTS);
    const uint32_t entries = loadouts ? *(const uint32_t*)(loadouts + CO_LOADOUT_COUNT) : 0;
    for (uint32_t i = 0; i < entries && collected < 8; i++)
    {
        const uint8_t* entry = (const uint8_t*)(loadouts + i * CO_LOADOUT);
        if (entry[0x28] != 0 || entry[0x2C] == 0) continue;
        uint32_t index = 0;
        while (index < CO_BOOSTERS && *(const uint32_t*)Booster(index) != *(const uint32_t*)(entry + 0x168)) index++;
        now[collected++] = index;
    }
    memcpy(old, (const void*)(b + CO_BOOSTERS_NOW), 16);
    if (memcmp(old, g_lastFour, 16) != 0) memset(g_extra, 0, sizeof g_extra);
    memcpy(old + 4, g_extra, 16);
    memcpy(g_lastFour, old, 16);
    const uint32_t width = collected > 4 || old[4] || old[5] || old[6] || old[7] ? 8u : 4u;
    if (memcmp(now, old, width * 4u) == 0) return;
    for (uint32_t s = 0; s < width; s++)
        if (!Holds(old, width, now[s])) Change(now[s], 1);
    for (uint32_t s = 0; s < width; s++)
        if (!Holds(now, width, old[s])) Change(old[s], 0);
    memcpy((void*)(b + CO_BOOSTERS_NOW), now, 16);
    memcpy(g_extra, now + 4, 16);
    memcpy(g_lastFour, now, 16);
}

#ifdef CO_TEST
void co_extract_test_reset(void)
{
    g_game = 0;
    g_seats = g_toldOther = g_toldNode = 0;
    g_entity = NULL;
    g_alive = NULL;
    g_hasNode = NULL;
    memset(&g_fn, 0, sizeof g_fn);
    memset(g_extra, 0, sizeof g_extra);
    memset(g_lastFour, 0, sizeof g_lastFour);
}
int co_extract_test_seats(void) { return g_seats; }
void co_extract_test_entity(EntityFn entity) { g_entity = entity; }
/* The stand-in's package and status effect functions, in the order find, load, unload, has, add, remove. */
void co_extract_test_calls(void* find, void* load, void* unload, void* has, void* add, void* remove)
{
    g_fn.find = (FindPackageFn)find; g_fn.load = (PackageFn)load; g_fn.unload = (PackageFn)unload;
    g_fn.has = (HasEffectFn)has; g_fn.add = (AddEffectFn)add; g_fn.remove = (RemoveEffectFn)remove;
}
const uint32_t* co_extract_test_extra(void) { return g_extra; }
#endif
