/* Eight players: tables a build for eight moves at run time, because their owner is packed between other objects and
 * cannot grow. The galactic map's cursors live in a system of the component block: they move when the state is built.
 *
 * Each table has three layouts. OFF is the game's code. KEPT keeps four entries and guards them: an index of four or
 * more skips the access. MOVED gives the table eight entries in a block of the mod's: the displacements that reach it
 * from its owner get the distance to the block, the counts say eight and the guards bound eight. KEPT is written at
 * install; MOVED when the owner is built, before the game's code first touches the table. Each switch compares every
 * byte before it writes, and a failed write takes back the ones before it. */
#include <string.h>
#include <windows.h>

#include "co.h"

typedef enum CoUiKind { CO_UI_GAME, CO_UI_BYTES, CO_UI_REL, CO_UI_JUMP } CoUiKind;
typedef struct CoUiBlockInfo { const char* name; uint32_t old, size; } CoUiBlockInfo;
typedef struct CoUiSite
{
    uint32_t rva;
    uint8_t size, block;
    CoUiKind kept, moved;     /* what KEPT and MOVED write here; OFF writes the game's bytes */
    uint8_t at;               /* REL: where the displacement is */
    int32_t add;              /* REL: what the region adds on top of the table's distance */
    uint16_t keptCave, movedCave;
    uint8_t game[12], keptBytes[12], movedBytes[12];
} CoUiSite;
typedef struct CoUiCave { uint16_t code, length; uint16_t jumps[3]; uint16_t field, fields; } CoUiCave;
typedef struct CoUiField { uint16_t at; uint32_t old; int32_t add; } CoUiField;
#include "co_ui_sites.h"

#define CO_UI_BLOCKS (sizeof kCoUiBlocks / sizeof kCoUiBlocks[0])
#define CO_UI_SITES (sizeof kCoUiSites / sizeof kCoUiSites[0])
#define CO_UI_CAVES (sizeof kCoUiCaves / sizeof kCoUiCaves[0])
#define CO_UI_ROOM 0x1000u
_Static_assert(sizeof kCoUiCode <= CO_UI_ROOM, "the caves' code must fit their room");
/* Around a moved table: an index the guards miss lands in the mod's memory instead of past it. */
#define CO_UI_PAD 0x1000u
enum { CO_UI_OFF, CO_UI_KEPT, CO_UI_MOVED };

/* What a layout means for the player, by block. */
static const struct { const char* four; const char* eight; } kCoUiSays[] = {
    {"the galactic map keeps four cursors", "the galactic map has eight cursors"},
};
_Static_assert(sizeof kCoUiSays / sizeof kCoUiSays[0] == CO_UI_BLOCKS, "every block says what it does");

typedef struct CoUiState
{
    int layout, failed;
    uintptr_t owner, memory, table, room;
    size_t bytes;
} CoUiState;
static CoUiState g_ui[CO_UI_BLOCKS];
static uintptr_t g_game, g_keptRoom;

static int64_t Distance(unsigned block)
{
    return (int64_t)g_ui[block].table - (int64_t)(g_ui[block].owner + kCoUiBlocks[block].old);
}

