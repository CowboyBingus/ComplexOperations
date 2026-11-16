/* Memory the extra large world gives back once nothing reads it (xl_free.c). Declared here because the large files
 * that call these come first in the unit. */
#ifndef CO_XL_FREE_H
#define CO_XL_FREE_H

static uint8_t* g_placeRegions[8]; /* op 0x2B's job regions on a larger map (WorldPlaceJob) */
static uint64_t WorldPlaceJobCounted(uint64_t a, uint64_t b, uint64_t c, uint64_t job);
static void WorldDispatchBegins(uint64_t records, uint64_t index, uint32_t count);
static int NearRenderFrame(uint32_t* frame);
static void MoveBandPixelsTick(void);
static void MoveTileTakeBand(int slot);
static void MoveBandStash(void);
static void MoveBandUnstash(void);
static void MoveTrimEvent(const char* why);
static int MoveTrimTick(void);
static int NearTilesDestroy(int ship);
static void MoveBuildLeft(int ship);
static uint64_t NearMainWorld(void);
static int NearTilesLeft(void);
static void MatUnitDestroy(uint64_t destroy, uint64_t world, uint64_t unit);
static void MatResInstall(void);
static void MatResTick(void);
static void MatResNote(void);
static void MatWaitInstall(void);
static void MatWaitSetup(int in);
static void MatWaitTick(void);
static void MatWaitDrain(void);
static void MatWaitNote(void);
static int NearGeneratorRuns(void);
static int TerrainSplitOn(void);
static int TerrainSplitLast(void);
static const char* TerrainSplitWhy(void);
static int TerrainTailsPending(void);
static int TerrainDestroySplit(uint64_t world, uint64_t h, uint64_t destroy);
static void TerrainTailCheck(uint64_t h, int raised, float cx, float cy, int cellX, int cellY);
static void TerrainTailTick(void);
static void TerrainTailDrain(uint64_t world, const char* why);
static void TerrainSplitInstall(void);

/* The research log of the terrains' materials (xl_terrain_research.c); a release has none of it. */
#ifdef CO_RESEARCH
static void TerrainLogState(const char* when);
static void TerrainLogBefore(void* world, const char* what);
static void TerrainLogSpawned(uint64_t terrain, const char* what);
static void TerrainLogSlots(void* world, const char* what, uint64_t* heightmap, uint64_t* material);
static void TerrainLogPassed(const char* hook, uint32_t type, uint32_t name, uint64_t handle, DWORD armedFor);
#else
#define TerrainLogState(when) ((void)0)
#define TerrainLogBefore(world, what) ((void)0)
#define TerrainLogSpawned(terrain, what) ((void)0)
#define TerrainLogSlots(world, what, heightmap, material) ((void)0)
#define TerrainLogPassed(hook, type, name, handle, armedFor) ((void)0)
#endif

#endif
