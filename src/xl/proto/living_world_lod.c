/* Data-only AI LOD for loader squads (KB living-world-density-design, C3). Hidden, calm members away from players are
 * marked dormant (entity +0x14 bit 0x2), which the detector, behaviour, targeting, weapon and status-effect passes skip,
 * and their detector scan timer is pushed ahead. Movement does not need the behaviour tick (S2), so patrols keep walking.
 * Off unless HELLFORGE_LW_LOD is 1 or 2: measurement 2 found the passes it skips under 0.1 ms a frame at 150 units. */

#define LW_LOD_NEAR 150.0f      /* within this of a player a member keeps the full update (D) */
#define LW_LOD_PERIOD 500u      /* ms between a squad's tier checks */
#define LW_LOD_DETECT_US 750000ull /* a dormant member's next scan at least this far ahead */
#define LW_LOD_MAX 1024
#define LW_LOD_REST_MS 1000u    /* a dormant member that moved under 0.3 m for this long is at rest */

static struct
{
    int on;                         /* -1 not read yet; 0 off, 1 dormant, 2 also steering off at rest (HELLFORGE_LW_LOD, `lod N`) */
    uint32_t ids[LW_LOD_MAX];       /* members this file made dormant */
    ULONGLONG kept[LW_LOD_MAX];     /* when each was last found dormant-tier in an online squad */
    ULONGLONG still[LW_LOD_MAX];    /* since when it has stood within 0.3 m of pos[] */
    float pos[LW_LOD_MAX][2];
    uint8_t steerOff[LW_LOD_MAX];   /* this file wrote its steering byte 0 */
    uint32_t frozen;
    int count;
    uint32_t full, mid, mismatch, already, gone; /* since the last note */
    ULONGLONG at[LW_SQUADS_MAX];    /* each squad's next tier check */
    uint8_t fullOf[LW_SQUADS_MAX];  /* members at the full update at its last check */
} g_lwAiLod = {.on = -1};

/* The member's entity (detector [game+0x3326548] +0x48) and detector record (+0x50, 0x13F8 B). The behaviour pass reads
 * the entity through its own array ([game+0x3326740] +0x58); before a bit is set both must give the same object. */
static int LwLodEntity(uint32_t unit, uintptr_t* entity, uintptr_t* detRec, int check)
{
    uintptr_t det = 0, ents = 0, recs = 0, beh = 0, behEnts = 0, other = 0; uint32_t di = 0, bi = 0;
    if (!EvpPtr(g_lw.game + 0x3326548u, &det) || !EvpHash(det + 0x30, unit, 8, 4, &di) || !EvpPtr(det + 0x48, &ents) ||
        !EvpPtr(ents + 8ull * di, entity) || !EvpPtr(det + 0x50, &recs)) return 0;
    *detRec = recs + 0x13F8ull * di;
    if (check && (!EvpPtr(g_lw.game + 0x3326740u, &beh) || !EvpHash(beh + 0x40, unit, 8, 4, &bi) || !EvpPtr(beh + 0x58, &behEnts) ||
        !EvpPtr(behEnts + 8ull * bi, &other) || other != *entity)) { g_lwAiLod.mismatch++; return 0; }
    return 1;
}

static int LwLodFind(uint32_t unit)
{
    for (int i = 0; i < g_lwAiLod.count; i++) if (g_lwAiLod.ids[i] == unit) return i;
    return -1;
}

/* The member's steering byte: 0x7DA099 skips a unit whose [[game+0x3326D18]+0x60] + 20 i + 4 is 0; 0x7DEAC8 sets it 1
 * at creation, and no other writer was found (KB living-world-density-design). The index is the AiCharacter hash's. */
static uintptr_t LwLodSteerByte(uint32_t unit)
{
    uintptr_t mgr = 0, recs = 0; uint32_t i = 0;
    if (!EvpPtr(g_lw.game + 0x3326D18u, &mgr) || !EvpHash(mgr + 0x30, unit, 8, 4, &i) || i >= EvpU32(mgr + 0x20, 0) || !EvpPtr(mgr + 0x60, &recs)) return 0;
    return recs + 20ull * i + 4;
}

static void LwLodSteer(int i, int on)
{
    uintptr_t b = LwLodSteerByte(g_lwAiLod.ids[i]);
    uint8_t v = on ? 1 : 0, cur = 2;
    g_lwAiLod.steerOff[i] = 0;
    if (b == 0 || SafeRead(b, &cur, 1) || cur == v || (on && cur != 0) || LwPoke(b, &v, 1)) return;
    g_lwAiLod.steerOff[i] = (uint8_t)!on;
}