static int Fits(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

/* A copy of every cave, its jumps led into the game; `block`'s fields (none for -1) get its table's distance. */
static uintptr_t Room(int block)
{
    const uintptr_t room = co_alloc_near(g_game + 0x1000000u, CO_UI_ROOM);
    DWORD before;
    if (room == 0) return 0;
    memcpy((void*)room, kCoUiCode, sizeof kCoUiCode);
    for (size_t i = 0; i < CO_UI_CAVES; i++)
    {
        const CoUiCave* c = &kCoUiCaves[i];
        uint8_t* cave = (uint8_t*)(room + c->code);
        for (size_t j = 0; j < 3 && c->jumps[j]; j++)
        {
            int32_t rel;
            memcpy(&rel, cave + c->jumps[j], 4);
            const int64_t real = (int64_t)rel + (int64_t)g_game - (int64_t)(uintptr_t)cave;
            if (!Fits(real)) goto failed;
            rel = (int32_t)real;
            memcpy(cave + c->jumps[j], &rel, 4);
        }
    }
    for (size_t i = 0; block >= 0 && i < CO_UI_SITES; i++)
    {
        const CoUiSite* s = &kCoUiSites[i];
        if (s->block != (unsigned)block || s->moved != CO_UI_JUMP) continue;
        const CoUiCave* c = &kCoUiCaves[s->movedCave];
        for (uint16_t f = c->field; f < c->field + c->fields; f++)
        {
            const int64_t v = (int64_t)kCoUiFields[f].old + Distance((unsigned)block) + kCoUiFields[f].add;
            if (!Fits(v)) goto failed;
            const int32_t value = (int32_t)v;
            memcpy((void*)(room + kCoUiFields[f].at), &value, 4);
        }
    }
    if (VirtualProtect((void*)room, CO_UI_ROOM, PAGE_EXECUTE_READ, &before)) return room;
failed:
    VirtualFree((void*)room, 0, MEM_RELEASE);
    return 0;
}

/* The bytes a layout has at a site; 0 when its table or cave is out of reach. */
static int Want(const CoUiSite* s, int layout, uint8_t out[12])
{
    const CoUiKind kind = layout == CO_UI_OFF ? CO_UI_GAME : layout == CO_UI_KEPT ? s->kept : s->moved;
    memcpy(out, s->game, s->size);
    if (kind == CO_UI_BYTES) memcpy(out, layout == CO_UI_KEPT ? s->keptBytes : s->movedBytes, s->size);
    if (kind == CO_UI_REL)
    {
        int32_t old;
        memcpy(&old, s->game + s->at, 4);
        const int64_t v = (int64_t)old + Distance(s->block) + s->add;
        if (!Fits(v)) return 0;
        const int32_t value = (int32_t)v;
        memcpy(out + s->at, &value, 4);
    }
    if (kind == CO_UI_JUMP)
    {
        const uintptr_t room = layout == CO_UI_KEPT ? g_keptRoom : g_ui[s->block].room;
        const uint16_t cave = layout == CO_UI_KEPT ? s->keptCave : s->movedCave;
        const int64_t rel = (int64_t)(room + kCoUiCaves[cave].code) - (int64_t)(g_game + s->rva + 5u);
        if (room == 0 || !Fits(rel)) return 0;
        const int32_t value = (int32_t)rel;
        memset(out, 0x90, s->size);
        out[0] = 0xE9;
        memcpy(out + 1, &value, 4);
    }
    return 1;
}

/* Every site of the block from one layout to the other, or none: the bytes must be `from`'s, and a failed write
 * puts back the ones before it. */
static int Switch(unsigned block, int from, int to)
{
    static uint8_t want[CO_UI_SITES][12];
    uint8_t was[12], is[12];
    for (size_t i = 0; i < CO_UI_SITES; i++)
    {
        const CoUiSite* s = &kCoUiSites[i];
        if (s->block != block) continue;
        if (!Want(s, from, was) || !Want(s, to, want[i]))
        { co_log("players: the %s table is out of reach of the game's code; %s", kCoUiBlocks[block].name, kCoUiSays[block].four); return 0; }
        if (g_coApi->read(g_game + s->rva, is, s->size) != 0 || memcmp(is, was, s->size) != 0)
        { co_log("players: the code at game+0x%X is not the reviewed one; %s", s->rva, kCoUiSays[block].four); return 0; }
    }
    for (size_t i = 0; i < CO_UI_SITES; i++)
    {
        const CoUiSite* s = &kCoUiSites[i];
        if (s->block != block || !Want(s, from, was) || memcmp(was, want[i], s->size) == 0) continue;
        if (g_coApi->write(g_game + s->rva, want[i], s->size) == 0) continue;
        while (i--)
            if (kCoUiSites[i].block == block && Want(&kCoUiSites[i], from, was) && memcmp(was, want[i], kCoUiSites[i].size) != 0)
                g_coApi->write(g_game + kCoUiSites[i].rva, was, kCoUiSites[i].size);
        co_log("players: game+0x%X could not be written; %s", s->rva, kCoUiSays[block].four);
        return 0;
    }
    return 1;
}

static void Release(CoUiState* b)
{
    if (b->room) VirtualFree((void*)b->room, 0, MEM_RELEASE);
    if (b->memory) VirtualFree((void*)b->memory, 0, MEM_RELEASE);
    b->room = b->memory = b->table = b->owner = 0;
}

/* The table at the old one's address modulo 0x100, so that every entry keeps its alignment. */
static void Move(unsigned block, uintptr_t owner)
{
    CoUiState* b = &g_ui[block];
    const uintptr_t old = owner + kCoUiBlocks[block].old;
    b->bytes = (2u * CO_UI_PAD + 0x100u + kCoUiBlocks[block].size + 0xFFFu) & ~(size_t)0xFFFu;
    b->memory = co_alloc_near(old, b->bytes);
    if (b->memory == 0)
    { b->failed = 1; co_log("players: no free memory within 2 GB of the %s table; %s", kCoUiBlocks[block].name, kCoUiSays[block].four); return; }
    b->owner = owner;
    b->table = b->memory + CO_UI_PAD + (old & 0xFFu);
    b->room = Room((int)block);
    if (b->room == 0) co_log("players: the %s table's code is out of reach; %s", kCoUiBlocks[block].name, kCoUiSays[block].four);
    if (b->room == 0 || !Switch(block, CO_UI_KEPT, CO_UI_MOVED)) { Release(b); b->failed = 1; return; }
    b->layout = CO_UI_MOVED;
    unsigned sites = 0;
    for (size_t i = 0; i < CO_UI_SITES; i++) sites += kCoUiSites[i].block == block;
    co_log("players: %s (%u code sites changed)", kCoUiSays[block].eight, sites);
}

/* Back to four entries before the owner is built again somewhere else. */
static int Back(unsigned block)
{
    if (!Switch(block, CO_UI_MOVED, CO_UI_KEPT))
    {
        g_ui[block].failed = 1;
        co_log("players: the game rebuilt the %s table's owner elsewhere and its code could not be set back; restart the game",
               kCoUiBlocks[block].name);
        return 0;
    }
    Release(&g_ui[block]);
    g_ui[block].layout = CO_UI_KEPT;
    return 1;
}

/* Runs in the state constructor's hook, before the game's constructor: the map system is in the component block, and
 * the map object is built only after the constructor has bound the system's global. The game zeroes its state first;
 * the moved table is outside it. */
void co_ui_state(uintptr_t block)
{
    CoUiState* b = &g_ui[CO_UI_MAP];
    if (CO_PLAYERS != 8 || (b->layout == CO_UI_MOVED && b->owner != block && !Back(CO_UI_MAP))) return;
    if (b->layout == CO_UI_KEPT && !b->failed) Move(CO_UI_MAP, block);
    if (b->layout == CO_UI_MOVED) memset((void*)b->memory, 0, b->bytes);
}

/* While a block's guards for four are not in, no index above 3 may exist: the join limit stays at four. */
int co_ui_guarded(void)
{
    for (size_t i = 0; i < CO_UI_SITES; i++)
        if (kCoUiSites[i].kept != CO_UI_GAME && g_ui[kCoUiSites[i].block].layout == CO_UI_OFF) return 0;
    return 1;
}

/* On the loader's thread, before the state constructor's hook goes in. A build for four has these guards in its counts
 * (scripts/gen_count_sites.py) and the tables stay as the game has them. */
void co_ui_install(uintptr_t game)
{
    if (CO_PLAYERS != 8) return;
    memset(g_ui, 0, sizeof g_ui);
    g_game = game;
    g_keptRoom = Room(-1);
    if (g_keptRoom == 0) { co_log("players: no free memory near the game's code; a session takes four players"); return; }
    for (unsigned block = 0; block < CO_UI_BLOCKS; block++)
        if (Switch(block, CO_UI_OFF, CO_UI_KEPT)) g_ui[block].layout = CO_UI_KEPT;
}

#ifdef CO_TEST
void co_ui_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_UI_SITES; i++) memcpy(image + kCoUiSites[i].rva, kCoUiSites[i].game, kCoUiSites[i].size);
}

