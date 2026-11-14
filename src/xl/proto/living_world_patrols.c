/* Patrols a player can see (included by living_world.c after the density rules). The squads came online unseen and
 * nothing brought them into view: they walked a cross-country grid while the roads, the open corridors, stood empty;
 * a squad meant to be seen at distance kept its place in the far share while it stood still or after the player had
 * walked up to it; and the far band reached past what an unzoomed view draws. Here: road nodes along the generator's
 * road points and a share of squads that keep to them, the far share's two rules, the band from the views in play, the
 * spawn band looking ahead of a vehicle, room for a squad about to walk into view, and a line per player every 30 s. */
#define LW_ROAD_SHARE_OF 10       /* one squad in this many is a road patrol (D: one about every 360 m of a 294 km network) */
#define LW_ROAD_NODES_MIN 500
#define LW_ROAD_OFF_COST 4.0f     /* a road patrol's path counts a stretch off the roads this many times */
#define LW_ROAD_END 100.0f        /* m: a road's first and last point lie this near its edge's nodes, or it is left alone */
#define LW_ROAD_NEAR 30.0f        /* m to a road point: "by a road" in the notes */
#define LW_PATROL_STEP_MS 125     /* an eighth of the online squads is read each step: every squad once a second */
#define LW_PATROL_SWAP_MS 10000   /* at most one filler squad gives way in this time */
#define LW_PATROL_NOTES 60        /* event lines a mission (the 30 s lines are apart) */

static CoLwView g_lwView;
static CoLwRoads g_lwRoads;
static struct { ULONGLONG stepAt, noteAt, swapAt, bandAt; uint32_t phase, notes, gaveWay, stoodOff; LONG mission; } g_lwPatrols;

static float LwRoadCost(int a, int b)
{
    return g_lwPathRoad && g_lwNodeKind[a] != 2 && g_lwNodeKind[b] != 2 ? LW_ROAD_OFF_COST : 1.0f;
}
/* Too few road nodes (a level whose roads have no points) and the road patrols would crowd them: none then. */
static int LwRoadSquad(int index) { return g_lwRoadNodes >= LW_ROAD_NODES_MIN && index % LW_ROAD_SHARE_OF == 0; }

static void LwUnlink(int a, int b)
{
    LwNode* n = &g_lwNodes[a];
    for (int k = 0; k < n->degree; k++) if (n->next[k] == b) { n->next[k] = n->next[--n->degree]; return; }
}

/* The road nodes. An edge (0x48 bytes, G + g_routeEdges, count G+0x11A71CC) holds its end nodes at +0x10 / +0x18 (as
 * LwCopyRoutes reads them), its type at +0x34, its points' range at +0x38 / +0x3C and its flags at +0x44; the points
 * (x, y in the nodes' own metres, 8 bytes each, G + g_routePoints, count G+0x11A71D4) are written by op 0x0E for an
 * edge of a type other than 2 and 8 without flag 2. Which end a range starts at is not assumed: the nearer one. */
