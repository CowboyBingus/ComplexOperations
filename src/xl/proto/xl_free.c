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

/* ---- Op 0x41's unit resources, freed after a later frame's fence ----------------------------------------------------
 * Each unit op 0x41's setup spawns owns a mesh resource (exe 0x1A8B80 sets its record's +0xC). Its destroy (0x337420 ->
 * 0x1A8E40) releases it in 0x1A8CE0: the handles' values into a release batch (0x4EB380), messages 0xC and 0xD carrying
 * only the batch (0x74B80, 0x10BDA0), then a flagged fence and a blocking wait (0x105E40, 0x106030) before the batch's
 * owner `res` and its two CPU arrays are freed: the render thread's whole last frame, 15-23 ms (A10.md). Here the record's
 * byte is cleared so the game's destroy skips 0x1A8CE0, the same release is pushed, and `res` waits in a list instead:
 * the render thread runs its queue in order on one thread and a fence the frame loop makes after the push is waited
 * before the third frame kick from it ([r+0x1A50], only 0x10A81C, from 0x8A73F; 0x8A7C4 waits, 0x8AAF2 posts after it). */
#define MAT_RES_PENDING 64
#define MAT_RES_KICKS 3u
typedef uint64_t (*MatResRecFn)(uint64_t map, const void* key);   /* 0x1B83E0 / 0x1B8240: the value, found or made */
typedef uint64_t (*MatResCtxFn)(uint64_t dev);                    /* the device's slot +0xF8 (0x525F40): a release batch */
typedef void (*MatResPairFn)(uint64_t a, uint64_t b);            /* 0x4EB380 (res, batch), 0x74B80 / 0x10BDA0 (r, batch) */
typedef uint32_t (*MatResFenceFn)(uint64_t r, uint8_t flag);      /* 0x105E40 */
typedef uint8_t (*MatResWaitFn)(uint64_t r, uint32_t id);         /* 0x106030 */
typedef void (*MatResEraseFn)(uint64_t map, const uint32_t* id);  /* 0x1BCB30 */
typedef void (*MatResReleaseFn)(uint64_t world, uint32_t id, uint8_t erase); /* 0x1A8CE0 itself */
typedef void (*MatResFreeFn)(uint64_t alloc, uint64_t p, uint64_t zero); /* an allocator's slot +0x40 */
typedef void (*MatResTeardownFn)(uint64_t world);                 /* 0x1A6E00 */
typedef struct
{
    MatResRecFn unitRec, resRec;
    MatResCtxFn ctxSlot;
    MatResPairFn release, pushC, pushD;
    MatResFenceFn fence;
    MatResWaitFn wait;
    MatResEraseFn erase;
    MatResReleaseFn original;
    uint64_t destroySlot; /* what [[api+8]+0x158] must hold: 0x337420 */
} MatResFns;
typedef struct { uint64_t world, r, res; uint32_t kick; LONG levels; } MatResEntry;
static struct
{
    int ready;                 /* 1 the reviewed code and the teardown hook, -1 not (why in `off`) */
    const char* off;
    MatResFns fn;
    MatResTeardownFn teardown; /* the hook's original */
    uint64_t app;              /* exe+0x1A10208: [[app]+0x338] is the frame loop's renderer; 0 in a test */
    SRWLOCK lock;              /* the list: the game thread's tick and a teardown on any thread */
    MatResEntry e[MAT_RES_PENDING];
    uint32_t n, deferred, freed, max, fallback, drains, leaked;
} g_matRes = {.lock = SRWLOCK_INIT};

static uint64_t MatResU64(uint64_t at) { uint64_t v = 0; return SafeRead((uintptr_t)at, &v, 8) ? 0 : v; }

/* The renderer of `world` ([[world+0x2301F8]]) when it is the frame loop's and threaded, its kick count in *kick. */
static uint64_t MatResRenderer(uint64_t world, uint32_t* kick)
{
    const uint64_t r = MatResU64(MatResU64(world + 0x2301F8u));
    uint8_t threaded = 0;
    if (r == 0 || (g_matRes.app != 0 && MatResU64(MatResU64(g_matRes.app) + 0x338) != r) || SafeRead((uintptr_t)r + 0x1480, &threaded, 1) || !threaded ||
        SafeRead((uintptr_t)r + 0x1A50, kick, 4))
        return 0;
    return r;
}

