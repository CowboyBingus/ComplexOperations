/* Patrols a player can see (included by living_world.c after the density rules). The squads came online unseen and
 * nothing brought them into view: they walked a cross-country grid while the roads, the open corridors, stood empty;
 * a squad meant to be seen at distance kept its place in the far share while it stood still or after the player had
 * walked up to it; and the far band reached past what an unzoomed view draws. Here: road nodes along the generator's
 * road points and a share of squads that keep to them, the far squads' two shares, the band from the views in play, the
 * spawn band looking ahead of a vehicle, room for a squad about to walk into view, and a line per player every 30 s. */
#define LW_ROAD_SHARE_OF 10       /* one squad in this many is a road patrol (D: one about every 360 m of a 294 km network) */
#define LW_ROAD_NODES_MIN 500
#define LW_ROAD_ART_OF 2          /* one road patrol in this many keeps to the arterials, the roads vehicles drive (D) */
#define LW_ROAD_ART_MIN 200       /* arterial nodes: fewer, and no road patrol is an arterial one */
#define LW_ROAD_SPUR_ODDS 4       /* an arterial patrol passing a spur turns into it one time in this many (D) */
#define LW_PATROL_YIELD 85.0f     /* m: a waiting squad's nine points lie within 23 m of each other, and LW_SPACING of all must stay free */
#define LW_ROAD_OFF_COST 4.0f     /* a road patrol's path counts a stretch off the roads this many times */
#define LW_ROAD_END 100.0f        /* m: a road's first and last point lie this near its edge's nodes, or it is left alone */
#define LW_ROAD_NEAR 30.0f        /* m to a road point: "by a road" in the notes */
#define LW_PATROL_STEP_MS 125     /* an eighth of the online squads is read each step: every squad once a second */
#define LW_PATROL_SWAP_MS 10000   /* at most one filler squad gives way in this time */
#define LW_PATROL_NOTES 60        /* event lines a mission (the 30 s lines are apart) */

static CoLwView g_lwView;
static CoLwRoads g_lwRoads;
static struct
{
    ULONGLONG stepAt, noteAt, swapAt, bandAt;
    uint32_t phase, notes, scanAt, gaveWay, served, stoodOff, sightRefused, ledOn, ledLeft, yielded;
    int hold1; /* the squad a filler gave way to, + 1 */
    LONG mission;
} g_lwPatrols;
static uint8_t g_lwPatrolLed[LW_SQUADS_MAX];     /* 1 in the spawn band only by a vehicle's lead, 2 online so, 4 a player came within R_on */
static ULONGLONG g_lwPatrolLookAt[LW_SQUADS_MAX]; /* a standing far squad's next sight test */
static uint16_t g_lwRoadEdge[LW_NODES_MAX];       /* a road node's route edge (the spurs join an edge) */
static int g_lwArtNodes;
/* Whether each patrol with a goal walks (co_lw_view.h, co_lw_move_*), and its own line every 5 s. */
#define LW_PATROL_TRACE 3000 /* 5 s lines a mission */
static CoLwMove g_lwPatrolMove[LW_SQUADS_MAX];
static uint32_t g_lwPatrolTraced;
static int LwRoadPatrolsOn(void); static void LwPatrolMoved(const LwSquad* q, int idx, int calm, int fighting, ULONGLONG now);
static int g_lwPatrolsOld; /* test command "patrols old 1": the rules before these, for a before value in the same run */

static float LwRoadCost(int a, int b)
{
    return g_lwPathRoad && (!g_lwNodeKind[a] || !g_lwNodeKind[b]) ? LW_ROAD_OFF_COST : 1.0f;
}
/* Too few road nodes (a level whose roads have no points) and the road patrols would crowd them: none then. */
/* 0 no road patrol, 1 one of any road, 2 one of the arterials and their spurs. */
static int LwRoadSquad(int index)
{
    if (g_lwRoadNodes < LW_ROAD_NODES_MIN || index % LW_ROAD_SHARE_OF) return 0;
    return g_lwArtNodes >= LW_ROAD_ART_MIN && index / LW_ROAD_SHARE_OF % LW_ROAD_ART_OF == 0 ? 2 : 1;
}
/* Where a squad starts: an arterial patrol at an arterial's point, another road patrol at another road's, the rest off them. */
static int LwRoadStart(int index, int node)
{
    const int kind = g_lwNodeKind[node], road = LwRoadSquad(index);
    return road == 2 ? (kind & CO_LW_ROADS_ARTERIAL) && (kind & CO_LW_ROADS_POINT) : road == 1 ? kind == CO_LW_ROADS_POINT : !(kind & (CO_LW_ROADS_POINT | CO_LW_ROADS_SPUR));
}
/* A road patrol's next node from `at`: an arterial patrol keeps to the arterials and now and then turns into a spur,
 * the others walk any road but the spurs; with no such way on, any road. */
