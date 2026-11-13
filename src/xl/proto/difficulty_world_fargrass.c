/* Extra large maps: the far-scatter ground overlay (far grass). Included after difficulty_world_move.c.
 *
 * The overlay (render config generator imp_bake) draws grass colour past the near scatter from the global scatter_lookup
 * (4096 x 4096), which op 0x4A's builder fills from scatter_density, an orthographic bake of the scatter from above. On a
 * 16 km level the bake's load-time views are cut (difficulty_world_diag.c: they would grow every scatter layer to the
 * whole map) and the overlay's shader maps a 2048 m square. So, on an extra large level in play:
 *  - the overlay runs the variant that the render setting debug_deformation_prevention picks, which hellforge-data.json
 *    edits to map 16384 m without the debug tint (2fa016b4649b8927; ecf7f262f57585a0, the far_scatter_enabled false
 *    variant, without its tint). Nothing else reads the setting;
 *  - the ground is baked as players come near, in views of a vanilla tile's 256 m into 64 x 64 texels of the kept
 *    scatter_density (4 m a texel, the spacing of the lookup, materials and zones), then the builder runs again. */

#define FG_WORLD 16384.0f     /* the extra large world, metres (g_world.live 8) */
#define FG_TILE 256.0f        /* a bake view: a vanilla phase-B tile's square */
#define FG_N 64               /* views a side */
#define FG_WANT 768.0f        /* a batch starts when unbaked ground is this close to a player ... */
#define FG_REACH 1536.0f      /* ... and bakes the views this close to one, nearest first */
#define FG_BATCH 64           /* views a batch at most */
#define FG_PICK_FRAMES 30u    /* level updates between looks for unbaked ground */
#define FG_SETTING_FRAMES 60u /* the setting is set again this often: an options apply resets the render settings */
#define FG_EMPTY_UPDATES 3u   /* the queue read empty this many level updates in a row: the builder's job has run */
#define FG_VIEW_EVERY 2u      /* a view every this many level updates: each view's scatter is generated afresh */
#define FG_WAITS 1200u        /* level updates a step waits for the queue before the level's bakes stop */
#define FG_AIM 0xF48990u      /* game: the phase-B camera's set-up (op 0x49) */
#define FG_GENERATE 0xF3B2A0u /* game: a generator's job on the queue (op 0x4A's last call) */
static const char kFgAimPin[] = "40 53 56 57 48 81 EC 20 01 00 00 0F 29 BC 24 10 01 00 00";
static const char kFgGeneratePin[] = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20";
#define FG_BUILDER 0xC4933EE1u  /* finalize_scatter_map_lookup_bake */
#define FG_VIEWPORT 0x5A038527u /* the phase-B viewport (op 0x49) */
#define FG_CAMERA_UNIT 0x465F4895F3DC98D1ull
#define FG_ENVIRONMENT 0x9F09B0185B47408Full
#define FG_FLAGS 0x10400u       /* op 0x4A's job: 0x400 gives the view the scatter's bake reach */

/* The kept textures: their names, where op 0x49 (+0x38..0x48 of its data) or 0x4C (merge R+0xB8) keeps them, format,
 * flags and id as made. */
enum { FG_DENSITY, FG_DENSITY_TMP, FG_LOOKUP_TMP, FG_ZONES, FG_TEXTURES };
static const struct { uint32_t name, at, format, flags, id; } kFgTex[FG_TEXTURES] = {
    {0x3AEAC078u, 0x38, 0x1A, 1, 0x3AEAC078u}, {0x6DD81D45u, 0x40, 0x1A, 1, 0x6DD81D45u},
    {0xDF3266D2u, 0x48, 0x16, 0, 0xDF3266D2u}, {0x2FFE957Du, 0xB8, 0x2A, 4, 0x1524FE5Au}};

enum { FG_IDLE, FG_VIEWS, FG_BUILD, FG_SETTLE };

typedef struct { uint8_t baked[FG_N * FG_N / 8]; } FgMap;
typedef void (*FgAimFn)(uint64_t world, uint32_t unit, uint64_t camera, float size, float k);
typedef void (*FgGenerateFn)(uint64_t queue, uint64_t world, uint32_t hash, uint64_t unused, uint32_t n1, const void* p1, uint32_t n2,
    const void* p2, const void* p3, uint32_t last);

static struct
{
    int installed;
    FgAimFn aim; FgGenerateFn generate;
    LONG level; /* the level (g_world.levels) the kept textures and the map belong to; -1 none */
    uint64_t tex[FG_TEXTURES];
    FgMap map;
    int state, count, next, published, off;
    uint16_t list[FG_BATCH];
    uint64_t world, queue, settings, vp, camera, env; uint32_t unit;
    uint32_t frames, empty, waits;
    int setting; uint32_t settingFrames;
    uint32_t batches, baked;
    ULONGLONG started;
} g_fg = {.level = -1};

