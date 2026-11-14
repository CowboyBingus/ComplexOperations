/* Diagnostic beside living_world_tilt.c (issue G): why a tilted unit stands on a prop. Read only, bounded a scan.
 * - The nav grid's objects at the prop. An object (0x80 B, 0x7B52A0 fills it): +0 its pose, 4 rows (the box is
 *   x row0 + y row1 + z row2 + row3, 0x9FB5C0), +0x40 flags, +0x44 material, +0x48 / +0x54 its box, +0x68 a carver,
 *   +0x70 a painter, +0x78 geometry. A cell's build start (0x7B7590) hands Havok each +0x68 as a carver, appends the
 *   geometry of flag 1 to the cell's input, and hands the +0x70 of flags 0x30 as a painter; nothing else of an object
 *   reaches the navmesh. The objects come from the engine's own gather (the nav job lever's pinned 0x7ACDD0, 0x7ADA50).
 * - The closest face for the unit's own agent type (0x7D89F0's sixth argument: a hash of the settings' agent types,
 *   [exe+0x27C5CF8]; type 0 or an unknown hash asks with no filter, 0x7D8AE2..0x7D8B39).
 * - Each nav cell's last build start: the objects in its box then, against those in it now, and the three counts the
 *   game keeps for its own line (0x7B2133: [cell+0x50] +0x60 carvers, +0x40 painters, +0x20 volumes).
 * - Whether a carver can reach the face over it. Havok hands a face only the carvers whose box meets the face's own box
 *   raised by the character height along up (0xB2E2C6..0xB2E361, 0xB7F2F0; the height is [settings+0x18], 0xB2DEF7);
 *   a cut piece is dropped when its centroid lies inside a carver, a point test in 3D (0xB32A3E..0xB32BF3, 0xB7EB40,
 *   0x82F010). A face that floats over a prop's top is never cut. The box Havok asks is the carver's volume's own
 *   ([[object+0x68]+0x18] +0x30 / +0x40, written at 0x82F5C7), not the grid object's. */
#define LW_TN_BUF 131072u     /* object pointers one gather may hand back: the grid holds no more objects */
#define LW_TN_CELLS 4096u     /* nav cells whose last build start is kept (64 x 64 at most) */
#define LW_TN_CELL_LINES 6    /* cell lines a 30 s window */
#define LW_TN_GROW 1.0f       /* metres an object's box is grown before it is asked for the prop's point */
enum { LW_TN_CARVER, LW_TN_GEOMETRY, LW_TN_PAINTER, LW_TN_INERT, LW_TN_KINDS };

/* carverLo..carverHi: the world heights the carver objects' boxes span; volumes: the carvers whose volume's own box
 * holds the point's (x, y), volLo..volHi the heights those span; owner: the first object's resource (+0x60) */
typedef struct { uint32_t n[LW_TN_KINDS]; uint32_t flags, material, painters, volumes; float lo, hi, carverLo, carverHi, volLo, volHi; uint64_t owner; int read; } LwTnAt;
typedef struct { uint32_t at[LW_TN_KINDS], game[3]; float box[4]; ULONGLONG tick; uint32_t builds; uint8_t noted; } LwTnCell;
static struct
{
    LONG level; ULONGLONG levelAt;
    float height; /* the character height of the last build's settings */
    LwTnCell cell[LW_TN_CELLS];
    uintptr_t buf[LW_TN_BUF];
} g_lwTn = {.level = -1};
static SRWLOCK g_lwTnLock = SRWLOCK_INIT; /* the buffer: the game thread's scan and the nav tick's build starts */

/* What the build start does with an object: its carver first, then its geometry, then its painter. */
static int LwTnKind(uint32_t flags, uintptr_t carver)
{
    if (carver != 0) return LW_TN_CARVER;
    if (flags & 1u) return LW_TN_GEOMETRY;
    return (flags & 0x30u) ? LW_TN_PAINTER : LW_TN_INERT;
}

