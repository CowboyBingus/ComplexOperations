/* Eight players: the session's peer lists, second step. With the neighbours moved (co_session.c) the lists can hold
 * eight entries; this makes the code count to eight. Most sites are one instruction with a four in it. Code the
 * compiler unrolled for four entries gets the other four from the mod: a cave beside the game's code, or a hook of the
 * whole function. The join gate counts to the build's player count while the player managers have eight slots, else
 * to four.
 * The same sites carry the guards: tables the game keeps at four entries skip a player index of four or more, and a
 * message whose list is longer than its handler's buffer is ignored. */
#include <string.h>
#include <windows.h>

#include "co.h"

typedef struct CoCountSite { uint32_t rva; uint8_t size, skip, group; uint8_t old[16], now[16]; } CoCountSite;
typedef struct CoCountClear { uint32_t rva; uint8_t mov, land; } CoCountClear;
typedef struct CoCountCave
{
    uint32_t rva;
    uint8_t size, group, replaced[12];
    uint16_t code, length;
    uint16_t jumps[3];        /* where the cave's jumps into the game have their rel32; 0 ends the list */
    uint16_t callAt;          /* where the callback's address goes */
    uint8_t callback;         /* its number in kCountCallbacks; 0xFF for none */
} CoCountCave;
/* What a cave's callback sees: the registers as the game had them, in the order the cave pushed them. */
typedef struct CoRegs { uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rbx, rdx, rcx, rax, flags; } CoRegs;
#include "co_count_sites.h"

#define CO_COUNT_SITES (sizeof kCoCountSites / sizeof kCoCountSites[0])
#define CO_COUNT_CLEARS (sizeof kCoCountClears / sizeof kCoCountClears[0])
#define CO_COUNT_CAVES (sizeof kCoCountCaves / sizeof kCoCountCaves[0])
#define CO_CAVE_ROOM 0x2000u
_Static_assert(sizeof kCoCountCode <= CO_CAVE_ROOM, "the caves' code must fit their room");
/* A site's or cave's group. Guards go in and out with the counts: a table that stays at four entries is safe only
 * while every access to it is guarded. The rest depend on the build. The loadout screen's eight records and cards
 * (CO_G_LOADOUT) replace the guards that keep a player index above 3 off its four-entry arrays (CO_G_GUARD4). The
 * per-peer loadout table's bounds (CO_G_SYNCED) are safe only while the table is in the mod's room for eight. The score
 * manager's loot caves (CO_G_LOOT) ask the player managers' state each time, so they fit both of its states. */
enum { CO_G_COUNT, CO_G_GUARD, CO_G_NET, CO_G_EIGHT, CO_G_TEST, CO_G_LOADOUT, CO_G_GUARD4, CO_G_SYNCED, CO_G_LOOT };
/* Test builds only (--test-first-slot): the host gives joining peers slots and player indexes from this number on,
 * so that two game instances reach the code for players 5 to 8. Zero is off. */
#ifndef CO_TEST_FIRST_SLOT
#define CO_TEST_FIRST_SLOT 0
#endif
_Static_assert(CO_PLAYERS == 4 || CO_PLAYERS == 8, "a session takes four or eight players");
_Static_assert(CO_TEST_FIRST_SLOT == 0 || (CO_PLAYERS == 8 && CO_TEST_FIRST_SLOT >= 1 && CO_TEST_FIRST_SLOT <= 7),
               "the test option needs a build for eight players and a slot from 1 to 7");
#define CO_PEERS 8u
#define CO_PLAYER_INDEXES ((uint32_t)CO_PLAYERS)   /* player indexes that count as valid */
#define CO_FIRST_SLOT ((uint32_t)(CO_TEST_FIRST_SLOT ? CO_TEST_FIRST_SLOT : 1))
#define CO_FIRST_INDEX ((uint32_t)CO_TEST_FIRST_SLOT)
#define CO_HOST_SYNC 0x159B0u     /* in the session context: the host's synchronizer, peer slots at +0x20 */
#define CO_SLOT 0x38u
#define CO_RECORDS 0x200u         /* in the extension: party records, 0x1C0 each */
#define CO_RECORD 0x1C0u
#define CO_VALUES 0xD8u           /* in the extension: peer values, 16 bytes each with the key first */
#define CO_SLOTS_READY 0x108B600u
#define CO_SLOTS_FREE 0x108B670u
#define CO_PEER_GONE 0x108B6B0u
/* The score manager's loot rows of 0x18 bytes: by score slot and by player index, four each in the game's place, eight
 * each behind the manager once it has moved (gen_manager_sites.py). */
#define CO_LOOT_ROW 0x18u
#define CO_LOOT_SLOTS 0xC440u
#define CO_LOOT_INDEXES 0xC4A0u
#define CO_LOOT_SLOTS_MOVED 0x37F20u
#define CO_LOOT_INDEXES_MOVED 0x37FE0u
#define CO_LOOT_COUNT 0xCu        /* in a row: the count the sums add */

