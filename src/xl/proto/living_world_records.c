/* On with the living world (HELLFORGE_LW_RECORDS=0 turns it off; KB xl-destruction-record-map-full): heavy extra large layouts register more
 * destructibles than the world's destruction records hold ([game+0x346D528]: the map +0xC5F48 with 0x8000 slots and
 * multiplier 1, the records +0xC5F78). The full map made every miss scan all slots (11 of 14.4 ms a frame), and the
 * counter's records ran past their 0x7000 into the other writers' records, a list (+0x1E2F90) and the counter itself. */
#define LW_REC_WORLD 0x346D528u
#define LW_REC_INSERT 0x13E0200u
#define LW_REC_RESET 0x13E1730u
#define LW_REC_BUILD 0x13DF7D0u
#define LW_REC_COUNTER 0x1EAF78u
#define LW_REC_MAP 0xC5F48u      /* keys pointer +0, capacity +8, empty key +0xC, multiplier +0x10 */
#define LW_REC_ROOM 0x7000u      /* the counter's records are 0x10000-0x16FFF; a ring (0x13E0760) has 0x17000 on */
#define LW_REC_SCATTER 0x9E3779B1u

typedef uintptr_t (*LwRecInsertFn)(uintptr_t a, uintptr_t entries, uint32_t count, uintptr_t d);
typedef void (*LwRecResetFn)(uintptr_t world);
static struct { void *insertOrig, *resetOrig, *builtOrig; volatile LONG dropped, told; LONG noted; uint32_t room; } g_lwRec;
static void LwRecScatter(uintptr_t world, const char* when);
static void LwTreeLevel(void); /* living_world_trees.c */

/* A level's first batch (counter 0): the map is cleared at each level by a path not found yet (not 0x13E1730), so the
 * multiplier is checked here too and set if that path reset it while the map is still empty. */
static void LwRecLevel(uintptr_t world)
{
    volatile uint32_t* map = (volatile uint32_t*)(world + LW_REC_MAP);
    const volatile uint32_t* keys = (const volatile uint32_t*)*(volatile uintptr_t*)(world + LW_REC_MAP);
    uint32_t used = 0;
    for (uint32_t i = 0; i < map[2] && i < 0x8000u; i++) used += keys[2 * i] != map[3];
    if (map[4] != LW_REC_SCATTER && used == 0) LwRecScatter(world, "found empty at a level's start");
    else Note("living world: destruction records: a level's first batch, multiplier 0x%X, %u keys before it, %ld left out last level",
        map[4], used, InterlockedExchange(&g_lwRec.dropped, 0));
    InterlockedExchange(&g_lwRec.told, 0);
    LwTreeLevel();
}

/* The one caller (0xACF8C0) passes a spawn batch's new destructibles, and the insert writes nothing back into them,
 * so the batch's tail past the room just gets no record: a lookup misses, as for the entries a full map lost before. */
static uintptr_t LwRecInsert(uintptr_t a, uintptr_t entries, uint32_t count, uintptr_t d)
{
    uintptr_t world;
    if (count && EvpPtr(g_lw.game + LW_REC_WORLD, &world))
    {
        uint32_t used = EvpU32(world + LW_REC_COUNTER, 0), room = used < g_lwRec.room ? g_lwRec.room - used : 0;
        if (used == 0) LwRecLevel(world);
        if (count > room)
        {
            InterlockedAdd(&g_lwRec.dropped, (LONG)(count - room));
            if (InterlockedExchange(&g_lwRec.told, 1) == 0)
                Note("living world: the destruction records are full (%u of %u): %u of this batch's %u destructibles get none", used, g_lwRec.room, count - room, count);
            count = room;
        }
    }
    return ((LwRecInsertFn)g_lwRec.insertOrig)(a, entries, count, d);
}

/* With the 30 s notes: the level's running count of destructibles without a record, when it has grown. */
static void LwRecNote(void)
{
    LONG dropped = g_lwRec.dropped;
    if (dropped > g_lwRec.noted) Note("living world: destruction records: %ld destructibles without a record so far this level", dropped);
    g_lwRec.noted = dropped;
}

