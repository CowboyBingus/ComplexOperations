/* Patrols a player can see (included by living_world.c after the density rules). The squads came online unseen and
 * nothing brought them into view: they walked a cross-country grid while the roads, the open corridors, stood empty;
 * a squad meant to be seen at distance kept its place in the far share while it stood still or after the player had
 * walked up to it; and the far band reached past what an unzoomed view draws. Here: road nodes along the generator's
 * road points and a share of squads that keep to them, the far squads' two shares, the band from the views in play, the
 * spawn band looking ahead of a vehicle, room for a squad about to walk into view, a road patrol's wait at the ring's
 * edge until it is online (co_lw_meet.h), small road patrols called to the road a player is on, and lines per player
 * every 30 s. */
#define LW_ROAD_SHARE_OF 10       /* one squad in this many is a road patrol, and as many again are added (LwRoadTailSet): one about every 180 m of a 294 km network */
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
static int g_lwRoadTail[2] = {LW_SQUADS_MAX, LW_SQUADS_MAX}; /* the squads [from, end) after the world's own: road patrols all */
_Static_assert(LW_SQUADS_BASE + (LW_SQUADS_BASE + LW_ROAD_SHARE_OF - 1) / LW_ROAD_SHARE_OF <= LW_SQUADS_MAX, "the added road patrols fit the table");
/* Whether each patrol with a goal walks (co_lw_view.h, co_lw_move_*), and its own line every 5 s. */
/* 5 s lines a mission: 30 patrols online write 360 a minute, so a research run's half hour needs its own limit. */
#ifdef CO_RESEARCH
#define LW_PATROL_TRACE 20000
#else
#define LW_PATROL_TRACE 3000
#endif
static CoLwMove g_lwPatrolMove[LW_SQUADS_MAX];
static uint32_t g_lwPatrolTraced;
static int LwRoadPatrolsOn(void); static void LwPatrolMoved(const LwSquad* q, int idx, int calm, int fighting, ULONGLONG now);
static int g_lwPatrolsOld; /* test command "patrols old 1": the rules before these, for a before value in the same run */
/* Patrols on their way to a player (co_lw_meet.h). A road patrol's wait at the ring's edge: since when, 0 for one that walks. */
#define LW_MEET_NOTES 120u
#define LW_UNFIT_NOTES 64u
#define LW_NO_FACE_NOTES 40u
#define LW_MEET_LOOKS 4u     /* looks a tick over all squads: each is three rays and a ground ray per player in reach */
#define LW_MEET_ON_ROAD 1u   /* offline, on a player's road */
#define LW_MEET_CAME 2u      /* online: it came online on its way in, and no player has been within R_on of it yet */
#define LW_MEET_FAR_ROAD 4u  /* online: that, on a player's road beyond R_on: its units are the far share's kept half */
#define LW_MEET_NEARING 8u   /* offline, a road patrol still out whose way leads to a player: the far share keeps its room */
static ULONGLONG g_lwMeetSince[LW_SQUADS_MAX], g_lwMeetLookAt[LW_SQUADS_MAX], g_lwMeetSwapAt[EVT_MAX_PLAYERS];
static uint8_t g_lwMeetMark[LW_SQUADS_MAX], g_lwMeetGap[LW_SQUADS_MAX][CO_LW_MEET_PLAYERS]; /* gap: samples since player i saw it walk */
static CoLwMeetCover g_lwMeetCover[LW_SQUADS_MAX];
static CoLwMeetHolds g_lwMeetHolds;
static int g_lwMeetOff; /* test command "meet 0" */
static uint8_t g_lwMeetClose[LW_SQUADS_MAX]; /* 1 while counted as offline within CO_LW_MEET_CLOSE of a player */
static uint8_t g_lwMeetCame[LW_SQUADS_MAX], g_lwMeetWatch[LW_SQUADS_MAX][CO_LW_MEET_PLAYERS]; /* online: the rule it came by; seconds player i has watched it walk */
static CoLwMeet g_lwMeetNear;
static struct
{
    uint32_t held, online, turned, back, gaveWay, noFiller, sight, room, spaced, other, close, notes, noFaceNotes;
    uint32_t looks, looksTick, covered, edge, beyond, cameCover, cameEdge, cameBeyond, dispatched, shareNo, farNo, zoneLeft; int roadFar;
    float closeLeast; ULONGLONG sampleAt;
} g_lwMeet;
/* A patrol called along the road to a player on it (co_lw_meet.h): each player's next call and side in turn; the
 * player + 1 a patrol was called to; the road nodes around the player; a patrol's stand beyond the band. */
static struct { ULONGLONG at[CO_LW_MEET_PLAYERS], tickAt; uint8_t turn[CO_LW_MEET_PLAYERS], begun[CO_LW_MEET_PLAYERS]; uint32_t called, none, came, start; } g_lwCall;
static uint8_t g_lwCallOf[LW_SQUADS_MAX], g_lwCallHops[LW_NODES_MAX], g_lwCallSide[LW_NODES_MAX];
static uint16_t g_lwCallUp[LW_NODES_MAX], g_lwCallQueue[LW_NODES_MAX];
static ULONGLONG g_lwZoneSince[LW_SQUADS_MAX], g_lwZoneNo[LW_SQUADS_MAX];
static void LwZoneClear(int idx) { g_lwZoneSince[idx] = g_lwZoneNo[idx] = 0; }
static ULONGLONG g_lwMeetTestNow; /* tests: the hold's clock */
static ULONGLONG LwMeetNow(void) { return g_lwMeetTestNow ? g_lwMeetTestNow : GetTickCount64(); }

static float LwRoadCost(int a, int b)
{
    return g_lwPathRoad && (!g_lwNodeKind[a] || !g_lwNodeKind[b]) ? LW_ROAD_OFF_COST : 1.0f;
}
/* Too few road nodes (a level whose roads have no points) and the road patrols would crowd them: none then. */
/* 0 no road patrol, 1 one of any road, 2 one of the arterials and their spurs. */
static int LwRoadSquad(int index)
{
    const int tail = index >= g_lwRoadTail[0] && index < g_lwRoadTail[1];
    if (g_lwRoadNodes < LW_ROAD_NODES_MIN || (!tail && index % LW_ROAD_SHARE_OF)) return 0;
    return g_lwArtNodes >= LW_ROAD_ART_MIN && (tail ? index - g_lwRoadTail[0] : index / LW_ROAD_SHARE_OF) % LW_ROAD_ART_OF == 0 ? 2 : 1;
}
/* The road patrols' indices in turn: the one after `i` (-1: the first). */
static int LwRoadNext(int i)
{
    const int next = i + 1;
    if (next >= g_lwRoadTail[0] && next < g_lwRoadTail[1]) return next;
    const int up = (next + LW_ROAD_SHARE_OF - 1) / LW_ROAD_SHARE_OF * LW_ROAD_SHARE_OF;
    return next < g_lwRoadTail[0] && up > g_lwRoadTail[0] ? g_lwRoadTail[0] : up;
}
/* LwPopulate: a road patrol has half a group's units, so as many again are added after the `base` squads, and the
 * roads hold twice the patrols for the units they held. The squads added. */
static int LwRoadTailSet(int base)
{
    const int extra = g_lwRoadNodes >= LW_ROAD_NODES_MIN ? (base + LW_ROAD_SHARE_OF - 1) / LW_ROAD_SHARE_OF : 0;
    g_lwRoadTail[0] = extra ? base : LW_SQUADS_MAX; g_lwRoadTail[1] = extra ? base + extra : LW_SQUADS_MAX;
    return extra;
}
/* A squad's units of its group's `size`: a road patrol's are few (co_lw_meet.h). */
static int LwRoadMembers(int index, int size) { return LwRoadSquad(index) ? co_lw_meet_road_members(size) : size; }
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
    g_lwCallOf[q - g_lwSquads] = 0; /* the way it was called along is walked */
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

/* A road left as its chord, once a level each (the first LW_UNFIT_NOTES): why, and where its points begin and end.
 * 1 when both lie outside the playable disc: the map's border ring, which no squad walks and no player reaches. */
static uint32_t LwRoadUnfit(uint32_t edge, uint32_t nth, const char* why, const float* first, const float* last, const uint64_t* ends)
{
    const float r = g_lwWorld.radius, d0 = sqrtf(first[0] * first[0] + first[1] * first[1]), d1 = sqrtf(last[0] * last[0] + last[1] * last[1]);
    const uint32_t out = d0 > r && d1 > r;
    if (nth <= LW_UNFIT_NOTES)
        Note("living world: roads: edge %u is left as it was: %s; its points run from (%.0f, %.0f) to (%.0f, %.0f), %.0f and %.0f m from the centre (playable to %.0f m: %s); "
            "end nodes %llx / %llx%s", edge, why, (double)first[0], (double)first[1], (double)last[0], (double)last[1], (double)d0, (double)d1, (double)r,
            out ? "both outside" : "not both outside", (unsigned long long)ends[0], (unsigned long long)ends[1], nth == LW_UNFIT_NOTES ? " (the last such line this level)" : "");
    return out;
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
    uint32_t roads = 0, noPoints = 0, off = 0, offOut = 0, full = 0, artRoads = 0; double km = 0, artKm = 0;
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
        const float* first = g_lwRoads.pts[range[0]]; const float* last = g_lwRoads.pts[range[1] - 1];
        if (!fit) { off++; offOut += LwRoadUnfit(e, off, "an end is no node of this level, or has no cell", first, last, ends); continue; }
        if (LwDist2(first, g_lwNodes[a].x, g_lwNodes[a].y) > LwDist2(first, g_lwNodes[b].x, g_lwNodes[b].y)) { int t = a; a = b; b = t; }
        if (LwDist2(first, g_lwNodes[a].x, g_lwNodes[a].y) > LW_ROAD_END * LW_ROAD_END || LwDist2(last, g_lwNodes[b].x, g_lwNodes[b].y) > LW_ROAD_END * LW_ROAD_END)
        { off++; offOut += LwRoadUnfit(e, off, "its points end more than 100 m from its nodes", first, last, ends); continue; }
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
        "%u without points, %u whose points or nodes do not fit (%u of them with both ends outside the playable disc), %u for room; one squad in %d keeps to roads",
        roads, edges, km, g_lwRoadNodes, (double)CO_LW_ROADS_STRETCH, points, noPoints, off, offOut, full, LW_ROAD_SHARE_OF);
    Note("living world: roads: arterials %u roads (%.0f km, %d nodes)%s; spurs %u of %u walked (%u nodes), %u too short for a node, %u that do not fit; "
        "%s", artRoads, artKm, g_lwArtNodes, built ? "" : " (the roads' build has none for this level)", spurs, g_spurs.level == g_world.levels ? g_spurs.count : 0,
        spurNodes, spurShort, spurOff, g_lwArtNodes >= LW_ROAD_ART_MIN ? "every second road patrol keeps to the arterials and their spurs" : "no arterial patrols");
}

/* Road patrols wait on the roads' build: a research build turns them off with HELLFORGE_LW_ROAD_PATROLS=0 (or on with
 * 1) for a run; a package build takes the default. */
