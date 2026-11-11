/* Mission arcs: an operation's objectives dealt by mission on an extra large map. Included after difficulty_block.c.
 *
 * Op 0x18 hands out clusters with no mission input and ObjectiveSpread sends each extra objective to the emptiest far
 * cluster, so a main's steps lay anywhere on the map (s98c: 2.0-11.7 km from it, 3 of 15 nearest their own main). Here
 * the objectives change sites among themselves, a site being the cluster an objective holds, so that each primary
 * block (one mission row's mains and steps) has its own arc of the map. Only objectives of one class swap (op 0x18's
 * rule, loc+0xF0 == cluster+0xD8) and extraction-type ones stay: no cluster, centre or count of objectives in a
 * cluster changes. No draws and the generator's data only, so every peer deals the same. */
#define ARC_MOST 64u
#define ARC_BLOCKS 16u

/* A row's own primary: the class (0 main, 1 step) of its first drawn entry of the id, else -1. The steps the loader
 * registers in a base row for its partners have weight 0 (RegisterPartnerSteps), so they are not the row's own. */
static int ArcRowClass(uintptr_t rows, uint32_t r, uint32_t id)
{
    uint8_t entries[32 * 0x18];
    if (id == 0 || SafeRead(rows + (uintptr_t)r * DIF_MISSION_ROW + 0x40, entries, sizeof entries)) return -1;
    for (int i = 0; i < 32; i++)
    {
        uint32_t entry, cls; float weight;
        memcpy(&entry, entries + i * 0x18, 4); memcpy(&weight, entries + i * 0x18 + 4, 4); memcpy(&cls, entries + i * 0x18 + 0xC, 4);
        if (entry == id && weight > 0.0f && cls <= 1) return (int)cls;
    }
    return -1;
}

/* The list's primary blocks. listClass is each id's class in the base row (-1: not listed; 2 and up: a side objective).
 * Walking the primaries in list order, a block is the longest run one mission row covers with its own mains and steps,
 * at least one a main: the base row's if it covers any (at most P entries: one mission's primaries, so its repeats are
 * blocks of their own), else the longest of every other row's, the first on a tie. block[i] is 1.., 0 for a side objective. 0: a primary
 * no row covers. */
static int ArcBlocks(uintptr_t rows, uint32_t base, const uint32_t* ids, const int* listClass, uint32_t count, uint32_t P, uint8_t* block, uint8_t* isMain,
    uint8_t* blockRow, uint32_t* blocksOut)
{
    uint32_t prim[ARC_MOST], np = 0, blocks = 0;
    if (count > ARC_MOST) return 0;
    for (uint32_t i = 0; i < count; i++) { block[i] = 0; isMain[i] = 0; if (listClass[i] <= 1) prim[np++] = i; }
    for (uint32_t k = 0; k < np;)
    {
        uint32_t bestLength = 0, bestRow = 0;
        for (uint32_t candidate = 0; candidate <= DIF_MISSION_ROWS; candidate++)
        {
            uint32_t r = candidate == 0 ? base : candidate - 1, j = k, mains = 0;
            if (candidate > 0 && r == base) continue;
            for (; j < np && (r != base || j - k < P); j++)
            {
                int c = ArcRowClass(rows, r, ids[prim[j]]), listed = listClass[prim[j]];
                if (c < 0 || (r == base ? listed == -1 : (c == 1) != (listed == 1))) break;
                mains += c == 0;
            }
            if (mains > 0 && j - k > bestLength) { bestLength = j - k; bestRow = r; }
            if (candidate == 0 && bestLength > 0) break; /* the base row's own block: no other row is asked */
        }
        if (bestLength == 0 || blocks == ARC_BLOCKS) return 0;
        blockRow[blocks++] = (uint8_t)bestRow;
        for (uint32_t j = k; j < k + bestLength; j++) { block[prim[j]] = (uint8_t)blocks; isMain[prim[j]] = ArcRowClass(rows, bestRow, ids[prim[j]]) == 0; }
        k += bestLength;
    }
    *blocksOut = blocks;
    return 1;
}

/* Each list the fill made, with its base row's block length P (the selection's leading primaries the base row owns),
 * found again by its whole content: a level's blocks never come from another level's fill. */
