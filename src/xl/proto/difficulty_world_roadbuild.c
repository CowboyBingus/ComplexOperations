/* ---- Drivable roads on a complex level: what its generation is given ----------------------------------------------
 * Two changes, both on the level's own ops only (a tile's ops and every other level run the game's code and values):
 * - Road points. Op 0x0E (0xF38640) gives a route edge one point per 5 m of its chord (0xF38D21..0xF38D37: n =
 *   chord / 5.0 + 1, truncated; n + 1 points) and leaves an edge without points when fewer than n + 2 of its pass's
 *   places are free (0xF38CBF..0xF38D41). A 16 km level asks for more than the 65535 ids op 0x0F can name, so edges
 *   went without points, and so without a path or a stamp corridor. The level's op 0x0E divides by the smallest
 *   spacing, in steps of 0.25 m from 5, at which every edge gets its points.
 * - Arterials. Before op 0x0E, on the edges as it reads them (op 0x86's paths between important locations carry flag 8;
 *   the zone passes after it close edges, flag 2, and a closed edge gets no points), those edges and the shortest
 *   connections from them to the node nearest each objective, outpost and op 0x17 place take 8 m, the width two vehicles pass on (a convoy
 *   map's op 0x87 raises its convoy edges to 10, 0xF8B264). Every later reader of the width (op 0x0F's capsules, the stamp tests, op 0x0D's line) sees it. */
#define ROADS_SPACING_GAME 5.0f
#define ROADS_SPACING_STEP 0.25f
/* Op 0x04 (0xF647A0) measures to the nearest point, not to the line: at 10 m the middle of a pair is 5 m from one,
 * inside an arterial's width, so the ground along it is treated as road throughout. */
#define ROADS_SPACING_MOST 10.0f
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
#pragma STDC FP_CONTRACT OFF /* mulss, mulss, addss as the game: a fused multiply-add could differ in the last bit */
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
    DWORD protect; /* the allocator's pages are data: op 0x0E jumps into this one */
    if (!VirtualProtect(stub, 0x40, PAGE_EXECUTE_READWRITE, &protect)) return "op 0x0E's road point spacing stub could not be made code";
    FlushInstructionCache(GetCurrentProcess(), stub, 0x40);
    uint8_t jump[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    int32_t rel = DifficultyJumpRel(game + 0xF38D25u, (uintptr_t)stub); memcpy(jump + 1, &rel, 4);
    /* the jump goes in last: until then the game's own division runs, and a stub with no key divides by the game's 5.0 */
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

/* ---- No stamp across an arterial ----
 * Op 0x2B's stamp test (0xF30B60) rejects a candidate whose shapes reach into a road's half width (0xFCC180, depth
 * over 0), but a type whose definition has bit 0 at +0x170 (0xF345E8) may reach in up to 0.55 x the width
 * (0xF30DA8..0xF30DC4): past the middle of the road. During the level's own op 0x2B a road of an arterial's width
 * takes no such stamp: the stub answers 0xF30DA8's test as for a type without the bit. xmm6 is the road's width there
 * (0xF30D72); al, the overlap, is kept. */
static const uint8_t kRoadToleranceStub[42] = {
    0xF3, 0x0F, 0x10, 0x45, 0x98,                   /*  0 movss xmm0, [rbp-0x68] (the depth, as 0xF30DAF) */
    0x80, 0x3D, 0x2C, 0x00, 0x00, 0x00, 0x00,       /*  5 cmp byte [rip -> +0x38], 0 (the level's own op 0x2B runs) */
    0x74, 0x10,                                     /* 12 je 30 */
    0x0F, 0x2F, 0x35, 0x27, 0x00, 0x00, 0x00,       /* 14 comiss xmm6, [rip -> +0x3C] (an arterial's width) */
    0x72, 0x07,                                     /* 21 jb 30 */
    0x38, 0xC0,                                     /* 23 cmp al, al (equal: no depth is let through) */
    0xE9, 0, 0, 0, 0,                               /* 25 jmp 0xF30DB4 (rel32 at 26) */
    0x80, 0xBD, 0xF8, 0x20, 0x00, 0x00, 0x00,       /* 30 cmp byte [rbp+0x20F8], 0 (the game's test) */
    0xE9, 0, 0, 0, 0};                              /* 37 jmp 0xF30DB4 (rel32 at 38) */
static volatile uint8_t* g_roadTolerance; /* the stub's switch (+0x38); null: not installed */
static void RoadToleranceBuild(uint8_t* stub, uintptr_t back)
{
    memset(stub, 0xCC, 0x40);
    memcpy(stub, kRoadToleranceStub, sizeof kRoadToleranceStub);
    int32_t rel = (int32_t)((int64_t)back - (int64_t)(uintptr_t)(stub + 30)); memcpy(stub + 26, &rel, 4);
    rel = (int32_t)((int64_t)back - (int64_t)(uintptr_t)(stub + 42)); memcpy(stub + 38, &rel, 4);
    const float width = ROADS_ARTERIAL_WIDTH;
    memset(stub + 0x38, 0, 4); memcpy(stub + 0x3C, &width, 4);
}
static const char* InstallRoadTolerance(uintptr_t game)
{
    if (!ApiCodeMatches(game + 0xF30D72u, "F3 41 0F 10 74 C2 30") || !ApiCodeMatches(game + 0xF30DA3u, "E8 D8 B3 09 00 80 BD F8 20 00 00 00 F3 0F 10 45 98 74 10 F3 0F 59 35") ||
        !ApiCodeMatches(game + 0xF345E8u, "0F B6 88 70 01 00 00 8B 45 D0 80 E1 01 88 4C 24 58"))
        return "op 0x2B's road test is not the reviewed one; a stamp type that may overlap a road keeps doing so on an arterial";
    uint8_t* stub = AllocateNearGame(game, 0x40);
    if (stub == NULL) return "no memory within 2 GB of game.dll for op 0x2B's road test";
    RoadToleranceBuild(stub, game + 0xF30DB4u);
    DWORD protect; /* the switch is in the page the test jumps into: code that can be written */
    if (!VirtualProtect(stub, 0x40, PAGE_EXECUTE_READWRITE, &protect)) return "op 0x2B's road test stub could not be made code";
    FlushInstructionCache(GetCurrentProcess(), stub, 0x40);
    uint8_t jump[12] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    int32_t rel = DifficultyJumpRel(game + 0xF30DA8u, (uintptr_t)stub); memcpy(jump + 1, &rel, 4);
    /* the jump goes in last; with the switch off the stub runs the game's own test */
    if (ApiWrite(game + 0xF30DA8u, jump, 12)) return "op 0x2B's road test could not be written";
    g_roadTolerance = stub + 0x38;
    return NULL;
}
static void RoadsToleranceEnd(void) { if (g_roadTolerance != NULL) *g_roadTolerance = 0; }

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
    /* the extraction is the place the game's own test names (0xF78A4D), whatever op appended it */
    uint32_t important = 0, extractions = 0;
    SafeRead(block + 0x11A715Cu, &important, 4);
    for (uint32_t i = 0; i < total && terms < 2 * ROADS_GROUP_MAX; i++)
    {
        const int extraction = i < important && g_importantList != 0 && ClusterExtraction(block + g_importantList + 0x108ull * i);
        if (!extraction && ops[i] != 0x16 && ops[i] != 0x17 && ops[i] != 0x82) continue;
        if (!SafeRead(block + (offset - 0x90) + 0xE0ull * i, &termXY[2 * terms], 8)) { terms++; extractions += (uint32_t)extraction; }
    }
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
    Note("roads: level %ld arterials: %u of %u open route edges (%.1f km; %u with flag 0x8) join %u objectives, outposts and op 0x17 places (%u an extraction); %u widened to %.1f m, "
        "%u wanted nodes not joined; the places lie median %.0f m, at most %.0f m from them; road set %08X", (long)g_world.levels, g_arterials.count, open,
        km, skeleton, terms, extractions, widened, (double)ROADS_ARTERIAL_WIDTH, unreached, (double)gapMedian, (double)gapMost, hash);
    VirtualFree(mem, 0, MEM_RELEASE);
    RoadsFreeEdges(e, n);
}

