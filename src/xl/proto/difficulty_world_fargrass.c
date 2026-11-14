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

/* FG_VIEWS and FG_BUILD wait to queue a job, FG_VIEW_WAIT and FG_SETTLE for it to run; only while a job is out are the
 * names published and the mover held, so a moving tile waits one view at most. */
enum { FG_IDLE, FG_VIEWS, FG_VIEW_WAIT, FG_BUILD, FG_SETTLE };

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
    uint32_t frames, empty, waits, deferred;
    int setting, settingNotes, clearNoted, edited; uint32_t settingFrames;
    uint32_t batches, baked;
    ULONGLONG started;
} g_fg = {.level = -1};

/* Wall-clock frame times (QPC between level updates in play): the last FG_RING with no job out against the jobs' own,
 * so each batch line shows whether its views cost frame time. The game's dt is clamped and would hide a spike. */
#define FG_RING 128
static struct { LONGLONG last, hz; float ring[FG_RING], lastMs; uint32_t at, filled; double worst, sum; uint32_t n; double before[2]; LONG64 scat, overlap; } g_fgFrames;
static void FgFrame(int live)
{
    LARGE_INTEGER t;
    if (!live) { g_fgFrames.last = 0; return; }
    QueryPerformanceCounter(&t);
    co_framecost_grass(g_fg.published);
    if (g_fgFrames.hz == 0) { LARGE_INTEGER hz; QueryPerformanceFrequency(&hz); g_fgFrames.hz = hz.QuadPart; }
    if (g_fgFrames.last != 0 && g_fgFrames.hz > 0)
    {
        const float ms = (float)((double)(t.QuadPart - g_fgFrames.last) * 1000.0 / (double)g_fgFrames.hz);
        g_fgFrames.lastMs = ms;
        if (g_fg.published) { g_fgFrames.n++; g_fgFrames.sum += ms; if (ms > g_fgFrames.worst) g_fgFrames.worst = ms; }
        else { g_fgFrames.ring[g_fgFrames.at] = ms; g_fgFrames.at = (g_fgFrames.at + 1) % FG_RING; if (g_fgFrames.filled < FG_RING) g_fgFrames.filled++; }
    }
    g_fgFrames.last = t.QuadPart;
}
/* A batch starts: the frames before it, and the counts start again. */
static void FgFramesStart(void)
{
    double worst = 0, sum = 0;
    for (uint32_t k = 0; k < g_fgFrames.filled; k++) { sum += g_fgFrames.ring[k]; if (g_fgFrames.ring[k] > worst) worst = g_fgFrames.ring[k]; }
    g_fgFrames.before[0] = worst; g_fgFrames.before[1] = g_fgFrames.filled ? sum / g_fgFrames.filled : 0;
    g_fgFrames.worst = g_fgFrames.sum = 0; g_fgFrames.n = 0; g_fgFrames.scat = g_tdScatBakeUpdates; g_fgFrames.overlap = g_tdScatOverlap;
}

/* A job goes out only after a frame no slower than a quarter over the mean of the last FG_RING without one, never by
 * force: a fight's spike holds the views, while a lasting change of load moves the mean within FG_RING frames. */
static int FgPaced(void)
{
    double sum = 0;
    for (uint32_t k = 0; k < g_fgFrames.filled; k++) sum += g_fgFrames.ring[k];
    if (g_fgFrames.filled < 16 || g_fgFrames.lastMs <= 1.25 * sum / g_fgFrames.filled + 1.0) { g_fg.deferred = 0; return 1; }
    g_fg.deferred++;
    return 0;
}

/* The ground this PC draws is around its own camera ([game+0x346D560]+0x3C, the pose's translation): the overlay is
 * per PC, so a client bakes for its own view. The streamer's research observer stands in for it in tests. */
static int FgEye(float* xy)
{
    uintptr_t cm = 0;
    float at[3];
    if (g_streamObserverOn) { xy[0] = g_streamObserverAt[0]; xy[1] = g_streamObserverAt[1]; return 1; }
    if (SafeRead(g_world.game + 0x346D560u, &cm, 8) || cm == 0 || SafeRead(cm + 0x3C, at, sizeof at) || !isfinite(at[0]) || !isfinite(at[1])) return 0;
    xy[0] = at[0]; xy[1] = at[1];
    return 1;
}