/* 0x1A8D75..0x1A8E0B on `res`: the index and vertex arrays through their own allocators, then res through the world's. */
static void MatResFree(uint64_t world, uint64_t res)
{
    uint8_t* b = (uint8_t*)(uintptr_t)res;
    static const uint32_t arrays[2][4] = {{0x1C4u, 0x1C0u, 0x1D0u, 0x1C8u}, {0x1ACu, 0x1A8u, 0x1B8u, 0x1B0u}}; /* capacity, count, allocator, data */
    for (int k = 0; k < 2; k++)
    {
        if (*(uint32_t*)(b + arrays[k][0]) == 0) continue;
        *(uint32_t*)(b + arrays[k][1]) = 0;
        const uint64_t alloc = *(uint64_t*)(b + arrays[k][2]);
        ((MatResFreeFn)(uintptr_t)*(uint64_t*)(uintptr_t)(*(uint64_t*)(uintptr_t)alloc + 0x40))(alloc, *(uint64_t*)(b + arrays[k][3]), 0);
        *(uint64_t*)(b + arrays[k][3]) = 0;
        *(uint32_t*)(b + arrays[k][0]) = 0;
    }
    const uint64_t alloc = world + 0x230058u;
    ((MatResFreeFn)(uintptr_t)*(uint64_t*)(uintptr_t)(*(uint64_t*)(uintptr_t)alloc + 0x40))(alloc, res, 0);
}

/* The copy of 0x1A8CE0(world, id, 1) up to its wait; 0 when the list is full or the renderer does not read (the caller
 * then runs 0x1A8CE0 itself). */
static int MatResDefer(uint64_t world, uint32_t id)
{
    uint32_t kick = 0;
    const uint64_t r = MatResRenderer(world, &kick);
    const uint64_t dev = r != 0 ? (MatResU64(r + 0x1168) != 0 ? MatResU64(r + 0x1168) : MatResU64(r + 0x1138)) : 0;
    /* the batch maker 0x1A8CE0 calls through +0xF8: 0x525F40 in both the d3d12 (0x16955D8) and d3d11 (0x1696788) tables */
    if (dev == 0 || MatResU64(MatResU64(dev) + 0xF8) != (uint64_t)(uintptr_t)g_matRes.fn.ctxSlot || g_matRes.n >= MAT_RES_PENDING) return 0;
    const uint64_t rec = g_matRes.fn.resRec(world + 0x2304D0u, &id);
    const uint64_t res = *(const uint64_t*)(uintptr_t)rec;
    const uint64_t batch = g_matRes.fn.ctxSlot(dev);
    g_matRes.fn.release(res, batch);
    g_matRes.fn.pushC(r, batch);
    g_matRes.fn.pushD(r, batch);
    uint32_t after = kick;
    SafeRead((uintptr_t)r + 0x1A50, &after, 4); /* the kick the push went in with: this thread alone kicks */
    AcquireSRWLockExclusive(&g_matRes.lock);
    if (res != 0) g_matRes.e[g_matRes.n++] = (MatResEntry){world, r, res, after, g_world.levels};
    if (g_matRes.n > g_matRes.max) g_matRes.max = g_matRes.n;
    g_matRes.deferred++;
    ReleaseSRWLockExclusive(&g_matRes.lock);
    g_matRes.fn.erase(world + 0x2304D0u, &id);
    return 1;
}

/* Op 0x41's destroy of one unit through the game's slot `destroy`: an owned resource is released here and freed later. */
static void MatUnitDestroy(uint64_t destroy, uint64_t world, uint64_t unit)
{
    typedef void (*DestroyFn)(uint64_t world, uint64_t unit);
    if (g_matRes.ready <= 0 || destroy != g_matRes.fn.destroySlot) { ((DestroyFn)(uintptr_t)destroy)(world, unit); return; }
    uint8_t* rec = (uint8_t*)(uintptr_t)g_matRes.fn.unitRec(world + 0x2304F8u, &unit);
    const uint32_t id = *(const uint32_t*)(rec + 8);
    if (rec[0xC] == 0) { ((DestroyFn)(uintptr_t)destroy)(world, unit); return; }
    rec[0xC] = 0; /* 0x1A8EAB: the game's destroy leaves the resource to us */
    ((DestroyFn)(uintptr_t)destroy)(world, unit);
    if (!MatResDefer(world, id)) { g_matRes.fallback++; g_matRes.fn.original(world, id, 1); }
}

/* Game thread, every frame: what a fence after its push has covered goes back; another level's is left (T2). */
static void MatResTick(void)
{
    if (g_matRes.n == 0) return;
    AcquireSRWLockExclusive(&g_matRes.lock);
    uint32_t keep = 0;
    for (uint32_t i = 0; i < g_matRes.n; i++)
    {
        MatResEntry* x = &g_matRes.e[i];
        uint32_t kick = 0;
        if (x->levels != g_world.levels) { g_matRes.leaked++; continue; }
        if (SafeRead((uintptr_t)x->r + 0x1A50, &kick, 4) || kick - x->kick < MAT_RES_KICKS) { g_matRes.e[keep++] = *x; continue; }
        MatResFree(x->world, x->res);
        g_matRes.freed++;
    }
    g_matRes.n = keep;
    ReleaseSRWLockExclusive(&g_matRes.lock);
}