/* A dormant member that has stood still since LW_LOD_REST_MS has its steering off; one that moves gets it back. */
static void LwLodRest(int i, uint32_t unit)
{
    ActHealth h; float p[3]; ULONGLONG now = GetTickCount64();
    if (!ActHealthOf(g_lw.game, unit, &h) || !h.unit || !EvpUnitPosition(h.unit, p)) return;
    float dx = p[0] - g_lwAiLod.pos[i][0], dy = p[1] - g_lwAiLod.pos[i][1];
    if (g_lwAiLod.still[i] == 0 || dx * dx + dy * dy > 0.09f)
    {
        g_lwAiLod.pos[i][0] = p[0]; g_lwAiLod.pos[i][1] = p[1]; g_lwAiLod.still[i] = now;
        if (g_lwAiLod.steerOff[i]) LwLodSteer(i, 1);
        return;
    }
    if (g_lwAiLod.on == 2 && !g_lwAiLod.steerOff[i] && now - g_lwAiLod.still[i] >= LW_LOD_REST_MS) LwLodSteer(i, 0);
    if (g_lwAiLod.steerOff[i]) g_lwAiLod.frozen++;
}

/* The game clock in microseconds ([[game+0x3326348]+0x18], as the detector compares it); 0 when unreadable. */
static uint64_t LwLodClock(void)
{
    uintptr_t t = 0; uint64_t clock = 0;
    return EvpPtr(g_lw.game + 0x3326348u, &t) && !SafeRead(t + 0x18, &clock, 8) ? clock : 0;
}

static void LwLodSleep(uint32_t unit, uint64_t clock)
{
    uintptr_t ent, rec; uint32_t flags = 0; uint64_t next = 0;
    if (!LwLodEntity(unit, &ent, &rec, 1) || SafeRead(ent + 0x14, &flags, 4)) return;
    int i = LwLodFind(unit);
    if (i < 0 && (flags & 2u)) { g_lwAiLod.already++; return; } /* dormant by the game: not ours to clear later */
    if (i < 0 && g_lwAiLod.count >= LW_LOD_MAX) return;
    if (!(flags & 2u)) { flags |= 2u; if (LwPoke(ent + 0x14, &flags, 4)) return; } /* also when the game cleared ours */
    if (i < 0) { i = g_lwAiLod.count++; g_lwAiLod.still[i] = 0; g_lwAiLod.steerOff[i] = 0; }
    g_lwAiLod.ids[i] = unit; g_lwAiLod.kept[i] = GetTickCount64();
    LwLodRest(i, unit);
    if (clock && !SafeRead(rec + 0x13D8, &next, 8) && next < clock + LW_LOD_DETECT_US)
    { next = clock + LW_LOD_DETECT_US; LwPoke(rec + 0x13D8, &next, 8); }
    g_lwAiLod.mid++;
}

/* Clears the bit and lets the detector scan at once. A unit that no longer resolves is dropped without a write. */
static void LwLodWake(int i, uint64_t clock)
{
    uint32_t unit = g_lwAiLod.ids[i], flags = 0;
    uintptr_t ent, rec;
    if (g_lwAiLod.steerOff[i]) LwLodSteer(i, 1);
    int last = --g_lwAiLod.count;
    g_lwAiLod.ids[i] = g_lwAiLod.ids[last]; g_lwAiLod.kept[i] = g_lwAiLod.kept[last]; g_lwAiLod.still[i] = g_lwAiLod.still[last];
    g_lwAiLod.steerOff[i] = g_lwAiLod.steerOff[last]; memcpy(g_lwAiLod.pos[i], g_lwAiLod.pos[last], sizeof g_lwAiLod.pos[i]);
    if (!LwLodEntity(unit, &ent, &rec, 0)) { g_lwAiLod.gone++; return; }
    if (!SafeRead(ent + 0x14, &flags, 4) && (flags & 2u)) { flags &= ~2u; LwPoke(ent + 0x14, &flags, 4); }
    if (clock) LwPoke(rec + 0x13D8, &clock, 8);
}

static int LwLodFull(LwSquad* q, const uint32_t* members, int n)
{
    if (q->intent == LW_INVESTIGATE || q->intent == LW_REINFORCE || q->inView) return 1;
    for (int i = 0; i < q->seenCount; i++)
        if (LwNearestPlayer2(q->seenAt[i][0], q->seenAt[i][1]) < LW_LOD_NEAR * LW_LOD_NEAR) return 1;
    for (int k = 0; k < n; k++) if (LwAlert(members[k]) != 0) return 1;
    if (q->seenCount == 0) return 1;
    float at[3] = {q->pos[0], q->pos[1], q->seenAt[0][2]};
    return LwSeenByAny(at, LwSquadBand(q));
}