static struct { uint16_t row; uint32_t count, P, ids[ARC_MOST]; } g_arcLists[8];
static uint32_t g_arcListNext;
static SRWLOCK g_arcListLock = SRWLOCK_INIT;
static int ArcListClass(uintptr_t rows, uint32_t row, uint32_t id)
{
    return id == DIF_LIVE_EVENT_OBJECTIVE ? 2 : RowClass(rows + (uintptr_t)row * DIF_MISSION_ROW, id);
}
static void ArcListKeep(uintptr_t rows, uint16_t row, const uint32_t* selected, uint32_t n, const uint32_t* list, uint32_t total)
{
    uint32_t P = 0;
    if (total == 0 || total > ARC_MOST || n > total) return;
    for (uint32_t i = 0; i < n; i++)
    {
        int c = ArcListClass(rows, row, selected[i]);
        if (c >= 2) continue;
        if (c == -1 || ArcRowClass(rows, row, selected[i]) < 0) break;
        P++;
    }
    AcquireSRWLockExclusive(&g_arcListLock);
    uint32_t at = g_arcListNext++ % 8;
    g_arcLists[at].row = row; g_arcLists[at].count = total; g_arcLists[at].P = P;
    memcpy(g_arcLists[at].ids, list, 4ull * total);
    ReleaseSRWLockExclusive(&g_arcListLock);
}
static uint32_t ArcListFind(uint16_t row, const uint32_t* ids, uint32_t count)
{
    uint32_t P = 0;
    AcquireSRWLockShared(&g_arcListLock);
    for (uint32_t i = 0; i < 8 && P == 0; i++)
        if (g_arcLists[i].row == row && g_arcLists[i].count == count && !memcmp(g_arcLists[i].ids, ids, 4ull * count)) P = g_arcLists[i].P;
    ReleaseSRWLockShared(&g_arcListLock);
    return P;
}

/* The objectives of one generation: site s is the cluster objective s holds (cluster[s]); take[i] is the site
 * objective i gets. An objective that stays (extraction-type, or of a block with no main to deal) keeps its own. */
typedef struct
{
    uint32_t o, blocks;
    uint32_t id[ARC_MOST];
    uint8_t block[ARC_MOST], isMain[ARC_MOST], cls[ARC_MOST], siteClass[ARC_MOST], cluster[ARC_MOST], stay[ARC_MOST];
    float x[ARC_MOST], y[ARC_MOST];
    uint8_t take[ARC_MOST], arc[ARC_MOST], blockArc[ARC_BLOCKS + 1];
    float mainX[ARC_BLOCKS + 1], mainY[ARC_BLOCKS + 1];
} ArcSet;

/* The free site of the class nearest (tx, ty): one outside the avoided clusters first, then one of arc g; the lower
 * index on a tie. 0xFF: none. */
static uint32_t ArcPick(const ArcSet* s, const uint8_t* used, const uint8_t* avoid, uint8_t cls, float tx, float ty, int g)
{
    uint32_t best = 0xFF; float bestD = 0; int bestRank = 0;
    for (uint32_t t = 0; t < s->o; t++)
    {
        if (used[t] || s->siteClass[t] != cls) continue;
        float d = (s->x[t] - tx) * (s->x[t] - tx) + (s->y[t] - ty) * (s->y[t] - ty);
        int rank = (avoid != NULL && avoid[s->cluster[t]] ? 0 : 2) + (g >= 0 && s->arc[t] == (uint8_t)g ? 1 : 0);
        if (best == 0xFF || rank > bestRank || (rank == bestRank && d < bestD)) { best = t; bestD = d; bestRank = rank; }
    }
    return best;
}

/* The clusters objective i should not go to while another site will do: for a main, those that already got a main; for
 * every objective, those holding another objective of its id (where that one was dealt, or where it stands if it has
 * not been yet), so copies of one objective do not gather. */
static const uint8_t* ArcAvoid(const ArcSet* s, uint32_t i, const uint8_t* mainCluster, uint8_t* avoid)
{
    for (uint32_t c = 0; c < 64; c++) avoid[c] = s->isMain[i] && mainCluster != NULL ? mainCluster[c] : 0;
    for (uint32_t j = 0; j < s->o; j++)
        if (j != i && s->id[j] == s->id[i]) avoid[s->cluster[s->take[j] != 0xFF ? s->take[j] : j]] = 1;
    return avoid;
}