typedef uint32_t (*SlotsFn)(uintptr_t);
typedef uint64_t (*PeerGoneFn)(uintptr_t, uint64_t, uint64_t, uint64_t);
static SlotsFn g_slotsReady, g_slotsFree;
static PeerGoneFn g_peerGone;
static uintptr_t g_game, g_context, g_extension, g_caves;
static int g_on, g_hooks = -1;
static uint32_t g_ignored;        /* a bit for each message shape that was logged as ignored */
static int g_toldFour;            /* the join limit's fall back to four was logged */
static uintptr_t g_syncedOld, g_syncedNew, g_syncedBlock;

static int Active(uint8_t group)
{
    return group <= CO_G_NET || group == CO_G_LOOT || ((group == CO_G_EIGHT || group == CO_G_LOADOUT || group == CO_G_SYNCED) && CO_PLAYERS == 8) ||
           (group == CO_G_GUARD4 && CO_PLAYERS == 4) || (group == CO_G_TEST && CO_TEST_FIRST_SLOT != 0);
}

static uint64_t* Slot(uintptr_t sync, uint32_t slot) { return (uint64_t*)(sync + 0x20u + slot * CO_SLOT); }
static uint32_t SlotField(uintptr_t sync, uint32_t slot, uint32_t field) { return *(uint32_t*)(sync + 0x20u + slot * CO_SLOT + field); }

/* A party record whose key is the peer: the game looked through records 0 to 3 and found none. */
static uint64_t FindRecord(uint64_t key)
{
    for (uint32_t i = 4; i < CO_PEERS; i++)
    {
        uintptr_t record = g_extension + CO_RECORDS + i * CO_RECORD;
        if (*(uint64_t*)record == key) return record + 8u;
    }
    return 0;
}

static void FindByRsi(CoRegs* regs) { if (regs->rcx == 0) regs->rcx = FindRecord(regs->rsi); }
static void FindByR11(CoRegs* regs) { if (regs->rcx == 0) regs->rcx = FindRecord(regs->r11); }

/* Peers a session takes. Players 5 to 8 need the managers' eight slots; without them a build for eight takes four. */
static uint32_t JoinLimit(void) { return CO_PLAYERS == 8 && co_managers_moved() ? 8u : 4u; }

/* The host's own slot always counts as used. A joining peer gets a slot from CO_FIRST_SLOT on, so only those are
 * open to it. */
static uint32_t FreeSlots(uintptr_t sync)
{
    uint32_t used = 1, open = 0;
    for (uint32_t i = 1; i < CO_PEERS; i++)
    {
        if (*Slot(sync, i) != 0) used++;
        else if (i >= CO_FIRST_SLOT) open++;
    }
    const uint32_t limit = JoinLimit();
    const uint32_t left = used < limit ? limit - used : 0;
    if (left == 0 && limit < (uint32_t)CO_PLAYERS && !g_toldFour)
    {
        g_toldFour = 1;
        co_log("session: the session is full at four players: the player managers have four slots");
    }
    return left < open ? left : open;
}

/* r9 is the host's synchronizer here; the game goes on with the free slots in ecx. */
static void JoinGate(CoRegs* regs) { regs->rcx = FreeSlots((uintptr_t)regs->r9); }

static void ClearValueKeys(CoRegs* regs)
{
    (void)regs;
    for (uint32_t i = 4; i < CO_PEERS; i++) *(uint64_t*)(g_extension + CO_VALUES + i * 16u) = 0;
}

/* rbx is the session context. The game's chain over slots 0 to 3 leaves the answer of the last occupied one. */
static void LastSlotState(CoRegs* regs)
{
    uintptr_t sync = (uintptr_t)regs->rbx + CO_HOST_SYNC;
    uint64_t answer = 0;
    for (uint32_t i = 0; i < CO_PEERS; i++)
        if (*Slot(sync, i) != 0) answer = SlotField(sync, i, 0x20) - 6u <= 2u;
    regs->rax = answer;
}

/* rax is the handler's number, rdx its arguments. A list longer than the handler's buffer would be copied over the
 * stack behind it: the cave returns from the handler when this leaves rax set. */
static void MessageFits(CoRegs* regs)
{
    const uint32_t shape = (uint32_t)regs->rax;
    const uint8_t* arguments = (const uint8_t*)regs->rdx;
    uint32_t at = 0;
    regs->rax = 0;
    for (const uint8_t* s = kCoMessageShapes[shape]; *s != 0xFF; s++)
    {
        if (*s == 0) { at++; continue; }
        const uint32_t count = *(const uint32_t*)(arguments + at * 16u + 8u);
        if (count > *s)
        {
            regs->rax = 1;
            if (!(g_ignored & 1u << shape))
            {
                g_ignored |= 1u << shape;
                co_log("session: a message with a list of %u entries was ignored; its handler at game+0x%X holds %u", count, kCoMessageHandlers[shape], (unsigned)*s);
            }
            return;
        }
        at += 1u + count;
    }
}

