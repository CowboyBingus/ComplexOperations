/* ---- Drivable roads on a complex level: what its generation is given ----------------------------------------------
 * Two changes, both on the level's own ops only (a tile's ops and every other level run the game's code and values):
 * - Road points. Op 0x0E (0xF38640) gives a route edge one point per 5 m of its chord (0xF38D21..0xF38D37: n =
 *   chord / 5.0 + 1, truncated; n + 1 points) and leaves an edge without points when fewer than n + 2 of its pass's
 *   places are free (0xF38CBF..0xF38D41). A 16 km level asks for more than the 65535 ids op 0x0F can name, so edges
 *   went without points, and so without a path or a stamp corridor. The level's op 0x0E divides by the smallest
 *   spacing, in steps of 0.25 m from 5, at which every edge gets its points.
 * - Arterials. After op 0x86 (the game's own paths between important locations: flag 8), those edges and the shortest
 *   connections from them to the node nearest each objective and outpost take the width op 0x87 gives a convoy road
 *   (0xF8B264). Every later reader of the width (op 0x0F's capsules, the stamp tests, op 0x0D's line) sees it. */
#define ROADS_SPACING_GAME 5.0f
#define ROADS_SPACING_STEP 0.25f
#define ROADS_SPACING_MOST 40.0f
/* The Bastion's hull is 4.95 m wide and an FRV's 2.71 m (their physics resources' collision boxes): the two pass. */
#define ROADS_ARTERIAL_WIDTH 8.0f
#define ROADS_GRAPH_NODES 16384u /* past these the choice is not made (a level has 2304 nodes and 3456 edges at most) */
#define ROADS_GRAPH_EDGES 32768u
#define ROADS_NO_NODE 0xFFFFFFFFu

/* Op 0x0E's second pass takes an edge that is drawable and has no points yet (0xF38AB0..0xF38AD4). */
static int RoadEdgeWantsPoints(const RoadEdge* e) { return RoadEdgeDrawable(e) && !(e->end > e->first); }
/* n as 0xF38B53..0xF38D37 computes it, in single precision; a chord that is not a number gives the game 0. */
static uint32_t RoadEdgeSteps(const RoadEdge* e, float spacing)
{
    const float dx = e->a[0] - e->b[0], dy = e->a[1] - e->b[1];
    const float v = sqrtf(dy * dy + dx * dx) / spacing + 1.0f;
    if (v != v) return 0;
    return v < 1.0e6f ? (uint32_t)v : 1000000u;
}
/* The edges op 0x0E would leave without points at this spacing, of the `capacity` places with `used` taken; taken[i]:
 * the first pass has given edge i its points. *total: the points in use afterwards. */
static uint32_t RoadsSpacingLeftOut(const RoadEdge* e, uint32_t n, const uint8_t* taken, uint32_t used, uint32_t capacity, float spacing, uint32_t* total)
{
    uint32_t out = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        if ((taken != NULL && taken[i]) || !RoadEdgeWantsPoints(&e[i])) continue;
        const uint32_t steps = RoadEdgeSteps(&e[i], spacing);
        if (used > capacity || capacity - used < steps + 2) { out++; continue; }
        used += (steps > 1 ? steps : 1) + 1;
    }
    *total = used;
    return out;
}
/* The smallest spacing that leaves no edge out; ROADS_SPACING_MOST when none does (*left: the edges it leaves out). */
static float RoadsSpacingChoose(const RoadEdge* e, uint32_t n, const uint8_t* taken, uint32_t used, uint32_t capacity, uint32_t* total, uint32_t* left)
{
    for (uint32_t k = 0;; k++)
    {
        const float spacing = ROADS_SPACING_GAME + ROADS_SPACING_STEP * (float)k;
        *left = RoadsSpacingLeftOut(e, n, taken, used, capacity, spacing, total);
        if (*left == 0 || spacing >= ROADS_SPACING_MOST) return spacing;
    }
}

/* 0xF38D25's divss xmm0, [5.0] as a stub: the level's own op 0x0E (its pass, kept at [rsp+0x78] by 0xF386AF, is the
 * key at +0x30) divides by the spacing at +0x38, any other caller by the game's constant. rax and the flags are dead
 * there (0xF38D37 and 0xF38D3F write them next). */
