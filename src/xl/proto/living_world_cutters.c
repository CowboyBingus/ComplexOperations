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
    int on, mode, client; uint32_t removes; ULONGLONG tickAt, notedAt;
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
    g_lwCut.on = 1; g_lwCut.mode = (int)mode; g_lwCut.removes = removes; g_lwCut.notedAt = GetTickCount64();
    co_cutters_switch(mode >= 2);
    if (mode == 1) Note("living world: tree cutters: record only on complex levels (the game adds every cutter as before): cap %u on, %u adds and %u removes a tick at most", cap, adds, removes);
    else Note("living world: tree cutters: switching on complex levels (mode %u): the game makes each tree's cutter and the mod adds those in reach: cap %u on, %u adds and %u removes a tick at most; a client %s",
        mode, cap, adds, removes, g_lwCut.client == 0 ? "is left to the game" : g_lwCut.client == 1 ? "adds around its players" : "adds none");
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
    LwCutNote(now, &t);
}

/* The mission's kind changed: the hooks record on a complex level only, and leaving one drops every record (the
 * engine destroys the trees' cutters itself). */
static void LwCutLive(int complex)
{
    uint32_t dropped = co_cutters_live(complex);
    memset(g_lwCut.held, 0, sizeof g_lwCut.held); /* a last place is of its own level */
    memset(g_lwCutAsk, 0, sizeof g_lwCutAsk);
    if (g_lwCut.on && !complex && dropped) Note("living world: tree cutters: %u records dropped at the level's end", dropped);
}
