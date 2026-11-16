/* Ring corpse freeze (KB fight-ragdoll-constraints lever 1; HELLFORGE_LW_CORPSE=0 off, 2 even unit slots only for a
 * measurement; command "corpse 0|1|2"). A corpse in the game's ring (32 entries, dll 0x13BE660) keeps its ragdoll
 * bodies in the physics world until 32 newer deaths evict it, and in fights their terrain pairs are most of the serial
 * pairs task. Vanilla freezes a corpse only in disposal, once it moved under 0.1 m^2 in a second (dll 0x13BE930-0x13BE9FF);
 * this runs that same step on ring corpses. It sends nothing over the network, and clients freeze their own copies. */
#define LW_CORPSE_RING 32
typedef int (*LwCorpseHandlesFn)(uint32_t unit, uint32_t mask, uint8_t* out);
typedef uint8_t (*LwCorpseValidFn)(uint32_t unit);
typedef const float* (*LwCorpsePosFn)(uint32_t unit, int node);
typedef void (*LwCorpseEnableFn)(const uint32_t* ids, uint32_t n, int on);
typedef void (*LwCorpseLevelFn)(const uint32_t* ids, uint32_t n, int level);
typedef uint8_t (*LwCorpseHeldFn)(uint32_t id, int level);
typedef struct { uint32_t unit; int have, done, calm; float last[3]; ULONGLONG at; } LwCorpseSlot;
static struct
{
    int mode, ready, failed; LwCorpseHandlesFn handles; LwCorpseValidFn valid; LwCorpsePosFn pos; LwCorpseEnableFn enable; LwCorpseLevelFn level;
    LwCorpseHeldFn held;
    LwCorpseSlot slot[LW_CORPSE_RING];
    uint32_t frozen, frozenNow, seen, posed, already; ULONGLONG notedAt;
} g_lwCorpse = {.mode = 1};

/* The game's routines through its own tables, each checked against this build's address, and the ragdoll pose sync's
 * layout. The pose sync (game.dll 0x7AAA70, its one caller 0x572154 passing state + 0x7574C8, which 0x56885B also
 * stores at 0x3326948): the entity count +0x10 (0x7AAB38), entities +0x38, runtime records +0x48 of 0x2BB8 bytes whose
 * byte +0x2BA4 is set while it moves the entity (0x7AABC0..0x7AAC29), an entity's unit +0xC (0x13C01C7). */
static int LwCorpseBind(void)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL), t = 0, s = 0, f[6] = {0};
    if (!ApiCodeMatches(g_lw.game + 0x56885Bu, "48 89 05 E6 E0 DB 02") || !ApiCodeMatches(g_lw.game + 0x568854u, "48 8D 83 C8 74 75 00") ||
        !ApiCodeMatches(g_lw.game + 0x7AAB38u, "41 39 75 10") ||
        !ApiCodeMatches(g_lw.game + 0x7AABC0u, "49 8B 5D 38 44 8B E6 4C 89 65 A0 48 8B 04 F3 48 8B C8 48 89 45 90 E8 ?? ?? ?? ?? 4D 8B 75 50 48 8B F8 4D 8B 7D 48") ||
        !ApiCodeMatches(g_lw.game + 0x7AAC00u, "49 69 C4 B8 2B 00 00 48 89 45 B0 4C 03 F8") ||
        !ApiCodeMatches(g_lw.game + 0x7AAC22u, "41 80 BF A4 2B 00 00 00") || !ApiCodeMatches(g_lw.game + 0x13C01E4u, "44 8B 4A 0C"))
    { Note("living world: corpse freeze: the ragdoll pose sync is not the reviewed code; off"); g_lwCorpse.failed = 1; return 0; }
    if (SafeRead(g_lw.game + 0x3326310u, &t, 8) || t == 0 || SafeRead(t, &f[0], 8)) return 0;
    if (SafeRead(g_lw.game + 0x3326308u, &t, 8) || t == 0 || SafeRead(t + 0x18, &s, 8) || s == 0 || SafeRead(s + 0x720, &f[1], 8) || SafeRead(s + 0x88, &f[2], 8)) return 0;
    if (SafeRead(g_lw.game + 0x3326338u, &t, 8) || t == 0 || SafeRead(t + 0x08, &f[3], 8)) return 0;
    if (SafeRead(g_lw.game + 0x33263F8u, &t, 8) || t == 0 || SafeRead(t + 0x48, &f[4], 8) || SafeRead(t + 0x58, &f[5], 8)) return 0;
    if (f[0] != exe + 0x7E1C20u || f[1] != exe + 0x1FEEB0u || f[2] != exe + 0x1FD190u || f[3] != exe + 0x77F4F0u || f[4] != exe + 0x7A3D00u ||
        f[5] != exe + 0x7A3F10u)
    { Note("living world: corpse freeze: the game's tables are not this build's; off"); g_lwCorpse.failed = 1; return 0; }
    g_lwCorpse.handles = (LwCorpseHandlesFn)f[0]; g_lwCorpse.valid = (LwCorpseValidFn)f[1]; g_lwCorpse.pos = (LwCorpsePosFn)f[2];
    g_lwCorpse.enable = (LwCorpseEnableFn)f[3]; g_lwCorpse.level = (LwCorpseLevelFn)f[4]; g_lwCorpse.held = (LwCorpseHeldFn)f[5];
    g_lwCorpse.ready = 1;
    return 1;
}

