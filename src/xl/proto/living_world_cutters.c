/* The trees' navmesh cutters near players and squads (../co_cutset.h): research switch
 * HELLFORGE_LW_CUTTERS=MODE[,CAP[,ADDS[,REMOVES]]], off by default. 1 records only: the game adds every cutter and the
 * set counts what the mod would do, to compare with the "navmesh cutters:" line. 2 holds the makes back and adds the
 * cutters in reach (no removes, no cap). 3 also removes and caps. A client follows HELLFORGE_LW_CUTTERS_CLIENT. */
#define LW_CUT_NOTE_MS 30000
/* A squad's request point is within 2 m of its place, its members' slots 5 m around the point and each up to 2.52 m
 * wide (LwPlace, LwMemberSlot): a tree whose hole could reach one of them is well inside this. */
#define LW_CUT_PLACE_M 32.0f
#define LW_CUT_WAIT_MS 5000 /* the longest a request waits for its trees' cutters; then it goes as it would have */
#define LW_CUT_ASK_MS 5000  /* a squad that asked this lately is a centre: a refused request asks again within 3 s */
enum { LW_CUT_WAITS, LW_CUT_IS_CUT, LW_CUT_GAMES, LW_CUT_LATE };
static struct
{
    int on, mode, client, body; uint32_t removes; ULONGLONG tickAt, notedAt;
    double holdSum, holdWorst;
    uint32_t ticks, adds, removed, shed, capped, waiting, over, players, last, squads, beside, left; double usSum, usWorst;
    CoCutHeld held[EVT_MAX_PLAYERS];
    uint32_t gate[4], gateLongest; /* requests by LW_CUT_ verdict since the start; the longest wait in ms */
} g_lwCut;
static struct { ULONGLONG first, last; } g_lwCutAsk[LW_SQUADS_MAX]; /* a squad's asks to come online: this wait's first, the last */

/* Squad `i` asks at `now`: how long it has waited, in ms. Asks over LW_CUT_ASK_MS apart are separate waits. */
static uint32_t LwCutAsked(int i, ULONGLONG now)
{
    if (g_lwCutAsk[i].last == 0 || now - g_lwCutAsk[i].last > LW_CUT_ASK_MS) g_lwCutAsk[i].first = now;
    g_lwCutAsk[i].last = now;
    return (uint32_t)(now - g_lwCutAsk[i].first);
}
/* What an ask gets, from co_cutters_cut's answer and the wait so far: only a place not cut in yet waits, and only
 * LW_CUT_WAIT_MS. */
static int LwCutVerdict(int cut, uint32_t waitedMs)
{
    return cut > 0 ? LW_CUT_IS_CUT : cut < 0 ? LW_CUT_GAMES : waitedMs > LW_CUT_WAIT_MS ? LW_CUT_LATE : LW_CUT_WAITS;
}
/* A squad is a centre while its units are in the world or on their way, and from its first ask to come online: its
 * place's cutters are then sent before the request that snaps its members onto the navmesh. */
static int LwCutSquad(int i, ULONGLONG now)
{
    const uint8_t state = g_lwSquads[i].state;
    return state == LW_ONLINE || state == LW_PENDING || (state == LW_OFFLINE && g_lwCutAsk[i].last != 0 && now - g_lwCutAsk[i].last <= LW_CUT_ASK_MS);
}
/* Host, before a squad's request is placed: 0 while the trees at its place wait for their cutters or for the navmesh
 * run that cuts them in. A point snapped onto the uncut navmesh can be inside a tree's hole when the cut lands. */
