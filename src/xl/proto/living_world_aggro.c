/* Host only: loader squad members see for themselves, and a bounded log of enemies near a player that stay calm.
 * A member that joins its squad looks through the squad's detector (0x800A14 -> 0x843FD0), which lists every member's
 * eye. Each eye with the target out of view clears the shared entry's visible byte (0x886655..0x886666), and awareness
 * +0x40 grows only while it is set (0x886DA2; it decays at 0x886E64). A spread loader squad of 9-12 nearly always has
 * such an eye, so it stood 13-20 m from a Helldiver for 17 s. 0x843FD0(behaviour, unit, unit) is the game's own reset
 * to a unit's detector (world update; it turns that detector on and binds the unit's ears and danger sensors to it). */
typedef void (*LwUseDetectorFn)(uintptr_t behaviour, uint32_t unit, uint32_t detector);
static LwUseDetectorFn g_lwUseDetector; /* the tests' stand-in for 0x843FD0 */
#define LW_AGP_NEAR 25.0f   /* m from a player */
#define LW_AGP_CALM 3000u   /* ms near and below alert 3 before a unit is logged */
#define LW_AGP_UNITS 64
#define LW_AGP_LINES 24     /* unit lines a session */
#define LW_AGP_FIGHTING 6   /* of them, units already at alert 3 or more */

typedef struct { uint32_t unit; ULONGLONG since, seen; uint8_t logged; } LwAgpUnit;
static struct
{
    LwAgpUnit u[LW_AGP_UNITS];
    int lines, fighting;
    ULONGLONG nextAt, noteAt;
    uint32_t nearSum, calmSum, loaderSum, loaderFighting, scans, own; /* since the last note */
} g_lwAgp;

/* Members of an online loader squad (LwOnlineTick) that look through another detector go back to their own: only a
 * detector the host owns (index below +0x20), as the world update's reset checks it. Returns how many moved. */
static int LwOwnDetectors(const uint32_t* members, int n)
{
    uintptr_t beh = 0, det = 0; uint32_t di = 0; int moved = 0;
    if (!EvpPtr(g_lw.game + 0x3326740u, &beh) || !EvpPtr(g_lw.game + 0x3326548u, &det)) return 0;
    LwUseDetectorFn use = g_lwUseDetector ? g_lwUseDetector : (LwUseDetectorFn)(g_lw.game + 0x843FD0u);
    for (int k = 0; k < n; k++)
    {
        uint32_t now = LwAwareDetector(members[k]);
        if (now == 0 || now == members[k] || !EvpHash(det + 0x30, members[k], 8, 4, &di) || di >= EvpU32(det + 0x20, 0)) continue;
        use(beh, members[k], members[k]);
        moved++;
    }
    g_lwAgp.own += (uint32_t)moved;
    return moved;
}

/* The hostile or friendly entry for `target` in a detector record: 1 with its fields. */
static int LwAgpEntry(uintptr_t r, uint32_t target, int friendly, uint8_t* e)
{
    uint32_t count = EvpU32(r + (friendly ? 0x818u : 0x310u), 0);
    for (uint32_t i = 0; i < count && i < 16; i++)
    {
        uintptr_t at = r + (friendly ? 0x820u : 0x318u) + 0x50ull * i;
        if (EvpU32(at, 0) == target) return !SafeRead(at, e, 0x50);
    }
    return 0;
}