static int FgBaked(const FgMap* m, int i, int j) { return (m->baked[(j * FG_N + i) >> 3] >> ((j * FG_N + i) & 7)) & 1; }
static void FgMark(FgMap* m, int i, int j) { m->baked[(j * FG_N + i) >> 3] |= (uint8_t)(1u << ((j * FG_N + i) & 7)); }

/* View (i, j): i east from the world's west edge, j north from its south edge. */
static void FgCentre(int i, int j, float* c)
{
    c[0] = -FG_WORLD / 2 + ((float)i + 0.5f) * FG_TILE; c[1] = -FG_WORLD / 2 + ((float)j + 0.5f) * FG_TILE;
}

/* The view's rectangle in the target, fractions from its top-left. The overlay reads v = 0.5 - y / 16384, so the top
 * row is the north edge, as op 0x4A's own rectangle (row 7 - j of 8). */
static void FgRect(int i, int j, float* r)
{
    r[0] = (float)i / FG_N; r[1] = (float)(FG_N - 1 - j) / FG_N; r[2] = 1.0f / FG_N; r[3] = 1.0f / FG_N;
}

/* Metres from (x, y) to view (i, j)'s square, 0 inside. */
static float FgDistance(int i, int j, float x, float y)
{
    float c[2];
    FgCentre(i, j, c);
    float dx = fmaxf(0.0f, fabsf(x - c[0]) - FG_TILE / 2), dy = fmaxf(0.0f, fabsf(y - c[1]) - FG_TILE / 2);
    return sqrtf(dx * dx + dy * dy);
}

/* Up to `max` unbaked views within `reach` of a player, nearest first (index j * FG_N + i); returns their count. */
static int FgBatch(const FgMap* m, const float (*players)[2], int count, float reach, uint16_t* list, int max)
{
    float dist[FG_BATCH];
    int n = 0;
    if (max > FG_BATCH) max = FG_BATCH;
    for (int j = 0; j < FG_N; j++)
        for (int i = 0; i < FG_N; i++)
        {
            if (FgBaked(m, i, j)) continue;
            float d = 1e30f;
            for (int p = 0; p < count; p++)
                if (isfinite(players[p][0]) && isfinite(players[p][1])) d = fminf(d, FgDistance(i, j, players[p][0], players[p][1]));
            if (!(d <= reach) || (n == max && !(d < dist[n - 1]))) continue;
            int at = n < max ? n++ : max - 1;
            while (at > 0 && d < dist[at - 1]) { dist[at] = dist[at - 1]; list[at] = list[at - 1]; at--; }
            dist[at] = d; list[at] = (uint16_t)(j * FG_N + i);
        }
    return n;
}

static int FgKept(void)
{
    for (int k = 0; k < FG_TEXTURES; k++) if (g_fg.tex[k] == 0) return 0;
    return 1;
}

static int FarGrassOn(void)
{
    static int on = -1;
    if (on < 0) { char t[4] = ""; on = !(GetEnvironmentVariableA("HELLFORGE_XL_FARGRASS", t, sizeof t) > 0 && t[0] == '0'); }
    return on;
}

static void FgPublish(int on)
{
    NearPublishFn publish = (NearPublishFn)NearApi(0x10, 0x210);
    NearNameFn unpublish = (NearNameFn)NearApi(0x10, 0x218);
    if (on && publish != NULL) for (int k = 0; k < FG_TEXTURES; k++) publish(kFgTex[k].name, g_fg.tex[k]);
    if (!on && unpublish != NULL) for (int k = 0; k < FG_TEXTURES; k++) unpublish(kFgTex[k].name);
    g_fg.published = on;
    InterlockedExchange(&g_namesFarGrass, on); /* the light probe watch names the publisher (NearProbeSeen) */
}

static void FgSetting(int on)
{
    typedef void (*SettingFn)(const char* name, const char* value);
    SettingFn set = (SettingFn)NearApi(0x10, 0xD8);
    if (set != NULL) set("debug_deformation_prevention", on ? "true" : "false");
    g_fg.setting = on; g_fg.settingFrames = 0;
}