#define LW_ROAD_PATROLS_DEFAULT 1
static int g_lwRoadPatrolsOn = -1;
static int LwRoadPatrolsOn(void)
{
    if (g_lwRoadPatrolsOn < 0) { char t[4] = ""; const DWORD n = GetEnvironmentVariableA("HELLFORGE_LW_ROAD_PATROLS", t, sizeof t); g_lwRoadPatrolsOn = co_lw_view_switch(t, n, LW_ROAD_PATROLS_DEFAULT); }
    return g_lwRoadPatrolsOn;
}
/* Test command "roadpatrols N": the roads are read once a mission, so the switch counts from the next one. */
static void LwRoadPatrolsCmd(int on)
{
    g_lwRoadPatrolsOn = on != 0;
    Note("living world: test: road patrols %s from the next mission's start (this mission keeps its %d road nodes)", g_lwRoadPatrolsOn ? "on" : "off", g_lwRoadNodes);
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
    if (q->state != LW_OFFLINE)
    {
        if (mine) { g_lwPatrols.hold1 = 0; g_lwPatrols.served++; }
        if (g_lwMeetSince[idx]) { g_lwMeetSince[idx] = 0; g_lwMeet.online++; }
        return;
    }
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
    if (near2 < LW_R_ON * LW_R_ON) g_lwMeetMark[idx] = 0; /* a near squad now: the distance rule and the far share as for any */
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

/* ---- patrols on their way to a player come online where they are not seen (co_lw_meet.h) --------------------------- */
static int LwMeetOn(void) { return !g_lwMeetOff && !g_lwPatrolsOld && g_lwPatrols.mission == g_lwMission; }
static int LwMeetWaits(const LwSquad* q) { return q->state == LW_OFFLINE && q->intent == LW_PATROL && g_lwMeetSince[q - g_lwSquads] != 0; }
/* A patrol on a player's road, or waiting at the ring's edge: every tick, placed before room, room made for it. */
static int LwMeetPriority(const LwSquad* q)
{
    return q->state == LW_OFFLINE && q->intent == LW_PATROL && (g_lwMeetSince[q - g_lwSquads] != 0 || (g_lwMeetMark[q - g_lwSquads] & LW_MEET_ON_ROAD));
}
static int LwMeetNote(void) { return g_lwMeet.notes < LW_MEET_NOTES ? (int)++g_lwMeet.notes : 0; }
/* Test command "meet 0|1": these rules alone, off or on, for a before value under the same patrol rules. */
static void LwMeetCmd(int on)
{
    g_lwMeetOff = on == 0;
    memset(g_lwMeetMark, 0, sizeof g_lwMeetMark); memset(g_lwMeetSince, 0, sizeof g_lwMeetSince); memset(&g_lwMeetHolds, 0, sizeof g_lwMeetHolds);
    memset(g_lwCallOf, 0, sizeof g_lwCallOf); memset(g_lwZoneSince, 0, sizeof g_lwZoneSince); memset(g_lwZoneNo, 0, sizeof g_lwZoneNo); memset(g_lwCall.at, 0, sizeof g_lwCall.at); memset(g_lwCall.begun, 0, sizeof g_lwCall.begun);
    Note("living world: test: a patrol on its way to a player %s (meet %d)", g_lwMeetOff ? "has no rule of its own: it comes online only where it stands unseen in the bands" :
        "comes online at the last hidden point of its way or beyond what is drawn, a road patrol waits at the ring's edge, and one is called to a player's road in turn", !g_lwMeetOff);
}
/* How far a player on another machine could draw squad q: through a 4x scope, the narrowest view in the game's data
 * (its own view is not known here). With CO_LW_MEET_PEER_SCOPED 0 it is taken to look unscoped, as the host's band does. */
static float LwMeetPeerDrawn(LwSquad* q)
{
    const float host = LwSquadBand(q);
    return CO_LW_MEET_PEER_SCOPED && q->scoped > host ? q->scoped : host;
}
static float LwMeetDrawn(LwSquad* q, int i) { return LwSightIsHost(i) ? LwSquadBand(q) : LwMeetPeerDrawn(q); }
/* The players who may refuse a patrol's point: those who could draw the squad there, with the band's margin. A group
 * whose units were never measured has a guessed band, and a squad without a group none: every player counts for
 * those, as for any other squad. NULL: all again. */
static void LwMeetSight(LwSquad* q)
{
    g_lwSightHost = g_lwSightPeer = 0;
    if (q == NULL || !LwMeetOn() || q->intent != LW_PATROL || q->held || q->pulled || q->cycled) return;
    if (LwSquadBand(q) < LW_R_ON || !q->measured) return;
    g_lwSightHost = LwSquadBand(q) + LW_FAR_MARGIN;
    g_lwSightPeer = LwMeetPeerDrawn(q) + LW_FAR_MARGIN;
    if (g_lwSightPeer > LW_SIGHT) g_lwSightPeer = LW_SIGHT;
}
/* How far squad q stands beyond what any player could draw of it, the margin included; negative inside a player's
 * band. A vehicle counts where it will be in CO_LW_VIEW_LEAD_S as well, so a squad asked for ahead of it is online
 * before the vehicle's band reaches it. */
static float LwMeetOver(LwSquad* q)
{
    float over = 3.0e38f;
    for (int i = 0; i < g_lwWorld.playersAt; i++)
    {
        float d = sqrtf(LwDist2(g_lwWorld.players[i], q->pos[0], q->pos[1])), lead[2], to = LwMeetDrawn(q, i) + LW_FAR_MARGIN;
        if (i < CO_LW_VIEW_PLAYERS && co_lw_view_lead(&g_lwView, (uint32_t)i, g_lwWorld.players[i], lead))
        { const float l = sqrtf(LwDist2(lead, q->pos[0], q->pos[1])); if (l < d) d = l; }
        if (to > LW_SIGHT) to = LW_SIGHT;
        if (d - to < over) over = d - to;
    }
    return over;
}
/* The look: the near sight test where the squad stands. Its refusals are no placement refusals (the 30 s line).
 * `beyond`: beyond every player's band, where the scoped test skips them all, any player with a line counts. */
static int LwMeetLookAtPos(LwSquad* q, int beyond)
{
    float p[3] = {q->pos[0], q->pos[1], 0};
    if (!LwGround(p[0], p[1], &p[2])) return 0;
    const uint32_t seen = g_lwWorld.seen;
    LwMeetSight(beyond ? NULL : q);
    const int hidden = LwHidden(p);
    LwMeetSight(NULL);
    g_lwWorld.seen = seen;
    return hidden;
}
static int (*g_lwMeetLook)(LwSquad*, int) = LwMeetLookAtPos;
static int (*g_lwMeetAhead)(LwSquad*) = LwAboutToBeSeen;
/* The road on from node `at`, come to from `from`: its one other road neighbour, -1 at a junction or an end. */
static int LwMeetRoadOn(int at, int from)
{
    int next = -1;
    for (int k = 0; k < g_lwNodes[at].degree; k++)
    {
        const int o = g_lwNodes[at].next[k];
        if (o == from || !g_lwNodeKind[o]) continue;
        if (next >= 0) return -1;
        next = o;
    }
    return next;
}
/* A road patrol's way: the road behind it to the next junction (4 nodes at most: a player who follows it stands
 * there), where it stands, its plan, and the road on from the plan's end to the next junction (14 nodes ahead at
 * most: a plan is 400 to 1200 m, the reach up to 960 m). 1 when a player stands within CO_LW_MEET_WAY of it. */
#define LW_MEET_WAY_BACK 4
#define LW_MEET_WAY_AHEAD 14
static int LwMeetOnRoad(const LwSquad* q)
{
    float way[LW_MEET_WAY_BACK + 2 + LW_MEET_WAY_AHEAD][2]; int back[LW_MEET_WAY_BACK], nb = 0, n = 0;
    int at = q->node, from = q->path[q->pathAt];
    while (nb < LW_MEET_WAY_BACK) { const int o = LwMeetRoadOn(at, from); if (o < 0) break; back[nb++] = o; from = at; at = o; }
    while (nb > 0) { nb--; way[n][0] = g_lwNodes[back[nb]].x; way[n++][1] = g_lwNodes[back[nb]].y; }
    way[n][0] = g_lwNodes[q->node].x; way[n++][1] = g_lwNodes[q->node].y;
    way[n][0] = q->pos[0]; way[n++][1] = q->pos[1];
    at = q->node; from = -1;
    for (int k = q->pathAt, ahead = 0; ahead < LW_MEET_WAY_AHEAD; ahead++)
    {
        const int o = k < q->pathLen ? q->path[k++] : LwMeetRoadOn(at, from);
        if (o < 0) break;
        way[n][0] = g_lwNodes[o].x; way[n++][1] = g_lwNodes[o].y; from = at; at = o;
    }
    for (int i = 0; i < g_lwWorld.playersAt; i++) if (co_lw_meet_on_way((const float (*)[2])way, n, g_lwWorld.players[i], CO_LW_MEET_WAY)) return 1;
    return 0;
}
/* Each visit of an offline squad, after its walk: how it asks this tick (co_lw_meet_act). A patrol between the ring
 * and the draw band on its way to a player looks where it stands, remembers a hidden point, and steps back to the
 * last one once it is seen. `busy`: no look this tick (a request is out, or its point was refused a moment ago). */
static int LwMeetApproach(LwSquad* q, ULONGLONG now, int busy)
{
    const int idx = (int)(q - g_lwSquads); uint8_t* mark = &g_lwMeetMark[idx]; CoLwMeetCover* c = &g_lwMeetCover[idx];
    *mark &= LW_MEET_ON_ROAD | LW_MEET_NEARING; /* the online marks are a past life's */
    if (!LwMeetOn() || q->intent != LW_PATROL || q->held || q->pulled || q->cycled || !q->survivors || g_lwWorld.playersAt <= 0 || q->pathAt >= q->pathLen)
    {
        /* an errand takes the squad: what it had as a patrol on a player's road ends here, not at the errand's end */
        if (q->intent != LW_PATROL && (g_lwMeetSince[idx] || (*mark & LW_MEET_ON_ROAD))) { g_lwMeetSince[idx] = 0; co_lw_meet_hold_end(&g_lwMeetHolds, idx); g_lwMeet.dispatched++; }
        if (q->intent != LW_PATROL) g_lwCallOf[idx] = 0;
        *mark = 0; c->known = 0; c->steps = 0; LwZoneClear(idx);
        return CO_LW_MEET_WALK;
    }
    const float rMin = g_lwWorld.rMin > 0 ? g_lwWorld.rMin : LW_R_MIN, reach = LwSquadBand(q) + LW_FAR_MARGIN + CO_LW_MEET_OUTER;
    const float d2 = LwNearestPlayer2(q->pos[0], q->pos[1]), out = LW_SIGHT + CO_LW_VIEW_LEAD_MAX + CO_LW_MEET_OUTER + CO_LW_MEET_NOTICE;
    if (d2 >= out * out) { *mark = 0; c->known = 0; c->steps = 0; LwZoneClear(idx); return CO_LW_MEET_WALK; } /* most squads: no band reaches them */
    const float over = LwMeetOver(q);
    const int road = LwRoadSquad(idx), beyond = over >= 0.0f;
    if (!beyond || over >= CO_LW_MEET_OUTER) LwZoneClear(idx); /* the players moved: it stands no longer where it was refused */
    if (d2 < rMin * rMin || over >= CO_LW_MEET_OUTER)
    {
        /* still out, its way leading to a player: the far share keeps its room from here, before it asks */
        *mark = d2 >= rMin * rMin && over < CO_LW_MEET_OUTER + CO_LW_MEET_NOTICE && road && LwMeetOnRoad(q) ? LW_MEET_NEARING : 0;
        c->known = 0; c->steps = 0;
        return CO_LW_MEET_WALK;
    }
    const LwNode* t = &g_lwNodes[q->path[q->pathAt]];
    const float dx = t->x - q->pos[0], dy = t->y - q->pos[1], len = sqrtf(dx * dx + dy * dy), k = len > 10.0f ? 10.0f / len : 1.0f;
    const float to[2] = {q->pos[0] + dx * k, q->pos[1] + dy * k};
    const int onRoad = road && LwMeetOnRoad(q);
    *mark = onRoad ? LW_MEET_ON_ROAD : 0;
    if (!onRoad) LwZoneClear(idx);
    if (!onRoad && !co_lw_meet_enters(q->pos, to, (const float (*)[3])g_lwWorld.players, g_lwWorld.playersAt, reach)) return CO_LW_MEET_WALK;
    if (busy || now < g_lwMeetLookAt[idx] || g_lwMeet.looksTick >= LW_MEET_LOOKS) return CO_LW_MEET_WALK;
    g_lwMeetLookAt[idx] = now + (onRoad ? CO_LW_MEET_LOOK_MS : CO_LW_MEET_LOOK_SLOW_MS); g_lwMeet.looksTick++; g_lwMeet.looks++;
    const int hidden = g_lwMeetLook(q, beyond);
    /* a point beyond the band is no cover to step back to: nobody draws the squad there, hidden or not */
    if (hidden && !beyond) co_lw_meet_cover_set(c, q->pos, q->node, q->path[q->pathAt], q->pathAt, now);
    const float d = sqrtf(d2);
    const int cover = !hidden && !beyond && co_lw_meet_cover_ok(c, q->pos, q->path, q->pathAt, q->pathLen, (const float (*)[3])g_lwWorld.players, g_lwWorld.playersAt, rMin + CO_LW_MEET_EDGE, now);
    const int ahead = hidden && onRoad && d >= LW_R_ON && !beyond && g_lwMeetAhead(q);
    const int act = co_lw_meet_act(onRoad, d, rMin, LW_R_ON, over, hidden, ahead, cover, q->measured);
    if (act == CO_LW_MEET_ASK_COVER)
    {
        q->pos[0] = c->at[0]; q->pos[1] = c->at[1]; q->pathAt = c->pathAt;
        if (q->node != c->node) { q->node = c->node; q->prevValid = 0; }
        c->known = 0; c->steps++; g_lwMeetSince[idx] = 0; g_lwMeet.covered++;
    }
    else if (act == CO_LW_MEET_ASK) g_lwMeet.edge++;
    else if (act == CO_LW_MEET_ASK_BEYOND) g_lwMeet.beyond++;
    /* seen where it stands inside the online radius, and nowhere to step back to: the near band's request would be
     * placed (a millisecond at 8 players) only to be refused for sight, so it waits for the next look */
    else if (!hidden && d < LW_R_ON) q->retry = now + (onRoad ? CO_LW_MEET_LOOK_MS : CO_LW_MEET_LOOK_SLOW_MS);
    return act;
}
/* The squads' tick, a request beyond the band refused, whatever for (the far share, the spacing from a squad just
 * requested, the bubble's room, a waiting squad's points): the patrol stands where it is (LwMeetHold) while the
 * refusals last, CO_LW_MEET_ZONE_WAIT_MS at most. Walking on, it would enter the band offline behind the squad
 * that was served. The far share's refusals are counted. */
static void LwMeetRefused(const LwSquad* q, int meet, const char* why, ULONGLONG now)
{
    const int idx = (int)(q - g_lwSquads);
    if (meet != CO_LW_MEET_ASK_BEYOND || q->state != LW_OFFLINE) return;
    if (strncmp(why, "the far share", 13) == 0) g_lwMeet.farNo++;
    if (!g_lwZoneSince[idx]) g_lwZoneSince[idx] = now;
    if (now < g_lwZoneSince[idx] + CO_LW_MEET_ZONE_WAIT_MS) g_lwZoneNo[idx] = now; /* waited out: it walks on, by the rules for cover */
}
static int LwCallInDisc(int node) { return g_lwNodes[node].x * g_lwNodes[node].x + g_lwNodes[node].y * g_lwNodes[node].y <= g_lwWorld.radius * g_lwWorld.radius; }
/* The road nodes within CO_LW_MEET_CALL_HOPS of `root`, breadth first: each one's hops, its node toward the root,
 * and the root's neighbour (its place in the root's list) its way comes in by. */
static void LwCallTree(int root)
{
    int head = 0, tail = 0;
    memset(g_lwCallHops, 255, sizeof g_lwCallHops);
    g_lwCallHops[root] = 0; g_lwCallSide[root] = 255; g_lwCallQueue[tail++] = (uint16_t)root;
    while (head < tail)
    {
        const int at = g_lwCallQueue[head++];
        if (g_lwCallHops[at] >= CO_LW_MEET_CALL_HOPS) continue;
        for (int k = 0; k < g_lwNodes[at].degree; k++)
        {
            const int o = g_lwNodes[at].next[k];
            if (!g_lwNodeKind[o] || g_lwCallHops[o] != 255 || !LwCallInDisc(o)) continue;
            g_lwCallHops[o] = (uint8_t)(g_lwCallHops[at] + 1); g_lwCallUp[o] = (uint16_t)at; g_lwCallSide[o] = at == root ? (uint8_t)k : g_lwCallSide[at];
            g_lwCallQueue[tail++] = (uint16_t)o;
        }
    }
}
/* The road node a player stands by: the nearest within CO_LW_MEET_CALL_NEAR one of whose stretches passes within
 * CO_LW_MEET_WAY of the player (a patrol walking it is then on the player's road). -1 with none. */
static int LwCallRoot(const float* p)
{
    int best = -1; float bd = CO_LW_MEET_CALL_NEAR * CO_LW_MEET_CALL_NEAR;
    for (int i = g_lwNodeCount - g_lwRoadNodes; i < g_lwNodeCount; i++)
    {
        const float d = LwDist2(p, g_lwNodes[i].x, g_lwNodes[i].y);
        if (d < bd && LwCallInDisc(i)) { bd = d; best = i; }
    }
    for (int k = 0; best >= 0 && k < g_lwNodes[best].degree; k++)
    {
        const int o = g_lwNodes[best].next[k];
        const float way[2][2] = {{g_lwNodes[best].x, g_lwNodes[best].y}, {g_lwNodes[o].x, g_lwNodes[o].y}};
        if (g_lwNodeKind[o] && co_lw_meet_on_way(way, 2, p, CO_LW_MEET_WAY)) return best;
    }
    return -1;
}
/* A patrol that may be called: offline on a stretch of road, on no errand, not called or on its way to a player
 * already, its group's size measured, and beyond the zone it comes online in for every player (it walks into the
 * zone, and asks there). */
static int LwCallFree(LwSquad* q)
{
    const int idx = (int)(q - g_lwSquads);
    if (!(q->state == LW_OFFLINE && q->intent == LW_PATROL && !q->held && !q->pulled && !q->cycled && q->survivors && q->pathAt < q->pathLen && !(q->retry && LwMeetNow() < q->retry) &&
        !g_lwCallOf[idx] && !g_lwMeetMark[idx] && !g_lwMeetSince[idx] && LwMeetOver(q) >= CO_LW_MEET_OUTER)) return 0;
    return q->measured; /* read after LwMeetOver filled the band's cache: a guessed band never asks beyond it, so the call would be lost */
}
/* The called patrol's plan: from the end of its stretch nearer the player (an about turn when that is behind it)
 * along the tree to the player's node, and the road on past it. */
static void LwCallSend(LwSquad* q, int toward, int root)
{
    uint16_t path[LW_PATH_MAX]; int n = 0, at = toward;
    if (toward == q->node) { q->node = q->path[q->pathAt]; q->prevValid = 0; }
    path[n++] = (uint16_t)toward;
    while (at != root && n < LW_PATH_MAX) { at = g_lwCallUp[at]; path[n++] = (uint16_t)at; }
    int from = n >= 2 ? path[n - 2] : q->node;
    for (int k = 0; k < CO_LW_MEET_CALL_PAST && n < LW_PATH_MAX; k++)
    {
        const int o = LwMeetRoadOn(at, from);
        if (o < 0 || !LwCallInDisc(o)) break;
        path[n++] = (uint16_t)o; from = at; at = o;
    }
    memcpy(q->path, path, (size_t)n * sizeof path[0]); q->pathLen = (uint8_t)n; q->pathAt = 0; q->planKind = 0;
}
/* One call for player i standing by road node `root`: the free patrol the fewest road nodes out, from the side whose
 * turn it is when one stands there. Its index, -1 with none within reach. */
static int LwCallFor(int i, int root)
{
    int sides[16], ns = 0, best = -1, bestHops = -1, bestOn = 0, bestToward = -1;
    const float reach = (float)(CO_LW_MEET_CALL_HOPS + 2) * CO_LW_ROADS_STRETCH; /* no patrol the tree holds stands farther */
    for (int k = 0; k < g_lwNodes[root].degree; k++) if (g_lwNodeKind[g_lwNodes[root].next[k]]) sides[ns++] = k;
    if (ns == 0) return -1;
    const int want = sides[(g_lwCall.turn[i] + i) % ns]; /* players standing together start on different sides */
    LwCallTree(root);
    for (int s = LwRoadNext(-1); s < g_lwWorld.count; s = LwRoadNext(s))
    {
        LwSquad* q = &g_lwSquads[s];
        if (LwDist2(g_lwWorld.players[i], q->pos[0], q->pos[1]) > reach * reach || !LwRoadSquad(s) || !LwCallFree(q)) continue;
        const int a = q->node, b = q->path[q->pathAt], toward = g_lwCallHops[b] < g_lwCallHops[a] ? b : a, hops = g_lwCallHops[toward];
        if (hops == 255 || toward == root) continue;
        const int on = g_lwCallSide[toward] == want;
        if (co_lw_meet_call_better(hops, on, bestHops, bestOn)) { best = s; bestHops = hops; bestOn = on; bestToward = toward; }
    }
    if (best < 0) return -1;
    LwSquad* q = &g_lwSquads[best];
    LwCallSend(q, bestToward, root);
    g_lwCallOf[best] = (uint8_t)(i + 1); g_lwCall.turn[i]++; g_lwCall.called++;
    if (LwMeetNote()) Note("living world: road patrol %d is called to player %d's road: %d road nodes out at (%.0f, %.0f), %.0f m from them, to walk in %s%s", best, i + 1, bestHops,
        (double)q->pos[0], (double)q->pos[1], (double)sqrtf(LwDist2(g_lwWorld.players[i], q->pos[0], q->pos[1])), bestOn ? "from the side whose turn it is" : "from another side (none stood on the one whose turn it is)",
        g_lwMeet.notes == LW_MEET_NOTES ? " (the last such line this mission)" : "");
    return best;
}
static int LwMeetRoadFar(void);
/* The time to player i's next call after squad q was called: q's far walk for that player, over the share. */
static uint32_t LwCallPeriod(LwSquad* q, int i, int roadPlayers)
{
    const float walk = LwMeetDrawn(q, i) + LW_FAR_MARGIN + 0.5f * CO_LW_MEET_OUTER - LW_R_ON;
    return co_lw_meet_call_ms((float)LwMeetRoadFar(), CO_LW_MEET_ROAD_MEMBERS, roadPlayers, walk);
}
/* Once a second: one player by a road whose time has come has a patrol called (co_lw_meet_call_ms apart for each),
 * the players taken in turn. The offline walk is free, so only the far share bounds what a road shows. */
static void LwCallTick(ULONGLONG now)
{
    if (now - g_lwCall.tickAt < 1000) return;
    g_lwCall.tickAt = now;
    if (!LwMeetOn() || g_lwRoadNodes < LW_ROAD_NODES_MIN) return;
    int root[CO_LW_MEET_PLAYERS], np = g_lwWorld.playersAt < CO_LW_MEET_PLAYERS ? g_lwWorld.playersAt : CO_LW_MEET_PLAYERS, roadPlayers = 0;
    for (int i = 0; i < np; i++) { root[i] = LwCallRoot(g_lwWorld.players[i]); roadPlayers += root[i] >= 0; }
    /* a player's first call waits its part of a period, so the players' calls are spread over it and the share
     * meets them one at a time (all at once, 8 players sent 32 units at a kept half of 23) */
    for (int i = 0; i < np; i++)
    {
        if (root[i] < 0 || g_lwCall.begun[i]) continue;
        LwSquad unit; memset(&unit, 0, sizeof unit);
        g_lwCall.begun[i] = 1;
        g_lwCall.at[i] = now + co_lw_meet_call_first(LwCallPeriod(&unit, i, roadPlayers), i, np);
    }
    for (int k = 0; k < np; k++)
    {
        const int i = (int)((g_lwCall.start + (uint32_t)k) % (uint32_t)np);
        if (root[i] < 0 || now < g_lwCall.at[i]) continue;
        g_lwCall.start = (uint32_t)i + 1;
        const int s = LwCallFor(i, root[i]);
        if (s < 0) { g_lwCall.none++; g_lwCall.at[i] = now + 5000; return; }
        g_lwCall.at[i] = now + LwCallPeriod(&g_lwSquads[s], i, roadPlayers);
        return;
    }
}
/* Each hold's bubble now (the players move, and the bubbles with them). */
static void LwMeetHoldBubbles(int* bubble, ULONGLONG now)
{
    for (int k = 0; k < CO_LW_MEET_HOLDS; k++)
    {
        const int i = g_lwMeetHolds.idx[k];
        bubble[k] = now >= g_lwMeetHolds.until[k] || i < 0 || i >= g_lwWorld.count ? -1 : g_lwDensity.on == 1 ? LwBubbleOf(g_lwSquads[i].pos[0], g_lwSquads[i].pos[1]) : 0;
    }
}
static int LwMeetBubble(const LwSquad* q) { return g_lwDensity.on == 1 ? LwBubbleOf(q->pos[0], q->pos[1]) : 0; }
/* LwRoomFor: the units held for patrols in the requester's bubble. */
static uint32_t LwMeetHeld(int requester)
{
    int bubble[CO_LW_MEET_HOLDS]; const ULONGLONG now = LwMeetNow();
    if (requester < 0 || requester >= g_lwWorld.count) return 0;
    LwMeetHoldBubbles(bubble, now);
    return co_lw_meet_held(&g_lwMeetHolds, bubble, requester, LwMeetBubble(&g_lwSquads[requester]), now);
}
/* The road patrols' units online or requested in bubble `b`, and that bubble's budget. */
static int LwMeetRoadUnits(int b, int* budget)
{
    int units = 0;
    for (int i = LwRoadNext(-1); i < g_lwWorld.count; i = LwRoadNext(i))
    {
        const LwSquad* q = &g_lwSquads[i];
        if (LwRoadSquad(i) && LwSquadUnits(q) && LwMeetBubble(q) == b) units += LwSquadUnits(q);
    }
    *budget = g_lwDensity.on == 1 && b >= 0 && b < g_lwDensity.count ? g_lwDensity.budget[b] : g_lwWorld.eff;
    return units;
}
/* LwFillerCandidate: a road patrol near a player stays, up to the road patrols' share of its bubble. */
static int LwMeetKeeps(const LwSquad* q)
{
    const int road = LwRoadSquad((int)(q - g_lwSquads));
    if (!road || !LwMeetOn()) return 0;
    int budget = 0; const int units = LwMeetRoadUnits(LwMeetBubble(q), &budget);
    return co_lw_meet_keeps(road, LwNearestPlayer2(q->pos[0], q->pos[1]), LW_R_ON, units, budget);
}
/* LwTickSums: the far units of patrols that came online on a player's road (-1: a new sum). */
static void LwMeetFarCount(int idx, int units)
{
    if (idx < 0) g_lwMeet.roadFar = 0;
    else if (g_lwMeetMark[idx] & LW_MEET_FAR_ROAD) g_lwMeet.roadFar += units;
}
/* The far units patrols on a player's road may hold: their own part of the budgets under the density rules, half
 * the far share without them (as the reserve kept it). */
static int LwMeetRoadFar(void)
{
    return g_lwDensity.on == 1 ? co_lw_meet_road_far(LwUnitBudget()) : (int)LwFarShareLimit() / CO_LW_MEET_FAR_RESERVE;
}
/* The far share for squad q's request: a patrol on a player's road has the road patrols' share, and the others
 * keep to half the far share while a road patrol is on its way to a player. */
static int LwMeetFarRoom(const LwSquad* q, int meet, int farUnits)
{
    const int onRoad = meet && (g_lwMeetMark[q - g_lwSquads] & LW_MEET_ON_ROAD);
    int reserve = 0; /* only while a road patrol is on its way to a player: the landscape has the whole share otherwise */
    for (int i = LwRoadNext(-1); i < g_lwWorld.count && !reserve; i = LwRoadNext(i)) reserve = g_lwSquads[i].state == LW_OFFLINE && (g_lwMeetMark[i] & (LW_MEET_ON_ROAD | LW_MEET_NEARING));
    reserve = reserve && LwMeetOn() && g_lwRoadNodes >= LW_ROAD_NODES_MIN && g_lwDensity.on == 1;
    return co_lw_meet_far_room(farUnits, g_lwMeet.roadFar, q->survivors, (int)LwFarShareLimit(), onRoad, reserve, g_lwDensity.on == 1 ? LwMeetRoadFar() : 0);
}
/* After a squad's request. One that came online on its way in keeps its place while it walks in (LwMeetStays). */
static void LwMeetAsked(LwSquad* q, int meet, int farAsk)
{
    const int idx = (int)(q - g_lwSquads);
    if (q->state == LW_OFFLINE) return;
    co_lw_meet_hold_end(&g_lwMeetHolds, idx);
    if (g_lwCallOf[idx]) { g_lwCallOf[idx] = 0; g_lwCall.came++; }
    LwZoneClear(idx);
    memset(g_lwMeetGap[idx], 255, sizeof g_lwMeetGap[idx]); memset(g_lwMeetWatch[idx], 0, sizeof g_lwMeetWatch[idx]); g_lwMeetCame[idx] = (uint8_t)meet;
    const uint8_t onRoad = g_lwMeetMark[idx] & LW_MEET_ON_ROAD;
    g_lwMeetMark[idx] = 0;
    if (!meet) return;
    g_lwMeetMark[idx] = (uint8_t)(LW_MEET_CAME | (onRoad && farAsk ? LW_MEET_FAR_ROAD : 0));
    if (meet == CO_LW_MEET_ASK_COVER) g_lwMeet.cameCover++; else if (meet == CO_LW_MEET_ASK_BEYOND) g_lwMeet.cameBeyond++; else g_lwMeet.cameEdge++;
    if (LwMeetNote()) Note("living world: %s %d comes online at (%.0f, %.0f), %.0f m from the nearest player, on its way to one: %s%s", LwRoadSquad(idx) ? "road patrol" : "patrol", idx,
        (double)q->pos[0], (double)q->pos[1], (double)sqrtf(LwNearestPlayer2(q->pos[0], q->pos[1])), meet == CO_LW_MEET_ASK_COVER ? "back at the last point of its way that was out of sight" :
        meet == CO_LW_MEET_ASK_BEYOND ? "beyond what is drawn" : "out of sight, its next stretch in view", g_lwMeet.notes == LW_MEET_NOTES ? " (the last such line this mission)" : "");
}
/* LwOnlineTick's distance rule: a squad that came online on its way in walks from beyond the widest band in play (a
 * player on another machine's, with two or more) to the offline radius, and beyond that radius it left after 10 s.
 * Ahead of a vehicle it was asked for up to the lead farther out, and a vehicle that stops must not lose it: the
 * distance has the lead, the time not (at the pace patrols walk, 2 m/s, it covers that walk too). */
static int LwMeetStays(LwSquad* q, float d2, ULONGLONG now)
{
    const float keep = (g_lwWorld.playersAt > 1 ? LwMeetPeerDrawn(q) : LwSquadBand(q)) + LW_FAR_MARGIN + CO_LW_MEET_OUTER + 50.0f, reach = keep + CO_LW_VIEW_LEAD_MAX;
    return (g_lwMeetMark[q - g_lwSquads] & LW_MEET_CAME) && now - q->since < co_lw_meet_stay_ms(keep, LW_R_OFF) && d2 < reach * reach;
}
/* Back toward the node it came from; its next plan from there may not lead straight back here, so the node ahead
 * becomes the one it "came from". */
static void LwMeetTurn(LwSquad* q, const char* why)
{
    const int idx = (int)(q - g_lwSquads); const uint16_t behind = q->node, ahead = q->path[q->pathAt];
    q->path[0] = behind; q->pathLen = 1; q->pathAt = 0; q->node = ahead;
    g_lwMeetSince[idx] = 0; g_lwMeet.turned++; g_lwCallOf[idx] = 0;
    if (LwMeetNote()) Note("living world: road patrol %d turns back at (%.0f, %.0f), %.0f m from the nearest player: %s%s", idx, (double)q->pos[0], (double)q->pos[1],
        (double)sqrtf(LwNearestPlayer2(q->pos[0], q->pos[1])), why, g_lwMeet.notes == LW_MEET_NOTES ? " (the last such line this mission)" : "");
}
/* LwWalk, offline, before a step to (nx, ny): 1 when the step is not taken. A road patrol that could not come online
 * on its way waits where its next step would take it into the ring no squad comes online in; a player who walks up
 * to it pushes it back along the stretch it came by; a minute without coming online and it turns. */
static int LwMeetHold(LwSquad* q, float nx, float ny)
{
    const int idx = (int)(q - g_lwSquads);
    if (!LwMeetOn() || q->intent != LW_PATROL || !LwRoadSquad(idx) || q->cycled || q->pulled || g_lwWorld.playersAt <= 0 || q->pathAt >= q->pathLen)
    { g_lwMeetSince[idx] = 0; return 0; }
    const float rMin = g_lwWorld.rMin > 0 ? g_lwWorld.rMin : LW_R_MIN, edge = rMin + CO_LW_MEET_EDGE, to[2] = {nx, ny};
    const ULONGLONG now = LwMeetNow();
    /* room is being made for it where its point stood: walking on it would be seen before the room is there */
    for (int k = 0; k < CO_LW_MEET_HOLDS; k++) if (g_lwMeetHolds.idx[k] == idx && now < g_lwMeetHolds.until[k]) return 1;
    /* refused the far share beyond the band: nobody draws it there, so it stands until the share has room; walking
     * on it would enter the band offline and be lost to the open road */
    if (co_lw_meet_zone_waits(g_lwZoneSince[idx], g_lwZoneNo[idx], now)) return 1;
    if (g_lwZoneNo[idx] && now >= g_lwZoneSince[idx] + CO_LW_MEET_ZONE_WAIT_MS) { g_lwMeet.zoneLeft++; g_lwZoneNo[idx] = 0; }
    const int enters = co_lw_meet_enters(q->pos, to, (const float (*)[3])g_lwWorld.players, g_lwWorld.playersAt, edge);
    if (!g_lwMeetSince[idx])
    {
        if (!enters) return 0;
        g_lwMeetSince[idx] = now; g_lwMeet.held++;
        if (LwMeetNote()) Note("living world: road patrol %d waits at the ring's edge at (%.0f, %.0f), %.0f m from the nearest player, to come online there%s", idx, (double)q->pos[0],
            (double)q->pos[1], (double)sqrtf(LwNearestPlayer2(q->pos[0], q->pos[1])), g_lwMeet.notes == LW_MEET_NOTES ? " (the last such line this mission)" : "");
        return 1;
    }
    const int next = co_lw_meet_next(g_lwMeetSince[idx], now, LwNearestPlayer2(q->pos[0], q->pos[1]), rMin);
    if (next == CO_LW_MEET_TURNS) { LwMeetTurn(q, "a minute at the ring's edge without coming online"); return 1; }
    if (next == CO_LW_MEET_BACK)
    {
        const float behind[2] = {g_lwNodes[q->node].x, g_lwNodes[q->node].y}; float out[2];
        if (!co_lw_meet_back(q->pos, behind, (const float (*)[3])g_lwWorld.players, g_lwWorld.playersAt, edge, out)) { LwMeetTurn(q, "a player came up to it and the stretch it came by lies inside the ring"); return 1; }
        q->pos[0] = out[0]; q->pos[1] = out[1]; g_lwMeet.back++;
        return 1;
    }
    if (!enters) { g_lwMeetSince[idx] = 0; return 0; } /* the player went: it walks on */
    return 1;
}
static LwSquad* (*g_lwMeetFiller)(const LwSquad*, float*) = LwPatrolFiller;
/* A request of a patrol with priority was refused: counted by reason. Its point stood and its bubble is full: the
 * farthest calm squad nobody sees leaves and the room is held for it, one hold and one swap in 5 s a bubble, never
 * beside an event squad's hold there, and only while the road patrols stay within their share of the bubble. */
static void LwMeetWhy(LwSquad* q, const char* why)
{
    const int idx = (int)(q - g_lwSquads);
    if (!LwMeetPriority(q)) return;
    const int room = LwRoomIsUnits(why);
    if (strncmp(why, "sight", 5) == 0) g_lwMeet.sight++; else if (room) g_lwMeet.room++; else if (strncmp(why, "within", 6) == 0) g_lwMeet.spaced++; else g_lwMeet.other++;
    const ULONGLONG now = LwMeetNow(); const int b = LwMeetBubble(q);
    if (!room || g_lwDensity.on != 1 || b < 0 || b >= EVT_MAX_PLAYERS || now - g_lwMeetSwapAt[b] < CO_LW_MEET_SWAP_MS) return;
    if (g_lwHold.idx >= 0 && g_lwHold.idx != idx && now < g_lwHold.until && LwHoldBubble(g_lwHold.idx, idx)) return;
    int bubble[CO_LW_MEET_HOLDS], budget = 0;
    LwMeetHoldBubbles(bubble, now);
    for (int k = 0; k < CO_LW_MEET_HOLDS; k++) if (bubble[k] == b) return; /* a hold runs there: its own, or another patrol's first */
    if (!co_lw_meet_road_room(LwMeetRoadUnits(b, &budget), q->survivors, budget)) { g_lwMeet.shareNo++; return; }
    g_lwMeetSwapAt[b] = now;
    float pickD;
    LwSquad* pick = g_lwMeetFiller(q, &pickD);
    if (pick == NULL) { g_lwMeet.noFiller++; return; }
    if (!co_lw_meet_hold(&g_lwMeetHolds, bubble, idx, q->survivors, b, now, 20000)) return;
    pick->forceOff = 2; g_lwMeet.gaveWay++;
    if (LwMeetNote()) Note("living world: squad %d (hidden, calm, %.0f m from the nearest player) goes offline to make room for road patrol %d, whose point %.0f m from a player stands%s",
        (int)(pick - g_lwSquads), (double)sqrtf(pickD), idx, (double)sqrtf(LwNearestPlayer2(q->pos[0], q->pos[1])), g_lwMeet.notes == LW_MEET_NOTES ? " (the last such line this mission)" : "");
}
/* The angle between player i's view and the road by it, in degrees (0 along it, 90 across); negative with no road
 * node within 100 m or no view read. */
static float LwMeetViewAngle(int i)
{
    float f[2]; int best = -1; float bd = 100.0f * 100.0f; const float* p = g_lwWorld.players[i];
    if (LwViewRead() == i) { f[0] = g_lwViewFwd[0]; f[1] = g_lwViewFwd[1]; } else if (!LwAimForward(i, f)) return -1.0f;
    for (int k = g_lwNodeCount - g_lwRoadNodes; k < g_lwNodeCount; k++) { const float d = LwDist2(p, g_lwNodes[k].x, g_lwNodes[k].y); if (d < bd) { bd = d; best = k; } }
    if (best < 0) return -1.0f;
    for (int k = 0; k < g_lwNodes[best].degree; k++)
    {
        const int o = g_lwNodes[best].next[k];
        if (!g_lwNodeKind[o]) continue;
        const float dx = g_lwNodes[o].x - g_lwNodes[best].x, dy = g_lwNodes[o].y - g_lwNodes[best].y, len = sqrtf(dx * dx + dy * dy);
        if (!(len > 1.0f)) continue;
        const float c = fabsf(dx * f[0] + dy * f[1]) / len;
        return acosf(c > 1.0f ? 1.0f : c) * 57.29578f;
    }
    return -1.0f;
}
/* Once a second: what each player has near and sees walking, and the road patrols that are offline close to one (the
 * rules' misses: a player who moved onto a squad faster than it steps back, a hellpod's landing, the mission's start). */
static void LwMeetSample(ULONGLONG now)
{
    if (now - g_lwMeet.sampleAt < 1000) return;
    g_lwMeet.sampleAt = now;
    float at[64][3], dist[64], band[64]; uint8_t units[64], road[64], walks[64]; int of[64]; uint32_t n = 0;
    for (int i = 0; i < g_lwWorld.count; i++)
    {
        LwSquad* q = &g_lwSquads[i];
        if (q->state == LW_ONLINE && q->alive > 0 && n < 64)
        {
            at[n][0] = q->pos[0]; at[n][1] = q->pos[1]; at[n][2] = q->seenCount > 0 && isfinite(q->seenAt[0][2]) ? q->seenAt[0][2] + 1.0f : NAN;
            units[n] = q->alive; road[n] = (uint8_t)LwRoadSquad(i); band[n] = LwSquadBand(q); of[n] = i;
            walks[n] = q->intent == LW_PATROL && g_lwView.squad[i].known && g_lwView.squad[i].walking; n++;
        }
        if (!LwRoadSquad(i)) continue;
        const float d2 = q->state == LW_OFFLINE && q->survivors && q->intent == LW_PATROL ? LwNearestPlayer2(q->pos[0], q->pos[1]) : 3.0e38f;
        if (d2 >= CO_LW_MEET_CLOSE * CO_LW_MEET_CLOSE) { g_lwMeetClose[i] = 0; continue; }
        if (!g_lwMeetClose[i]) { g_lwMeetClose[i] = 1; g_lwMeet.close++; }
        if (g_lwMeet.closeLeast <= 0 || sqrtf(d2) < g_lwMeet.closeLeast) g_lwMeet.closeLeast = sqrtf(d2) > 0.1f ? sqrtf(d2) : 0.1f;
    }
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_MEET_PLAYERS; i++)
    {
        const float* p = g_lwWorld.players[i]; uint32_t seenRoad = 0, newRoad = 0, newArt = 0, seenOther = 0, newOther = 0;
        for (uint32_t k = 0; k < n; k++)
        {
            dist[k] = sqrtf(LwDist2(p, at[k][0], at[k][1]));
            /* seen: a clear line within what is drawn, on the screen where the view is known, and walking */
            const int seen = walks[k] && isfinite(at[k][2]) && dist[k] <= band[k] && LwSeenFrom(p, g_lwWorld.units[i], at[k]) == 1 && !LwOffScreen(i, at[k]);
            const int fresh = co_lw_meet_sighting(&g_lwMeetGap[of[k]][i], seen);
            if (co_lw_meet_watched(&g_lwMeetWatch[of[k]][i], seen && dist[k] <= CO_LW_MEET_WATCH_M))
                Note("living world: sighting: %s %d watched walking by player %d within %.0f m for %d s (%.0f m now); it came online %s", road[k] == 2 ? "arterial patrol" : road[k] ? "road patrol" : "patrol",
                    of[k], i + 1, (double)CO_LW_MEET_WATCH_M, CO_LW_MEET_WATCH_S, (double)dist[k], g_lwMeetCame[of[k]] == CO_LW_MEET_ASK_COVER ? "on its way in: from cover" :
                    g_lwMeetCame[of[k]] == CO_LW_MEET_ASK_BEYOND ? "on its way in: beyond the band" : g_lwMeetCame[of[k]] ? "on its way in: at the edge of cover" : "by the bands' own rules");
            if (!seen) continue;
            if (road[k]) { seenRoad++; newRoad += (uint32_t)fresh; newArt += (uint32_t)(fresh && road[k] == 2); } else { seenOther++; newOther += (uint32_t)fresh; }
        }
        uint8_t isRoad[64];
        for (uint32_t k = 0; k < n; k++) isRoad[k] = road[k] != 0;
        co_lw_meet_sample(&g_lwMeetNear, (uint32_t)i, now, co_lw_roads_near(&g_lwRoads, p[0], p[1], LW_ROAD_NEAR), dist, units, isRoad, n);
        co_lw_meet_seen(&g_lwMeetNear, (uint32_t)i, seenRoad, newRoad, newArt, seenOther, newOther, LwMeetViewAngle(i));
    }
}
static void LwMeetNotes(void)
{
    char line[640]; int waiting = 0, onRoad = 0, holds = 0; const ULONGLONG now = LwMeetNow();
    for (int i = LwRoadNext(-1); i < g_lwWorld.count; i = LwRoadNext(i)) { waiting += LwMeetWaits(&g_lwSquads[i]); onRoad += g_lwSquads[i].state == LW_OFFLINE && (g_lwMeetMark[i] & LW_MEET_ON_ROAD) != 0; }
    for (int k = 0; k < CO_LW_MEET_HOLDS; k++) holds += now < g_lwMeetHolds.until[k];
    Note("living world: meet (30 s): %d road patrols offline on a player's road now, %d of them waiting at the ring's edge; %u looks, %u steps back to cover, %u asked at the edge of cover, "
        "%u asked beyond the band; came online on their way in: %u from cover, %u at its edge, %u beyond the band, %u from the ring's edge; %u began to wait, %u turned back, "
        "%u steps back from a player, %u sent on an errand; %u squads gave way to them (%u times no calm hidden squad to leave, %u times the road patrols' share was full), %d holds now; "
        "their requests refused: sight %u, room %u, spacing %u, other %u (%u of them beyond the band, for the far share; %u such patrols walked on after %u s); "
        "%u called to a player's road, %u of the called came online, %u times none to call; far units of patrols on a player's road %d; "
        "%u road patrols offline within %.0f m of a player (nearest %.0f m)%s", onRoad, waiting, g_lwMeet.looks, g_lwMeet.covered, g_lwMeet.edge, g_lwMeet.beyond, g_lwMeet.cameCover,
        g_lwMeet.cameEdge, g_lwMeet.cameBeyond, g_lwMeet.online, g_lwMeet.held, g_lwMeet.turned, g_lwMeet.back, g_lwMeet.dispatched, g_lwMeet.gaveWay, g_lwMeet.noFiller, g_lwMeet.shareNo, holds,
        g_lwMeet.sight, g_lwMeet.room, g_lwMeet.spaced, g_lwMeet.other, g_lwMeet.farNo, g_lwMeet.zoneLeft, CO_LW_MEET_ZONE_WAIT_MS / 1000u, g_lwCall.called, g_lwCall.came, g_lwCall.none, g_lwMeet.roadFar, g_lwMeet.close, (double)CO_LW_MEET_CLOSE, (double)g_lwMeet.closeLeast,
        g_lwPatrolsOld ? "; the old rules (test): no such rule" : g_lwMeetOff ? "; meet 0 (test): no such rule" : "");
    for (int i = 0; i < g_lwWorld.playersAt && i < CO_LW_MEET_PLAYERS; i++)
    {
        if (co_lw_meet_note(&g_lwMeetNear, (uint32_t)i, line, sizeof line)) Note("living world: %s", line);
        if (co_lw_meet_seen_note(&g_lwMeetNear, (uint32_t)i, line, sizeof line)) Note("living world: %s", line);
    }
    co_lw_meet_restart(&g_lwMeetNear);
    g_lwMeet.held = g_lwMeet.online = g_lwMeet.turned = g_lwMeet.back = g_lwMeet.gaveWay = g_lwMeet.noFiller = g_lwMeet.sight = g_lwMeet.room = g_lwMeet.spaced = g_lwMeet.other = g_lwMeet.close = 0;
    g_lwMeet.looks = g_lwMeet.covered = g_lwMeet.edge = g_lwMeet.beyond = g_lwMeet.cameCover = g_lwMeet.cameEdge = g_lwMeet.cameBeyond = g_lwMeet.dispatched = g_lwMeet.shareNo = 0;
    g_lwMeet.closeLeast = 0; g_lwMeet.farNo = g_lwMeet.zoneLeft = 0; g_lwCall.called = g_lwCall.came = g_lwCall.none = 0;
}
/* The squad whose next node had no navmesh within 25 m (the 30 s line only counts them). */
static void LwNoFaceNote(const LwSquad* q)
{
    if (g_lwMeet.noFaceNotes >= LW_NO_FACE_NOTES || q->pathAt >= q->pathLen) return;
    const int idx = (int)(q - g_lwSquads), node = q->path[q->pathAt];
    g_lwMeet.noFaceNotes++;
    Note("living world: squad %d (%s) at (%.0f, %.0f): its next node %d at (%.0f, %.0f) has no navmesh within 25 m; it plans again%s", idx,
        LwRoadSquad(idx) == 2 ? "arterial patrol" : LwRoadSquad(idx) ? "road patrol" : "off the roads", (double)q->pos[0], (double)q->pos[1], node, (double)g_lwNodes[node].x,
        (double)g_lwNodes[node].y, g_lwMeet.noFaceNotes == LW_NO_FACE_NOTES ? " (the last such line this mission)" : "");
}