/* Whether the box (lo..hi along the rows of pose m), grown, stands over p: the row nearest world up is not asked, so
 * a carver that ends under the prop's surface still counts. *zlo / *zhi: the box's world heights. */
static int LwTnOver(const float* m, const float* lo, const float* hi, const float* p, float grow, float* zlo, float* zhi)
{
    const float d[3] = {p[0] - m[12], p[1] - m[13], p[2] - m[14]};
    float len[3], t[3], centre = m[14], reach = 0; int upRow = 0;
    for (int a = 0; a < 3; a++)
    {
        const float* r = m + 4 * a;
        len[a] = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (!(len[a] > 1e-6f) || !(hi[a] >= lo[a])) return 0;
        t[a] = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / (len[a] * len[a]);
        centre += (lo[a] + hi[a]) * 0.5f * r[2]; reach += fabsf((hi[a] - lo[a]) * 0.5f * r[2]);
        if (fabsf(r[2]) / len[a] > fabsf(m[4 * upRow + 2]) / len[upRow]) upRow = a;
    }
    *zlo = centre - reach; *zhi = centre + reach;
    for (int a = 0; a < 3; a++)
        if (a != upRow && (t[a] < lo[a] - grow / len[a] || t[a] > hi[a] + grow / len[a])) return 0;
    return 1;
}

/* A carver's volume's world box, the one Havok's queries ask: 1 when read. */
static int LwTnVolumeBox(uintptr_t carver, float* lo, float* hi)
{
    uintptr_t volume = 0;
    return EvpPtr(carver + 0x18, &volume) && !SafeRead(volume + 0x30, lo, 12) && !SafeRead(volume + 0x40, hi, 12) && hi[0] >= lo[0] && hi[1] >= lo[1] && hi[2] >= lo[2];
}

/* The objects of the grid cells lo..hi (0x7ADA50, as the build start asks): how many, -1 when the lever's pins did not
 * hold or the cells hold more than the buffer. */
static int LwTnGather(uintptr_t grid, uint32_t lo, uint32_t hi)
{
    if (g_lwNj.gather == NULL || g_lwNj.cell == NULL) return -1;
    uint32_t n = g_lwNj.gather(grid, lo, hi, g_lwTn.buf, LW_TN_BUF);
    return n > LW_TN_BUF ? -1 : (int)n;
}

/* The nav objects standing over p, by kind; the first one's flags, material and heights. */
static void LwTnObjects(uintptr_t nav, const float* p, LwTnAt* out)
{
    memset(out, 0, sizeof *out);
    if (nav == 0 || g_lwNj.cell == NULL) return;
    uint32_t c = g_lwNj.cell(nav + 0x6710u, p[0], p[1]);
    AcquireSRWLockExclusive(&g_lwTnLock);
    int n = LwTnGather(nav + 0x6710u, c, c);
    out->read = n >= 0;
    for (int i = 0; i < n; i++)
    {
        uint8_t o[0x80]; float m[16], box[6], zlo, zhi; uint32_t fm[2]; uintptr_t carver, painter;
        if (SafeRead(g_lwTn.buf[i], o, sizeof o)) continue;
        memcpy(m, o, sizeof m); memcpy(fm, o + 0x40, sizeof fm); memcpy(box, o + 0x48, sizeof box); memcpy(&carver, o + 0x68, 8); memcpy(&painter, o + 0x70, 8);
        if (!LwTnOver(m, box, box + 3, p, LW_TN_GROW, &zlo, &zhi)) continue;
        int kind = LwTnKind(fm[0], carver);
        if (out->n[0] + out->n[1] + out->n[2] + out->n[3] == 0) { out->flags = fm[0]; out->material = fm[1]; out->lo = zlo; out->hi = zhi; memcpy(&out->owner, o + 0x60, 8); }
        if (kind == LW_TN_CARVER)
        {
            if (out->n[LW_TN_CARVER] == 0 || zlo < out->carverLo) out->carverLo = zlo;
            if (out->n[LW_TN_CARVER] == 0 || zhi > out->carverHi) out->carverHi = zhi;
        }
        float vlo[3], vhi[3];
        if (kind == LW_TN_CARVER && LwTnVolumeBox(carver, vlo, vhi) && p[0] >= vlo[0] && p[0] <= vhi[0] && p[1] >= vlo[1] && p[1] <= vhi[1])
        {
            if (out->volumes == 0 || vlo[2] < out->volLo) out->volLo = vlo[2];
            if (out->volumes == 0 || vhi[2] > out->volHi) out->volHi = vhi[2];
            out->volumes++;
        }
        out->painters += painter != 0;
        out->n[kind]++;
    }
    ReleaseSRWLockExclusive(&g_lwTnLock);
}