static int LwCutReady(const LwSquad* q)
{
    if (!g_lwCut.on || g_lwCut.mode < 2) return 1;
    uint32_t waiting = 0;
    const uint32_t waited = LwCutAsked((int)(q - g_lwSquads), GetTickCount64());
    const int verdict = LwCutVerdict(co_cutters_cut(q->pos[0], q->pos[1], LW_CUT_PLACE_M, &waiting), waited);
    g_lwCut.gate[verdict]++;
    if (verdict != LW_CUT_GAMES && verdict != LW_CUT_WAITS && waited > g_lwCut.gateLongest) g_lwCut.gateLongest = waited;
    return verdict != LW_CUT_WAITS;
}

/* The switch's text into v = {mode, cap, adds, removes}: 1 on, 0 off (empty or a leading 0), -1 not valid. */
static int LwCutParse(const char* t, unsigned* v)
{
    v[0] = 0; v[1] = CO_CUTSET_CAP; v[2] = CO_CUTSET_ADDS; v[3] = CO_CUTSET_REMOVES;
    if (t[0] == 0 || t[0] == '0') return 0;
    sscanf(t, "%u%*[ ,]%u%*[ ,]%u%*[ ,]%u", &v[0], &v[1], &v[2], &v[3]);
    if (v[0] < 1 || v[0] > 3 || v[1] > CO_CUTSET_SLOTS || v[2] > CO_CUTSET_ADDS || v[3] > CO_CUTSET_BATCH) return -1;
    if (v[0] == 2) { v[1] = CO_CUTSET_SLOTS; v[3] = 0; } /* adds only: a cutter left on is what the game does */
    return 1;
}
/* A client's part from HELLFORGE_LW_CUTTERS_CLIENT: 0 left to the game, 1 adds around its players, 2 adds none. */
static int LwCutClient(const char* t) { return t[0] >= '0' && t[0] <= '2' ? t[0] - '0' : 1; }

/* The tick's centres into `c` (room for CO_CUTSET_CENTRES); returns how many, the first out[0] of them players.
 * `peer` and `place` are the streamer's list. out = {players, at the last place, squads, beside a player, left out}.
 * The squads are the host's (LwCutSquad). */
static uint32_t LwCutCentres(const EvpPlayer* list, int count, const uint64_t* peer, float (*place)[2], int streamed, int host, int none, ULONGLONG now, CoCutCentre* c, uint32_t* out)
{
    uint32_t n = 0;
    out[1] = out[2] = out[3] = out[4] = 0;
    for (int i = 0; i < count && i < EVT_MAX_PLAYERS && !none; i++)
    {
        float at[2] = {0, 0}, use[2];
        int reads = 0;
        for (int p = 0; p < streamed && !reads; p++)
            if (peer[p] == list[i].peer) { at[0] = place[p][0]; at[1] = place[p][1]; reads = 1; }
        const int kind = co_cutset_player(g_lwCut.held, EVT_MAX_PLAYERS, list[i].peer, reads, at[0], at[1], (uint32_t)now, use);
        if (kind == 0) continue;
        c[n++] = (CoCutCentre){use[0], use[1], CO_CUTSET_PLAYER_IN, CO_CUTSET_PLAYER_OUT};
        out[1] += (uint32_t)(kind == 2);
    }
    out[0] = n;
    for (int i = 0; host && i < g_lwWorld.count; i++)
    {
        if (!LwCutSquad(i, now)) continue;
        const CoCutCentre q = {g_lwSquads[i].pos[0], g_lwSquads[i].pos[1], CO_CUTSET_SQUAD_IN, CO_CUTSET_SQUAD_OUT};
        if (co_cutset_covered(c, out[0], &q)) out[3]++;
        else if (n < CO_CUTSET_CENTRES) { c[n++] = q; out[2]++; }
        else out[4]++;
    }
    return n;
}