/* Test command "travel V S": the Helldiver moves along the arterials' nodes at V m/s for S seconds, a step a tick. A
 * step of 13 m every 0.65 s read as a hellpod (co_settle.h: over 60 m/s) and the squads were placed for a player
 * hundreds of metres away; steps of V x dt, 2 m at most, read as the speed they stand for. The way on is the first
 * arterial neighbour, so two runs from one node take one way; a new command near where the last ended goes on along
 * it, and "travelback V" walks the nodes passed back to the first, so a second arm starts where the first did. */
#if defined(CO_RESEARCH) || defined(CO_TEST)
#define LW_TRAVEL_TRAIL 512
static struct { float v, pos[2]; int at, from, seq, trailN, back; uint16_t trail[LW_TRAVEL_TRAIL]; ULONGLONG until, last, began; double way; } g_lwTravel;
static void LwTravelEnd(const char* why)
{
    const double s = (double)(GetTickCount64() - g_lwTravel.began) / 1000.0;
    Note("living world: test command %d: travel: %s after %.0f m in %.0f s (%.1f m/s), at (%.0f, %.0f)", g_lwTravel.seq, why, g_lwTravel.way, s, s > 0 ? g_lwTravel.way / s : 0.0,
        (double)g_lwTravel.pos[0], (double)g_lwTravel.pos[1]);
    g_lwTravel.until = 0;
}
static void LwTravelCmd(int seq, float v, float s)
{
    if (g_lwTravel.until) LwTravelEnd("stopped by a new command");
    if (!(v > 0) || !(s > 0) || v > 40.0f) { Note("living world: test command %d: travel: off (speed over 0 to 40 m/s, seconds over 0)", seq); return; }
    if (g_lwWorld.playersAt <= 0 || g_lwArtNodes == 0) { Note("living world: test command %d: travel: no player position or no arterial nodes", seq); return; }
    int best = -1; float bd = 3.0e38f; const float* p = g_lwWorld.players[0];
    for (int i = g_lwNodeCount - g_lwRoadNodes; i < g_lwNodeCount; i++)
    {
        const float d = LwDist2(p, g_lwNodes[i].x, g_lwNodes[i].y);
        if ((g_lwNodeKind[i] & CO_LW_ROADS_ARTERIAL) && d < bd) { bd = d; best = i; }
    }
    if (best < 0 || bd > 150.0f * 150.0f) { Note("living world: test command %d: travel: no arterial node within 150 m (the nearest %.0f m): teleport to one first", seq, (double)sqrtf(bd)); return; }
    const ULONGLONG now = GetTickCount64();
    const int on = g_lwTravel.trailN > 0 && LwDist2(p, g_lwTravel.pos[0], g_lwTravel.pos[1]) < 30.0f * 30.0f; /* goes on along the last way */
    if (!on) { g_lwTravel.at = best; g_lwTravel.from = -1; g_lwTravel.trailN = 0; }
    g_lwTravel.v = v; g_lwTravel.pos[0] = p[0]; g_lwTravel.pos[1] = p[1]; g_lwTravel.seq = seq; g_lwTravel.back = 0;
    g_lwTravel.until = now + (ULONGLONG)(s * 1000.0f); g_lwTravel.last = g_lwTravel.began = now; g_lwTravel.way = 0;
    /* the way on from that node, for the driver to turn the camera along it before the segment */
    const LwNode* t = &g_lwNodes[g_lwTravel.at];
    int ahead = co_lw_roads_step(t->next, t->degree, g_lwNodeKind, CO_LW_ROADS_ARTERIAL, g_lwTravel.from, 0);
    if (ahead < 0) ahead = g_lwTravel.at;
    Note("living world: test command %d: travel: %.1f m/s for %.0f s from (%.0f, %.0f) %s arterial node %d at (%.0f, %.0f), then along the arterials; the way on: (%.0f, %.0f)", seq, (double)v, (double)s,
        (double)p[0], (double)p[1], on ? "on to" : "to its nearest", g_lwTravel.at, (double)t->x, (double)t->y, (double)g_lwNodes[ahead].x, (double)g_lwNodes[ahead].y);
}
/* Test command "wayon": where the next travel heads, for the driver to turn the camera along the road before it. */
static void LwWayOnCmd(int seq)
{
    if (g_lwWorld.playersAt <= 0) { Note("living world: test command %d: wayon: no player position", seq); return; }
    int at = -1, from = -1; float bd = 150.0f * 150.0f; const float* p = g_lwWorld.players[0];
    if (g_lwTravel.trailN > 0 && LwDist2(p, g_lwTravel.pos[0], g_lwTravel.pos[1]) < 30.0f * 30.0f) { at = g_lwTravel.at; from = g_lwTravel.from; }
    else for (int i = g_lwNodeCount - g_lwRoadNodes; i < g_lwNodeCount; i++)
    {
        const float d = LwDist2(p, g_lwNodes[i].x, g_lwNodes[i].y);
        if ((g_lwNodeKind[i] & CO_LW_ROADS_ARTERIAL) && d < bd) { bd = d; at = i; }
    }
    if (at < 0) { Note("living world: test command %d: wayon: no arterial node within 150 m", seq); return; }
    if (LwDist2(p, g_lwNodes[at].x, g_lwNodes[at].y) < 20.0f * 20.0f) /* standing on it: the node after */
    {
        const int next = co_lw_roads_step(g_lwNodes[at].next, g_lwNodes[at].degree, g_lwNodeKind, CO_LW_ROADS_ARTERIAL, from, 0);
        if (next >= 0) at = next;
    }
    Note("living world: test command %d: wayon: (%.1f, %.1f)", seq, (double)g_lwNodes[at].x, (double)g_lwNodes[at].y);
}
static void LwTravelBackCmd(int seq, float v)
{
    if (g_lwTravel.until) LwTravelEnd("stopped by a new command");
    if (!(v > 0) || v > 40.0f || g_lwTravel.trailN <= 0 || g_lwWorld.playersAt <= 0) { Note("living world: test command %d: travelback: no way travelled to go back along, or no speed over 0 to 40 m/s", seq); return; }
    const ULONGLONG now = GetTickCount64(); const float* p = g_lwWorld.players[0];
    g_lwTravel.v = v; g_lwTravel.pos[0] = p[0]; g_lwTravel.pos[1] = p[1]; g_lwTravel.seq = seq; g_lwTravel.back = 1; g_lwTravel.at = g_lwTravel.trail[g_lwTravel.trailN - 1];
    g_lwTravel.until = now + 3600000u; g_lwTravel.last = g_lwTravel.began = now; g_lwTravel.way = 0;
    Note("living world: test command %d: travelback: %.1f m/s back over %d nodes to node %d at (%.0f, %.0f)", seq, (double)v, g_lwTravel.trailN, g_lwTravel.trail[0],
        (double)g_lwNodes[g_lwTravel.trail[0]].x, (double)g_lwNodes[g_lwTravel.trail[0]].y);
}
static void LwTravelTick(ULONGLONG now)
{
    if (now == 0) { memset(&g_lwTravel, 0, sizeof g_lwTravel); return; } /* a new mission */
    if (!g_lwTravel.until) return;
    if (now >= g_lwTravel.until) { LwTravelEnd("done"); return; }
    float step = co_lw_meet_travel_step(g_lwTravel.v, (float)(now - g_lwTravel.last) / 1000.0f);
    g_lwTravel.last = now;
    for (int hops = 0; step > 0 && hops < 4; hops++)
    {
        const LwNode* t = &g_lwNodes[g_lwTravel.at];
        const float dx = t->x - g_lwTravel.pos[0], dy = t->y - g_lwTravel.pos[1], d = sqrtf(dx * dx + dy * dy);
        if (d > step) { g_lwTravel.pos[0] += dx / d * step; g_lwTravel.pos[1] += dy / d * step; g_lwTravel.way += step; break; }
        g_lwTravel.pos[0] = t->x; g_lwTravel.pos[1] = t->y; g_lwTravel.way += d; step -= d;
        if (g_lwTravel.back)
        {
            if (--g_lwTravel.trailN <= 0) { g_lwTravel.trailN = 0; step = -1.0f; break; } /* at the first node: moved there below, then done */
            g_lwTravel.at = g_lwTravel.trail[g_lwTravel.trailN - 1];
            continue;
        }
        if (g_lwTravel.trailN < LW_TRAVEL_TRAIL) g_lwTravel.trail[g_lwTravel.trailN++] = (uint16_t)g_lwTravel.at;
        int next = co_lw_roads_step(t->next, t->degree, g_lwNodeKind, CO_LW_ROADS_ARTERIAL, g_lwTravel.from, 0);
        const float r = g_lwWorld.radius - 50.0f;
        if (next >= 0 && g_lwNodes[next].x * g_lwNodes[next].x + g_lwNodes[next].y * g_lwNodes[next].y > r * r) next = -1; /* the disc's edge */
        if (next < 0) next = g_lwTravel.from; /* an arterial's end: back */
        if (next < 0) { LwTravelEnd("no way on from its first node"); return; }
        g_lwTravel.from = g_lwTravel.at; g_lwTravel.at = next;
    }
    g_lwTeleportForce = g_lwTeleportQuiet = 1;
    const int moved = LwTestTeleport(g_lwTravel.seq, g_lwTravel.pos[0], g_lwTravel.pos[1]);
    g_lwTeleportForce = g_lwTeleportQuiet = 0;
    if (!moved) LwTravelEnd("stopped: a step was refused (the line above)");
    else if (step < 0) LwTravelEnd("back at its first node");
}
/* Test command "beside D": the Helldiver stands D m to the side of an arterial node, on a side whose eye has a clear
 * line to the road there, so a segment watches the road from beside it. The nearest node first, then the next ones
 * along the road: a point with enemies within 120 m is passed over (the guarded teleport refuses it, and a segment
 * there is a fight, not a watch). Every outcome has its own line: "beside: D m beside ..." or "beside: refused: ...". */