/* T1: before the world's render teardown (0x1A6E00) frees its allocator, its waiting resources go after one flagged
 * fence, as 0x1A8CE0 would have waited for each. */
static void MatResDrain(uint64_t world)
{
    AcquireSRWLockExclusive(&g_matRes.lock);
    uint64_t r = 0;
    for (uint32_t i = 0; i < g_matRes.n && r == 0; i++) if (g_matRes.e[i].world == world) r = g_matRes.e[i].r;
    if (r != 0)
    {
        g_matRes.fn.wait(r, g_matRes.fn.fence(r, 1));
        uint32_t keep = 0;
        for (uint32_t i = 0; i < g_matRes.n; i++)
            if (g_matRes.e[i].world == world) { MatResFree(world, g_matRes.e[i].res); g_matRes.freed++; }
            else g_matRes.e[keep++] = g_matRes.e[i];
        g_matRes.n = keep;
        g_matRes.drains++;
    }
    ReleaseSRWLockExclusive(&g_matRes.lock);
}

static void MatWaitDrain(void);
static void MatResTeardown(uint64_t world)
{
    MatWaitDrain(); /* the renderer's fences before anything of the world goes */
    MatResDrain(world);
    g_matRes.teardown(world);
}

/* The exe's bytes the copy relies on, beyond the fence's (kDrainPins from 1) and the frame kicks' (NearRenderFrame). */
static const struct { uint32_t at; const char* bytes; } kMatResPins[] = {
    {0x337420u, "41 B0 01 E9 18 1A E7 FF"}, /* the destroy slot: 0x1A8E40 with r8b = 1 */
    {0x1A8E5Bu, "48 8B F2 48 8B F9 48 8D 54 24 58 48 81 C1 F8 04 23 00 45 0F B6 E0 E8 6A F5 00 00 48 8B 8F F8 01 23 00 48 8B D6 4C 8B F8 E8 F8 14 03 00 "
                "49 8D 57 08 48 8D 8F D0 04 23 00 E8 A8 F3 00 00 49 8D 57 08 48 8D 8F D0 04 23 00 E8 98 F3 00 00 FF 48 3C 41 80 7F 0C 00 74 0F 41 8B 57 08 "
                "41 B0 01 48 8B CF E8 1F FE FF FF"}, /* its record, +0xC deciding 0x1A8CE0 */
    {0x1A8CE0u, "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 89 54 24 10 57 41 56 41 57 48 83 EC 20 48 8B F1 48 8D 54 24 48 48 81 C1 D0 04 23 00 45 0F B6 F0 "
                "E8 2C F5 00 00 48 8B 96 F8 01 23 00 4C 8B F8 48 8B 3A 48 8B 8F 68 11 00 00 48 85 C9 75 07 48 8B 8F 38 11 00 00 48 8B 01 FF 90 F8 00 00 00 "
                "49 8B 0F 48 8B D0 48 8B D8 E8 35 26 34 00 48 8B D3 48 8B CF E8 2A BE EC FF 48 8B D3 48 8B CF E8 3F 30 F6 FF B2 01 48 8B CF E8 D5 D0 F5 FF "
                "8B D0 48 8B CF E8 BB D2 F5 FF 49 8B 1F 48 85 DB 0F 84 8D 00 00 00 33 FF 39 BB C4 01 00 00 74 32 39 BB C0 01 00 00 76 06 89 BB C0 01 00 00 "
                "48 8B 8B D0 01 00 00 45 33 C0 48 8B 93 C8 01 00 00 48 8B 01 FF 50 40 48 89 BB C8 01 00 00 89 BB C4 01 00 00 39 BB AC 01 00 00 74 32 39 BB "
                "A8 01 00 00 76 06 89 BB A8 01 00 00 48 8B 8B B8 01 00 00 45 33 C0 48 8B 93 B0 01 00 00 48 8B 01 FF 50 40 48 89 BB B0 01 00 00 89 BB AC 01 "
                "00 00 48 8B 86 58 00 23 00 48 8D 8E 58 00 23 00 45 33 C0 48 8B D3 FF 50 40 45 84 F6 74 11 48 8D 54 24 48 48 8D 8E D0 04 23 00 E8 0C 3D 01 "
                "00"}, /* the release this copies, whole */
    {0x1B83E0u, "48 89 5C 24 10 57 48 83 EC 20 83 79 20 00 48 8B FA 48 8B D9"},
    {0x1B8240u, "48 89 5C 24 10 57 48 83 EC 20 83 79 20 00 48 8B FA 48 8B D9"},
    {0x4EB380u, "48 85 D2 0F 84 30 01 00 00 48 89 6C 24 20 57 48 83 EC 20 48 8B FA"},
    {0x74B80u, "48 8B C4 53 48 81 EC 80 00 00 00 80 B9 80 14 00 00 00 48 8B D9 0F 84 0E 01 00 00"},
    {0x10BDA0u, "48 8B C4 48 89 58 10 57 48 81 EC 80 00 00 00 8B B9 98 13 00 00 0F 57 C0 0F 57 C9"},
    {0x525F40u, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8D 99 18 03 00 00 48 8B F1 48 8B 03"}, /* a batch from dev+0x318 */
    {0x1BCB30u, "40 55 48 8B EC 48 83 EC 50 4C 8B D1 8B 49 1C 85"},
    {0x105E40u, "40 53 57 48 83 EC 38 80 B9 80 14 00 00 00 0F B6 FA 48 8B D9"},
    {0x106030u, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20"},
    {0x1A6E00u, "48 89 5C 24 20 55 56 57 41 56 41 57 48 83 EC 20 48 8B 51 20 48 8B F9"}, /* the teardown, rdi = world to its end */
    {0x1A73E0u, "4C 8D BF F8 04 23 00 41 8B DD 41 8B 17 85 D2 74"}, /* it walks the unit map ... */
    {0x1A7488u, "41 3B 1E 73 4D 8B C3 45 33 C0 48 8D 0C 80 49 8B 46 08 48 03 C9 8B 14 C8 48 8B CF E8 38 18 00 00"}, /* ... and releases what the resource map holds */
    {0x10A81Cu, "41 89 BF 50 1A 00 00"}, {0x8A73Fu, "E8 7C FE 07 00"}, /* the one kick, from the frame loop */
};

/* Once, at install: the copy is on only with every pin read and the teardown hooked. */
static void MatResInstall(void)
{
    if (g_matRes.ready != 0) return;
    const uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL);
    uint32_t frame = 0;
    g_matRes.ready = -1;
    g_matRes.off = "no exe";
    if (exe == 0) return;
    for (size_t i = 0; i < sizeof kMatResPins / sizeof kMatResPins[0]; i++)
        if (ApiCodeMatches(exe + kMatResPins[i].at, kMatResPins[i].bytes) != 1) { g_matRes.off = "an exe pin"; Note("difficulty: moving tiles: op 0x41's resources keep the game's wait (exe+0x%X is not the reviewed code)", kMatResPins[i].at); return; }
    for (size_t i = 1; i < sizeof kDrainPins / sizeof kDrainPins[0]; i++)
        if (ApiCodeMatches(exe + kDrainPins[i].at, kDrainPins[i].bytes) != 1) { g_matRes.off = "a fence pin"; Note("difficulty: moving tiles: op 0x41's resources keep the game's wait (exe+0x%X is not the reviewed code)", kDrainPins[i].at); return; }
    (void)NearRenderFrame(&frame);
    if (!g_renderFramePinned) { g_matRes.off = "a kick pin"; Note("difficulty: moving tiles: op 0x41's resources keep the game's wait (the frame kicks are not the reviewed code)"); return; }
    g_matRes.fn = (MatResFns){(MatResRecFn)(exe + 0x1B83E0u), (MatResRecFn)(exe + 0x1B8240u), (MatResCtxFn)(exe + 0x525F40u), (MatResPairFn)(exe + 0x4EB380u),
        (MatResPairFn)(exe + 0x74B80u), (MatResPairFn)(exe + 0x10BDA0u), (MatResFenceFn)(exe + 0x105E40u), (MatResWaitFn)(exe + 0x106030u),
        (MatResEraseFn)(exe + 0x1BCB30u), (MatResReleaseFn)(exe + 0x1A8CE0u), exe + 0x337420u};
    g_matRes.app = exe + 0x1A10208u;
    if (LoaderHook("op 0x41's resources: the world's render teardown", exe + 0x1A6E00u, (void*)MatResTeardown, (void**)&g_matRes.teardown) || g_matRes.teardown == NULL)
    { g_matRes.teardown = NULL; g_matRes.off = "no teardown hook"; Note("difficulty: moving tiles: op 0x41's resources keep the game's wait (the render teardown could not be hooked)"); return; }
    g_matRes.ready = 1;
    g_matRes.off = NULL;
    Note("difficulty: moving tiles: op 0x41's unit resources are freed %u frame kicks after their release instead of behind a fence wait (exe 0x1A8CE0)", MAT_RES_KICKS);
}

/* The build's line (MoveCostNote), then the counts start again. */
static void MatResNote(void)
{
    if (g_matRes.deferred == 0 && g_matRes.fallback == 0 && g_matRes.drains == 0 && g_matRes.leaked == 0 && g_matRes.freed == 0) return;
    Note("difficulty: moving tiles: op 0x41's unit resources this build: %u released without the wait, %u freed after %u frame kicks or a teardown, "
        "pending max %u (now %u), %u by the game's wait (list full or no renderer), %u teardown drains, %u left to a gone level",
        g_matRes.deferred, g_matRes.freed, MAT_RES_KICKS, g_matRes.max, g_matRes.n, g_matRes.fallback, g_matRes.drains, g_matRes.leaked);
    g_matRes.deferred = g_matRes.freed = g_matRes.fallback = g_matRes.drains = g_matRes.leaked = 0;
    g_matRes.max = g_matRes.n;
}

/* ---- Op 0x41's setup create: its fence waited later -----------------------------------------------------------------
 * The setup's unit resource (0x1A8290) pushes its batch (0xC, 0xD) and a flagged fence (0x105E40 dl=1), then blocks in
 * 0x106030 until the render thread reaches that fence: a frame behind, so 10-20 ms (A10c.md). The pushes stay as they
 * are, so the render thread still drains the device's jobs at the fence (0x1132E6) before any later message; only the
 * game thread's wait moves to CoXlTick, once a later frame fence was waited (2 kicks). Until then the fence's slot in
 * [r+0x14E0] stays taken (0x105E40 reuses only empty ones, 0x106030 empties it) and its object stays allocated. */
#define MAT_WAIT_PENDING 8
#define MAT_WAIT_KICKS 2u
#define MAT_WAIT_RETURN 0x1A844Eu /* after 0x1A8449: call 0x106030, 0x1A8290's wait */
typedef struct { uint64_t r; uint32_t id, kick; } MatWaitEntry;
static struct
{
    int ready;             /* 1 hooked, -1 not */
    uintptr_t exe;
    MatResWaitFn orig;     /* the hook's original 0x106030 */
    volatile LONG inSetup; /* the game thread is inside op 0x41's setup call (terrain.c) */
    DWORD thread;
    SRWLOCK lock;          /* the list: CoXlTick and the teardown hook */
    MatWaitEntry e[MAT_WAIT_PENDING];
    uint32_t n, deferred, completed, max, fallback;
} g_matWait = {.lock = SRWLOCK_INIT};

/* What 0x106030 returns at 0x1A844E is not read (the next instruction loads [r14+0x150]), so 0 stands for the wait. */
static uint8_t MatWaitFrom(uint64_t r, uint32_t id, uintptr_t from)
{
    if (g_matWait.inSetup && from == g_matWait.exe + MAT_WAIT_RETURN && GetCurrentThreadId() == g_matWait.thread)
    {
        uint8_t threaded = 0; uint32_t kick = 0; uint64_t dev = 0, table = 0;
        if (!SafeRead((uintptr_t)r + 0x1480, &threaded, 1) && threaded && !SafeRead((uintptr_t)r + 0x1A50, &kick, 4) &&
            (dev = MatResU64(r + 0x1168)) == 0) dev = MatResU64(r + 0x1138);
        if (dev != 0) table = MatResU64(dev);
        /* d3d12's device table only: d3d11's create path (+0xE8 0x706EE0) was not reviewed */
        AcquireSRWLockExclusive(&g_matWait.lock);
        const int room = table == g_matWait.exe + 0x16955D8u && g_matWait.n < MAT_WAIT_PENDING; /* 0 without a render thread */
        if (room)
        {
            g_matWait.e[g_matWait.n++] = (MatWaitEntry){r, id, kick};
            if (g_matWait.n > g_matWait.max) g_matWait.max = g_matWait.n;
            g_matWait.deferred++;
        }
        else g_matWait.fallback++;
        ReleaseSRWLockExclusive(&g_matWait.lock);
        if (room) return 0;
    }
    return g_matWait.orig(r, id);
}
static uint8_t MatWaitDetour(uint64_t r, uint32_t id) { return MatWaitFrom(r, id, (uintptr_t)__builtin_return_address(0)); }

/* Game thread: around the mod's call into op 0x41's setup. */
static void MatWaitSetup(int in)
{
    if (g_matWait.ready <= 0) return;
    g_matWait.thread = GetCurrentThreadId();
    InterlockedExchange(&g_matWait.inSetup, in);
}

/* Game thread, every frame (CoXlTick): a wait whose fence a later frame fence covers goes back to the game's own
 * 0x106030, which then finds the event set and frees the fence; `all` waits the rest too (a teardown, the level's end). */
static void MatWaitComplete(int all)
{
    MatWaitEntry due[MAT_WAIT_PENDING];
    uint32_t keep = 0, n = 0;
    AcquireSRWLockExclusive(&g_matWait.lock);
    for (uint32_t i = 0; i < g_matWait.n; i++)
    {
        MatWaitEntry x = g_matWait.e[i];
        uint32_t kick = 0;
        if (!all && (SafeRead((uintptr_t)x.r + 0x1A50, &kick, 4) || kick - x.kick < MAT_WAIT_KICKS)) g_matWait.e[keep++] = x;
        else due[n++] = x;
    }
    g_matWait.n = keep;
    g_matWait.completed += n;
    ReleaseSRWLockExclusive(&g_matWait.lock);
    for (uint32_t i = 0; i < n; i++) g_matWait.orig(due[i].r, due[i].id); /* outside the lock: it can still block */
}
static void MatWaitTick(void) { if (g_matWait.n != 0) MatWaitComplete(0); }
static void MatWaitDrain(void) { if (g_matWait.n != 0) MatWaitComplete(1); }

/* The bytes the deferral relies on: the call site, the slot scan and reuse (0x105E40), the wait's prologue and its
 * emptying of the slot (0x106030); the render side's flagged drain is kDrainPins' 0x1132E6. */
static const struct { uint32_t at; const char* bytes; } kMatWaitPins[] = {
    {0x1A843Au, "B2 01 48 8B CF E8 FC D9 F5 FF 8B D0 48 8B CF E8 E2 DB F5 FF 49 8B 86 50 01 00 00"}, /* fence(r, 1), wait, then [r14+0x150] */
    {0x105EEBu, "8B 16 85 D2 74 20 48 8B 8B E0 14 00 00 0F 1F 84 00 00 00 00 00 48 83 3C F9 00 0F 84 0E 01 00 00 FF C7 3B FA 75 EF"}, /* an empty slot only */
    {0x106019u, "3B FA 0F 84 F0 FE FF FF 4C 89 34 F9"},
    {0x105FDFu, "8B C7"}, /* the id is the slot */
    {0x105EB4u, "45 33 C9 49 89 76 40 45 33 C0 33 D2 33 C9 FF 15 F0 09 2F 01 49 89 06"}, /* its event: (0, 0, 0, 0), auto-reset */
    {0x106098u, "83 BF B0 13 00 00 00 74 49 BA 01 00 00 00 FF 15 4C 06 2F 01 85 C0 74 5A"}, /* a set event: no wait */
    {0x1060EAu, "33 D2 FF 15 06 06 2F 01 85 C0 40 0F 95 C5 85 C0 74 0E 49 8B 0E BA FF FF FF FF FF 15 EE 05 2F 01"},
    {0x629A6Du, "48 83 C1 10 FF 15 41 CC DC 00 48 8B 0B FF 15 88 CC DC 00"}, /* the render thread sets it */
    {0x106030u, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 80 B9 80 14 00 00 00 48 8B F9 8B F2 0F 84 04 01 00 00 48 81 C1 F0 14 00 00 FF 15 54 06 2F 01 "
                "48 8B 87 E0 14 00 00 48 8D 8F F0 14 00 00 4C 8B 34 F0 48 C7 04 F0 00 00 00 00"}, /* the slot emptied under the lock */
};

/* Once, after MatResInstall: only with its teardown hook (T1 drains these first). */
static void MatWaitInstall(void)
{
    if (g_matWait.ready != 0) return;
    g_matWait.ready = -1;
    const uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL);
    if (exe == 0 || g_matRes.ready != 1) return;
    for (size_t i = 0; i < sizeof kMatWaitPins / sizeof kMatWaitPins[0]; i++)
        if (ApiCodeMatches(exe + kMatWaitPins[i].at, kMatWaitPins[i].bytes) != 1)
        { Note("difficulty: moving tiles: op 0x41's setup keeps the game's fence wait (exe+0x%X is not the reviewed code)", kMatWaitPins[i].at); return; }
    g_matWait.exe = exe;
    if (LoaderHook("op 0x41's setup: its fence wait", exe + 0x106030u, (void*)MatWaitDetour, (void**)&g_matWait.orig) || g_matWait.orig == NULL)
    { g_matWait.orig = NULL; Note("difficulty: moving tiles: op 0x41's setup keeps the game's fence wait (0x106030 could not be hooked)"); return; }
    g_matWait.ready = 1;
    Note("difficulty: moving tiles: op 0x41's setup no longer waits for its resource's fence; the wait is done %u frame kicks later (exe 0x1A8449)", MAT_WAIT_KICKS);
}