/* rsi is the first slot. The game takes the first free slot; this one starts at CO_FIRST_SLOT and takes a lower one
 * only when none is left there. rdx is the slot, as the game's own search leaves it. */
static void JoinSlot(CoRegs* regs)
{
    const uintptr_t sync = (uintptr_t)regs->rsi - 0x20u;
    regs->rdx = 0;
    for (uint32_t n = 0; n < CO_PEERS - 1u; n++)
    {
        const uint32_t i = n + CO_FIRST_SLOT < CO_PEERS ? n + CO_FIRST_SLOT : n + CO_FIRST_SLOT - CO_PEERS + 1u;
        if (*Slot(sync, i) == 0) { regs->rdx = (uint64_t)(uintptr_t)Slot(sync, i); return; }
    }
}

/* r13 is the index flags, one byte for each player index. The first free index from CO_FIRST_INDEX on is taken and
 * left in r8d, as the game's search does from zero; -1 when all eight are taken. */
static void JoinIndex(CoRegs* regs)
{
    uint8_t* flags = (uint8_t*)(uintptr_t)regs->r13;
    regs->r8 = 0xFFFFFFFFu;
    for (uint32_t n = 0; n < CO_PEERS; n++)
    {
        const uint32_t i = (n + CO_FIRST_INDEX) % CO_PEERS;
        if (flags[i] == 0) { flags[i] = 1; regs->r8 = i; return; }
    }
}

static uint32_t LootRows(void) { return co_managers_moved() ? 8u : 4u; }

/* rax is the score manager plus the old place of the rows: every row is cleared where the rows are now. */
static void ClearLootSlots(CoRegs* regs)
{
    const uintptr_t manager = (uintptr_t)regs->rax - CO_LOOT_SLOTS;
    memset((void*)(manager + (co_managers_moved() ? CO_LOOT_SLOTS_MOVED : CO_LOOT_SLOTS)), 0, LootRows() * CO_LOOT_ROW);
}

static void ClearLootIndexes(CoRegs* regs)
{
    const uintptr_t manager = (uintptr_t)regs->rax - CO_LOOT_INDEXES;
    memset((void*)(manager + (co_managers_moved() ? CO_LOOT_INDEXES_MOVED : CO_LOOT_INDEXES)), 0, LootRows() * CO_LOOT_ROW);
}

/* rax is the score manager; the game's code wrote this sum out for four rows. It goes back in eax. */
static void SumLoot(CoRegs* regs)
{
    const uintptr_t rows = (uintptr_t)regs->rax + (co_managers_moved() ? CO_LOOT_SLOTS_MOVED : CO_LOOT_SLOTS);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < LootRows(); i++) sum += *(const uint32_t*)(rows + i * CO_LOOT_ROW + CO_LOOT_COUNT);
    regs->rax = sum;
}

/* eax is a score slot or a player index: the carry flag says it has a loot row, for the cave's jae. */
static void LootIndex(CoRegs* regs)
{
    regs->flags = (regs->flags & ~(uint64_t)1) | ((uint32_t)regs->rax < LootRows());
}

/* By the numbers in gen_count_sites.py; 8 to 13 are kept for other caves. */
static void (*const kCountCallbacks[])(CoRegs*) = {
    FindByRsi, FindByR11, JoinGate, ClearValueKeys, LastSlotState, MessageFits, JoinSlot, JoinIndex,
    [14] = ClearLootSlots, ClearLootIndexes, SumLoot, LootIndex,
};
#define CO_CALLBACKS (sizeof kCountCallbacks / sizeof kCountCallbacks[0])

/* The session init clears the keys 1 to 3 itself; its hook (co_session.c) calls this behind it. */
void co_counts_session_init(void) { if (g_on) ClearValueKeys(NULL); }

/* Slots with a peer in state 5 or later and a player index. */
static uint32_t SlotsReadyDetour(uintptr_t sync)
{
    if (!g_on) return g_slotsReady(sync);
    uint32_t ready = 0;
    for (uint32_t i = 0; i < CO_PEERS; i++)
        ready += *Slot(sync, i) != 0 && SlotField(sync, i, 0x20) >= 5 && SlotField(sync, i, 0x14) < CO_PLAYER_INDEXES;
    return ready;
}

static uint32_t SlotsFreeDetour(uintptr_t sync) { return g_on ? FreeSlots(sync) : g_slotsFree(sync); }

/* A peer left: the game takes it out of the party records 0 to 3; the same for records 4 to 7. A record lists peer
 * keys (count at +8, keys from +0x10) and 32-byte entries (count at +0x38, entries from +0x40, key first); a record
 * without keys is zeroed. */