/* A hash's place among the settings' agent types (R = [exe+0x27C5CF8]: T = R + [R+0x34], count [T], hashes T+8): -1
 * when it is not one. Place 0 asks the face query with no filter, as no hash does. */
static int LwTnAgentIndex(uintptr_t exe, uint32_t hash)
{
    uintptr_t R = 0; uint32_t oT = 0, count = 0, h = 0;
    if (hash == 0 || exe == 0 || !EvpPtr(exe + 0x27C5CF8u, &R) || SafeRead(R + 0x34, &oT, 4) || SafeRead(R + oT, &count, 4) || count > 64) return -1;
    for (uint32_t t = 0; t < count; t++)
        if (!SafeRead(R + 8 + oT + 4ull * t, &h, 4) && h == hash) return (int)t;
    return -1;
}

/* An entity's agent type hash, from its NavigationComponent record (as LwNavTypeDump reads it): 0 when it has none. */
static uint32_t LwTnAgent(uint32_t entity)
{
    uintptr_t nav = 0, keys = 0, recs = 0; uint32_t places = 0, empty = 0, mult = 0, type = 0;
    if (!EvpPtr(g_lw.game + 0x3326418u, &nav) || !EvpPtr(nav + 0x30, &keys) || !EvpPtr(nav + 0x50, &recs) || SafeRead(nav + 0x38, &places, 4) ||
        SafeRead(nav + 0x3C, &empty, 4) || SafeRead(nav + 0x40, &mult, 4) || places == 0 || (places & (places - 1)) != 0) return 0;
    for (uint32_t p = 0; p < places; p++)
    {
        uint32_t kv[2] = {0, 0};
        if (SafeRead(keys + 8ull * ((entity * mult + p) & (places - 1)), kv, 8) || kv[0] == empty) return 0;
        if (kv[0] == entity) return SafeRead(recs + 0xA8ull * kv[1] + 0x9C, &type, 4) ? 0 : type;
    }
    return 0;
}

/* The mission's nav world, as the game's own spawn snap finds it ([dir+0x648], game 0x95374C): any level. */
static uintptr_t LwTnNav(uint32_t* id)
{
    uintptr_t dir = 0, worlds = 0, nav = 0, exe = g_navExe;
    *id = 0xFFFFFFFFu;
    if (exe == 0 || !EvpPtr(g_lw.game + LW_DIRECTOR, &dir)) return 0;
    *id = EvpU32(dir + 0x648, 0xFFFFFFFFu);
    return *id < EvpU32(exe + LW_NAV_WORLDS + 8, 0) && EvpPtr(exe + LW_NAV_WORLDS, &worlds) && EvpPtr(worlds + 8ull * *id, &nav) ? nav : 0;
}

/* The navmesh face nearest (x, y, z) within 10 m for an agent type hash (0: any face), from the nav API's closest
 * face ([[game+0x3326330]+0x10] = exe 0x7D89F0): 1 with the face point. */
static int LwTnFace(float x, float y, float z, uint32_t agent, float* f)
{
    typedef int (*SnapFn)(uint32_t nav, float* p, float radius, float* out, float* normal, uint32_t agent);
    uintptr_t table = 0, fn = 0; uint32_t id;
    if (LwTnNav(&id) == 0 || !EvpPtr(g_lw.game + 0x3326330u, &table) || !EvpPtr(table + 0x10, &fn) || fn == 0) return 0;
    f[0] = x; f[1] = y; f[2] = z;
    return ((SnapFn)fn)(id, f, 10.0f, f, NULL, agent) != 0 && isfinite(f[2]);
}

