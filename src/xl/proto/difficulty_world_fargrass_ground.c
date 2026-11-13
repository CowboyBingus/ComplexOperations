/* Extra large maps: the ground height the far-grass overlay fades against. Included after xl_free.c.
 * The overlay paints only near the height map it samples, and the level's map lies up to 5 m under a live tile's ground
 * (session 58fcd496), so grass faded out exactly where the 3D grass ends. hellforge-data.json points that sample at t8,
 * named complex_operations_far_grass_ground (bound by nothing else); this publishes the texture under that name.
 * A texel per 4 m, centred as the overlay samples it: the drawn ground at its centre, raised by how far the ground rises
 * above the texels' bilinear surface in the cells around it. So the interpolated height is never under the ground, and
 * on a plane of any slope it is the ground (a box maximum stood 4 m above a 45 degree slope and painted rocks there).
 * Format 22, the level heightmap's own: u16 unorm, (h + 500) / 1000 as the shader decodes it, rounded up.
 * Whenever ours is not in, the level's own height map stands under the name: t8 unbound would read -500 m and paint
 * nothing. HELLFORGE_XL_FARGRASS_GROUND=0 keeps it there. */

#define FG_GROUND_NAME 0x612E769Eu      /* murmur64("complex_operations_far_grass_ground") >> 32 */
#define FG_GROUND_LEVEL_MAP 0x2A9E3232u /* generated_heightmap */
#define FG_GROUND_N 4096
#define FG_GROUND_TEXEL (16384.0f / (float)FG_GROUND_N)
#define FG_GROUND_FORMAT 22u             /* 16 bits a texel in the upload's format table (exe 0x1658990), not a block format */
#define FG_GROUND_Q (65535.0f / 1000.0f) /* u16 steps a metre */

typedef struct { NearGround ground; float centre[2]; } FgGroundTile;
typedef struct { NearGround coarse; FgGroundTile tiles[NEAR_TILES_MAX]; int count; uint16_t* out; } FgGroundJob;

static struct
{
    LONG level;
    uint16_t* pixels;    /* the texture's pixels; the render thread reads them once, NEAR_PIXELS_FRAMES after the create */
    uint64_t tex, shown; /* our texture, and what stands under the name (ours or the level's map) */
    uint32_t madeFrame; int uploading;
    LONG builtGen, buildingGen;
    HANDLE worker; volatile LONG done;
    FgGroundJob job;
    ULONGLONG started; double lastSeconds;
    uint32_t builds; int edited; /* the loaded variant reads the name: 1 yes, -1 no, 0 not looked yet this level */
    int stopped;                 /* no render frame count: no more builds this level */
} g_fgGround = {.level = -1};

/* The game thread's update intervals around an upload: a texture made with data is copied on the render thread in the
 * frame it is made, and the game thread waits for that frame's fence a frame later (xl_free.c). */
#define FG_CLOCK_BEFORE 32
#define FG_CLOCK_AFTER 8
static struct { LARGE_INTEGER last; float before[FG_CLOCK_BEFORE], after[FG_CLOCK_AFTER]; int n, at, left; uint32_t build; } g_fgClock;

static int FgGroundOn(void)
{
    static int on = -1;
    if (on < 0) { char t[4] = ""; on = !(GetEnvironmentVariableA("HELLFORGE_XL_FARGRASS_GROUND", t, sizeof t) > 0 && t[0] == '0'); }
    return on;
}
/* Whether the loaded overlay variant reads the name: its t8 entry renamed and its fade sample on t8 (both stages), as
 * hellforge-data.json edits them. Without that edit (an older HellForge, or a spec without it) nothing reads our texture. */
static int FgGroundEdited(void)
{
    typedef uint8_t (*CanGetFn)(uint64_t type, uint64_t name);
    typedef uint64_t (*GetFn)(uint64_t name, uint64_t type);
    CanGetFn canGet = (CanGetFn)NearApi(0x10, 0x388);
    GetFn get = (GetFn)NearApi(0x10, 0x3F8);
    if (canGet == NULL || get == NULL || !canGet(FG_SHADER_LIBRARY, kFgShaders[0].name)) return 0;
    const uint64_t lib = get(kFgShaders[0].name, FG_SHADER_LIBRARY);
    uint32_t name = 0, t0 = 0, t1 = 0;
    return lib != 0 && !SafeRead(lib + 0x62C, &name, 4) && name == FG_GROUND_NAME && !SafeRead(lib + kFgShaders[0].at[0] + 0x3348, &t0, 4) && t0 == 8 &&
        !SafeRead(lib + kFgShaders[0].at[1] + 0x3348, &t1, 4) && t1 == 8;
}