static uint64_t PeerGoneDetour(uintptr_t sync, uint64_t key, uint64_t a3, uint64_t a4)
{
    uint64_t result = g_peerGone(sync, key, a3, a4);
    if (!g_on) return result;
    for (uint32_t i = 4; i < CO_PEERS; i++)
    {
        uint8_t* record = (uint8_t*)(g_extension + CO_RECORDS + i * CO_RECORD);
        uint32_t* keys = (uint32_t*)(record + 8);
        uint32_t* entries = (uint32_t*)(record + 0x38);
        for (uint32_t j = 0; j < *keys; j++)
            if (*(uint64_t*)(record + 0x10 + j * 8u) == key)
            {
                --*keys;
                *(uint64_t*)(record + 0x10 + j * 8u) = *(uint64_t*)(record + 0x10 + *keys * 8u);
                j--;
            }
        for (uint32_t j = 0; j < *entries; j++)
            if (*(uint64_t*)(record + 0x40 + j * 0x20u) == key)
            {
                --*entries;
                memmove(record + 0x40 + j * 0x20u, record + 0x40 + *entries * 0x20u, 0x20);
                j--;
            }
        if (*keys == 0) memset(record, 0, CO_RECORD);
    }
    return result;
}

/* A site's bytes as the game has them now: the reviewed ones, but for the distance the session step wrote. */
static int SiteMatches(const CoCountSite* s)
{
    uint8_t bytes[16];
    if (g_coApi->read(g_game + s->rva, bytes, s->size) != 0) return 0;
    for (uint8_t i = 0; i < s->size; i++)
        if (!(s->skip != 0xFF && i >= s->skip && i < s->skip + 4) && bytes[i] != s->old[i]) return 0;
    return 1;
}

static int WriteSite(const CoCountSite* s, const uint8_t* to)
{
    if (s->skip == 0xFF) return g_coApi->write(g_game + s->rva, to, s->size);
    if (s->skip && g_coApi->write(g_game + s->rva, to, s->skip) != 0) return -1;
    return s->skip + 4 < s->size ? g_coApi->write(g_game + s->rva + s->skip + 4u, to + s->skip + 4, s->size - s->skip - 4u) : 0;
}

/* xor eax,eax; mov [reg+d],eax <-> and qword [reg+d],0: the distance moves one byte forward. */
static int WriteClear(const CoCountClear* c, int eight)
{
    uint8_t bytes[8], to[8];
    if (g_coApi->read(g_game + c->rva, bytes, 8) != 0) return -1;
    if (eight)
    {
        if (bytes[0] != 0x33 || bytes[1] != 0xC0 || bytes[2] != 0x89 || bytes[3] != c->mov) return -1;
        to[0] = 0x48; to[1] = 0x83; to[2] = c->land;
        memcpy(to + 3, bytes + 4, 4);
        to[7] = 0;
    }
    else
    {
        if (bytes[0] != 0x48 || bytes[1] != 0x83 || bytes[2] != c->land || bytes[7] != 0) return -1;
        to[0] = 0x33; to[1] = 0xC0; to[2] = 0x89; to[3] = c->mov;
        memcpy(to + 4, bytes + 3, 4);
    }
    return g_coApi->write(g_game + c->rva, to, 8);
}

static void Back(size_t sites, size_t clears, size_t caves)
{
    for (size_t i = 0; i < caves; i++)
        if (Active(kCoCountCaves[i].group)) g_coApi->write(g_game + kCoCountCaves[i].rva, kCoCountCaves[i].replaced, kCoCountCaves[i].size);
    for (size_t i = 0; i < clears; i++) WriteClear(&kCoCountClears[i], 0);
    for (size_t i = 0; i < sites; i++)
        if (Active(kCoCountSites[i].group)) WriteSite(&kCoCountSites[i], kCoCountSites[i].old);
}

/* How many sites and caves of a group there are; `caves` gets the caves alone. */
static unsigned InGroup(uint8_t group, unsigned* caves)
{
    unsigned sites = 0, found = 0;
    for (size_t i = 0; i < CO_COUNT_SITES; i++) sites += kCoCountSites[i].group == group;
    for (size_t i = 0; i < CO_COUNT_CAVES; i++) found += kCoCountCaves[i].group == group;
    if (caves != NULL) *caves = found;
    return sites + found;
}

/* The engine's per-peer loadout table: four entries of 0x9F8 (a record, the peer's id behind it) inline in the engine
 * object, so it cannot grow where it is. The game reaches it by one pointer and three registry entries, all set at
 * startup before the first apply; this gives them a block of eight entries with the four copied, at the old address's
 * place modulo 16. Undo puts the pointer and the entries back after the sites; the loader keeps no record of
 * these writes for a mod that is not live. */
typedef struct CoRegistryEntry { uint64_t at; uint32_t type, pad; } CoRegistryEntry;
_Static_assert(sizeof(CoRegistryEntry) == 16, "a registry entry is 16 bytes");
#define CO_REGISTRY_ROOM 0x51Bu   /* entries the largest list has room for; a higher count is not the reviewed registry */

