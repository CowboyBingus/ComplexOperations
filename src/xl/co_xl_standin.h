/* STAND-INS, not the real thing. The prototype was built into a HellForge that had its own level 11 install; these are
 * the names of that install the prototype's files read, with the values that HellForge build had, so the files
 * compile. Nothing here installs or moves anything: the stock level 11 is HellForge's. Each part says what has to take
 * its place; the generation block's layout is no stand-in any more.
 * Only for co_xl.c. */
#ifndef CO_XL_STANDIN_H
#define CO_XL_STANDIN_H

/* ---- the level and its settings ------------------------------------------------------------------------------------
 * To replace: the settings of a complex operation, set when an operation rolled one (co_roll.c), and the stock level's
 * row through HfApi.difficulty_row(11). All zero here: map_scale 0 is "no larger map" to every reader. */
#define DIF_VANILLA 10u
typedef struct Difficulty
{
    HfMod* mod;
    char name[192];
    uint32_t item, text[2];
    float encounter, guardforce, cooldown, patrolFrequency, patrolCap;
    uint32_t grant;
    uint64_t icon;
    float mapScale, outpostScale, minutes;
    uint32_t optional, major, flags, missions;
} Difficulty;
static Difficulty g_difficulty;
static uintptr_t g_difficultyGame;       /* game.dll once the install ran: 0, so its readers stay off */
static uint32_t g_operationOptional, g_operationMajor;
#define g_cityZoneRatio (co_rows_city_ratio()) /* level 10's city factor over the complex row's */
static int LargerMaps(void) { return g_difficulty.mapScale > 1.0f; }
/* The install's work in progress. The prototype's steps read the game's base from it and nothing else. */
typedef struct DifficultyInstall { uintptr_t game; } DifficultyInstall;

/* ---- the generation block's layout ---------------------------------------------------------------------------------
 * HellForge owns the block: every offset and count is the one it reports (co_capacity.c), read at each use, and 0 until
 * the block has moved, so nothing is read from a layout the game does not have. */
#define CO_LOCATIONS_MAX CO_GEN_LOCATIONS /* the tables the mod keeps per location: the most HellForge gives */
static inline uint32_t CoLocations(void) { uint32_t n = co_gen_places(HF_GEN_LOCATIONS_A); return n < CO_LOCATIONS_MAX ? n : CO_LOCATIONS_MAX; }
#define DIF_LOCATIONS (CoLocations())
#define DIF_LOCATIONS_A_NEW (co_gen_at(HF_GEN_LOCATIONS_A))
#define DIF_LOCATIONS_B_NEW (co_gen_at(HF_GEN_LOCATIONS_B))
#define DIF_CLUSTERS_NEW (co_gen_at(HF_GEN_CLUSTERS))
#define DIF_IMPORTANT (co_gen_places(HF_GEN_IMPORTANT))
#define g_importantList (co_gen_at(HF_GEN_IMPORTANT))
#define g_routeNodes (co_gen_at(HF_GEN_ROUTE_NODES))
#define g_routeEdges (co_gen_at(HF_GEN_ROUTE_EDGES))
#define g_routePoints (co_gen_at(HF_GEN_ROUTE_POINTS))
static uint32_t WorldBlockSize(void) { return co_gen_block(); }
/* The objective list is one record; its place is its row field (ids at +4, the count at +0xD4). HellForge's holds 30
 * objectives, and the mod's lists are bounded by what it reports, whatever that is. */
#define DIF_OBJLIST_PLACES (co_gen_places(HF_GEN_OBJECTIVE_LIST))
#define g_objectiveListPlaces DIF_OBJLIST_PLACES
#define CO_OBJLIST_AT (co_gen_at(HF_GEN_OBJECTIVE_LIST))
/* Each objective's stamp entries (first, count) in the record with 30 places: HellForge's difficulty_block.c, "layout
 * L30". Another count is another record, whose places the mod does not know: 0, and the research notes say nothing. */
static inline int CoObjlistStamps(int32_t* starts, int32_t* counts)
{
    if (DIF_OBJLIST_PLACES != 30u) return 0;
    *starts = -0x78; *counts = -0xF0;
    return 1;
}
/* The generation's own share of the location table; the rest is the stamp window's. The extra large world's rule, not
 * the block's: the table must hold more (CO_GEN_LOCATIONS is asked for). */
#define DIF_GEN_LOCATIONS 16384u
/* STAND-IN: the weld pool's count with 576 blocks of 256 vertices, the prototype's own pool. Read by research notes
 * only; the pool's step is not in the mod, and stock's pool has other places. */
#define DIF_WELD_COUNT 0x168CC60u
/* Row type of the block's site tables (proto/important_sites.h, proto/route_sites.h; not compiled: HellForge moves them). */
typedef struct LocationSite { uint32_t rva; uint8_t at; uint32_t value; } LocationSite;

/* ---- state other install steps left behind -------------------------------------------------------------------------
 * To replace: each with the mod's own step once it has one. As they are, every reader takes its "not installed" path. */
