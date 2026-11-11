/* STAND-INS, not the real thing. The prototype was built into a HellForge that had its own level 11 install; these are
 * the names of that install the prototype's files read, with the values that HellForge build had, so the files
 * compile. Nothing here installs or moves anything: the stock level 11 is HellForge's, and how this mod gets the room
 * the values below describe on top of it is not decided. Each part says what has to take its place.
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
static float g_cityZoneRatio = 1.0f;
static int LargerMaps(void) { return g_difficulty.mapScale > 1.0f; }
/* The install's work in progress. The prototype's steps read the game's base from it and nothing else. */
typedef struct DifficultyInstall { uintptr_t game; } DifficultyInstall;

/* ---- the generation block's layout ---------------------------------------------------------------------------------
 * To replace: the layout the game's code really has once the block's capacity is settled. These are the offsets and
 * counts of the prototype's HellForge (65024 locations); stock HellForge moves the same block with other numbers. */
#define DIF_LOCATIONS 65024u
#define DIF_GEN_LOCATIONS 16384u
#define DIF_LOCATIONS_B_NEW 0x1F8B280u
#define DIF_OBJLIST_PLACES 48u
#define DIF_CLUSTERS_NEW 0x270B530u
#define DIF_IMPORTANT 1024u
#define DIF_BLOCK_NEW 0x28682D0u
#define DIF_WELD_COUNT 0x168CC60u /* the weld pool's count with 576 blocks of 256 vertices */
static uint32_t g_objectiveListPlaces = 16; /* 48 once the list has moved */
static uint32_t WorldBlockSize(void) { return DIF_BLOCK_NEW; }
/* The objective list's moved place, as the arcs find it: the one row of the install's table they match. The value is
 * the prototype's, and it lies inside that layout's own locations array; the real place must come with the layout. */
static const struct { uint32_t rva; uint8_t length, at, width; uint32_t old, value; uint8_t pin[8]; } kObjectiveListSites[1] = {
    {0xC1AD93u, 8, 4, 4, 0x8BC570u, 0x15AD370u, {0x4D, 0x8D, 0xBC, 0x24, 0x70, 0xC5, 0x8B, 0x00}}};
/* Row type of the block's site tables (proto/important_sites.h, proto/route_sites.h; not compiled until the block is). */
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