#define LW_BESIDE_NODES 8
static int LwEnemiesNear(float x, float y, float r, float* nearest);
static void LwBesideCmd(int seq, float d)
{
    if (g_lwWorld.playersAt <= 0 || !(d > 0) || d > 200.0f) { Note("living world: test command %d: beside: refused: no player position, or no distance over 0 to 200 m", seq); return; }
    int best = -1; float bd = 300.0f * 300.0f; const float* p = g_lwWorld.players[0];
    for (int i = g_lwNodeCount - g_lwRoadNodes; i < g_lwNodeCount; i++)
    {
        const float d2 = LwDist2(p, g_lwNodes[i].x, g_lwNodes[i].y);
        if ((g_lwNodeKind[i] & CO_LW_ROADS_ARTERIAL) && d2 < bd) { bd = d2; best = i; }
    }
    if (best < 0) { Note("living world: test command %d: beside: refused: no arterial node within 300 m", seq); return; }
    float feet[2 * LW_BESIDE_NODES][3], road[2 * LW_BESIDE_NODES][2]; uint8_t clear[2 * LW_BESIDE_NODES]; uint16_t foes[2 * LW_BESIDE_NODES]; int node[2 * LW_BESIDE_NODES], n = 0;
    for (int at = best, from = -1, k = 0; k < LW_BESIDE_NODES; k++)
    {
        const int next = co_lw_roads_step(g_lwNodes[at].next, g_lwNodes[at].degree, g_lwNodeKind, CO_LW_ROADS_ARTERIAL, from, 0);
        if (next < 0) break;
        const float dx = g_lwNodes[next].x - g_lwNodes[at].x, dy = g_lwNodes[next].y - g_lwNodes[at].y, len = sqrtf(dx * dx + dy * dy);
        float eye[3] = {g_lwNodes[at].x, g_lwNodes[at].y, 0}, nearest;
        const int ground = len > 1.0f && LwGround(eye[0], eye[1], &eye[2]);
        eye[2] += 1.0f;
        for (int side = 0; side < 2; side++, n++)
        {
            float* f = feet[n];
            f[0] = eye[0] + (side ? dy : -dy) / (len > 1.0f ? len : 1.0f) * d; f[1] = eye[1] + (side ? -dx : dx) / (len > 1.0f ? len : 1.0f) * d; f[2] = 0;
            road[n][0] = eye[0]; road[n][1] = eye[1]; node[n] = at;
            clear[n] = ground && LwGround(f[0], f[1], &f[2]) && LwSeenFrom(f, g_lwWorld.units[0], eye) == 1;
            const int count = clear[n] ? LwEnemiesNear(f[0], f[1], 120.0f, &nearest) : 0;
            foes[n] = (uint16_t)(count > 65535 ? 65535 : count);
        }
        from = at; at = next;
    }
    int least = -1; const int pick = co_lw_meet_beside(clear, foes, n, &least);
    if (pick < 0 && least < 0) { Note("living world: test command %d: beside: refused: no point %.0f m to either side of %d arterial nodes from node %d on has ground and a clear line to the road", seq, (double)d, n / 2, best); return; }
    if (pick < 0) { Note("living world: test command %d: beside: refused: enemies within 120 m of every point with a clear line %.0f m beside %d arterial nodes from node %d on (the fewest %d, beside node %d)", seq, (double)d, n / 2, best, (int)foes[least], node[least]); return; }
    if (!LwTestTeleport(seq, feet[pick][0], feet[pick][1])) { Note("living world: test command %d: beside: refused: the teleport beside arterial node %d did not go (its line above says why)", seq, node[pick]); return; }
    Note("living world: test command %d: beside: %.0f m beside arterial node %d at (%.1f, %.1f), a clear line to the road there and no enemy within 120 m; %d points passed over; the nearest node %d at (%.1f, %.1f)",
        seq, (double)d, node[pick], (double)road[pick][0], (double)road[pick][1], pick, best, (double)g_lwNodes[best].x, (double)g_lwNodes[best].y);
}
#else
/* A package build has no travel: its commands are a run's tools. */
static void LwTravelCmd(int seq, float v, float s) { (void)seq; (void)v; (void)s; }
static void LwTravelBackCmd(int seq, float v) { (void)seq; (void)v; }
static void LwTravelTick(ULONGLONG now) { (void)now; }
static void LwBesideCmd(int seq, float d) { (void)seq; (void)d; }
static void LwWayOnCmd(int seq) { (void)seq; }
#endif

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
    LwMeetNotes();
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
        memset(g_lwMeetSince, 0, sizeof g_lwMeetSince); memset(g_lwMeetClose, 0, sizeof g_lwMeetClose); memset(g_lwMeetMark, 0, sizeof g_lwMeetMark); memset(g_lwMeetLookAt, 0, sizeof g_lwMeetLookAt);
        memset(g_lwMeetCover, 0, sizeof g_lwMeetCover); memset(&g_lwMeetHolds, 0, sizeof g_lwMeetHolds); memset(g_lwMeetSwapAt, 0, sizeof g_lwMeetSwapAt); memset(g_lwMeetGap, 255, sizeof g_lwMeetGap); memset(&g_lwMeet, 0, sizeof g_lwMeet); memset(&g_lwMeetNear, 0, sizeof g_lwMeetNear); LwTravelTick(0);
        memset(&g_lwCall, 0, sizeof g_lwCall); memset(g_lwCallOf, 0, sizeof g_lwCallOf); memset(g_lwZoneSince, 0, sizeof g_lwZoneSince); memset(g_lwZoneNo, 0, sizeof g_lwZoneNo);
        g_lwPatrols.mission = g_lwMission; g_lwPatrols.noteAt = now;
    }
    g_lwMeet.looksTick = 0;
    co_lw_view_level(&g_lwView, g_lwMission, now);
    LwBandTick(now);
    LwTravelTick(now);
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
    LwMeetSample(now);
    LwCallTick(now);
    LwPatrolGiveWay(now);
    if (now - g_lwPatrols.noteAt >= 30000) { g_lwPatrols.noteAt = now; LwPatrolsNotes(now); }
}

