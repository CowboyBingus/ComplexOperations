/* Extra large levels: the navmesh's terrain surface at the game's own vertical accuracy (issue G: units tilted on rocks).
 * The Recast stage of a nav cell (exe 0x89A6C0) takes one value, max(trunc(10 x spacing) / 10, 0.5), as the cell size
 * and the cell height (0x89A7B3 / 0x89A7B9; heightfield +0x20 / +0x24, 0x89AA86 / 0x89AA8C): 0.5 m in the game, 4 m
 * here. A column's span ends at ceil(height / cell height) (0x89B840..0x89B88D), so the surface Havok is handed floats
 * up to 4 m over the ground, over the rocks standing on it; their tops then lie under a face and are dropped (the
 * segment from each centroid up the character height, 0xB32B0C..0xB32BF3 and 0xB6DA29), and paths run straight over
 * rocks the units have to climb. Three entries, each called from 0x89A6C0 alone:
 * - 0x89B630, the heights into spans: the heightfield's cell height is set before it runs; the cell size stays.
 * - 0x829590, the compact heightfield: the walkable height and climb are counts of cell heights (ceil(2 / v) and
 *   floor(5 / v) of the one value, 0x89A7AE..0x89A7FA) and are made from the cell height: 4 and 10 at 0.5 m.
 * - 0x8B53B0, the detail mesh: its sample distance (6 v) and error (v) are set, and its vertices, which it raises by
 *   the cell height (0x8B5CE2), are raised to the cell size: 0x89A6C0 lowers each by the one value (0x89B08F..0x89B0FC).
 * A level whose cells are 0.5 m is left as it is. On the nav job's worker threads: nothing here is shared but counts. */
#define LW_NH_CH 0.5f        /* the cell height: the game's own on its maps */
#define LW_NH_ERROR 0.5f     /* the detail mesh's largest error, metres: the game's own on its maps */
#define LW_NH_DIST 16.0f     /* the detail mesh's sample distance, metres: four cells; at one a cell takes far longer to build */
#define LW_NH_COARSE 0.5f    /* a cell size over this is an extra large level's */
#define LW_NH_SPANS 8191.0f  /* cell heights a span can count: 13 bits (0x89B862, 0x89B882) */

typedef uintptr_t (*LwNhRasterFn)(uintptr_t terrain, uintptr_t b, const float* bounds, float* field);
typedef uint8_t (*LwNhCompactFn)(uintptr_t ctx, int height, int climb, float* field, uintptr_t compact);
typedef uint8_t (*LwNhDetailFn)(uintptr_t ctx, uintptr_t mesh, const float* compact, float dist, float error, uintptr_t detail);
static struct
{
    float ch, error, dist; /* 0 cell height: off */
    LwNhRasterFn raster; LwNhCompactFn compact; LwNhDetailFn detail;
    DWORD tls; int timed;
    volatile LONG builds, changed, tall, clamped, said, us, usMax, verts, vertsMax, tris, trisMax, width, rangeMax;
} g_lwNh;
static int g_lwNhDone;

/* The cell height a heightfield gets: `want` when its cells are an extra large level's, the game gave it its cell
 * size, and its heights fit a span's count; else the game's. */
static float LwNhCh(float cs, float ch, float zlo, float zhi, float want)
{
    return want > 0.0f && want < cs && cs > LW_NH_COARSE && ch == cs && (zhi - zlo) / want <= LW_NH_SPANS ? want : ch;
}

/* Whether the hooks changed a heightfield of this cell size and cell height. */
static int LwNhOurs(float cs, float ch) { return cs > LW_NH_COARSE && ch > 0.0f && ch < cs; }

/* The game's walkable height (2 m, up) and climb (5 m, down) as counts of cell heights (0x16A9AE0, 0x16A9C90). */
static int LwNhHeight(float ch) { return (int)ceilf(2.0f / ch); }
static int LwNhClimb(float ch) { return (int)floorf(5.0f / ch); }

/* The columns whose span ends at the last count a span holds (spans: one a column, its end in bits 13..25; 0x8BCDF8). */
static int LwNhClamped(const uintptr_t* spans, int columns)
{
    int n = 0;
    for (int i = 0; i < columns; i++) n += spans[i] != 0 && ((*(const uint32_t*)spans[i] >> 13) & 0x1FFFu) == 0x1FFFu;
    return n;
}