/* Where navmesh heights flo..fhi lie against carver boxes spanning lo..hi, as Havok hands a triangle its carvers (the
 * triangle's box raised by the character height): 1 all of it is over them, -1 they are over all of it raised, 0 they
 * meet. */
static int LwTnReach(float flo, float fhi, float lo, float hi, float height) { return flo > hi ? 1 : fhi + height < lo ? -1 : 0; }

/* A face against the prop's surface in the same column: 1 over it, -1 under it, 0 within 0.3 m of it. */
static int LwTnAgainst(float face, float surface) { return face > surface + 0.3f ? 1 : face < surface - 0.3f ? -1 : 0; }

/* The face in the column of (x, y): the closest face asked from z and from heights around it, the first that lies
 * within 0.5 m across. The closest face alone is the nearest in 3D, which may be a face beside the column. */
static int LwTnColumnFace(float x, float y, float z, uint32_t agent, float* fz)
{
    static const float kFrom[] = {0.0f, 2.0f, 4.0f, 6.0f, -2.0f};
    for (unsigned k = 0; k < sizeof kFrom / sizeof kFrom[0]; k++)
    {
        float f[3];
        if (LwTnFace(x, y, z + kFrom[k], agent, f) && hypotf(f[0] - x, f[1] - y) < 0.5f) { *fz = f[2]; return 1; }
    }
    return 0;
}

/* The navmesh's heights around a column: the faces in the columns of (x, y) and of the 8 points 4 m around it (a nav
 * cell's size on these levels). The input triangle Havok boxes is not kept by the game; this stands for its heights.
 * The number of columns with a face. */
static int LwTnAround(float x, float y, float z, float* lo, float* hi)
{
    int n = 0;
    for (int k = 0; k < 9; k++)
    {
        float fz;
        if (!LwTnColumnFace(x + 4.0f * (float)(k % 3 - 1), y + 4.0f * (float)(k / 3 - 1), z, 0, &fz)) continue;
        if (n == 0 || fz < *lo) *lo = fz;
        if (n == 0 || fz > *hi) *hi = fz;
        n++;
    }
    return n;
}

/* The objects in the box (x0, y0, x1, y1), by kind: the engine's gather over its grid cells, counted as LwTnKind. */
static int LwTnBoxCount(uintptr_t grid, const float* box, uint32_t* count)
{
    memset(count, 0, sizeof(uint32_t) * LW_TN_KINDS);
    if (g_lwNj.cell == NULL) return 0;
    AcquireSRWLockExclusive(&g_lwTnLock);
    int n = LwTnGather(grid, g_lwNj.cell(grid, box[0], box[1]), g_lwNj.cell(grid, box[2], box[3]));
    for (int i = 0; i < n; i++)
    {
        uint32_t flags = 0; uintptr_t carver = 0;
        if (!SafeRead(g_lwTn.buf[i] + 0x40, &flags, 4) && !SafeRead(g_lwTn.buf[i] + 0x68, &carver, 8)) count[LwTnKind(flags, carver)]++;
    }
    ReleaseSRWLockExclusive(&g_lwTnLock);
    return n >= 0;
}

/* The box a cell's build start gathers from (0x7B7682..0x7B76D9): its snapshot's bounds ([cell+0x18] +0xD0 / +0xE0),
 * widened by the vector at exe 0x16AA6E0. The snapshot is the build's own, so this is read at the start only. */