/* A listed unit no online squad has kept dormant for 3 s (its squad went offline, or it left the squad alive) is woken;
 * one whose entity is gone leaves the list without a write. */
static void LwLodPurge(uint64_t clock)
{
    ULONGLONG now = GetTickCount64();
    for (int i = g_lwAiLod.count - 1; i >= 0; i--)
        if (now - g_lwAiLod.kept[i] > 3000) LwLodWake(i, clock);
}

static void LwLodWakeAll(void)
{
    uint64_t clock = LwLodClock();
    while (g_lwAiLod.count) LwLodWake(g_lwAiLod.count - 1, clock);
}

static void LwLodCmd(int on)
{
    g_lwAiLod.on = on < 0 ? 0 : on > 2 ? 2 : on;
    if (g_lwAiLod.on == 0) LwLodWakeAll();
    for (int i = 0; i < g_lwAiLod.count && g_lwAiLod.on == 1; i++) if (g_lwAiLod.steerOff[i]) LwLodSteer(i, 1);
    Note("living world: test: AI LOD %d (%s)", g_lwAiLod.on, g_lwAiLod.on == 0 ? "off, every member woken" : g_lwAiLod.on == 1 ? "dormant" : "dormant, steering off at rest");
}

static void LwLodSquad(LwSquad* q, uint64_t clock)
{
    uint32_t members[32];
    int n = LwMembers(q->squad, members), s = (int)(q - g_lwSquads);
    g_lwAiLod.fullOf[s] = 0;
    if (n <= 0) return;
    if (!LwLodFull(q, members, n)) { for (int k = 0; k < n; k++) LwLodSleep(members[k], clock); return; }
    g_lwAiLod.fullOf[s] = (uint8_t)n;
    for (int k = 0; k < n; k++) { int i = LwLodFind(members[k]); if (i >= 0) LwLodWake(i, clock); }
    g_lwAiLod.full += (uint32_t)n;
}

/* Once a tick, after the squads' own ticks: each online squad's tier every LW_LOD_PERIOD. */
static void LwLodTick(void)
{
    if (g_lwAiLod.on < 0) { char v[4] = ""; g_lwAiLod.on = GetEnvironmentVariableA("HELLFORGE_LW_LOD", v, sizeof v) > 0 && v[0] >= '0' && v[0] <= '2' ? v[0] - '0' : 0; }
    if (!g_lwAiLod.on || !LwDensityOn()) { if (g_lwAiLod.count) LwLodWakeAll(); return; }
    static ULONGLONG purgeAt;
    ULONGLONG now = GetTickCount64();
    uint64_t clock = LwLodClock();
    if (now >= purgeAt) { purgeAt = now + 1000; LwLodPurge(clock); }
    for (int s = 0; s < g_lwWorld.count && s < LW_SQUADS_MAX; s++)
    {
        if (g_lwSquads[s].state != LW_ONLINE || now < g_lwAiLod.at[s]) continue;
        g_lwAiLod.at[s] = now + LW_LOD_PERIOD + (ULONGLONG)(s % 10) * 10;
        LwLodSquad(&g_lwSquads[s], clock);
    }
}

/* Loader units at the full update now (C9 counts them against HELLFORGE_LW_FULL_MAX); with the LOD off, every one. */
static int LwLodFullUnits(void)
{
    int n = 0;
    for (int s = 0; s < g_lwWorld.count && s < LW_SQUADS_MAX; s++)
        if (g_lwSquads[s].state == LW_ONLINE) n += g_lwAiLod.on > 0 ? g_lwAiLod.fullOf[s] : g_lwSquads[s].alive;
    return n;
}

static void LwLodNote(void)
{
    if (g_lwAiLod.on < 1 || g_lwDensity.on != 1) return;
    int off = 0;
    for (int i = 0; i < g_lwAiLod.count; i++) off += g_lwAiLod.steerOff[i];
    Note("living world: AI LOD %d (30 s, member checks): %u full, %u dormant (%u at rest, steering off); now %d dormant, %d steering off; %u already dormant by the game, %u entity mismatches, %u gone",
        g_lwAiLod.on, g_lwAiLod.full, g_lwAiLod.mid, g_lwAiLod.frozen, g_lwAiLod.count, off, g_lwAiLod.already, g_lwAiLod.mismatch, g_lwAiLod.gone);
    g_lwAiLod.full = g_lwAiLod.mid = g_lwAiLod.frozen = g_lwAiLod.already = g_lwAiLod.mismatch = g_lwAiLod.gone = 0;
}
