/* A complex level's objective list from the selection (co_objectives.h). The tables are the game's: a mission row's 32
 * objective entries (+0x40: id, weight, class at +0xC, most uses at +0x14) and the objective definitions (+0 id,
 * +0x20 most uses a mission, +0x2C exclusive bits, +0x2D no place on the map). */
#include <string.h>

#include <windows.h>

#include "co_objectives.h"
#include "co_mix.h"

#define MISSIONS 0x3773420u
#define MISSION_ROW 0x380u
#define OBJECTIVES 0x32EF870u
#define OBJECTIVE_ROWS 153u
#define OBJECTIVE_ROW 0xA0u
#define LIVE_EVENT 0x68BFBB59u
#define RADAR 0xF1969B14u
#define SELECTED 32u /* a descriptor's ids */

typedef struct Entry { uint32_t id; float weight; uint32_t c, cls, least, most; } Entry;
typedef struct Definition { uint32_t id, most; uint8_t bits, nowhere; } Definition;
static Entry g_rows[CO_MIX_ROWS][32];
static Definition g_definitions[OBJECTIVE_ROWS];
static SRWLOCK g_lock = SRWLOCK_INIT; /* the tables above and the kept lists */
static struct
{
    uint32_t row, count, blocks, ids[CO_LIST_MOST];
    uint8_t block[CO_LIST_MOST], main[CO_LIST_MOST], blockRow[CO_LIST_BLOCKS], blockMission[CO_LIST_BLOCKS];
} g_kept[8];
static uint32_t g_keptNext;

static int Tables(uintptr_t game)
{
    _Static_assert(sizeof(Entry) == 0x18, "a mission row's objective entry");
    for (uint32_t r = 0; r < CO_MIX_ROWS; r++)
        if (g_coApi->read(game + MISSIONS + r * MISSION_ROW + 0x40, g_rows[r], sizeof g_rows[r]) != 0) return 0;
    for (uint32_t d = 0; d < OBJECTIVE_ROWS; d++)
    {
        uint8_t row[0x2E];
        if (g_coApi->read(game + OBJECTIVES + d * OBJECTIVE_ROW, row, sizeof row) != 0) return 0;
        memcpy(&g_definitions[d].id, row, 4); memcpy(&g_definitions[d].most, row + 0x20, 4);
        g_definitions[d].bits = row[0x2C]; g_definitions[d].nowhere = row[0x2D];
    }
    return 1;
}

/* The first class the row gives the id, or -1: the game's own lookup. */
static int RowClass(uint32_t r, uint32_t id)
{
    for (int k = 0; k < 32; k++) if (g_rows[r][k].id == id) return (int)g_rows[r][k].cls;
    return -1;
}

/* A row's own primary: the class (0 main, 1 step) of its first drawn entry of the id, else -1. The steps HellForge
 * registers in a base row for its partners have weight 0, so they are not the row's own. */
static int OwnClass(uint32_t r, uint32_t id)
{
    for (int k = 0; id != 0 && k < 32; k++)
        if (g_rows[r][k].id == id && g_rows[r][k].weight > 0.0f && g_rows[r][k].cls <= 1) return (int)g_rows[r][k].cls;
    return -1;
}

static const Definition* DefinitionOf(uint32_t id) /* the first of the id, as the game looks it up; row 0 is none */
{
    for (uint32_t d = 1; d < OBJECTIVE_ROWS; d++) if (g_definitions[d].id == id) return &g_definitions[d];
    return NULL;
}

/* The list's primary blocks, by the rule the mission arcs were approved with. Walking the primaries (class 0, 1 or
 * not in the base row) in list order, a block is the longest run one mission row covers with its own mains and steps,
 * at least one a main: the base row's if it covers any (at most `own` entries, one mission's), else the longest of
 * every other row's, the first on a tie. 0: a primary no row covers, or more blocks than the arcs deal. */