/* The ground as drawn at a point (NearVisibleAt's rule over the job's snapshot): the tile alone inside a hidden square,
 * the higher of tile and coarse terrain over the rest of a tile's span, the coarse terrain elsewhere. */
static float FgGroundAt(const FgGroundJob* j, float x, float y)
{
    float best = -9999.0f;
    for (int k = 0; k < j->count; k++)
    {
        const FgGroundTile* t = &j->tiles[k];
        float dx = fabsf(x - t->centre[0]), dy = fabsf(y - t->centre[1]);
        if (dx <= NEAR_HIDE / 2 && dy <= NEAR_HIDE / 2) return NearGroundAt(&t->ground, x, y);
        if (dx <= t->ground.size / 2 && dy <= t->ground.size / 2) best = fmaxf(best, NearGroundAt(&t->ground, x, y));
    }
    return fmaxf(best, NearGroundAt(&j->coarse, x, y));
}

static float FgTexelX(int c) { return ((float)c + 0.5f) * FG_GROUND_TEXEL - 8192.0f; }
static float FgTexelY(int r) { return 8192.0f - ((float)r + 0.5f) * FG_GROUND_TEXEL; }
static uint16_t FgGroundEncode(float h)
{
    float q = ceilf((h + 500.0f) * FG_GROUND_Q);
    return (uint16_t)(q < 0.0f ? 0.0f : q > 65535.0f ? 65535.0f : q);
}
static float FgGroundDecode(uint32_t q) { return (float)q / FG_GROUND_Q - 500.0f; }

static void FgGroundPoints(const FgGroundJob* j, int r, uint16_t* row)
{
    const float y = FgTexelY(r);
    for (int c = 0; c < FG_GROUND_N; c++) row[c] = FgGroundEncode(FgGroundAt(j, FgTexelX(c), y));
}

/* Every sample of grid g in the strip between texel rows y0 (top) and y1: how far the drawn ground there rises above the
 * bilinear surface of the four texels around it, the largest kept per cell in d. */
static void FgGroundGridRises(const FgGroundJob* j, const NearGround* g, const uint16_t* top, const uint16_t* bottom, float y0, float y1, float* d)
{
    if (g->v == NULL || g->n < 2) return;
    const float step = g->size / (float)(g->n - 1), left = g->cx - g->size / 2, north = g->cy + g->size / 2, last = (float)(g->n - 1);
    const float i0 = fmaxf(ceilf((north - y0) / step), 0.0f), i1 = fminf(floorf((north - y1) / step), last);
    const float k0 = fmaxf(ceilf((FgTexelX(0) - left) / step), 0.0f), k1 = fminf(floorf((FgTexelX(FG_GROUND_N - 1) - left) / step), last);
    for (float i = i0; i <= i1; i++)
    {
        const float sy = north - i * step, v = (y0 - sy) / FG_GROUND_TEXEL;
        for (float k = k0; k <= k1; k++)
        {
            const float sx = left + k * step;
            int c = (int)floorf((sx - FgTexelX(0)) / FG_GROUND_TEXEL);
            if (c > FG_GROUND_N - 2) c = FG_GROUND_N - 2;
            const float u = (sx - FgTexelX(c)) / FG_GROUND_TEXEL;
            const float b = (FgGroundDecode(top[c]) * (1 - u) + FgGroundDecode(top[c + 1]) * u) * (1 - v) +
                (FgGroundDecode(bottom[c]) * (1 - u) + FgGroundDecode(bottom[c + 1]) * u) * v;
            const float rise = FgGroundAt(j, sx, sy) - b;
            if (rise > d[c]) d[c] = rise;
        }
    }
}

/* Per cell between texel rows r and r + 1 (cell c spans columns c and c + 1): the drawn ground's rise above the texels'
 * bilinear surface at the samples of every grid drawn there, plus a quarter of the cell's twist, which bounds how far
 * that surface can part from the samples' between them. */