static int LwRoadStep(int road, int at, int from)
{
    const LwNode* n = &g_lwNodes[at];
    int next = -1;
    if (road == 2 && LwRand() % LW_ROAD_SPUR_ODDS == 0) next = co_lw_roads_step(n->next, n->degree, g_lwNodeKind, CO_LW_ROADS_SPUR, from, LwRand());
    if (next < 0) next = co_lw_roads_step(n->next, n->degree, g_lwNodeKind, road == 2 ? CO_LW_ROADS_ARTERIAL : CO_LW_ROADS_END | CO_LW_ROADS_POINT, from, LwRand());
    if (next < 0) next = co_lw_roads_step(n->next, n->degree, g_lwNodeKind, 0xFF, from, LwRand());
    return next;
}

/* A road patrol's way: from a road node, hop by hop along the roads (never straight back, but for a road's end), up
 * to 1200 m of road; from anywhere else, back to the nearest road node. Goals drawn from all nodes left the roads. */
static int LwRoadPlan(LwSquad* q, int home)
{
    const float r2 = g_lwWorld.radius * g_lwWorld.radius;
    const int road = LwRoadSquad((int)(q - g_lwSquads));
    /* A plan asked for over one not yet walked: LwSendGoal found no navmesh at its first node. The same node again
     * would leave the squad standing without a goal, so from a road the patrol takes another way (back, at most);
     * off the roads, or with no other way, the grid's plan (0) draws a goal by chance. */
    const int refused = q->pathAt < q->pathLen ? q->path[q->pathAt] : -1;
    if (!g_lwNodeKind[q->node])
    {
        if (refused >= 0) return 0;
        int best = -1; float bd = 3.0e38f;
        for (int i = g_lwNodeCount - g_lwRoadNodes; i < g_lwNodeCount; i++)
        {
            float d = LwDist2(q->pos, g_lwNodes[i].x, g_lwNodes[i].y);
            if (road == 2 && !(g_lwNodeKind[i] & CO_LW_ROADS_ARTERIAL)) continue;
            if (d < bd && g_lwNodes[i].x * g_lwNodes[i].x + g_lwNodes[i].y * g_lwNodes[i].y <= r2) { bd = d; best = i; }
        }
        g_lwPathRoad = 1;
        int n = best >= 0 ? LwPath(q->node, best, q->path, LW_PATH_MAX) : 0;
        g_lwPathRoad = 0;
        if (n > 0) { q->pathLen = (uint8_t)n; q->pathAt = 0; }
        return n > 0;
    }
    for (int tries = 0; tries < 4; tries++)
    {
        int hops = 4 + (int)(LwRand() % 9u), n = 0, at = q->node, from = refused >= 0 ? refused : q->prevValid ? q->prevNode : -1;
        uint16_t path[LW_PATH_MAX];
        while (n < hops)
        {
            int next = LwRoadStep(road, at, from);
            if (next < 0 && n == 0 && refused < 0 && from >= 0 && g_lwNodeKind[from]) next = from; /* a road's end: about turn */
            if (next < 0 || g_lwNodes[next].x * g_lwNodes[next].x + g_lwNodes[next].y * g_lwNodes[next].y > r2) break;
            path[n++] = (uint16_t)next; from = at; at = next;
        }
        if (n == 0) return 0;
        if (home && tries < 3 && !LwTerritoryGoalOk(q, at)) continue;
        memcpy(q->path, path, (size_t)n * sizeof path[0]); q->pathLen = (uint8_t)n; q->pathAt = 0;
        return 1;
    }
    return 0;
}

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
    memset(g_lwNodeKind, 0, sizeof g_lwNodeKind); memset(g_lwRoadEdge, 0xFF, sizeof g_lwRoadEdge); g_lwRoadNodes = g_lwArtNodes = 0; g_lwRoads.count = 0;
    if (g_lwWorld.radius < 1500.0f) return; /* a vanilla disc: the level's routes are its roads already */
    if (!LwRoadPatrolsOn()) { Note("living world: roads: road patrols are switched off; the squads keep to the routes and the grid"); return; }
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
    uint32_t roads = 0, noPoints = 0, off = 0, full = 0, artRoads = 0; double km = 0, artKm = 0;
    /* the roads' build marks its arterials and lists its spurs for the level it last built: only that level's are read */
    const int built = g_arterials.level == g_world.levels && g_arterials.count > 0;
    for (uint32_t e = 0; e < edges; e++)
    {
        uint8_t rec[0x48]; uint64_t ends[4]; uint32_t range[2]; uint16_t flags;
        if (SafeRead(G + g_routeEdges + 0x48ull * e, rec, sizeof rec)) break;
        memcpy(ends, rec + 0x10, 32); memcpy(range, rec + 0x38, 8); memcpy(&flags, rec + 0x44, 2);
        if (rec[0x34] == 2 || rec[0x34] == 8 || (flags & 2)) continue;
        const int fit = !(ends[0] < nodes || ends[1] < nodes || (ends[0] - nodes) % 0x98u || (ends[1] - nodes) % 0x98u ||
            (ends[0] - nodes) / 0x98u >= levelNodes || (ends[1] - nodes) / 0x98u >= levelNodes || ends[0] == ends[1] || ends[2] == 0 || ends[3] == 0); /* cells +0x20 / +0x28: the copy links no edge without both */
        int a = fit ? (int)((ends[0] - nodes) / 0x98u) : 0, b = fit ? (int)((ends[1] - nodes) / 0x98u) : 0;
        const uint8_t art = built && e < ROADS_EDGES_MAX && ((g_arterials.bits[e >> 3] >> (e & 7)) & 1) ? CO_LW_ROADS_ARTERIAL : 0;
        if (fit) { g_lwNodeKind[a] |= CO_LW_ROADS_END | art; g_lwNodeKind[b] |= CO_LW_ROADS_END | art; } /* a road without points is its chord: still a road */
        if (range[1] > points || range[1] < range[0] + 2) { noPoints++; continue; }
        if (!fit) { off++; continue; }
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
            g_lwNodeKind[g_lwNodeCount] = CO_LW_ROADS_POINT | art; g_lwRoadEdge[g_lwNodeCount] = (uint16_t)(e < 0xFFFFu ? e : 0xFFFFu);
            LwLink(prev, g_lwNodeCount); prev = g_lwNodeCount++; g_lwRoadNodes++; if (art) g_lwArtNodes++;
        }
        LwLink(prev, b);
        roads++; if (art) artRoads++;
        double len = 0;
        for (uint32_t i = range[0] + 1; i < range[1]; i++) len += sqrt((double)LwDist2(g_lwRoads.pts[i], g_lwRoads.pts[i - 1][0], g_lwRoads.pts[i - 1][1])) / 1000.0;
        km += len; if (art) artKm += len;
    }
    /* A spur's points run from its place to a point of an arterial edge. Its nodes join that edge's nearest road node
     * (an end node for a short edge); none stands within half a stretch of the place, so a patrol turns back short of it. */
    uint32_t spurs = 0, spurNodes = 0, spurShort = 0, spurOff = 0;
    for (uint32_t k = 0; g_spurs.level == g_world.levels && built && k < g_spurs.count && k < ROADS_SPURS; k++)
    {
        const RoadSpur* sp = &g_spurs.spur[k];
        uint32_t cut[32];
        if (sp->end > points || sp->end < sp->first + 2 || sp->edge >= edges) { spurOff++; continue; }
        const uint32_t n = co_lw_roads_cut((const float (*)[2])g_lwRoads.pts, sp->first, sp->end, CO_LW_ROADS_STRETCH, cut, 32);
        if (n == 0) { spurShort++; continue; }
        const float* to = g_lwRoads.pts[sp->end - 1];
        int join = -1; float jd = LW_ROAD_END * LW_ROAD_END * 4.0f; uint64_t ends[2] = {0, 0};
        for (int i = g_lwNodeCount - g_lwRoadNodes; i < g_lwNodeCount; i++)
        {
            float d = LwDist2(to, g_lwNodes[i].x, g_lwNodes[i].y);
            if (g_lwRoadEdge[i] == sp->edge && d < jd) { jd = d; join = i; }
        }
        for (int end = 0; end < 2 && !SafeRead(G + g_routeEdges + 0x48ull * sp->edge + 0x10, ends, 16); end++)
        {
            if (ends[end] < nodes || (ends[end] - nodes) % 0x98u || (ends[end] - nodes) / 0x98u >= levelNodes) continue;
            const int i = (int)((ends[end] - nodes) / 0x98u); float d = LwDist2(to, g_lwNodes[i].x, g_lwNodes[i].y);
            if (d < jd) { jd = d; join = i; }
        }
        if (join < 0 || g_lwNodeCount + (int)n > LW_NODES_MAX || g_lwNodes[join].degree >= 16) { spurOff++; continue; }
        int prev = join;
        for (uint32_t c = n; c-- > 0;) /* from the arterial out */
        {
            LwNode* o = &g_lwNodes[g_lwNodeCount]; memset(o, 0, sizeof *o);
            o->x = g_lwRoads.pts[cut[c]][0]; o->y = g_lwRoads.pts[cut[c]][1];
            g_lwNodeKind[g_lwNodeCount] = CO_LW_ROADS_SPUR;
            LwLink(prev, g_lwNodeCount); prev = g_lwNodeCount++; g_lwRoadNodes++; spurNodes++;
        }
        spurs++;
    }
    Note("living world: roads: %u roads of %u edges walked along their points (%.0f km, %d nodes about %.0f m apart; %u points); left as they were: "
        "%u without points, %u whose points or nodes do not fit, %u for room; one squad in %d keeps to roads", roads, edges, km, g_lwRoadNodes,
        (double)CO_LW_ROADS_STRETCH, points, noPoints, off, full, LW_ROAD_SHARE_OF);
    Note("living world: roads: arterials %u roads (%.0f km, %d nodes)%s; spurs %u of %u walked (%u nodes), %u too short for a node, %u that do not fit; "
        "%s", artRoads, artKm, g_lwArtNodes, built ? "" : " (the roads' build has none for this level)", spurs, g_spurs.level == g_world.levels ? g_spurs.count : 0,
        spurNodes, spurShort, spurOff, g_lwArtNodes >= LW_ROAD_ART_MIN ? "every second road patrol keeps to the arterials and their spurs" : "no arterial patrols");
}