/* An earlier level's textures, destroyed as op 0x4B destroys them; a batch's own objects went with its world. */
static void FgFree(void)
{
    if (g_fg.published) FgPublish(0);
    NearReleaseFn destroy = (NearReleaseFn)NearApi(0x10, 0x190);
    for (int k = 0; k < FG_TEXTURES; k++)
    {
        if (g_fg.tex[k] != 0 && destroy != NULL) destroy(g_fg.tex[k]);
        g_fg.tex[k] = 0;
    }
    if (g_fg.state != FG_IDLE) Note("difficulty: far grass: the level ended inside a batch; its viewport, camera and environment are left to the world");
    g_fg.state = FG_IDLE; g_fg.vp = g_fg.camera = g_fg.env = 0; g_fg.unit = 0;
    memset(&g_fg.map, 0, sizeof g_fg.map);
    g_fg.level = -1; g_fg.off = 0; g_fg.batches = g_fg.baked = 0;
}

/* Ops 0x4B (destroys op 0x49's targets) and 0x67 (the merge's, generated_zones among them) on an extra large level: the
 * builder's inputs stay for the bakes in play. Each op gets a 1 x 1 stand-in of the same format to unpublish and
 * destroy: neither op tests for null, and the destroy queues the pointer for the render thread (exe 0x31B360). */
static void FarGrassKeep(uint64_t record, uint32_t op)
{
    if (!g_fg.installed || g_world.current != 8.0f || (op != 0x4B && op != 0x67)) return;
    if (g_fg.level != g_world.levels) { FgFree(); g_fg.level = g_world.levels; }
    uint64_t d = 0, base = 0;
    if (SafeRead(record + 8, &d, 8) || d == 0) return;
    int first = FG_DENSITY, last = FG_LOOKUP_TMP;
    if (op == 0x4B)
    {
        int32_t made = 0;
        if (SafeRead(d + 0x90, &made, 4) || made < 1) return; /* 0x4B's own test: nothing was made */
        base = d;
    }
    else
    {
        if (SafeRead(d, &base, 8) || base == 0) return; /* R, the merge object */
        first = last = FG_ZONES;
    }
    NearCreateFn create = (NearCreateFn)NearApi(0x10, 0x170);
    if (create == NULL) return;
    for (int k = first; k <= last; k++)
    {
        uint64_t tex = 0;
        if (g_fg.tex[k] != 0 || SafeRead(base + kFgTex[k].at, &tex, 8) || tex == 0) continue;
        uint64_t standIn = create(1, 1, 1, kFgTex[k].format, kFgTex[k].id, kFgTex[k].flags);
        if (standIn == 0) continue;
        *(volatile uint64_t*)(uintptr_t)(base + kFgTex[k].at) = standIn;
        g_fg.tex[k] = tex;
    }
    if (op == 0x67 && FgKept()) Note("difficulty: far grass: scatter_density, its temporary, scatter_lookup_tmp and generated_zones are kept for the bakes in play");
}

/* A level init or the ship's set-up: the last level's textures go, and the overlay's variant is the game's again. */
static void FarGrassLevelLeft(void)
{
    if (g_fg.setting) FgSetting(0);
    if (g_fg.level >= 0) FgFree();
}

static int FarGrassBusy(void) { return g_fg.state != FG_IDLE; }

/* Op 0x4A's job for one view: an entry of 0x1D0 bytes on the generator's queue (game 0xF6155E..0xF615E5). */
static void FgQueueView(void)
{
    const uint64_t queue = g_fg.queue;
    LONG64 mine = InterlockedExchangeAdd64((volatile LONG64*)(uintptr_t)queue, 1);
    while (*(volatile LONG64*)(uintptr_t)(queue + 8) != mine) YieldProcessor();
    uint32_t n = *(volatile uint32_t*)(uintptr_t)(queue + 0x10);
    uint8_t* e = (uint8_t*)(uintptr_t)(queue + 0x18 + (uint64_t)n * 0x1D0);
    *(volatile uint32_t*)(uintptr_t)(queue + 0x10) = n + 1;
    const uint32_t flags = FG_FLAGS, id = 0xFFFFFFFFu;
    memcpy(e + 0x00, &g_fg.world, 8); memcpy(e + 0x08, &g_fg.camera, 8); memcpy(e + 0x10, &g_fg.vp, 8); memcpy(e + 0x18, &g_fg.env, 8);
    memcpy(e + 0x20, &flags, 4); e[0x24] = 0; memset(e + 0x38, 0, 2);
    SafeRead(g_fg.settings + 0x74, e + 0x58, 16);
    memcpy(e + 0x68, &id, 4);
    InterlockedExchangeAdd64((volatile LONG64*)(uintptr_t)(queue + 8), 1);
}