static int FgBaked(const FgMap* m, int i, int j) { return (m->baked[(j * FG_N + i) >> 3] >> ((j * FG_N + i) & 7)) & 1; }
static void FgMark(FgMap* m, int i, int j) { m->baked[(j * FG_N + i) >> 3] |= (uint8_t)(1u << ((j * FG_N + i) & 7)); }
static void FgUnmark(FgMap* m, int i, int j) { m->baked[(j * FG_N + i) >> 3] &= (uint8_t)~(1u << ((j * FG_N + i) & 7)); }

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
}

/* The setter (exe 0x31A200 -> 0x10D880) only queues a message, so the setting is set again every FG_SETTING_FRAMES in
 * play: an options apply puts the profile's value back. The getter (0x319F30) waits on the render thread (0x105E40,
 * 0x106030), so the live value is not read. A normal map must not keep it (wrong square), so it is cleared at every
 * ship and level init too; FgProfileGuard keeps it out of the profile. Changes are logged (64 lines a session). */
static void FgSetting(int on, const char* why)
{
    typedef void (*SettingFn)(const char* name, const char* value);
    SettingFn set = (SettingFn)NearApi(0x10, 0xD8);
    if (set != NULL) set("debug_deformation_prevention", on ? "true" : "false");
    if (why != NULL && g_fg.settingNotes < 64)
    {
        g_fg.settingNotes++;
        Note("difficulty: far grass: debug_deformation_prevention %s (%s)%s", on ? "true" : "false", why, set == NULL ? "; no setter, not set" : "");
    }
    g_fg.setting = on && set != NULL; g_fg.settingFrames = 0;
}

/* Crater cuts in scatter_lookup (crater CS 7083c2aa68db2524) would go at every builder run: its CS 52d73e589aaefe80
 * rewrites each texel from scatter_density. hellforge-data.json edits it to return first for a texel whose view's bit is
 * set in zone_material_scatter_linker's spare bits 12-15 (entry (y >> 6) * 16 + (x >> 8), bit 12 + ((x >> 6) & 3)); the
 * shader masks entries & 15 and the game fills them with slot indices below 32 (game 0xF5F530). So before each build the
 * mod uploads the level's own linker (op 0x4A's record +8 +0x1168, 0x800 bytes) with every view not baked since the last
 * build kept, as op 0x4A uploads it (game 0xF61418: [api+0x10]+0x158 by name, +0x208 copies at the call, exe 0x4BD71A). */
#define FG_LINKER 0x499EAFD7u
#define FG_BUILDER_CS 0x52D73E589AAEFE80ull
static struct { uint16_t game[1024], sent[1024]; LONG level; int dirty, keepNotes, builder; FgMap pending; uint32_t kept; } g_fgLinker = {.level = -1};

/* Op 0x4A on an extra large level: its record's linker once its first step has filled it (every entry a slot). */
static void FarGrassLinker(uint64_t record)
{
    uint16_t e[1024];
    int any = 0;
    if (SafeRead(record + 8 + 0x1168, e, sizeof e)) return;
    for (int k = 0; k < 1024; k++) { if (e[k] >= 32) return; any |= e[k]; }
    if (!any) return;
    memcpy(g_fgLinker.game, e, sizeof e);
    g_fgLinker.level = g_world.levels;
}

static const char* FgLinkerSend(const uint16_t* entries)
{
    typedef void* (*FindFn)(uint32_t name);
    typedef void (*UpdateFn)(void* resource, const void* data, uint32_t size);
    FindFn find = (FindFn)NearApi(0x10, 0x158);
    UpdateFn update = (UpdateFn)NearApi(0x10, 0x208);
    void* res = find != NULL && update != NULL ? find(FG_LINKER) : NULL;
    if (res == NULL) return "zone_material_scatter_linker is not found";
    memcpy(g_fgLinker.sent, entries, sizeof g_fgLinker.sent);
    update(res, g_fgLinker.sent, sizeof g_fgLinker.sent);
    return NULL;
}