/* Before the level's own op 0x2B: its road test takes no tolerant stamp on an arterial while the level has any. */
static void RoadsToleranceBegin(void) { if (g_roadTolerance != NULL) *g_roadTolerance = g_arterials.level == g_world.levels && g_arterials.count != 0; }

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
static struct { LONG level; uint32_t terms, reserve, count, points; float xy[2 * ROADS_SPURS]; RoadSpur spur[ROADS_SPURS]; } g_spurs = {.level = -1}; /* points: the count the spurs left */
typedef void (*RoadCapsuleFn)(uint64_t index, const float* capsule, uint32_t id);
static RoadCapsuleFn g_roadCapsule; /* tests: in place of the game's 0xFCADC0 */

static uint32_t RoadsSpurCount(float length) { return length < ROADS_SPUR_MOST ? (uint32_t)ceilf(length / ROADS_SPUR_STEP) + 1 : 0; }
static void RoadsSpurTerminals(const float* xy, const float* gap, uint32_t terms)
{
    g_spurs.level = g_world.levels; g_spurs.terms = terms; g_spurs.reserve = 0; g_spurs.count = 0; g_spurs.points = 0;
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

/* The stamp index's free places, counted to `most`: its free list starts at u32 [index + 8 + 0x2C] (0xFFFFFFFF: none,
 * 0xC04F1C), each free place's first u32 names the next ([index + 8 + 0x30] + place x u16 [index + 8 + 0x24],
 * 0xC04F3C..0xC04F7A). 0xFCADC0 writes through what 0xC04EF0 returns (0xFCAE69), also when that is null. -1: not readable. */
static int RoadsIndexFree(uint64_t index, uint32_t most)
{
    uint32_t head = 0, n = 0; uint16_t stride = 0; uint64_t base = 0;
    if (SafeRead(index + 8 + 0x2C, &head, 4) || SafeRead(index + 8 + 0x24, &stride, 2) || SafeRead(index + 8 + 0x30, &base, 8)) return -1;
    while (head != 0xFFFFFFFFu && n < most)
    {
        if (SafeRead(base + (uint32_t)(stride * head), &head, 4)) return -1;
        n++;
    }
    return (int)n;
}
#define ROADS_INDEX_COUNT 0x400000u
static void RoadsIndexNote(uint64_t context, const char* when)
{
    uint64_t index = 0;
    if (g_spurs.level != g_world.levels || SafeRead(context + 0x20, &index, 8) || index == 0) return;
    Note("roads: level %ld stamp index %s: %d places free", (long)g_world.levels, when, RoadsIndexFree(index, ROADS_INDEX_COUNT));
}
/* The road point count after an op that could touch the points: it must stay what the spurs left. */
static void RoadsPointsWatch(uintptr_t block, uint32_t op)
{
    if (g_spurs.level != g_world.levels || g_spurs.points == 0) return;
    if (op != 0x03 && op != 0x09 && op != 0x5A && op != 0x8F && op != 0x3D && op != 0x0D && op != 0x41) return;
    uint32_t now = 0;
    SafeRead(block + 0x11A71D4u, &now, 4);
    Note("roads: level %ld road points after op 0x%02X: %u (the spurs left %u)%s", (long)g_world.levels, op, now, g_spurs.points, now == g_spurs.points ? "" : ": CHANGED");
}

/* roadlines-<level>.tsv, so a run can stand on the roads: a row per point in path order, world metres (the generator's
 * less half the world): "a", edge, width, x, y for an arterial's; "s", spur, the edge it joins, x, y for a spur's,
 * from its place to the arterial. */
static void RoadsLinesFile(uintptr_t block)
{
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL; uint32_t points = 0, rows = 0;
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    float* pts = RoadsReadPoints(block, &L, &points);
    wchar_t name[48];
    swprintf(name, 48, L"roadlines-%ld.tsv", (long)g_world.levels);
    HANDLE file = pts != NULL ? OpenLog(name) : INVALID_HANDLE_VALUE;
    const float half = 1024.0f * g_world.current;
    char line[96]; DWORD wrote;
    for (uint32_t i = 0; file != INVALID_HANDLE_VALUE && i < n; i++)
    {
        if (!((g_arterials.bits[i >> 3] >> (i & 7)) & 1) || !RoadEdgeHasPoints(&e[i], points)) continue;
        for (uint32_t p = e[i].first; p < e[i].end; p++, rows++)
        {
            const int len = snprintf(line, sizeof line, "a\t%u\t%.1f\t%.1f\t%.1f\n", i, (double)e[i].width, (double)(pts[2 * p] - half), (double)(pts[2 * p + 1] - half));
            WriteFile(file, line, TsvLength(line, sizeof line, len), &wrote, NULL);
        }
    }
    for (uint32_t k = 0; file != INVALID_HANDLE_VALUE && k < g_spurs.count; k++)
        for (uint32_t p = g_spurs.spur[k].first; p < g_spurs.spur[k].end && p < points; p++, rows++)
        {
            const int len = snprintf(line, sizeof line, "s\t%u\t%u\t%.1f\t%.1f\n", k, g_spurs.spur[k].edge, (double)(pts[2 * p] - half), (double)(pts[2 * p + 1] - half));
            WriteFile(file, line, TsvLength(line, sizeof line, len), &wrote, NULL);
        }
    if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); Note("roads: level %ld roadlines-%ld.tsv written (%u points)", (long)g_world.levels, (long)g_world.levels, rows); }
    RoadsFreeEdges(e, n);
    if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE);
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
    uint32_t onRoad = 0, tooFar = 0, noRoom = 0, noIndex = 0, at = points; double km = 0; float longest = 0;
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
        if (RoadsIndexFree(index, count - 1) < (int)count - 1) { noIndex++; continue; } /* a full index would be written through null */
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
    g_spurs.points = at;
    Note("roads: level %ld spurs: %u of %u places joined to the arterials (%.1f km, the longest %.0f m; %u road points of %u kept for them), "
        "%u on a road already, %u too far, %u without room, %u without a place in the stamp index (%d free after them)", (long)g_world.levels, g_spurs.count,
        g_spurs.terms, km, (double)longest, at - points, g_spurs.reserve, onRoad, tooFar, noRoom, noIndex, RoadsIndexFree(index, ROADS_INDEX_COUNT));
    RoadsFreeEdges(e, n);
    VirtualFree(pts, 0, MEM_RELEASE);
    RoadsLinesFile(block);
}