static void FgObjectsDestroy(void)
{
    typedef void (*EnvDestroyFn)(uint64_t world, uint64_t env);
    typedef void (*UnitDestroyFn)(uint64_t world, uint32_t unit);
    typedef void (*VpDestroyFn)(uint64_t world, uint64_t vp);
    EnvDestroyFn env = (EnvDestroyFn)NearApi(0x08, 0x478);
    UnitDestroyFn unit = (UnitDestroyFn)NearApi(0x08, 0x60);
    VpDestroyFn vp = (VpDestroyFn)NearApi(0x10, 0x88);
    if (g_fg.env != 0 && env != NULL) env(g_fg.world, g_fg.env);
    if (g_fg.camera != 0 && unit != NULL) unit(g_fg.world, g_fg.unit);
    if (g_fg.vp != 0 && vp != NULL) vp(g_fg.world, g_fg.vp);
    g_fg.vp = g_fg.camera = g_fg.env = 0; g_fg.unit = 0;
}

/* The batch's viewport, camera and environment, made as op 0x49 makes phase B's on the level's world. The far plane at
 * 1002 marks the bake's view (TD_SCAT_BAKE_FAR); the load-time scatter cuts look for 1001. */
static int FgObjects(void)
{
    typedef uint64_t (*VpFn)(uint64_t world, uint32_t name);
    typedef uint32_t (*SpawnFn)(uint64_t world, uint64_t hash, const float* pose);
    typedef uint64_t (*CameraFn)(uint32_t unit, uint32_t index);
    typedef uint64_t (*EnvFn)(uint64_t world, uint64_t hash);
    typedef void (*FarFn)(uint64_t camera, float farZ);
    typedef void (*OutputFn)(uint64_t vp, uint64_t tex);
    VpFn vp = (VpFn)NearApi(0x10, 0x80);
    SpawnFn spawn = (SpawnFn)NearApi(0x08, 0x08);
    CameraFn camera = (CameraFn)NearApi(0x18, 0x6B0);
    EnvFn env = (EnvFn)NearApi(0x08, 0x468);
    FarFn setFar = (FarFn)NearApi(0x20, 0x70);
    OutputFn output = (OutputFn)NearApi(0xE0, 0x10);
    if (vp == NULL || spawn == NULL || camera == NULL || env == NULL || setFar == NULL || output == NULL) return 0;
    static const float pose[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 501.0f, 1};
    g_fg.vp = vp(g_fg.world, FG_VIEWPORT);
    g_fg.unit = spawn(g_fg.world, FG_CAMERA_UNIT, pose);
    g_fg.camera = camera(g_fg.unit, 0);
    g_fg.env = env(g_fg.world, FG_ENVIRONMENT);
    if (g_fg.vp == 0 || g_fg.camera == 0 || g_fg.env == 0) { FgObjectsDestroy(); return 0; }
    g_fg.aim(g_fg.world, g_fg.unit, g_fg.camera, FG_TILE * 8.0f, 8.0f); /* its box is +-size / 2k: 256 m a side */
    setFar(g_fg.camera, TD_SCAT_BAKE_FAR);
    output(g_fg.vp, g_fg.tex[FG_DENSITY]);
    return 1;
}

/* One view: the camera over its square, the viewport's rectangle, op 0x4A's job (game 0xF616B0..0xF61832). */
static int FgView(int index)
{
    typedef void (*MoveFn)(uint64_t camera, uint32_t unit, const float* xyz);
    typedef void (*UnitFn)(uint64_t world, uint32_t unit);
    typedef void (*RectFn)(uint64_t vp, float x, float y, float w, float h);
    MoveFn move = (MoveFn)NearApi(0x20, 0x20);
    UnitFn update = (UnitFn)NearApi(0x08, 0x100);
    RectFn rect = (RectFn)NearApi(0xE0, 0x0);
    if (move == NULL || update == NULL || rect == NULL) return 0;
    const int i = index % FG_N, j = index / FG_N;
    float c[2], r[4];
    FgCentre(i, j, c); FgRect(i, j, r);
    const float at[3] = {c[0], c[1], 501.0f};
    move(g_fg.camera, g_fg.unit, at);
    update(g_fg.world, g_fg.unit);
    rect(g_fg.vp, r[0], r[1], r[2], r[3]);
    FgQueueView();
    FgMark(&g_fg.map, i, j);
    g_fg.baked++;
    return 1;
}

static void FgEnd(int stop, const char* why)
{
    FgObjectsDestroy();
    FgPublish(0);
    g_fg.state = FG_IDLE; g_fg.frames = 0;
    if (stop) { g_fg.off = 1; Note("difficulty: far grass: %s; no more bakes this level", why); }
}