/* The build's line (MoveCostNote). */
static void MatWaitNote(void)
{
    if (g_matWait.deferred == 0 && g_matWait.completed == 0 && g_matWait.fallback == 0) return;
    Note("difficulty: moving tiles: op 0x41's setup create waits this build: %u deferred, %u completed later, pending max %u (now %u), %u waited inline (list full, "
        "no render thread or not d3d12)", g_matWait.deferred, g_matWait.completed, g_matWait.max, g_matWait.n, g_matWait.fallback);
    g_matWait.deferred = g_matWait.completed = g_matWait.fallback = 0;
    g_matWait.max = g_matWait.n;
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
static int NearTerrainEntry(uint64_t world, uint64_t handle, uint64_t* at)
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
        if (key == handle) { *at = entries + (uint64_t)i * 0x170; return 1; }
        if (next == 0x7FFFFFFFu) return 0;
        i = next;
    }
    return -1;
}

static int NearTerrainIn(uint64_t world, uint64_t handle) { uint64_t at = 0; return NearTerrainEntry(world, handle, &at); }

/* Under the world's terrain lock, which the destroy takes too (so never held across it). */
static int NearTerrainInLocked(uint64_t world, uint64_t handle)
{
    const int locked = world != 0 && g_nearTerrainLock[0] != NULL && g_nearTerrainLock[1] != NULL;
    if (locked) g_nearTerrainLock[0](world + 0x230520u);
    const int in = NearTerrainIn(world, handle);
    if (locked) g_nearTerrainLock[1](world + 0x230520u);
    return in;
}

