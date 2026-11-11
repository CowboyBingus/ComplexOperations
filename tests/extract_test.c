/* The caves past four players against a stand-in game image: the HiveMind's difficulty multipliers, the Pelican's seats
 * 8 and 9 behind the check of its unit's nodes, and every player's boosters. A build for eight writes them, each group
 * on its own: a changed byte leaves that group out and the session lists go in. A failed write takes everything back. A
 * build for four writes none of them. The difficulty caves, the attach node caves and the seat and booster callbacks
 * run natively. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void co_session_test_fill(uint8_t* image);
void co_session_test_reset(void);
int co_session_test_state(void);
unsigned co_session_test_sites(void);
unsigned co_session_test_stub_count(void);
void co_counts_test_fill(uint8_t* image);
void co_counts_test_reset(void);
int co_counts_test_on(void);
int co_counts_test_reviewed(const uint8_t* image);
uintptr_t co_counts_test_caves(void);
unsigned co_counts_test_room(void);
int co_counts_test_players(void);
int co_counts_test_write_of(uint32_t rva);
unsigned co_counts_test_group(uint8_t group);
int co_counts_test_active(uint8_t group);
uint32_t co_counts_test_refused(void);
uint64_t co_counts_test_seat_data(uint32_t* count);
uint64_t co_counts_test_seat_list(uint64_t lists);
uint64_t co_counts_test_seat_valid(uint64_t flags);
const uint8_t* co_counts_test_seat_block(size_t* size);
uint32_t co_counts_test_seat_node(uint64_t entity, uint32_t seat);
uint32_t co_counts_test_booster_pin(uint32_t rva);
int co_counts_test_booster_open(uint32_t rva);
void co_pods_test_reset(void);
void co_managers_test_reset(void);
void co_managers_test_force(int state);
void co_ui_test_reset(void);
void co_registry_test_fill(uint8_t* image);
void co_registry_test_reset(void);
void co_ui_test_force(int layout);
void co_extract_test_reset(void);
int co_extract_test_seats(void);
void co_extract_test_entity(uintptr_t (*entity)(uint32_t));
void co_extract_test_calls(void* find, void* load, void* unload, void* has, void* add, void* remove);
const uint32_t* co_extract_test_extra(void);

#define SESSION_INIT 0x134D740u
#define SESSION_GLOBAL 0x347CEF0u
#define CONTEXT 0x400000u
#define SYNCED_ENTRY 0x9F8u
#define ENTER 0x34C0000u            /* free in the stand-in: the test's ways into the caves */
#define G_DIFFICULTY 15
#define G_SEATS 16
#define G_BOOSTERS 17
/* The seat data the block copies, the collections and the script api (co_extract.c). */
#define SEAT_RECORDS 0x31AE690u
#define SEAT_LISTS 0x31AE590u
#define SEAT_PAIRS 0x31AEAF0u
#define SEAT_NODES 0x215C800u         /* the event handler's attach node per seat */
#define SEAT_LIST_AT 0x578u
#define COLLECTIONS 0x3326D88u
#define SCRIPT_API 0x3326308u
#define GUNSHIP 0xEF3A4136B21592CBull
#define LEFT_4 0xFC07BFB6u
#define RIGHT_4 0x27393B02u
#define LEFT_3 0xDD1461AEu
#define RIGHT_3 0x6AD680F5u
#define HUB 0xA524B781u
/* The booster update's globals. */
#define LOADOUTS 0x347CE50u
#define BOOSTER_TABLE 0x32FDA90u
#define PACKAGES 0x347CEB0u
#define ENGINE 0x3326340u
#define PLAYERS 0x3326468u
#define EFFECTS 0x37CB600u
#define EFFECTS_LEA 0x856993u
#define EFFECTS_MOVED 0x37CC000u   /* where a test puts the list as HellForge's stratagems move it */
#define B_NOW 0x14u

typedef uint64_t (*InitFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*SeatDataFn)(uint32_t, uint32_t*);
typedef uint64_t (*SeatListFn)(uint64_t, uint32_t);
typedef uint8_t (*SeatValidFn)(uint64_t, uint32_t);
typedef void (*BoostersFn)(uintptr_t);
typedef void (*RunFn)(const void*, uint32_t, float*);
typedef uint32_t (*NodeFn)(const void*, uint32_t, uintptr_t);

static int g_failed;
static const char* g_case;
static HfMod* const g_mod = (HfMod*)&g_fake;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static const uint32_t kDifficulty[] = {
    0x7F7B14u, 0x7F7BEAu, 0x7F8347u, 0x7F8F89u, 0x7F9B10u, 0x7F9C08u, 0x7F9D80u, 0x7F9E60u, 0x7F9F50u,
    0x7FA03Eu, 0x7FA129u, 0x7FA209u, 0x90BF86u, 0x913610u, 0x9136F0u, 0x94E9AAu, 0x94E9B3u, 0x9560E1u,
    0x9590A0u, 0x959220u, 0x9593E5u, 0x9595F0u, 0x9596D0u, 0x9597D5u, 0x95D5A3u, 0x95D691u,
};
#define DIFFICULTY_SITES (sizeof kDifficulty / sizeof kDifficulty[0])
static const struct { uint32_t rva, size; } kSeats[] = {
    {0x11956A4u, 6}, {0x119592Cu, 9}, {0x634758u, 5}, {0x634ED2u, 5}, {0x636C78u, 5}, {0x6370BCu, 5}, {0x11963E3u, 9},
    {0x1196945u, 8}, {0x1196CB0u, 10}, {0x119A188u, 10}, {0x119756Du, 9}, {0x119A857u, 10}, {0x119AF2Au, 10},
    {0x1191702u, 8}, {0x11917E2u, 8},
};
#define SEAT_SITES (sizeof kSeats / sizeof kSeats[0])
#define BOOSTER_SITE 0x573690u

/* A difficulty site's mulss as the game has it: its length, xmm register and base register. */
typedef struct { uint32_t size, xmm, base; } Mulss;
static Mulss Decode(const uint8_t* at)
{
    Mulss m = {0, 0, 0};
    uint32_t i = 1, rex = 0;
    if (at[0] != 0xF3) return m;
    if (at[1] >= 0x40 && at[1] <= 0x4F) { rex = at[1]; i = 2; }
    if (at[i] != 0x0F || at[i + 1] != 0x59) return m;
    m.size = i + 8;
    m.xmm = (at[i + 2] >> 3) & 7u;
    m.base = (at[i + 3] & 7u) | (rex & 1u ? 8u : 0u);
    return m;
}