static void LwCutInit(void)
{
    char t[64] = "", cl[8] = ""; unsigned v[4];
    if (!GetEnvironmentVariableA("HELLFORGE_LW_CUTTERS", t, sizeof t)) return;
    const int valid = LwCutParse(t, v);
    if (valid == 0) return;
    if (valid < 0)
    { Note("living world: tree cutters: HELLFORGE_LW_CUTTERS=MODE[,CAP[,ADDS[,REMOVES]]]: mode 1 to 3, cap up to %u, adds up to %u, removes up to %u a tick; not recorded", CO_CUTSET_SLOTS, CO_CUTSET_ADDS, CO_CUTSET_BATCH); return; }
    const unsigned mode = v[0], cap = v[1], adds = v[2], removes = v[3];
    GetEnvironmentVariableA("HELLFORGE_LW_CUTTERS_CLIENT", cl, sizeof cl);
    g_lwCut.client = LwCutClient(cl);
    const char* why = co_cutters_record_arm(ApiModuleBase("helldivers2.exe", NULL), cap, adds);
    if (why != NULL) { Note("living world: tree cutters: %s; not recorded", why); return; }
    char body[8] = "";
    GetEnvironmentVariableA("HELLFORGE_LW_CUTTERS_BODY", body, sizeof body);
    g_lwCut.body = body[0] != '0';
    g_lwCut.on = 1; g_lwCut.mode = (int)mode; g_lwCut.removes = removes; g_lwCut.notedAt = GetTickCount64();
    co_cutters_switch(mode >= 2);
    if (mode == 1) Note("living world: tree cutters: record only on complex levels (the game adds every cutter as before): cap %u on, %u adds and %u removes a tick at most", cap, adds, removes);
    else Note("living world: tree cutters: switching on complex levels (mode %u): the game makes each tree's cutter and the mod adds those in reach: cap %u on, %u adds and %u removes a tick at most; a client %s",
        mode, cap, adds, removes, g_lwCut.client == 0 ? "is left to the game" : g_lwCut.client == 1 ? "adds around its players" : "adds none");
}

/* Launch measurements, HELLFORGE_LW_CUTTERS_BODY=0 turns them off: what a squad member placed beside a tree does
 * (a second apart for 20 s), and the navigators' repaths against the navmesh runs that take cutter changes. */
#define LW_CUT_BODY_TREE_M 6.0f   /* "beside a tree": a trunk's hole and the agent's radius are well inside this */
#define LW_CUT_BODY_ENROL_MS 5000 /* a squad's members appear within this of its coming online */
#define LW_CUT_BODY_S 20
#define LW_CUT_BODY_MAX 32
#define LW_CUT_BODY_LEVEL 96      /* members watched a level: 20 lines each */
#define LW_CUT_BODY_STUCK 5       /* seconds with a goal and no way made */
#define LW_CUT_NAVS 1024
typedef struct LwCutBody { uint32_t unit; int squad; ULONGLONG at; float spot[2]; uint8_t used, samples, still, busy, stuck; } LwCutBody;
static struct
{
    LwCutBody unit[LW_CUT_BODY_MAX]; uint32_t squadOf[LW_SQUADS_MAX]; ULONGLONG until[LW_SQUADS_MAX];
    uint32_t watched, stuck, gone;
    struct { uint32_t handle, count; } nav[LW_CUT_NAVS];
    uint32_t took, sinceRun, after, afterTicks, other, otherTicks, navs, unread;
} g_lwCutBody;

/* A navigator handle's place in its pool, as the engine's lookup 0x7DF8D0: `form` is the pool's +0x1C (the slot's
 * size, where its own handle is, where its navigator pointer is). 0 for a handle the pool does not hold. */