static int Blocks(uint32_t base, const uint32_t* ids, const int* listClass, uint32_t count, uint32_t own, uint8_t* block, uint8_t* isMain,
    uint8_t* blockRow, uint32_t* blocksOut)
{
    uint32_t prim[CO_LIST_MOST], np = 0, blocks = 0;
    for (uint32_t i = 0; i < count; i++) { block[i] = 0; isMain[i] = 0; if (listClass[i] <= 1) prim[np++] = i; }
    for (uint32_t k = 0; k < np;)
    {
        uint32_t bestLength = 0, bestRow = 0;
        for (uint32_t candidate = 0; candidate <= CO_MIX_ROWS; candidate++)
        {
            uint32_t r = candidate == 0 ? base : candidate - 1, j = k, mains = 0;
            if (candidate > 0 && r == base) continue;
            for (; j < np && (r != base || j - k < own); j++)
            {
                const int c = OwnClass(r, ids[prim[j]]), listed = listClass[prim[j]];
                if (c < 0 || (r == base ? listed == -1 : (c == 1) != (listed == 1))) break;
                mains += c == 0;
            }
            if (mains > 0 && j - k > bestLength) { bestLength = j - k; bestRow = r; }
            if (candidate == 0 && bestLength > 0) break; /* the base row's own block: no other row is asked */
        }
        if (bestLength == 0 || blocks == CO_LIST_BLOCKS) return 0;
        blockRow[blocks++] = (uint8_t)bestRow;
        for (uint32_t j = k; j < k + bestLength; j++) { block[prim[j]] = (uint8_t)blocks; isMain[prim[j]] = OwnClass(bestRow, ids[prim[j]]) == 0; }
        k += bestLength;
    }
    *blocksOut = blocks;
    return 1;
}

static void Keep(uint32_t row, const CoList* f)
{
    const uint32_t at = g_keptNext++ % 8;
    g_kept[at].row = row; g_kept[at].count = f->count; g_kept[at].blocks = f->blocks;
    memcpy(g_kept[at].ids, f->ids, sizeof f->ids); memcpy(g_kept[at].block, f->block, sizeof f->block);
    memcpy(g_kept[at].main, f->main, sizeof f->main); memcpy(g_kept[at].blockRow, f->blockRow, sizeof f->blockRow);
    memcpy(g_kept[at].blockMission, f->blockMission, sizeof f->blockMission);
}