static void LwRoadRoutes(uintptr_t lg)
{
    uintptr_t G = GenerationBlock(g_lw.game);
    uint32_t edges = 0, points = 0, levelNodes = 0; float size = 0;
    memset(g_lwNodeKind, 0, sizeof g_lwNodeKind); g_lwRoadNodes = 0; g_lwRoads.count = 0;
    if (g_lwWorld.radius < 1500.0f) return; /* a vanilla disc: the level's routes are its roads already */
    if (G == 0 || SafeRead(G + 0x11A71CCu, &edges, 4) || SafeRead(G + 0x11A71D4u, &points, 4) || SafeRead(G + 0x11A71B4u, &levelNodes, 4) ||
        SafeRead(lg + 0x1E0, &size, 4) || edges == 0 || edges > 65536 || points < 2 || points > CO_LW_ROADS_POINTS || (int)levelNodes > g_lwNodeCount)
    { Note("living world: roads: no road points to read (edges %u, points %u); the squads keep to the routes and the grid", edges, points); return; }
    const float half = size * 0.5f;
    for (uint32_t i = 0; i < points; i++)
    {
        float p[2];
        if (SafeRead(G + g_routePoints + 8ull * i, p, 8)) { g_lwRoads.count = 0; Note("living world: roads: point %u does not read", i); return; }
        g_lwRoads.pts[i][0] = p[0] - half; g_lwRoads.pts[i][1] = p[1] - half;
    }
    co_lw_roads_index(&g_lwRoads, points, -half);
    const uintptr_t nodes = G + g_routeNodes;
    uint32_t roads = 0, noPoints = 0, off = 0, full = 0; double km = 0;
    for (uint32_t e = 0; e < edges; e++)
    {
        uint8_t rec[0x48]; uint64_t ends[4]; uint32_t range[2]; uint16_t flags;
        if (SafeRead(G + g_routeEdges + 0x48ull * e, rec, sizeof rec)) break;
        memcpy(ends, rec + 0x10, 32); memcpy(range, rec + 0x38, 8); memcpy(&flags, rec + 0x44, 2);
        if (rec[0x34] == 2 || rec[0x34] == 8 || (flags & 2)) continue;
        if (range[1] > points || range[1] < range[0] + 2) { noPoints++; continue; }
        if (ends[0] < nodes || ends[1] < nodes || (ends[0] - nodes) % 0x98u || (ends[1] - nodes) % 0x98u ||
            (ends[0] - nodes) / 0x98u >= levelNodes || (ends[1] - nodes) / 0x98u >= levelNodes || ends[0] == ends[1] || ends[2] == 0 || ends[3] == 0) { off++; continue; } /* cells +0x20 / +0x28: the copy links no edge without both */
        int a = (int)((ends[0] - nodes) / 0x98u), b = (int)((ends[1] - nodes) / 0x98u);
        const float* first = g_lwRoads.pts[range[0]]; const float* last = g_lwRoads.pts[range[1] - 1];
        if (LwDist2(first, g_lwNodes[a].x, g_lwNodes[a].y) > LwDist2(first, g_lwNodes[b].x, g_lwNodes[b].y)) { int t = a; a = b; b = t; }
        if (LwDist2(first, g_lwNodes[a].x, g_lwNodes[a].y) > LW_ROAD_END * LW_ROAD_END || LwDist2(last, g_lwNodes[b].x, g_lwNodes[b].y) > LW_ROAD_END * LW_ROAD_END) { off++; continue; }
        uint32_t cut[256];
        uint32_t n = co_lw_roads_cut((const float (*)[2])g_lwRoads.pts, range[0], range[1], CO_LW_ROADS_STRETCH, cut, 256);
        if (n == 0) continue; /* a short road: its chord is the road */
        if (g_lwNodeCount + (int)n > LW_NODES_MAX || g_lwNodes[a].degree >= 16 || g_lwNodes[b].degree >= 16) { full++; continue; }
        LwUnlink(a, b); LwUnlink(b, a);
        int prev = a;
        for (uint32_t k = 0; k < n; k++)
        {
            LwNode* o = &g_lwNodes[g_lwNodeCount]; memset(o, 0, sizeof *o);
            o->x = g_lwRoads.pts[cut[k]][0]; o->y = g_lwRoads.pts[cut[k]][1];
            g_lwNodeKind[g_lwNodeCount] = 2;
            LwLink(prev, g_lwNodeCount); prev = g_lwNodeCount++; g_lwRoadNodes++;
        }
        LwLink(prev, b);
        g_lwNodeKind[a] = g_lwNodeKind[a] ? g_lwNodeKind[a] : 1; g_lwNodeKind[b] = g_lwNodeKind[b] ? g_lwNodeKind[b] : 1;
        roads++;
        for (uint32_t i = range[0] + 1; i < range[1]; i++) km += sqrt((double)LwDist2(g_lwRoads.pts[i], g_lwRoads.pts[i - 1][0], g_lwRoads.pts[i - 1][1])) / 1000.0;
    }
    Note("living world: roads: %u roads of %u edges walked along their points (%.0f km, %d nodes about %.0f m apart; %u points); left as they were: "
        "%u without points, %u whose points or nodes do not fit, %u for room; one squad in %d keeps to roads", roads, edges, km, g_lwRoadNodes,
        (double)CO_LW_ROADS_STRETCH, points, noPoints, off, full, LW_ROAD_SHARE_OF);
}

/* A squad's place in the far share (co_lw_view_far): by sightline, still beyond R_on, and walking. */
static int LwPatrolFar(const LwSquad* q)
{
    if (!q->farOn) return 0;
    if (q->state != LW_ONLINE) return 1; /* requested: on its way in */
    return co_lw_view_far(1, LwNearestPlayer2(q->pos[0], q->pos[1]), LW_R_ON, co_lw_view_stood(&g_lwView, (uint32_t)(q - g_lwSquads), GetTickCount64()));
}

/* The band test's distance for an offline squad: the nearest player's, or the band of the point a vehicle will reach. */
static float LwLeadNear2(const float* pos)
{
    float near2 = LwNearestPlayer2(pos[0], pos[1]), lead2 = 3.0e38f, rMin = g_lwWorld.rMin > 0 ? g_lwWorld.rMin : LW_R_MIN;
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_VIEW_PLAYERS; i++)
    {
        float lead[2];
        if (!co_lw_view_lead(&g_lwView, (uint32_t)i, g_lwWorld.players[i], lead)) continue;
        float d = LwDist2(lead, pos[0], pos[1]);
        if (d < lead2) lead2 = d;
    }
    return co_lw_view_band2(near2, lead2, rMin, LW_R_ON);
}