/* ---- The corridor census (log only) ----
 * What stands in the strips a vehicle is to drive: every location whose box (+0xB0) comes nearer an arterial's or a
 * spur's point pair than half its width, as the stamp tests measure it. The level's own op 0x2B stamps, the stamps
 * the mod added, the plan's, and the places of other ops (a spur's own place apart) are counted on their own. */
/* A box in a strip is not yet a rock on the road: the box (+0xB0) is the bounds of the location's shapes (0xF30C42),
 * and the game tests the shapes (0xFCC180: the circles and quads of [[location+0x90]] at the location's position and
 * angle +8 against the capsule of a pair and a half width; 1 and the depth when they overlap; called so at 0xF81CCF). */
typedef uint8_t (*RoadShapeFn)(uint64_t shape, const float* pos, float angle, const float* p0, const float* p1, float half, float* at, float* depth);
static RoadShapeFn g_roadShape; /* tests: in place of the game's 0xFCC180 */
#define ROADS_STRIP_CELL 64.0f
#define ROADS_STRIP_GRID 512
typedef struct RoadStrip { float seg[4], half; uint32_t edge, spur; } RoadStrip; /* spur: 1 + its index; 0 an arterial's pair */
typedef struct RoadStrips { RoadStrip* s; uint32_t n; uint32_t* start; uint32_t* item; int grid; } RoadStrips;
typedef struct RoadStripHit { float d, half; uint32_t edge, spur; int centred; } RoadStripHit;
static int RoadStripCell(float v, int grid) { const int c = (int)floorf(v / ROADS_STRIP_CELL); return c < 0 ? 0 : c >= grid ? grid - 1 : c; }
/* The strips in a grid of 64 m cells: each in every cell its pair, widened by its half width, touches. */
static int RoadStripsIndex(RoadStrips* t, float world)
{
    t->grid = (int)ceilf(world / ROADS_STRIP_CELL);
    if (t->grid < 1) t->grid = 1;
    if (t->grid > ROADS_STRIP_GRID) t->grid = ROADS_STRIP_GRID;
    const size_t cells = (size_t)t->grid * (size_t)t->grid;
    t->start = (uint32_t*)VirtualAlloc(NULL, (cells + 1) * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (t->start == NULL) return 0;
    for (int pass = 0; pass < 2; pass++)
    {
        for (uint32_t k = 0; k < t->n; k++)
        {
            const RoadStrip* s = &t->s[k];
            const int x0 = RoadStripCell(fminf(s->seg[0], s->seg[2]) - s->half, t->grid), x1 = RoadStripCell(fmaxf(s->seg[0], s->seg[2]) + s->half, t->grid);
            const int y0 = RoadStripCell(fminf(s->seg[1], s->seg[3]) - s->half, t->grid), y1 = RoadStripCell(fmaxf(s->seg[1], s->seg[3]) + s->half, t->grid);
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                {
                    const size_t c = (size_t)y * (size_t)t->grid + (size_t)x;
                    if (pass == 0) t->start[c + 1]++; else t->item[t->start[c]++] = k;
                }
        }
        if (pass == 1) break;
        for (size_t c = 0; c < cells; c++) t->start[c + 1] += t->start[c];
        const uint32_t total = t->start[cells];
        t->item = (uint32_t*)VirtualAlloc(NULL, ((size_t)total + 1) * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (t->item == NULL) { VirtualFree(t->start, 0, MEM_RELEASE); t->start = NULL; return 0; }
    }
    /* the fill moved each cell's start to its end: back by one cell */
    for (size_t c = cells; c > 0; c--) t->start[c] = t->start[c - 1];
    t->start[0] = 0;
    return 1;
}
/* The strip the box lies deepest in (the least distance from its pair, under its half width): 1 with *hit, 0 when the
 * box is in none. own: 1 + the spur whose place this is (its own spur ends in it), 0 none. */
static int RoadStripsHit(const RoadStrips* t, const float pos[2], const float lo[2], const float hi[2], uint32_t own, RoadStripHit* hit)
{
    int found = 0;
    const int x0 = RoadStripCell(lo[0], t->grid), x1 = RoadStripCell(hi[0], t->grid), y0 = RoadStripCell(lo[1], t->grid), y1 = RoadStripCell(hi[1], t->grid);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            const size_t c = (size_t)y * (size_t)t->grid + (size_t)x;
            for (uint32_t k = t->start[c]; k < t->start[c + 1]; k++)
            {
                const RoadStrip* s = &t->s[t->item[k]];
                if (own != 0 && s->spur == own) continue;
                const float d = StampSegmentBox(s->seg, lo, hi);
                if (!(d < s->half) || (found && !(d < hit->d))) continue;
                found = 1;
                hit->d = d; hit->half = s->half; hit->edge = s->edge; hit->spur = s->spur;
                hit->centred = RoadsSegment(pos[0], pos[1], s->seg[0], s->seg[1], s->seg[2], s->seg[3]) < s->half;
            }
        }
    return found;
}

/* The deepest the location's shapes reach into a strip its box touches: -1 not readable, 0 in none. */
static float RoadStripsShape(const RoadStrips* t, RoadShapeFn fn, const uint8_t* loc, const float lo[2], const float hi[2], uint32_t own)
{
    uint64_t cell, shape = 0; float pos[2], angle, deepest = 0.0f;
    memcpy(&cell, loc + 0x90, 8); memcpy(pos, loc, 8); memcpy(&angle, loc + 8, 4);
    if (cell == 0 || SafeRead(cell, &shape, 8) || shape == 0) return -1.0f;
    const int x0 = RoadStripCell(lo[0], t->grid), x1 = RoadStripCell(hi[0], t->grid), y0 = RoadStripCell(lo[1], t->grid), y1 = RoadStripCell(hi[1], t->grid);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            const size_t c = (size_t)y * (size_t)t->grid + (size_t)x;
            for (uint32_t k = t->start[c]; k < t->start[c + 1]; k++)
            {
                const RoadStrip* s = &t->s[t->item[k]];
                float at[2] = {0, 0}, depth = 0.0f;
                if ((own != 0 && s->spur == own) || !(StampSegmentBox(s->seg, lo, hi) < s->half)) continue;
                if (fn(shape, pos, angle, &s->seg[0], &s->seg[2], s->half, at, &depth) && depth > deepest) deepest = depth;
            }
        }
    return deepest;
}