static int LwTnCellBox(uintptr_t grid, uint32_t cellId, float* box)
{
    uintptr_t cells = 0, snap = 0; uint16_t stride = 0; float lo[2], hi[2], m[2];
    if (g_navExe == 0 || !EvpPtr(grid + 0x38, &cells) || SafeRead(grid + 0x26, &stride, 2) || !EvpPtr(cells + (uintptr_t)stride * cellId + 0x18, &snap) ||
        SafeRead(snap + 0xD0, lo, 8) || SafeRead(snap + 0xE0, hi, 8) || SafeRead(g_navExe + 0x16AA6E0u, m, 8)) return 0;
    box[0] = lo[0] - m[0]; box[1] = lo[1] - m[1]; box[2] = hi[0] + m[0]; box[3] = hi[1] + m[1];
    return isfinite(box[0]) && isfinite(box[1]) && isfinite(box[2]) && isfinite(box[3]);
}

static void LwTnLevel(void)
{
    if (g_lwTn.level == g_world.levels) return;
    g_lwTn.level = g_world.levels; g_lwTn.levelAt = GetTickCount64();
    memset(g_lwTn.cell, 0, sizeof g_lwTn.cell);
}

/* The three counts of a cell's build the game keeps for its own line: carvers, painters, volumes. 0 when unread. */
static int LwTnGameCounts(uintptr_t grid, uint32_t cellId, uint32_t* out)
{
    uintptr_t cells = 0, stats = 0; uint16_t stride = 0;
    out[0] = out[1] = out[2] = 0;
    return EvpPtr(grid + 0x38, &cells) && !SafeRead(grid + 0x26, &stride, 2) && EvpPtr(cells + (uintptr_t)stride * cellId + 0x50, &stats) &&
        !SafeRead(stats + 0x60, &out[0], 4) && !SafeRead(stats + 0x40, &out[1], 4) && !SafeRead(stats + 0x20, &out[2], 4);
}

/* After a cell's build start (the nav job's hook, main thread): what its box held, kept as the cell's last build. */
static void LwTnCellStart(uintptr_t grid, uint32_t cellId)
{
    LwTnLevel();
    if (cellId >= LW_TN_CELLS) return;
    LwTnCell* c = &g_lwTn.cell[cellId];
    float box[4], height = 0; uintptr_t cells = 0, snap = 0; uint16_t stride = 0;
    if (!LwTnCellBox(grid, cellId, box) || !LwTnBoxCount(grid, box, c->at)) return;
    if (EvpPtr(grid + 0x38, &cells) && !SafeRead(grid + 0x26, &stride, 2) && EvpPtr(cells + (uintptr_t)stride * cellId + 0x18, &snap) &&
        !SafeRead(snap + 0x18, &height, 4) && height > 0.0f && height < 100.0f) g_lwTn.height = height;
    LwTnGameCounts(grid, cellId, c->game);
    memcpy(c->box, box, sizeof box);
    c->tick = GetTickCount64(); c->builds++; c->noted = 0;
}

/* Whether a cell's box holds more carvers, geometry or painters now than its last build was handed. */
static int LwTnLate(const uint32_t* at, const uint32_t* now) { return now[LW_TN_CARVER] > at[LW_TN_CARVER] || now[LW_TN_GEOMETRY] > at[LW_TN_GEOMETRY] || now[LW_TN_PAINTER] > at[LW_TN_PAINTER]; }

/* Whether (x, y) lies in the box of a built cell: 0 or 1 by box and point alone. */
static int LwTnInBox(const float* box, float x, float y) { return x >= box[0] && x <= box[2] && y >= box[1] && y <= box[3]; }

/* The built cells whose box holds (x, y): 1 when one of them holds more now than its last build was handed, 0 when
 * none does, -1 when no built cell holds the point. */
static int LwTnPointLate(uintptr_t nav, float x, float y)
{
    int found = -1;
    for (uint32_t k = 0; k < LW_TN_CELLS; k++)
    {
        const LwTnCell* c = &g_lwTn.cell[k]; uint32_t now[LW_TN_KINDS];
        if (c->builds == 0 || !LwTnInBox(c->box, x, y) || !LwTnBoxCount(nav + 0x6710u, c->box, now)) continue;
        if (LwTnLate(c->at, now)) return 1;
        found = 0;
    }
    return found;
}

/* With the 30 s notes: each cell built since the last note, and how many cells' boxes hold more now than their last
 * build was handed (those objects are not in the cell's navmesh until it is built again). */