/* A handle is its terrain's address (the spawn returns the 0xAA8-byte object, exe 0x1ABF49..0x1AC198), which a later
 * terrain can get again. The entry also keeps the spawn's descriptor (copied at 0x1ABFAA..0x1AC038): the heightmap
 * it was spawned with at +0x40 and the material map at +0x48 (0x1AB0A7, 0x1AB0B5), which tell the two apart.
 * That field (`material`: the second) of a listed terrain; 0 when it is not listed or does not read. */
static uint64_t NearTerrainMapLocked(uint64_t world, uint64_t handle, int material)
{
    const int locked = world != 0 && g_nearTerrainLock[0] != NULL && g_nearTerrainLock[1] != NULL;
    uint64_t at = 0, map = 0;
    if (locked) g_nearTerrainLock[0](world + 0x230520u);
    if (NearTerrainEntry(world, handle, &at) != 1 || SafeRead(at + (material ? 0x48u : 0x40u), &map, 8)) map = 0;
    if (locked) g_nearTerrainLock[1](world + 0x230520u);
    return map;
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
static int NearJobRuns(uintptr_t level)
{
    uintptr_t job = 0;
    uint8_t runs = 0;
    return level != 0 && !SafeRead(level + 0xF718u, &job, 8) && job != 0 && !SafeRead(job + 8, &runs, 1) && runs != 0;
}
static int NearGeneratorRuns(void) { return NearJobRuns(g_worldLevel); }

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
 * the list first. An address in the list may be another unit's by now, so it must also be this build's: its world at
 * +0x18 and the pose it was spawned with at +0x20 (the constructor, exe 0xD7879..0xD78A8), which stands the offset
 * north of the tile, where nothing of the level is. -1 when they could not be looked up: kept. */
static int MoveStampUnitMine(uint64_t world, uint64_t unit)
{
    uint64_t in = 0;
    float y = 0.0f;
    return !SafeRead((uintptr_t)unit + 0x18, &in, 8) && in == world && !SafeRead((uintptr_t)unit + DIF_STREAM_UNIT_POSE + 13 * 4, &y, 4) &&
        fabsf(y - (g_move.centre[1] + MOVE_STAMP_OFFSET)) <= NEAR_SIDE / 2 + 2 * MOVE_STAMP_MARGIN;
}

static int MoveStampUnitsLeft(uint64_t world, uint32_t* absent, uint32_t* other, int destroyThem)
{
    uintptr_t wi = 0, table = 0, destroy = 0;
    uint64_t list = 0, *copy = NULL;
    uint32_t count = 0;
    *absent = *other = 0;
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
        if (!MoveStampUnitMine(world, unit)) { (*other)++; continue; }
        if (destroyThem)
        {
            if (gone == 0) ((WorldStreamAnimWaitFn)wait)(world); /* as every game destroy */
            ((WorldStreamDestroyUnitFn)destroy)(world, unit, 0, 0);
        }
        gone++;
    }
    if (copy != NULL) VirtualFree(copy, 0, MEM_RELEASE);
    return gone;
}