/* The far band follows the views in play: the host's own angle as its camera has it now (a scope narrows it, and the
 * band grows while it does), and for peers, whose angle is not known here, the narrowest unzoomed setting (45 deg). */
static void LwBandTick(ULONGLONG now)
{
    if (now - g_lwPatrols.bandAt < 250) return;
    g_lwPatrols.bandAt = now;
    const float deg = 3.14159265f / 180.0f;
    float f = 45.0f * deg;
    if (LwViewRead() >= 0 && g_lwViewFov > 0) { f = g_lwViewFov; if (g_lwWorld.playersAt > 1 && f > 45.0f * deg) f = 45.0f * deg; }
    if (f < 11.25f * deg) f = 11.25f * deg;
    if (f > 90.0f * deg) f = 90.0f * deg;
    if (fabsf(f - g_lwBandFov) < 1.0f * deg) return;
    g_lwBandFov = f;
    for (int k = 0; k < LW_RADII; k++) if (g_lwRadii[k].definition) g_lwRadii[k].band = LwFarBand(g_lwRadii[k].radius);
    g_lwRadiiGen++;
}

static int LwPatrolNote(void) { return g_lwPatrols.notes < LW_PATROL_NOTES ? (int)++g_lwPatrols.notes : 0; }

/* One online squad's sample: where its leader stands, who has a clear line to it, whether a member fights. */
static void LwPatrolSample(LwSquad* q, ULONGLONG now)
{
    CoLwViewSample s; memset(&s, 0, sizeof s);
    const int idx = (int)(q - g_lwSquads);
    s.index = (uint32_t)idx; s.units = q->alive; s.nowMs = now; s.pos[0] = q->pos[0]; s.pos[1] = q->pos[1];
    s.road = (uint8_t)co_lw_roads_near(&g_lwRoads, q->pos[0], q->pos[1], LW_ROAD_NEAR);
    s.sightline = q->farOn; s.patrol = q->intent == LW_PATROL;
    int calm = 1;
    for (int k = 0; k < q->seenCount; k++) { uint32_t alert = LwAlert(q->seen[k]); if (alert) calm = 0; if (alert >= 3) s.fighting = 1; }
    s.players = (uint8_t)(g_lwWorld.playersAt < CO_LW_VIEW_PLAYERS ? g_lwWorld.playersAt : CO_LW_VIEW_PLAYERS);
    float chest[3] = {q->pos[0], q->pos[1], 0}, band = LwSquadBand(q);
    int placed = q->seenCount > 0 && isfinite(q->seenAt[0][2]);
    if (placed) chest[2] = q->seenAt[0][2] + 1.0f; /* the first listed member's height: no ground ray for it */
    for (int i = 0; i < s.players && placed; i++)
    {
        float d2 = LwDist2(g_lwWorld.players[i], q->pos[0], q->pos[1]);
        if (d2 > band * band || LwSeenFrom(g_lwWorld.players[i], g_lwWorld.units[i], chest) != 1) continue;
        s.seen[i] = d2 <= CO_LW_VIEW_NEAR * CO_LW_VIEW_NEAR ? CO_LW_VIEW_SEEN_NEAR : CO_LW_VIEW_SEEN_FAR;
    }
    co_lw_view_sample(&g_lwView, &s);
    /* a far squad that stands out of every player's sight holds a place nobody sees filled: offline, as a swap takes it */
    if (q->farOn && s.patrol && calm && !q->forceOff && co_lw_view_stood(&g_lwView, s.index, now) >= 2 * CO_LW_VIEW_STAND_MS)
    {
        float at[3] = {q->pos[0], q->pos[1], placed ? q->seenAt[0][2] : 0};
        if (placed && LwHidden(at))
        {
            q->forceOff = 2; g_lwPatrols.stoodOff++;
            if (LwPatrolNote()) Note("living world: squad %d came online by sightline and stood %.0f s out of every player's sight, %.0f m from the nearest: offline",
                idx, (double)co_lw_view_stood(&g_lwView, s.index, now) / 1000.0, (double)sqrtf(LwNearestPlayer2(q->pos[0], q->pos[1])));
        }
    }
}

/* Room for a squad about to walk into view. A squad in the spawn band whose way ahead a player sees, refused only
 * because its bubble is full, takes the place of the farthest calm squad nobody sees (LwFillerPick, as a far squad in
 * sight does): the room is held for it, and it still comes online at its own hidden point. */
