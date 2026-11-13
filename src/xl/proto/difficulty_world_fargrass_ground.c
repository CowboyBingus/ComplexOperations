/* Extra large maps: the ground height the far-grass overlay fades against. Included after xl_free.c.
 * The overlay paints only near the height map it samples, and the level's map lies up to 5 m under a live tile's ground
 * (session 58fcd496), so grass faded out exactly where the 3D grass ends. hellforge-data.json points that sample at t8,
 * named complex_operations_far_grass_ground (bound by nothing else); this publishes the texture under that name. A texel
 * per 4 m, centred as the overlay samples it, holds the highest drawn ground within its bilinear reach, so the
 * interpolated height is never under the ground. Format 1 (R32_FLOAT), (h + 500) / 1000 as the shader decodes it. Until
 * the first build the level's map stands under the name; HELLFORGE_XL_FARGRASS_GROUND=0 keeps it there. */

#define FG_GROUND_NAME 0x612E769Eu /* murmur64("complex_operations_far_grass_ground") >> 32 */
#define FG_GROUND_N 4096
#define FG_GROUND_TEXEL (16384.0f / (float)FG_GROUND_N)

typedef struct { NearGround ground; float centre[2]; } FgGroundTile;
typedef struct { NearGround coarse; FgGroundTile tiles[NEAR_TILES_MAX]; int count; float* out; } FgGroundJob;

static struct
{
    LONG level;
    float* pixels;       /* the texture's pixels; the render thread reads them once, NEAR_PIXELS_FRAMES after the create */
    uint64_t tex, shown; /* our texture, and what stands under the name (ours or the level's map) */
    uint32_t madeFrame; int framed;
    LONG builtGen, buildingGen;
    HANDLE worker; volatile LONG done;
    FgGroundJob job;
    ULONGLONG started; double lastSeconds;
    uint32_t builds; int edited; /* the loaded variant reads the name: 1 yes, -1 no, 0 not looked yet this level */
} g_fgGround = {.level = -1};

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

/* The highest sample of g whose cell meets [x0, x1] x [y0, y1]: what its bilinear surface reaches there. -9999: none. */
static float FgSamplesMax(const NearGround* g, float x0, float y0, float x1, float y1)
{
    if (g->v == NULL || g->n < 2) return -9999.0f;
    const float step = g->size / (float)(g->n - 1), left = g->cx - g->size / 2, top = g->cy + g->size / 2, last = (float)(g->n - 1);
    float c0 = floorf((x0 - left) / step), c1 = ceilf((x1 - left) / step), r0 = floorf((top - y1) / step), r1 = ceilf((top - y0) / step);
    if (c1 < 0 || r1 < 0 || c0 > last || r0 > last) return -9999.0f;
    int a = (int)fmaxf(c0, 0), b = (int)fminf(c1, last), p = (int)fmaxf(r0, 0), q = (int)fminf(r1, last);
    uint16_t best = 0;
    for (int r = p; r <= q; r++)
    {
        const uint16_t* row = g->v + (size_t)r * g->n;
        for (int c = a; c <= b; c++) if (row[c] > best) best = row[c];
    }
    return (float)best / 65535.0f * 1000.0f - 500.0f;
}

/* Texel (c, r)'s height in metres: the highest drawn ground within its bilinear reach. */
static float FgGroundTexel(const FgGroundJob* j, int c, int r)
{
    const float x = ((float)c + 0.5f) * FG_GROUND_TEXEL - 8192.0f, y = 8192.0f - ((float)r + 0.5f) * FG_GROUND_TEXEL, h = FG_GROUND_TEXEL;
    float best = -9999.0f;
    for (int k = 0; k < j->count; k++)
    {
        const FgGroundTile* t = &j->tiles[k];
        float dx = fabsf(x - t->centre[0]), dy = fabsf(y - t->centre[1]);
        /* wholly in a hidden square: no coarse terrain is drawn there */
        if (dx + h <= NEAR_HIDE / 2 && dy + h <= NEAR_HIDE / 2) return FgSamplesMax(&t->ground, x - h, y - h, x + h, y + h);
        if (dx - h <= t->ground.size / 2 && dy - h <= t->ground.size / 2) best = fmaxf(best, FgSamplesMax(&t->ground, x - h, y - h, x + h, y + h));
    }
    return fmaxf(best, FgSamplesMax(&j->coarse, x - h, y - h, x + h, y + h));
}

