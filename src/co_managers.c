/* Eight players: the game's per-player component managers with eight slots.
 *
 * Each manager is a struct inside the game's component block with four slots in every per-player array and the next
 * manager right behind it. As for the pod manager (co_pods.c), the mod builds each one in its own memory: the places
 * that reach a manager from the block get the distance to the new struct, the arrays that eight slots of a neighbour
 * would overrun get a new offset behind the old end, and the capacity checks say 8. Everything else finds the manager
 * through the global the game's own code binds, so it follows without a change. */
#include <string.h>
#include <windows.h>

#include "co.h"

typedef enum CoManagerKind { CO_MGR_BLOCK, CO_MGR_ARRAY, CO_MGR_CAP } CoManagerKind;
typedef struct CoManager { const char* name; uint32_t global, offset, size, newSize; } CoManager;
typedef struct CoManagerSite
{
    uint32_t rva;
    uint8_t at;       /* offset of the value in the instruction */
    uint32_t old;     /* its vanilla value */
    uint8_t manager;
    int32_t add;      /* what the array's new offset adds */
    CoManagerKind kind;
    uint8_t pin[8];   /* the instruction bytes before the value */
} CoManagerSite;
#include "co_manager_sites.h"

#define CO_MANAGERS (sizeof kCoManagers / sizeof kCoManagers[0])
#define CO_MANAGER_SITES (sizeof kCoManagerSites / sizeof kCoManagerSites[0])
#define CO_MANAGER_SLOTS 8u

static uintptr_t g_game, g_memory, g_block;
static size_t g_used;
static uint32_t g_place[CO_MANAGERS];
static int g_state; /* 0 not moved, 1 eight slots, -1 left as the game has it */

static size_t SiteSize(const CoManagerSite* s) { return s->kind == CO_MGR_CAP ? 1u : 4u; }

/* 0 when the new struct is too far from the block for the instruction. */
static int SiteValue(const CoManagerSite* s, uint32_t* value)
{
    if (s->kind == CO_MGR_CAP) { *value = CO_MANAGER_SLOTS; return 1; }
    if (s->kind == CO_MGR_ARRAY) { *value = s->old + (uint32_t)s->add; return 1; }
    const CoManager* m = &kCoManagers[s->manager];
    int64_t v = (int64_t)(g_memory + g_place[s->manager]) - (int64_t)(g_block + m->offset) + (int64_t)s->old + s->add;
    *value = (uint32_t)(int32_t)v;
    return v >= INT32_MIN && v <= INT32_MAX;
}

static void Undo(size_t count)
{
    for (size_t j = 0; j < count; j++)
        g_coApi->write(g_game + kCoManagerSites[j].rva + kCoManagerSites[j].at, &kCoManagerSites[j].old, SiteSize(&kCoManagerSites[j]));
}

static void Move(uintptr_t block)
{
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        const CoManagerSite* s = &kCoManagerSites[i];
        uint8_t bytes[12];
        if (g_coApi->read(g_game + s->rva, bytes, s->at + SiteSize(s)) != 0 || memcmp(bytes, s->pin, s->at) != 0 ||
            memcmp(bytes + s->at, &s->old, SiteSize(s)) != 0)
        {
            co_log("players: the code at game+0x%X is not the reviewed one; four player slots as before", s->rva);
            return;
        }
    }
    g_used = 0;
    for (size_t i = 0; i < CO_MANAGERS; i++) { g_place[i] = (uint32_t)g_used; g_used += (kCoManagers[i].newSize + 0xFFFu) & ~(size_t)0xFFFu; }
    g_memory = co_alloc_near(block, g_used);
    if (g_memory == 0) { co_log("players: no free memory within 2 GB of the entity state; four player slots as before"); return; }
    g_block = block;
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        const CoManagerSite* s = &kCoManagerSites[i];
        uint32_t value;
        /* The game cannot run on a mix: the sites written so far go back. */
        if (!SiteValue(s, &value) || g_coApi->write(g_game + s->rva + s->at, &value, SiteSize(s)) != 0)
        {
            Undo(i);
            VirtualFree((void*)g_memory, 0, MEM_RELEASE);
            g_memory = g_block = 0;
            co_log("players: game+0x%X could not be written; four player slots as before", s->rva);
            return;
        }
    }
    g_state = 1;
    co_log("players: %u component managers have %u slots (%u code sites changed)", (unsigned)CO_MANAGERS, CO_MANAGER_SLOTS,
           (unsigned)CO_MANAGER_SITES);
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
        Undo(CO_MANAGER_SITES);
        VirtualFree((void*)g_memory, 0, MEM_RELEASE);
        g_memory = g_block = 0;
        g_state = 0;
    }
    if (g_state == 0) { g_state = -1; Move(block); }
    if (g_state == 1) memset((void*)g_memory, 0, g_used);
#ifdef CO_CANARY
    co_managers_canary(1);
#endif
}

void co_managers_install(uintptr_t game) { g_game = game; }

#ifdef CO_TEST
void co_managers_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_MANAGER_SITES; i++)
    {
        memcpy(image + kCoManagerSites[i].rva, kCoManagerSites[i].pin, kCoManagerSites[i].at);
        memcpy(image + kCoManagerSites[i].rva + kCoManagerSites[i].at, &kCoManagerSites[i].old, SiteSize(&kCoManagerSites[i]));
    }
}

void co_managers_test_reset(void)
{
    if (g_memory) VirtualFree((void*)g_memory, 0, MEM_RELEASE);
    g_memory = g_block = g_game = 0;
    g_state = 0;
}

int co_managers_test_state(void) { return g_state; }
uintptr_t co_managers_test_memory(void) { return g_memory; }
uintptr_t co_managers_test_place(unsigned i) { return g_memory + g_place[i]; }
unsigned co_managers_test_sites(void) { return (unsigned)CO_MANAGER_SITES; }
#endif