static int LwCutNavSlot(uint32_t handle, uint32_t count, uint32_t mask, uint32_t live, uint32_t form, uint32_t* key, uint32_t* at)
{
    const uint32_t i = handle & mask, base = (form & 0xFFFFu) * i;
    if (handle == 0xFFFFFFFFu || i >= count || (live & handle) == 0) return 0;
    *key = base + ((form >> 16) & 0xFFu); *at = base + (form >> 24);
    return 1;
}
static uintptr_t LwCutNavigator(uintptr_t exe, uint32_t handle)
{
    uintptr_t pool = 0, base = 0, nav = 0; uint32_t head[7], key = 0, at = 0, own = 0; /* the pool's +0x1C .. +0x34 */
    if (handle == 0xFFFFFFFFu || !EvpPtr(exe + 0x27C5A60u + 0x28ull * (handle >> 30), &pool) || !EvpPtr(pool, &base) || SafeRead(pool + 0x1C, head, sizeof head)) return 0;
    if (!LwCutNavSlot(handle, head[2], head[3], head[6], head[0], &key, &at) || SafeRead(base + key, &own, 4) || own != handle || !EvpPtr(base + at, &nav)) return 0;
    return nav;
}
/* An entity's navigation record ([game+0x3326418]: hash +0x30, its component at [+0x48][i] with the entity at +8,
 * records +0x50 of 0xA8 as 0x5397EA..0x53985A adds one); 0 with none. */
static uintptr_t LwCutNavRecord(uint32_t unit)
{
    uintptr_t mgr = 0, recs = 0, descs = 0, d = 0; uint32_t i = 0;
    if (!EvpPtr(g_lw.game + 0x3326418u, &mgr) || !EvpHash(mgr + 0x30, unit, 8, 4, &i) || !EvpPtr(mgr + 0x48, &descs) || !EvpPtr(descs + 8ull * i, &d) ||
        EvpU32(d + 8, 0) != unit || !EvpPtr(mgr + 0x50, &recs)) return 0;
    return recs + 0xA8ull * i;
}
/* The mover's last clip status of an entity (its 0xA4-byte work record's +0x60, which 0x59EA72 tests for 4). */
static uint32_t LwCutMotion(uint32_t unit)
{
    uintptr_t mgr = 0, descs = 0, d = 0, recs = 0; uint32_t i = 0, status = 0xFFFFFFFFu;
    if (!EvpPtr(g_lw.game + 0x3326558u, &mgr) || !EvpHash(mgr + 0x48A0, unit, 8, 4, &i) || !EvpPtr(mgr + 0x48B8, &descs) || !EvpPtr(descs + 8ull * i, &d) ||
        EvpU32(d + 8, 0) != unit || !EvpPtr(mgr + 0x48D0, &recs) || SafeRead(recs + 0xA4ull * i + 0x60, &status, 4)) return 0xFFFFFFFFu;
    return status;
}
/* One second's sample of a watched member: 1 when it has a goal and for LW_CUT_BODY_STUCK samples in a row either
 * stayed within 0.5 m of one spot or its navigator's state was not 0 (following its path). */
static int LwCutBodyStep(LwCutBody* b, const float* p, uint32_t goals, uint32_t state)
{
    const float dx = p[0] - b->spot[0], dy = p[1] - b->spot[1];
    if (dx * dx + dy * dy >= 0.5f * 0.5f) { b->spot[0] = p[0]; b->spot[1] = p[1]; b->still = 0; }
    else if (b->still < 255) b->still++;
    b->busy = state == 0 ? 0 : b->busy < 255 ? (uint8_t)(b->busy + 1) : 255;
    if (goals == 0) b->still = b->busy = 0;
    return b->still >= LW_CUT_BODY_STUCK || b->busy >= LW_CUT_BODY_STUCK;
}
/* Navigation record `i` holds navigator `handle` whose repath count reads `count`: the repaths since the last read
 * of the same navigator there (a record's slot is reused, so another handle starts afresh). */
static uint32_t LwCutRepathStep(uint32_t i, uint32_t handle, uint32_t count)
{
    const uint32_t d = g_lwCutBody.nav[i].handle == handle && count >= g_lwCutBody.nav[i].count ? count - g_lwCutBody.nav[i].count : 0;
    g_lwCutBody.nav[i].handle = handle; g_lwCutBody.nav[i].count = count;
    return d;
}
/* A tick's repaths (navigator +0x204, which 0xBDD8BC raises), counted apart for the second after a navmesh run
 * that began with changes: `took` is the count of such runs. */