static void FgGroundRows(const FgGroundJob* j, int r0, int r1)
{
    for (int r = r0; r < r1; r++)
        for (int c = 0; c < FG_GROUND_N; c++)
        {
            float m = FgGroundTexel(j, c, r);
            j->out[(size_t)r * FG_GROUND_N + c] = m < -9000.0f ? 0.0f : (m + 500.0f) / 1000.0f;
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

static void FgGroundDestroy(uint64_t tex)
{
    typedef void (*DestroyFn)(uint64_t tex);
    DestroyFn destroy = (DestroyFn)NearApi(0x10, 0x6B0);
    if (tex != 0 && destroy != NULL) destroy(tex);
}

/* The texture from the worker's pixels, under the name; the one it replaces goes after (same command stream). */
static void FgGroundMake(void)
{
    typedef uint64_t (*CreateFn)(uint32_t w, uint32_t h, uint32_t format, void* pixels);
    CreateFn create = (CreateFn)NearApi(0x10, 0x6A8);
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL);
    uint64_t tex = (uintptr_t)create == exe + 0x31E740u && ApiCodeMatches(exe + 0x31E740u, "48 89 5C 24 08 48 89 6C 24 10 56 57 41 54 41 56 41 57 48 83 EC 50") &&
        (uintptr_t)NearApi(0x10, 0x6B0) == exe + 0x31E920u ? create(FG_GROUND_N, FG_GROUND_N, 1, g_fgGround.pixels) : 0;
    g_fgGround.lastSeconds = (double)(GetTickCount64() - g_fgGround.started) / 1000.0;
    if (tex == 0) { Note("difficulty: far grass: the ground texture was not created; the overlay keeps the level's heights"); return; }
    g_fgGround.framed = NearRenderFrame(&g_fgGround.madeFrame);
    uint64_t old = g_fgGround.tex;
    g_fgGround.tex = tex; g_fgGround.builtGen = g_fgGround.buildingGen; g_fgGround.builds++;
    FgGroundPublish(tex);
    FgGroundDestroy(old);
    if (g_fgGround.builds <= 4 || g_fgGround.builds % 16 == 0)
        Note("difficulty: far grass: ground heights for the overlay: build %u, %d tiles, %.2f s; texture 0x%llX", g_fgGround.builds, g_fgGround.job.count,
            g_fgGround.lastSeconds, (unsigned long long)tex);
}

/* The pixels are free again once the render thread has copied them (NEAR_PIXELS_FRAMES kicks after the create). */
static int FgGroundPixelsFree(void)
{
    uint32_t now = 0;
    if (g_fgGround.tex == 0) return 1;
    return g_fgGround.framed && NearRenderFrame(&now) && now - g_fgGround.madeFrame >= NEAR_PIXELS_FRAMES;
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
}

/* After the level update, on the game thread (the render API's thread). */
static void FgGroundTick(uint64_t level)
{
    if (level == 0 || level != g_worldLevelLive || g_world.live != 8.0f) return;
    if (g_fgGround.level != g_world.levels)
    {
        if (g_fgGround.level >= 0) FgGroundLevelLeft();
        g_fgGround.level = g_world.levels; g_fgGround.builtGen = g_fgGround.buildingGen = g_tilesGen - 1;
    }
    if (g_fgGround.shown == 0 && g_oneMip.tex != 0 && g_oneMip.level == g_world.levels) FgGroundPublish(g_oneMip.tex);
    if (g_fgGround.edited == 0)
    {
        g_fgGround.edited = FgGroundEdited() ? 1 : -1;
        Note("difficulty: far grass: the overlay's height fade %s", g_fgGround.edited > 0 ? (FgGroundOn() ? "reads the drawn ground (complex_operations_far_grass_ground)" :
            "reads the level's map (HELLFORGE_XL_FARGRASS_GROUND=0)") : "is the game's: the level's map (no fade edit in the loaded variant)");
    }
    if (!FgGroundOn() || g_fgGround.edited < 0) return;
    if (g_fgGround.worker != NULL)
    {
        if (!g_fgGround.done) return;
        WaitForSingleObject(g_fgGround.worker, INFINITE); CloseHandle(g_fgGround.worker); g_fgGround.worker = NULL; g_fgGround.done = 0;
        FgGroundMake();
        return;
    }
    LONG gen = g_tilesGen;
    if (gen == g_fgGround.builtGen || g_nearCoarseGround.v == NULL || !FgGroundPixelsFree()) return;
    if (g_fgGround.pixels == NULL)
    {
        g_fgGround.pixels = (float*)VirtualAlloc(NULL, (size_t)FG_GROUND_N * FG_GROUND_N * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
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