static void Start(const char* name)
{
    const HfApi* api;
    const uint64_t none = 0;
    g_case = name;
    co_pods_test_reset();
    co_managers_test_reset();
    co_session_test_reset();
    co_counts_test_reset();
    co_ui_test_reset();
    co_extract_test_reset();
    co_registry_test_reset();
    api = fake_host_reset(CO_GAME_BUILD);
    co_session_test_fill(g_fake.image);
    co_counts_test_fill(g_fake.image);
    co_registry_test_fill(g_fake.image);
    memcpy(g_fake.image + SESSION_GLOBAL, &none, 8);
    if (hellforge_mod_init(api, g_mod) == 0 && g_fake.loaded != NULL) g_fake.loaded(g_mod, HF_EVENT_GAME_LOADED, NULL);
    /* The per-peer loadout table the session step moves, as the game keeps it. */
    uint8_t* table = g_fake.image + FAKE_SYNCED_TABLE;
    for (uint32_t i = 0; i < 4; i++)
    {
        const uint64_t peer = 0x7000u + i;
        memset(table + i * SYNCED_ENTRY, 0x40 + (int)i, 0x9F0);
        memcpy(table + i * SYNCED_ENTRY + 0x9F0, &peer, 8);
    }
}

static void Init(void)
{
    InitFn init = (InitFn)fake_host_detour(SESSION_INIT);
    const uint64_t context = (uint64_t)(uintptr_t)(g_fake.image + CONTEXT);
    memcpy(g_fake.image + SESSION_GLOBAL, &context, 8);
    if (init != NULL) init(0, 0, 0, 0);
    co_managers_test_force(1);
    co_ui_test_force(1);
}

static void Put(uint32_t rva, const void* value, size_t size) { memcpy(g_fake.image + rva, value, size); }
static void PutPointer(uint32_t rva, const void* pointer) { const uint64_t p = (uint64_t)(uintptr_t)pointer; Put(rva, &p, 8); }

/* The jump at a cave's site, the rest of the replaced bytes as nops; the cave it leads to. */
static const uint8_t* Cave(uint32_t site, uint32_t size)
{
    const uint8_t* image = g_fake.image;
    const uintptr_t caves = co_counts_test_caves();
    int32_t to;
    if (caves == 0 || image[site] != 0xE9) return NULL;
    for (uint32_t i = 5; i < size; i++)
        if (image[site + i] != 0x90) return NULL;
    memcpy(&to, image + site + 5 - 4, 4);
    const uint8_t* cave = image + site + 5 + to;
    return (uintptr_t)cave >= caves && (uintptr_t)cave < caves + co_counts_test_room() ? cave : NULL;
}

/* The stand-in has no code: the test puts its own where a cave goes back into the game, and makes it runnable. */
static void Landing(uint32_t rva, const uint8_t* code, size_t size)
{
    DWORD before;
    memcpy(g_fake.image + rva, code, size);
    CHECK(VirtualProtect(g_fake.image + rva, size, PAGE_EXECUTE_READWRITE, &before));
}

/* Every byte the game had at the extraction and difficulty sites. */
static uint8_t g_original[DIFFICULTY_SITES + SEAT_SITES + 1][12];
static void Remember(void)
{
    for (size_t i = 0; i < DIFFICULTY_SITES; i++) memcpy(g_original[i], g_fake.image + kDifficulty[i], 12);
    for (size_t i = 0; i < SEAT_SITES; i++) memcpy(g_original[DIFFICULTY_SITES + i], g_fake.image + kSeats[i].rva, 12);
    memcpy(g_original[DIFFICULTY_SITES + SEAT_SITES], g_fake.image + BOOSTER_SITE, 12);
}

/* How many of a group's sites hold a cave; the others hold the game's bytes. */
static unsigned Caves(int group)
{
    unsigned n = 0;
    if (group == G_DIFFICULTY)
        for (size_t i = 0; i < DIFFICULTY_SITES; i++)
        {
            const Mulss m = Decode(g_original[i]);
            if (Cave(kDifficulty[i], m.size) != NULL) n++;
            else CHECK(memcmp(g_fake.image + kDifficulty[i], g_original[i], m.size) == 0);
        }
    if (group == G_SEATS)
        for (size_t i = 0; i < SEAT_SITES; i++)
        {
            if (Cave(kSeats[i].rva, kSeats[i].size) != NULL) n++;
            else CHECK(memcmp(g_fake.image + kSeats[i].rva, g_original[DIFFICULTY_SITES + i], kSeats[i].size) == 0);
        }
    if (group == G_BOOSTERS)
    {
        if (Cave(BOOSTER_SITE, 5) != NULL) n++;
        else CHECK(memcmp(g_fake.image + BOOSTER_SITE, g_original[DIFFICULTY_SITES + SEAT_SITES], 5) == 0);
    }
    return n;
}

/* The way into a difficulty cave: the base register holds the record, eax the count - 1 and xmmN the value, xmm0 and
 * xmm1 two others; back in the game, all four go out to `out` {value, eax, xmm0, xmm1}. */
static void RunDifficulty(const uint8_t* cave, uint32_t back, Mulss m, const void* record, uint32_t count, float* out)
{
    uint8_t enter[96], land[64];
    size_t n = 0;
    static const uint8_t kSave[] = {0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x48,
                                    0x4C, 0x89, 0x44, 0x24, 0x20, 0x0F, 0x11, 0x74, 0x24, 0x28, 0x0F, 0x11, 0x7C, 0x24, 0x38,
                                    0xF3, 0x41, 0x0F, 0x10, 0x40, 0x08, 0xF3, 0x41, 0x0F, 0x10, 0x48, 0x0C};
    memcpy(enter, kSave, sizeof kSave);
    n = sizeof kSave;
    enter[n++] = 0xF3; enter[n++] = 0x41; enter[n++] = 0x0F; enter[n++] = 0x10; enter[n++] = (uint8_t)(m.xmm << 3);   /* movss xmmN, [r8] */
    enter[n++] = 0x89; enter[n++] = 0xD0;                                                                             /* mov eax, edx */
    enter[n++] = (uint8_t)(0x48 | (m.base >> 3)); enter[n++] = 0x89; enter[n++] = (uint8_t)(0xC8 | (m.base & 7u));    /* mov base, rcx */
    enter[n++] = 0xFF; enter[n++] = 0x25; memset(enter + n, 0, 4); n += 4;                                            /* jmp [rip] */
    const uint64_t to = (uint64_t)(uintptr_t)cave;
    memcpy(enter + n, &to, 8);
    n += 8;
    Landing(ENTER, enter, n);
    static const uint8_t kStore[] = {0x89, 0x41, 0x04, 0xF3, 0x0F, 0x11, 0x41, 0x08, 0xF3, 0x0F, 0x11, 0x49, 0x0C,
                                     0x0F, 0x10, 0x74, 0x24, 0x28, 0x0F, 0x10, 0x7C, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x48,
                                     0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5F, 0x5E, 0x5D, 0x5B, 0xC3};
    const uint8_t kLoad[] = {0x48, 0x8B, 0x4C, 0x24, 0x20, 0xF3, 0x0F, 0x11, (uint8_t)(0x01 | m.xmm << 3)};      /* movss [rcx], xmmN */
    memcpy(land, kLoad, sizeof kLoad);
    memcpy(land + sizeof kLoad, kStore, sizeof kStore);
    Landing(back, land, sizeof kLoad + sizeof kStore);
    ((RunFn)(uintptr_t)(g_fake.image + ENTER))(record, count - 1, out);
}