static const uint8_t kRoadSpacingStub[0x28] = {
    0x48, 0x8B, 0x44, 0x24, 0x78,                   /*  0 mov rax, [rsp+0x78] */
    0x48, 0x3B, 0x05, 0x24, 0x00, 0x00, 0x00,       /*  5 cmp rax, [rip -> +0x30] */
    0x75, 0x0D,                                     /* 12 jne 27 */
    0xF3, 0x0F, 0x5E, 0x05, 0x22, 0x00, 0x00, 0x00, /* 14 divss xmm0, [rip -> +0x38] */
    0xE9, 0, 0, 0, 0,                               /* 22 jmp 0xF38D2D (rel32 at 23) */
    0xF3, 0x0F, 0x5E, 0x05, 0, 0, 0, 0,             /* 27 divss xmm0, [the game's 5.0] (rel32 at 31) */
    0xE9, 0, 0, 0, 0};                              /* 35 jmp 0xF38D2D (rel32 at 36) */
static void RoadSpacingBuild(uint8_t* stub, uintptr_t constant, uintptr_t back)
{
    memset(stub, 0xCC, 0x40);
    memcpy(stub, kRoadSpacingStub, sizeof kRoadSpacingStub);
    int32_t rel = (int32_t)((int64_t)back - (int64_t)(uintptr_t)(stub + 27)); memcpy(stub + 23, &rel, 4);
    rel = (int32_t)((int64_t)constant - (int64_t)(uintptr_t)(stub + 35)); memcpy(stub + 31, &rel, 4);
    rel = (int32_t)((int64_t)back - (int64_t)(uintptr_t)(stub + 40)); memcpy(stub + 36, &rel, 4);
    memset(stub + 0x30, 0, 8);
    const float game = ROADS_SPACING_GAME;
    memcpy(stub + 0x38, &game, 4);
}
static struct { volatile uint64_t* key; volatile float* spacing; LONG level; } g_roadSpacing = {.level = -1};
static const char* InstallRoadSpacing(uintptr_t game)
{
    float five = 0;
    if (!ApiCodeMatches(game + 0xF386AFu, "48 89 4C 24 78") ||
        !ApiCodeMatches(game + 0xF38D21u, "41 0F 28 C4 F3 0F 5E 05 77 E7 48 01 F3 44 0F 58 F1 F3 41 0F 58 C5 F3 48 0F 2C F0 8D 46 02 3B C8") ||
        SafeRead(game + 0x23C74A4u, &five, 4) || five != ROADS_SPACING_GAME)
        return "op 0x0E's road points are not the reviewed ones; a large level's last route edges stay without points";
    uint8_t* stub = AllocateNearGame(game, 0x40);
    if (stub == NULL) return "no memory within 2 GB of game.dll for op 0x0E's road point spacing";
    RoadSpacingBuild(stub, game + 0x23C74A4u, game + 0xF38D2Du);
    uint8_t jump[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    int32_t rel = DifficultyJumpRel(game + 0xF38D25u, (uintptr_t)stub); memcpy(jump + 1, &rel, 4);
    if (ApiWrite(game + 0xF38D25u, jump, 8)) return "op 0x0E's road point spacing could not be written";
    g_roadSpacing.key = (volatile uint64_t*)(stub + 0x30);
    g_roadSpacing.spacing = (volatile float*)(stub + 0x38);
    return NULL;
}

/* Op 0x0E's first pass (0xF387FA..0xF38A59): each of its entries (0x40: node pointers +0 / +8, edge pointer +0x38)
 * whose edge has no flag 0x20 and no points takes ceil(length / 2) + 1 points, unchecked. taken[] marks those edges;
 * the points they take are added to *used. 0 when an entry cannot be read. */
static int RoadsFirstPass(uint64_t pass, uintptr_t edgesAt, const RoadEdge* e, uint32_t n, uint8_t* taken, uint32_t* used)
{
    uint64_t entries = 0, countAt = 0; uint32_t count = 0;
    if (SafeRead(pass + 0x30, &entries, 8) || SafeRead(pass + 0x50, &countAt, 8) || countAt == 0 || SafeRead(countAt, &count, 4) || count > 65536u) return 0;
    for (uint32_t k = 0; k < count; k++)
    {
        uint64_t entry[8]; float a[2], b[2];
        if (SafeRead(entries + 0x40ull * k, entry, sizeof entry)) return 0;
        if (entry[7] < edgesAt || (entry[7] - edgesAt) % 0x48 || (entry[7] - edgesAt) / 0x48 >= n) continue;
        const uint32_t i = (uint32_t)((entry[7] - edgesAt) / 0x48);
        if ((e[i].flags & 0x20) || e[i].end > e[i].first || taken[i]) continue;
        if (SafeRead(entry[0], a, 8) || SafeRead(entry[1], b, 8)) return 0;
        const float dx = a[0] - b[0], dy = a[1] - b[1], length = sqrtf(dx * dx + dy * dy);
        if (!(length > 0.0f)) continue;
        taken[i] = 1;
        *used += (length < 1.0e6f ? (uint32_t)ceilf(length * 0.5f) : 1000000u) + 1;
    }
    return 1;
}

static uint32_t RoadsSpurReserve(void);
/* Before the level's op 0x0E: the spacing its edges need, and the pass as the stub's key. Once a level. */
static void RoadsSpacingBegin(uintptr_t block, uint64_t record)
{
    if (g_roadSpacing.key == NULL || g_roadSpacing.level == g_world.levels) return;
    g_roadSpacing.level = g_world.levels;
    const uint64_t pass = record + 8;
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    uint64_t edgesAt = 0, usedAt = 0; uint32_t used = 0, capacity = 0;
    RoadEdge* e = NULL;
    /* the pass must be this level's: its edges are the block's */
    if (SafeRead(pass + 0x38, &edgesAt, 8) || edgesAt != block + L.edgesAt || SafeRead(pass + 0x70, &usedAt, 8) || usedAt == 0 ||
        SafeRead(usedAt, &used, 4) || SafeRead(pass + 0x78, &capacity, 4))
    { Note("roads: level %ld road points: op 0x0E's pass is not readable; the game's 5.00 m", (long)g_world.levels); return; }
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    uint8_t* taken = n ? (uint8_t*)VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : NULL;
    if (taken == NULL || !RoadsFirstPass(pass, (uintptr_t)edgesAt, e, n, taken, &used))
    {
        Note("roads: level %ld road points: the route edges are not readable; the game's 5.00 m", (long)g_world.levels);
        RoadsFreeEdges(e, n);
        if (taken != NULL) VirtualFree(taken, 0, MEM_RELEASE);
        return;
    }
    if (capacity > ROADS_POINT_IDS) capacity = ROADS_POINT_IDS; /* op 0x0F names a point in 16 bits */
    const uint32_t reserve = RoadsSpurReserve(); /* the spurs' points come after op 0x0F, from the same places */
    capacity = capacity > used + reserve ? capacity - reserve : used;
    uint32_t atGame = 0, total = 0, left = 0;
    const uint32_t leftAtGame = RoadsSpacingLeftOut(e, n, taken, used, capacity, ROADS_SPACING_GAME, &atGame);
    const float spacing = RoadsSpacingChoose(e, n, taken, used, capacity, &total, &left);
    *g_roadSpacing.spacing = spacing;
    *g_roadSpacing.key = pass;
    Note("roads: level %ld road points every %.2f m: %u of %u places used, %u edges left without points (every 5.00 m: %u edges left without)",
        (long)g_world.levels, (double)spacing, total, capacity, left, leftAtGame);
    RoadsFreeEdges(e, n);
    VirtualFree(taken, 0, MEM_RELEASE);
}
static void RoadsSpacingEnd(void) { if (g_roadSpacing.key != NULL) *g_roadSpacing.key = 0; }

/* ---- the arterials ---- */
typedef struct RoadsGraph
{
    const RoadEdge* e;
    const uint32_t* ends; /* two node indices an edge; ROADS_NO_NODE when the edge names none */
    const float* nodeXY;
    uint32_t edges, nodes;
} RoadsGraph;
/* A nature map's open edge (type 3: 0xFD0731) with the width op 0x08 gave it (a preview's edges have none); a closed
 * one (flag 2) gets no points and no corridor. */
static int RoadEdgeArterialOk(const RoadsGraph* g, uint32_t i)
{
    return g->e[i].type == 3 && !(g->e[i].flags & 2) && g->e[i].width > 0.0f && g->ends[2 * i] < g->nodes && g->ends[2 * i + 1] < g->nodes && g->ends[2 * i] != g->ends[2 * i + 1];
}
typedef struct RoadsHeapItem { uint64_t d; uint32_t node; } RoadsHeapItem;
static int RoadsHeapLess(const RoadsHeapItem* a, const RoadsHeapItem* b) { return a->d < b->d || (a->d == b->d && a->node < b->node); }
static void RoadsHeapPush(RoadsHeapItem* h, uint32_t* n, uint64_t d, uint32_t node)
{
    uint32_t i = (*n)++;
    while (i > 0)
    {
        const uint32_t up = (i - 1) / 2;
        const RoadsHeapItem item = {d, node};
        if (!RoadsHeapLess(&item, &h[up])) break;
        h[i] = h[up]; i = up;
    }
    h[i].d = d; h[i].node = node;
}
static RoadsHeapItem RoadsHeapPop(RoadsHeapItem* h, uint32_t* n)
{
    const RoadsHeapItem top = h[0], last = h[--(*n)];
    uint32_t i = 0;
    for (;;)
    {
        uint32_t c = 2 * i + 1;
        if (c >= *n) break;
        if (c + 1 < *n && RoadsHeapLess(&h[c + 1], &h[c])) c++;
        if (!RoadsHeapLess(&h[c], &last)) break;
        h[i] = h[c]; i = c;
    }
    h[i] = last;
    return top;
}
/* chosen[i] = 1 for the arterial edges: the flag-8 edges, and the cheapest paths (by chord, an arterial edge free; ties
 * to the lower node, then the lower edge) that join them and the node nearest each terminal into one network, grown
 * from the lowest wanted node. Returns their count; *unreached: wanted nodes no usable edge joins to it. Integer
 * costs and fixed orders only: every machine of a session chooses the same edges. */
static uint32_t RoadsArterials(const RoadsGraph* g, const float* termXY, uint32_t terms, uint8_t* chosen, uint32_t* unreached)
{
    *unreached = 0;
    memset(chosen, 0, g->edges);
    if (g->nodes == 0 || g->edges == 0 || g->nodes > ROADS_GRAPH_NODES || g->edges > ROADS_GRAPH_EDGES) return 0;
    const size_t heapItems = 2 * (size_t)g->edges + g->nodes, words = 3 * (size_t)g->edges + 3 * (size_t)g->nodes + 1;
    uint8_t* mem = (uint8_t*)VirtualAlloc(NULL, (size_t)g->nodes * 8 + heapItems * sizeof(RoadsHeapItem) + words * 4 + 2 * (size_t)g->nodes + g->edges,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); /* zeroed: start[], want[] and inTree[] begin at 0 */
    if (mem == NULL) return 0;
    uint64_t* dist = (uint64_t*)mem;
    RoadsHeapItem* heap = (RoadsHeapItem*)(dist + g->nodes);
    uint32_t* cost = (uint32_t*)(heap + heapItems); uint32_t* start = cost + g->edges; uint32_t* fill = start + g->nodes + 1;
    uint32_t* via = fill + g->nodes; uint32_t* list = via + g->nodes;
    uint8_t* want = (uint8_t*)(list + 2 * (size_t)g->edges); uint8_t* inTree = want + g->nodes; uint8_t* usable = inTree + g->nodes;
    /* each node's edges, ascending */
    for (uint32_t i = 0; i < g->edges; i++)
    {
        usable[i] = (uint8_t)RoadEdgeArterialOk(g, i);
        if (!usable[i]) continue;
        const float dx = g->e[i].a[0] - g->e[i].b[0], dy = g->e[i].a[1] - g->e[i].b[1], length = sqrtf(dx * dx + dy * dy);
        cost[i] = length < 1.0e6f ? (uint32_t)(length * 16.0f + 0.5f) : 16000000u; /* 1/16 m; not a number: the most */
        start[g->ends[2 * i] + 1]++; start[g->ends[2 * i + 1] + 1]++;
        if (g->e[i].flags & 8) { chosen[i] = 1; want[g->ends[2 * i]] = 1; want[g->ends[2 * i + 1]] = 1; }
    }
    for (uint32_t v = 0; v < g->nodes; v++) { start[v + 1] += start[v]; fill[v] = start[v]; }
    for (uint32_t i = 0; i < g->edges; i++)
        if (usable[i]) { list[fill[g->ends[2 * i]]++] = i; list[fill[g->ends[2 * i + 1]]++] = i; }
    /* each terminal's nearest node that has a usable edge */
    for (uint32_t t = 0; t < terms; t++)
    {
        uint32_t best = ROADS_NO_NODE; double bestD = 0;
        for (uint32_t v = 0; v < g->nodes; v++)
        {
            if (start[v + 1] == start[v]) continue;
            const double dx = (double)g->nodeXY[2 * v] - termXY[2 * t], dy = (double)g->nodeXY[2 * v + 1] - termXY[2 * t + 1], d = dx * dx + dy * dy;
            if (best == ROADS_NO_NODE || d < bestD) { best = v; bestD = d; }
        }
        if (best != ROADS_NO_NODE) want[best] = 1;
    }
    uint32_t first = 0;
    while (first < g->nodes && !want[first]) first++;
    if (first < g->nodes) inTree[first] = 1;
    for (int more = first < g->nodes; more;)
    {
        uint32_t held = 0, hit = ROADS_NO_NODE;
        for (uint32_t v = 0; v < g->nodes; v++)
        {
            dist[v] = inTree[v] ? 0 : UINT64_MAX;
            if (inTree[v]) RoadsHeapPush(heap, &held, 0, v);
        }
        while (held > 0 && hit == ROADS_NO_NODE)
        {
            const RoadsHeapItem at = RoadsHeapPop(heap, &held);
            if (at.d != dist[at.node]) continue;
            if (want[at.node] && !inTree[at.node]) { hit = at.node; break; }
            for (uint32_t k = start[at.node]; k < start[at.node + 1]; k++)
            {
                const uint32_t i = list[k], to = g->ends[2 * i] == at.node ? g->ends[2 * i + 1] : g->ends[2 * i];
                const uint64_t d = at.d + (chosen[i] ? 0u : cost[i]);
                if (d < dist[to]) { dist[to] = d; via[to] = i; RoadsHeapPush(heap, &held, d, to); }
            }
        }
        more = hit != ROADS_NO_NODE;
        for (uint32_t v = hit; more && !inTree[v];)
        {
            const uint32_t i = via[v];
            chosen[i] = 1; inTree[v] = 1;
            v = g->ends[2 * i] == v ? g->ends[2 * i + 1] : g->ends[2 * i];
        }
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < g->edges; i++) count += chosen[i];
    for (uint32_t v = 0; v < g->nodes; v++) *unreached += want[v] && !inTree[v];
    VirtualFree(mem, 0, MEM_RELEASE);
    return count;
}

/* The level's arterial edges, for what follows them (spurs, the stamp thinning, the paint, the living world's routes):
 * bit i is edge i of the level's route edges (G + g_routeEdges), valid while `level` is g_world.levels. */
static struct { LONG level; uint32_t count; uint8_t bits[ROADS_EDGES_MAX / 8]; } g_arterials = {.level = -1};

static void RoadsSpurTerminals(const float* xy, const float* gap, uint32_t terms);
/* After the level's op 0x86: the arterials are chosen and take their width (never a narrower one). The terminals are
 * the locations ops 0x16, 0x17 and 0x82 placed (ops[i]: the op that appended location i). */
static void RoadsArterialsApply(uintptr_t block, const uint8_t* ops, uint32_t total)
{
    static float termXY[2 * 2 * ROADS_GROUP_MAX];
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    const uintptr_t nodesAt = block + g_routeNodes;
    uint32_t nodes = 0, offset = 0, terms = 0;
    RoadEdge* e = NULL;
    g_arterials.level = g_world.levels; g_arterials.count = 0;
    memset(g_arterials.bits, 0, sizeof g_arterials.bits);
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    if (n == 0 || SafeRead(block + 0x11A71B4u, &nodes, 4) || nodes == 0 || nodes > ROADS_GRAPH_NODES || n > ROADS_GRAPH_EDGES ||
        SafeRead(g_world.game + 0xC176A7 + 3, &offset, 4) || offset < 0x90)
    { Note("roads: level %ld arterials: the route graph is not readable; no edge widened", (long)g_world.levels); RoadsFreeEdges(e, n); return; }
    uint8_t* mem = (uint8_t*)VirtualAlloc(NULL, (size_t)nodes * 8 + (size_t)n * 9, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (mem == NULL) { RoadsFreeEdges(e, n); return; }
    float* nodeXY = (float*)mem; uint32_t* ends = (uint32_t*)(nodeXY + 2 * (size_t)nodes); uint8_t* chosen = (uint8_t*)(ends + 2 * (size_t)n);
    const uint8_t* raw = (const uint8_t*)e - (size_t)n * 0x48;
    int readable = 1; uint32_t open = 0;
    for (uint32_t v = 0; v < nodes && readable; v++) readable = !SafeRead(nodesAt + 0x98ull * v, &nodeXY[2 * v], 8);
    for (uint32_t i = 0; i < n; i++)
        for (int q = 0; q < 2; q++)
        {
            uint64_t p; memcpy(&p, raw + 0x48ull * i + 0x10 + 8 * q, 8);
            ends[2 * i + q] = p >= nodesAt && (p - nodesAt) % 0x98 == 0 && (p - nodesAt) / 0x98 < nodes ? (uint32_t)((p - nodesAt) / 0x98) : ROADS_NO_NODE;
        }
    for (uint32_t i = 0; i < total && terms < 2 * ROADS_GROUP_MAX; i++)
        if ((ops[i] == 0x16 || ops[i] == 0x17 || ops[i] == 0x82) && !SafeRead(block + (offset - 0x90) + 0xE0ull * i, &termXY[2 * terms], 8)) terms++;
    const RoadsGraph g = {e, ends, nodeXY, n, nodes};
    for (uint32_t i = 0; i < n; i++) open += (uint32_t)RoadEdgeArterialOk(&g, i);
    if (!readable || open == 0)
    {
        Note("roads: level %ld arterials: %s; no edge widened", (long)g_world.levels, readable ? "no open type-3 route edge with a width (a structured map, or a preview)" : "the route nodes are not readable");
        VirtualFree(mem, 0, MEM_RELEASE); RoadsFreeEdges(e, n);
        return;
    }
    uint32_t unreached = 0, skeleton = 0, widened = 0, hash = 2166136261u; double km = 0;
    g_arterials.count = RoadsArterials(&g, termXY, terms, chosen, &unreached);
    for (uint32_t i = 0; i < n; i++)
    {
        if (!chosen[i]) continue;
        g_arterials.bits[i >> 3] |= (uint8_t)(1u << (i & 7));
        for (int q = 0; q < 4; q++) hash = (hash ^ ((i >> (8 * q)) & 0xFFu)) * 16777619u; /* the road set, to compare between machines */
        km += RoadEdgeKm(&e[i]); skeleton += (e[i].flags & 8) != 0;
        if (e[i].width < ROADS_ARTERIAL_WIDTH) { *(volatile float*)(block + L.edgesAt + 0x48ull * i + 0x30) = ROADS_ARTERIAL_WIDTH; widened++; }
    }
    /* what the spurs have to cover: each terminal's distance to the nearest arterial edge's chord */
    static float gap[2 * ROADS_GROUP_MAX];
    float gapMedian, gapMost;
    for (uint32_t t = 0; t < terms; t++)
    {
        gap[t] = ROADS_NONE;
        for (uint32_t i = 0; i < n; i++) if (chosen[i]) gap[t] = RoadsEdgeDistance(termXY[2 * t], termXY[2 * t + 1], &e[i], NULL, gap[t]);
    }
    RoadsSpurTerminals(termXY, gap, terms);
    RoadsSummary(gap, terms, &gapMedian, &gapMost);
    Note("roads: level %ld arterials: %u of %u open route edges (%.1f km; %u with flag 0x8) join %u objectives and outposts; %u widened to %.1f m, "
        "%u wanted nodes not joined; the places lie median %.0f m, at most %.0f m from them; road set %08X", (long)g_world.levels, g_arterials.count, open,
        km, skeleton, terms, widened, (double)ROADS_ARTERIAL_WIDTH, unreached, (double)gapMedian, (double)gapMost, hash);
    VirtualFree(mem, 0, MEM_RELEASE);
    RoadsFreeEdges(e, n);
}

/* After the level's op 0x0E: the arterial edges it left without points have no path and no corridor (none is the aim). */
static void RoadsArterialsPoints(uintptr_t block)
{
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL;
    uint32_t points = 0, empty = 0;
    if (g_arterials.level != g_world.levels || g_arterials.count == 0) return;
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    if (n == 0 || SafeRead(block + 0x11A71D4u, &points, 4)) { RoadsFreeEdges(e, n); return; }
    for (uint32_t i = 0; i < n; i++) empty += ((g_arterials.bits[i >> 3] >> (i & 7)) & 1) && !RoadEdgeHasPoints(&e[i], points);
    Note("roads: level %ld arterials after op 0x0E: %u of %u arterial edges without points (%u road points)", (long)g_world.levels, empty, g_arterials.count, points);
    RoadsFreeEdges(e, n);
}

/* ---- the spurs ----
 * A place lies off the route graph (its edges are the borders between route cells), so each terminal gets a straight
 * spur to the nearest point of an arterial's path. A spur is not a route edge: its points are appended to the level's
 * road points and each pair goes into the level's stamp index as op 0x0F puts a route's (0xC14EE0..0xC14F1E: capsule
 * {p0, p1, r, r} and the id, point index | (edge << 2 | 1) << 16, to 0xFCADC0 with the index [context + 0x20]). The
 * edge named is the arterial it joins: the stamp tests (0xF30D40, the mod's merge) read the width from it. */
#define ROADS_SPUR_STEP 10.0f    /* a pair as long as a route's own: the index holds boxes of that size */
#define ROADS_SPUR_MOST 1000.0f  /* a place farther from the network gets none */
#define ROADS_SPUR_SLACK 40.0f   /* the path's meander about the chord the reserve is counted from */
#define ROADS_SPURS (2 * ROADS_GROUP_MAX)
typedef struct RoadSpur { float from[2], to[2]; uint32_t edge, first, end; } RoadSpur; /* place, the arterial's point; points [first, end) */
static struct { LONG level; uint32_t terms, reserve, count; float xy[2 * ROADS_SPURS]; RoadSpur spur[ROADS_SPURS]; } g_spurs = {.level = -1};
typedef void (*RoadCapsuleFn)(uint64_t index, const float* capsule, uint32_t id);
static RoadCapsuleFn g_roadCapsule; /* tests: in place of the game's 0xFCADC0 */

static uint32_t RoadsSpurCount(float length) { return length < ROADS_SPUR_MOST ? (uint32_t)ceilf(length / ROADS_SPUR_STEP) + 1 : 0; }
static void RoadsSpurTerminals(const float* xy, const float* gap, uint32_t terms)
{
    g_spurs.level = g_world.levels; g_spurs.terms = terms; g_spurs.reserve = 0; g_spurs.count = 0;
    memcpy(g_spurs.xy, xy, (size_t)terms * 8);
    for (uint32_t t = 0; t < terms; t++) g_spurs.reserve += RoadsSpurCount(gap[t] + ROADS_SPUR_SLACK);
}
static uint32_t RoadsSpurReserve(void) { return g_spurs.level == g_world.levels ? g_spurs.reserve : 0; }

/* The nearest point of the arterials' paths (bits: the arterial edges; the lower edge and point on a tie); the
 * distance, ROADS_NONE when no arterial has points. */
static float RoadsSpurAttach(float x, float y, const RoadEdge* e, uint32_t n, const uint8_t* bits, const float* pts, uint32_t points, float to[2], uint32_t* edge)
{
    float best = ROADS_NONE;
    for (uint32_t i = 0; i < n; i++)
    {
        if (!((bits[i >> 3] >> (i & 7)) & 1) || !RoadEdgeHasPoints(&e[i], points)) continue;
        for (uint32_t p = e[i].first + 1; p < e[i].end; p++)
        {
            const float ax = pts[2 * (p - 1)], ay = pts[2 * (p - 1) + 1], dx = pts[2 * p] - ax, dy = pts[2 * p + 1] - ay, l2 = dx * dx + dy * dy;
            float t = 0.0f;
            if (l2 > 1.0e-9f) { t = ((x - ax) * dx + (y - ay) * dy) / l2; t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t; }
            const float qx = ax + t * dx, qy = ay + t * dy, d = sqrtf((x - qx) * (x - qx) + (y - qy) * (y - qy));
            if (d < best) { best = d; to[0] = qx; to[1] = qy; *edge = i; }
        }
    }
    return best;
}
/* count points from `from` to `to`, both included, evenly */
static void RoadsSpurPoints(const float from[2], const float to[2], uint32_t count, float* out)
{
    for (uint32_t k = 0; k < count; k++)
    {
        const float t = (float)k / (float)(count - 1);
        out[2 * k] = from[0] + (to[0] - from[0]) * t; out[2 * k + 1] = from[1] + (to[1] - from[1]) * t;
    }
}

/* After the level's op 0x0F: the spurs' points and their capsules in the level's index, before any stamp is placed. */
static void RoadsSpursBuild(uintptr_t block, uint64_t context)
{
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL;
    uint64_t index = 0; uint32_t points = 0;
    if (g_spurs.level != g_world.levels || g_arterials.level != g_world.levels || g_arterials.count == 0 || g_spurs.terms == 0) return;
    const RoadCapsuleFn capsule = g_roadCapsule != NULL ? g_roadCapsule :
        ApiCodeMatches(g_world.game + 0xFCADC0u, "48 89 5C 24 10 57 48 83 EC 60 F3 0F 10 4A 08") ? (RoadCapsuleFn)(g_world.game + 0xFCADC0u) : NULL;
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    float* pts = RoadsReadPoints(block, &L, &points);
    uint32_t room = co_gen_places(HF_GEN_ROUTE_POINTS);
    if (room > ROADS_POINT_IDS) room = ROADS_POINT_IDS;
    if (capsule == NULL || n == 0 || pts == NULL || SafeRead(context + 0x20, &index, 8) || index == 0)
    {
        Note("roads: level %ld spurs: the stamp index or the road points are not readable; no spur made", (long)g_world.levels);
        RoadsFreeEdges(e, n); if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE);
        return;
    }
    uint32_t onRoad = 0, tooFar = 0, noRoom = 0, at = points; double km = 0; float longest = 0;
    static float line[2 * 128];
    for (uint32_t t = 0; t < g_spurs.terms; t++)
    {
        RoadSpur* s = &g_spurs.spur[g_spurs.count];
        memcpy(s->from, &g_spurs.xy[2 * t], 8);
        const float d = RoadsSpurAttach(s->from[0], s->from[1], e, n, g_arterials.bits, pts, points, s->to, &s->edge);
        const uint32_t count = RoadsSpurCount(d);
        if (d < 1.0f) { onRoad++; continue; }
        if (count == 0 || count > 128) { tooFar++; continue; }
        if (at + count > room) { noRoom++; continue; }
        RoadsSpurPoints(s->from, s->to, count, line);
        const float r = 0.5f * (e[s->edge].width > ROADS_ARTERIAL_WIDTH ? e[s->edge].width : ROADS_ARTERIAL_WIDTH);
        for (uint32_t k = 0; k < count; k++)
        {
            uint64_t xy; memcpy(&xy, &line[2 * k], 8);
            *(volatile uint64_t*)(block + L.pointsAt + 8ull * (at + k)) = xy;
            if (k == 0) continue;
            const float c[6] = {line[2 * k - 2], line[2 * k - 1], line[2 * k], line[2 * k + 1], r, r};
            capsule(index, c, (at + k) | ((s->edge << 2 | 1u) << 16));
        }
        s->first = at; s->end = at + count; at += count;
        g_spurs.count++; km += d / 1000.0; if (d > longest) longest = d;
    }
    if (at != points) *(volatile uint32_t*)(block + 0x11A71D4u) = at;
    Note("roads: level %ld spurs: %u of %u places joined to the arterials (%.1f km, the longest %.0f m; %u road points of %u kept for them), "
        "%u on a road already, %u too far, %u without room", (long)g_world.levels, g_spurs.count, g_spurs.terms, km, (double)longest, at - points,
        g_spurs.reserve, onRoad, tooFar, noRoom);
    RoadsFreeEdges(e, n);
    VirtualFree(pts, 0, MEM_RELEASE);
}