/* Vanilla's settle test: the unit's root moved under 0.1 m^2 ([dll 0x23C675C]) since the sample a second ago. */
static int LwCorpseSettled(LwCorpseSlot* s, ULONGLONG now)
{
    if (now - s->at < 1000 || !g_lwCorpse.valid(s->unit)) return 0; /* the position read needs a live unit, as 0x13BE8DC */
    s->at = now;
    const float* p = g_lwCorpse.pos(s->unit, 0);
    float dx = p[0] - s->last[0], dy = p[1] - s->last[1], dz = p[2] - s->last[2];
    int settled = s->have && dx * dx + dy * dy + dz * dz < 0.1f;
    memcpy(s->last, p, sizeof s->last); s->have = 1;
    return settled;
}

/* 1 while the ragdoll pose sync still moves the unit (on a client, its old entity until that entity is gone): the
 * game's disposal step only meets corpses past it. A manager that does not read counts as moving. */
static int LwCorpsePosed(uint32_t unit)
{
    uintptr_t m = 0, entities = 0, runtime = 0; uint32_t count = 0;
    if (SafeRead(g_lw.game + 0x3326948u, &m, 8) || m == 0) return 0;
    if (SafeRead(m + 0x10, &count, 4) || SafeRead(m + 0x38, &entities, 8) || SafeRead(m + 0x48, &runtime, 8) || count > 4096) return 1;
    for (uint32_t i = 0; i < count; i++)
    {
        uintptr_t entity = 0; uint32_t of = 0; uint8_t on = 0;
        if (SafeRead(entities + 8 * (uintptr_t)i, &entity, 8)) return 1;
        if (entity == 0) continue;
        if (SafeRead(entity + 0xC, &of, 4)) return 1;
        if (of == unit && (SafeRead(runtime + 0x2BB8 * (uintptr_t)i + 0x2BA4, &on, 1) || on)) return 1;
    }
    return 0;
}

/* Vanilla's two calls, skipping the actors when the entry's byte +0x38 says so (dll 0x13BE9C7). The ragdoll level
 * request is counted (exe 0x7A3D00 adds one to a byte per level; 0x7A3DF0 takes one off), and the game wakes a ragdoll
 * by taking one level-3 request off (dll 0x11AA50A, 0x5A5159, 0xB7FF36): a second request would keep it frozen. So
 * a corpse whose ragdolls already hold one (HellForge 0.5's corpse-freeze, or disposal; exe 0x7A3F10 reads the count)
 * is left as it is. 1 frozen here, 2 already frozen, 0 no handles. */
static int LwCorpseFreeze(uint32_t unit, uint8_t keepActors)
{
    uint8_t h[0x60] = {0}; uint64_t actors, ragdolls; uint32_t na, nr;
    if (g_lwCorpse.handles(unit, 5, h) <= 0) return 0; /* 5: actors and ragdolls; the getter writes up to +0x58 */
    memcpy(&actors, h, 8); memcpy(&na, h + 0x38, 4); memcpy(&ragdolls, h + 0x10, 8); memcpy(&nr, h + 0x40, 4);
    for (uint32_t i = 0; ragdolls && i < nr; i++)
        if (g_lwCorpse.held(((const uint32_t*)(uintptr_t)ragdolls)[i], 3) != 0) return 2;
    if (!keepActors && actors && na) g_lwCorpse.enable((const uint32_t*)(uintptr_t)actors, na, 0);
    if (ragdolls && nr) g_lwCorpse.level((const uint32_t*)(uintptr_t)ragdolls, nr, 3);
    return 1;
}

static void LwCorpseNote(ULONGLONG now)
{
    if (now - g_lwCorpse.notedAt < 30000) return;
    if (g_lwCorpse.mode == 2 || LwTestTraceOn())
        Note("living world: corpse freeze %d: %u ring corpses frozen now, %u frozen, %u found frozen already, %u waits for the pose sync and %u new ring corpses in 30 s",
            g_lwCorpse.mode, g_lwCorpse.frozenNow, g_lwCorpse.frozen, g_lwCorpse.already, g_lwCorpse.posed, g_lwCorpse.seen);
    g_lwCorpse.frozen = g_lwCorpse.seen = g_lwCorpse.already = g_lwCorpse.posed = 0; g_lwCorpse.notedAt = now;
}

