/* Eight players: the game's per-player component managers with eight slots.
 *
 * Each manager is a struct inside the game's component block with four slots in every per-player array and the next
 * manager right behind it. As for the pod manager (co_pods.c), the mod builds each one in its own memory: the places
 * that reach a manager from the block get the distance to the new struct, the arrays that eight slots of a neighbour
 * would overrun get a new offset behind the old end, and the capacity checks say 8. Everything else finds the manager
 * through the global the game's own code binds, so it follows without a change. Code the game wrote out for four rows of
 * a moved array goes to a cave beside the game's code that does all eight, written and undone with the rest.
 * A build for eight also moves the avatar manager the same way, from eight slots to sixteen: players' bodies hold a
 * slot too, so eight players need more than eight. */
#include <string.h>
#include <windows.h>

#include "co.h"

typedef enum CoManagerKind { CO_MGR_BLOCK, CO_MGR_ARRAY, CO_MGR_CAP, CO_MGR_CODE } CoManagerKind;
typedef struct CoManager
{
    const char* name;
    uint32_t global, offset, size, newSize;
    uint8_t slots;    /* what the capacity checks say once it has moved */
    uint8_t players;  /* the smallest build (CO_PLAYERS) that moves it */
} CoManager;
typedef struct CoManagerSite
{
    uint32_t rva;
    uint8_t at;       /* offset of the value in the instruction */
    uint32_t old;     /* its vanilla value */
    uint8_t manager;
    int32_t add;      /* what the array's new offset adds; for code, the four new bytes */
    CoManagerKind kind;
    uint8_t pin[8];   /* the instruction bytes before the value */
} CoManagerSite;
typedef struct CoManagerCave
{
    uint32_t rva;
    uint8_t size;     /* the bytes the jump replaces */
    uint32_t back;    /* where the cave goes on in the game; 0 when its code returns */
    uint8_t length, replaced[12], code[64];
} CoManagerCave;
#include "co_manager_sites.h"

#define CO_MANAGERS (sizeof kCoManagers / sizeof kCoManagers[0])
#define CO_MANAGER_SITES (sizeof kCoManagerSites / sizeof kCoManagerSites[0])
#define CO_MANAGER_CAVES (sizeof kCoManagerCaves / sizeof kCoManagerCaves[0])
/* A cave's room: its code and the jump back. */
#define CO_MGR_CAVE_ROOM 0x50u
_Static_assert(CO_MANAGER_CAVES * CO_MGR_CAVE_ROOM <= 0x1000u, "the caves must fit their page");

static uintptr_t g_game, g_memory, g_block, g_caves;
static size_t g_used;
static uint32_t g_place[CO_MANAGERS];
static int g_state; /* 0 not moved, 1 eight slots, -1 left as the game has it */

static size_t SiteSize(const CoManagerSite* s) { return s->kind == CO_MGR_CAP ? 1u : 4u; }
static int Active(const CoManagerSite* s) { return kCoManagers[s->manager].players <= CO_PLAYERS; }

/* 0 when the new struct is too far from the block for the instruction. */
static int SiteValue(const CoManagerSite* s, uint32_t* value)
{
    if (s->kind == CO_MGR_CAP) { *value = kCoManagers[s->manager].slots; return 1; }
    if (s->kind == CO_MGR_ARRAY) { *value = s->old + (uint32_t)s->add; return 1; }
    if (s->kind == CO_MGR_CODE) { *value = (uint32_t)s->add; return 1; }
    const CoManager* m = &kCoManagers[s->manager];
    int64_t v = (int64_t)(g_memory + g_place[s->manager]) - (int64_t)(g_block + m->offset) + (int64_t)s->old + s->add;
    *value = (uint32_t)(int32_t)v;
    return v >= INT32_MIN && v <= INT32_MAX;
}

/* The caves go first: they work on the moved rows. */
static void Undo(size_t sites, size_t caves)
{
    for (size_t j = 0; j < caves; j++) g_coApi->write(g_game + kCoManagerCaves[j].rva, kCoManagerCaves[j].replaced, kCoManagerCaves[j].size);
    for (size_t j = 0; j < sites; j++)
        if (Active(&kCoManagerSites[j]))
            g_coApi->write(g_game + kCoManagerSites[j].rva + kCoManagerSites[j].at, &kCoManagerSites[j].old, SiteSize(&kCoManagerSites[j]));
}

static void Release(void)
{
    co_free_near(g_memory);
    if (g_caves) VirtualFree((void*)g_caves, 0, MEM_RELEASE);
    g_memory = g_block = g_caves = 0;
}

