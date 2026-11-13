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

/* ---- rpmalloc's caches after tile events ---------------------------------------------------------------------------
 * The trim-all exe 0x3245F0 ([api+0x10]+0x520) releases this thread's cached spans and the global caches at once; each
 * pool worker trims after its job loop and the render thread on message 0x29. The frame update 0xAB5000 calls it every
 * 3600 s (game 0xAB762E) and calls the level update this runs in (0xAB519E), so the thread and the frame phase are
 * the game's own. Once a burst of tile events is TRIM_QUIET frames past (their textures freed), outside a build. */
#define TRIM_QUIET 8u
#define TRIM_FOLLOW 60u /* frames: the workers' and the render thread's trims are done by then */
static const char kTrimPin[] = "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 48 8B 35 02 BC 6E 01 B1 01 48 8B 9E 18 03 00 00 E8 DC BC 30 00";
static struct
{
    int pins; LONG level; uintptr_t call, stats;
    int pending; uint32_t quiet, events, trims, follow; const char* last;
    LONG64 mapped0, commit0;
} g_trim = {.level = -1};

/* The trim, the statistics leaf (memprobe's MP_RP_STATS) and the slot the game calls: 1 when all are the reviewed ones. */
static int MoveTrimPins(void)
{
    if (g_trim.pins) return g_trim.pins > 0;
    uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL), api = 0, table = 0, fn = 0;
    int ok = exe != 0 && ApiCodeMatches(exe + 0x3245F0u, kTrimPin) == 1 && ApiCodeMatches(exe + 0x324630u, "48 8B 08 48 83 C0 08 C6 41 7D 01") == 1 &&
        ApiCodeMatches(exe + 0x324690u, "B8 29 00 00 00 66 89 42 0C") == 1 &&
        ApiCodeMatches(exe + 0x66BE20u, "48 C7 41 10 00 00 00 00 4C 8B D1 48 63 15 52 54 CF 01") == 1 && ApiCodeMatches(exe + 0x66BF43u, "48 C1 E2 10 49 01 52 10 C3") == 1 &&
        ApiCodeMatches(g_world.game + 0xAB761Cu, "48 8B 05 E5 EC 86 02 49 89 8E 18 76 AF 07 48 8B 48 10 FF 91 20 05 00 00") == 1 &&
        !SafeRead(g_world.game + 0x3326308u, &api, 8) && api != 0 && !SafeRead(api + 0x10, &table, 8) && table != 0 && !SafeRead(table + 0x520, &fn, 8) &&
        fn == exe + 0x3245F0u;
    g_trim.pins = ok ? 1 : -1;
    if (ok) { g_trim.call = fn; g_trim.stats = exe + 0x66BE20u; }
    else Note("difficulty: moving tiles: rpmalloc's trim is not the reviewed code; tile events do not trim it");
    return ok;
}

/* The process's commit in bytes as the memory notes take it (Wine: PagefileUsage), -1 when it does not read. */
static LONG64 MoveTrimCommit(void)
{
    static BOOL (WINAPI* info)(HANDLE, void*, DWORD);
    static int looked;
    if (!looked) { looked = 1; HMODULE k = GetModuleHandleA("kernel32.dll"); if (k) info = (BOOL (WINAPI*)(HANDLE, void*, DWORD))(void*)GetProcAddress(k, "K32GetProcessMemoryInfo"); }
    WorldMemCounters c; memset(&c, 0, sizeof c); c.cb = sizeof c;
    return info != NULL && info(GetCurrentProcess(), &c, sizeof c) ? (LONG64)(c.priv ? c.priv : c.page) : -1;
}

static void MoveTrimEvent(const char* why)
{
    g_trim.pending = 1; g_trim.quiet = 0; g_trim.events++; g_trim.last = why;
}

/* Each mover frame: the follow-up line, then a due trim. 1 when it trimmed (the frame's one heavy step). */
static int MoveTrimTick(void)
{
    if (g_trim.level != g_world.levels) { g_trim.level = g_world.levels; g_trim.pending = 0; g_trim.follow = 0; g_trim.events = 0; g_trim.trims = 0; }
    if (g_trim.follow && --g_trim.follow == 0)
    {
        LONG64 s[7] = {0}, commit = MoveTrimCommit();
        ((void (*)(void*))g_trim.stats)(s);
        Note("difficulty: moving tiles: rpmalloc %u frames after trim %u: mapped %lld MB (%+lld since before it), commit %lld MB (%+lld); the frames' own allocations count too",
            TRIM_FOLLOW, g_trim.trims, (long long)(s[0] >> 20), (long long)((s[0] - g_trim.mapped0) >> 20), (long long)(commit >> 20), (long long)((commit - g_trim.commit0) >> 20));
    }
    if (!g_trim.pending || ++g_trim.quiet < TRIM_QUIET || (g_move.state != MV_IDLE && g_move.state != MV_PACKAGE)) return 0;
    g_trim.pending = 0;
    if (!MoveTrimPins()) return 0;
    LONG64 a[7] = {0}, b[7] = {0}, c0 = MoveTrimCommit();
    ((void (*)(void*))g_trim.stats)(a);
    LARGE_INTEGER t0, t1, hz; QueryPerformanceCounter(&t0);
    ((void (*)(uint64_t))g_trim.call)(0);
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&hz);
    LONG64 c1 = MoveTrimCommit();
    ((void (*)(void*))g_trim.stats)(b);
    g_trim.trims++; g_trim.follow = TRIM_FOLLOW; g_trim.mapped0 = a[0]; g_trim.commit0 = c0;
    Note("difficulty: moving tiles: rpmalloc trim %u after %u tile event(s) (the last: %s): %.1f ms on the game thread; mapped %lld -> %lld MB (%+lld), "
        "global cache %lld -> %lld MB; commit %lld -> %lld MB (%+lld); the workers and the render thread trim their own heaps next",
        g_trim.trims, g_trim.events, g_trim.last ? g_trim.last : "?", (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart,
        (long long)(a[0] >> 20), (long long)(b[0] >> 20), (long long)((b[0] - a[0]) >> 20), (long long)(a[2] >> 20), (long long)(b[2] >> 20),
        (long long)(c0 >> 20), (long long)(c1 >> 20), (long long)((c1 - c0) >> 20));
    g_trim.events = 0;
    return 1;
}