static void LwCutRepaths(uintptr_t exe, uint32_t took)
{
    uintptr_t mgr = 0, recs = 0; uint32_t n = 0, sum = 0, read = 0, unread = 0;
    if (took != g_lwCutBody.took) { g_lwCutBody.took = took; g_lwCutBody.sinceRun = 0; }
    else if (g_lwCutBody.sinceRun < 1000) g_lwCutBody.sinceRun++;
    if (!EvpPtr(g_lw.game + 0x3326418u, &mgr) || SafeRead(mgr + 0x18, &n, 4) || !EvpPtr(mgr + 0x50, &recs)) return;
    if (n > LW_CUT_NAVS) { unread = n - LW_CUT_NAVS; n = LW_CUT_NAVS; }
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t handle = 0xFFFFFFFFu, count = 0; uintptr_t nav;
        if (SafeRead(recs + 0xA8ull * i + 8, &handle, 4) || handle == 0xFFFFFFFFu) continue; /* no navigator yet */
        if (!(nav = LwCutNavigator(exe, handle)) || SafeRead(nav + 0x204, &count, 4)) { unread++; continue; }
        sum += LwCutRepathStep(i, handle, count); read++;
    }
    g_lwCutBody.navs = read; g_lwCutBody.unread = unread;
    if (g_lwCutBody.sinceRun < 1000 / CO_CUTSET_TICK_MS) { g_lwCutBody.after += sum; g_lwCutBody.afterTicks++; }
    else { g_lwCutBody.other += sum; g_lwCutBody.otherTicks++; }
}
static void LwCutBodySample(uintptr_t exe, LwCutBody* b, ULONGLONG now)
{
    ActHealth h; float p[3], ray = NAN; int tile = 0, on = 0;
    if (!ActHealthOf(g_lw.game, b->unit, &h) || !h.unit || !EvpUnitPosition(h.unit, p))
    {
        Note("living world: tree cutters: body: unit %08X of squad %d is gone %u s after it was placed", b->unit, b->squad, (unsigned)b->samples);
        b->used = 0; g_lwCutBody.gone++;
        return;
    }
    const uintptr_t rec = LwCutNavRecord(b->unit);
    uint32_t state = 0xFFFFFFFFu, handle = 0xFFFFFFFFu, goals = 0, repaths = 0; uint8_t stale = 0xFF;
    if (rec) { SafeRead(rec + 8, &handle, 4); SafeRead(rec + 0xC, &state, 4); SafeRead(rec + 0x14, &stale, 1); }
    const uintptr_t nav = LwCutNavigator(exe, handle);
    if (nav) { SafeRead(nav + 0x28, &goals, 4); SafeRead(nav + 0x204, &repaths, 4); }
    if (b->samples == 0) { b->spot[0] = p[0]; b->spot[1] = p[1]; }
    const float from = sqrtf((p[0] - b->spot[0]) * (p[0] - b->spot[0]) + (p[1] - b->spot[1]) * (p[1] - b->spot[1]));
    const float tree = co_cutters_tree(p[0], p[1], LW_CUT_PLACE_M, &on), drawn = MoveDrawnGround(p[0], p[1], &tile);
    const int stuck = b->samples != 0 && nav && rec && LwCutBodyStep(b, p, goals, state);
    if (stuck && !b->stuck) { b->stuck = 1; g_lwCutBody.stuck++; }
    LwGround(p[0], p[1], &ray);
    Note("living world: tree cutters: body: unit %08X of squad %d, %u s: at (%.1f, %.1f, %.2f), %.2f over the ground ray, %.2f over the drawn ground; nearest tree %.1f m (cutter %s); %.2f m from its last spot; navigation state %u, stale %u, goals %u, repaths %u; motion status %u%s%s",
        b->unit, b->squad, (unsigned)b->samples, (double)p[0], (double)p[1], (double)p[2], (double)(p[2] - ray), (double)(p[2] - drawn), (double)tree, on ? "on" : "off",
        (double)from, state, (unsigned)stale, goals, repaths, LwCutMotion(b->unit), rec && nav ? "" : "; its navigation record or navigator did not read",
        stuck ? "; STUCK: a goal, and 5 s within 0.5 m of one spot or with a state other than 0" : "");
    b->at = now;
    if (++b->samples > LW_CUT_BODY_S) b->used = 0;
}
/* Host: the members of a squad that came online in the last 5 s and stand beside a recorded tree are watched. */
static void LwCutBodies(uintptr_t exe, ULONGLONG now)
{
    for (int i = 0; i < g_lwWorld.count; i++)
    {
        const LwSquad* q = &g_lwSquads[i];
        if (q->state != LW_ONLINE) { g_lwCutBody.squadOf[i] = 0; continue; }
        if (q->squad != g_lwCutBody.squadOf[i]) { g_lwCutBody.squadOf[i] = q->squad; g_lwCutBody.until[i] = now + LW_CUT_BODY_ENROL_MS; }
        if (now >= g_lwCutBody.until[i]) continue;
        for (int k = 0; k < q->seenCount && g_lwCutBody.watched < LW_CUT_BODY_LEVEL; k++)
        {
            int on = 0, slot = -1, known = 0;
            for (int u = 0; u < LW_CUT_BODY_MAX; u++)
                if (!g_lwCutBody.unit[u].used) { if (slot < 0) slot = u; }
                else if (g_lwCutBody.unit[u].unit == q->seen[k]) known = 1;
            if (known || slot < 0 || (q->seenAt[k][0] == 0.0f && q->seenAt[k][1] == 0.0f)) continue;
            if (co_cutters_tree(q->seenAt[k][0], q->seenAt[k][1], LW_CUT_BODY_TREE_M, &on) < 0.0f) continue;
            memset(&g_lwCutBody.unit[slot], 0, sizeof g_lwCutBody.unit[slot]);
            g_lwCutBody.unit[slot].unit = q->seen[k]; g_lwCutBody.unit[slot].squad = i; g_lwCutBody.unit[slot].used = 1;
            g_lwCutBody.watched++;
        }
    }
    for (int u = 0; u < LW_CUT_BODY_MAX; u++)
        if (g_lwCutBody.unit[u].used && now - g_lwCutBody.unit[u].at >= 1000) LwCutBodySample(exe, &g_lwCutBody.unit[u], now);
}

