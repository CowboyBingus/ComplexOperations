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
typedef struct { uint32_t unit; int have, done; float last[3]; ULONGLONG at; } LwCorpseSlot;
static struct
{
    int mode, ready, failed; LwCorpseHandlesFn handles; LwCorpseValidFn valid; LwCorpsePosFn pos; LwCorpseEnableFn enable; LwCorpseLevelFn level;
    LwCorpseSlot slot[LW_CORPSE_RING];
    uint32_t frozen, frozenNow, seen; ULONGLONG notedAt;
} g_lwCorpse = {.mode = 1};

/* The game's routines through its own tables, each checked against this build's address. */
static int LwCorpseBind(void)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL), t = 0, s = 0, f[5] = {0};
    if (SafeRead(g_lw.game + 0x3326310u, &t, 8) || t == 0 || SafeRead(t, &f[0], 8)) return 0;
    if (SafeRead(g_lw.game + 0x3326308u, &t, 8) || t == 0 || SafeRead(t + 0x18, &s, 8) || s == 0 || SafeRead(s + 0x720, &f[1], 8) || SafeRead(s + 0x88, &f[2], 8)) return 0;
    if (SafeRead(g_lw.game + 0x3326338u, &t, 8) || t == 0 || SafeRead(t + 0x08, &f[3], 8)) return 0;
    if (SafeRead(g_lw.game + 0x33263F8u, &t, 8) || t == 0 || SafeRead(t + 0x48, &f[4], 8)) return 0;
    if (f[0] != exe + 0x7E1C20u || f[1] != exe + 0x1FEEB0u || f[2] != exe + 0x1FD190u || f[3] != exe + 0x77F4F0u || f[4] != exe + 0x7A3D00u)
    { Note("living world: corpse freeze: the game's tables are not this build's; off"); g_lwCorpse.failed = 1; return 0; }
    g_lwCorpse.handles = (LwCorpseHandlesFn)f[0]; g_lwCorpse.valid = (LwCorpseValidFn)f[1]; g_lwCorpse.pos = (LwCorpsePosFn)f[2];
    g_lwCorpse.enable = (LwCorpseEnableFn)f[3]; g_lwCorpse.level = (LwCorpseLevelFn)f[4]; g_lwCorpse.ready = 1;
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

/* Vanilla's two calls, skipping the actors when the entry's byte +0x38 says so (dll 0x13BE9C7). The ragdoll level
 * request is counted (exe 0x7A3D00), so disposal's second request later is harmless. */
static int LwCorpseFreeze(uint32_t unit, uint8_t keepActors)
{
    uint8_t h[0x60] = {0}; uint64_t actors, ragdolls; uint32_t na, nr;
    if (g_lwCorpse.handles(unit, 5, h) <= 0) return 0; /* 5: actors and ragdolls; the getter writes up to +0x58 */
    memcpy(&actors, h, 8); memcpy(&na, h + 0x38, 4); memcpy(&ragdolls, h + 0x10, 8); memcpy(&nr, h + 0x40, 4);
    if (!keepActors && actors && na) g_lwCorpse.enable((const uint32_t*)(uintptr_t)actors, na, 0);
    if (ragdolls && nr) g_lwCorpse.level((const uint32_t*)(uintptr_t)ragdolls, nr, 3);
    return 1;
}

static void LwCorpseNote(ULONGLONG now)
{
    if (now - g_lwCorpse.notedAt < 30000) return;
    if (g_lwCorpse.mode == 2 || LwTestTraceOn())
        Note("living world: corpse freeze %d: %u ring corpses frozen now, %u frozen and %u new ring corpses in 30 s",
            g_lwCorpse.mode, g_lwCorpse.frozenNow, g_lwCorpse.frozen, g_lwCorpse.seen);
    g_lwCorpse.frozen = g_lwCorpse.seen = 0; g_lwCorpse.notedAt = now;
}

/* One ring entry: 1 when its corpse is frozen. */
static int LwCorpseSlotTick(LwCorpseSlot* s, const uint8_t* e, ULONGLONG now)
{
    uint32_t unit, state;
    memcpy(&unit, e, 4); memcpy(&state, e + 8, 4);
    if (unit == 0 || state != 2) { s->unit = 0; return 0; } /* 2: converted to a Corpse (0x13BE707) */
    if (s->unit != unit) { memset(s, 0, sizeof *s); s->unit = unit; g_lwCorpse.seen++; }
    if (s->done || (g_lwCorpse.mode == 2 && (unit & 1u))) return s->done;
    if (LwCorpseSettled(s, now) && LwCorpseFreeze(unit, e[0x38])) { s->done = 1; g_lwCorpse.frozen++; }
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
