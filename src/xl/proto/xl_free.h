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

#endif