enum { ROADS_IN_OWN, ROADS_IN_ADDED, ROADS_IN_PLAN, ROADS_IN_PLACE, ROADS_IN_KINDS };
typedef struct RoadsCensus
{
    uint32_t box[ROADS_IN_KINDS], centred[ROADS_IN_KINDS], onSpur[ROADS_IN_KINDS], listed; float nearest; HANDLE file; float halfWorld;
    RoadShapeFn shapeFn; uint32_t shapeIn[ROADS_IN_KINDS], shapeUnread[ROADS_IN_KINDS]; float deepest;
} RoadsCensus;
static void RoadsCensusItem(RoadsCensus* c, const RoadStrips* t, int kind, uint32_t index, uint32_t op, const uint8_t* loc, uint32_t own)
{
    static const char* const kNames[ROADS_IN_KINDS] = {"own", "added", "plan", "place"};
    float pos[2], box[4]; uint32_t type, raw; uint16_t flags; RoadStripHit hit;
    memcpy(pos, loc, 8); memcpy(box, loc + 0xB0, 16); memcpy(&type, loc + 0xD8, 4); memcpy(&flags, loc + 0xD4, 2); memcpy(&raw, loc + 0xC8, 4);
    const float lo[2] = {fminf(box[0], box[2]), fminf(box[1], box[3])}, hi[2] = {fmaxf(box[0], box[2]), fmaxf(box[1], box[3])};
    if (!(lo[0] <= hi[0]) || !(lo[1] <= hi[1]) || !RoadStripsHit(t, pos, lo, hi, own, &hit)) return;
    c->box[kind]++; c->centred[kind] += (uint32_t)hit.centred; c->onSpur[kind] += hit.spur != 0;
    if (kind != ROADS_IN_PLACE && hit.d < c->nearest) c->nearest = hit.d;
    /* the shapes of the level's own locations: the generation still holds them (op 0x2B read them for its last test) */
    float depth = -1.0f;
    if (c->shapeFn != NULL && (kind == ROADS_IN_OWN || kind == ROADS_IN_PLACE)) depth = RoadStripsShape(t, c->shapeFn, loc, lo, hi, own);
    if (depth > 0.0f) { c->shapeIn[kind]++; if (kind == ROADS_IN_OWN && depth > c->deepest) c->deepest = depth; }
    else if (depth < 0.0f) c->shapeUnread[kind]++;
    char line[240]; DWORD wrote;
    const int len = snprintf(line, sizeof line, "%s\t%u\t0x%X\t%u\t0x%X\t0x%X\t%.1f\t%.1f\t%.1f\t%.1f\t%.2f\t%.1f\t%u\t%s\t%d\t%.2f\n", kNames[kind], index, op, type, flags, raw,
        (double)(pos[0] - c->halfWorld), (double)(pos[1] - c->halfWorld), (double)(hi[0] - lo[0]), (double)(hi[1] - lo[1]), (double)hit.d, (double)hit.half, hit.edge,
        hit.spur ? "spur" : "arterial", hit.centred, (double)depth);
    if (c->file != INVALID_HANDLE_VALUE) WriteFile(c->file, line, TsvLength(line, sizeof line, len), &wrote, NULL);
    if (c->listed++ < 8)
        Note("roads: level %ld corridor: %s %u (op 0x%X, type %u, flags 0x%X, location +0xC8 0x%X) at (%.0f, %.0f), box %.1f x %.1f m, %.2f m from the middle of %s %u's %.1f m half width%s",
            (long)g_world.levels, kNames[kind], index, op, type, flags, raw, (double)(pos[0] - c->halfWorld), (double)(pos[1] - c->halfWorld), (double)(hi[0] - lo[0]),
            (double)(hi[1] - lo[1]), (double)hit.d, hit.spur ? "the spur at edge" : "arterial edge", hit.edge, (double)hit.half, hit.centred ? ", its centre inside" : "");
}