/* Road patrols wait on the roads' build: a research build turns them off with HELLFORGE_LW_ROAD_PATROLS=0 (or on with
 * 1) for a run; a package build takes the default. */
#define LW_ROAD_PATROLS_DEFAULT 1
static int LwRoadPatrolsOn(void)
{
    static int on = -1;
    if (on < 0) { char t[4] = ""; const DWORD n = GetEnvironmentVariableA("HELLFORGE_LW_ROAD_PATROLS", t, sizeof t); on = co_lw_view_switch(t, n, LW_ROAD_PATROLS_DEFAULT); }
    return on;
}

/* A squad's place among the far ones (co_lw_view_far): by sightline and still beyond R_on, walking or standing. */
static int LwPatrolFar(const LwSquad* q)
{
    if (!q->farOn) return CO_LW_VIEW_FAR_NONE;
    if (q->state != LW_ONLINE || g_lwPatrolsOld) return CO_LW_VIEW_FAR_WALKS; /* requested: on its way in */
    return co_lw_view_far(1, LwNearestPlayer2(q->pos[0], q->pos[1]), LW_R_ON, co_lw_view_stood(&g_lwView, (uint32_t)(q - g_lwSquads), GetTickCount64()));
}
/* The far units a new far squad is measured against, from the walking and the standing ones' (co_lw_view_far_sum). */
static int LwPatrolFarSum(int walking, int standing) { return co_lw_view_far_sum(walking, standing, (int)LwFarShareLimit()); }