/* How many objectives of i's id end in the cluster of site t, i itself left out and j counted at site tj. */
static uint32_t ArcSameAt(const ArcSet* s, uint32_t i, uint32_t t, uint32_t j, uint32_t tj)
{
    uint32_t same = 0;
    for (uint32_t q = 0; q < s->o; q++)
        if (q != i && s->id[q] == s->id[i]) same += s->cluster[q == j ? tj : s->take[q]] == s->cluster[t];
    return same;
}

/* After the deal: the last side objectives to pick can be left with sites only in clusters that hold their id already
 * (a level-11 list repeats its side ids many times). Each objective that ends with more of its id about it than any
 * cluster held before trades sites with another dealt objective that is not a main, of the same class both ways, so
 * that neither ends so; a side objective first, then the trade that moves the two least from where they were dealt,
 * the lower index on a tie. What cannot be traded is left for the caller's check. */
static void ArcUnstack(ArcSet* s)
{
    uint32_t most = 0;
    for (uint32_t i = 0; i < s->o; i++)
    {
        uint32_t same = 1;
        for (uint32_t j = 0; j < s->o; j++) same += j != i && s->id[j] == s->id[i] && s->cluster[j] == s->cluster[i];
        if (same > most) most = same;
    }
    for (uint32_t round = 0; round < ARC_MOST; round++)
    {
        int traded = 0;
        for (uint32_t i = 0; i < s->o && !traded; i++)
        {
            if (s->stay[i] || s->isMain[i] || ArcSameAt(s, i, s->take[i], i, s->take[i]) + 1 <= most) continue;
            uint32_t best = 0xFF; float bestD = 0; int bestSide = 0;
            for (uint32_t j = 0; j < s->o; j++)
            {
                uint32_t ti = s->take[i], tj = s->take[j];
                if (j == i || s->stay[j] || s->isMain[j] || s->id[j] == s->id[i] || s->siteClass[tj] != s->cls[i] || s->siteClass[ti] != s->cls[j] ||
                    s->cluster[ti] == s->cluster[tj]) continue;
                if (ArcSameAt(s, i, tj, j, ti) + 1 > most || ArcSameAt(s, j, ti, i, tj) + 1 > most) continue;
                float d = (s->x[ti] - s->x[tj]) * (s->x[ti] - s->x[tj]) + (s->y[ti] - s->y[tj]) * (s->y[ti] - s->y[tj]);
                int side = s->block[j] == 0;
                if (best == 0xFF || side > bestSide || (side == bestSide && d < bestD)) { best = j; bestD = d; bestSide = side; }
            }
            if (best == 0xFF) continue;
            uint8_t t = s->take[i]; s->take[i] = s->take[best]; s->take[best] = t;
            traded = 1;
        }
        if (!traded) break;
    }
}

/* The deal. Arcs: the sites in order of direction from the map's centre, from the widest gap on, cut into one equal
 * run per block with a main. A block's first main takes the site nearest its arc's mean point; then, a block at a time
 * in turn, its other mains and its steps take the free site nearest that main (own arc first), a main never a cluster
 * that already got a main while another will do; a side objective keeps its site unless a primary took it, and then
 * takes the nearest free one. No pick joins another objective of its id while another site will do, and what gathers
 * all the same is traded apart at the end (ArcUnstack). 0: nothing to deal. */