static void LwPatrolGiveWay(ULONGLONG now)
{
    if (now - g_lwPatrols.swapAt < LW_PATROL_SWAP_MS || g_lwDensity.on != 1) return;
    if (g_lwHold.idx >= 0 && now < g_lwHold.until) return; /* a held squad is being served */
    const float rMin = g_lwWorld.rMin > 0 ? g_lwWorld.rMin : LW_R_MIN;
    int tries = 0;
    for (int i = 0; i < g_lwWorld.count && tries < 3; i++)
    {
        LwSquad* q = &g_lwSquads[i];
        if (q->state != LW_OFFLINE || q->intent != LW_PATROL || q->held || q->pulled || q->survivors == 0 || (q->retry && now < q->retry)) continue;
        float near2 = LwNearestPlayer2(q->pos[0], q->pos[1]);
        if (near2 < rMin * rMin || near2 >= LW_R_ON * LW_R_ON) continue;
        int b = LwBubbleOf(q->pos[0], q->pos[1]);
        if (b < 0 || g_lwDensity.units[b] + (int)q->survivors <= g_lwDensity.budget[b]) continue; /* room: it asks for itself */
        if (LwRecentNear(q->pos)) continue;
        tries++;
        if (!LwAboutToBeSeen(q)) continue;
        float pickD;
        LwSquad* pick = LwFillerPick(q, &pickD);
        g_lwPatrols.swapAt = now;
        if (pick == NULL) return;
        pick->forceOff = 2; g_lwPatrols.gaveWay++;
        g_lwHold.idx = i; g_lwHold.units = q->survivors; g_lwHold.until = now + 20000;
        if (LwPatrolNote()) Note("living world: squad %d (hidden, calm, %.0f m from the nearest player) goes offline to make room for squad %d, %.0f m away, whose way ahead a player sees",
            (int)(pick - g_lwSquads), (double)sqrtf(pickD), i, (double)sqrtf(near2));
        return;
    }
    g_lwPatrols.swapAt = now - LW_PATROL_SWAP_MS + 2000; /* looked and found none: again in 2 s */
}

static void LwPatrolsNotes(ULONGLONG now)
{
    char line[512];
    int farNow = 0, roadOnline = 0, led = 0;
    for (int i = 0; i < g_lwWorld.count; i++)
    {
        const LwSquad* q = &g_lwSquads[i];
        if (q->state != LW_ONLINE) continue;
        if (LwPatrolFar(q)) farNow += q->alive;
        if (LwRoadSquad(i)) roadOnline++;
    }
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_VIEW_PLAYERS; i++) { float lead[2]; led += co_lw_view_lead(&g_lwView, (uint32_t)i, g_lwWorld.players[i], lead); }
    if (co_lw_view_note(&g_lwView, now, line, sizeof line))
        Note("living world: %s; far share %d of %.0f units; road squads online %d; %u gave way to a squad walking into view, %u stood unseen and left; "
            "band for a 2 m unit %.0f m (%.0f deg view); %d players' bands look ahead", line, farNow, (double)LwFarShareLimit(), roadOnline, g_lwPatrols.gaveWay,
            g_lwPatrols.stoodOff, (double)LwFarBand(2.0f), (double)(g_lwBandFov * 57.29578f), led);
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_VIEW_PLAYERS; i++)
        if (co_lw_view_player_note(&g_lwView, (uint32_t)i, now, line, sizeof line)) Note("living world: %s", line);
    co_lw_view_restart(&g_lwView, now);
    g_lwPatrols.gaveWay = g_lwPatrols.stoodOff = 0;
}

static void LwPatrolsTick(void)
{
    ULONGLONG now = GetTickCount64();
    if (g_lwPatrols.mission != g_lwMission) { memset(&g_lwPatrols, 0, sizeof g_lwPatrols); g_lwPatrols.mission = g_lwMission; g_lwPatrols.noteAt = now; }
    co_lw_view_level(&g_lwView, g_lwMission, now);
    LwBandTick(now);
    if (now - g_lwPatrols.stepAt < LW_PATROL_STEP_MS) return;
    g_lwPatrols.stepAt = now;
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_VIEW_PLAYERS; i++) co_lw_view_player(&g_lwView, (uint32_t)i, g_lwWorld.players[i], now);
    const uint32_t phase = g_lwPatrols.phase++ % 8u;
    for (int i = (int)phase; i < g_lwWorld.count; i += 8)
    {
        LwSquad* q = &g_lwSquads[i];
        if (q->state == LW_ONLINE && q->alive > 0) LwPatrolSample(q, now);
        else if (g_lwView.squad[i].known) co_lw_view_gone(&g_lwView, (uint32_t)i);
    }
    LwPatrolGiveWay(now);
    if (now - g_lwPatrols.noteAt >= 30000) { g_lwPatrols.noteAt = now; LwPatrolsNotes(now); }
}