/* The band test's distance for an offline squad: the nearest player's, or the band of the point a vehicle will reach. */
static float LwLeadNear2(const LwSquad* q)
{
    const float* pos = q->pos; uint8_t* led = &g_lwPatrolLed[q - g_lwSquads];
    float near2 = LwNearestPlayer2(pos[0], pos[1]), lead2 = 3.0e38f, rMin = g_lwWorld.rMin > 0 ? g_lwWorld.rMin : LW_R_MIN;
    *led &= (uint8_t)~1u;
    if (g_lwPatrolsOld) return near2;
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_VIEW_PLAYERS; i++)
    {
        float lead[2];
        if (!co_lw_view_lead(&g_lwView, (uint32_t)i, g_lwWorld.players[i], lead)) continue;
        float d = LwDist2(lead, pos[0], pos[1]);
        if (d < lead2) lead2 = d;
    }
    float band2 = co_lw_view_band2(near2, lead2, rMin, LW_R_ON);
    if (band2 != near2) *led |= 1;
    return band2;
}

/* The room a filler squad left is held for squad `idx` (N's hold rules ask here: an errand may take the hold over, a
 * far squad's swap may not, or two fillers leave for one place). */
static int LwPatrolHolds(int idx)
{
    return g_lwPatrols.hold1 == idx + 1 && g_lwHold.idx == idx && GetTickCount64() < g_lwHold.until;
}
/* After a squad's request: one given room or looked ahead to stands at the edge of cover, where the sight test
 * refuses most, so the notes count it. */