static int ArcDeal(ArcSet* s, float centre)
{
    uint8_t order[ARC_MOST], used[ARC_MOST], mainCluster[64] = {0}, haveMain[ARC_BLOCKS + 1] = {0}, avoid[64];
    float dir[ARC_MOST];
    uint32_t sites = 0, arcs = 0;
    for (uint32_t b = 1; b <= s->blocks; b++)
    {
        s->blockArc[b] = 0xFF;
        for (uint32_t i = 0; i < s->o; i++) if (s->block[i] == b && s->isMain[i] && !s->stay[i]) { s->blockArc[b] = (uint8_t)arcs++; break; }
    }
    for (uint32_t i = 0; i < s->o; i++)
    {
        if (s->block[i] != 0 && s->blockArc[s->block[i]] == 0xFF) s->stay[i] = 1;
        s->take[i] = s->stay[i] ? (uint8_t)i : 0xFF; used[i] = s->stay[i]; s->arc[i] = 0xFF;
        if (s->stay[i]) { if (s->isMain[i]) mainCluster[s->cluster[i]] = 1; continue; }
        float dx = s->x[i] - centre, dy = s->y[i] - centre;
        dir[i] = fabsf(dx) + fabsf(dy) < 1.0f ? 0.0f : ClusterDirection(dx, dy);
        uint32_t k = sites++;
        while (k > 0 && dir[order[k - 1]] > dir[i]) { order[k] = order[k - 1]; k--; }
        order[k] = (uint8_t)i;
    }
    if (arcs < 2 || sites < arcs) return 0;
    uint32_t start = 0; float widest = -1.0f;
    for (uint32_t k = 0; k < sites; k++)
    {
        float gap = dir[order[(k + 1) % sites]] - dir[order[k]];
        if (k + 1 == sites) gap += 4.0f;
        if (gap > widest) { widest = gap; start = (k + 1) % sites; }
    }
    float ax[ARC_BLOCKS] = {0}, ay[ARC_BLOCKS] = {0}; uint32_t an[ARC_BLOCKS] = {0};
    for (uint32_t k = 0; k < sites; k++)
    {
        uint32_t t = order[(start + k) % sites], g = k * arcs / sites;
        s->arc[t] = (uint8_t)g; ax[g] += s->x[t]; ay[g] += s->y[t]; an[g]++;
    }
    for (uint32_t b = 1; b <= s->blocks; b++)
        for (uint32_t i = 0, g = s->blockArc[b]; g != 0xFF && i < s->o; i++)
        {
            if (s->block[i] != b || !s->isMain[i] || s->stay[i]) continue;
            uint32_t t = ArcPick(s, used, ArcAvoid(s, i, mainCluster, avoid), s->cls[i], ax[g] / (float)an[g], ay[g] / (float)an[g], (int)g);
            if (t == 0xFF) return 0;
            s->take[i] = (uint8_t)t; used[t] = 1; mainCluster[s->cluster[t]] = 1;
            s->mainX[b] = s->x[t]; s->mainY[b] = s->y[t]; haveMain[b] = 1;
            break;
        }
    for (int any = 1; any;)
    {
        any = 0;
        for (uint32_t b = 1; b <= s->blocks; b++)
            for (uint32_t i = 0; haveMain[b] && i < s->o; i++)
            {
                if (s->block[i] != b || s->take[i] != 0xFF) continue;
                uint32_t t = ArcPick(s, used, ArcAvoid(s, i, mainCluster, avoid), s->cls[i], s->mainX[b], s->mainY[b], (int)s->blockArc[b]);
                if (t == 0xFF) return 0;
                s->take[i] = (uint8_t)t; used[t] = 1; any = 1;
                if (s->isMain[i]) mainCluster[s->cluster[t]] = 1;
                break;
            }
    }
    for (uint32_t i = 0; i < s->o; i++) if (s->take[i] == 0xFF && !used[i]) { s->take[i] = (uint8_t)i; used[i] = 1; } /* a side objective whose site is free */
    for (uint32_t i = 0; i < s->o; i++)
    {
        if (s->take[i] != 0xFF) continue;
        uint32_t t = ArcPick(s, used, ArcAvoid(s, i, NULL, avoid), s->cls[i], s->x[i], s->y[i], -1);
        if (t == 0xFF) return 0;
        s->take[i] = (uint8_t)t; used[t] = 1;
    }
    ArcUnstack(s);
    return 1;
}

/* The most objectives of one id in one cluster, before (after == 0) or after the deal. */
static uint32_t ArcSameId(const ArcSet* s, int after)
{
    uint32_t most = 0;
    for (uint32_t i = 0; i < s->o; i++)
    {
        uint32_t same = 0, mine = after ? s->cluster[s->take[i]] : s->cluster[i];
        for (uint32_t j = 0; j < s->o; j++) same += s->id[j] == s->id[i] && (after ? s->cluster[s->take[j]] : s->cluster[j]) == mine;
        if (same > most) most = same;
    }
    return most;
}

/* One row an objective into `name` (the level's number in it). `planned` 0: the deal was not made, every objective is
 * shown where it is. */