#ifdef CO_TEST
/* Squad 0 as a road patrol on a line of road nodes 1..n, 100 m apart, node 0 off the roads and joined to node 1. It
 * stands at node `at`, come from `prev` (-1: from nowhere), with an unwalked plan that begins at `refused` (-1:
 * none). The new plan's first node, -1 with no road plan. */
int CoXlTestLwRoadPlan(int n, int at, int prev, int refused)
{
    LwSquad* q = &g_lwSquads[0];
    memset(q, 0, sizeof *q); memset(g_lwNodeKind, 0, sizeof g_lwNodeKind);
    g_lwRoadNodes = LW_ROAD_NODES_MIN; g_lwNodeCount = g_lwRoadNodes + 1; g_lwArtNodes = 0; g_lwWorld.radius = 1.0e6f;
    for (int i = 0; i < g_lwNodeCount; i++)
    {
        LwNode* node = &g_lwNodes[i];
        memset(node, 0, sizeof *node); node->x = i <= n ? 100.0f * (float)i : 1.0e7f; /* the rest lie outside the level */
        if (i > n) continue;
        if (i > 0) node->next[node->degree++] = (uint16_t)(i - 1);
        if (i < n) node->next[node->degree++] = (uint16_t)(i + 1);
        g_lwNodeKind[i] = i == 0 ? 0 : i == 1 || i == n ? CO_LW_ROADS_END : CO_LW_ROADS_POINT;
    }
    q->node = (uint16_t)at; q->pos[0] = g_lwNodes[at].x; q->prevValid = prev >= 0; q->prevNode = (uint16_t)(prev >= 0 ? prev : 0);
    if (refused >= 0) { q->path[0] = (uint16_t)refused; q->pathLen = 1; }
    const int planned = LwRoadPlan(q, 0);
    g_lwNodeCount = g_lwRoadNodes = 0;
    return planned ? q->path[0] : -1;
}
/* The road patrols' switch as this build reads it. */
int CoXlTestLwRoadPatrolsOn(void) { return LwRoadPatrolsOn(); }
/* On that line of road nodes 1..n, squad `index` (0: a road patrol, 1: none) walks offline from node `at` toward node
 * n at 2 m/s for `seconds` (eighth-second ticks), a player at (px, 0) for the first `moveAt` seconds and at (px2, 0)
 * after; `old`: by the old rules. Returns the least distance from the player it walked at; `out`: where it ends (x),
 * waiting or not, and how often it began to wait, turned back and stepped back. */
