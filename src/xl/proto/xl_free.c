/* Memory the extra large world gives back once nothing reads it: op 0x2B's job regions after the op, and a spawned
 * tile's band pixels after the render thread's upload. Included after difficulty_world_move.c. */

/* ---- Op 0x2B's job regions ----------------------------------------------------------------------------------------- */
static volatile LONG g_placeJobsIn; /* jobs under way: a job's note reads its region after the job's done flag */
static struct { int pending; DWORD thread; } g_placeFree;

static uint64_t WorldPlaceJobCounted(uint64_t a, uint64_t b, uint64_t c, uint64_t job)
{
    InterlockedIncrement(&g_placeJobsIn);
    uint64_t r = WorldPlaceJob(a, b, c, job);
    InterlockedDecrement(&g_placeJobsIn);
    return r;
}

/* The op's merge (game 0xF35030) copies every record out by value and its jobs live in its arena, so once it has
 * returned nothing points into the regions. The op calls no dispatcher, so a later call on its thread is past it. */
static void WorldPlaceRegionsFree(void)
{
    if (!g_placeFree.pending || g_placeFree.thread != GetCurrentThreadId() || g_placeJobsIn != 0) return;
    int freed = 0;
    for (int k = 0; k < 8; k++)
        if (g_placeRegions[k] != NULL) { VirtualFree(g_placeRegions[k], 0, MEM_RELEASE); g_placeRegions[k] = NULL; freed++; }
    g_placeFree.pending = 0;
    if (freed) Note("difficulty: extra large maps: op 0x2B's %d job regions given back after its merge (%d MiB committed)", freed, freed * 128);
}

/* Each dispatcher call's start: a 0x2B record on a larger map marks its thread; the next call there on another op frees
 * the regions (a later 0x2B makes them again). */
static void WorldDispatchBegins(uint64_t records, uint64_t index, uint32_t count)
{
    FpsLiftStep(records, index, count);
    uint32_t at = 0, op = 0;
    if (SafeRead(index, &at, 4) || SafeRead(records + (uint64_t)at * 0x19B0u, &op, 4)) return;
    if (op != 0x2B) { WorldPlaceRegionsFree(); return; }
    if (g_world.current > 1.0f) { g_placeFree.pending = 1; g_placeFree.thread = GetCurrentThreadId(); }
}

/* ---- Band pixels ---------------------------------------------------------------------------------------------------
 * The render thread reads a created texture's pixels once, in the upload copy (exe 0x6EC1D2), before the end message
 * of the frame it was made in is done (0x10A950 waits for every resource job); the main thread waits for that frame's
 * fence a frame later (0x8A7C4). So 4 frame kicks ([r+0x1A50], +1 a kick at 0x10A81C) after the create, none read. */
#define NEAR_PIXELS_FRAMES 4u
static int g_renderFramePinned = -1;
static int NearRenderFrame(uint32_t* frame)
{
    uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL), app = 0, r = 0;
    uint8_t threaded = 0;
    if (exe == 0) return 0;
    if (g_renderFramePinned < 0)
        g_renderFramePinned = ApiCodeMatches(exe + 0x31E870u, "48 8B 05 91 19 6F 01 48 8B 98 38 03 00 00") == 1 && ApiCodeMatches(exe + 0x31E8A8u, "44 38 A3 80 14 00 00") == 1 &&
            ApiCodeMatches(exe + 0x10A80Au, "41 8B BF 50 1A 00 00 49 8B 8F 48 12 00 00 FF C7") == 1 && ApiCodeMatches(exe + 0x10A81Cu, "41 89 BF 50 1A 00 00") == 1 &&
            ApiCodeMatches(exe + 0x10A977u, "E8 B4 11 00 00") == 1 && ApiCodeMatches(exe + 0x8A738u, "48 8B 8E 38 03 00 00 E8 7C FE 07 00") == 1 &&
            ApiCodeMatches(exe + 0x8A7BDu, "48 8B 8E 38 03 00 00 E8 67 B8 07 00") == 1 && ApiCodeMatches(exe + 0x8AAE9u, "48 8B 8E 38 03 00 00 33 D2 E8 49 B3 07 00") == 1;
    /* [r+0x1480] 0: the create posts nothing to a render thread (0x31E8A8), a path not followed here */
    return g_renderFramePinned && !SafeRead(exe + 0x1A10208u, &app, 8) && app != 0 && !SafeRead(app + 0x338, &r, 8) && r != 0 &&
        !SafeRead(r + 0x1480, &threaded, 1) && threaded != 0 && !SafeRead(r + 0x1A50, frame, 4);
}

/* 1 when the texture made at render frame `made` no longer needs its pixels. */
static int NearPixelsDone(int framed, uint32_t made)
{
    uint32_t now = 0;
    return framed && NearRenderFrame(&now) && now - made >= NEAR_PIXELS_FRAMES;
}

/* The drop tile's band aside while a build has g_matband, and back; a built tile takes the build's. */
static void MoveBandStash(void)
{
    g_move.dropBand = g_matband.tex; g_move.dropBandPixels = g_matband.pixels; g_move.dropBandServed = g_matband.served;
    g_move.dropBandFramed = g_matband.framed; g_move.dropBandFrame = g_matband.frame;
}
static void MoveBandUnstash(void)
{
    g_matband.tex = g_move.dropBand; g_matband.pixels = g_move.dropBandPixels; g_matband.served = g_move.dropBandServed;
    g_matband.framed = g_move.dropBandFramed; g_matband.frame = g_move.dropBandFrame;
}
static void MoveTileTakeBand(int slot)
{
    g_tiles[slot].band = g_matband.tex; g_tiles[slot].bandPixels = g_matband.pixels;
    g_tiles[slot].bandFramed = g_matband.framed; g_tiles[slot].bandFrame = g_matband.frame;
}

/* A spawned tile's band pixels are read only by its texture's upload and its spawn's readback (NearMatReadAsync, while
 * NearSpawn runs), so they go once both are past. The drop tile's are g_matband's while no build has them stashed. */
static void MoveBandPixelsTick(void)
{
    for (int k = 1; k < NEAR_TILES_MAX; k++)
        if (g_tiles[k].live && g_tiles[k].bandPixels != NULL && NearPixelsDone(g_tiles[k].bandFramed, g_tiles[k].bandFrame))
        {
            VirtualFree(g_tiles[k].bandPixels, 0, MEM_RELEASE);
            g_tiles[k].bandPixels = NULL; g_move.pixelsFreed++;
            Note("difficulty: moving tiles: the band pixels of cell (%d, %d) given back (64 MiB, %u this level)", g_tiles[k].cell[0], g_tiles[k].cell[1], g_move.pixelsFreed);
        }
    if (!g_move.stashed && g_near.state == NEAR_IN && g_matband.pixels != NULL && g_matband.tex != 0 && NearPixelsDone(g_matband.framed, g_matband.frame))
    {
        VirtualFree(g_matband.pixels, 0, MEM_RELEASE);
        g_matband.pixels = NULL; g_move.pixelsFreed++;
        Note("difficulty: moving tiles: the drop tile's band pixels given back (64 MiB, %u this level)", g_move.pixelsFreed);
    }
}