static int g_hudGrouped;                      /* the HUD's objective rows grouped by instance */
static uintptr_t g_markerMap;                 /* the map marker list of 64 */
static volatile LONG* g_spawnerPlacesFull;    /* the director's spawner list of 1024: its full counter */
#define DIF_MISSION_ROWS 162u
static uint8_t g_mixBase[DIF_MISSION_ROWS];   /* second-primary mixes by mission row */
static uintptr_t g_navExe;                    /* helldivers2.exe once the nav grid step ran */
static float* g_navWorldTile;                 /* the nav tile size of an extra large world */
static uint32_t g_navWorldScale = 1;

/* ---- loader state with no HfApi call -------------------------------------------------------------------------------
 * To replace: a text call in HfApi (the HUD's changing texts), and the spawn event's caller for one test trace. */
typedef const char* (*EngineTextFn)(uint32_t id, void* a, void* b, void* c);
static EngineTextFn g_engineText;
static const char* (*g_textDynamic)(uint32_t id);
static uintptr_t g_spawnCaller;

/* ---- real code, the same in stock HellForge ------------------------------------------------------------------------
 * Game addresses and helpers of the level install that do not depend on who installs. They stay. */
#define DIF_WORLD 0x3326340u           /* the world: +0xAE288 its level generator, whose +0x2C8 is the block */
#define DIF_MISSIONS 0x3773420u
#define DIF_MISSION_ROW 0x380u
#define DIF_OBJECTIVES 0x32EF870u
#define DIF_OBJECTIVE_ROWS 153u
#define DIF_OBJECTIVE_ROW 0xA0u
#define DIF_LIVE_EVENT_OBJECTIVE 0x68BFBB59u
#define DIF_ARENA_ALLOCATE 0x173B8D0u  /* (arena, size, alignment) */

/* What the stubs that call C share: the volatile registers pushed and popped around the call, xmm0..xmm5 kept at
 * [rsp+0x20..0x70], and the call itself (mov rax, imm64 at +2; call rax). */
#define STUB_PUSH_VOLATILE 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53
#define STUB_POP_VOLATILE 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58
#define STUB_SAVE_XMM 0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30, \
    0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50, \
    0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70
#define STUB_LOAD_XMM 0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30, \
    0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50, \
    0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70
#define STUB_CALL_IMM64 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xD0
/* A C call from game code: 192 bytes keep a 16-byte aligned rsp aligned. 54 bytes each. */
#define STUB_ENTER STUB_PUSH_VOLATILE, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, STUB_SAVE_XMM
#define STUB_LEAVE STUB_LOAD_XMM, 0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00, STUB_POP_VOLATILE

/* A signed 32-bit displacement that can hold this distance (the stub jumps and the table reads are rel32). */
static int InRel32(int64_t distance) { return distance >= -2147483647LL - 1 && distance <= 2147483647LL; }

/* The rel32 a jump patch is about to write; the cast hides a stub outside +-2 GB, so the diagnostic line says it. */
static int32_t DifficultyJumpRel(uintptr_t site, uintptr_t stub)
{
    int64_t distance = (int64_t)stub - (int64_t)(site + 5);
    NoteDiagnostic("difficulty: stub site=0x%llx stub=0x%llx distance=%+lld in-range=%s", (unsigned long long)site,
        (unsigned long long)stub, (long long)distance, InRel32(distance) ? "yes" : "no");
    return (int32_t)distance;
}

/* The first class the mission row gives an id (its 32 entries: +0 id, +0xC class), or -1. */
static int RowClass(uintptr_t row, uint32_t id)
{
    uint8_t entries[32 * 0x18];
    if (SafeRead(row + 0x40, entries, sizeof entries)) return -1;
    for (int i = 0; i < 32; i++)
    {
        uint32_t entry, cls;
        memcpy(&entry, entries + i * 0x18, 4); memcpy(&cls, entries + i * 0x18 + 0xC, 4);
        if (entry == id) return (int)cls;
    }
    return -1;
}

/* The definition of an objective id (+0 id, +0x20 most uses per mission, +0x24 / +0x28 levels), or 0. */
static uintptr_t ObjectiveDef(uintptr_t game, uint32_t id)
{
    for (uint32_t d = 1; d < DIF_OBJECTIVE_ROWS; d++)
    {
        uint32_t at = 0;
        if (!SafeRead(game + DIF_OBJECTIVES + d * DIF_OBJECTIVE_ROW, &at, 4) && at == id) return game + DIF_OBJECTIVES + d * DIF_OBJECTIVE_ROW;
    }
    return 0;
}

/* The generation block a level generator has now, or 0. */
static uintptr_t GenerationBlock(uintptr_t game)
{
    uintptr_t world = 0, levelGen = 0, block = 0;
    if (!SafeRead(game + DIF_WORLD, &world, 8) && world != 0 && !SafeRead(world + 0xAE288, &levelGen, 8) && levelGen != 0)
        SafeRead(levelGen + 0x2C8, &block, 8);
    return block;
}

#endif