/* Each cave whole and executable before any jump leads to it; 0 when the game's code is out of reach. */
static int BuildCaves(void)
{
    for (size_t i = 0; i < CO_MANAGER_CAVES; i++)
    {
        const CoManagerCave* c = &kCoManagerCaves[i];
        uint8_t* cave = (uint8_t*)(g_caves + i * CO_MGR_CAVE_ROOM);
        memcpy(cave, c->code, c->length);
        if (c->back == 0) continue;
        const int64_t back = (int64_t)(g_game + c->back) - (int64_t)((uintptr_t)cave + c->length + 5u);
        if (back < INT32_MIN || back > INT32_MAX) return 0;
        const int32_t rel = (int32_t)back;
        cave[c->length] = 0xE9;
        memcpy(cave + c->length + 1, &rel, 4);
    }
    DWORD before;
    return VirtualProtect((void*)g_caves, 0x1000u, PAGE_EXECUTE_READ, &before) != 0;
}

static int WriteCave(size_t i)
{
    const CoManagerCave* c = &kCoManagerCaves[i];
    uint8_t jump[12];
    const int64_t to = (int64_t)(g_caves + i * CO_MGR_CAVE_ROOM) - (int64_t)(g_game + c->rva + 5u);
    if (to < INT32_MIN || to > INT32_MAX) return -1;
    const int32_t rel = (int32_t)to;
    memset(jump, 0x90, sizeof jump);
    jump[0] = 0xE9;
    memcpy(jump + 1, &rel, 4);
    return g_coApi->write(g_game + c->rva, jump, c->size);
}

static void Move(uintptr_t block)
{
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        const CoManagerSite* s = &kCoManagerSites[i];
        uint8_t bytes[12];
        if (!Active(s)) continue;
        if (g_coApi->read(g_game + s->rva, bytes, s->at + SiteSize(s)) != 0 || memcmp(bytes, s->pin, s->at) != 0 ||
            memcmp(bytes + s->at, &s->old, SiteSize(s)) != 0)
        {
            co_log("players: the code at game+0x%X is not the reviewed one; four player slots as before", s->rva);
            return;
        }
    }
    for (size_t i = 0; i < CO_MANAGER_CAVES; i++)
    {
        uint8_t bytes[12];
        const CoManagerCave* c = &kCoManagerCaves[i];
        if (g_coApi->read(g_game + c->rva, bytes, c->size) != 0 || memcmp(bytes, c->replaced, c->size) != 0)
        {
            co_log("players: the code at game+0x%X is not the reviewed one; four player slots as before", c->rva);
            return;
        }
    }
    g_used = 0;
    for (size_t i = 0; i < CO_MANAGERS; i++)
        if (kCoManagers[i].players <= CO_PLAYERS) { g_place[i] = (uint32_t)g_used; g_used += (kCoManagers[i].newSize + 0xFFFu) & ~(size_t)0xFFFu; }
    g_memory = co_alloc_near(block, g_used);
    if (g_memory == 0) { co_log("players: no free memory within 2 GB of the entity state; four player slots as before"); return; }
    g_caves = co_alloc_near(g_game + 0x1000000u, 0x1000u);
    if (g_caves == 0 || !BuildCaves())
    { Release(); co_log("players: no free memory near the game's code; four player slots as before"); return; }
    g_block = block;
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        const CoManagerSite* s = &kCoManagerSites[i];
        uint32_t value;
        if (!Active(s)) continue;
        /* The game cannot run on a mix: the sites written so far go back. */
        if (!SiteValue(s, &value) || g_coApi->write(g_game + s->rva + s->at, &value, SiteSize(s)) != 0)
        {
            Undo(i, 0);
            Release();
            co_log("players: game+0x%X could not be written; four player slots as before", s->rva);
            return;
        }
    }
    /* The caves follow the rows they work on. */
    for (size_t i = 0; i < CO_MANAGER_CAVES; i++)
        if (WriteCave(i) != 0)
        {
            Undo(CO_MANAGER_SITES, i);
            Release();
            co_log("players: game+0x%X could not be written; four player slots as before", kCoManagerCaves[i].rva);
            return;
        }
    g_state = 1;
    {
        unsigned managers = 0, sites = 0, avatars = 0;
        for (size_t i = 0; i < CO_MANAGERS; i++) managers += kCoManagers[i].slots == 8u;
        for (size_t i = 0; i < CO_MANAGER_SITES; i++)
        {
            if (!Active(&kCoManagerSites[i])) continue;
            if (kCoManagers[kCoManagerSites[i].manager].slots == 8u) sites++;
            else avatars++;
        }
        co_log("players: %u component managers have 8 slots (%u code sites changed)", managers, sites);
        if (avatars != 0) co_log("players: the avatar manager has 16 slots for players and their bodies (%u code sites changed)", avatars);
    }
    co_log("players: the score manager's loot rows are cleared and added for eight rows (%u caves)", (unsigned)CO_MANAGER_CAVES);
}

#ifdef CO_CANARY
/* A check build: the managers' old places in the block hold a pattern. Code that still writes there is a site the
 * lists miss. */
