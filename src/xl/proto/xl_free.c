/* Memory the extra large world gives back once nothing reads it: op 0x2B's job regions after the op, a spawned tile's
 * band pixels after the render thread's upload, an expired tile when its destroy waits for nothing. Included after
 * difficulty_world_move.c. */

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

/* ---- The destroy's render fence ------------------------------------------------------------------------------------
 * The destroy (exe 0x1ADEA0) posts a fence with its flag set on the renderer [[world+0x2301F8]] and blocks until the
 * render thread reaches it and the device's job list is empty (0x1132E6 -> 0x10BB30). The frame tick waits only for the
 * frame before last (0x8A7C4), so in the level update the render thread may still be drawing the last frame and a
 * build's merge renders. A release waits for a frame where it has nothing queued (RELEASE-HITCH.md). */
static const struct { uint32_t at; const char* bytes; } kDrainPins[] = {
    {0x1ADF8Bu, "49 8B 8E F8 01 23 00 48 8B D5 E8 E6 B4 F1 FF 49 8B 86 F8 01 23 00 B2 01 48 8B 18 48 8B CB E8 92 7E F5 FF 8B D0 48 8B CB E8 78 80 F5 FF"},
    {0x105E47u, "80 B9 80 14 00 00 00"}, /* no render thread: no fence */
    {0x105F62u, "8B B3 98 13 00 00 8B D6 8B 84 B3 80 13 00 00 41 3B C7 74 EC 8D 48 01"}, /* the game's buffer, its count */
    {0x10A850u, "B8 FF FF FF FF F0 45 0F B1 A4 BF 80 13 00 00 41 87 BF 98 13 00 00"}, /* the kick: the next of 6 */
    {0x113F4Au, "44 8B 83 9C 13 00 00 B8 AB AA AA AA 41 8D 68 01 F7 E5 C1 EA 02 8D 04 52 03 C0 2B E8"}, /* the render thread's next: alike */
    {0x113EC0u, "8B 83 9C 13 00 00 48 83 C0 62"}, /* the render thread's buffer */
    {0x113EF3u, "8B 83 9C 13 00 00 48 8D 0C 83 41 8B C7 F0 0F C1 81 80 13 00 00"}, /* counted down once run */
    {0x1139A4u, "E6 32 11 00"}, /* message 0x10, the fence */
    {0x1132E6u, "49 8B 18 80 7B 48 00 74 08 49 8B CE E8 39 88 FF FF"}, /* flagged: the device's jobs first */
    {0x10BB44u, "48 8B B9 68 11 00 00 48 8B E9 33 DB 48 8D B7 C8 03 00 00 48 8B CE FF 15 00 AC 2E 01 8B BF D8 03 00 00"},
};
static int g_drainPinned; /* 1 the reviewed code, -1 not */
static struct { uint8_t threaded; uint32_t game, render, queued, jobs; } g_drain;

/* The game's slot holds the thunk 0x336AD0, a lone jmp to the destroy with its arguments as they are (run mp-city5). */
static int MoveDestroySlot(uintptr_t exe, uintptr_t slot)
{
    return exe != 0 && (slot == exe + 0x1ADEA0u || (slot == exe + 0x336AD0u && ApiCodeMatches(slot, "E9 CB 73 E7 FF") == 1));
}