float CoXlTestLwMeetWalk(int index, int n, int at, float px, float px2, int moveAt, int seconds, int old, float* out)
{
    LwSquad* q = &g_lwSquads[index]; float least = 3.0e38f;
    memset(q, 0, sizeof *q); memset(g_lwNodeKind, 0, sizeof g_lwNodeKind); memset(&g_lwMeet, 0, sizeof g_lwMeet); g_lwMeetSince[index] = 0;
    g_lwRoadNodes = LW_ROAD_NODES_MIN; g_lwNodeCount = g_lwRoadNodes + 1; g_lwArtNodes = 0; g_lwWorld.radius = 1.0e6f; g_lwWorld.rMin = LW_R_MIN; g_lwTerr.built = 0;
    for (int i = 0; i < g_lwNodeCount; i++)
    {
        LwNode* node = &g_lwNodes[i];
        memset(node, 0, sizeof *node); node->x = i <= n ? 100.0f * (float)i : 1.0e7f;
        if (i > n) continue;
        if (i > 0) node->next[node->degree++] = (uint16_t)(i - 1);
        if (i < n) node->next[node->degree++] = (uint16_t)(i + 1);
        g_lwNodeKind[i] = i == 0 ? 0 : i == 1 || i == n ? CO_LW_ROADS_END : CO_LW_ROADS_POINT;
    }
    q->state = LW_OFFLINE; q->survivors = 6; q->speed = 2.0f; q->node = (uint16_t)at; q->pos[0] = g_lwNodes[at].x; q->prevNode = (uint16_t)(at - 1); q->prevValid = at > 1;
    for (int k = at + 1; k <= n && q->pathLen < LW_PATH_MAX; k++) q->path[q->pathLen++] = (uint16_t)k;
    g_lwWorld.playersAt = 1; g_lwWorld.players[0][1] = g_lwWorld.players[0][2] = 0;
    g_lwPatrols.mission = g_lwMission; g_lwPatrolsOld = old;
    for (int tick = 0; tick < seconds * 8; tick++)
    {
        g_lwWorld.players[0][0] = tick < moveAt * 8 ? px : px2;
        g_lwMeetTestNow = 1000000u + (ULONGLONG)tick * 125u;
        if (index && q->pathAt >= q->pathLen) break; /* a squad off the roads has no plan on this line */
        LwWalk(q, 0.125f);
        const float d = sqrtf(LwDist2(q->pos, g_lwWorld.players[0][0], g_lwWorld.players[0][1]));
        if (d < least) least = d;
    }
    out[0] = q->pos[0]; out[1] = (float)LwMeetWaits(q); out[2] = (float)g_lwMeet.held; out[3] = (float)g_lwMeet.turned; out[4] = (float)g_lwMeet.back;
    g_lwNodeCount = g_lwRoadNodes = 0; g_lwMeetTestNow = 0; g_lwPatrolsOld = 0; g_lwWorld.playersAt = 0; g_lwMeetSince[index] = 0; memset(&g_lwMeet, 0, sizeof g_lwMeet);
    return least;
}
/* The seen rules without the game: the sight ray and the way ahead are stubs, the look and its ranges are the game's.
 * No ray reaches a point whose x lies in [g_lwTestHid[0], g_lwTestHid[1]], or in the first half of every
 * -g_lwTestHid[1] m; its way ahead is seen as g_lwTestAhead says. g_lwTestRays: the rays asked per player. */
static float g_lwTestHid[2], g_lwTestSpeed; static int g_lwTestAhead, g_lwTestFiller = -1, g_lwTestMeetOff, g_lwTestBusy, g_lwTestRays[2];
static int g_lwTestGroup = 1, g_lwTestViewOf = 1, g_lwTestLocal = 0, g_lwTestBlind;
static int g_lwTestFarNo;
/* The walks that follow have every request beyond the band refused: 1 for the far share, 2 for the spacing (the
 * text the spacing rule itself writes for a request `1 m` away a second ago), 3 for the bubble's room. 0: none. */
void CoXlTestLwSeenFarNo(int refuse) { g_lwTestFarNo = refuse; }
static const char* LwTestRefusal(const LwSquad* q)
{
    static char text[160];
    if (g_lwTestFarNo == 2) { LwSpacedText(text, sizeof text, -1, 1.0f, 1000); return text; }
    if (g_lwTestFarNo == 3) { LwDensityRoom(q->pos, 1000, 0, text, sizeof text); return text; }
    return "the far share: a test";
}
/* The far-share refusals counted in this 30 s line so far. */
int CoXlTestLwSeenFarNoCount(void) { return (int)g_lwMeet.farNo; }
static int LwTestSeenRay(int i, const float* point)
{
    if (i < 2) g_lwTestRays[i]++;
    if (i == 0 && g_lwTestBlind) return 0;
    if (g_lwTestHid[1] < 0) return !(fmodf(point[0], -g_lwTestHid[1]) < -0.5f * g_lwTestHid[1]); /* cover and open ground in turn */
    return !(point[0] >= g_lwTestHid[0] && point[0] <= g_lwTestHid[1]);
}
/* The group, the view and the vehicle for what follows. `group`: 1 its units measured (2 m), 0 never online, -1 the
 * squad has no group. `viewOf`: the player the host's camera is on (1-based, 0 not read). `local`: this machine's
 * player (0-based). `speed`: player 0 drives toward -x at this many m/s. `blind`: no ray of player 0 reaches a point. */