/* What a count's multiplier is: the game's own for 1 to 4, else the continuation with the 3 -> 4 step, at least 0.1. */
static float Expected(float value, const float* v, uint32_t count)
{
    if (count <= 4) return value * v[count - 1];
    const float step = v[3] - v[2], gone = (float)(count - 4) * step, sum = gone + v[3];
    return value * (sum > 0.1f ? sum : 0.1f);
}

static int Same(float a, float b) { uint32_t x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4); return x == y; }

/* The stand-in's Pelican: seat collections, the owners' entity records and the engine's unit functions. */
static uint8_t g_collections[0x60];
static uint32_t g_types[4 * 0x64 / 4];
static uint8_t g_owners[4][16];
static uintptr_t g_ownerList[4];
static uint8_t g_entities[4][24];
static void* g_unitApi[0x740 / 8];
static uint8_t g_script[0x20];
static uint32_t g_units[4];          /* by owner: the unit; 0xDEAD is a unit that is not alive */
static int g_isGunship[4], g_left[4], g_right[4], g_calls, g_aliveZero;

static int Owner(uint32_t unit)
{
    for (int i = 0; i < 4; i++)
        if (g_units[i] == unit) return i;
    return -1;
}
static uintptr_t Entity(uint32_t id) { return id >= 0x500u && id < 0x504u ? (uintptr_t)g_entities[id - 0x500u] : 0; }
/* A unit handle of 0 is slot 0, generation 0 to the engine: alive if slot 0 holds a unit, as here. */
static uint8_t Alive(uint32_t unit) { g_calls++; g_aliveZero += unit == 0; return unit != 0xDEADu && Owner(unit) >= 0; }
static uint8_t Is(uint32_t unit, uint64_t name) { g_calls++; return name == GUNSHIP && g_isGunship[Owner(unit)]; }
static uint8_t HasNode(uint32_t unit, uint32_t hash)
{
    g_calls++;
    return (hash == LEFT_4 && g_left[Owner(unit)]) || (hash == RIGHT_4 && g_right[Owner(unit)]);
}

/* `count` collections of these types, owner i's unit 0x900 + i. */
static void Pelicans(uint32_t count, const uint32_t* types)
{
    const uint32_t* t = types;
    memset(g_collections, 0, sizeof g_collections);
    memset(g_types, 0, sizeof g_types);
    memset(g_isGunship, 0, sizeof g_isGunship);
    memset(g_left, 0, sizeof g_left);
    memset(g_right, 0, sizeof g_right);
    g_calls = g_aliveZero = 0;
    for (uint32_t i = 0; i < 4; i++)
    {
        const uint32_t id = 0x500u + i;
        g_types[i * 0x64 / 4] = i < count ? t[i] : 0;
        memset(g_owners[i], 0, 16);
        memcpy(g_owners[i] + 8, &id, 4);
        g_ownerList[i] = (uintptr_t)g_owners[i];
        g_units[i] = 0x900u + i;
        memset(g_entities[i], 0, 24);
        memcpy(g_entities[i] + 8, &id, 4);
        memcpy(g_entities[i] + 0xC, &g_units[i], 4);
    }
    memcpy(g_collections + 0x10, &count, 4);
    const uint64_t owners = (uint64_t)(uintptr_t)g_ownerList, records = (uint64_t)(uintptr_t)g_types;
    memcpy(g_collections + 0x38, &owners, 8);
    memcpy(g_collections + 0x48, &records, 8);
    memset(g_unitApi, 0, sizeof g_unitApi);
    g_unitApi[0x6D0 / 8] = (void*)HasNode;
    g_unitApi[0x720 / 8] = (void*)Alive;
    g_unitApi[0x730 / 8] = (void*)Is;
    const uint64_t unitApi = (uint64_t)(uintptr_t)g_unitApi;
    memcpy(g_script + 0x18, &unitApi, 8);
    PutPointer(SCRIPT_API, g_script);
    PutPointer(COLLECTIONS, g_collections);
    co_extract_test_entity(Entity);
}

/* The way into the seat list cave: the seat goes in eax. */
static SeatListFn SeatListCave(const uint8_t* cave)
{
    uint8_t enter[16] = {0x89, 0xD0, 0xFF, 0x25, 0, 0, 0, 0};
    const uint64_t to = (uint64_t)(uintptr_t)cave;
    memcpy(enter + 8, &to, 8);
    Landing(ENTER + 0x100u, enter, sizeof enter);
    return (SeatListFn)(uintptr_t)(g_fake.image + ENTER + 0x100u);
}

/* The way into an attach node cave as the event handler has it: r14 the image, esi the seat, rdi the vehicle's entity
 * record; back in the game (`back`), r8d goes out. */
static NodeFn NodeCave(const uint8_t* cave, uint32_t back)
{
    static const uint8_t kEnter[] = {0x56, 0x57, 0x41, 0x56, 0x49, 0x89, 0xCE, 0x89, 0xD6, 0x4C, 0x89, 0xC7,
                                     0x41, 0xB8, 0x11, 0x11, 0x11, 0x11, 0xFF, 0x25, 0, 0, 0, 0};
    static const uint8_t kBack[] = {0x44, 0x89, 0xC0, 0x41, 0x5E, 0x5F, 0x5E, 0xC3};
    uint8_t enter[sizeof kEnter + 8];
    const uint64_t to = (uint64_t)(uintptr_t)cave;
    memcpy(enter, kEnter, sizeof kEnter);
    memcpy(enter + sizeof kEnter, &to, 8);
    Landing(ENTER + 0x200u, enter, sizeof enter);
    Landing(back, kBack, sizeof kBack);
    return (NodeFn)(uintptr_t)(g_fake.image + ENTER + 0x200u);
}