static void LwPatrolAsked(const LwSquad* q)
{
    const int idx = (int)(q - g_lwSquads), mine = g_lwPatrols.hold1 == idx + 1;
    if (q->state != LW_OFFLINE) { if (mine) { g_lwPatrols.hold1 = 0; g_lwPatrols.served++; } return; }
    if ((LwPatrolHolds(idx) || (g_lwPatrolLed[idx] & 1)) && g_lwOnlineWhy && strncmp(g_lwOnlineWhy, "sight", 5) == 0) g_lwPatrols.sightRefused++;
}
static void LwPatrolHoldTaken(void) { g_lwPatrols.hold1 = 0; }
/* A patrol does not come online where a squad sent by an event waits at the band's edge: within LW_SPACING of one of
 * that squad's points it would refuse the squad's own request for a minute (LwRecentNear). */
static int LwPatrolYields(const LwSquad* q)
{
    if (q->intent != LW_PATROL || q->held) return 0;
    for (const LwSquad* o = g_lwSquads; o < g_lwSquads + g_lwWorld.count; o++)
        if (o != q && o->held && o->state == LW_OFFLINE && LwDist2(o->pos, q->pos[0], q->pos[1]) < LW_PATROL_YIELD * LW_PATROL_YIELD) { g_lwPatrols.yielded++; return 1; }
    return 0;
}
static void LwPatrolsOldCmd(int on)
{
    g_lwPatrolsOld = on != 0; g_lwPatrols.bandAt = 0;
    Note("living world: test: patrols by the %s rules (the far share, the band's view, the look ahead, giving way)", g_lwPatrolsOld ? "old" : "new");
}

/* The far band follows the views in play: the host's own angle as its camera has it now (a scope narrows it, and the
 * band grows while it does), and for peers, whose angle is not known here, the narrowest unzoomed setting (45 deg). */
static void LwBandTick(ULONGLONG now)
{
    if (now - g_lwPatrols.bandAt < 250) return;
    g_lwPatrols.bandAt = now;
    const float deg = 3.14159265f / 180.0f;
    float f = 45.0f * deg;
    if (g_lwPatrolsOld) f = 11.25f * deg; /* every player through a 4x scope */
    else if (LwViewRead() >= 0 && g_lwViewFov > 0) { f = g_lwViewFov; if (g_lwWorld.playersAt > 1 && f > 45.0f * deg) f = 45.0f * deg; }
    if (f < 11.25f * deg) f = 11.25f * deg;
    if (f > 90.0f * deg) f = 90.0f * deg;
    if (fabsf(f - g_lwBandFov) < 1.0f * deg) return;
    g_lwBandFov = f;
    for (int k = 0; k < LW_RADII; k++) if (g_lwRadii[k].definition) g_lwRadii[k].band = LwFarBand(g_lwRadii[k].radius);
    g_lwRadiiGen++;
}

static int LwPatrolNote(void) { return g_lwPatrols.notes < LW_PATROL_NOTES ? (int)++g_lwPatrols.notes : 0; }

/* No player has a line to where any listed member stands (LwHidden: chest, head and a tall unit's top). */
static int LwSquadHidden(const LwSquad* q)
{
    for (int k = 0; k < q->seenCount; k++) if (!isfinite(q->seenAt[k][2]) || !LwHidden(q->seenAt[k])) return 0;
    return q->seenCount > 0;
}
/* Whether a far squad that stands may be taken offline: not while the stall recovery still tries (co_lw_view_gave_up). */
static int LwPatrolMayRemove(const LwSquad* q, ULONGLONG now)
{
    return co_lw_view_gave_up(co_lw_view_stood(&g_lwView, (uint32_t)(q - g_lwSquads), now), q->stallTries, q->goalAt != 0);
}