/* After the level's op 0x2B and the mod's stamps: one line, the first eight finds, and corridor-<level>.tsv with all
 * (kind, index, op, and of the 0xE0 location record type +0xD8, flags +0xD4 and the u32 at +0xC8, x, y in world metres, box, distance, half width, edge, class, centred,
 * and how deep its shapes reach into a strip: 0 none, -1 not tested).
 * before: the locations op 0x2B found; the ones from there on are its own, then the added ones. */
static void RoadsCorridorNote(uintptr_t block, const uint8_t* ops, uint32_t before, uint32_t total)
{
    if (g_arterials.level != g_world.levels || g_arterials.count == 0) return;
    LARGE_INTEGER t0, t1, hz;
    QueryPerformanceCounter(&t0);
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL; uint32_t points = 0, offset = 0;
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    float* pts = RoadsReadPoints(block, &L, &points);
    const uint32_t spurs = g_spurs.level == g_world.levels ? g_spurs.count : 0;
    RoadStrips t = {NULL, 0, NULL, NULL, 0};
    uint32_t pairs = 0;
    for (uint32_t i = 0; i < n; i++)
        if (((g_arterials.bits[i >> 3] >> (i & 7)) & 1) && RoadEdgeHasPoints(&e[i], points)) pairs += e[i].end - e[i].first - 1;
    for (uint32_t k = 0; k < spurs; k++) if (g_spurs.spur[k].end <= points) pairs += g_spurs.spur[k].end - g_spurs.spur[k].first - 1;
    if (pts != NULL && pairs != 0) t.s = (RoadStrip*)VirtualAlloc(NULL, (size_t)pairs * sizeof *t.s, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (t.s == NULL || SafeRead(g_world.game + 0xC176A7 + 3, &offset, 4) || offset < 0x90)
    {
        Note("roads: level %ld corridor: no arterial has points, or the locations are not readable; nothing counted", (long)g_world.levels);
        goto done;
    }
    double km[2] = {0, 0};
    for (uint32_t i = 0; i < n; i++)
    {
        if (!((g_arterials.bits[i >> 3] >> (i & 7)) & 1) || !RoadEdgeHasPoints(&e[i], points)) continue;
        for (uint32_t p = e[i].first + 1; p < e[i].end; p++)
        {
            RoadStrip* s = &t.s[t.n++];
            memcpy(s->seg, &pts[2 * (p - 1)], 16); s->half = 0.5f * e[i].width; s->edge = i; s->spur = 0;
            km[0] += sqrt((double)(s->seg[2] - s->seg[0]) * (s->seg[2] - s->seg[0]) + (double)(s->seg[3] - s->seg[1]) * (s->seg[3] - s->seg[1])) / 1000.0;
        }
    }
    for (uint32_t k = 0; k < spurs; k++)
    {
        const RoadSpur* sp = &g_spurs.spur[k];
        if (sp->end > points || sp->edge >= n) continue;
        for (uint32_t p = sp->first + 1; p < sp->end; p++)
        {
            RoadStrip* s = &t.s[t.n++];
            memcpy(s->seg, &pts[2 * (p - 1)], 16); s->half = 0.5f * e[sp->edge].width; s->edge = sp->edge; s->spur = k + 1;
            km[1] += sqrt((double)(s->seg[2] - s->seg[0]) * (s->seg[2] - s->seg[0]) + (double)(s->seg[3] - s->seg[1]) * (s->seg[3] - s->seg[1])) / 1000.0;
        }
    }
    if (!RoadStripsIndex(&t, 2048.0f * g_world.current)) goto done;
    RoadsCensus c; memset(&c, 0, sizeof c);
    c.nearest = ROADS_NONE; c.halfWorld = 1024.0f * g_world.current;
    c.shapeFn = g_roadShape != NULL ? g_roadShape :
        ApiCodeMatches(g_world.game + 0xFCC180u, "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 18 FF FF FF") ? (RoadShapeFn)(g_world.game + 0xFCC180u) : NULL;
    wchar_t name[48];
    swprintf(name, 48, L"corridor-%ld.tsv", (long)g_world.levels);
    c.file = OpenLog(name);
    const uint32_t added = g_stamps.ringLevel == g_world.levels ? g_stamps.first : 0xFFFFFFFFu;
    for (uint32_t i = 0; i < total; i++)
    {
        uint8_t loc[0xE0];
        if (SafeRead(block + (offset - 0x90) + 0xE0ull * i, loc, sizeof loc)) break;
        const uint8_t op = i < before && i < DIF_LOCATIONS ? ops[i] : 0x2B;
        uint32_t own = 0;
        if (op != 0x2B) /* a place's own spur starts at its position */
            for (uint32_t k = 0; k < spurs && own == 0; k++) if (memcmp(g_spurs.spur[k].from, loc, 8) == 0) own = k + 1;
        RoadsCensusItem(&c, &t, i >= added ? ROADS_IN_ADDED : op == 0x2B ? ROADS_IN_OWN : ROADS_IN_PLACE, i, op, loc, own);
    }
    const uint32_t planned = g_plan.level == g_world.levels && g_stamps.cand != NULL ? g_plan.count : 0;
    for (uint32_t k = 0; k < planned; k++) RoadsCensusItem(&c, &t, ROADS_IN_PLAN, k, 0x2B, g_stamps.cand[k].loc, 0);
    if (c.file != INVALID_HANDLE_VALUE) CloseHandle(c.file);
    const double all = km[0] + km[1];
    const uint32_t stamps = c.box[ROADS_IN_OWN] + c.box[ROADS_IN_ADDED] + c.box[ROADS_IN_PLAN];
    char clear[48];
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&hz);
    if (c.nearest < ROADS_NONE) snprintf(clear, sizeof clear, "%.1f m to the nearest of them", (double)(2.0f * c.nearest)); else snprintf(clear, sizeof clear, "the full width everywhere");
    Note("roads: level %ld corridor: %.1f km of arterials and %.1f km of spurs (%u pairs): inside the strip %u of the level's own op 0x2B stamps (%u centred, %u on a spur), "
        "%u added stamps (%u centred, %u on a spur), %u of the plan's (%u centred, %u on a spur), %u places of other ops (%u centred, %u on a spur); %.2f stamps per km; "
        "clear across the middle: %s; counted in %.1f ms", (long)g_world.levels, km[0], km[1], t.n, c.box[ROADS_IN_OWN], c.centred[ROADS_IN_OWN], c.onSpur[ROADS_IN_OWN],
        c.box[ROADS_IN_ADDED], c.centred[ROADS_IN_ADDED], c.onSpur[ROADS_IN_ADDED], c.box[ROADS_IN_PLAN], c.centred[ROADS_IN_PLAN], c.onSpur[ROADS_IN_PLAN],
        c.box[ROADS_IN_PLACE], c.centred[ROADS_IN_PLACE], c.onSpur[ROADS_IN_PLACE], all > 0 ? stamps / all : 0.0, clear,
        (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart);
    if (c.shapeFn == NULL) Note("roads: level %ld corridor shapes: the game's shape test is not the reviewed one; boxes only", (long)g_world.levels);
    else
        Note("roads: level %ld corridor shapes: of the %u own stamps with a box in the strip %u have a shape inside the half width (the deepest %.2f m in), %u not readable; "
            "of the %u places of other ops %u have, %u not readable", (long)g_world.levels, c.box[ROADS_IN_OWN], c.shapeIn[ROADS_IN_OWN], (double)c.deepest,
            c.shapeUnread[ROADS_IN_OWN], c.box[ROADS_IN_PLACE], c.shapeIn[ROADS_IN_PLACE], c.shapeUnread[ROADS_IN_PLACE]);
done:
    if (t.item != NULL) VirtualFree(t.item, 0, MEM_RELEASE);
    if (t.start != NULL) VirtualFree(t.start, 0, MEM_RELEASE);
    if (t.s != NULL) VirtualFree(t.s, 0, MEM_RELEASE);
    RoadsFreeEdges(e, n);
    if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE);
}