static void ArcTable(const wchar_t* format, const ArcSet* s, const uint32_t* index, const uint8_t* kind, int mixes, float centre, int planned)
{
    wchar_t name[48];
    swprintf(name, 48, format, (long)g_world.levels);
    HANDLE file = OpenLog(name);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[200]; DWORD wrote;
    int len = snprintf(line, sizeof line, "record\tid\tkind\tclass\tblock\tmission\tmain\tstays\tarc\tcluster_before\tcluster_after\tx_before\ty_before\tx_after\ty_after\tsite_class\n");
    WriteFile(file, line, (DWORD)len, &wrote, NULL);
    for (uint32_t i = 0; i < s->o; i++)
    {
        uint32_t t = planned ? s->take[i] : i, b = s->block[i];
        len = snprintf(line, sizeof line, "%u\t%08x\t%u\t%u\t%d\t%d\t%u\t%u\t%d\t%u\t%u\t%.1f\t%.1f\t%.1f\t%.1f\t%u\n", index[i], s->id[i], (unsigned)kind[i], (unsigned)s->cls[i],
            b ? (int)b - 1 : -1, b ? (int)(mixes ? (b - 1) / 2 : b - 1) : -1, (unsigned)s->isMain[i], (unsigned)s->stay[i], planned && s->arc[t] != 0xFF ? (int)s->arc[t] : -1,
            (unsigned)s->cluster[i], (unsigned)s->cluster[t], (double)(s->x[i] - centre), (double)(s->y[i] - centre), (double)(s->x[t] - centre), (double)(s->y[t] - centre),
            (unsigned)s->siteClass[i]);
        WriteFile(file, line, (DWORD)len, &wrote, NULL);
    }
    CloseHandle(file);
}