/* One ring entry: 1 when its corpse is frozen. */
static int LwCorpseSlotTick(LwCorpseSlot* s, const uint8_t* e, ULONGLONG now)
{
    uint32_t unit, state;
    memcpy(&unit, e, 4); memcpy(&state, e + 8, 4);
    if (unit == 0 || state != 2) { s->unit = 0; return 0; } /* 2: converted to a Corpse (0x13BE707) */
    if (s->unit != unit) { memset(s, 0, sizeof *s); s->unit = unit; g_lwCorpse.seen++; }
    if (s->done || (g_lwCorpse.mode == 2 && (unit & 1u))) return s->done;
    if (now - s->at < 1000) return 0;
    /* two settled samples, one more than the game's disposal and HellForge's corpse-freeze ask: with that on, it
     * freezes first and this finds the request held */
    s->calm = LwCorpseSettled(s, now) ? s->calm + 1 : 0;
    if (s->calm < 2) return 0;
    if (LwCorpsePosed(unit)) { g_lwCorpse.posed++; return 0; }
    int r = LwCorpseFreeze(unit, e[0x38]);
    if (r != 0) { s->done = 1; if (r == 1) g_lwCorpse.frozen++; else g_lwCorpse.already++; }
    return s->done;
}

/* Game thread, once a frame, as the corpse service itself runs. */
static void LwCorpseFrame(void)
{
    uintptr_t svc = 0; ULONGLONG now = GetTickCount64(); uint32_t live = 0; uint8_t e[0x40];
    if (g_lwCorpse.mode == 0 || g_lwCorpse.failed || (!g_lwCorpse.ready && !LwCorpseBind())) return;
    if (SafeRead(g_lw.game + 0x346D500u, &svc, 8) || svc == 0) return;
    for (int i = 0; i < LW_CORPSE_RING; i++)
    {
        if (SafeRead(svc + 0x18 + 0x40u * (uintptr_t)i, e, sizeof e)) return;
        live += (uint32_t)LwCorpseSlotTick(&g_lwCorpse.slot[i], e, now);
    }
    g_lwCorpse.frozenNow = live;
    LwCorpseNote(now);
}

static void LwCorpseInit(void)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_LW_CORPSE", t, sizeof t) && t[0] >= '0' && t[0] <= '2') g_lwCorpse.mode = t[0] - '0';
    g_lwCorpse.notedAt = GetTickCount64();
}
/* Leaving a complex level: its corpses go with it, so none of their slots may match a unit of the next. */
static void LwCorpseLeave(void) { memset(g_lwCorpse.slot, 0, sizeof g_lwCorpse.slot); g_lwCorpse.frozenNow = 0; }
/* Off stops new freezes; frozen corpses stay as they are, as in disposal. */
static void LwCorpseCmd(int mode)
{
    if (mode < 0 || mode > 2) { Note("living world: test: corpse freeze: a value outside 0..2"); return; }
    g_lwCorpse.mode = mode; Note("living world: test: corpse freeze %d", mode);
}

#ifdef CO_TEST
/* One ring slot as LwCorpseFrame ticks it, with the engine's calls stood in for and the pose sync's manager read from
 * `game` + 0x3326948: the slot's result, and the freeze's counts. */
void CoXlTestCorpseSetup(uintptr_t game, void* handles, void* valid, void* pos, void* enable, void* level, void* held)
{
    g_lw.game = game;
    memset(&g_lwCorpse.slot, 0, sizeof g_lwCorpse.slot);
    g_lwCorpse.mode = 1; g_lwCorpse.failed = 0; g_lwCorpse.ready = 1;
    g_lwCorpse.handles = (LwCorpseHandlesFn)handles; g_lwCorpse.valid = (LwCorpseValidFn)valid; g_lwCorpse.pos = (LwCorpsePosFn)pos;
    g_lwCorpse.enable = (LwCorpseEnableFn)enable; g_lwCorpse.level = (LwCorpseLevelFn)level; g_lwCorpse.held = (LwCorpseHeldFn)held;
    g_lwCorpse.frozen = g_lwCorpse.already = g_lwCorpse.posed = g_lwCorpse.seen = 0;
}
int CoXlTestCorpseTick(uint32_t unit, ULONGLONG now)
{
    uint8_t e[0x40] = {0}; uint32_t state = 2;
    memcpy(e, &unit, 4); memcpy(e + 8, &state, 4);
    return LwCorpseSlotTick(&g_lwCorpse.slot[0], e, now);
}
void CoXlTestCorpseCounts(uint32_t* frozen, uint32_t* already, uint32_t* posed) { *frozen = g_lwCorpse.frozen; *already = g_lwCorpse.already; *posed = g_lwCorpse.posed; }
#endif