/* Whether the loaded builder is the edited one (its first new instruction, mov r1.xy, vThreadID, at stage 0x8F8 +0x304);
 * the game's own ignores the bits, so this only says whether the cuts will stay. */
static int FgBuilderEdited(void)
{
    typedef uint8_t (*CanGetFn)(uint64_t type, uint64_t name);
    typedef uint64_t (*GetFn)(uint64_t name, uint64_t type);
    static const uint8_t mov[16] = {0x36, 0, 0, 4, 0x32, 0, 0x10, 0, 1, 0, 0, 0, 0x46, 0, 2, 0};
    CanGetFn canGet = (CanGetFn)NearApi(0x10, 0x388);
    GetFn get = (GetFn)NearApi(0x10, 0x3F8);
    uint8_t at[16];
    const uint64_t lib = canGet != NULL && get != NULL && canGet(0xE5EE32A477239A93ull, FG_BUILDER_CS) ? get(FG_BUILDER_CS, 0xE5EE32A477239A93ull) : 0;
    return lib != 0 && !SafeRead(lib + 0x8F8 + 0x304, at, sizeof at) && memcmp(at, mov, sizeof mov) == 0;
}

/* Before the builder's job: every view but those baked since the last build keeps its texels. */
static void FgLinkerKeep(void)
{
    if (g_fgLinker.builder == 0)
    {
        g_fgLinker.builder = FgBuilderEdited() ? 1 : -1;
        Note("difficulty: far grass: the scatter lookup builder is %s", g_fgLinker.builder > 0 ?
            "the edited one (0x52D73E589AAEFE80): views not baked since the last build keep their texels and crater cuts" :
            "the game's own (no build-data): each build puts the grass back in earlier craters");
    }
    uint16_t e[1024];
    const char* why = g_fgLinker.level != g_world.levels ? "op 0x4A's linker was not read this level" : NULL;
    g_fgLinker.kept = 0;
    if (why == NULL)
    {
        for (int k = 0; k < 1024; k++) e[k] = g_fgLinker.game[k] & 0x0FFFu;
        for (int j = 0; j < FG_N; j++)
            for (int i = 0; i < FG_N; i++)
                if (!FgBaked(&g_fgLinker.pending, i, j)) { e[(FG_N - 1 - j) * 16 + (i >> 2)] |= (uint16_t)(1u << (12 + (i & 3))); g_fgLinker.kept++; }
        why = FgLinkerSend(e);
    }
    if (why == NULL) { g_fgLinker.dirty = 1; memset(&g_fgLinker.pending, 0, sizeof g_fgLinker.pending); return; }
    g_fgLinker.kept = 0;
    if (g_fgLinker.keepNotes++ < 4) Note("difficulty: far grass: crater cuts are not kept by this build: %s", why);
}

/* The profile file is written only by exe 0x77A30 (callers 0x7F9BE, 0x324587, the Lua saves), which serialises the
 * settings document [app+0x2A8]. The renderer's fill 0x104770 copies live render settings into that document's absent
 * render_settings keys (0x10515B), so an apply in play could store `true`. Before either, the key is made a false bool
 * (the document's node: type in bits 0-2, 1 a bool, its value byte +8; operator[] 0x62ED0 adds what is missing). */