void CoXlTestLwSeenSetup(int group, int viewOf, int local, float speed, int blind) { g_lwTestGroup = group; g_lwTestViewOf = viewOf; g_lwTestLocal = local; g_lwTestSpeed = speed; g_lwTestBlind = blind; }
static void LwTestSeenBegin(LwSquad* q, int np)
{
    memset(g_lwRadii, 0, sizeof g_lwRadii); g_lwRadiiGen++;
    q->definition = g_lwTestGroup < 0 ? 0 : 1;
    if (g_lwTestGroup > 0) LwGroupRadiusSeen(1, LW_RADIUS_DEFAULT);
    g_lwWorld.playersAt = np; memset(g_lwWorld.local, 0, sizeof g_lwWorld.local); g_lwWorld.local[g_lwTestLocal] = 1;
    g_lwTestView = g_lwTestViewOf; g_lwTestRay = LwTestSeenRay; g_lwTestRays[0] = g_lwTestRays[1] = 0;
    memset(&g_lwView.player[0], 0, sizeof g_lwView.player[0]);
    if (g_lwTestSpeed > 0) { g_lwView.player[0].known = 1; g_lwView.player[0].v[0] = -g_lwTestSpeed; }
}
static void LwTestSeenDone(void) { g_lwTestView = 0; g_lwTestRay = NULL; memset(&g_lwView.player[0], 0, sizeof g_lwView.player[0]); memset(g_lwRadii, 0, sizeof g_lwRadii); g_lwRadiiGen++; }
/* How far player `player`'s sight counts for a patrol's own request: the group, the view and the players as set up. */
float CoXlTestLwSeenRange(int player, int np)
{
    LwSquad* q = &g_lwSquads[0]; memset(q, 0, sizeof *q);
    g_lwPatrols.mission = g_lwMission; g_lwPatrolsOld = 0; g_lwMeetOff = g_lwTestMeetOff;
    LwTestSeenBegin(q, np);
    LwMeetSight(q);
    const float r = sqrtf(LwSightRange2(player));
    LwMeetSight(NULL); LwTestSeenDone(); g_lwWorld.playersAt = 0;
    return r;
}
static int LwTestAheadSeen(LwSquad* q) { (void)q; return g_lwTestAhead; }
static LwSquad* LwTestFillerPick(const LwSquad* except, float* d2) { (void)except; *d2 = 0; return g_lwTestFiller >= 0 ? &g_lwSquads[g_lwTestFiller] : NULL; }
/* The draw band with its margin for a squad of the default radius, as the rules read it. */
float CoXlTestLwSeenBand(void) { LwSquad q; memset(&q, 0, sizeof q); q.definition = 1; return LwSquadBand(&q) + LW_FAR_MARGIN; }
/* The scoped band a squad of group 5 has from its group's own slot, its units measured at `radius` m. */
float CoXlTestLwSeenScopedOf(float radius)
{
    LwSquad q; memset(&q, 0, sizeof q); memset(g_lwRadii, 0, sizeof g_lwRadii); g_lwRadiiGen++;
    q.definition = 5; LwGroupRadiusSeen(5, radius);
    LwSquadBand(&q);
    memset(g_lwRadii, 0, sizeof g_lwRadii); g_lwRadiiGen++;
    return q.scoped;
}
/* The band of a squad with no group while group 5's units are measured at 8 m: no group's slot is its own. */
float CoXlTestLwSeenBandNoGroup(void)
{
    LwSquad q; memset(&q, 0, sizeof q); memset(g_lwRadii, 0, sizeof g_lwRadii); g_lwRadiiGen++;
    LwGroupRadiusSeen(5, 8.0f);
    const float band = LwSquadBand(&q);
    memset(g_lwRadii, 0, sizeof g_lwRadii); g_lwRadiiGen++;
    return band;
}
/* On the line of road nodes 1..n (100 m apart, y 0), squad `index` (0: a road patrol, 1: none) walks offline from x
 * `from` toward node n at 2 m/s for `seconds`, the `np` players at `pl` (x, y) standing; hidden where its x lies in
 * [hid0, hid1], its way ahead seen as `ahead`; at second `errand` (-1: never) it is sent to reinforce. Each tick is
 * LwWalk and LwMeetApproach, as the squads' tick has them; the first ask ends the walk when `comes` (it came online).
 * out: 0 x at the end, 1 the ask's kind (0 none), 2 x where it asked, 3 the least distance to a player it walked at,
 * 4 waiting at the ring, 5 steps back to cover, 6 its mark, 7 errands counted, 8 looks, 9 has priority at the end,
 * 10 a look held its request back, 11 and 12 the sight rays asked for players 0 and 1, 13 its requests beyond the
 * band refused for the far share, 14 whether it walked on after standing for them. */
void CoXlTestLwSeenWalk(int index, int n, float from, const float (*pl)[2], int np, float hid0, float hid1, int ahead, int seconds, int errand, int comes, float* out)
{
    LwSquad* q = &g_lwSquads[index]; float least = 3.0e38f;
    memset(q, 0, sizeof *q); memset(g_lwNodeKind, 0, sizeof g_lwNodeKind); memset(&g_lwMeet, 0, sizeof g_lwMeet); memset(&g_lwMeetHolds, 0, sizeof g_lwMeetHolds);
    g_lwMeetSince[index] = 0; g_lwMeetMark[index] = 0; g_lwMeetLookAt[index] = 0; memset(&g_lwMeetCover[index], 0, sizeof g_lwMeetCover[index]);
    g_lwRoadNodes = LW_ROAD_NODES_MIN; g_lwNodeCount = g_lwRoadNodes + 1; g_lwArtNodes = 0; g_lwWorld.radius = 1.0e6f; g_lwWorld.rMin = LW_R_MIN; g_lwTerr.built = 0;
    for (int i = 0; i < g_lwNodeCount; i++)
    {
        LwNode* node = &g_lwNodes[i];
        memset(node, 0, sizeof *node); node->x = i <= n ? 100.0f * (float)i : 1.0e7f;
        if (i > n) continue;
        if (i > 0) node->next[node->degree++] = (uint16_t)(i - 1);
        if (i < n) node->next[node->degree++] = (uint16_t)(i + 1);
        g_lwNodeKind[i] = i == 0 ? 0 : i == 1 || i == n ? CO_LW_ROADS_END : CO_LW_ROADS_POINT;
    }
    const int at = (int)(from / 100.0f);
    q->state = LW_OFFLINE; q->survivors = 6; q->speed = 2.0f; q->node = (uint16_t)at; q->pos[0] = from; q->prevNode = (uint16_t)(at - 1); q->prevValid = at > 1;
    for (int k = at + 1; k <= n && q->pathLen < LW_PATH_MAX; k++) q->path[q->pathLen++] = (uint16_t)k;
    LwTestSeenBegin(q, np); g_lwWorld.count = index + 1;
    for (int i = 0; i < np; i++) { g_lwWorld.players[i][0] = pl[i][0]; g_lwWorld.players[i][1] = pl[i][1]; g_lwWorld.players[i][2] = 0; }
    g_lwPatrols.mission = g_lwMission; g_lwPatrolsOld = 0; g_lwMeetOff = g_lwTestMeetOff;
    g_lwTestHid[0] = hid0; g_lwTestHid[1] = hid1; g_lwTestAhead = ahead; g_lwMeetAhead = LwTestAheadSeen;
    out[1] = out[2] = 0;
    for (int tick = 0; tick < seconds * 8; tick++)
    {
        const ULONGLONG now = 1000000u + (ULONGLONG)tick * 125u;
        g_lwMeetTestNow = now; g_lwMeet.looksTick = 0;
        if (errand >= 0 && tick == errand * 8) q->intent = LW_REINFORCE;
        if (q->intent == LW_PATROL) LwWalk(q, 0.125f);
        const int meet = LwMeetApproach(q, now, g_lwTestBusy);
        for (int i = 0; i < np; i++) { const float d = sqrtf(LwDist2(q->pos, pl[i][0], pl[i][1])); if (d < least) least = d; }
        if (!meet) continue;
        if (out[1] == 0) { out[1] = (float)meet; out[2] = q->pos[0]; }
        if (g_lwTestFarNo && meet == CO_LW_MEET_ASK_BEYOND) { LwMeetRefused(q, meet, LwTestRefusal(q), now); q->retry = now + 3000; continue; } /* as the squads' tick refuses it */
        if (comes) { q->state = LW_PENDING; q->since = now; LwMeetAsked(q, meet, 1); break; }
    }
    out[0] = q->pos[0]; out[3] = least; out[4] = (float)LwMeetWaits(q); out[5] = (float)g_lwMeet.covered; out[6] = (float)g_lwMeetMark[index];
    out[7] = (float)g_lwMeet.dispatched; out[8] = (float)g_lwMeet.looks; out[9] = (float)LwMeetPriority(q); out[10] = (float)(q->retry != 0);
    out[11] = (float)g_lwTestRays[0]; out[12] = (float)g_lwTestRays[1];
    out[13] = (float)g_lwMeet.farNo; out[14] = (float)g_lwMeet.zoneLeft; LwZoneClear(index);
    LwTestSeenDone();
    g_lwNodeCount = g_lwRoadNodes = 0; g_lwMeetTestNow = 0; g_lwWorld.playersAt = 0; g_lwWorld.count = 0; g_lwMeetSince[index] = 0; g_lwMeetMark[index] = 0;
    g_lwMeetAhead = LwAboutToBeSeen; memset(&g_lwMeet, 0, sizeof g_lwMeet); g_lwMeetOff = 0;
}
/* A world for the room rules: `np` players at `pl`, every bubble's budget `budget`, 40 squads free. */
void CoXlTestLwSeenWorld(const float (*pl)[2], int np, int budget)
{
    memset(g_lwSquads, 0, 40 * sizeof g_lwSquads[0]); memset(&g_lwMeet, 0, sizeof g_lwMeet); memset(&g_lwMeetHolds, 0, sizeof g_lwMeetHolds);
    memset(g_lwMeetMark, 0, 40); memset(g_lwMeetSince, 0, 40 * sizeof g_lwMeetSince[0]); memset(g_lwMeetSwapAt, 0, sizeof g_lwMeetSwapAt);
    g_lwWorld.playersAt = np; g_lwWorld.count = 40; g_lwWorld.eff = budget; g_lwRoadNodes = LW_ROAD_NODES_MIN; g_lwArtNodes = 0;
    for (int i = 0; i < np; i++) { g_lwWorld.players[i][0] = pl[i][0]; g_lwWorld.players[i][1] = pl[i][1]; g_lwWorld.players[i][2] = 0; }
    g_lwDensity.on = 1; g_lwDensity.checkedAt = GetTickCount64(); g_lwDensity.budgetOverride = budget; LwBubblesTick();
    g_lwHold.idx = -1; g_lwPatrols.mission = g_lwMission; g_lwPatrolsOld = 0; g_lwMeetOff = 0; g_lwMeetFiller = LwTestFillerPick; g_lwTestFiller = -1;
}
/* Squad `idx` at (x, y): state (1 offline, 2 pending, 3 online) with `units`, and its mark. */
void CoXlTestLwSeenSquad(int idx, float x, float y, int state, int units, int mark)
{
    LwSquad* q = &g_lwSquads[idx];
    q->state = (uint8_t)state; q->pos[0] = x; q->pos[1] = y; q->survivors = q->cap = q->alive = (uint8_t)units; g_lwMeetMark[idx] = (uint8_t)mark; q->definition = 1;
    LwBubblesTick();
}
/* Squad `idx`'s request was refused with `why` at `nowMs`; `filler` is the squad the pick would name (-1: none).
 * 1 when that squad was sent offline for it. */
int CoXlTestLwSeenWhy(int idx, const char* why, uint64_t nowMs, int filler)
{
    g_lwMeetTestNow = nowMs; g_lwTestFiller = filler;
    LwMeetWhy(&g_lwSquads[idx], why);
    const int left = filler >= 0 && g_lwSquads[filler].forceOff == 2;
    if (filler >= 0) g_lwSquads[filler].forceOff = 0;
    return left;
}
uint32_t CoXlTestLwSeenHeld(int requester, uint64_t nowMs) { g_lwMeetTestNow = nowMs; return LwMeetHeld(requester); }
/* 1 when road patrol `idx` does not take its next step (away from every player) at `nowMs`. */
int CoXlTestLwSeenStands(int idx, uint64_t nowMs)
{
    LwSquad* q = &g_lwSquads[idx];
    g_lwMeetTestNow = nowMs; q->pathLen = 1; q->pathAt = 0; q->path[0] = 0;
    const int stands = LwMeetHold(q, q->pos[0] + 1.0f, q->pos[1]);
    q->pathLen = 0; g_lwMeetSince[idx] = 0;
    return stands;
}
/* An event squad's hold (g_lwHold) for squad `idx` until `untilMs` by the real clock's side: -1 ends it. */
void CoXlTestLwSeenEventHold(int idx, uint32_t units) { g_lwHold.idx = idx; g_lwHold.units = units; g_lwHold.until = idx >= 0 ? GetTickCount64() + 20000 : 0; }
/* The units LwRoomFor counts as held against `requester`: the event squad's hold and the patrols' holds. */
uint32_t CoXlTestLwSeenEventHeld(int requester) { return LwHoldsAgainst(requester); }
/* Squad `idx`, a patrol waiting at the ring, is sent to reinforce at `nowMs`. Bits: 1 it still counts as waiting,
 * 2 it still has priority, 4 its wait and 8 its mark are left after its next visit. */