/* ---- The slope profile (log only) ----
 * How steep the ground is along each arterial and spur, from the level's own height capture (g_near.global: one float
 * per cell, row 0 at +y, TerrainNearDrop's indexing). The capture is read back after op 0x2B, so this measures the
 * roads as built; it cannot steer them. The near terrain copies it at the level's op 0x65, after ops 0x3D, 0x0D and
 * 0x41 ran. The two grades are the tank's and the FRV's grip as read from their physics
 * data, not a limit the game states. */
#define ROADS_GRADE_TANK 18.0f
#define ROADS_GRADE_FRV 31.0f
#define ROADS_TRACK 1.3f /* half the FRV's wheel track: the cross slope is taken over it */
typedef struct RoadsGround { const float* h; uint32_t cols, rows; float step, water; } RoadsGround;
typedef struct RoadsSlope { double metres, overTank, overFrv, wet; float grade, cross, low, high; } RoadsSlope;
/* The ground between the four cell centres about (x, y), generator metres. */
static float RoadsGroundAt(const RoadsGround* g, float x, float y)
{
    float fx = x / g->step - 0.5f, fy = y / g->step - 0.5f;
    const float mx = (float)(g->cols - 1), my = (float)(g->rows - 1);
    fx = fx < 0.0f ? 0.0f : fx > mx ? mx : fx; fy = fy < 0.0f ? 0.0f : fy > my ? my : fy;
    uint32_t c = (uint32_t)fx, r = (uint32_t)fy;
    if (c > g->cols - 2) c = g->cols - 2;
    if (r > g->rows - 2) r = g->rows - 2;
    const float tx = fx - (float)c, ty = fy - (float)r;
    const float* lo = g->h + (size_t)(g->rows - 1 - r) * g->cols + c;
    const float* hi = g->h + (size_t)(g->rows - 2 - r) * g->cols + c;
    return (lo[0] * (1.0f - tx) + lo[1] * tx) * (1.0f - ty) + (hi[0] * (1.0f - tx) + hi[1] * tx) * ty;
}
/* Along the n points: each pair in pieces of a cell at most; a piece's grade from its ends, its cross slope from the
 * ground a wheel track to each side of its far end. Adds to *s (grade, cross: the worst, in degrees). */