#define FG_SAVE 0x77A30u
#define FG_FILL 0x104770u
#define FG_DOC_GET 0x62ED0u
typedef void* (*FgDocGetFn)(void* node, const char* key);
static FgDocGetFn g_fgDocGet;
static void (*g_fgSave)(uintptr_t app);
static void (*g_fgFill)(uintptr_t renderer, void* doc);
static LONG g_fgGuardNotes;
static void FgProfileGuard(void* doc, const char* before)
{
    if (doc == NULL || g_fgDocGet == NULL) return;
    uint8_t* node = (uint8_t*)g_fgDocGet(g_fgDocGet(doc, "render_settings"), "debug_deformation_prevention");
    uint32_t head; memcpy(&head, node, 4);
    const uint32_t type = head & 7u;
    if (type > 1u || (type == 1u && node[8] == 0)) return;
    head = (head & ~6u) | 1u; memcpy(node, &head, 4); node[8] = 0;
    if (InterlockedIncrement(&g_fgGuardNotes) <= 8)
        Note("difficulty: far grass: the settings document's debug_deformation_prevention was %s before %s; it is false now", type ? "true" : "missing", before);
}
static void FgSaveGuarded(uintptr_t app) { FgProfileGuard(*(void**)(app + 0x2A8), "a profile save"); g_fgSave(app); }
static void FgFillGuarded(uintptr_t renderer, void* doc) { FgProfileGuard(doc, "the renderer's settings fill"); g_fgFill(renderer, doc); }

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
    memset(&g_fgLinker.pending, 0, sizeof g_fgLinker.pending); g_fgLinker.builder = 0;
    /* the game's own entries back, only this level's: a level whose op 0x4A gave none has its own upload already */
    if (g_fgLinker.dirty && (g_fgLinker.level != g_world.levels || FgLinkerSend(g_fgLinker.game) == NULL)) g_fgLinker.dirty = 0;
    g_fg.level = -1; g_fg.off = 0; g_fg.batches = g_fg.baked = 0; g_fg.edited = 0; g_tdScatWaitUs = g_tdScatWaitMaxUs = 0; g_scatBakeOver = 0;
    g_fgFrames.filled = g_fgFrames.at = 0; g_fgFrames.last = 0;
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
    /* Only the session's first clear and real changes spend a log line, so the cap lasts a long session. */
    FgSetting(0, g_fg.setting ? "the level is left" : g_fg.clearNoted ? NULL : "a level init or the ship");
    g_fg.clearNoted = 1;
    if (g_fg.level >= 0) FgFree();
}

static int FarGrassBusy(void) { return g_fg.published; }

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

/* The overlay variants the setting picks must be the edited ones: the stock debug variant tints the ground blue, which
 * a DLL copied without HellForge's build-data would show. A shader_library resource's buffer is its GPU part in file
 * order (loader 0x12A0D0 fixes pointers in place) and stays while the library is online, so the two pixel stages sit
 * at the offsets hellforge-data.json edits. Found by can_get / get ([api+0x10]+0x388 (type, name), +0x3F8 (name, type)). */
#define FG_SHADER_LIBRARY 0xE5EE32A477239A93ull
static const struct { uint64_t name; uint32_t at[2], size, run; int scale; } kFgShaders[2] = {
    {0x2FA016B4649B8927ull, {0x1FB8, 0x7DE0}, 0x4F14, 0x4B18, 1}, {0xECF7F262F57585A0ull, {0x1CC8, 0x44F8}, 0x1C20, 0x1840, 0}};