static int MoveDrainPins(uintptr_t exe)
{
    if (g_drainPinned) return g_drainPinned > 0;
    const uintptr_t slot = NearApi(0x08, 0x1E0);
    int ok = MoveDestroySlot(exe, slot);
    size_t i = 0;
    for (; ok && i < sizeof kDrainPins / sizeof kDrainPins[0]; i++) ok = ApiCodeMatches(exe + kDrainPins[i].at, kDrainPins[i].bytes) == 1;
    g_drainPinned = ok ? 1 : -1;
    if (ok) return 1;
    /* The note says which check failed at run time and what it read. */
    const int pin = MoveDestroySlot(exe, slot) && i > 0;
    char where[96] = "?";
    uint8_t got[8] = {0};
    if (slot != 0) ModuleAt(slot, where, sizeof where);
    if (pin) SafeRead(exe + kDrainPins[i - 1].at, got, sizeof got);
    Note("difficulty: moving tiles: the destroy's render fence is not the reviewed code; releases do not wait for a drained render queue "
         "(%s; the slot holds 0x%llX in %s, exe 0x%llX; pin exe+0x%X reads %02X %02X %02X %02X %02X %02X %02X %02X)",
         exe == 0 ? "no exe" : pin ? "a pin" : "the destroy's slot", (unsigned long long)slot, where, (unsigned long long)exe,
         pin ? kDrainPins[i - 1].at : 0u, got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7]);
    return 0;
}

/* 1 when a fence posted now is served at once (no render thread, or it is in the game's buffer with nothing waiting
 * and no device job), 0 when work is queued before it, -1 when it does not read (the release goes as before). */
static int MoveRenderDrained(uint64_t world)
{
    uintptr_t rw = 0, r = 0, dev = 0;
    if (!MoveDrainPins(ApiModuleBase("helldivers2.exe", NULL)) || world == 0 || SafeRead(world + 0x2301F8u, &rw, 8) || rw == 0 || SafeRead(rw, &r, 8) || r == 0 ||
        SafeRead(r + 0x1480, &g_drain.threaded, 1))
        return -1;
    if (!g_drain.threaded) return 1;
    if (SafeRead(r + 0x1398, &g_drain.game, 4) || SafeRead(r + 0x139C, &g_drain.render, 4) || g_drain.game > 5 ||
        SafeRead(r + 0x1380 + 4 * (uintptr_t)g_drain.game, &g_drain.queued, 4) || SafeRead(r + 0x1168, &dev, 8) || dev == 0 || SafeRead(dev + 0x3D8, &g_drain.jobs, 4))
        return -1;
    return g_drain.render == g_drain.game && g_drain.queued == 0 && g_drain.jobs == 0;
}

/* The release line's account of the render queue as the release found it. */
static const char* MoveDrainText(int drained, uint32_t held)
{
    static char text[192];
    if (drained < 0) return "the render queue not read";
    if (!g_drain.threaded) return "no render thread";
    snprintf(text, sizeof text, "the render queue %s after %u held frame(s) (the render thread in buffer %u, the game's %u; %u message(s) queued, %u device job(s))",
        drained ? "drained" : "NOT drained", held, g_drain.render, g_drain.game, g_drain.queued, g_drain.jobs);
    return text;
}

#define MOVE_DRAIN_FRAMES 8u /* a due release holds the frame's step at most this long for a drained render queue */

static double MoveTicksMs(LONGLONG ticks)
{
    LARGE_INTEGER hz; QueryPerformanceFrequency(&hz);
    return (double)ticks * 1000.0 / (double)hz.QuadPart;
}

/* The mover's state in the line lets a run check that no release falls inside the merge's world-sized window; the
 * destroy alone and the render queue it found, that its fence waited for nothing. */
static void MoveReleaseTimed(int k, int drained)
{
    int building = g_move.state != MV_IDLE;
    LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
    MoveRelease(k);
    QueryPerformanceCounter(&t1);
    char destroyed[48];
    if (g_move.destroyTicks >= 0) snprintf(destroyed, sizeof destroyed, "the destroy alone %.1f ms", MoveTicksMs(g_move.destroyTicks));
    else snprintf(destroyed, sizeof destroyed, "no destroy");
    if (building)
        Note("difficulty: moving tiles: the release took %.1f ms on the game thread, during the build of cell (%d, %d) (mover %s, near state %d); %s, %s", MoveTicksMs(t1.QuadPart - t0.QuadPart),
            g_move.cell[0], g_move.cell[1], MoveStateName(g_move.state), g_near.state, destroyed, MoveDrainText(drained, g_move.drainWaits));
    else Note("difficulty: moving tiles: the release took %.1f ms on the game thread; %s, %s", MoveTicksMs(t1.QuadPart - t0.QuadPart), destroyed, MoveDrainText(drained, g_move.drainWaits));
}