int co_list_kept(uint32_t row, const uint32_t* ids, uint32_t count, uint8_t* block, uint8_t* isMain, uint8_t* blockRow, uint8_t* blockMission,
    uint32_t* blocks)
{
    int found = 0;
    if (count == 0 || count > CO_LIST_MOST) return 0;
    AcquireSRWLockShared(&g_lock);
    for (uint32_t i = 0; i < 8 && !found; i++)
    {
        if (g_kept[i].blocks == 0 || g_kept[i].row != row || g_kept[i].count != count || memcmp(g_kept[i].ids, ids, 4ull * count) != 0) continue;
        memcpy(block, g_kept[i].block, count); memcpy(isMain, g_kept[i].main, count);
        memcpy(blockRow, g_kept[i].blockRow, sizeof g_kept[i].blockRow);
        if (blockMission != NULL) memcpy(blockMission, g_kept[i].blockMission, sizeof g_kept[i].blockMission);
        *blocks = g_kept[i].blocks;
        found = 1;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/* One block of the selection again, as the list's next block: mission q's. */
static void Again(CoList* out, const uint32_t* selected, uint32_t n, const uint8_t* block, const uint8_t* isMain, const uint8_t* blockRow, uint32_t k,
    uint32_t q)
{
    out->blockMission[out->blocks] = (uint8_t)q;
    out->blockRow[out->blocks++] = blockRow[k];
    for (uint32_t i = 0; i < n; i++)
    {
        if (block[i] != k + 1) continue;
        out->ids[out->count] = selected[i]; out->block[out->count] = (uint8_t)out->blocks; out->main[out->count] = isMain[i];
        out->count++;
    }
}

/* Major and then optional ids of the selection again while places are free: an id once a sweep, in the selection's
 * order, within what the missions of the row hold. Past that the game's placement found no free site for the repeats. */
static void Repeats(CoList* out, const uint32_t* selected, uint32_t n, const int* cls, uint32_t row, const CoListRules* rules)
{
    const uint32_t m = rules->missions, want[2] = {rules->wantMajor, rules->wantOptional};
    uint32_t uses[SELECTED] = {0}, cap[SELECTED], first[SELECTED], bitUses[8] = {0}, have[2] = {out->major[0], out->optional[0]};
    uint8_t bits[SELECTED], may[SELECTED];
    for (uint32_t i = 0; i < n; i++)
    {
        const Definition* d = DefinitionOf(selected[i]);
        uint32_t most = 0xFFFFFFFFu;
        for (int k = 0; k < 32; k++) if (g_rows[row][k].id == selected[i]) { most = g_rows[row][k].most; break; }
        cap[i] = d == NULL ? 0 : d->most < most ? d->most : most;
        cap[i] = cap[i] > 0xFFFFFFFFu / m ? 0xFFFFFFFFu : cap[i] * m;
        bits[i] = d == NULL ? 0 : d->bits;
        /* an objective with no place on the map is spawned from the descriptor: a repeat on the list is nothing */
        may[i] = d != NULL && !d->nowhere && selected[i] != LIVE_EVENT;
        first[i] = i;
        for (uint32_t j = 0; j < i; j++) if (selected[j] == selected[i]) { first[i] = j; break; }
        uses[first[i]]++;
        for (int t = 0; t < 8; t++) if (bits[i] & (1u << t)) bitUses[t]++;
    }
    for (int c = 0; c < 2; c++) /* majors first: the game places the list in order */
    {
        for (int progress = 1; progress && have[c] < want[c] && out->count < rules->places;)
        {
            progress = 0;
            for (uint32_t i = 0; i < n && have[c] < want[c] && out->count < rules->places; i++)
            {
                int ok = cls[i] == c + 2 && first[i] == i && may[i] && uses[i] < cap[i];
                for (int t = 0; t < 8 && ok; t++) if ((bits[i] & (1u << t)) && bitUses[t] >= m) ok = 0;
                if (!ok) continue;
                out->ids[out->count++] = selected[i];
                uses[i]++;
                for (int t = 0; t < 8; t++) if (bits[i] & (1u << t)) bitUses[t]++;
                have[c]++;
                progress = 1;
            }
        }
        if (have[c] < want[c] && out->count < rules->places) out->capped[c] = 1;
    }
    out->major[1] = have[0]; out->optional[1] = have[1];
}

const char* co_list(uintptr_t game, uint32_t row, const uint32_t* selected, uint32_t n, const CoListRules* rules, CoList* out)
{
    int cls[SELECTED], listClass[SELECTED];
    uint8_t block[CO_LIST_MOST], isMain[CO_LIST_MOST], blockRow[CO_LIST_BLOCKS], drop[SELECTED] = {0}, other[CO_LIST_BLOCKS][CO_LIST_BLOCKS];
    uint32_t unit = 0, own = 0, length[CO_LIST_BLOCKS] = {0}, can[2] = {0}, used = n, firsts = 0, total;
    memset(out, 0, sizeof *out);
    if (n == 0 || n > SELECTED || row >= CO_MIX_ROWS || rules->missions == 0 || rules->places < n || rules->places > CO_LIST_MOST)
        return "the selection is none a list is made from";
    AcquireSRWLockExclusive(&g_lock);
    if (!Tables(game)) { ReleaseSRWLockExclusive(&g_lock); return "the mission rows or the objective definitions could not be read"; }
    for (uint32_t i = 0; i < n; i++)
    {
        const Definition* d = DefinitionOf(selected[i]);
        cls[i] = RowClass(row, selected[i]);
        listClass[i] = selected[i] == LIVE_EVENT ? 2 : cls[i];
        out->major[0] += cls[i] == 2; out->optional[0] += cls[i] == 3;
        /* what may make room: a major or optional objective that has a place on the map and is not the level's own */
        if (listClass[i] >= 2 && (cls[i] == 2 || cls[i] == 3) && selected[i] != LIVE_EVENT && selected[i] != RADAR && d != NULL && !d->nowhere)
            can[cls[i] - 2]++;
        else if (listClass[i] >= 2) drop[i] = 0xFF; /* stays */
    }
    for (uint32_t i = 0; i < n; i++) /* the leading primaries the base row owns: one mission's block of it */
    {
        if (listClass[i] >= 2) continue;
        if (listClass[i] == -1 || OwnClass(row, selected[i]) < 0) break;
        own++;
    }
    if (!Blocks(row, selected, listClass, n, own, block, isMain, blockRow, &unit)) { unit = 0; memset(block, 0, sizeof block); memset(isMain, 0, sizeof isMain); }
    for (uint32_t i = 0; i < n; i++) if (block[i]) length[block[i] - 1]++;
    /* what the other missions get: the first block each, then the other blocks a mission at a time. `used` is the
     * list's length with them; what passes the places comes off the selection's side objectives. */
    memset(other, 0, sizeof other);
    total = unit;
    uint32_t off[2] = {0};
    if (rules->primaries && unit > 0)
    {
        for (uint32_t q = 1; q < rules->missions && total < CO_LIST_BLOCKS; q++)
        {
            const uint32_t need = used + length[0] > rules->places ? used + length[0] - rules->places : 0;
            if (need > can[0] - off[0] + can[1] - off[1]) break;
            const uint32_t optional = need < can[1] - off[1] ? need : can[1] - off[1];
            off[1] += optional; off[0] += need - optional;
            used += length[0] - need; firsts++; total++;
        }
        int room = 1;
        for (uint32_t q = 1; q <= firsts && room; q++)
            for (uint32_t k = 1; k < unit; k++)
            {
                const uint32_t need = used + length[k] > rules->places ? used + length[k] - rules->places : 0;
                if (need > can[1] - off[1] || total == CO_LIST_BLOCKS) { room = 0; break; }
                off[1] += need; used += length[k] - need; other[q][k] = 1; total++;
            }
        out->short_ = (rules->missions - 1) * unit - (total - unit);
    }
    out->dropped[0] = off[0]; out->dropped[1] = off[1];
    for (int c = 1; c >= 0; c--) /* from the end of the selection, as HellForge makes room for a partner */
        for (uint32_t i = n; i-- > 0 && off[c] > 0;)
            if (drop[i] == 0 && listClass[i] >= 2 && cls[i] == c + 2) { drop[i] = 1; off[c]--; }
    for (uint32_t i = 0; i < n; i++)
    {
        if (drop[i] == 1) continue;
        out->ids[out->count] = selected[i]; out->block[out->count] = block[i]; out->main[out->count] = isMain[i];
        out->count++;
    }
    memcpy(out->blockRow, blockRow, unit);
    out->blocks = unit; /* the selection's own blocks: the first mission's (blockMission 0) */
    for (uint32_t q = 1; q <= firsts; q++)
    {
        Again(out, selected, n, block, isMain, blockRow, 0, q);
        for (uint32_t k = 1; k < unit; k++) if (other[q][k]) Again(out, selected, n, block, isMain, blockRow, k, q);
    }
    out->unit = unit; out->own = own; out->held = unit > 0 ? 1 + firsts : 0;
    out->major[1] = out->major[0] - out->dropped[0]; out->optional[1] = out->optional[0] - out->dropped[1];
    if (out->count < rules->places) Repeats(out, selected, n, cls, row, rules);
    for (uint32_t i = 0; i < out->count; i++) { out->mains += out->main[i]; out->steps += out->block[i] != 0 && !out->main[i]; }
    if (out->blocks > 0) Keep(row, out);
    ReleaseSRWLockExclusive(&g_lock);
    return NULL;
}

#ifdef CO_TEST
void co_list_test_reset(void) { memset(g_kept, 0, sizeof g_kept); g_keptNext = 0; }
#endif