/* Every probe of the map (21 functions, and the insert 0x172F790 through it) hashes with the multiplier field, so it
 * may change only while the map is empty: right after the constructor (0x13DF7D0, once at start-up) or the reset
 * clears it. Multiplier 1 left one long cluster; on a live level's 27303 keys 0x9E3779B1 makes a miss 11 probes
 * instead of 2316 and a hit 2 instead of 143 (rehash.py). */
static void LwRecScatter(uintptr_t world, const char* when)
{
    volatile uint32_t* map = (volatile uint32_t*)(world + LW_REC_MAP);
    if (map[2] == 0x8000u) map[4] = LW_REC_SCATTER;
    Note("living world: destruction records %s (%ld left out before); multiplier 0x%X", when, InterlockedExchange(&g_lwRec.dropped, 0), map[4]);
    InterlockedExchange(&g_lwRec.told, 0);
}

static void LwRecBuilt(uintptr_t world)
{
    ((LwRecResetFn)g_lwRec.builtOrig)(world);
    LwRecScatter(world, "built");
}

static void LwRecReset(uintptr_t world)
{
    ((LwRecResetFn)g_lwRec.resetOrig)(world);
    LwRecScatter(world, "reset");
}

/* The pins hold the facts the fix rests on: the record numbering (0x13E02C7), the insert's probe reading capacity and
 * multiplier (0x13E030B), the other writers' bases (0x13E078E, 0x13E0B77) and the constructor's capacity (0x13DF877). */
static void LwInstallRecords(void)
{
    static int done;
    char t[4] = "";
    if (done++) return;
    if (GetEnvironmentVariableA("HELLFORGE_LW_RECORDS", t, sizeof t) > 0 && t[0] == '0') return;
    uintptr_t g = g_lw.game;
    float room = LwEnvFloat("HELLFORGE_LW_RECORDS_ROOM", (float)LW_REC_ROOM);   /* lower only, to test the cap */
    g_lwRec.room = room >= 1.0f && room < (float)LW_REC_ROOM ? (uint32_t)room : LW_REC_ROOM;
    if (!ApiCodeMatches(g + LW_REC_INSERT, "48 8B C4 48 89 48 08 53 56 57 41 57 48 81 EC 98 00 00 00") ||
        !ApiCodeMatches(g + 0x13E02C7u, "8D 90 00 00 01 00 FF C0 41 89 86 78 AF 1E 00") ||
        !ApiCodeMatches(g + 0x13E030Bu, "45 8B 8E 50 5F 0C 00 45 8B 96 58 5F 0C 00 44 0F AF D3") ||
        !ApiCodeMatches(g + 0x13E0B77u, "41 81 C0 00 74 01 00") ||
        !ApiCodeMatches(g + 0x13E078Eu, "8D 82 00 70 01 00") ||
        !ApiCodeMatches(g + 0x13DF877u, "48 C7 83 50 5F 0C 00 00 80 00 00") ||
        !ApiCodeMatches(g + LW_REC_RESET, "48 89 5C 24 08 57 48 83 EC 20 33 FF 48 8B D9 48 89 B9 78 AF 1E 00"))
    { Note("living world: the destruction records are not the reviewed code; not changed"); return; }
    if (LoaderHook("living world: destruction record room", g + LW_REC_INSERT, (void*)LwRecInsert, &g_lwRec.insertOrig) ||
        g_lwRec.insertOrig == NULL)
    { g_lwRec.insertOrig = NULL; Note("living world: the destruction record insert could not be hooked"); return; }
    if (!ApiCodeMatches(g + LW_REC_BUILD, "48 89 5C 24 08 48 89 74 24 10 57 48 81 EC A0 00 00 00") ||
        LoaderHook("living world: destruction record multiplier", g + LW_REC_BUILD, (void*)LwRecBuilt, &g_lwRec.builtOrig) ||
        g_lwRec.builtOrig == NULL ||
        LoaderHook("living world: destruction record multiplier", g + LW_REC_RESET, (void*)LwRecReset, &g_lwRec.resetOrig) ||
        g_lwRec.resetOrig == NULL)
    { Note("living world: the destruction records' constructor or reset could not be hooked; room only"); return; }
    Note("living world: destruction records capped at %u, map multiplier 0x%X from the next world", g_lwRec.room, LW_REC_SCATTER);
}