static void FgGroundRises(const FgGroundJob* j, const uint16_t* top, const uint16_t* bottom, int r, float* d)
{
    const float y0 = FgTexelY(r), y1 = FgTexelY(r + 1);
    for (int c = 0; c < FG_GROUND_N - 1; c++) d[c] = 0.0f;
    FgGroundGridRises(j, &j->coarse, top, bottom, y0, y1, d);
    for (int k = 0; k < j->count; k++)
    {
        const NearGround* g = &j->tiles[k].ground;
        if (y1 <= g->cy + g->size / 2 && y0 >= g->cy - g->size / 2) FgGroundGridRises(j, g, top, bottom, y0, y1, d);
    }
    for (int c = 0; c < FG_GROUND_N - 1; c++)
        d[c] += fabsf(FgGroundDecode(top[c]) - FgGroundDecode(top[c + 1]) - FgGroundDecode(bottom[c]) + FgGroundDecode(bottom[c + 1])) / 4.0f;
}

/* Rows r0 .. r1 - 1 of the texture into j->out: each texel its centre's ground, raised by the largest rise of the (up to
 * four) cells it is a corner of. A cell's four corners all rise by at least its rise, and bilinear weights sum to one. */
static void FgGroundRows(const FgGroundJob* j, int r0, int r1)
{
    uint16_t rows[3][FG_GROUND_N];
    float cells[2][FG_GROUND_N];
    uint16_t *above = rows[0], *here = rows[1], *below = rows[2];
    float *dAbove = cells[0], *dHere = cells[1];
    FgGroundPoints(j, r0, here);
    if (r0 > 0) { FgGroundPoints(j, r0 - 1, above); FgGroundRises(j, above, here, r0 - 1, dAbove); }
    else for (int c = 0; c < FG_GROUND_N - 1; c++) dAbove[c] = 0.0f;
    for (int r = r0; r < r1; r++)
    {
        if (r + 1 < FG_GROUND_N) { FgGroundPoints(j, r + 1, below); FgGroundRises(j, here, below, r, dHere); }
        else for (int c = 0; c < FG_GROUND_N - 1; c++) dHere[c] = 0.0f;
        uint16_t* out = j->out + (size_t)r * FG_GROUND_N;
        for (int c = 0; c < FG_GROUND_N; c++)
        {
            float rise = 0.0f;
            if (c > 0) rise = fmaxf(rise, fmaxf(dAbove[c - 1], dHere[c - 1]));
            if (c < FG_GROUND_N - 1) rise = fmaxf(rise, fmaxf(dAbove[c], dHere[c]));
            float q = (float)here[c] + ceilf(rise * FG_GROUND_Q);
            out[c] = (uint16_t)(q > 65535.0f ? 65535.0f : q);
        }
        uint16_t* t = above; above = here; here = below; below = t;
        float* s = dAbove; dAbove = dHere; dHere = s;
    }
}

static DWORD WINAPI FgGroundWorker(LPVOID unused)
{
    (void)unused;
    FgGroundRows(&g_fgGround.job, 0, FG_GROUND_N);
    InterlockedExchange(&g_fgGround.done, 1);
    return 0;
}

static void FgGroundPublish(uint64_t tex)
{
    NearPublishFn publish = (NearPublishFn)NearApi(0x10, 0x210);
    if (publish != NULL && tex != 0) { publish(FG_GROUND_NAME, tex); g_fgGround.shown = tex; }
}

/* While ours is not in: the level's own height map (its one-mip copy while published, else the render config's, which
 * lives as long as the game). Looked up each update, as the one-mip copy is made and dropped per level. */
static void FgGroundFallback(void)
{
    if (g_fgGround.tex != 0 && g_fgGround.shown == g_fgGround.tex) return;
    NearFindFn find = (NearFindFn)NearApi(0x10, 0x158);
    uint64_t own = find != NULL ? find(FG_GROUND_LEVEL_MAP) : 0;
    if (own != 0 && own != g_fgGround.shown) FgGroundPublish(own);
}

static void FgGroundDestroy(uint64_t tex)
{
    typedef void (*DestroyFn)(uint64_t tex);
    DestroyFn destroy = (DestroyFn)NearApi(0x10, 0x6B0);
    if (tex != 0 && destroy != NULL) destroy(tex);
}