static const char* FgShaderCheck(void)
{
    typedef uint8_t (*CanGetFn)(uint64_t type, uint64_t name);
    typedef uint64_t (*GetFn)(uint64_t name, uint64_t type);
    static const uint8_t nops[16] = {0x3A, 0, 0, 1, 0x3A, 0, 0, 1, 0x3A, 0, 0, 1, 0x3A, 0, 0, 1};
    static const uint8_t scale[8] = {0, 0, 0x80, 0x38, 0, 0, 0x80, 0xB8}; /* 1/16384, -1/16384 */
    CanGetFn canGet = (CanGetFn)NearApi(0x10, 0x388);
    GetFn get = (GetFn)NearApi(0x10, 0x3F8);
    if (canGet == NULL || get == NULL) return "the resource lookup is missing";
    for (int k = 0; k < 2; k++)
    {
        if (!canGet(FG_SHADER_LIBRARY, kFgShaders[k].name)) return "an overlay variant is not loaded";
        const uint64_t lib = get(kFgShaders[k].name, FG_SHADER_LIBRARY);
        if (lib == 0) return "an overlay variant is not loaded";
        for (int s = 0; s < 2; s++)
        {
            const uint64_t blob = lib + kFgShaders[k].at[s];
            uint8_t head[28], run[16], c[2][8];
            uint32_t size = 0;
            if (SafeRead(blob, head, sizeof head) || memcmp(head, "DXBC", 4) != 0) return "an overlay variant's stage is not where the edit put it";
            memcpy(&size, head + 24, 4);
            if (size != kFgShaders[k].size) return "an overlay variant's stage is not where the edit put it";
            if (SafeRead(blob + kFgShaders[k].run, run, sizeof run) || memcmp(run, nops, sizeof nops) != 0)
                return "the overlay variants are the game's own (no build-data): their debug tint is still in";
            if (kFgShaders[k].scale && (SafeRead(blob + 0x1818, c[0], 8) || SafeRead(blob + 0x1894, c[1], 8) || memcmp(c[0], scale, 8) != 0 ||
                memcmp(c[1], scale, 8) != 0))
                return "the overlay variants are the game's own (no build-data): they map 2048 m";
        }
    }
    return NULL;
}
/* Checked once a level, at its first update in play; a refusal is logged and that level has no far grass. */
static int FgShadersEdited(void)
{
    if (g_fg.edited == 0)
    {
        const char* why = FgShaderCheck();
        g_fg.edited = why == NULL ? 1 : -1;
        if (why == NULL) Note("difficulty: far grass: the edited overlay variants are loaded (0x2FA016B4649B8927, 0xECF7F262F57585A0)");
        else Note("difficulty: far grass: off, debug_deformation_prevention stays false: %s", why);
    }
    return g_fg.edited > 0;
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

/* One view: the camera over its square, the viewport's rectangle, op 0x4A's job (game 0xF616B0..0xF61832). Not op 0x4A's
 * next call, [api+8]+0x640 (exe 0x339690): its message 0xB rebuilds every scatter layer tree of the world (exe
 * 0x216FE8), which in play would throw the players' cells away each view. A cell's instances depend on the cell and the
 * layer only (exe 0x21343B..0x213479), and the view's records are freed for reuse at the next update (0x953A11). */
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
    FgMark(&g_fg.map, i, j); FgMark(&g_fgLinker.pending, i, j);
    g_fg.baked++;
    return 1;
}

static void FgEnd(int stop, const char* why)
{
    FgObjectsDestroy();
    if (g_fg.published) FgPublish(0);
    g_fg.state = FG_IDLE; g_fg.frames = 0;
    if (stop) { g_fg.off = 1; Note("difficulty: far grass: %s; no more bakes this level", why); }
}

/* After a batch: SpeedTree's memory (the speedtree_plugin proxy [exe+0x1A100D8]: live bytes +0x30, peak +0x40; every
 * scatter container allocates through it) and the node vectors of the system the bake updated (its vtable exe+0x1673E48
 * and type 0x11 at +8; layers [+0x1C0] 0x238 apart, [+0x1B8] of them; tree [layer+0x40], node capacity +0x98, 0xE0 a
 * node). Both stay flat from batch to batch when the bake grows nothing per baked area. */
static void FgScatterNote(void)
{
    const uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL), sys = g_tdScatBakeSys;
    uintptr_t proxy = 0, vt = 0, layers = 0;
    uint64_t live = 0, peak = 0, nodes = 0, most = 0, bound = 0;
    uint32_t type = 0, count = 0, read = 0;
    float finest = 0;
    if (!SafeRead(exe + 0x1A100D8u, &proxy, 8) && proxy != 0) { SafeRead(proxy + 0x30, &live, 8); SafeRead(proxy + 0x40, &peak, 8); }
    if (sys != 0 && !SafeRead(sys, &vt, 8) && vt == exe + 0x1673E48u && !SafeRead(sys + 8, &type, 4) && type == 0x11 && !SafeRead(sys + 0x1B8, &count, 4) &&
        count <= 64 && !SafeRead(sys + 0x1C0, &layers, 8) && layers != 0)
        for (uint32_t i = 0; i < count; i++)
        {
            uintptr_t tree = 0; uint64_t cap = 0;
            if (SafeRead(layers + (uint64_t)i * 0x238u + 0x40, &tree, 8) || tree == 0 || SafeRead(tree + 0x98, &cap, 8)) continue;
            nodes += cap; if (cap > most) most = cap; read++;
            /* the cells a 256 m view can touch: its square and the layer's pad (cell size +0x22C, pad +0x230) */
            float size = 0, grow = 0;
            if (SafeRead(layers + (uint64_t)i * 0x238u + 0x22C, &size, 4) || SafeRead(layers + (uint64_t)i * 0x238u + 0x230, &grow, 4) || !(size >= 1.0f)) continue;
            const uint64_t side = (uint64_t)ceilf((FG_TILE + 2.0f * grow) / size) + 1;
            if (side * side > bound) { bound = side * side; finest = size; }
        }
    Note("difficulty: far grass: SpeedTree memory %llu MiB (peak %llu MiB); node vectors %llu KiB over %u of %u layers, the largest %llu nodes; "
        "a bake view's layer touches %llu cells at most (%.1f m cells); %ld bake views over the limit this level",
        (unsigned long long)(live >> 20), (unsigned long long)(peak >> 20), (unsigned long long)(nodes * 0xE0u >> 10), read, count, (unsigned long long)most,
        (unsigned long long)bound, finest, g_scatBakeOver);
}