static void ObjectiveArcs(uint64_t a, uint64_t pass, uint64_t clusters, uint32_t n, float centre)
{
    uint64_t lg = 0, records = 0, countAt = 0; uint8_t level = 0;
    if (g_difficulty.missions < 2 || g_difficultyGame == 0 || SafeRead(pass + 0x18, &lg, 8) || lg == 0 || SafeRead(lg + 0xF9, &level, 1) || level != DIF_VANILLA + 1 || !LevelComplex((uintptr_t)lg)) return;
    const char* why = NULL;
    static ArcSet set; /* under g_arcDealLock */
    static SRWLOCK g_arcDealLock = SRWLOCK_INIT;
    static uint64_t rec[ARC_MOST], kids[ARC_MOST][8];
    static uint32_t index[ARC_MOST];
    static uint8_t nk[ARC_MOST], kind[ARC_MOST];
    uint32_t count = 0, listed = 0, ids[ARC_MOST], blocks = 0, P = 0, moved = 0, children = 0, stayed = 0, members = 0, unlisted = 0;
    int listClass[ARC_MOST];
    uint8_t entryBlock[ARC_MOST], entryMain[ARC_MOST], blockRow[ARC_BLOCKS];
    uint16_t row = 0;
    const uintptr_t rows = g_difficultyGame + DIF_MISSIONS;
    /* the list's place is the one HellForge moved the game's code to */
    const uint32_t listAt = CO_OBJLIST_AT;
    const uint64_t G = clusters - DIF_CLUSTERS_NEW, S = G + listAt;
    AcquireSRWLockExclusive(&g_arcDealLock);
    ArcSet* s = &set;
    memset(s, 0, sizeof *s);
    /* the level's own list and records: the generation block the clusters lie in */
    if (DIF_OBJLIST_PLACES == 0 || DIF_OBJLIST_PLACES > ARC_MOST || DIF_CLUSTERS_NEW == 0 || listAt == 0 || n == 0 || n > 64 || SafeRead(a + 0x18, &records, 8) || records != G + g_importantList ||
        SafeRead(a + 0x28, &countAt, 8) || countAt == 0 || SafeRead(countAt, &count, 4) || count > DIF_IMPORTANT) why = "the generation block is not the moved one";
    else if (SafeRead(S, &row, 2) || row >= DIF_MISSION_ROWS || SafeRead(S + 0xD4, &listed, 4) || listed == 0 || listed > DIF_OBJLIST_PLACES ||
        SafeRead(S + 4, ids, 4ull * listed)) why = "the level's objective list could not be read";
    else if ((P = ArcListFind(row, ids, listed)) == 0) why = "the level's objective list is not one the fill made";
    if (why == NULL)
    {
        for (uint32_t i = 0; i < listed; i++) listClass[i] = ArcListClass(rows, row, ids[i]);
        /* a list the fill made carries its blocks (../co_objectives.c); any other is read by the same rule */
        if (!co_list_kept(row, ids, listed, entryBlock, entryMain, blockRow, &blocks) && !ArcBlocks(rows, row, ids, listClass, listed, P, entryBlock, entryMain, blockRow, &blocks))
            why = "a primary on the list is no mission row's";
        else if (blocks < 2) why = "one block only";
    }
    /* the objective records in order against the list in order (op 0x16 places the list in order and leaves out an id
     * with no entry); a main is kind 7 there (0xF90074) */
    for (uint32_t l = 0, at = 0; why == NULL && l < count; l++)
    {
        uint64_t r = records + 0x108ull * l, cl = 0; uint32_t id = 0, o = s->o;
        if (!ClusterObjective(r)) continue;
        if (o == ARC_MOST || SafeRead(r + 0xD8, &id, 4) || SafeRead(r + 0xE0, &cl, 8) || SafeRead(r + 0xF0, &s->cls[o], 1) || SafeRead(r + 0xDC, &kind[o], 1) ||
            cl < clusters || (cl - clusters) % 0xE0 != 0 || (cl - clusters) / 0xE0 >= n) { why = "an objective's record could not be read"; break; }
        /* only op 0x16 places the list; an objective another op placed (an open map's op 0x6 one) is no mission's:
         * it keeps its site and belongs to no block */
        const int other = l >= DIF_LOCATIONS || g_locationOp[l] != 0x16;
        if (other)
        {
            /* a wrong mark must not drop a list objective out of its block quietly */
            int onList = 0;
            for (uint32_t q = 0; q < listed; q++) onList |= ids[q] == id;
            if (onList) { why = "an objective on the level's list was placed by another op"; break; }
        }
        else
        {
            while (at < listed && ids[at] != id) at++;
            if (at == listed) { why = "an objective is not on the level's list"; break; }
            if (entryMain[at] != (kind[o] == 7)) { why = "a main on the list is not a main on the map"; break; }
        }
        float xy[2]; uint64_t at2 = cl;
        if (SafeRead(at2, xy, 8) || !isfinite(xy[0]) || !isfinite(xy[1]) || SafeRead(at2 + 0xD8, &s->siteClass[o], 1)) { why = "a cluster could not be read"; break; }
        rec[o] = r; index[o] = l; s->id[o] = id; s->cluster[o] = (uint8_t)((cl - clusters) / 0xE0);
        s->block[o] = other ? 0 : entryBlock[at]; s->isMain[o] = other ? kind[o] == 7 : entryMain[at];
        s->x[o] = xy[0]; s->y[o] = xy[1]; s->stay[o] = (uint8_t)(other || ClusterExtraction(r));
        if (other) unlisted++; else at++;
        s->o++;
    }
    s->blocks = blocks;
    int planned = 0;
    if (why == NULL) planned = ArcDeal(s, centre);
    if (why == NULL && !planned) why = "fewer than two blocks with a main to deal, or fewer sites than blocks";
    if (why == NULL && ArcSameId(s, 1) > ArcSameId(s, 0)) why = "a cluster would hold more objectives of one id than any did";
    /* checked before anything moves: each moved objective's mode 1/3 children (they follow the parent's cluster,
     * 0xF7A0F0) and every cluster's count afterwards */
    int32_t delta[64] = {0};
    for (uint32_t i = 0; why == NULL && i < s->o; i++)
    {
        uint64_t from = clusters + 0xE0ull * s->cluster[i], m[24]; uint32_t c = 0;
        nk[i] = 0;
        if (s->cluster[s->take[i]] == s->cluster[i]) continue;
        if (SafeRead(from + 0xD0, &c, 4) || c > 24 || SafeRead(from + 0x10, m, 8ull * c)) { why = "a cluster could not be read"; break; }
        for (uint32_t q = 0; q < c; q++)
        {
            uint8_t k = 0, mode = 0; uint64_t parent = 0;
            if (SafeRead(m[q] + 0xDC, &k, 1) || k != 0xB || SafeRead(m[q] + 0xF8, &parent, 8) || parent != rec[i] || SafeRead(m[q] + 0x100, &mode, 1) ||
                !(mode == 1 || mode == 3)) continue;
            if (nk[i] == 8) { why = "an objective has more than 8 children"; break; }
            kids[i][nk[i]++] = m[q];
        }
        delta[s->cluster[i]] -= 1 + nk[i]; delta[s->cluster[s->take[i]]] += 1 + nk[i];
    }
    for (uint32_t i = 0; why == NULL && i < n; i++)
    {
        uint32_t c = 0;
        if (SafeRead(clusters + 0xE0ull * i + 0xD0, &c, 4) || (int32_t)c + delta[i] > 24 || (int32_t)c + delta[i] < 0) why = "a cluster would hold more than 24 members";
    }
    if (why != NULL)
    {
        Note("difficulty: extra large maps: mission arcs: not dealt (%s); row %u, %u ids on the list, %u objectives, %u blocks", why, (unsigned)row, listed, s->o, blocks);
        /* what the deal read (and planned, when it got that far), for whoever looks into the refusal */
        if (s->o > 0)
        {
            int mixed = 0;
            for (uint32_t b = 0; b < blocks && b < ARC_BLOCKS; b++) mixed |= blockRow[b] != row;
            ArcTable(L"arcs-refused-%ld.tsv", s, index, kind, mixed, centre, planned);
        }
        ReleaseSRWLockExclusive(&g_arcDealLock);
        return;
    }
    /* every removal before any insertion, in record order: no cluster passes 24 on the way */
    for (uint32_t i = 0; i < s->o; i++)
        if (s->cluster[s->take[i]] != s->cluster[i])
        {
            ClusterRemove(rec[i], clusters + 0xE0ull * s->cluster[i]);
            for (uint32_t q = 0; q < nk[i]; q++) ClusterRemove(kids[i][q], clusters + 0xE0ull * s->cluster[i]);
        }
    for (uint32_t i = 0; i < s->o; i++)
        if (s->cluster[s->take[i]] != s->cluster[i])
        {
            ClusterAdd(rec[i], clusters + 0xE0ull * s->cluster[s->take[i]]); moved++;
            for (uint32_t q = 0; q < nk[i]; q++) { ClusterAdd(kids[i][q], clusters + 0xE0ull * s->cluster[s->take[i]]); children++; }
        }
    /* the note and arcs-<level>.tsv: one row an objective */
    int mixes = 0;
    for (uint32_t b = 0; b < blocks; b++) mixes |= blockRow[b] != row;
    double before = 0, after = 0; float farBefore = 0, farAfter = 0;
    for (uint32_t i = 0; i < s->o; i++)
    {
        uint32_t first = ARC_MOST;
        stayed += s->stay[i];
        if (s->block[i] == 0) continue;
        for (uint32_t j = 0; j < s->o; j++) if (s->block[j] == s->block[i] && s->isMain[j]) { first = j; break; }
        if (first == ARC_MOST || first == i) continue;
        uint32_t ti = s->take[i], tf = s->take[first];
        float d0 = sqrtf((s->x[i] - s->x[first]) * (s->x[i] - s->x[first]) + (s->y[i] - s->y[first]) * (s->y[i] - s->y[first]));
        float d1 = sqrtf((s->x[ti] - s->x[tf]) * (s->x[ti] - s->x[tf]) + (s->y[ti] - s->y[tf]) * (s->y[ti] - s->y[tf]));
        before += d0; after += d1; members++;
        if (d0 > farBefore) farBefore = d0;
        if (d1 > farAfter) farAfter = d1;
    }
    char text[1400];
    int at = snprintf(text, sizeof text, "difficulty: extra large maps: mission arcs: row %u, %u ids on the list (base block %u), %u objectives in %u blocks; a block member's "
        "distance to its first main: mean %.0f -> %.0f m, farthest %.0f -> %.0f m (%u members); %u objectives and %u children moved, %u stay; same id in one cluster at most "
        "%u -> %u; %u placed by another op than the list's stay as they are; blocks (row: main at x,y, members):", (unsigned)row, listed, P, s->o, blocks, members ? before / members : 0.0, members ? after / members : 0.0,
        (double)farBefore, (double)farAfter, members, moved, children, stayed, ArcSameId(s, 0), ArcSameId(s, 1), unlisted);
    for (uint32_t b = 1; b <= blocks && at > 0 && at < (int)sizeof text - 48; b++)
    {
        uint32_t size = 0;
        for (uint32_t i = 0; i < s->o; i++) size += s->block[i] == b;
        if (s->blockArc[b] != 0xFF) at += snprintf(text + at, sizeof text - at, " %u: %.0f,%.0f, %u;", (unsigned)blockRow[b - 1], (double)s->mainX[b], (double)s->mainY[b], size);
        else at += snprintf(text + at, sizeof text - at, " %u: stays, %u;", (unsigned)blockRow[b - 1], size);
    }
    Note("%s", text);
    ArcTable(L"arcs-%ld.tsv", s, index, kind, mixes, centre, 1);
    ReleaseSRWLockExclusive(&g_arcDealLock);
}