/* The registry entries of the table at `table`: one in each list, or 0. */
static int FindEntries(uintptr_t table, uintptr_t entries[3])
{
    uintptr_t registry = 0;
    if (g_coApi->read(g_game + CO_SYNCED_REGISTRY, &registry, sizeof registry) != 0 || registry == 0) return 0;
    for (size_t list = 0; list < 3; list++)
    {
        uint32_t count = 0;
        entries[list] = 0;
        if (g_coApi->read(registry + kCoSyncedLists[list], &count, 4) != 0 || count > CO_REGISTRY_ROOM) return 0;
        for (uint32_t i = 0; i < count; i++)
        {
            CoRegistryEntry entry;
            const uintptr_t at = registry + kCoSyncedLists[list] + 8u + i * (uintptr_t)sizeof entry;
            if (g_coApi->read(at, &entry, sizeof entry) != 0) return 0;
            if (entry.at != table || entry.type != CO_SYNCED_TYPE) continue;
            if (entries[list] != 0) return 0;
            entries[list] = at;
        }
        if (entries[list] == 0) return 0;
    }
    return 1;
}

/* The pointer and the registry entries lead from `from` to `to`; a failed write puts back the ones before it. */
static int Repoint(uintptr_t from, uintptr_t to, const uintptr_t entries[3])
{
    const uint64_t old = from, now = to;
    if (g_coApi->write(g_game + CO_SYNCED_POINTER, &now, 8) != 0) return 0;
    for (size_t i = 0; i < 3; i++)
        if (g_coApi->write(entries[i], &now, 8) != 0)
        {
            while (i--) g_coApi->write(entries[i], &old, 8);
            g_coApi->write(g_game + CO_SYNCED_POINTER, &old, 8);
            return 0;
        }
    return 1;
}