static void FgBatchNote(void)
{
    g_fg.batches++;
    if (g_fg.batches > 8 && g_fg.batches % 16 != 0) return;
    Note("difficulty: far grass: batch %u: %d views of 256 m in %.1f s (%lld scatter updates of its views; %lld updates waited for their layer, "
        "other views %.2f ms in all this level, %.2f ms at most); %u of %d views baked this level; frames (wall clock) with a job out %u, worst %.1f ms, "
        "mean %.1f ms; the %u before the batch worst %.1f ms, mean %.1f ms; its build kept %u views' texels", g_fg.batches,
        g_fg.count, (double)(GetTickCount64() - g_fg.started) / 1000.0, (long long)(g_tdScatBakeUpdates - g_fgFrames.scat),
        (long long)(g_tdScatOverlap - g_fgFrames.overlap), (double)g_tdScatWaitUs / 1000.0, (double)g_tdScatWaitMaxUs / 1000.0, g_fg.baked, FG_N * FG_N,
        g_fgFrames.n, g_fgFrames.worst,
        g_fgFrames.n ? g_fgFrames.sum / g_fgFrames.n : 0.0, g_fgFrames.filled, g_fgFrames.before[0], g_fgFrames.before[1], g_fgLinker.kept);
    FgScatterNote();
    WorldMemoryNote("far grass batch", 0xFFFFFFFFu, 0, 1);
}

/* After the level update, on the game thread, and only in play (level state 3). A job goes out only with the mover idle,
 * its merge's renders drained and the queue empty; MoveTick waits while one is out (FarGrassBusy). */