/* After the level update, on the game thread. A moving tile's build and a batch never overlap: MoveTick waits while
 * FarGrassBusy, and a batch starts only with the mover idle and its merge's renders drained. */
static void FarGrassTick(uint64_t level)
{
    if (!g_fg.installed) return;
    uint32_t state = 0;
    uint64_t block = 0;
    int live = level != 0 && level == g_worldLevelLive && g_world.live == 8.0f && g_fg.level == g_world.levels && FgKept() &&
        !SafeRead(level + 0x1E4, &state, 4) && state == 3 && !SafeRead(level + 0x2A8, &block, 8) && block != 0;
    if (!live)
    {
        if (g_fg.setting) FgSetting(0);
        return;
    }
    if (!g_fg.setting || ++g_fg.settingFrames >= FG_SETTING_FRAMES) FgSetting(1);
    const uint64_t context = block + 0x19B008u;
    if (g_fg.state == FG_IDLE)
    {
        if (g_fg.off || ++g_fg.frames < FG_PICK_FRAMES) return;
        if (g_move.state != MV_IDLE || TdScatterMergeWindow() || NearQueued(context) != 0) return;
        g_fg.frames = 0;
        MovePlayers();
        if (FgBatch(&g_fg.map, (const float (*)[2])g_move.player, g_move.players, FG_WANT, g_fg.list, 1) == 0) return;
        g_fg.count = FgBatch(&g_fg.map, (const float (*)[2])g_move.player, g_move.players, FG_REACH, g_fg.list, FG_BATCH);
        if (SafeRead(context + 0x40, &g_fg.world, 8) || g_fg.world == 0 || SafeRead(context + 0x50, &g_fg.queue, 8) || g_fg.queue == 0 ||
            SafeRead(context + 8, &g_fg.settings, 8) || g_fg.settings == 0) return;
        FgPublish(1);
        if (!FgObjects()) { FgEnd(1, "the bake's viewport, camera or environment could not be made"); return; }
        g_fg.next = 0; g_fg.waits = 0; g_fg.started = GetTickCount64();
        g_fg.state = FG_VIEWS;
    }
    if (g_fg.state == FG_VIEWS)
    {
        /* one job on the queue at a time: the rectangle belongs to the viewport, not to the job */
        if (++g_fg.frames < FG_VIEW_EVERY || NearQueued(context) != 0)
        {
            if (++g_fg.waits > FG_WAITS) FgEnd(1, "the generator's queue did not empty");
            return;
        }
        g_fg.frames = 0; g_fg.waits = 0;
        if (!FgView(g_fg.list[g_fg.next++])) { FgEnd(1, "a view's camera or viewport call is missing"); return; }
        if (g_fg.next >= g_fg.count) g_fg.state = FG_BUILD;
        return;
    }
    if (g_fg.state == FG_BUILD)
    {
        if (NearQueued(context) != 0) { if (++g_fg.waits > FG_WAITS) FgEnd(1, "the generator's queue did not empty"); return; }
        g_fg.generate(g_fg.queue, g_fg.world, FG_BUILDER, 0, 0, NULL, 0, NULL, NULL, 0);
        g_fg.empty = 0; g_fg.waits = 0;
        g_fg.state = FG_SETTLE;
        return;
    }
    /* FG_SETTLE: a job taken off the queue runs within the next frame (TD_SCAT_MERGE_EMPTY_UPDATES) */
    if (NearQueued(context) != 0) { g_fg.empty = 0; if (++g_fg.waits > FG_WAITS) FgEnd(1, "the builder's job did not leave the queue"); return; }
    if (++g_fg.empty < FG_EMPTY_UPDATES) return;
    FgEnd(0, NULL);
    g_fg.batches++;
    if (g_fg.batches <= 4 || g_fg.batches % 16 == 0)
        Note("difficulty: far grass: batch %u: %d views of 256 m in %.1f s; %u of %d views baked this level", g_fg.batches, g_fg.count,
            (double)(GetTickCount64() - g_fg.started) / 1000.0, g_fg.baked, FG_N * FG_N);
}

static void FarGrassInstall(uintptr_t game)
{
    if (g_fg.installed || !FarGrassOn()) return;
    if (ApiCodeMatches(game + FG_AIM, kFgAimPin) != 1 || ApiCodeMatches(game + FG_GENERATE, kFgGeneratePin) != 1)
    { Note("difficulty: far grass: the bake camera or the generator queue is not the reviewed code; no far grass on extra large maps"); return; }
    g_fg.aim = (FgAimFn)(game + FG_AIM);
    g_fg.generate = (FgGenerateFn)(game + FG_GENERATE);
    g_fg.installed = 1;
}