/* One online squad's sample: where its leader stands, who has a clear line to it, whether a member fights. */
static void LwPatrolSample(LwSquad* q, ULONGLONG now)
{
    CoLwViewSample s; memset(&s, 0, sizeof s);
    const int idx = (int)(q - g_lwSquads);
    s.index = (uint32_t)idx; s.units = q->alive; s.nowMs = now; s.pos[0] = q->pos[0]; s.pos[1] = q->pos[1];
    s.road = (uint8_t)co_lw_roads_near(&g_lwRoads, q->pos[0], q->pos[1], LW_ROAD_NEAR);
    s.sightline = q->farOn; s.patrol = q->intent == LW_PATROL;
    int calm = 1;
    s.harm = (uint32_t)q->lost * 100000u;
    for (int k = 0; k < q->seenCount; k++)
    {
        uint32_t alert = LwAlert(q->seen[k]); ActHealth h;
        if (alert) calm = 0;
        if (alert >= 3) s.fighting = 1;
        if (ActHealthOf(g_lw.game, q->seen[k], &h) && h.max > h.health && h.max > 0) s.harm += (uint32_t)(h.max - (h.health > 0 ? h.health : 0));
    }
    const float near2 = LwNearestPlayer2(q->pos[0], q->pos[1]);
    LwPatrolMoved(q, idx, calm, s.fighting, now);
    uint8_t* led = &g_lwPatrolLed[idx];
    if ((*led & 1) && !(*led & 2)) { *led |= 2; g_lwPatrols.ledOn++; }
    if ((*led & 2) && near2 < LW_R_ON * LW_R_ON) *led |= 4;
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
    /* a far squad that stands out of every player's sight holds a place nobody sees filled: offline, as a swap takes
     * it. Only beyond R_on (nearer it is a near squad) and with every member hidden, tested again 5 s after a no. */
    if (!g_lwPatrolsOld && q->farOn && s.patrol && calm && !q->forceOff && near2 >= LW_R_ON * LW_R_ON && now >= g_lwPatrolLookAt[idx] && LwPatrolMayRemove(q, now))
    {
        g_lwPatrolLookAt[idx] = now + 5000;
        if (LwSquadHidden(q))
        {
            q->forceOff = 2; g_lwPatrols.stoodOff++;
            if (LwPatrolNote()) Note("living world: squad %d came online by sightline and stood %.0f s with every member out of every player's sight, %.0f m from the nearest: offline",
                idx, (double)co_lw_view_stood(&g_lwView, s.index, now) / 1000.0, (double)sqrtf(near2));
        }
    }
}
/* Once a second for an online squad: its way, and every 5 s a line with what tells a stander from a walker. */
static void LwPatrolMoved(const LwSquad* q, int idx, int calm, int fighting, ULONGLONG now)
{
    const float left = sqrtf(LwDist2(q->pos, q->goalTo[0], q->goalTo[1]));
    float way5 = 0;
    if (!co_lw_move_sample(&g_lwPatrolMove[idx], q->pos, co_lw_move_state(q->intent == LW_PATROL, calm, q->goalAt != 0, left, LW_ARRIVE), &way5)) return;
    if (q->intent != LW_PATROL || g_lwPatrolTraced >= LW_PATROL_TRACE) return;
    const uintptr_t nav = q->squad ? LwNavRecord(q->squad) : 0;
    g_lwPatrolTraced++;
    Note("living world: patrol %d: at (%.0f, %.0f), %.1f m in 5 s; %s; goal %s (%.0f, %.0f) %.0f m away, sent %.0f s ago, path left %.0f m, request pending %u, "
        "stall tries %u; route %u of %u; %s; %u alive, %.0f m from the nearest player%s", idx, (double)q->pos[0], (double)q->pos[1], (double)way5,
        fighting ? "fighting" : calm ? "calm" : "alert", q->goalAt ? "at" : "none, last", (double)q->goalTo[0], (double)q->goalTo[1], (double)left,
        q->goalAt ? (double)(now - q->goalAt) / 1000.0 : -1.0, (double)LwStallPath(q, nav), nav ? EvpU32(nav + 0x90, 0) & 0xFF : 0, q->stallTries, q->pathAt, q->pathLen,
        LwRoadSquad(idx) == 2 ? "arterial patrol" : LwRoadSquad(idx) ? "road patrol" : q->farOn ? "by sightline" : "near", q->alive,
        (double)sqrtf(LwNearestPlayer2(q->pos[0], q->pos[1])), g_lwPatrolTraced == LW_PATROL_TRACE ? " (the last such line this mission)" : "");
}
/* The squad left the world's online ones: one that came online ahead of a vehicle and no player reached is counted. */
static void LwPatrolGone(int i)
{
    uint8_t* led = &g_lwPatrolLed[i];
    if (g_lwView.squad[i].known) co_lw_view_gone(&g_lwView, (uint32_t)i);
    co_lw_move_gone(&g_lwPatrolMove[i]);
    if ((*led & 2) && !(*led & 4)) g_lwPatrols.ledLeft++;
    *led &= 1;
}

/* The filler squad that gives way: of the three calm patrol squads farthest from every player, the farthest whose
 * every member is hidden (LwFillerPick tests the leader's place alone). */