static void FarGrassTick(uint64_t level)
{
    if (!g_fg.installed) return;
    uint32_t state = 0;
    uint64_t block = 0;
    int live = level != 0 && level == g_worldLevelLive && g_world.live == 8.0f && g_fg.level == g_world.levels && FgKept() &&
        !SafeRead(level + 0x1E4, &state, 4) && state == 3 && !SafeRead(level + 0x2A8, &block, 8) && block != 0;
    FgFrame(live);
    if (!live)
    {
        if (g_fg.setting) FgSetting(0, "not in play on an extra large level");
        return;
    }
    if (!FgShadersEdited()) return;
    if (!g_fg.setting) FgSetting(1, "in play on an extra large level");
    else if (++g_fg.settingFrames >= FG_SETTING_FRAMES) FgSetting(1, NULL);
    const uint64_t context = block + 0x19B008u;
    if (g_fg.state == FG_IDLE)
    {
        float eye[2];
        if (g_fg.off || ++g_fg.frames < FG_PICK_FRAMES) return;
        if (g_move.state != MV_IDLE || TdScatterMergeWindow() || NearQueued(context) != 0) return;
        g_fg.frames = 0;
        if (!FgEye(eye) || FgBatch(&g_fg.map, (const float (*)[2])eye, 1, FG_WANT, g_fg.list, 1) == 0) return;
        g_fg.count = FgBatch(&g_fg.map, (const float (*)[2])eye, 1, FG_REACH, g_fg.list, FG_BATCH);
        if (SafeRead(context + 0x40, &g_fg.world, 8) || g_fg.world == 0 || SafeRead(context + 0x50, &g_fg.queue, 8) || g_fg.queue == 0 ||
            SafeRead(context + 8, &g_fg.settings, 8) || g_fg.settings == 0) return;
        if (!FgObjects()) { FgEnd(1, "the bake's viewport, camera or environment could not be made"); return; }
        g_fg.next = 0; g_fg.waits = 0; g_fg.deferred = 0; g_fg.started = GetTickCount64();
        FgFramesStart();
        g_fg.state = FG_VIEWS;
        return;
    }
    if (g_fg.state == FG_VIEWS || g_fg.state == FG_BUILD)
    {
        /* one job on the queue at a time: the rectangle belongs to the viewport, not to the job */
        if (g_move.state != MV_IDLE || TdScatterMergeWindow() || NearQueued(context) != 0 || !FgPaced()) return;
        FgPublish(1);
        if (g_fg.state == FG_VIEWS)
        {
            if (!FgView(g_fg.list[g_fg.next++])) { FgEnd(1, "a view's camera or viewport call is missing"); return; }
            g_fg.state = FG_VIEW_WAIT;
        }
        else
        {
            FgLinkerKeep();
            g_fg.generate(g_fg.queue, g_fg.world, FG_BUILDER, 0, 0, NULL, 0, NULL, NULL, 0);
            g_fg.state = FG_SETTLE;
        }
        g_fg.empty = 0; g_fg.waits = 0;
        return;
    }
    /* FG_VIEW_WAIT, FG_SETTLE: a job taken off the queue runs within the next frame (TD_SCAT_MERGE_EMPTY_UPDATES) */
    if (NearQueued(context) != 0)
    {
        g_fg.empty = 0;
        if (++g_fg.waits > FG_WAITS) FgEnd(1, g_fg.state == FG_SETTLE ? "the builder's job did not leave the queue" : "the generator's queue did not empty");
        return;
    }
    if (++g_fg.empty < FG_EMPTY_UPDATES) return;
    FgPublish(0);
    /* a view over the node limit had z-0 boxes: its texels stay as they are, the views before it are built */
    if (g_fg.state == FG_VIEW_WAIT && g_scatBakeOver != 0 && !g_fg.off)
    {
        const int last = g_fg.list[g_fg.next - 1];
        FgUnmark(&g_fgLinker.pending, last % FG_N, last / FG_N);
        g_fg.off = 1; g_fg.next = g_fg.count;
    }
    if (g_fg.state == FG_VIEW_WAIT) { g_fg.state = g_fg.next >= g_fg.count ? FG_BUILD : FG_VIEWS; return; }
    FgEnd(0, NULL);
    FgBatchNote();
}

static void FarGrassInstall(uintptr_t game)
{
    if (g_fg.installed || !FarGrassOn()) return;
    if (ApiCodeMatches(game + FG_AIM, kFgAimPin) != 1 || ApiCodeMatches(game + FG_GENERATE, kFgGeneratePin) != 1)
    { Note("difficulty: far grass: the bake camera or the generator queue is not the reviewed code; no far grass on extra large maps"); return; }
    /* No guard on the profile, no setting: far grass stays off. */
    const uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL);
    if (ApiCodeMatches(exe + FG_SAVE, "48 8B C4 55 53 57 48 8D A8 C8 F6 FF FF") != 1 ||
        ApiCodeMatches(exe + FG_FILL, "48 89 5C 24 10 48 89 4C 24 08 55 56 57 41 54") != 1 ||
        ApiCodeMatches(exe + FG_DOC_GET, "48 89 5C 24 18 48 89 6C 24 20 57 48 83 EC 30") != 1 ||
        LoaderHook("far grass: the profile save keeps debug_deformation_prevention false", exe + FG_SAVE, (void*)FgSaveGuarded, (void**)&g_fgSave) != 0 ||
        LoaderHook("far grass: the renderer's settings fill keeps it false", exe + FG_FILL, (void*)FgFillGuarded, (void**)&g_fgFill) != 0)
    { Note("difficulty: far grass: the profile save or the renderer's settings fill is not the reviewed code; no far grass on extra large maps"); return; }
    g_fgDocGet = (FgDocGetFn)(exe + FG_DOC_GET);
    g_fg.aim = (FgAimFn)(game + FG_AIM);
    g_fg.generate = (FgGenerateFn)(game + FG_GENERATE);
    g_fg.installed = 1;
}