/* The generator is idle, which every call below rests on: its job gone and, at the ship, state 0, which only the
 * level's own teardown leaves behind (0xF7444A..0xF7444F). A level init has written its own 1 before the hook
 * (0xF7136D) and comes only once the last generation has ended (co_drain, the preview tick, the game's retry). */
static int MoveGeneratorIdle(int ship, uint32_t* state, int* job)
{
    const uintptr_t level = g_move.levelPtr != 0 ? g_move.levelPtr : g_worldLevel;
    *job = NearJobRuns(level);
    if (level == 0 || SafeRead(level + 0x1E4, state, 4)) { *state = 0xFFFFFFFFu; return 0; }
    return *state == (ship ? 0u : 1u) && !*job;
}

/* The ship's set-up or a level init, on the game thread, before the tile terrains go: what a build under way left in
 * the world goes as its own failure would take it, in one call (no frame of this level follows). While the generator
 * is not idle everything is kept as it is for the next level init. The package's release is never sent here
 * (NearLevelLeft). */
static void MoveBuildLeft(int ship)
{
    const char* why = ship ? "the ship" : "a level init";
    Split63Left(why); /* first: no range of its renders again */
    const int state = g_move.state, merge = g_move.merge, held = g_move.acquired;
    const uint32_t units = g_move.stTemp > g_move.stGone ? g_move.stTemp - g_move.stGone : 0;
    if (units == 0 && !merge && !held)
    {
        if (state == MV_IDLE || g_move.cutNoted) return;
        g_move.cutNoted = 1;
        Note("difficulty: moving tiles: %s: the build of cell (%d, %d) was cut short in step '%s' with no stamp unit, merge or package of it left; "
            "its tile goes with the tile terrains", why, g_move.cell[0], g_move.cell[1], MoveStateName(state));
        return;
    }
    uint32_t generator = 0, absent = 0, other = 0;
    int job = 0;
    if (ship) g_move.shipSeen = 1; /* kept for a level init that takes this build over */
    if (!MoveGeneratorIdle(ship, &generator, &job))
    {
        Note("difficulty: moving tiles: %s: the build of cell (%d, %d) was cut short in step '%s', and the level generator is not idle (state %u, its job %s): "
            "its %u stamp units, its merge (%s) and its package (%s) wait for the next level init", why, g_move.cell[0], g_move.cell[1], MoveStateName(state),
            generator, job ? "runs" : "is gone", units, merge ? "up" : "not up", held ? "held" : "not held");
        return;
    }
    /* Once the ship is reached the mission's unload has destroyed every unit of the world (game 0xAB3CD9..0xAB3DEE: it
     * starts at generator state 0 and goes on until none is left), and the ship's units live in the same world: an
     * address of the build's that is listed again is another unit's. So the units are forgotten, never destroyed; only
     * a level init straight from play still destroys what it finds. */
    const int destroyThem = !g_move.shipSeen;
    const int ring = MoveSpawnRingEmpty(MoveWorld());
    LARGE_INTEGER t0, t1, hz;
    QueryPerformanceCounter(&t0);
    const int gone = units != 0 ? MoveStampUnitsLeft(MoveWorld(), &absent, &other, destroyThem) : 0;
    g_move.stTemp = g_move.stGone = 0; g_move.stamped = 0; /* gone or not: the next build's list takes their place */
    g_nearStampOffset[0] = g_nearStampOffset[1] = 0.0f;    /* else the next level's own 0x63 would look 20 km off */
    const int torn = MoveTeardown(1);
    MovePackageLater();
    g_move.cutNoted = 1;
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&hz);
    char others[224] = "", ringText[40] = "";
    int at = 0;
    if (other != 0) at = snprintf(others, sizeof others, ", %u at their addresses are not this build's units (left alone)", other);
    if (!destroyThem && gone > 0)
        snprintf(others + at, sizeof others - (size_t)at, ", WARNING: %d still listed at the build's place are left in the world (nothing is destroyed after the ship)", gone);
    if (destroyThem) snprintf(ringText, sizeof ringText, "; the spawn ring %s", ring == 1 ? "empty" : ring == 0 ? "not empty" : "did not read");
    Note("difficulty: moving tiles: %s: the build of cell (%d, %d) was cut short in step '%s': %d of its %u stamp units destroyed, %u already gone%s%s; "
        "the merge %s; the merge package %s (generator state %u; R+0x108 %s; %.1f ms on the game thread%s)", why, g_move.cell[0], g_move.cell[1],
        MoveStateName(state), gone < 0 || !destroyThem ? 0 : gone, units, absent, others,
        gone >= 0 ? "" : destroyThem ? ", the rest KEPT in the world (its unit list or the destroy did not read)" : ", the rest not looked up (the unit list did not read)",
        torn ? "torn down" : merge ? "NOT torn down" : "was not up",
        !held ? "was not held" : ship ? "is left to the next level init, which takes it over" : "is this level init's now",
        generator, !torn ? "not looked at" : g_move.r108Out == 1 ? "destroyed" : g_move.r108Out == 2 ? "already gone" : "none",
        (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart, ringText);
}