/* The first expired tile (not the drop tile) clear of the merge cameras, -1 none. */
static int MoveReleaseDue(ULONGLONG now)
{
    for (int k = 1; k < NEAR_TILES_MAX; k++)
        if (g_tiles[k].live && g_tiles[k].level == g_world.levels && now - g_tiles[k].seen > MOVE_KEEP_MS && MoveReleaseClear(k)) return k;
    return -1;
}

/* Every frame, building or not: an expired tile goes, one a frame, in a frame where the destroy's fence waits for
 * nothing. Until then the frame does nothing else, so the build queues no render work; after MOVE_DRAIN_FRAMES it goes
 * anyway. 1 when the frame went to a release. */
static int MoveReleaseTick(ULONGLONG now)
{
    if (g_move.players == 0 || MoveLayerWait()) return 0;
    MoveKeepSeen(now);
    int k = MoveReleaseDue(now);
    if (k < 0) { g_move.drainWaits = 0; return 0; }
    int drained = MoveRenderDrained(MoveWorld());
    if (drained == 0 && g_move.drainWaits < MOVE_DRAIN_FRAMES) { g_move.drainWaits++; return 1; }
    MoveReleaseTimed(k, drained);
    g_move.drainWaits = 0;
    return 1;
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

/* ---- The tiles' terrains at a level's end --------------------------------------------------------------------------
 * A tile's terrain is spawned into the main world ([[game+0x3326340]+0x10E8]), which the ship and every level share,
 * and no op of the level destroys it. Left there, the next levels draw it with the maps the next replay frees: the
 * binder 0x6F8B30 then reads a freed map's heap link as its texture handle (crash exe+0x6F9394). */
#define NEAR_TERRAINS 0x230550u /* world: the terrains by handle; entries (0x170 bytes) at +8, count +0x18, buckets +0x1C */

static uint64_t NearMainWorld(void)
{
    uint64_t engine = 0, world = 0;
    if (SafeRead(g_world.game + 0x3326340u, &engine, 8) || engine == 0 || SafeRead(engine + 0x10E8u, &world, 8)) return 0;
    return world;
}

/* The world's find, exe 0x1BCF30, which neither inserts nor locks: 1 when `handle` is a terrain of `world`, 0 when not,
 * -1 when the map does not read. The destroy (0x1ADEA0) finds with insert (0x1B86C0) and frees the handle's object, so
 * a handle not found here is never given to it. */
static int NearTerrainIn(uint64_t world, uint64_t handle)
{
    const uint64_t map = world + NEAR_TERRAINS;
    uint64_t entries = 0, key = 0;
    uint32_t count = 0, buckets = 0, next = 0;
    if (world == 0 || SafeRead(map + 8, &entries, 8) || SafeRead(map + 0x18, &count, 4) || SafeRead(map + 0x1C, &buckets, 4)) return -1;
    if (count == 0) return 0;
    if (entries == 0 || buckets == 0) return -1;
    const uint32_t m = 0x5BD1E995u;
    uint32_t a = (uint32_t)handle * m, b = (uint32_t)(handle >> 32) * m;
    a = (a ^ a >> 24) * m; b = (b ^ b >> 24) * m;
    uint32_t h = ((a ^ 0xDE8F4CA8u) * m) ^ b;
    h = (h ^ h >> 13) * m; h ^= h >> 15;
    uint32_t i = h % buckets;
    if (SafeRead(entries + (uint64_t)i * 0x170 + 0x168, &next, 4)) return -1;
    if (next == 0xFFFFFFFEu) return 0; /* an empty bucket */
    for (uint32_t steps = 0; steps <= count; steps++)
    {
        if (SafeRead(entries + (uint64_t)i * 0x170, &key, 8) || SafeRead(entries + (uint64_t)i * 0x170 + 0x168, &next, 4)) return -1;
        if (key == handle) return 1;
        if (next == 0x7FFFFFFFu) return 0;
        i = next;
    }
    return -1;
}

/* Under the world's terrain lock, which the destroy takes too (so never held across it). */
static int NearTerrainInLocked(uint64_t world, uint64_t handle)
{
    const int locked = world != 0 && g_nearTerrainLock[0] != NULL && g_nearTerrainLock[1] != NULL;
    if (locked) g_nearTerrainLock[0](world + 0x230520u);
    const int in = NearTerrainIn(world, handle);
    if (locked) g_nearTerrainLock[1](world + 0x230520u);
    return in;
}

/* Terrains a replay found still in the world (NearTilesLeft): the new level takes their slots, so their handles wait
 * here for the next level's end. */
#define NEAR_LEFT_MAX 16
static uint64_t g_nearLeft[NEAR_LEFT_MAX];

/* Every tile terrain handle the mod holds: the tiles', g_near's, a build's stashed drop tile's and the ones left. */
#define NEAR_HANDLES (NEAR_TILES_MAX + 2 + NEAR_LEFT_MAX)
static int NearHandles(uint64_t** at)
{
    int n = 0;
    for (int k = 0; k < NEAR_TILES_MAX; k++) at[n++] = &g_tiles[k].handle;
    at[n++] = &g_near.handle;
    if (g_move.stashed) at[n++] = &g_move.dropHandle;
    for (int k = 0; k < NEAR_LEFT_MAX; k++) at[n++] = &g_nearLeft[k];
    return n;
}

/* The generator's job runs ([[level+0xF718]+8], which the level's own teardown waits on at 0xF7428A before it destroys
 * the level's terrain): its 0x6A holds the world's terrain lock around a tile's work. */
static int NearGeneratorRuns(void)
{
    uintptr_t job = 0;
    uint8_t runs = 0;
    return g_worldLevel != 0 && !SafeRead(g_worldLevel + 0xF718u, &job, 8) && job != 0 && !SafeRead(job + 8, &runs, 1) && runs != 0;
}

/* The last level's tile terrains destroyed on the game thread (a level init or the ship's set-up), as MoveRelease
 * destroys one; their maps go at the next replay. Kept: any handle whose world does not read; every one while the
 * generator's job runs (the lock is never asked for beside it); at the ship a drop tile between its 0x69 and 0x6B,
 * which that job may take up again. A level init comes only once the job has left (state 0: co_drain, the preview
 * tick, the game's regenerate), so it destroys that tile too. A handle is forgotten once its terrain is gone: its
 * address can be reused. */
static int NearTilesDestroy(int ship)
{
    const char* why = ship ? "the ship" : "a level init";
    typedef void (*DestroyFn)(uint64_t world, uint64_t handle);
    DestroyFn destroy = (DestroyFn)NearApi(0x08, 0x1E0);
    uint64_t world = NearMainWorld(), *at[NEAR_HANDLES], seen[NEAR_HANDLES];
    int verdict[NEAR_HANDLES], unique = 0, gone = 0, absent = 0, kept = 0;
    const int n = NearHandles(at);
    const uint64_t generating = ship && g_move.state == MV_IDLE && g_near.state == NEAR_READY ? g_near.handle : 0;
    const int busy = NearGeneratorRuns();
    const int known = !busy && world != 0 && (g_near.world == 0 || g_near.world == world);
    LARGE_INTEGER t0, t1, hz;
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < n; i++)
    {
        const uint64_t h = *at[i];
        int j = 0;
        while (j < unique && seen[j] != h) j++;
        if (h == 0 || j < unique) continue;
        seen[unique] = h;
        const int in = !known || h == generating ? -1 : NearTerrainInLocked(world, h);
        verdict[unique] = in > 0 && destroy != NULL ? 1 : in == 0 ? 0 : -1;
        if (verdict[unique] == 1)
        {
            if (gone == 0) NearTilesClear(); /* their heights stop being read before the terrain goes */
            destroy(world, h);
            gone++;
        }
        else if (verdict[unique] == 0) absent++;
        else kept++;
        unique++;
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < unique; j++)
            if (*at[i] != 0 && *at[i] == seen[j] && verdict[j] >= 0) { *at[i] = 0; break; }
    if (!g_move.stashed) g_move.dropHandle = 0;
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&hz);
    if (unique > 0)
        Note("difficulty: near terrain: %s: %d tile terrain(s) of the last level destroyed (%.1f ms on the game thread), %d already gone, %d kept "
            "(%s); their maps go at the next replay", why, gone, (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart, absent, kept,
            busy ? "the generator's job still runs" : !known ? "the main world does not read" : destroy == NULL ? "no destroy call" : generating != 0 ? "the generation may still finalize the drop tile" : "none");
    return gone;
}