/* The detail vertices (x, height, y) raised by `by`. */
static void LwNhLift(float* verts, int n, float by) { for (int i = 0; i < n; i++) verts[3 * i + 1] += by; }

static LONG LwNhMax(volatile LONG* at, LONG v) { LONG was; do was = *at; while (v > was && InterlockedCompareExchange(at, v, was) != was); return v; }

static uintptr_t LwNhRaster(uintptr_t terrain, uintptr_t b, const float* bounds, float* field)
{
    LARGE_INTEGER t;
    if (g_lwNh.timed && QueryPerformanceCounter(&t)) TlsSetValue(g_lwNh.tls, (void*)(uintptr_t)t.QuadPart);
    InterlockedIncrement(&g_lwNh.builds);
    const float cs = field[8], was = field[9], range = field[6] - field[3], ch = LwNhCh(cs, was, field[3], field[6], g_lwNh.ch);
    if (ch == was)
    {
        if (LwNhCh(cs, was, 0.0f, 0.0f, g_lwNh.ch) != was) InterlockedIncrement(&g_lwNh.tall); /* its heights alone refused it */
        return g_lwNh.raster(terrain, b, bounds, field);
    }
    field[9] = ch;
    InterlockedIncrement(&g_lwNh.changed); InterlockedExchange(&g_lwNh.width, *(const int32_t*)field); LwNhMax(&g_lwNh.rangeMax, (LONG)range);
    if (!InterlockedExchange(&g_lwNh.said, 1))
        Note("living world: nav heights: the first nav cell's terrain box spans z %.1f to %.1f m: %.0f of a span's %.0f cell heights of %.2f m",
            (double)field[3], (double)field[6], (double)(range / ch), (double)LW_NH_SPANS, (double)ch);
    uintptr_t r = g_lwNh.raster(terrain, b, bounds, field);
    const uintptr_t* spans = *(const uintptr_t**)((const uint8_t*)field + 0x28); const int32_t* wh = (const int32_t*)field;
    if (spans != NULL && wh[0] > 0 && wh[1] > 0 && wh[0] <= 4096 && wh[1] <= 4096) InterlockedExchangeAdd(&g_lwNh.clamped, LwNhClamped(spans, wh[0] * wh[1]));
    return r;
}

static uint8_t LwNhCompact(uintptr_t ctx, int height, int climb, float* field, uintptr_t compact)
{
    const int ours = LwNhOurs(field[8], field[9]);
    return g_lwNh.compact(ctx, ours ? LwNhHeight(field[9]) : height, ours ? LwNhClimb(field[9]) : climb, field, compact);
}

static uint8_t LwNhDetail(uintptr_t ctx, uintptr_t mesh, const float* compact, float dist, float error, uintptr_t detail)
{
    const float cs = compact[13], ch = compact[14]; /* +0x34, +0x38 (0x82968E..0x8296A1) */
    const int ours = LwNhOurs(cs, ch);
    uint8_t ok = g_lwNh.detail(ctx, mesh, compact, ours ? g_lwNh.dist : dist, ours ? g_lwNh.error : error, detail);
    if (ok && detail != 0)
    {
        float* verts = *(float**)(detail + 8); const int32_t n = *(const int32_t*)(detail + 0x1C), tris = *(const int32_t*)(detail + 0x20);
        if (ours && verts != NULL && n > 0) LwNhLift(verts, n, cs - ch);
        InterlockedExchangeAdd(&g_lwNh.verts, n); InterlockedExchangeAdd(&g_lwNh.tris, tris);
        LwNhMax(&g_lwNh.vertsMax, n); LwNhMax(&g_lwNh.trisMax, tris);
    }
    LARGE_INTEGER t, f; uintptr_t from = g_lwNh.timed ? (uintptr_t)TlsGetValue(g_lwNh.tls) : 0;
    if (from != 0 && QueryPerformanceCounter(&t) && QueryPerformanceFrequency(&f) && f.QuadPart > 0)
    {
        LONG us = (LONG)((double)((uint64_t)t.QuadPart - from) * 1e6 / (double)f.QuadPart);
        InterlockedExchangeAdd(&g_lwNh.us, us); LwNhMax(&g_lwNh.usMax, us);
        TlsSetValue(g_lwNh.tls, NULL);
    }
    return ok;
}