static void LwAgpLine(uint32_t unit, float d, int player)
{
    uintptr_t game = g_lw.game, det = 0, ents = 0, recs = 0, ent = 0, r = 0, ai = 0, aiRecs = 0, st = 0, clock = 0;
    uint32_t avatar = g_lwWorld.avatars[player], detE = LwAwareDetector(unit), di = 0, ii = 0, flags = 0xFFFFFFFFu, synced = 0xFFFFFFFFu;
    uint32_t u = 0; uint64_t res = EvpEntityResource(game, unit, &u), now = 0;
    if (EvpPtr(game + 0x3326348u, &clock)) SafeRead(clock + 0x18, &now, 8);
    if (EvpPtr(game + 0x33266C8u, &ai) && EvpHash(ai + 0x2330, unit, 8, 4, &ii) && EvpPtr(ai + 0x2350, &aiRecs))
    { flags = EvpU32(aiRecs + 0xA50ull * ii + 0xE4, 0xFFFFFFFFu); if (EvpPtr(ai + 0x2360, &st)) synced = EvpU32(st + 20ull * ii, 0xFFFFFFFFu); }
    int sq = -1; LwOnlineMember(unit, &sq);
    char dline[300] = "no awareness detector", hline[160] = "no hostile entry for the player", fline[40] = "";
    if (detE && EvpPtr(game + 0x3326548u, &det) && EvpHash(det + 0x30, detE, 8, 4, &di) && EvpPtr(det + 0x50, &recs) && EvpPtr(det + 0x48, &ents) &&
        EvpPtr(ents + 8ull * di, &ent))
    {
        r = recs + 0x13F8ull * di;
        uint64_t next = 0; SafeRead(r + 0x13D8, &next, 8);
        uintptr_t set = ((uintptr_t (*)(uintptr_t))(game + 0x510170u))(ent); /* the detector's settings (0x8877FC) */
        uint8_t always = 0xFF, skip = 0xFF; if (set) { SafeRead(set + 0x14, &always, 1); SafeRead(set + 0x15, &skip, 1); }
        uint8_t on = 0; SafeRead(r + 0x13F0, &on, 1);
        snprintf(dline, sizeof dline, "detector %08X (owned %s, entity +0x14 %X, %s): faction %X, lists %X, on %u, eyes %u, next scan %+.2f s, pending %u, hostile %u, friendly %u, sensors %u/%u/%u (+0, ears +0x104, +0x188); avatar_always_enemy %u, +0x15 %u",
            detE, di < EvpU32(det + 0x20, 0) ? "yes" : "NO", EvpU32(ent + 0x14, 0), detE == unit ? "its own" : "shared", EvpU32(r + 0x13E8, 0), EvpU32(r + 0x13EC, 0), on,
            EvpU32(r + 0x20C, 0), now && next ? ((double)(int64_t)(next - now)) / 1e6 : 0.0, EvpU32(r + 0x1368, 0), EvpU32(r + 0x310, 0), EvpU32(r + 0x818, 0),
            EvpU32(r, 0), EvpU32(r + 0x104, 0), EvpU32(r + 0x188, 0), always, skip);
        uint8_t e[0x50];
        if (LwAgpEntry(r, avatar, 0, e))
        {
            uint64_t seen; float hear, danger, aware; uint32_t ef, eyes; memcpy(&seen, e + 0x10, 8); memcpy(&hear, e + 0x38, 4); memcpy(&danger, e + 0x3C, 4);
            memcpy(&aware, e + 0x40, 4); memcpy(&ef, e + 0x48, 4); memcpy(&eyes, e + 0x4C, 4);
            snprintf(hline, sizeof hline, "hostile entry: flags %X, visible %u, awareness %.2f, eyes %X, seen %.1f s ago, hearing %.1f, danger %.1f",
                ef, e[0x30], (double)aware, eyes, seen && now >= seen ? (double)(now - seen) / 1e6 : -1.0, (double)hear, (double)danger);
        }
        if (LwAgpEntry(r, avatar, 1, e)) snprintf(fline, sizeof fline, "; the player is in its FRIENDLY list");
    }
    Note("living world: aggro probe: %s %08X (%s%d, squad entity %08X, resource %016llX) %.1f m from player %d (%08X): alert %u, AiEnemy flags %X, synced state %u; %s; %s%s",
        LwAlert(unit) >= 3 ? "fighting" : "calm", unit, sq >= 0 ? "squad " : "not a loader squad's ", sq, sq >= 0 ? g_lwSquads[sq].squad : 0, (unsigned long long)res, (double)d, player, avatar,
        LwAlert(unit), flags, synced, dline, hline, fline);
}