static void LwTnNote(void)
{
    uint32_t id; uintptr_t nav = LwTnNav(&id);
    LwTnLevel();
    if (nav == 0) return;
    uint32_t built = 0, late = 0, lines = 0, lateCarvers = 0, firstLate = 0xFFFFFFFFu;
    for (uint32_t k = 0; k < LW_TN_CELLS; k++)
    {
        LwTnCell* c = &g_lwTn.cell[k]; uint32_t now[LW_TN_KINDS];
        if (c->builds == 0 || !LwTnBoxCount(nav + 0x6710u, c->box, now)) continue;
        built++;
        if (LwTnLate(c->at, now)) { late++; lateCarvers += now[LW_TN_CARVER] > c->at[LW_TN_CARVER] ? now[LW_TN_CARVER] - c->at[LW_TN_CARVER] : 0; if (firstLate == 0xFFFFFFFFu) firstLate = k; }
        if (c->noted || lines >= LW_TN_CELL_LINES) continue;
        c->noted = 1; lines++;
        Note("living world: tilt: nav cell %u: build %u started %.1f s into the level with %u carvers, %u geometry shapes, %u painters in its box; now %u, %u, %u; the game's own counts: %u carvers, %u painters, %u volumes; character height %.2f m",
            k, c->builds, (double)(c->tick - g_lwTn.levelAt) / 1000.0, c->at[LW_TN_CARVER], c->at[LW_TN_GEOMETRY], c->at[LW_TN_PAINTER],
            now[LW_TN_CARVER], now[LW_TN_GEOMETRY], now[LW_TN_PAINTER], c->game[0], c->game[1], c->game[2], (double)g_lwTn.height);
    }
    if (built)
        Note("living world: tilt: nav cells (30 s): %u built this level; %u hold more in their box now than their last build was handed (%u carvers more; the first is cell %d)",
            built, late, lateCarvers, (int)firstLate);
}

#ifdef CO_TEST
int CoXlTestTnKind(uint32_t flags, uintptr_t carver) { return LwTnKind(flags, carver); }
int CoXlTestTnOver(const float* m, const float* lo, const float* hi, const float* p, float grow, float* zlo, float* zhi) { return LwTnOver(m, lo, hi, p, grow, zlo, zhi); }
int CoXlTestTnReach(float flo, float fhi, float lo, float hi, float height) { return LwTnReach(flo, fhi, lo, hi, height); }
int CoXlTestTnAgainst(float face, float surface) { return LwTnAgainst(face, surface); }
int CoXlTestTnInBox(const float* box, float x, float y) { return LwTnInBox(box, x, y); }
int CoXlTestTnLate(const uint32_t* at, const uint32_t* now) { return LwTnLate(at, now); }
int CoXlTestTnAgentIndex(uintptr_t exe, uint32_t hash) { return LwTnAgentIndex(exe, hash); }
/* The engine's cell and gather steps as the test's own; then the objects over p as the probe counts them. */
void CoXlTestTnObjects(uintptr_t nav, uint32_t (*cell)(uintptr_t, float, float), uint32_t (*gather)(uintptr_t, uint32_t, uint32_t, uintptr_t*, uint32_t),
    const float* p, uint32_t* counts, uint32_t* flags, uint32_t* material, int* read, float* carverZ, uint32_t* painters, uint64_t* owner, float* volumeZ, uint32_t* volumes)
{
    LwTnAt at;
    g_lwNj.cell = (LwNjCellFn)cell; g_lwNj.gather = (LwNjGatherFn)gather;
    LwTnObjects(nav, p, &at);
    memcpy(counts, at.n, sizeof at.n); *flags = at.flags; *material = at.material; *read = at.read;
    carverZ[0] = at.carverLo; carverZ[1] = at.carverHi; *painters = at.painters; *owner = at.owner;
    volumeZ[0] = at.volLo; volumeZ[1] = at.volHi; *volumes = at.volumes;
}
#endif