static int SyncedMove(void)
{
    uintptr_t table = 0, engine = 0, entries[3];
    if (g_coApi->read(g_game + CO_SYNCED_POINTER, &table, sizeof table) != 0 || g_coApi->read(g_game + CO_SYNCED_ENGINE, &engine, sizeof engine) != 0 ||
        table == 0 || engine == 0 || table != engine + CO_SYNCED_MEMBER || !FindEntries(table, entries))
    { co_log("players: the per-peer loadout table is not where the game keeps it"); return 0; }
    g_syncedBlock = (uintptr_t)VirtualAlloc(NULL, CO_SYNCED_NEW_SIZE + 16u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (g_syncedBlock == 0) { co_log("players: no memory for the per-peer loadout table"); return 0; }
    g_syncedNew = g_syncedBlock + (table & 15u);
    if (g_coApi->read(table, (void*)g_syncedNew, CO_SYNCED_OLD_SIZE) != 0 || !Repoint(table, g_syncedNew, entries))
    {
        VirtualFree((void*)g_syncedBlock, 0, MEM_RELEASE);
        g_syncedBlock = g_syncedNew = 0;
        co_log("players: the per-peer loadout table could not be moved");
        return 0;
    }
    g_syncedOld = table;
    return 1;
}

/* Entries 0 to 3 go back to the game's table; a pointer or entry that no longer leads to the mod's block is left. */
static void SyncedBack(void)
{
    uintptr_t table = 0, entries[3];
    const uint64_t old = g_syncedOld;
    if (g_syncedBlock == 0) return;
    if (g_coApi->read(g_game + CO_SYNCED_POINTER, &table, sizeof table) == 0 && table == g_syncedNew)
    {
        memcpy((void*)g_syncedOld, (const void*)g_syncedNew, CO_SYNCED_OLD_SIZE);
        g_coApi->write(g_game + CO_SYNCED_POINTER, &old, 8);
    }
    if (FindEntries(g_syncedNew, entries))
        for (size_t i = 0; i < 3; i++) g_coApi->write(entries[i], &old, 8);
    VirtualFree((void*)g_syncedBlock, 0, MEM_RELEASE);
    g_syncedBlock = g_syncedNew = g_syncedOld = 0;
}

/* The session step undoes its sites by writing distances back; this goes first, so they are where it expects them.
 * The table goes back behind the sites that bound it. */
void co_counts_undo(void)
{
    if (!g_on) return;
    g_on = 0;
    Back(CO_COUNT_SITES, CO_COUNT_CLEARS, CO_COUNT_CAVES);
    SyncedBack();
    if (g_caves) VirtualFree((void*)g_caves, 0, MEM_RELEASE);
    g_caves = 0;
}

/* Called by the session step once the arrays are moved and their old places zeroed. */
void co_counts_apply(uintptr_t context, uintptr_t extension)
{
    static const char kStays[] = "the session lists count to four as before";
    if (g_on || g_hooks != 1 || g_game == 0) return;
    for (size_t i = 0; i < CO_COUNT_SITES; i++)
        if (Active(kCoCountSites[i].group) && !SiteMatches(&kCoCountSites[i]))
        { co_log("session: the code at game+0x%X is not the reviewed one; %s", kCoCountSites[i].rva, kStays); return; }
    for (size_t i = 0; i < CO_COUNT_CAVES; i++)
    {
        uint8_t bytes[12];
        const CoCountCave* c = &kCoCountCaves[i];
        if (!Active(c->group)) continue;
        if (g_coApi->read(g_game + c->rva, bytes, c->size) != 0 || memcmp(bytes, c->replaced, c->size) != 0)
        { co_log("session: the code at game+0x%X is not the reviewed one; %s", c->rva, kStays); return; }
        if (c->callback != 0xFF && (c->callback >= CO_CALLBACKS || kCountCallbacks[c->callback] == NULL))
        { co_log("session: the cave at game+0x%X has no callback %u; %s", c->rva, (unsigned)c->callback, kStays); return; }
    }
    g_caves = co_alloc_near(g_game + 0x1000000u, CO_CAVE_ROOM);
    if (g_caves == 0 || sizeof kCoCountCode > CO_CAVE_ROOM) { co_log("session: no free memory near the game's code; %s", kStays); return; }
    memcpy((void*)g_caves, kCoCountCode, sizeof kCoCountCode);
    for (size_t i = 0; i < CO_COUNT_CAVES; i++)
    {
        const CoCountCave* c = &kCoCountCaves[i];
        uint8_t* cave = (uint8_t*)(g_caves + c->code);
        /* Assembled with the cave and the game both at zero: a jump into the game is off by their real distance. */
        for (size_t j = 0; j < 3 && c->jumps[j]; j++)
        {
            int32_t rel;
            memcpy(&rel, cave + c->jumps[j], 4);
            int64_t real = (int64_t)rel + (int64_t)g_game - (int64_t)(uintptr_t)cave;
            if (real < INT32_MIN || real > INT32_MAX)
            { VirtualFree((void*)g_caves, 0, MEM_RELEASE); g_caves = 0; co_log("session: the game's code is out of reach; %s", kStays); return; }
            rel = (int32_t)real;
            memcpy(cave + c->jumps[j], &rel, 4);
        }
        if (c->callback != 0xFF)
        {
            uint64_t callback = (uint64_t)(uintptr_t)kCountCallbacks[c->callback];
            memcpy(cave + c->callAt, &callback, 8);
        }
    }
    DWORD before;
    if (!VirtualProtect((void*)g_caves, CO_CAVE_ROOM, PAGE_EXECUTE_READ, &before))
    { VirtualFree((void*)g_caves, 0, MEM_RELEASE); g_caves = 0; co_log("session: the caves could not be made executable; %s", kStays); return; }
    /* The table moves before any site bounds it by eight. */
    if (Active(CO_G_SYNCED) && !SyncedMove())
    { VirtualFree((void*)g_caves, 0, MEM_RELEASE); g_caves = 0; co_log("session: %s", kStays); return; }
    g_context = context;
    g_extension = extension;
    /* The game cannot run on a mix: whatever is written goes back when one write fails. */
    for (size_t i = 0; i < CO_COUNT_SITES; i++)
        if (Active(kCoCountSites[i].group) && WriteSite(&kCoCountSites[i], kCoCountSites[i].now) != 0)
        { Back(i, 0, 0); goto failed; }
    for (size_t i = 0; i < CO_COUNT_CLEARS; i++)
        if (WriteClear(&kCoCountClears[i], 1) != 0)
        { Back(CO_COUNT_SITES, i, 0); goto failed; }
    for (size_t i = 0; i < CO_COUNT_CAVES; i++)
    {
        const CoCountCave* c = &kCoCountCaves[i];
        uint8_t jump[12];
        if (!Active(c->group)) continue;
        int32_t to = (int32_t)((int64_t)(g_caves + c->code) - (int64_t)(g_game + c->rva + 5u));
        memset(jump, 0x90, sizeof jump);
        jump[0] = 0xE9;
        memcpy(jump + 1, &to, 4);
        if (g_coApi->write(g_game + c->rva, jump, c->size) != 0)
        { Back(CO_COUNT_SITES, CO_COUNT_CLEARS, i); goto failed; }
    }
    g_on = 1;
    {
        unsigned caves;
        const unsigned counts = InGroup(CO_G_COUNT, &caves) + (unsigned)CO_COUNT_CLEARS;
        co_log("session: the session lists count to eight (%u code sites changed, %u through caves); a session %s %u players",
               counts, caves, CO_PLAYERS == 8 ? "takes" : "still takes", (unsigned)CO_PLAYERS);
        co_log("players: %u accesses to four-entry tables of the game skip a player index above 3",
               InGroup(CO_G_GUARD, NULL) + (Active(CO_G_GUARD4) ? InGroup(CO_G_GUARD4, NULL) : 0u));
        co_log("session: %u guards keep the lists in messages inside the game's buffers", InGroup(CO_G_NET, NULL));
        co_log("players: the score manager's loot rows are cleared, added and bounded where the managers keep them (%u caves)",
               InGroup(CO_G_LOOT, NULL));
        if (Active(CO_G_EIGHT))
            co_log("session: player indexes 4 to 7 are valid, the SOS beacon counts to eight and the lobby takes eight (%u code sites changed)",
                   InGroup(CO_G_EIGHT, NULL));
        if (Active(CO_G_LOADOUT))
            co_log("players: the loadout screen has eight records and cards (%u code sites changed)", InGroup(CO_G_LOADOUT, NULL));
        if (Active(CO_G_SYNCED))
            co_log("players: the per-peer loadout table moved to room for eight peers (%u code sites changed)", InGroup(CO_G_SYNCED, NULL));
    }
    return;
failed:
    SyncedBack();
    VirtualFree((void*)g_caves, 0, MEM_RELEASE);
    g_caves = 0;
    co_log("session: a count site could not be written; %s", kStays);
}

/* The hooks stay for good and pass through while the counts are off. */
void co_counts_install(uintptr_t game)
{
    g_game = game;
    if (g_coApi->code_matches(game + CO_SLOTS_READY, "33 C0 48 39 41 20 74 12 83 79 40 05") != 1 ||
        g_coApi->code_matches(game + CO_SLOTS_FREE, "33 C0 4C 8B C1 48 39 41 58 0F 95 C0") != 1 ||
        g_coApi->code_matches(game + CO_PEER_GONE, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC E0 01 00 00") != 1)
    { g_hooks = 0; co_log("session: this is not the game code the mod was made for (a game update or another mod); the session lists count to four as before"); return; }
    if (g_coApi->hook(g_coMod, game + CO_SLOTS_READY, (void*)SlotsReadyDetour, (void**)&g_slotsReady) != 0 ||
        g_coApi->hook(g_coMod, game + CO_SLOTS_FREE, (void*)SlotsFreeDetour, (void**)&g_slotsFree) != 0 ||
        g_coApi->hook(g_coMod, game + CO_PEER_GONE, (void*)PeerGoneDetour, (void**)&g_peerGone) != 0)
    { g_hooks = 0; co_log("session: a function of the session lists could not be hooked; the session lists count to four as before"); return; }
    g_hooks = 1;
    if (CO_TEST_FIRST_SLOT != 0)
        co_log("session: TEST BUILD, not for play: as host, joining peers get slots and player indexes from %u on", CO_FIRST_SLOT);
}

#ifdef CO_TEST
void co_counts_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < CO_COUNT_SITES; i++) memcpy(image + kCoCountSites[i].rva, kCoCountSites[i].old, kCoCountSites[i].size);
    for (size_t i = 0; i < CO_COUNT_CAVES; i++) memcpy(image + kCoCountCaves[i].rva, kCoCountCaves[i].replaced, kCoCountCaves[i].size);
    for (size_t i = 0; i < CO_COUNT_CLEARS; i++)
    {
        const uint8_t head[] = {0x33, 0xC0, 0x89, kCoCountClears[i].mov};
        memcpy(image + kCoCountClears[i].rva, head, sizeof head);
    }
    static const uint8_t kReady[] = {0x33, 0xC0, 0x48, 0x39, 0x41, 0x20, 0x74, 0x12, 0x83, 0x79, 0x40, 0x05};
    static const uint8_t kFree[] = {0x33, 0xC0, 0x4C, 0x8B, 0xC1, 0x48, 0x39, 0x41, 0x58, 0x0F, 0x95, 0xC0};
    static const uint8_t kGone[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x81, 0xEC, 0xE0, 0x01, 0x00, 0x00};
    memcpy(image + CO_SLOTS_READY, kReady, sizeof kReady);
    memcpy(image + CO_SLOTS_FREE, kFree, sizeof kFree);
    memcpy(image + CO_PEER_GONE, kGone, sizeof kGone);
}

void co_counts_test_reset(void)
{
    if (g_syncedBlock) VirtualFree((void*)g_syncedBlock, 0, MEM_RELEASE);
    g_syncedBlock = g_syncedNew = g_syncedOld = 0;
    if (g_caves) VirtualFree((void*)g_caves, 0, MEM_RELEASE);
    g_caves = g_game = 0;
    g_on = 0;
    g_hooks = -1;
    g_ignored = 0;
    g_toldFour = 0;
}

/* Every site, clear and cave site holds the game's bytes (a moved array's distance aside). */
int co_counts_test_reviewed(const uint8_t* image)
{
    for (size_t i = 0; i < CO_COUNT_SITES; i++)
        for (uint8_t j = 0; j < kCoCountSites[i].size; j++)
            if (!(kCoCountSites[i].skip != 0xFF && j >= kCoCountSites[i].skip && j < kCoCountSites[i].skip + 4) &&
                image[kCoCountSites[i].rva + j] != kCoCountSites[i].old[j]) return 0;
    for (size_t i = 0; i < CO_COUNT_CLEARS; i++)
        if (image[kCoCountClears[i].rva] != 0x33 || image[kCoCountClears[i].rva + 2] != 0x89) return 0;
    for (size_t i = 0; i < CO_COUNT_CAVES; i++)
        if (memcmp(image + kCoCountCaves[i].rva, kCoCountCaves[i].replaced, kCoCountCaves[i].size) != 0) return 0;
    return 1;
}

unsigned co_counts_test_guards(void) { return InGroup(CO_G_GUARD, NULL) + (Active(CO_G_GUARD4) ? InGroup(CO_G_GUARD4, NULL) : 0u); }
unsigned co_counts_test_loadout(void) { return InGroup(CO_G_LOADOUT, NULL); }
unsigned co_counts_test_synced(void) { return InGroup(CO_G_SYNCED, NULL); }
/* Where the per-peer loadout table is now: the mod's block, or 0 while it is the game's. */
uintptr_t co_counts_test_synced_table(void) { return g_syncedNew; }
/* The sites, clears and caves this build writes. */
unsigned co_counts_test_sites(void)
{
    unsigned n = (unsigned)CO_COUNT_CLEARS;
    for (uint8_t group = CO_G_COUNT; group <= CO_G_LOOT; group++)
        if (Active(group)) n += InGroup(group, NULL);
    return n;
}
unsigned co_counts_test_net(void) { return InGroup(CO_G_NET, NULL); }
int co_counts_test_players(void) { return CO_PLAYERS; }
int co_counts_test_first_slot(void) { return CO_TEST_FIRST_SLOT; }
int co_counts_test_fits(uint32_t shape, const void* arguments)
{
    CoRegs regs = {0};
    regs.rax = shape; regs.rdx = (uint64_t)(uintptr_t)arguments;
    MessageFits(&regs);
    return regs.rax == 0;
}
/* The slot number a joining peer gets (-1 for none), and its player index; the index is marked as taken. */
int co_counts_test_join_slot(uintptr_t sync)
{
    CoRegs regs = {0};
    regs.rsi = sync + 0x20u;
    JoinSlot(&regs);
    return regs.rdx == 0 ? -1 : (int)((regs.rdx - sync - 0x20u) / CO_SLOT);
}
uint32_t co_counts_test_join_index(uint8_t* flags)
{
    CoRegs regs = {0};
    regs.r13 = (uint64_t)(uintptr_t)flags;
    JoinIndex(&regs);
    return (uint32_t)regs.r8;
}
/* The number of the write that changes the site or cave at rva, from the first of the counts (the table move's
 * writes first); -1 when this build does not write it. */
/* The writes of the table move (the pointer and three registry entries), made before the first site. */
int co_counts_test_move_writes(void) { return Active(CO_G_SYNCED) ? 4 : 0; }
int co_counts_test_write_of(uint32_t rva)
{
    int n = co_counts_test_move_writes();
    for (size_t i = 0; i < CO_COUNT_SITES; i++)
    {
        const CoCountSite* s = &kCoCountSites[i];
        if (!Active(s->group)) continue;
        if (s->rva == rva) return n;
        n += s->skip == 0xFF ? 1 : (s->skip != 0) + (s->skip + 4 < s->size);
    }
    n += (int)CO_COUNT_CLEARS;
    for (size_t i = 0; i < CO_COUNT_CAVES; i++)
        if (Active(kCoCountCaves[i].group))
        {
            if (kCoCountCaves[i].rva == rva) return n;
            n++;
        }
    return -1;
}
int co_counts_test_on(void) { return g_on; }
uintptr_t co_counts_test_caves(void) { return g_caves; }
uint32_t co_counts_test_ready(uintptr_t sync) { return SlotsReadyDetour(sync); }
uint32_t co_counts_test_free(uintptr_t sync) { return SlotsFreeDetour(sync); }
uint64_t co_counts_test_find(uint64_t key) { return FindRecord(key); }
void co_counts_test_gone(uintptr_t sync, uint64_t key) { PeerGoneDetour(sync, key, 0, 0); }
uint64_t co_counts_test_last(uintptr_t context) { CoRegs regs = {0}; regs.rbx = context; LastSlotState(&regs); return regs.rax; }
/* The score manager's loot callbacks, as their caves call them. */
void co_counts_test_clear_loot(uintptr_t manager, int byIndex)
{
    CoRegs regs = {0};
    regs.rax = manager + (byIndex ? CO_LOOT_INDEXES : CO_LOOT_SLOTS);
    (byIndex ? ClearLootIndexes : ClearLootSlots)(&regs);
}
uint32_t co_counts_test_sum_loot(uintptr_t manager) { CoRegs regs = {0}; regs.rax = manager; SumLoot(&regs); return (uint32_t)regs.rax; }
uint64_t co_counts_test_loot_index(uint32_t index, uint64_t flags) { CoRegs regs = {0}; regs.rax = index; regs.flags = flags; LootIndex(&regs); return regs.flags; }
#endif