/* The new level's replay, before it frees the last level's tile maps: a terrain still in the world (or one whose world
 * does not read) keeps every map of that level, leaked, since the world may draw it in this level. Its handle waits
 * in g_nearLeft for the next level's end, which destroys it. */
static int NearTilesLeft(void)
{
    const uint64_t world = NearMainWorld();
    uint64_t *at[NEAR_HANDLES], still[NEAR_LEFT_MAX];
    const int n = NearHandles(at);
    int left = 0, lost = 0;
    for (int i = 0; i < n; i++)
    {
        const uint64_t h = *at[i];
        *at[i] = 0;
        if (h == 0 || NearTerrainInLocked(world, h) == 0) continue;
        int j = 0;
        while (j < left && still[j] != h) j++;
        if (j < left) continue;
        if (left < NEAR_LEFT_MAX) still[left++] = h; else lost++;
    }
    for (int k = 0; k < left; k++) g_nearLeft[k] = still[k];
    if (left == 0) { Note("difficulty: near terrain: 0 tile terrains left from the last level"); return 0; }
    for (int k = 0; k < NEAR_TILES_MAX; k++) { g_tiles[k].tex[0] = g_tiles[k].tex[1] = g_tiles[k].band = 0; g_tiles[k].bandPixels = NULL; }
    g_near.tex[0] = g_near.tex[1] = 0; g_matband.tex = 0; g_matband.pixels = NULL;
    g_move.dropTex[0] = g_move.dropTex[1] = g_move.dropBand = 0; g_move.dropBandPixels = NULL;
    Note("difficulty: near terrain: %d tile terrain(s) left in the world from the last level; their maps stay allocated (leaked) so that it never draws "
        "freed ones; the next level's end destroys them (%d more than the list holds)", left + lost, lost);
    return left + lost;
}