static void RoadsSlopeAlong(const RoadsGround* g, const float* pts, uint32_t n, RoadsSlope* s)
{
    const float deg = 57.29578f;
    for (uint32_t p = 1; p < n; p++)
    {
        const float ax = pts[2 * p - 2], ay = pts[2 * p - 1], dx = pts[2 * p] - ax, dy = pts[2 * p + 1] - ay, length = sqrtf(dx * dx + dy * dy);
        if (!(length > 1.0e-3f) || !(length < 1.0e5f)) continue;
        const uint32_t pieces = (uint32_t)ceilf(length / g->step);
        const float piece = length / (float)pieces, nx = -dy / length * ROADS_TRACK, ny = dx / length * ROADS_TRACK;
        float before = RoadsGroundAt(g, ax, ay);
        if (s->metres == 0.0) { s->low = s->high = before; }
        for (uint32_t k = 1; k <= pieces; k++)
        {
            const float t = (float)k / (float)pieces, x = ax + dx * t, y = ay + dy * t, h = RoadsGroundAt(g, x, y);
            const float grade = atanf(fabsf(h - before) / piece) * deg;
            const float cross = atanf(fabsf(RoadsGroundAt(g, x + nx, y + ny) - RoadsGroundAt(g, x - nx, y - ny)) / (2.0f * ROADS_TRACK)) * deg;
            s->metres += piece;
            if (grade > ROADS_GRADE_TANK) s->overTank += piece;
            if (grade > ROADS_GRADE_FRV) s->overFrv += piece;
            if (h <= g->water || before <= g->water) s->wet += piece;
            if (grade > s->grade) s->grade = grade;
            if (cross > s->cross) s->cross = cross;
            if (h < s->low) s->low = h;
            if (h > s->high) s->high = h;
            before = h;
        }
    }
}