void co_ui_test_reset(void)
{
    for (unsigned block = 0; block < CO_UI_BLOCKS; block++) Release(&g_ui[block]);
    memset(g_ui, 0, sizeof g_ui);
    if (g_keptRoom) VirtualFree((void*)g_keptRoom, 0, MEM_RELEASE);
    g_keptRoom = g_game = 0;
}

/* Sets every block's layout without writing: for other steps' tests only. */
void co_ui_test_force(int layout)
{
    for (unsigned block = 0; block < CO_UI_BLOCKS; block++) g_ui[block].layout = layout;
}

int co_ui_test_layout(unsigned block) { return g_ui[block].layout; }
uintptr_t co_ui_test_table(unsigned block) { return g_ui[block].table; }
uintptr_t co_ui_test_room(unsigned block) { return block < CO_UI_BLOCKS ? g_ui[block].room : g_keptRoom; }
unsigned co_ui_test_sites(unsigned block)
{
    unsigned n = 0;
    for (size_t i = 0; i < CO_UI_SITES; i++) n += kCoUiSites[i].block == block;
    return n;
}
/* The sites whose bytes KEPT changes. */
unsigned co_ui_test_kept(void)
{
    unsigned n = 0;
    for (size_t i = 0; i < CO_UI_SITES; i++) n += kCoUiSites[i].kept != CO_UI_GAME;
    return n;
}
#endif