/* ---- A tile build the level's end cut short ------------------------------------------------------------------------
 * MoveTick runs only in a live level, so a build under way at the level's end never reaches its own teardown, and the
 * level's (0xF6FF60, from state 4) ends only what a generation leaves: the first merge camera [R+8], R+0xB8, the 0x94
 * lookup, the environment slot. The build's stamp units, the second camera, R+0x108 and the merge's other resources
 * would stay in the main world, and the stamp pass's camera offset would move the next level's own 0x63. */

static int MoveU64Cmp(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return x < y ? -1 : x > y;
}

/* The build's stamp units still in the world, destroyed as the level's teardown destroys its own with their stamp
 * terrains spawned (0xF742F7..0xF7432F). The destroy (exe 0x338960) takes a unit it does not find in the world's list
 * (array [world+0x230590], count [world+0x230588]) for the last one and frees it, so each is looked up in a copy of
 * the list first. -1 when they could not be looked up: kept. */
static int MoveStampUnitsLeft(uint64_t world, uint32_t* absent)
{
    uintptr_t wi = 0, table = 0, destroy = 0;
    uint64_t list = 0, *copy = NULL;
    uint32_t count = 0;
    *absent = 0;
    if (world == 0 || world != NearMainWorld() || SafeRead(g_world.game + DIF_STREAM_INTERFACE, &wi, 8) || wi == 0 || SafeRead(wi + DIF_STREAM_TABLE, &table, 8) ||
        table == 0 || SafeRead(table + DIF_STREAM_DESTROY_ENTRY, &destroy, 8) || destroy == 0 || SafeRead(world + 0x230588u, &count, 4) ||
        SafeRead(world + 0x230590u, &list, 8) || count > (1u << 22) || (count != 0 && list == 0)) return -1;
    const uintptr_t wait = WorldStreamAnimWait(table);
    if (wait == 0) return -1;
    if (count != 0 && ((copy = (uint64_t*)VirtualAlloc(NULL, (size_t)count * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) == NULL || SafeRead(list, copy, (size_t)count * 8)))
    { if (copy != NULL) VirtualFree(copy, 0, MEM_RELEASE); return -1; }
    if (count != 0) qsort(copy, count, 8, MoveU64Cmp);
    int gone = 0;
    for (uint32_t k = g_move.stGone; k < g_move.stTemp; k++)
    {
        const uint64_t unit = g_move.stUnit[k];
        if (unit == 0) continue;
        if (count == 0 || bsearch(&unit, copy, count, 8, MoveU64Cmp) == NULL) { (*absent)++; continue; }
        if (gone == 0) ((WorldStreamAnimWaitFn)wait)(world); /* as every game destroy */
        ((WorldStreamDestroyUnitFn)destroy)(world, unit, 0, 0);
        gone++;
    }
    if (copy != NULL) VirtualFree(copy, 0, MEM_RELEASE);
    return gone;
}

/* The ship's set-up or a level init, on the game thread, before the tile terrains go: what a build under way left in
 * the world goes as its own failure would take it, in one call (no frame of this level follows). The package's
 * release stays pending for the next level's end: never in the call that destroys the merge. */
static void MoveBuildLeft(const char* why)
{
    if (g_move.stTemp <= g_move.stGone && !g_move.merge && !g_move.acquired) return;
    const int state = g_move.state, merge = g_move.merge, held = g_move.acquired, ring = MoveSpawnRingEmpty(MoveWorld());
    const uint32_t units = g_move.stTemp > g_move.stGone ? g_move.stTemp - g_move.stGone : 0;
    uint32_t absent = 0;
    LARGE_INTEGER t0, t1, hz;
    QueryPerformanceCounter(&t0);
    const int gone = units != 0 ? MoveStampUnitsLeft(MoveWorld(), &absent) : 0;
    g_move.stTemp = g_move.stGone = 0; g_move.stamped = 0; /* gone or not: the next build's list takes their place */
    g_nearStampOffset[0] = g_nearStampOffset[1] = 0.0f;    /* else the next level's own 0x63 would look 20 km off */
    const int torn = MoveTeardown(1);
    MovePackageLater();
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&hz);
    Note("difficulty: moving tiles: %s: the build of cell (%d, %d) was cut short in step '%s': %d of its %u stamp units destroyed, %u already gone%s; "
        "the merge %s; the merge package %s (%.1f ms on the game thread; the spawn ring %s)", why, g_move.cell[0], g_move.cell[1], MoveStateName(state),
        gone < 0 ? 0 : gone, units, absent, gone < 0 ? ", the rest KEPT in the world (its unit list or the destroy did not read)" : "",
        torn ? "torn down" : merge ? "NOT torn down" : "was not up", held ? "released at the next level's end" : "was not held",
        (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart, ring == 1 ? "empty" : ring == 0 ? "NOT empty" : "did not read");
}