/* The game's attach nodes per seat, and what both node caves give for seats 0 to 12 on this entity record: the
 * game's node, and for 8 and 9 the one co_seat_node names. */
static int NodesAre(uintptr_t entity, uint32_t left, uint32_t right)
{
    static const uint32_t kNodes[] = {0x995DDD30u, 0xEB814487u, LEFT_3, 0x726600ADu, 0xA6571071u, RIGHT_3, HUB, 0, 0, 0, 0x1234u, 0x5678u, 0x9ABCu};
    static const struct { uint32_t rva, back; } kAt[] = {{0x1191702u, 0x119170Au}, {0x11917E2u, 0x11917EAu}};
    int ok = 1;
    Put(SEAT_NODES, kNodes, sizeof kNodes);
    for (size_t i = 0; i < 2; i++)
    {
        const uint8_t* cave = Cave(kAt[i].rva, 8);
        if (cave == NULL) return 0;
        const NodeFn node = NodeCave(cave, kAt[i].back);
        for (uint32_t seat = 0; seat < 13; seat++)
        {
            const uint32_t want = seat == 8 ? left : seat == 9 ? right : kNodes[seat], got = node(g_fake.image, seat, entity);
            if (got != want) { printf("  cave 0x%X seat %u: node 0x%X, expected 0x%X\n", kAt[i].rva, seat, got, want); ok = 0; }
        }
    }
    return ok && co_counts_test_seat_node(entity, 8) == left && co_counts_test_seat_node(entity, 9) == right;
}

static void SeatsAsTheGame(void)
{
    uint32_t count = 8;
    CHECK(co_counts_test_seat_data(&count) == (uint64_t)(uintptr_t)(g_fake.image + SEAT_RECORDS) && count == 8);
    CHECK(co_counts_test_seat_list(0x1000) == 0x1000 + 6 * 0x1C);
    CHECK(co_counts_test_seat_valid(0x247) == 0x246 && co_counts_test_seat_valid(0x246) == 0x246);
}

static void SeatsOn(void)
{
    uint32_t count = 8;
    size_t size;
    const uint8_t* block = co_counts_test_seat_block(&size);
    CHECK(co_counts_test_seat_data(&count) == (uint64_t)(uintptr_t)block && count == 10);
    CHECK(co_counts_test_seat_list(0x1000) == (uint64_t)(uintptr_t)(block + SEAT_LIST_AT));
    CHECK(co_counts_test_seat_valid(0x246) == 0x247 && co_counts_test_seat_valid(0x247) == 0x247);
}

/* The booster stand-ins: what was loaded, unloaded, given and taken, in order. */
static char g_events[512];
static uint8_t g_record[0x30];
static uint8_t g_player[16];
static void Event(const char* format, uint32_t value)
{
    const size_t used = strlen(g_events);
    snprintf(g_events + used, sizeof g_events - used, format, value);
}
static uint64_t* Find(uint64_t* out, uint32_t package) { *out = package != 0 ? 0xA000u + package : 0; return out; }
static void Load(uintptr_t manager, uint64_t* package, uint32_t n) { if (manager == 0x7777 && n == 1) Event("L%X ", (uint32_t)(*package - 0xA000u)); }
static void Unload(uintptr_t manager, uint64_t* package, uint32_t n) { if (manager == 0x7777 && n == 1) Event("U%X ", (uint32_t)(*package - 0xA000u)); }
static int g_has;
static uint8_t Has(const uint32_t* player, uint32_t id, uint32_t effect)
{
    return player == (const uint32_t*)(g_player + 8) && id == 0x42u && effect == 15 ? (uint8_t)g_has : 0xFF;
}
static void Add(const uint32_t* player, uint32_t id, void* record, uint32_t one)
{
    if (player == (const uint32_t*)(g_player + 8) && id == 0x42u && one == 1) { memcpy(g_record, record, sizeof g_record); Event("A%u ", *(uint32_t*)record); }
}
static void Remove(const uint32_t* player, uint32_t id, uint32_t effect)
{
    if (player == (const uint32_t*)(g_player + 8) && id == 0x42u) Event("R%u ", effect);
}

static uint8_t* g_loadouts;
static uint8_t g_engine[0xAC220], g_players[0xF0], g_effect[0x58], g_moved[0x58], g_b[0x40];

/* The players' boosters by table index; an index of 0x15 or more is an id the table does not hold. */
static void Players(uint32_t count, const uint32_t* index)
{
    memset(g_loadouts, 0, 0x2D204);
    memcpy(g_loadouts + 0x2D200, &count, 4);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t id = 0x100u + index[i];
        g_loadouts[i * 0x1690 + 0x2C] = 1;
        memcpy(g_loadouts + i * 0x1690 + 0x168, &id, 4);
    }
}

/* The booster table as the game holds it: ids 0x100 + i, a package for every index not divisible by 3, a status effect
 * (15) for index 20 alone. */
/* The game's lea r13 at 0x856993, pointed at the list as the game or HellForge has it. */
static void ListAt(uint32_t list)
{
    const uint8_t lea[3] = {0x4C, 0x8D, 0x2D};
    const int32_t disp = (int32_t)(list - (EFFECTS_LEA + 7));
    Put(EFFECTS_LEA, lea, 3);
    Put(EFFECTS_LEA + 3, &disp, 4);
}

static void Boosters(uint32_t mode)
{
    for (uint32_t i = 0; i < 21; i++)
    {
        const uint32_t id = 0x100u + i, package = i % 3 ? 0x500u + i : 0, effect = i == 20 ? 15u : 0;
        Put(BOOSTER_TABLE + i * 0x38, &id, 4);
        Put(BOOSTER_TABLE + i * 0x38 + 4, &effect, 4);
        Put(BOOSTER_TABLE + i * 0x38 + 0x34, &package, 4);
    }
    PutPointer(LOADOUTS, g_loadouts);
    const uint64_t manager = 0x7777;
    Put(PACKAGES, &manager, 8);
    memcpy(g_engine + 0xAC21C, &mode, 4);
    PutPointer(ENGINE, g_engine);
    const uint32_t local = 1, id = 0x42, value = 0x3F800000u;
    const uint64_t player = (uint64_t)(uintptr_t)g_player;
    memcpy(g_players + 0x88, &local, 4);
    memcpy(g_players + 0xE8, &player, 8);
    memcpy(g_player + 8, &id, 4);
    PutPointer(PLAYERS, g_players);
    memcpy(g_effect + 0x50, &value, 4);
    PutPointer(EFFECTS + 15 * 8, g_effect);
    ListAt(EFFECTS);
    memset(g_b, 0, sizeof g_b);
    co_extract_test_calls((void*)Find, (void*)Load, (void*)Unload, (void*)Has, (void*)Add, (void*)Remove);
}