int CoXlTestLwSeenErrand(int idx, uint64_t nowMs)
{
    LwSquad* q = &g_lwSquads[idx];
    g_lwMeetTestNow = nowMs; g_lwMeetSince[idx] = nowMs; q->intent = LW_REINFORCE;
    int bits = LwMeetWaits(q) | LwMeetPriority(q) << 1;
    LwMeetApproach(q, nowMs, 0);
    bits |= (g_lwMeetSince[idx] != 0) << 2 | (g_lwMeetMark[idx] != 0) << 3;
    q->intent = LW_PATROL;
    return bits;
}
/* A request of a patrol on its way to a player through LwOnlineGates, placement and room as given: how often each
 * was asked. */
static int g_lwTestGate[4];
static int LwTestSeenPlace(LwSquad* q, uintptr_t slot, uint32_t cap, float* pos, float* target) { (void)q; (void)slot; (void)cap; (void)pos; (void)target; g_lwTestGate[2]++; return g_lwTestGate[0]; }
static int LwTestSeenRoom(uint32_t members) { (void)members; g_lwTestGate[3]++; snprintf(g_lwRoomWhy, sizeof g_lwRoomWhy, "the cap: bubble 70 online + 8 over 72 (1 players)"); return g_lwTestGate[1]; }
int CoXlTestLwSeenGates(int placeOk, int roomOk, int* places, int* rooms)
{
    LwSquad* q = &g_lwSquads[0]; float pos[3] = {0}, target[3] = {0};
    q->held = 0; g_lwTestGate[0] = placeOk; g_lwTestGate[1] = roomOk; g_lwTestGate[2] = g_lwTestGate[3] = 0;
    g_lwGates.place = LwTestSeenPlace; g_lwGates.room = LwTestSeenRoom; g_lwFarSpawn = 0; g_lwMeetSpawn = 1;
    const int r = LwOnlineGates(q, 0, 8, pos, target);
    g_lwGates.place = LwOnlinePlace; g_lwGates.room = LwRoomFor; g_lwMeetSpawn = 0; q->retry = 0;
    *places = g_lwTestGate[2]; *rooms = g_lwTestGate[3];
    return r;
}
/* The seen rules off or on for the walks that follow, as the test command "meet" sets them. */
void CoXlTestLwSeenSwitch(int on) { g_lwTestMeetOff = g_lwMeetOff = !on; }
/* 1: the walks that follow have a request out every tick, so they take no look. */
void CoXlTestLwSeenBusy(int busy) { g_lwTestBusy = busy; }
int CoXlTestLwSeenKeeps(int idx) { return LwMeetKeeps(&g_lwSquads[idx]); }
int CoXlTestLwSeenFarRoom(int idx, int meet, int farUnits, int roadFar) { g_lwMeet.roadFar = roadFar; return LwMeetFarRoom(&g_lwSquads[idx], meet, farUnits); }
/* 1 while squad `idx`, online since `sinceMs`, `d` m from the nearest player, is kept by the stay rule at `nowMs`. */
int CoXlTestLwSeenStays(int idx, float d, uint64_t sinceMs, uint64_t nowMs)
{
    g_lwSquads[idx].since = sinceMs; LwGroupRadiusSeen(1, LW_RADIUS_DEFAULT);
    return LwMeetStays(&g_lwSquads[idx], d * d, nowMs);
}
/* The map's cap refuses: 1 when its text reads as a refusal for units (what a swap makes room for). */
int CoXlTestLwSeenCapText(void)
{
    g_lwWorld.onlineUnits = 60; g_lwWorld.eff = 48;
    const int refused = !LwRoomCap(8, 0);
    return refused && LwRoomIsUnits(g_lwRoomWhy) && !LwRoomIsUnits("sight: seen") && !LwRoomIsUnits("within 60 m of an online squad");
}
/* The road patrols among `count` squads, `base` of them the world's own (0: none added): the first `max` as index
 * x 10 + kind (1 any road, 2 arterial) into `out`; how many there are. `members`: the units of squad `probe` of a
 * group of 9 x 100 + of a group of 3 x 10 + whether it is a road patrol. `more`: the squads added. */
int CoXlTestLwRoadTail(int base, int count, int probe, int* out, int max, int* members, int* more)
{
    int n = 0;
    g_lwRoadNodes = LW_ROAD_NODES_MIN; g_lwArtNodes = LW_ROAD_ART_MIN; g_lwWorld.count = count;
    *more = base ? LwRoadTailSet(base) : 0;
    for (int i = LwRoadNext(-1); i < g_lwWorld.count; i = LwRoadNext(i)) { if (n < max) out[n] = i * 10 + LwRoadSquad(i); n++; }
    int all = 0;
    for (int i = 0; i < count; i++) all += LwRoadSquad(i) != 0;
    if (all != n) n = -1; /* the turn missed one, or took one that is none */
    *members = LwRoadMembers(probe, 9) * 100 + LwRoadMembers(probe, 3) * 10 + (LwRoadSquad(probe) != 0);
    g_lwRoadNodes = g_lwArtNodes = 0; g_lwWorld.count = 0; g_lwRoadTail[0] = g_lwRoadTail[1] = LW_SQUADS_MAX;
    return n;
}
/* A world for the calls: a line of road nodes 1..n (100 m apart, y 0), with a side road of 3 nodes north from node
 * `fork` (0: none); the `np` players at `pl`, player 0 the host; road patrols 10 k (k < squads) offline at x sx[k]
 * walking toward +x (dir 1) or -x (-1) with mark mk[k]; a far share of 18. */
void CoXlTestLwCallWorld(int n, int fork, const float (*pl)[2], int np, const float* sx, const int* dir, const int* mk, int squads)
{
    memset(g_lwSquads, 0, 40 * sizeof g_lwSquads[0]); memset(g_lwNodeKind, 0, sizeof g_lwNodeKind); memset(&g_lwMeet, 0, sizeof g_lwMeet); memset(&g_lwCall, 0, sizeof g_lwCall);
    memset(g_lwCallOf, 0, 40); memset(g_lwMeetMark, 0, 40); memset(g_lwMeetSince, 0, 40 * sizeof g_lwMeetSince[0]);
    g_lwRoadNodes = LW_ROAD_NODES_MIN; g_lwNodeCount = g_lwRoadNodes + 1; g_lwArtNodes = 0; g_lwWorld.radius = 1.0e6f; g_lwWorld.rMin = LW_R_MIN; g_lwWorld.eff = 36; g_lwDensity.on = 0;
    for (int i = 0; i < g_lwNodeCount; i++)
    {
        LwNode* node = &g_lwNodes[i];
        memset(node, 0, sizeof *node); node->x = i <= n ? 100.0f * (float)i : 1.0e7f;
        if (i > n || i == 0) continue;
        if (i > 1) node->next[node->degree++] = (uint16_t)(i - 1);
        if (i < n) node->next[node->degree++] = (uint16_t)(i + 1);
        g_lwNodeKind[i] = i == 1 || i == n ? CO_LW_ROADS_END : CO_LW_ROADS_POINT;
    }
    for (int k = 1; fork && k <= 3; k++)
    {
        LwNode* node = &g_lwNodes[n + k]; const int prev = k == 1 ? fork : n + k - 1;
        node->x = 100.0f * (float)fork; node->y = 100.0f * (float)k; g_lwNodeKind[n + k] = CO_LW_ROADS_POINT;
        node->next[node->degree++] = (uint16_t)prev; g_lwNodes[prev].next[g_lwNodes[prev].degree++] = (uint16_t)(n + k);
    }
    LwTestSeenBegin(&g_lwSquads[0], np); g_lwWorld.count = 40;
    for (int i = 0; i < np; i++) { g_lwWorld.players[i][0] = pl[i][0]; g_lwWorld.players[i][1] = pl[i][1]; g_lwWorld.players[i][2] = 0; }
    for (int k = 0; k < squads; k++)
    {
        LwSquad* q = &g_lwSquads[10 * k]; const int a = (int)(sx[k] / 100.0f);
        q->state = LW_OFFLINE; q->survivors = 4; q->speed = 2.0f; q->definition = 1; q->pos[0] = sx[k]; q->pathLen = 1;
        q->node = (uint16_t)(dir[k] > 0 ? a : a + 1); q->path[0] = (uint16_t)(dir[k] > 0 ? a + 1 : a);
        g_lwMeetMark[10 * k] = (uint8_t)mk[k];
    }
    g_lwPatrols.mission = g_lwMission; g_lwPatrolsOld = 0; g_lwMeetOff = 0; g_lwMeetTestNow = 1000000u;
}
/* One call for player `i` at its `turn`: the squad called, -1 none in reach, -2 the player is by no road. out: 0 its
 * plan's first node, 1 the plan's length, 2 its last node, 3 the node it walks from, 4 the player + 1 it is called to. */
int CoXlTestLwCallOne(int i, int turn, int* out)
{
    const int root = LwCallRoot(g_lwWorld.players[i]);
    if (root < 0) return -2;
    g_lwCall.turn[i] = (uint8_t)turn;
    const int s = LwCallFor(i, root);
    if (s < 0) return -1;
    const LwSquad* q = &g_lwSquads[s];
    out[0] = q->path[0]; out[1] = q->pathLen; out[2] = q->path[q->pathLen - 1]; out[3] = q->node; out[4] = g_lwCallOf[s];
    return s;
}
/* The calls' tick at `nowMs`: the patrols called since the world was made. */
int CoXlTestLwCallTick(uint64_t nowMs) { LwCallTick(nowMs); return (int)g_lwCall.called; }
/* Squad `idx`: 0 plans its next way, 1 is sent on an errand and visited, 2 turns back, 3 comes online. 1 while it
 * still counts as called; with 3, 2 when it was not counted as a called patrol that came. */
int CoXlTestLwCallOver(int idx, int how)
{
    LwSquad* q = &g_lwSquads[idx]; const uint32_t came = g_lwCall.came;
    if (how == 1) { q->intent = LW_REINFORCE; LwMeetApproach(q, 1000000u, 0); q->intent = LW_PATROL; }
    else if (how == 2) LwMeetTurn(q, "a test");
    else if (how == 3) { q->state = LW_PENDING; LwMeetAsked(q, CO_LW_MEET_ASK_BEYOND, 1); q->state = LW_OFFLINE; if (g_lwCall.came != came + 1) return 2; }
    else { q->node = q->path[q->pathLen - 1]; q->pathAt = q->pathLen; LwRoadPlan(q, 0); }
    return g_lwCallOf[idx] != 0;
}
/* Squad `idx` is of group `group`, whose units were never measured (any but 1). */
void CoXlTestLwCallGroup(int idx, int group) { g_lwSquads[idx].definition = (uintptr_t)group; g_lwSquads[idx].bandGen = 0; }
/* The player + 1 squad `idx` is called to, 0 for none. */
int CoXlTestLwCallOf(int idx) { return g_lwCallOf[idx]; }
/* Road patrol 0 of the calls' world at x `x`, its plan the road on to node `to`: visited, refused the far share
 * there, then put at x `x2` (with `turn`, its plan back to node 1) and visited a second later. Bits: 1 it stands
 * after the refusal, 2 it still stands after the second visit. */
int CoXlTestLwCallZone(float x, int to, float x2, int turn)
{
    LwSquad* q = &g_lwSquads[0]; const ULONGLONG now = 2000000u; int bits = 0;
    LwZoneClear(0); g_lwMeetMark[0] = 0; g_lwCallOf[0] = 0; g_lwMeetSince[0] = 0;
    for (int pass = 0; pass < 2; pass++)
    {
        const float at = pass ? x2 : x; const int a = (int)(at / 100.0f), back = pass && turn;
        q->pos[0] = at; q->pos[1] = 0; q->node = (uint16_t)(back ? a + 1 : a); q->pathLen = 0; q->pathAt = 0; q->retry = 0;
        for (int k = back ? a : a + 1; back ? k >= 1 : k <= to; k += back ? -1 : 1) if (q->pathLen < LW_PATH_MAX) q->path[q->pathLen++] = (uint16_t)k;
        g_lwMeetTestNow = now + 1000u * (ULONGLONG)pass;
        LwMeetApproach(q, g_lwMeetTestNow, 1);
        if (!pass) LwMeetRefused(q, CO_LW_MEET_ASK_BEYOND, "the far share: a test", now);
        bits |= LwMeetHold(q, at + (back ? -1.0f : 1.0f), 0) << pass;
    }
    LwZoneClear(0); g_lwMeetMark[0] = 0; g_lwMeetSince[0] = 0; g_lwMeetTestNow = 1000000u;
    return bits;
}
void CoXlTestLwCallEnd(void)
{
    LwTestSeenDone();
    memset(g_lwSquads, 0, 40 * sizeof g_lwSquads[0]); memset(g_lwCallOf, 0, 40); memset(g_lwMeetMark, 0, 40); memset(&g_lwCall, 0, sizeof g_lwCall); memset(&g_lwMeet, 0, sizeof g_lwMeet);
    g_lwNodeCount = g_lwRoadNodes = 0; g_lwMeetTestNow = 0; g_lwWorld.playersAt = 0; g_lwWorld.count = 0; g_lwDensity.on = -1;
}
void CoXlTestLwSeenEnd(void)
{
    g_lwWorld.playersAt = 0; g_lwWorld.count = 0; g_lwRoadNodes = 0; g_lwMeetTestNow = 0; g_lwDensity.on = -1; g_lwDensity.budgetOverride = -1; g_lwMeetFiller = LwPatrolFiller;
    memset(g_lwSquads, 0, 40 * sizeof g_lwSquads[0]); memset(&g_lwMeetHolds, 0, sizeof g_lwMeetHolds); memset(g_lwMeetMark, 0, 40); g_lwHold.idx = -1;
    memset(g_lwRadii, 0, sizeof g_lwRadii); g_lwRadiiGen++;
}
#endif