static void LwCutNote(ULONGLONG now, const CoCuttersTick* t)
{
    if (now - g_lwCut.notedAt < LW_CUT_NOTE_MS || g_lwCut.ticks == 0) return;
    uint32_t used = 0, room = 0;
    const char* mode = g_lwCut.mode == 1 ? "record only" : "switching";
    int pool = co_cutters_pool(ApiModuleBase("helldivers2.exe", NULL), CO_CUTTERS_WORLD, &used, &room);
    Note("living world: tree cutters (%s): %u records (%u the game adds), %u would be on of cap %u; in %u ticks: %u would-add, %u would-remove (%u for room at the cap), the cap held on %u ticks, most waiting %u, most past a tick's adds %u; centres %u players (%u at the last place read) + %u squads (%u more inside a player's reach, %u left out past %u centres); tick mean %.0f us, worst %.0f us; pool %u of %u%s; AI world %p%s%s",
        mode, t->records, t->wanted, t->on, t->cap, g_lwCut.ticks, g_lwCut.adds, g_lwCut.removed, g_lwCut.shed, g_lwCut.capped, g_lwCut.waiting, g_lwCut.over,
        g_lwCut.players, g_lwCut.last, g_lwCut.squads, g_lwCut.beside, g_lwCut.left, CO_CUTSET_CENTRES, g_lwCut.usSum / g_lwCut.ticks, g_lwCut.usWorst, used, room, pool ? "" : ", not read",
        (void*)t->world, t->bound ? "" : ", not bound", t->hooked ? "" : ", the make or the destroy is not hooked");
    Note("living world: tree cutters (%s): since the start: %llu trees' cutters made, %llu other cutters, %llu destroyed; %llu made with no AI world or pool, %llu at no place, %llu in a slot that still had a record, %llu destroys of a slot's older handle; %llu worlds or pools bound, %llu records dropped while they waited; off the tick's thread: %llu makes, %llu destroys on %u threads (before the first tick too)%s",
        mode, (unsigned long long)t->trees, (unsigned long long)t->others, (unsigned long long)t->destroyed, (unsigned long long)t->unbound, (unsigned long long)t->refused,
        (unsigned long long)t->replaced, (unsigned long long)t->stale, (unsigned long long)t->binds, (unsigned long long)t->stranded,
        (unsigned long long)t->makesOff, (unsigned long long)t->destroysOff, t->threadsOff, t->trees == 0 && t->others != 0 ? "; NO TREE RECOGNISED (another mod's hook outside this one?): every cutter is the game's" : "");
    if (g_lwCut.mode >= 2)
        Note("living world: tree cutters (switching): since the start: %llu makes held back, %llu added by the game with no tick in the last second, %llu added at once (not recorded), %llu not added (the AI world or the pool had gone); %llu switched on, %llu switched off; %llu ticks skipped with the AI step in flight; the set's lock held mean %.0f us, worst %.0f us a tick; %u navmesh runs began with changes; squads' requests: %u placed with their trees cut in (longest wait %u ms), %u where the cutters are the game's, %u let through uncut after %u ms, %u asks made to wait",
            (unsigned long long)t->held, (unsigned long long)t->stock, (unsigned long long)t->atOnce, (unsigned long long)t->noWorld, (unsigned long long)t->sentOn, (unsigned long long)t->sentOff, (unsigned long long)t->skips, g_lwCut.holdSum / g_lwCut.ticks, g_lwCut.holdWorst,
            t->took, g_lwCut.gate[LW_CUT_IS_CUT], g_lwCut.gateLongest, g_lwCut.gate[LW_CUT_GAMES], g_lwCut.gate[LW_CUT_LATE], LW_CUT_WAIT_MS, g_lwCut.gate[LW_CUT_WAITS]);
    if (g_lwCut.body)
        Note("living world: tree cutters: bodies: %u squad members watched beside a tree since the level's start (%u gone while watched, %u STUCK); navigators' repaths: %u in %u ticks within a second after a navmesh run that began with changes, %u in %u other ticks; %u navigators read at the last tick, %u not",
            g_lwCutBody.watched, g_lwCutBody.gone, g_lwCutBody.stuck, g_lwCutBody.after, g_lwCutBody.afterTicks, g_lwCutBody.other, g_lwCutBody.otherTicks, g_lwCutBody.navs, g_lwCutBody.unread);
    g_lwCutBody.after = g_lwCutBody.afterTicks = g_lwCutBody.other = g_lwCutBody.otherTicks = 0;
    g_lwCut.ticks = g_lwCut.adds = g_lwCut.removed = g_lwCut.shed = g_lwCut.capped = g_lwCut.waiting = g_lwCut.over = 0;
    g_lwCut.usSum = g_lwCut.usWorst = g_lwCut.holdSum = g_lwCut.holdWorst = 0; g_lwCut.notedAt = now;
}