static uint8_t g_told[CO_MANAGERS];
void co_managers_canary(int fill)
{
    static unsigned checks;
    if (g_state != 1) return;
    if (!fill && (++checks == 1 || checks % 20 == 0))
        co_log("players: CANARY checked %u times; first old place holds %02X", checks, *(uint8_t*)(g_block + kCoManagers[0].offset));
    for (size_t i = 0; i < CO_MANAGERS; i++)
    {
        uint8_t* old = (uint8_t*)(g_block + kCoManagers[i].offset);
        if (kCoManagers[i].players > CO_PLAYERS) continue;
        if (fill) { memset(old, 0xC5, kCoManagers[i].size); g_told[i] = 0; continue; }
        for (uint32_t at = 0; at < kCoManagers[i].size && !g_told[i]; at++)
            if (old[at] != 0xC5)
            {
                g_told[i] = 1;
                co_log("players: CANARY %s +0x%X holds %02X %02X %02X %02X", kCoManagers[i].name, at, old[at], old[at + 1], old[at + 2], old[at + 3]);
            }
    }
}
#endif

/* Runs in the state constructor's hook, before the game's constructor. The sites hold distances from one component
 * block, so a state built somewhere else gets them written again. The game zeroes its state before the constructor;
 * the moved managers are outside it. */
void co_managers_state(uintptr_t block)
{
    if (g_state == 1 && block != g_block)
    {
        Undo(CO_MANAGER_SITES, CO_MANAGER_CAVES);
        Release();
        g_state = 0;
    }
    if (g_state == 0)
    {
        size_t active = 0;
        for (size_t i = 0; i < CO_MANAGERS; i++) active += kCoManagers[i].players <= CO_PLAYERS;
        g_state = -1;
        if (active != 0) Move(block);
        else co_log("players: a build for four keeps the game's player managers");
    }
    if (g_state == 1) memset((void*)g_memory, 0, g_used);
#ifdef CO_CANARY
    co_managers_canary(1);
#endif
}

void co_managers_install(uintptr_t game) { g_game = game; }

int co_managers_moved(void) { return g_state == 1; }

/* An array the mover gave a new offset has a site with exactly the game's offset; a member with none stayed. */
uint32_t co_managers_offset(uint32_t global, uint32_t offset)
{
    if (g_state != 1) return offset;
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        const CoManagerSite* s = &kCoManagerSites[i];
        if (s->kind == CO_MGR_ARRAY && s->old == offset && kCoManagers[s->manager].global == global && Active(s)) return offset + (uint32_t)s->add;
    }
    return offset;
}

/* The game's own count is the value its capacity checks hold. */
uint32_t co_managers_slots(uint32_t global)
{
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        const CoManagerSite* s = &kCoManagerSites[i];
        if (s->kind != CO_MGR_CAP || kCoManagers[s->manager].global != global) continue;
        return g_state == 1 && Active(s) ? kCoManagers[s->manager].slots : s->old;
    }
    return 0;
}

#ifdef CO_TEST
void co_managers_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        memcpy(image + kCoManagerSites[i].rva, kCoManagerSites[i].pin, kCoManagerSites[i].at);
        memcpy(image + kCoManagerSites[i].rva + kCoManagerSites[i].at, &kCoManagerSites[i].old, SiteSize(&kCoManagerSites[i]));
    }
    for (size_t i = 0; i < CO_MANAGER_CAVES; i++) memcpy(image + kCoManagerCaves[i].rva, kCoManagerCaves[i].replaced, kCoManagerCaves[i].size);
}

void co_managers_test_reset(void)
{
    Release();
    g_game = 0;
    g_state = 0;
}

int co_managers_test_state(void) { return g_state; }
uintptr_t co_managers_test_memory(void) { return g_memory; }
uintptr_t co_managers_test_place(unsigned i) { return g_memory + g_place[i]; }
/* The sites this build writes. */
unsigned co_managers_test_sites(void)
{
    unsigned n = 0;
    for (size_t i = 0; i < CO_MANAGER_SITES; i++) n += Active(&kCoManagerSites[i]);
    return n;
}
/* The manager's number by name; -1 when this build does not move it. */
int co_managers_test_index(const char* name)
{
    for (size_t i = 0; i < CO_MANAGERS; i++)
        if (strcmp(kCoManagers[i].name, name) == 0) return kCoManagers[i].players <= CO_PLAYERS ? (int)i : -1;
    return -1;
}
/* Sets the state the other steps see, without memory: for their tests only. */
void co_managers_test_force(int state) { g_state = state; }
unsigned co_managers_test_caves(void) { return (unsigned)CO_MANAGER_CAVES; }
uintptr_t co_managers_test_cave_room(void) { return g_caves; }
/* The number of the write that changes the site or cave at rva; -1 for none. */
int co_managers_test_write_of(uint32_t rva)
{
    int n = 0;
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        if (!Active(&kCoManagerSites[i])) continue;
        if (kCoManagerSites[i].rva == rva) return n;
        n++;
    }
    for (size_t i = 0; i < CO_MANAGER_CAVES; i++)
        if (kCoManagerCaves[i].rva == rva) return n + (int)i;
    return -1;
}
#endif