static LONG g_roadsSlopeLevel = -1;
/* Once a level, when its height capture is kept: a line for the arterials, one for the spurs, and slopes-<level>.tsv
 * (class, index, edge, metres, steepest grade, metres over 18 and over 31 degrees, worst cross slope, metres at or
 * under the highest water, lowest and highest ground). */
static void RoadsSlopeNote(uintptr_t block)
{
    if (g_roadsSlopeLevel == g_world.levels || g_arterials.level != g_world.levels || g_arterials.count == 0 || g_near.level != g_world.levels ||
        g_near.global == NULL || g_near.columns < 2 || g_near.rows < 2) return;
    g_roadsSlopeLevel = g_world.levels;
    const RoadsLayout L = {g_routeEdges, g_routePoints, 0};
    RoadEdge* e = NULL; uint32_t points = 0;
    const uint32_t n = RoadsReadEdges(block, &L, &e);
    float* pts = RoadsReadPoints(block, &L, &points);
    const int water = g_nearWater.level == g_world.levels && g_nearWater.count > 0;
    const RoadsGround g = {g_near.global, g_near.columns, g_near.rows, 2048.0f * g_world.current / (float)g_near.columns, water ? g_nearWater.high : -1.0e30f};
    const uint32_t spurs = g_spurs.level == g_world.levels ? g_spurs.count : 0;
    wchar_t name[48];
    swprintf(name, 48, L"slopes-%ld.tsv", (long)g_world.levels);
    HANDLE file = pts != NULL ? OpenLog(name) : INVALID_HANDLE_VALUE;
    RoadsSlope sum[2]; uint32_t parts[2] = {0, 0}, steep[2] = {0, 0}, worst[2] = {0, 0};
    memset(sum, 0, sizeof sum);
    for (uint32_t k = 0; pts != NULL && k < n + spurs; k++)
    {
        const int spur = k >= n;
        uint32_t first, end, edge;
        if (!spur)
        {
            if (!((g_arterials.bits[k >> 3] >> (k & 7)) & 1) || !RoadEdgeHasPoints(&e[k], points)) continue;
            first = e[k].first; end = e[k].end; edge = k;
        }
        else { first = g_spurs.spur[k - n].first; end = g_spurs.spur[k - n].end; edge = g_spurs.spur[k - n].edge; }
        if (end > points || end <= first) continue;
        RoadsSlope s; memset(&s, 0, sizeof s);
        RoadsSlopeAlong(&g, pts + 2 * (size_t)first, end - first, &s);
        RoadsSlope* t = &sum[spur];
        parts[spur]++; steep[spur] += s.overTank > 0.0;
        if (s.grade > t->grade) { t->grade = s.grade; worst[spur] = spur ? k - n : k; }
        if (s.cross > t->cross) t->cross = s.cross;
        t->metres += s.metres; t->overTank += s.overTank; t->overFrv += s.overFrv; t->wet += s.wet;
        char line[192]; DWORD wrote;
        const int len = snprintf(line, sizeof line, "%s\t%u\t%u\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\n", spur ? "spur" : "arterial", spur ? k - n : k, edge,
            s.metres, (double)s.grade, s.overTank, s.overFrv, (double)s.cross, s.wet, (double)s.low, (double)s.high);
        if (file != INVALID_HANDLE_VALUE) WriteFile(file, line, TsvLength(line, sizeof line, len), &wrote, NULL);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    char wet[64];
    if (water) snprintf(wet, sizeof wet, "the highest water at %.1f m", (double)g_nearWater.high); else snprintf(wet, sizeof wet, "no water body");
    for (int c = 0; c < 2; c++)
        Note("roads: level %ld slopes: %u %s, %.1f km on the %.0f m height capture kept at op 0x65: steepest %.1f deg (%s %u), %.0f m over %.0f deg in %u of them, %.0f m over %.0f deg, "
            "cross slope at most %.1f deg, %.0f m at or under water (%s)", (long)g_world.levels, parts[c], c ? "spurs" : "arterial edges", sum[c].metres / 1000.0,
            (double)g.step, (double)sum[c].grade, c ? "spur" : "edge", worst[c], sum[c].overTank, (double)ROADS_GRADE_TANK, steep[c], sum[c].overFrv,
            (double)ROADS_GRADE_FRV, (double)sum[c].cross, sum[c].wet, wet);
    RoadsFreeEdges(e, n);
    if (pts != NULL) VirtualFree(pts, 0, MEM_RELEASE);
}
