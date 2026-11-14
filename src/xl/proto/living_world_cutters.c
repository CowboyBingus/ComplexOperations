/* The trees' navmesh cutters near players and squads (../co_cutset.h), record only: research switch
 * HELLFORGE_LW_CUTTERS=1[,CAP[,ADDS[,REMOVES]]], off by default. The game still adds every tree's cutter itself; the
 * set counts what the mod would switch on and off, so one run compares it with the "navmesh cutters:" line. */
#define LW_CUT_NOTE_MS 30000
static struct
{
    int on; uint32_t removes; ULONGLONG tickAt, notedAt;
    uint32_t ticks, adds, removed, shed, capped, waiting, over, players, last, squads, beside, left; double usSum, usWorst;
    CoCutHeld held[EVT_MAX_PLAYERS];
} g_lwCut;

static void LwCutInit(void)
{
    char t[64] = ""; unsigned cap = CO_CUTSET_CAP, adds = CO_CUTSET_ADDS, removes = CO_CUTSET_REMOVES;
    if (!GetEnvironmentVariableA("HELLFORGE_LW_CUTTERS", t, sizeof t) || t[0] == 0 || t[0] == '0') return;
    sscanf(t, "%*d%*[ ,]%u%*[ ,]%u%*[ ,]%u", &cap, &adds, &removes);
    if (cap > CO_CUTSET_SLOTS || adds > CO_CUTSET_ADDS || removes > CO_CUTSET_BATCH)
    { Note("living world: tree cutters: HELLFORGE_LW_CUTTERS=1[,CAP[,ADDS[,REMOVES]]]: cap up to %u, adds up to %u, removes up to %u a tick; not recorded", CO_CUTSET_SLOTS, CO_CUTSET_ADDS, CO_CUTSET_BATCH); return; }
    const char* why = co_cutters_record_arm(ApiModuleBase("helldivers2.exe", NULL), cap, adds);
    if (why != NULL) { Note("living world: tree cutters: %s; not recorded", why); return; }
    g_lwCut.on = 1; g_lwCut.removes = removes; g_lwCut.notedAt = GetTickCount64();
    Note("living world: tree cutters: record only on complex levels (the game adds every cutter as before): cap %u on, %u adds and %u removes a tick at most", cap, adds, removes);
}

static void LwCutNote(ULONGLONG now, const CoCuttersTick* t)
{
    if (now - g_lwCut.notedAt < LW_CUT_NOTE_MS || g_lwCut.ticks == 0) return;
    uint32_t used = 0, room = 0;
    int pool = co_cutters_pool(ApiModuleBase("helldivers2.exe", NULL), CO_CUTTERS_WORLD, &used, &room);
    Note("living world: tree cutters (record only): %u records (%u the game adds), %u would be on of cap %u; in %u ticks: %u would-add, %u would-remove (%u for room at the cap), the cap held on %u ticks, most waiting %u, most past a tick's adds %u; centres %u players (%u at the last place read) + %u squads (%u more inside a player's reach, %u left out past %u centres); tick mean %.0f us, worst %.0f us; pool %u of %u%s; AI world %p%s%s",
        t->records, t->wanted, t->on, t->cap, g_lwCut.ticks, g_lwCut.adds, g_lwCut.removed, g_lwCut.shed, g_lwCut.capped, g_lwCut.waiting, g_lwCut.over,
        g_lwCut.players, g_lwCut.last, g_lwCut.squads, g_lwCut.beside, g_lwCut.left, CO_CUTSET_CENTRES, g_lwCut.usSum / g_lwCut.ticks, g_lwCut.usWorst, used, room, pool ? "" : ", not read",
        (void*)t->world, t->bound ? "" : ", not bound", t->hooked ? "" : ", the make or the destroy is not hooked");
    Note("living world: tree cutters (record only): since the start: %llu trees' cutters made, %llu other cutters, %llu destroyed; %llu made with no AI world or pool, %llu at no place, %llu in a slot that still had a record, %llu destroys of a slot's older handle; %llu worlds or pools bound",
        (unsigned long long)t->trees, (unsigned long long)t->others, (unsigned long long)t->destroyed, (unsigned long long)t->unbound, (unsigned long long)t->refused,
        (unsigned long long)t->replaced, (unsigned long long)t->stale, (unsigned long long)t->binds);
    g_lwCut.ticks = g_lwCut.adds = g_lwCut.removed = g_lwCut.shed = g_lwCut.capped = g_lwCut.waiting = g_lwCut.over = 0;
    g_lwCut.usSum = g_lwCut.usWorst = 0; g_lwCut.notedAt = now;
}