/* With the 30 s notes: the Recast stages since the last note, from their heights to their detail mesh. */
static void LwNhNote(void)
{
    LONG builds = InterlockedExchange(&g_lwNh.builds, 0), changed = InterlockedExchange(&g_lwNh.changed, 0), us = InterlockedExchange(&g_lwNh.us, 0);
    LONG usMax = InterlockedExchange(&g_lwNh.usMax, 0), verts = InterlockedExchange(&g_lwNh.verts, 0), tris = InterlockedExchange(&g_lwNh.tris, 0);
    LONG vertsMax = InterlockedExchange(&g_lwNh.vertsMax, 0), trisMax = InterlockedExchange(&g_lwNh.trisMax, 0);
    LONG tall = InterlockedExchange(&g_lwNh.tall, 0), clamped = InterlockedExchange(&g_lwNh.clamped, 0);
    if (!builds) return;
    Note("living world: nav heights (30 s): %ld nav cells' terrain built, %ld with a cell height of %.2f m beside their cell size (%ld columns across; detail error %.2f m, "
        "sampled every %.1f m), %ld left for heights past a span's count, %ld clamped columns (tallest box so far %ld m); heights to detail mesh %.1f ms a cell on average, "
        "%.1f ms at most; detail vertices %ld (%ld at most a cell), triangles %ld (%ld at most)",
        (long)builds, (long)changed, (double)g_lwNh.ch, (long)g_lwNh.width, (double)g_lwNh.error, (double)g_lwNh.dist, (long)tall, (long)clamped, (long)g_lwNh.rangeMax,
        (double)us / 1000.0 / (double)builds, (double)usMax / 1000.0, (long)verts, (long)vertsMax, (long)tris, (long)trisMax);
}

/* A research build's value for a switch, within lo..hi; `def` when it is not set or not one. */
static float LwNhSwitch(const char* name, float def, float lo, float hi)
{
    char t[16] = "";
    if (GetEnvironmentVariableA(name, t, sizeof t) == 0) return def;
    float v = strtof(t, NULL);
    return v >= lo && v <= hi ? v : def;
}

/* The three entries' first bytes, their one call each in 0x89A6C0, and the code these hooks stand on: the cell height's
 * store, the walkable counts, the detail mesh's raise and the lowering of its vertices. */