/* Game thread, every peer, 4 Hz: a client has the AI world and the forest's cutters too (the make has no host
 * test). Centres: every player's Helldiver, or for 30 s the last place it read at (a dead player's surroundings
 * keep their cutters), and on the host the squads (LwCutSquad) that a player's reach does not already hold. */
static void LwCutFrame(int mission, int host)
{
    if (!g_lwCut.on || !mission) return;
    if (!host && g_lwCut.mode >= 2 && g_lwCut.client == 0) return; /* no tick: this client's makes are the game's */
    ULONGLONG now = GetTickCount64();
    if (now - g_lwCut.tickAt < CO_CUTSET_TICK_MS) return;
    CoCutCentre c[CO_CUTSET_CENTRES]; uint32_t counts[5];
    EvpPlayer list[EVT_MAX_PLAYERS];
    int count = EvpPlayers(g_lw.game, list);
    /* The places the streamer streams trees around (a hellpod's flight held still, co_settle.h), so a tree streamed
     * in for a player is in that player's reach here. A player the streamer does not have keeps the last place.
     * Copied under the streamer's lock: a generator thread's capture empties the list, and a list read half
     * written would pair a peer with another player's place. A capture can hold the lock for long, so a busy
     * lock leaves the tick to the next frame. */
    uint64_t peer[EVT_MAX_PLAYERS]; float place[EVT_MAX_PLAYERS][2];
    if (!TryAcquireSRWLockShared(&g_streamLock)) return;
    int streamed = (int)g_stream.playerCount - (g_streamObserverOn ? 1 : 0);
    if (streamed > EVT_MAX_PLAYERS) streamed = EVT_MAX_PLAYERS;
    for (int p = 0; p < streamed; p++) { peer[p] = g_streamPlayerPeer[p]; place[p][0] = g_streamPlayerAt[p][0]; place[p][1] = g_streamPlayerAt[p][1]; }
    ReleaseSRWLockShared(&g_streamLock);
    g_lwCut.tickAt = now;
    const int none = !host && g_lwCut.mode >= 2 && g_lwCut.client == 2; /* a client that adds none: no centre */
    const uint32_t n = LwCutCentres(list, count, peer, place, streamed, host, none, now, c, counts);
    CoCuttersTick t;
    if (!co_cutters_tick(c, n, (uint32_t)now, g_lwCut.removes, &t)) return;
    g_lwCut.ticks++; g_lwCut.adds += t.adds; g_lwCut.removed += t.removes; g_lwCut.shed += t.shed; g_lwCut.capped += (uint32_t)(t.capped != 0);
    if (t.waiting > g_lwCut.waiting) g_lwCut.waiting = t.waiting;
    if (t.over > g_lwCut.over) g_lwCut.over = t.over;
    g_lwCut.players = counts[0]; g_lwCut.last = counts[1]; g_lwCut.squads = counts[2]; g_lwCut.beside = counts[3]; g_lwCut.left = counts[4];
    g_lwCut.usSum += t.us; if (t.us > g_lwCut.usWorst) g_lwCut.usWorst = t.us;
    g_lwCut.holdSum += t.holdUs; if (t.holdUs > g_lwCut.holdWorst) g_lwCut.holdWorst = t.holdUs;
    if (g_lwCut.body)
    {
        const uintptr_t exe = ApiModuleBase("helldivers2.exe", NULL);
        LwCutRepaths(exe, t.took);
        if (host) LwCutBodies(exe, now);
    }
    LwCutNote(now, &t);
}

/* The mission's kind changed: the hooks record on a complex level only, and leaving one drops every record (the
 * engine destroys the trees' cutters itself). */
static void LwCutLive(int complex)
{
    uint32_t dropped = co_cutters_live(complex);
    memset(g_lwCut.held, 0, sizeof g_lwCut.held); /* a last place is of its own level */
    memset(g_lwCutAsk, 0, sizeof g_lwCutAsk);
    memset(&g_lwCutBody, 0, sizeof g_lwCutBody);
    if (g_lwCut.on && !complex && dropped) Note("living world: tree cutters: %u records dropped at the level's end", dropped);
}