static int Update(BoostersFn update, const char* expected, const uint32_t* now)
{
    g_events[0] = 0;
    update((uintptr_t)g_b);
    const int ok = strcmp(g_events, expected) == 0 && memcmp(g_b + B_NOW, now, 16) == 0;
    if (!ok) printf("  events '%s', expected '%s'\n", g_events, expected);
    return ok;
}

int main(void)
{
    const int players = co_counts_test_players();
    const int moves = (int)(co_session_test_sites() + co_session_test_stub_count());
    g_loadouts = calloc(1, 0x2D204 + 0x1690);

    Start("the reviewed game");
    Remember();
    CHECK(co_counts_test_group(G_DIFFICULTY) == DIFFICULTY_SITES && co_counts_test_group(G_SEATS) == SEAT_SITES &&
          co_counts_test_group(G_BOOSTERS) == 1);
    for (size_t i = 0; i < DIFFICULTY_SITES; i++) CHECK(Decode(g_original[i]).size != 0);
    Init();
    CHECK(co_session_test_state() == 1 && co_counts_test_on() == 1 && co_counts_test_refused() == 0);
    for (int group = G_DIFFICULTY; group <= G_BOOSTERS; group++) CHECK(co_counts_test_active((uint8_t)group) == (players == 8));
    CHECK(Caves(G_DIFFICULTY) == (players == 8 ? DIFFICULTY_SITES : 0u));
    CHECK(Caves(G_SEATS) == (players == 8 ? SEAT_SITES : 0u) && Caves(G_BOOSTERS) == (players == 8 ? 1u : 0u));
    CHECK(fake_host_logged("players: the HiveMind's difficulty multipliers go on past four players (26 caves)") == (players == 8));
    CHECK(fake_host_logged("extraction: the Pelican's seats 8 and 9 are in place (15 caves); they open once its unit has their nodes") ==
          (players == 8));
    CHECK(fake_host_logged("players: the boosters of all eight players are loaded (1 cave)") == (players == 8));
    CHECK(!fake_host_logged("multipliers stop at four") && !fake_host_logged("six passenger seats") &&
          !fake_host_logged("players 5 to 8 are not loaded"));
    if (players != 8)
    {
        /* A build for four writes none of it: the join gate keeps the count at four, where the caves change nothing. */
        CHECK(co_counts_test_write_of(kDifficulty[0]) == -1 && co_counts_test_write_of(kSeats[0].rva) == -1 &&
              co_counts_test_write_of(BOOSTER_SITE) == -1);
        printf("%s\n", g_failed ? "extract: failures" : "extract: all cases passed");
        return g_failed != 0;
    }

    /* Every multiplier for counts 1 to 8: the game's own instruction for 1 to 4, the continuation after, at least 0.1;
     * eax, xmm0 and xmm1 as they came. */
    {
        static const float kRows[][4] = {{1.0f, 1.5f, 2.0f, 2.5f}, {1.0f, 0.8f, 0.6f, 0.4f}, {0.3f, 0.7f, 1.1f, 1.3f},
                                         {2.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, NAN}, {1.0f, 1.0f, NAN, 1.0f}};
        static uint8_t record[0x440];
        const uint8_t* caves[DIFFICULTY_SITES];
        g_case = "the difficulty caves";
        for (size_t i = 0; i < DIFFICULTY_SITES; i++) caves[i] = Cave(kDifficulty[i], Decode(g_original[i]).size);
        for (size_t row = 0; row < sizeof kRows / sizeof kRows[0]; row++)
        {
            memset(record, 0, sizeof record);
            for (uint32_t k = 0; k < 13; k++) memcpy(record + 0x13C + 0x3C * k, kRows[row], 16);
            for (size_t i = 0; i < DIFFICULTY_SITES; i++)
            {
                const Mulss m = Decode(g_original[i]);
                if (caves[i] == NULL) { CHECK(caves[i] != NULL); continue; }
                for (uint32_t count = 1; count <= 8; count++)
                {
                    float out[4] = {3.0f, 0, -7.25f, 11.5f};
                    RunDifficulty(caves[i], kDifficulty[i] + m.size, m, record, count, out);
                    const float want = Expected(3.0f, kRows[row], count);
                    uint32_t eax;
                    memcpy(&eax, &out[1], 4);
                    if (!Same(out[0], want) || eax != count - 1 || !Same(out[2], -7.25f) || !Same(out[3], 11.5f))
                    {
                        printf("  site 0x%X row %u count %u: %g (expected %g), eax %u\n", kDifficulty[i], (unsigned)row, count, out[0], want, eax);
                        CHECK(0);
                    }
                }
            }
        }
        /* Spot values: 2.5 + 4 * 0.5 = 4.5 at eight; a falling row stops at 0.1; a NaN gives 0.1. */
        CHECK(Same(Expected(1.0f, kRows[0], 8), 4.5f) && Same(Expected(1.0f, kRows[1], 8), 0.1f) && Same(Expected(1.0f, kRows[4], 5), 0.1f));
    }

    /* The seat block: records 0 to 6 as the game's, the hub linking 0 to 6, 8 and 9, seats 8 and 9 linking the hub, then
     * the entrance list with 8 and 9 before its end. */
    {
        size_t size;
        const uint8_t* block = co_counts_test_seat_block(&size);
        static const uint32_t kHub[] = {HUB, 7, 9, 0, 1, 2, 3, 4, 5, 6, 8, 9};
        static const uint32_t kLeft[] = {LEFT_4, 8, 1, 7}, kRight[] = {RIGHT_4, 9, 1, 7};
        static const int32_t kEntrance[] = {0, 3, 1, 4, 2, 5, 8, 9, -1};
        static const int32_t kGameEntrance[] = {0, 3, 1, 4, 2, 5, -1};
        g_case = "the seat block";
        CHECK(size == 0x59C && (uintptr_t)block % 16 == 0);
        CHECK(memcmp(block, g_fake.image + SEAT_RECORDS, 7 * 0x8C) == 0);
        CHECK(memcmp(block + 7 * 0x8C, kHub, sizeof kHub) == 0 &&
              memcmp(block + 7 * 0x8C + sizeof kHub, g_fake.image + SEAT_RECORDS + 7 * 0x8C + sizeof kHub, 0x8C - sizeof kHub) == 0);
        CHECK(memcmp(block + 8 * 0x8C, kLeft, sizeof kLeft) == 0 && memcmp(block + 9 * 0x8C, kRight, sizeof kRight) == 0);
        for (uint32_t at = sizeof kLeft; at < 0x8C; at++) CHECK(block[8 * 0x8C + at] == 0 && block[9 * 0x8C + at] == 0);
        CHECK(memcmp(block + SEAT_LIST_AT, kEntrance, sizeof kEntrance) == 0);
        CHECK(memcmp(g_fake.image + SEAT_LISTS + 6 * 0x1C, kGameEntrance, sizeof kGameEntrance) == 0);
    }

    /* The node check: undecided while no Pelican has a live unit, then decided once by the first one. */
    {
        static const uint32_t kOne[] = {40}, kDeadFirst[] = {12, 40, 40};
        g_case = "no Pelican yet";
        Pelicans(0, kOne);
        SeatsAsTheGame();
        CHECK(co_extract_test_seats() == 0 && g_calls == 0);

        g_case = "a Pelican whose unit is not alive, another type, then one with both nodes";
        Pelicans(3, kDeadFirst);
        g_units[1] = 0xDEADu;
        memcpy(g_entities[1] + 0xC, &g_units[1], 4);
        g_isGunship[2] = g_left[2] = g_right[2] = 1;
        g_isGunship[0] = 1;
        SeatsOn();
        CHECK(co_extract_test_seats() == 1);
        CHECK(fake_host_logged("extraction: the Pelican's unit has the nodes attach_passenger_left_4 and right_4: eight passenger seats"));
        const int calls = g_calls;
        g_left[2] = 0;
        SeatsOn();
        CHECK(g_calls == calls);

        /* Each attach asks its own unit: the gunship with both nodes gets them, one that lost left_4 (as one without
         * the recipe data) and owner 0 without any get the third seats' nodes, as do no record, unit 0 and a dead unit. */
        g_case = "the attach nodes, unit by unit";
        g_left[2] = 1;
        CHECK(NodesAre((uintptr_t)g_entities[2], LEFT_4, RIGHT_4));
        g_left[2] = 0;
        CHECK(NodesAre((uintptr_t)g_entities[2], LEFT_3, RIGHT_4));
        CHECK(NodesAre((uintptr_t)g_entities[0], LEFT_3, RIGHT_3));
        CHECK(NodesAre(0, LEFT_3, RIGHT_3));
        CHECK(fake_host_logged("extraction: seat 8 has no node of its own here; its passenger sits at the third seat's node"));
        {
            const uint32_t zero = 0, dead = 0xDEADu;
            memcpy(g_entities[3] + 0xC, &zero, 4);
            g_aliveZero = 0;
            CHECK(NodesAre((uintptr_t)g_entities[3], LEFT_3, RIGHT_3) && g_aliveZero == 0);
            memcpy(g_entities[3] + 0xC, &dead, 4);
            CHECK(NodesAre((uintptr_t)g_entities[3], LEFT_3, RIGHT_3));
        }

        /* The callbacks as the caves call them: the getter's count, the entrance's list, and seats 8 and 9 valid. */
        const SeatDataFn data = (SeatDataFn)(uintptr_t)Cave(kSeats[0].rva, kSeats[0].size);
        const uint8_t* list = Cave(kSeats[7].rva, kSeats[7].size);
        const SeatValidFn valid = (SeatValidFn)(uintptr_t)Cave(kSeats[6].rva, kSeats[6].size);
        static const uint8_t kTrue[] = {0xB0, 0x01, 0xC3}, kFalse[] = {0x31, 0xC0, 0xC3}, kGame[] = {0xB0, 0x02, 0xC3};
        size_t size;
        const uint8_t* block = co_counts_test_seat_block(&size);
        uint32_t count = 0;
        CHECK(data != NULL && list != NULL && valid != NULL);
        if (data != NULL && list != NULL && valid != NULL)
        {
            Landing(0x119630Eu, kTrue, sizeof kTrue);
            Landing(0x11962F7u, kFalse, sizeof kFalse);
            Landing(0x11963ECu, kGame, sizeof kGame);
            const SeatListFn lists = SeatListCave(list);
            CHECK(data(40, &count) == (uint64_t)(uintptr_t)block && count == 10);
            CHECK(lists(0x1000, 6) == (uint64_t)(uintptr_t)(block + SEAT_LIST_AT));
            CHECK(lists(0x1000, 3) == 0x1000 + 3 * 0x1C && lists(0x1000, 8) == 0x1000 && lists(0x1000, 9) == 0x1000);
            for (uint32_t seat = 0; seat < 12; seat++) CHECK(valid(0, seat) == (seat < 8 ? 2 : seat < 10 ? 1 : 0));
        }

        g_case = "a Pelican without the nodes";
        Start(g_case);
        Init();
        Pelicans(1, kOne);
        g_isGunship[0] = g_left[0] = 1;
        SeatsAsTheGame();
        CHECK(co_extract_test_seats() == -1 && fake_host_logged("extraction: the Pelican's unit has no attach_passenger_left_4 and right_4 nodes"));
        const int before = g_calls;
        g_right[0] = 1;
        SeatsAsTheGame();
        CHECK(g_calls == before);
        {
            const SeatDataFn off = (SeatDataFn)(uintptr_t)Cave(kSeats[0].rva, kSeats[0].size);
            const uint8_t* offList = Cave(kSeats[7].rva, kSeats[7].size);
            const SeatValidFn offValid = (SeatValidFn)(uintptr_t)Cave(kSeats[6].rva, kSeats[6].size);
            CHECK(off != NULL && offList != NULL && offValid != NULL);
            if (off != NULL && offList != NULL && offValid != NULL)
            {
                Landing(0x119630Eu, kTrue, sizeof kTrue);
                Landing(0x11962F7u, kFalse, sizeof kFalse);
                Landing(0x11963ECu, kGame, sizeof kGame);
                const SeatListFn lists = SeatListCave(offList);
                count = 0;
                CHECK(off(40, &count) == (uint64_t)(uintptr_t)(g_fake.image + SEAT_RECORDS) && count == 8);
                CHECK(lists(0x1000, 6) == 0x1000 + 6 * 0x1C);
                for (uint32_t seat = 0; seat < 12; seat++) CHECK(offValid(0, seat) == (seat < 8 ? 2 : 0));
            }
        }
        /* A peer's message can still seat someone in 8 or 9: with the seats off they sit at the third seats' nodes,
         * even on a unit that has the new ones. */
        g_case = "the attach nodes with the seats off";
        g_left[0] = 1;
        CHECK(NodesAre((uintptr_t)g_entities[0], LEFT_3, RIGHT_3));

        g_case = "unit 0 before a gunship";
        Start(g_case);
        Init();
        {
            static const uint32_t kTwo[] = {40, 40};
            const uint32_t zero = 0;
            Pelicans(2, kTwo);
            g_units[0] = 0;
            memcpy(g_entities[0] + 0xC, &zero, 4);
            g_isGunship[0] = g_isGunship[1] = g_left[1] = g_right[1] = 1;
            SeatsOn();
            CHECK(co_extract_test_seats() == 1 && g_aliveZero == 0);
        }

        g_case = "seat type 40 on another unit, then a gunship";
        Start(g_case);
        Init();
        {
            static const uint32_t kTwo[] = {40, 40};
            Pelicans(2, kTwo);
            g_left[0] = g_right[0] = 1;
            SeatsAsTheGame();
            SeatsAsTheGame();
            CHECK(co_extract_test_seats() == 0 &&
                  fake_host_logged("extraction: a unit other than shuttle_gunship has seat type 40; the mod looks at the next one"));
            g_isGunship[1] = g_left[1] = g_right[1] = 1;
            SeatsOn();
            CHECK(co_extract_test_seats() == 1);
            CHECK(NodesAre((uintptr_t)g_entities[0], LEFT_4, RIGHT_4) && NodesAre((uintptr_t)g_entities[1], LEFT_4, RIGHT_4));
            g_left[0] = g_right[0] = 0;
            CHECK(NodesAre((uintptr_t)g_entities[0], LEFT_3, RIGHT_3));
        }

        g_case = "the engine's unit functions changed";
        Start(g_case);
        Init();
        Pelicans(1, kOne);
        g_isGunship[0] = g_left[0] = g_right[0] = 1;
        g_unitApi[0x6D0 / 8] = g_fake.image + 0x34D0000u;
        g_fake.noMatch[0] = 0x34D0000u;
        SeatsAsTheGame();
        CHECK(co_extract_test_seats() == -1 && g_calls == 0 &&
              fake_host_logged("extraction: the engine's unit functions are not the reviewed ones; the Pelican keeps six passenger seats"));
    }

    /* The boosters: four wide as the game with four players or fewer, eight wide past them; B keeps the first four. */
    {
        static const uint32_t kThree[] = {1, 2, 4}, kSix[] = {1, 2, 4, 5, 7, 8}, kFive[] = {1, 2, 4, 5, 7}, kTwo[] = {1, 2};
        static const uint32_t kNowThree[] = {1, 2, 4, 0}, kNowSix[] = {1, 2, 4, 5}, kNowTwo[] = {1, 2, 0, 0}, kZero[4] = {0};
        static const uint8_t kRet[] = {0xC3};
        Start("the boosters");
        Init();
        Boosters(1);
        const BoostersFn cave = (BoostersFn)(uintptr_t)Cave(BOOSTER_SITE, 5);
        CHECK(cave != NULL);
        Landing(BOOSTER_SITE + 5, kRet, sizeof kRet);
        const BoostersFn update = cave != NULL ? cave : co_boosters_update;
        g_case = "three players";
        Players(3, kThree);
        CHECK(Update(update, "L501 L502 L504 ", kNowThree) && Update(update, "", kNowThree));
        g_case = "six players";
        Players(6, kSix);
        CHECK(Update(update, "L505 L507 L508 ", kNowSix) && Update(update, "", kNowSix));
        CHECK(co_extract_test_extra()[0] == 7 && co_extract_test_extra()[1] == 8 && co_extract_test_extra()[2] == 0);
        g_case = "the sixth leaves";
        Players(5, kFive);
        CHECK(Update(update, "U508 ", kNowSix));
        g_case = "down to two";
        Players(2, kTwo);
        CHECK(Update(update, "U504 U505 U507 ", kNowTwo) && Update(update, "", kNowTwo));
        CHECK(co_extract_test_extra()[0] == 0);
        g_case = "the world resets B";
        Players(6, kSix);
        CHECK(Update(update, "L504 L505 L507 L508 ", kNowSix));
        memset(g_b, 0, sizeof g_b);
        CHECK(Update(update, "L501 L502 L504 L505 L507 L508 ", kNowSix));
        g_case = "nobody";
        Players(0, kTwo);
        CHECK(Update(update, "U501 U502 U504 U505 U507 U508 ", kZero) && Update(update, "", kZero));
        g_case = "an id the table does not hold";
        {
            static const uint32_t kUnknown[] = {1, 2, 3, 4, 0x999};
            static const uint32_t kNowUnknown[] = {1, 2, 3, 4};
            Players(5, kUnknown);
            CHECK(Update(update, "L501 L502 L504 ", kNowUnknown) && co_extract_test_extra()[0] == 0x15);
        }

        g_case = "a status effect in a mission";
        Start(g_case);
        Init();
        Boosters(4);
        {
            static const uint32_t kFive20[] = {1, 2, 4, 5, 20}, kFour[] = {1, 2, 4, 5};
            Players(5, kFive20);
            g_has = 0;
            CHECK(Update(co_boosters_update, "L501 L502 L504 L505 L514 A15 ", kFour));
            uint32_t effect, value;
            memcpy(&effect, g_record, 4);
            memcpy(&value, g_record + 4, 4);
            CHECK(effect == 15 && value == 0x3F800000u && g_record[8] == 0 && g_record[9] == 1);
            for (uint32_t at = 10; at < sizeof g_record; at++) CHECK(g_record[at] == 0);
            Players(4, kFour);
            g_has = 1;
            CHECK(Update(co_boosters_update, "U514 R15 ", kFour));

            /* A mod added a stratagem: HellForge moved the list and rewrote the lea. The value comes from the moved
             * list, and the pin leaves the lea's four rewritten bytes open, so the check still passes. */
            g_case = "the status effects' list moved";
            const uint32_t moved = 0x40000000u;
            memcpy(g_moved + 0x50, &moved, 4);
            PutPointer(EFFECTS_MOVED + 15 * 8, g_moved);
            ListAt(EFFECTS_MOVED);
            Players(5, kFive20);
            g_has = 0;
            memset(g_record, 0, sizeof g_record);
            CHECK(Update(co_boosters_update, "L514 A15 ", kFour));
            memcpy(&value, g_record + 4, 4);
            CHECK(value == moved);
            for (uint32_t at = EFFECTS_LEA; at < EFFECTS_LEA + 10; at++)
                CHECK(co_counts_test_booster_open(at) == (at >= EFFECTS_LEA + 3 && at < EFFECTS_LEA + 7));
            ListAt(EFFECTS);
        }
    }

    /* Each group on its own: a changed byte leaves that group out, says so once, and the rest goes in. */
    {
        static const struct { const char* name; uint32_t rva, at; int group; const char* line; } kChanged[] = {
            {"a difficulty site changed", 0x94E9B3u, 4, G_DIFFICULTY,
             "players: the code at game+0x94E9B3 is not the reviewed one; the HiveMind's multipliers stop at four players"},
            {"a seat site changed", 0x11917E2u, 7, G_SEATS,
             "extraction: the code at game+0x11917E2 is not the reviewed one; the Pelican keeps six passenger seats"},
            {"the seat records changed", SEAT_RECORDS + 7 * 0x8C + 8, 0, G_SEATS,
             "extraction: the seat data at game+0x31AE690 is not the reviewed one; the Pelican keeps six passenger seats"},
            {"the entrance list changed", SEAT_LISTS + 6 * 0x1C + 24, 0, G_SEATS,
             "extraction: the seat data at game+0x31AE638 is not the reviewed one; the Pelican keeps six passenger seats"},
            {"the entrance pairs changed", SEAT_PAIRS + 9 * 4, 0, G_SEATS,
             "extraction: the seat data at game+0x31AEAF0 is not the reviewed one; the Pelican keeps six passenger seats"},
            {"the booster site changed", BOOSTER_SITE, 1, G_BOOSTERS,
             "players: the code at game+0x573690 is not the reviewed one; boosters of players 5 to 8 are not loaded"},
        };
        for (size_t i = 0; i < sizeof kChanged / sizeof kChanged[0]; i++)
        {
            Start(kChanged[i].name);
            g_fake.image[kChanged[i].rva + kChanged[i].at] ^= 0x01;
            Remember();
            Init();
            CHECK(co_counts_test_on() == 1 && co_counts_test_refused() == 1u << kChanged[i].group && fake_host_logged(kChanged[i].line));
            CHECK(fake_host_logged("session: the session lists count to eight"));
            CHECK(Caves(G_DIFFICULTY) == (kChanged[i].group == G_DIFFICULTY ? 0u : DIFFICULTY_SITES));
            CHECK(Caves(G_SEATS) == (kChanged[i].group == G_SEATS ? 0u : SEAT_SITES));
            CHECK(Caves(G_BOOSTERS) == (kChanged[i].group == G_BOOSTERS ? 0u : 1u));
        }
        static const struct { const char* name; uint32_t rva; int group; const char* line; } kCode[] = {
            {"the entity lookup changed", 0xFD9D40u, G_SEATS, "extraction: the code at game+0xFD9D40 is not the reviewed one"},
            {"the seat event handler changed", 0x1191531u, G_SEATS, "extraction: the code at game+0x1191531 is not the reviewed one"},
            {"the booster collector changed", 0x8568BAu, G_BOOSTERS, "players: the code at game+0x8568BA is not the reviewed one"},
            {"the status effect call changed", 0x66F190u, G_BOOSTERS, "players: the code at game+0x66F190 is not the reviewed one"},
        };
        for (size_t i = 0; i < sizeof kCode / sizeof kCode[0]; i++)
        {
            Start(kCode[i].name);
            Remember();
            g_fake.noMatch[0] = kCode[i].rva;
            Init();
            CHECK(co_counts_test_on() == 1 && co_counts_test_refused() == 1u << kCode[i].group && fake_host_logged(kCode[i].line));
            CHECK(Caves(kCode[i].group) == 0);
        }
        /* What the booster callback rebuilds of 0x8568A0 and its callees is pinned: the stride, B+0x14, the package,
         * mode, effect, players, no-player, effect-table and package-manager reads, the store, the callees' calls and
         * argument use. Each pin's change leaves the boosters out. */
        static const uint32_t kDepends[] = {0x8568BAu, 0x8568D7u, 0x8568F0u, 0x856903u, 0x856916u, 0x85692Fu, 0x856945u, 0x8569E3u,
                                            0x8569E8u, 0x8569F5u, 0x856A07u, 0x856A13u, 0x856A2Au, 0x856A3Au, 0x856A43u, 0x856A50u,
                                            0x856A5Bu, 0x856A6Eu, 0x856A73u, 0x856A8Eu, 0x856A9Fu, 0x856B1Fu, 0x856B6Fu, 0x856B7Eu,
                                            0x856BB6u, 0x1268A99u, 0x12FE350u, 0x12FE4E0u, 0x670E30u, 0x66F1B7u, 0x66F4C0u};
        uint32_t pins[sizeof kDepends / sizeof kDepends[0]];
        size_t distinct = 0;
        for (size_t i = 0; i < sizeof kDepends / sizeof kDepends[0]; i++)
        {
            const uint32_t pin = co_counts_test_booster_pin(kDepends[i]);
            g_case = "a booster dependency is pinned";
            if (pin == 0) { printf("  0x%X is in no booster pin\n", kDepends[i]); CHECK(pin != 0); continue; }
            size_t j = 0;
            while (j < distinct && pins[j] != pin) j++;
            if (j < distinct) continue;
            pins[distinct++] = pin;
            static char name[64], line[160];
            snprintf(name, sizeof name, "the booster code at game+0x%X changed", pin);
            snprintf(line, sizeof line, "players: the code at game+0x%X is not the reviewed one; boosters of players 5 to 8 are not loaded", pin);
            Start(name);
            Remember();
            g_fake.noMatch[0] = pin;
            Init();
            CHECK(co_counts_test_on() == 1 && co_counts_test_refused() == 1u << G_BOOSTERS && fake_host_logged(line));
            CHECK(Caves(G_BOOSTERS) == 0 && Caves(G_SEATS) == SEAT_SITES);
        }
        CHECK(distinct == 9);
    }

    /* A failed write at any of them takes back every site, as with the others. */
    {
        const uint32_t kFirst[] = {kDifficulty[0], kDifficulty[DIFFICULTY_SITES - 1], kSeats[0].rva, kSeats[SEAT_SITES - 1].rva, BOOSTER_SITE};
        for (size_t i = 0; i < sizeof kFirst / sizeof kFirst[0]; i++)
        {
            static char name[64];
            snprintf(name, sizeof name, "the write at game+0x%X fails", kFirst[i]);
            Start(name);
            const int at = co_counts_test_write_of(kFirst[i]);
            CHECK(at >= 0);
            g_fake.failWriteAt = moves + at;
            Init();
            CHECK(co_counts_test_on() == 0 && co_counts_test_reviewed(g_fake.image));
            CHECK(fake_host_logged("a count site could not be written; the session lists count to four as before"));
            CHECK(!fake_host_logged("go on past four players") && !fake_host_logged("seats 8 and 9 are in place"));
        }
    }

    printf("%s\n", g_failed ? "extract: failures" : "extract: all cases passed");
    return g_failed != 0;
}