static void FgGroundClock(void)
{
    LARGE_INTEGER now, hz;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&hz);
    if (g_fgClock.last.QuadPart != 0)
    {
        float ms = (float)((double)(now.QuadPart - g_fgClock.last.QuadPart) * 1000.0 / (double)hz.QuadPart);
        if (g_fgClock.left > 0)
        {
            g_fgClock.after[FG_CLOCK_AFTER - g_fgClock.left] = ms;
            if (--g_fgClock.left == 0)
            {
                float v[FG_CLOCK_BEFORE], a = 0.0f;
                int n = g_fgClock.n;
                memcpy(v, g_fgClock.before, sizeof v);
                for (int p = 1; p < n; p++) for (int q = p; q > 0 && v[q] < v[q - 1]; q--) { float t = v[q]; v[q] = v[q - 1]; v[q - 1] = t; }
                for (int k = 0; k < FG_CLOCK_AFTER; k++) a = fmaxf(a, g_fgClock.after[k]);
                Note("difficulty: far grass: ground heights build %u's upload: the %d updates after it %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f ms (longest %.1f); "
                    "the %d before: median %.1f ms, longest %.1f", g_fgClock.build, FG_CLOCK_AFTER, g_fgClock.after[0], g_fgClock.after[1], g_fgClock.after[2],
                    g_fgClock.after[3], g_fgClock.after[4], g_fgClock.after[5], g_fgClock.after[6], g_fgClock.after[7], a, n, v[n / 2], v[n - 1]);
            }
        }
        else { g_fgClock.before[g_fgClock.at] = ms; g_fgClock.at = (g_fgClock.at + 1) % FG_CLOCK_BEFORE; if (g_fgClock.n < FG_CLOCK_BEFORE) g_fgClock.n++; }
    }
    g_fgClock.last = now;
}

/* Without the render thread's frame count the pixels' upload cannot be timed, so they could never be reused or freed;
 * and with [r+0x1480] clear, which that count needs, neither the create nor the destroy reaches the GPU (0x31E8A8,
 * 0x31E93B): a texture made then would only leak. */
static int FgGroundFramed(uint32_t* frame, const char* when)
{
    if (NearRenderFrame(frame)) return 1;
    if (!g_fgGround.stopped) Note("difficulty: far grass: no render frame count %s; the ground heights stop at build %u (the overlay keeps the level's map until then)", when, g_fgGround.builds);
    g_fgGround.stopped = 1;
    return 0;
}

/* The texture from the worker's pixels, under the name; the one it replaces goes after (same command stream). */
static void FgGroundMake(void)
{
    typedef uint64_t (*CreateFn)(uint32_t w, uint32_t h, uint32_t format, void* pixels);
    CreateFn create = (CreateFn)NearApi(0x10, 0x6A8);
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL);
    uint32_t frame = 0;
    g_fgGround.lastSeconds = (double)(GetTickCount64() - g_fgGround.started) / 1000.0;
    if (!FgGroundFramed(&frame, "at the create")) return;
    uint64_t tex = (uintptr_t)create == exe + 0x31E740u && ApiCodeMatches(exe + 0x31E740u, "48 89 5C 24 08 48 89 6C 24 10 56 57 41 54 41 56 41 57 48 83 EC 50") &&
        (uintptr_t)NearApi(0x10, 0x6B0) == exe + 0x31E920u ? create(FG_GROUND_N, FG_GROUND_N, FG_GROUND_FORMAT, g_fgGround.pixels) : 0;
    if (tex == 0) { Note("difficulty: far grass: the ground texture was not created; the overlay keeps the level's heights"); return; }
    g_fgGround.madeFrame = frame; g_fgGround.uploading = 1;
    uint64_t old = g_fgGround.tex;
    g_fgGround.tex = tex; g_fgGround.builtGen = g_fgGround.buildingGen; g_fgGround.builds++;
    FgGroundPublish(tex);
    FgGroundDestroy(old);
    if (g_fgGround.builds <= 4 || g_fgGround.builds % 16 == 0)
    {
        Note("difficulty: far grass: ground heights for the overlay: build %u, %d tiles, %.2f s; texture 0x%llX", g_fgGround.builds, g_fgGround.job.count,
            g_fgGround.lastSeconds, (unsigned long long)tex);
        if (g_fgClock.n > 0) { g_fgClock.left = FG_CLOCK_AFTER; g_fgClock.build = g_fgGround.builds; }
    }
}

/* The pixels are free again once the render thread has copied them (NEAR_PIXELS_FRAMES kicks after the create). */
static int FgGroundPixelsFree(void)
{
    uint32_t now = 0;
    if (!g_fgGround.uploading) return 1;
    if (!FgGroundFramed(&now, "after an upload") || now - g_fgGround.madeFrame < NEAR_PIXELS_FRAMES) return 0;
    g_fgGround.uploading = 0;
    return 1;
}