/* Game thread, every peer, 4 Hz: a client has the AI world and the forest's cutters too (the make has no host
 * test). Centres: every player's Helldiver, or for 30 s the last place it read at (a dead player's surroundings
 * keep their cutters), and on the host the online squads that a player's reach does not already hold. */
static void LwCutFrame(int mission, int host)
{
    if (!g_lwCut.on || !mission) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_lwCut.tickAt < CO_CUTSET_TICK_MS) return;
    g_lwCut.tickAt = now;
    CoCutCentre c[CO_CUTSET_CENTRES]; uint32_t n = 0, last = 0, squads = 0, beside = 0, left = 0;
    EvpPlayer list[EVT_MAX_PLAYERS];
    int count = EvpPlayers(g_lw.game, list);
    for (int i = 0; i < count && i < EVT_MAX_PLAYERS; i++)
    {
        ActHealth h; float at[3] = {0, 0, 0}, use[2];
        const int reads = list[i].avatar && ActHealthOf(g_lw.game, list[i].avatar, &h) && h.unit && EvpUnitPosition(h.unit, at);
        const int kind = co_cutset_player(g_lwCut.held, EVT_MAX_PLAYERS, list[i].peer, reads, at[0], at[1], (uint32_t)now, use);
        if (kind == 0) continue;
        c[n++] = (CoCutCentre){use[0], use[1], CO_CUTSET_PLAYER_IN, CO_CUTSET_PLAYER_OUT};
        last += (uint32_t)(kind == 2);
    }
    const uint32_t players = n;
    for (int i = 0; host && i < g_lwWorld.count; i++)
    {
        if (!g_lwEvery[i] || g_lwSquads[i].state != LW_ONLINE) continue;
        const CoCutCentre q = {g_lwSquads[i].pos[0], g_lwSquads[i].pos[1], CO_CUTSET_SQUAD_IN, CO_CUTSET_SQUAD_OUT};
        if (co_cutset_covered(c, players, &q)) beside++;
        else if (n < CO_CUTSET_CENTRES) { c[n++] = q; squads++; }
        else left++;
    }
    CoCuttersTick t;
    if (!co_cutters_tick(c, n, (uint32_t)now, g_lwCut.removes, &t)) return;
    g_lwCut.ticks++; g_lwCut.adds += t.adds; g_lwCut.removed += t.removes; g_lwCut.shed += t.shed; g_lwCut.capped += (uint32_t)(t.capped != 0);
    if (t.waiting > g_lwCut.waiting) g_lwCut.waiting = t.waiting;
    if (t.over > g_lwCut.over) g_lwCut.over = t.over;
    g_lwCut.players = players; g_lwCut.last = last; g_lwCut.squads = squads; g_lwCut.beside = beside; g_lwCut.left = left;
    g_lwCut.usSum += t.us; if (t.us > g_lwCut.usWorst) g_lwCut.usWorst = t.us;
    LwCutNote(now, &t);
}

/* The mission's kind changed: the hooks record on a complex level only, and leaving one drops every record (the
 * engine destroys the trees' cutters itself). */
static void LwCutLive(int complex)
{
    uint32_t dropped = co_cutters_live(complex);
    memset(g_lwCut.held, 0, sizeof g_lwCut.held); /* a last place is of its own level */
    if (g_lwCut.on && !complex && dropped) Note("living world: tree cutters (record only): %u records dropped at the level's end", dropped);
}