static void LwInstallNavHeight(void)
{
    static const struct { uint32_t rva; const char* bytes; } pins[] = {
        {0x89B630u, "48 8B C4 4C 89 48 20 89 50 10 53 55 57 41 54 41"}, {0x829590u, "4C 89 4C 24 20 44 89 44 24 18 89 54 24 10 48 89"},
        {0x8B53B0u, "48 8B C4 4C 89 40 18 48 89 50 10 48 89 48 08 55"},
        {0x89AA86u, "F3 44 0F 11 40 20 F3 44 0F 11 40 24"}, /* the heightfield's cell size and cell height */
        {0x89AB53u, "E8 D8 0A 00 00"}, {0x89ABA0u, "E8 EB E9 F8 FF"}, {0x89AEB0u, "E8 FB A4 01 00"},
        {0x89B78Au, "F3 0F 5E 7B 24"},       /* heights over the heightfield's cell height */
        {0x89B862u, "B8 FF 1F 00 00"}, {0x89B882u, "B9 FF 1F 00 00"}, /* a span's last count */
        {0x8BCDF8u, "0F B7 4C 24 50 48 89 43 38 0F B6 44 24 58 C1 E1 0D"}, /* a span: its end from bit 13 */
        {0x82968Eu, "41 8B 46 20 41 89 47 34 41 8B 46 24"}, /* the compact heightfield's cell size and height from the heightfield's */
        {0x8B5CE2u, "F3 0F 10 50 38"},       /* the detail vertices' raise: the compact heightfield's cell height */
        {0x89B08Fu, "44 0F 57 05 D9 F7 E0 00"}, /* and the one value they are lowered by */
    };
    if (g_lwNhDone++) return;
    g_lwNh.ch = LwNhSwitch("HELLFORGE_XL_NAV_CH", LW_NH_CH, 0.0f, 4.0f);
    g_lwNh.error = LwNhSwitch("HELLFORGE_XL_NAV_DETAIL_ERROR", LW_NH_ERROR, 0.1f, 8.0f);
    g_lwNh.dist = LwNhSwitch("HELLFORGE_XL_NAV_DETAIL_DIST", LW_NH_DIST, 0.0f, 64.0f);
    uintptr_t e = ApiModuleBase("helldivers2.exe", NULL);
    if (e == 0) e = (uintptr_t)GetModuleHandleA(NULL);
    int ok = !strcmp(g_build, "25480438");
    for (size_t i = 0; ok && i < sizeof pins / sizeof pins[0]; i++)
        if (!ExeCodeMatches(e + pins[i].rva, pins[i].bytes)) { Note("living world: nav heights: exe+0x%X is not the reviewed code", pins[i].rva); ok = 0; }
    if (!ok) { Note("living world: nav heights not installed: the navmesh keeps its 4 m cell height on extra large levels"); return; }
    g_lwNh.tls = TlsAlloc(); g_lwNh.timed = g_lwNh.tls != TLS_OUT_OF_INDEXES;
    /* the detail mesh first: without it a changed cell height would leave the surface lowered by the difference */
    if (LoaderHook("nav heights: detail mesh", e + 0x8B53B0u, (void*)LwNhDetail, (void**)&g_lwNh.detail) || g_lwNh.detail == NULL ||
        LoaderHook("nav heights: compact heightfield", e + 0x829590u, (void*)LwNhCompact, (void**)&g_lwNh.compact) || g_lwNh.compact == NULL)
    { Note("living world: nav heights not installed: a Recast entry could not be hooked"); return; }
    if (LoaderHook("nav heights: heights into spans", e + 0x89B630u, (void*)LwNhRaster, (void**)&g_lwNh.raster) || g_lwNh.raster == NULL)
    { Note("living world: nav heights not installed: the heights' entry could not be hooked"); return; }
    Note("living world: nav heights installed: where a level's nav cells are over 0.5 m the navmesh's terrain has a cell height of %.2f m (0: as the game has it), detail error %.2f m, sampled every %.1f m",
        (double)g_lwNh.ch, (double)g_lwNh.error, (double)g_lwNh.dist);
}

#ifdef CO_TEST
float CoXlTestNhCh(float cs, float ch, float zlo, float zhi, float want) { return LwNhCh(cs, ch, zlo, zhi, want); }
int CoXlTestNhCells(float ch, int climb) { return climb ? LwNhClimb(ch) : LwNhHeight(ch); }
int CoXlTestNhClamped(const uintptr_t* spans, int columns) { return LwNhClamped(spans, columns); }
void CoXlTestNhCounts(long* changed, long* tall, long* clamped) { *changed = InterlockedExchange(&g_lwNh.changed, 0); *tall = InterlockedExchange(&g_lwNh.tall, 0); *clamped = InterlockedExchange(&g_lwNh.clamped, 0); }
void CoXlTestNhLift(float* verts, int n, float by) { LwNhLift(verts, n, by); }
/* The hooks over the test's own stages. */
void CoXlTestNhSet(float ch, float error, float dist, void* raster, void* compact, void* detail)
{
    g_lwNh.ch = ch; g_lwNh.error = error; g_lwNh.dist = dist; g_lwNh.timed = 0;
    g_lwNh.raster = (LwNhRasterFn)raster; g_lwNh.compact = (LwNhCompactFn)compact; g_lwNh.detail = (LwNhDetailFn)detail;
}
int CoXlTestNhInstall(void) { g_lwNhDone = 0; g_lwNh.raster = NULL; g_lwNh.compact = NULL; g_lwNh.detail = NULL; LwInstallNavHeight(); return g_lwNh.raster != NULL; }
uintptr_t CoXlTestNhRaster(float* field) { return LwNhRaster(1, 2, NULL, field); }
uint8_t CoXlTestNhCompact(int height, int climb, float* field) { return LwNhCompact(1, height, climb, field, 5); }
uint8_t CoXlTestNhDetail(const float* compact, float dist, float error, uintptr_t detail) { return LwNhDetail(1, 2, compact, dist, error, detail); }
#endif