static LwSquad* LwPatrolFiller(const LwSquad* except, float* pickD2)
{
    LwSquad* best[3] = {NULL, NULL, NULL}; float bd[3] = {-1, -1, -1};
    for (int i = 0; i < g_lwWorld.count; i++)
    {
        LwSquad* q = &g_lwSquads[i]; float at[3];
        if (!LwFillerCandidate(q, except, at)) continue;
        float d2 = LwNearestPlayer2(at[0], at[1]);
        for (int k = 0; k < 3; k++) if (d2 > bd[k]) { for (int j = 2; j > k; j--) { best[j] = best[j - 1]; bd[j] = bd[j - 1]; } best[k] = q; bd[k] = d2; break; }
    }
    for (int k = 0; k < 3; k++) if (best[k] && LwSquadHidden(best[k])) { *pickD2 = bd[k]; return best[k]; }
    return NULL;
}

/* Room for a squad about to walk into view. A squad in the spawn band whose way ahead a player sees, refused only
 * because its bubble is full, takes the place of the farthest calm squad nobody sees (LwFillerPick, as a far squad in
 * sight does): the room is held for it, and it still comes online at its own hidden point. */
static void LwPatrolGiveWay(ULONGLONG now)
{
    if (now - g_lwPatrols.swapAt < LW_PATROL_SWAP_MS || g_lwDensity.on != 1 || g_lwPatrolsOld) return;
    if (g_lwHold.idx >= 0 && now < g_lwHold.until) return; /* a held squad is being served */
    const float rMin = g_lwWorld.rMin > 0 ? g_lwWorld.rMin : LW_R_MIN;
    const uint32_t count = (uint32_t)g_lwWorld.count; uint32_t k = 0;
    for (int tries = 0; k < count && tries < 3; k++)
    {
        const int i = (int)co_lw_view_scan_at(g_lwPatrols.scanAt, k, count); /* from where the last scan stopped */
        LwSquad* q = &g_lwSquads[i];
        if (q->state != LW_OFFLINE || q->intent != LW_PATROL || q->held || q->pulled || q->survivors == 0 || (q->retry && now < q->retry)) continue;
        float near2 = LwNearestPlayer2(q->pos[0], q->pos[1]);
        if (near2 < rMin * rMin || near2 >= LW_R_ON * LW_R_ON) continue;
        int b = LwBubbleOf(q->pos[0], q->pos[1]);
        if (b < 0 || g_lwDensity.units[b] + (int)q->survivors <= g_lwDensity.budget[b]) continue; /* room: it asks for itself */
        if (LwRecentNear(q->pos) || LwPatrolYields(q)) continue;
        tries++;
        if (!LwAboutToBeSeen(q)) continue;
        float pickD;
        LwSquad* pick = LwPatrolFiller(q, &pickD);
        g_lwPatrols.swapAt = now; g_lwPatrols.scanAt = co_lw_view_scan_at(g_lwPatrols.scanAt, k + 1, count);
        if (pick == NULL) return;
        pick->forceOff = 2; g_lwPatrols.gaveWay++;
        g_lwHold.idx = i; g_lwHold.units = q->survivors; g_lwHold.until = now + 20000; g_lwPatrols.hold1 = i + 1;
        if (LwPatrolNote()) Note("living world: squad %d (hidden, calm, %.0f m from the nearest player) goes offline to make room for squad %d, %.0f m away, whose way ahead a player sees",
            (int)(pick - g_lwSquads), (double)sqrtf(pickD), i, (double)sqrtf(near2));
        return;
    }
    g_lwPatrols.scanAt = co_lw_view_scan_at(g_lwPatrols.scanAt, k, count);
    g_lwPatrols.swapAt = now - LW_PATROL_SWAP_MS + 2000; /* looked and found none: again in 2 s */
}