/* Off an extra large level the pixels go as soon as the render thread is done with them. */
static void FgGroundFreePixels(void)
{
    if (g_fgGround.pixels == NULL || g_fgGround.worker != NULL || !FgGroundPixelsFree()) return;
    VirtualFree(g_fgGround.pixels, 0, MEM_RELEASE);
    g_fgGround.pixels = NULL;
}

static void FgGroundLevelLeft(void)
{
    if (g_fgGround.worker != NULL) { WaitForSingleObject(g_fgGround.worker, INFINITE); CloseHandle(g_fgGround.worker); g_fgGround.worker = NULL; }
    if (g_fgGround.shown != 0)
    {
        NearNameFn unpublish = (NearNameFn)NearApi(0x10, 0x218);
        if (unpublish != NULL) unpublish(FG_GROUND_NAME);
    }
    FgGroundDestroy(g_fgGround.tex);
    g_fgGround.tex = g_fgGround.shown = 0; g_fgGround.done = 0; g_fgGround.level = -1; g_fgGround.builds = 0; g_fgGround.edited = 0;
    g_fgGround.stopped = 0;
    memset(&g_fgClock, 0, sizeof g_fgClock);
    FgGroundFreePixels();
}

/* After the level update, on the game thread (the render API's thread). */
static void FgGroundTick(uint64_t level)
{
    if (level == 0 || level != g_worldLevelLive || g_world.live != 8.0f)
    {
        if (g_fgGround.level < 0) FgGroundFreePixels();
        return;
    }
    if (g_fgGround.level != g_world.levels)
    {
        if (g_fgGround.level >= 0) FgGroundLevelLeft();
        g_fgGround.level = g_world.levels; g_fgGround.builtGen = g_fgGround.buildingGen = g_tilesGen - 1;
    }
    FgGroundClock();
    FgGroundFallback();
    if (g_fgGround.edited == 0)
    {
        g_fgGround.edited = FgGroundEdited() ? 1 : -1;
        Note("difficulty: far grass: the overlay's height fade %s", g_fgGround.edited > 0 ? (FgGroundOn() ? "reads the drawn ground (complex_operations_far_grass_ground)" :
            "reads the level's map (HELLFORGE_XL_FARGRASS_GROUND=0)") : "is the game's: the level's map (no fade edit in the loaded variant)");
    }
    if (!FgGroundOn() || g_fgGround.edited < 0 || g_fgGround.stopped) return;
    if (g_fgGround.worker != NULL)
    {
        if (!g_fgGround.done) return;
        WaitForSingleObject(g_fgGround.worker, INFINITE); CloseHandle(g_fgGround.worker); g_fgGround.worker = NULL; g_fgGround.done = 0;
        FgGroundMake();
        return;
    }
    LONG gen = g_tilesGen;
    uint32_t frame = 0;
    if (gen == g_fgGround.builtGen || g_nearCoarseGround.v == NULL || !FgGroundPixelsFree() || !FgGroundFramed(&frame, "before a build")) return;
    if (g_fgGround.pixels == NULL)
    {
        g_fgGround.pixels = (uint16_t*)VirtualAlloc(NULL, (size_t)FG_GROUND_N * FG_GROUND_N * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (g_fgGround.pixels == NULL) { g_fgGround.builtGen = gen; Note("difficulty: far grass: no memory for the ground heights; the overlay keeps the level's"); return; }
    }
    FgGroundJob* j = &g_fgGround.job;
    j->coarse = g_nearCoarseGround; j->out = g_fgGround.pixels; j->count = 0;
    AcquireSRWLockShared(&g_tilesLock);
    for (int k = 0; k < NEAR_TILES_MAX; k++)
        if (g_tiles[k].live && g_tiles[k].level == g_world.levels && g_tiles[k].ground.v != NULL)
        { j->tiles[j->count].ground = g_tiles[k].ground; j->tiles[j->count].centre[0] = g_tiles[k].centre[0]; j->tiles[j->count].centre[1] = g_tiles[k].centre[1]; j->count++; }
    ReleaseSRWLockShared(&g_tilesLock);
    g_fgGround.buildingGen = gen; g_fgGround.started = GetTickCount64(); g_fgGround.done = 0;
    g_fgGround.worker = CreateThread(NULL, 256 * 1024, FgGroundWorker, NULL, 0, NULL);
    if (g_fgGround.worker == NULL) { g_fgGround.builtGen = gen; Note("difficulty: far grass: no worker thread for the ground heights"); return; }
    SetThreadPriority(g_fgGround.worker, THREAD_PRIORITY_BELOW_NORMAL);
}
