/* Eight players: the type registry's list of loadout card widgets. Every loadout card adds its widget to one list of the
 * game's type registry, which has room for four entries; the next two lists lie right behind it, so eight cards would
 * overwrite them. The list moves to a block of the mod's with room for eight: the 7 loads of the registry's global
 * whose value reaches the list read a qword of the mod's instead, which holds the new list minus the list's offset.
 * Those loads reach nothing else of the registry, and no other load reaches the list (scripts/sites/registry_sites.py).
 * A build for eight moves it before the loadout screen's sites go in, and takes it back with them. */
#include <string.h>
#include <windows.h>

#include "co.h"

typedef struct CoRegistrySite { uint32_t rva; uint8_t size, at; uint8_t game[8]; } CoRegistrySite;
#include "co_registry_sites.h"

#define CO_REGISTRY_SITES (sizeof kCoRegistrySites / sizeof kCoRegistrySites[0])
/* The block: the qword the loads read, then the list as the game lays it out (a count, entries from +8). */
#define CO_CARD_BASE 0x0u
#define CO_CARD_AT 0x10u
#define CO_CARD_BYTES (8u + CO_CARD_ROOM * CO_CARD_ENTRY)
#define CO_CARD_BLOCK 0x1000u
_Static_assert(CO_CARD_AT % 8 == 0 && CO_CARD_AT + CO_CARD_BYTES <= CO_CARD_BLOCK, "the list fits its block at the game's alignment");
static const char kKeeps[] = "the loadout screen keeps four cards";
static const char kStays[] = "the loadout cards' list stays in the mod's memory";

static uintptr_t g_game, g_block;
static int g_on;

/* What a site's displacement holds: the distance to the global, or to the mod's qword. 0 when it is out of reach. */
static int Distance(const CoRegistrySite* s, int on, int32_t* out)
{
    if (!on) { memcpy(out, s->game + s->at, 4); return 1; }
    const int64_t d = (int64_t)(g_block + CO_CARD_BASE) - (int64_t)(g_game + s->rva + s->size);
    if (d < INT32_MIN || d > INT32_MAX) return 0;
    *out = (int32_t)d;
    return 1;
}

/* Every site to `on`, or none: the game cannot run with its adds in one list and its walkers in the other. */
static int Switch(int on, const char* otherwise)
{
    int32_t want[CO_REGISTRY_SITES], was;
    for (size_t i = 0; i < CO_REGISTRY_SITES; i++)
        if (!Distance(&kCoRegistrySites[i], on, &want[i]))
        { co_log("players: the mod's memory is out of reach of game+0x%X; %s", kCoRegistrySites[i].rva, otherwise); return 0; }
    for (size_t i = 0; i < CO_REGISTRY_SITES; i++)
    {
        const CoRegistrySite* s = &kCoRegistrySites[i];
        if (g_coApi->write(g_game + s->rva + s->at, &want[i], 4) == 0) continue;
        while (i--)
            if (Distance(&kCoRegistrySites[i], !on, &was)) g_coApi->write(g_game + kCoRegistrySites[i].rva + kCoRegistrySites[i].at, &was, 4);
        co_log("players: game+0x%X could not be written; %s", s->rva, otherwise);
        return 0;
    }
    return 1;
}

/* From the counts' apply in engine init's first session state, before any loadout screen exists. A card already in the
 * game's list means a screen built for four cards, which the loadout sites must not change either: refused, not copied. */
int co_registry_cards(uintptr_t game)
{
    uintptr_t registry = 0;
    uint32_t count = 0;
    if (g_on) return 1;
    g_game = game;
    /* No registry yet: nothing can be registered in it. */
    if (g_coApi->read(game + CO_REGISTRY_GLOBAL, &registry, sizeof registry) != 0 ||
        (registry != 0 && g_coApi->read(registry + CO_CARD_LIST, &count, sizeof count) != 0))
    { co_log("players: the type registry could not be read; %s", kKeeps); return 0; }
    if (count != 0) { co_log("players: the type registry's list of loadout cards is not empty (%u); %s", count, kKeeps); return 0; }
    for (size_t i = 0; i < CO_REGISTRY_SITES; i++)
    {
        const CoRegistrySite* s = &kCoRegistrySites[i];
        uint8_t bytes[8];
        if (g_coApi->read(game + s->rva, bytes, s->size) != 0 || memcmp(bytes, s->game, s->size) != 0)
        { co_log("players: the code at game+0x%X is not the reviewed one; %s", s->rva, kKeeps); return 0; }
    }
    /* Kept once allocated: a walker on another thread may still hold the qword's value. */
    if (g_block == 0) g_block = co_alloc_near(game + 0x1000000u, CO_CARD_BLOCK);
    if (g_block == 0) { co_log("players: no free memory near the game's code; %s", kKeeps); return 0; }
    memset((void*)(g_block + CO_CARD_AT), 0, CO_CARD_BYTES);
    *(uint64_t*)(g_block + CO_CARD_BASE) = (uint64_t)(g_block + CO_CARD_AT - CO_CARD_LIST);
    if (!Switch(1, kKeeps)) return 0;
    g_on = 1;
    co_log("players: the type registry's list of loadout cards has room for eight (%u code sites changed)", (unsigned)CO_REGISTRY_SITES);
    return 1;
}

/* A card in the mod's list takes itself out of it later, through the same loads: they keep leading there. */
void co_registry_back(void)
{
    if (!g_on) return;
    const uint32_t count = *(volatile const uint32_t*)(g_block + CO_CARD_AT);
    if (count != 0) { co_log("players: the loadout cards' list is not empty (%u); %s", count, kStays); return; }
    if (Switch(0, kStays)) g_on = 0;
}

#ifdef CO_TEST
void co_registry_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_REGISTRY_SITES; i++) memcpy(image + kCoRegistrySites[i].rva, kCoRegistrySites[i].game, kCoRegistrySites[i].size);
}

void co_registry_test_reset(void)
{
    if (g_block) VirtualFree((void*)g_block, 0, MEM_RELEASE);
    g_block = g_game = 0;
    g_on = 0;
}

unsigned co_registry_test_sites(void) { return (unsigned)CO_REGISTRY_SITES; }
uint32_t co_registry_test_rva(unsigned i) { return kCoRegistrySites[i].rva; }
int co_registry_test_on(void) { return g_on; }
/* Where the list is in the mod's block, and the qword the loads read; 0 without a block. */
uintptr_t co_registry_test_list(void) { return g_block ? g_block + CO_CARD_AT : 0; }
uintptr_t co_registry_test_slot(void) { return g_block ? g_block + CO_CARD_BASE : 0; }
unsigned co_registry_test_room(void) { return CO_CARD_ROOM; }
/* Every site holds the game's bytes. */
int co_registry_test_reviewed(const uint8_t* image)
{
    for (size_t i = 0; i < CO_REGISTRY_SITES; i++)
        if (memcmp(image + kCoRegistrySites[i].rva, kCoRegistrySites[i].game, kCoRegistrySites[i].size) != 0) return 0;
    return 1;
}
#endif