static void LwPatrolsNotes(ULONGLONG now)
{
    char line[512];
    int farWalk = 0, farStand = 0, roadOnline = 0, artOnline = 0, led = 0;
    for (int i = 0; i < g_lwWorld.count; i++)
    {
        const LwSquad* q = &g_lwSquads[i];
        if (q->state != LW_ONLINE) continue;
        const int place = LwPatrolFar(q);
        if (place == CO_LW_VIEW_FAR_WALKS) farWalk += q->alive; else if (place == CO_LW_VIEW_FAR_STANDS) farStand += q->alive;
        if (LwRoadSquad(i)) roadOnline++;
        if (LwRoadSquad(i) == 2) artOnline++;
    }
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_VIEW_PLAYERS; i++) { float lead[2]; led += co_lw_view_lead(&g_lwView, (uint32_t)i, g_lwWorld.players[i], lead); }
    if (co_lw_view_note(&g_lwView, now, line, sizeof line))
        Note("living world: %s; far beyond %.0f m: %d units walking, %d standing, a share of %.0f each; road squads online %d (%d the arterials'); %u gave way to a squad walking "
            "into view (%u such squads came online), %u stood unseen and left; ahead of a vehicle %u squads came online, %u went offline with no player within "
            "%.0f m; %u requests of squads given room or ahead of a vehicle refused for sight; %u patrol requests left a waiting squad's points free; band for a 2 m unit %.0f m (%.0f deg view); %d players' bands "
            "look ahead%s", line, (double)LW_R_ON, farWalk, farStand, (double)LwFarShareLimit(), roadOnline, artOnline, g_lwPatrols.gaveWay, g_lwPatrols.served,
            g_lwPatrols.stoodOff, g_lwPatrols.ledOn, g_lwPatrols.ledLeft, (double)LW_R_ON, g_lwPatrols.sightRefused, g_lwPatrols.yielded, (double)LwFarBand(2.0f),
            (double)(g_lwBandFov * 57.29578f), led, g_lwPatrolsOld ? "; the old rules (test)" : "");
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_VIEW_PLAYERS; i++)
        if (co_lw_view_player_note(&g_lwView, (uint32_t)i, now, line, sizeof line)) Note("living world: %s", line);
    co_lw_move_note(g_lwPatrolMove, (uint32_t)g_lwWorld.count, line, sizeof line); Note("living world: %s", line);
    co_lw_view_restart(&g_lwView, now);
    g_lwPatrols.gaveWay = g_lwPatrols.served = g_lwPatrols.stoodOff = g_lwPatrols.ledOn = g_lwPatrols.ledLeft = g_lwPatrols.sightRefused = g_lwPatrols.yielded = 0;
}

static void LwPatrolsTick(void)
{
    ULONGLONG now = GetTickCount64();
    if (g_lwPatrols.mission != g_lwMission)
    {
        memset(&g_lwPatrols, 0, sizeof g_lwPatrols); memset(g_lwPatrolLed, 0, sizeof g_lwPatrolLed); memset(g_lwPatrolLookAt, 0, sizeof g_lwPatrolLookAt);
        memset(g_lwPatrolMove, 0, sizeof g_lwPatrolMove); g_lwPatrolTraced = 0;
        g_lwPatrols.mission = g_lwMission; g_lwPatrols.noteAt = now;
    }
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
        else LwPatrolGone(i);
    }
    LwPatrolGiveWay(now);
    if (now - g_lwPatrols.noteAt >= 30000) { g_lwPatrols.noteAt = now; LwPatrolsNotes(now); }
}

#ifdef CO_TEST
/* Squad 0 as a road patrol on a line of `n` road nodes 100 m apart, standing at node `at`, come from `prev` (-1: from
 * nowhere), with an unwalked plan that begins at `refused` (-1: none). The new plan's first node, -1 with no road plan. */
int CoXlTestLwRoadPlan(int n, int at, int prev, int refused)
{
    LwSquad* q = &g_lwSquads[0];
    memset(q, 0, sizeof *q); memset(g_lwNodeKind, 0, sizeof g_lwNodeKind);
    g_lwNodeCount = g_lwRoadNodes = LW_ROAD_NODES_MIN; g_lwArtNodes = 0; g_lwWorld.radius = 1.0e6f;
    for (int i = 0; i < n; i++)
    {
        LwNode* node = &g_lwNodes[i];
        memset(node, 0, sizeof *node); node->x = 100.0f * (float)i;
        if (i > 0) node->next[node->degree++] = (uint16_t)(i - 1);
        if (i + 1 < n) node->next[node->degree++] = (uint16_t)(i + 1);
        g_lwNodeKind[i] = i == 0 || i == n - 1 ? CO_LW_ROADS_END : CO_LW_ROADS_POINT;
    }
    q->node = (uint16_t)at; q->pos[0] = g_lwNodes[at].x; q->prevValid = prev >= 0; q->prevNode = (uint16_t)(prev >= 0 ? prev : 0);
    if (refused >= 0) { q->path[0] = (uint16_t)refused; q->pathLen = 1; }
    const int planned = LwRoadPlan(q, 0);
    g_lwNodeCount = g_lwRoadNodes = 0;
    return planned ? q->path[0] : -1;
}
/* The road patrols' switch as this build reads it. */
int CoXlTestLwRoadPatrolsOn(void) { return LwRoadPatrolsOn(); }
#endif