/* Twice a second: the AI units the host owns ([game+0x33266C8]: owned +0x2324, entity pointers +0x2348) near a player. */
static void LwAggroProbe(void)
{
    ULONGLONG now = GetTickCount64();
    if (now < g_lwAgp.nextAt || g_lwWorld.playersAt <= 0) return;
    g_lwAgp.nextAt = now + 500;
    uintptr_t ai = 0, ents = 0;
    if (!EvpPtr(g_lw.game + 0x33266C8u, &ai) || !EvpPtr(ai + 0x2348, &ents)) return;
    uint32_t owned = EvpU32(ai + 0x2324, 0), nearN = 0, calmN = 0, loaderN = 0, loaderFightN = 0;
    for (uint32_t i = 0; i < owned && i < 4096; i++)
    {
        uintptr_t ent = 0; uint32_t unit, handle = 0; float p[3];
        if (!EvpPtr(ents + 8ull * i, &ent) || (unit = EvpU32(ent + 8, 0)) == 0 || !EvpEntityResource(g_lw.game, unit, &handle) || !handle ||
            !EvpUnitPosition(handle, p)) continue;
        int player = -1; float best = LW_AGP_NEAR * LW_AGP_NEAR;
        for (int k = 0; k < g_lwWorld.playersAt; k++)
        {
            float dx = p[0] - g_lwWorld.players[k][0], dy = p[1] - g_lwWorld.players[k][1], dz = p[2] - g_lwWorld.players[k][2], d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) { best = d2; player = k; }
        }
        if (player < 0) continue;
        nearN++;
        int fighting = LwAlert(unit) >= 3, slot = -1, empty = -1, sq = -1;
        calmN += (uint32_t)!fighting;
        if (LwOnlineMember(unit, &sq) && sq >= 0) { loaderN++; loaderFightN += (uint32_t)fighting; }
        for (int k = 0; k < LW_AGP_UNITS && slot < 0; k++) { if (g_lwAgp.u[k].unit == unit) slot = k; else if (empty < 0 && now - g_lwAgp.u[k].seen > 5000) empty = k; }
        if (slot < 0) { if (empty < 0) continue; slot = empty; g_lwAgp.u[slot] = (LwAgpUnit){.unit = unit, .since = now}; }
        LwAgpUnit* a = &g_lwAgp.u[slot];
        if (fighting && a->logged != 2) a->since = now; /* calm time counts from its last fight */
        a->seen = now;
        if (g_lwAgp.lines >= LW_AGP_LINES || a->logged) continue;
        if (fighting ? g_lwAgp.fighting >= LW_AGP_FIGHTING : now - a->since < LW_AGP_CALM) continue;
        a->logged = fighting ? 2 : 1; g_lwAgp.lines++; g_lwAgp.fighting += fighting;
        LwAgpLine(unit, sqrtf(best), player);
    }
    g_lwAgp.nearSum += nearN; g_lwAgp.calmSum += calmN; g_lwAgp.loaderSum += loaderN; g_lwAgp.loaderFighting += loaderFightN; g_lwAgp.scans++;
    if (now - g_lwAgp.noteAt >= 30000)
    {
        double scans = g_lwAgp.scans ? (double)g_lwAgp.scans : 1.0;
        if (g_lwAgp.nearSum || g_lwAgp.own) Note("living world: aggro (30 s): %.1f enemies within %.0f m of a player on average, %.1f of them below alert 3; loader members %.1f, %.1f of them at alert 3+; %u members moved to their own detector; %d units logged so far",
            g_lwAgp.nearSum / scans, (double)LW_AGP_NEAR, g_lwAgp.calmSum / scans, g_lwAgp.loaderSum / scans, g_lwAgp.loaderFighting / scans, g_lwAgp.own, g_lwAgp.lines);
        g_lwAgp.nearSum = g_lwAgp.calmSum = g_lwAgp.loaderSum = g_lwAgp.loaderFighting = g_lwAgp.scans = g_lwAgp.own = 0; g_lwAgp.noteAt = now;
    }
}
