/* FixUnreachableAreas on a complex level (co_pocket.h). The game's code is pinned from its image (co_pocket_sites.h). */
#include <string.h>

#include <windows.h>

#include "co_pocket.h"
#include "co_pocket_sites.h"
#include "co_chain.h"
#include "co_normal.h"
#include "co_shim.h"

static struct
{
    uint8_t* page;
    char (*in_disk)(uint64_t, uint64_t);
    volatile LONG joined, failed;
} g_pocket;

/* ---- the reconnect ------------------------------------------------------------------------------------------------ */

enum { CELL_UNKNOWN, CELL_OPEN, CELL_PASSABLE, CELL_CLOSED };

static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static uint64_t U64(const uint8_t* at) { uint64_t v; memcpy(&v, at, 8); return v; }

/* The disk test walks a cell's routes and their end points with no check of its own: all of it must be readable. */
static int RoutesReadable(uint64_t cell, uint64_t routes[16])
{
    uint8_t ends[0x20];
    if (co_safe_read((uintptr_t)(cell + 8), routes, 16 * 8)) return 0;
    for (int k = 0; k < 16 && routes[k] != 0; k++) if (co_safe_read((uintptr_t)routes[k], ends, sizeof ends)) return 0;
    return 1;
}

/* Blocked as the pass tests a cell (0xF67BD8): type 1..6, outside the disk, or the planet's flag with a type object
 * that lacks bit 5 of +0x170. A blocked cell carries a path when it lies in the disk and holds no location. */
static uint8_t Classify(const CoPocketGraph* g, const uint8_t* sub, uint64_t subAt, uint64_t cell)
{
    uint64_t routes[16], object = 0, location = 0;
    uint8_t flags = 0;
    if (!RoutesReadable(cell, routes)) return CELL_CLOSED;
    const int disk = g->in_disk(g->ctx, subAt) != 0;
    int blocked = U32(sub) - 1u <= 5u || !disk;
    if (!blocked && g->planet)
        blocked = co_safe_read((uintptr_t)(g->types + (uint64_t)(int64_t)(int32_t)U32(sub + 0xC) * 8), &object, 8) || object == 0 ||
            co_safe_read((uintptr_t)(object + 0x170), &flags, 1) || !((flags >> 5) & 1);
    if (!blocked) return CELL_OPEN;
    if (co_safe_read((uintptr_t)(cell + 0x98), &location, 8)) location = 1;
    return disk && location == 0 ? CELL_PASSABLE : CELL_CLOSED;
}

int co_pocket_reconnect(const CoPocketGraph* g, uint32_t pocket, uint32_t mainId, uint32_t* searched)
{
    const uint32_t n = g->count;
    if (searched != NULL) *searched = 0;
    if (mainId == 0xFFFFFFFFu || pocket == mainId || n == 0 || n > CO_POCKET_CELLS_MAX || g->cells == 0 || g->subregions == 0 || g->ids == 0) return -1;
    /* per cell: where the search came from, the queue, the ids and the subregions as read, what the cell is */
    uint8_t* scratch = (uint8_t*)VirtualAlloc(NULL, (size_t)n * (12 + 0x20 + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (scratch == NULL) return -1;
    int32_t* from = (int32_t*)scratch;
    uint32_t* queue = (uint32_t*)(from + n);
    uint32_t* ids = queue + n;
    uint8_t* subs = (uint8_t*)(ids + n);
    uint8_t* kind = subs + (size_t)n * 0x20;
    int length = -1;
    if (co_safe_read((uintptr_t)g->ids, ids, (size_t)n * 4) || co_safe_read((uintptr_t)g->subregions, subs, (size_t)n * 0x20)) goto done;
    uint32_t head = 0, tail = 0, last = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++)
    {
        from[i] = -2;
        if (ids[i] == pocket && U32(subs + (size_t)i * 0x20) - 1u > 5u) { from[i] = -1; queue[tail++] = i; last = i; }
    }
    int32_t found = -1;
    while (head < tail && found < 0)
    {
        const uint32_t i = queue[head++];
        const uint64_t cell = g->cells + (uint64_t)i * 0x110;
        uint64_t routes[16];
        /* the pass walks from a subregion's own route cell and takes cell k's subregion for subregion k (0xF67CA5):
         * a subregion that names another cell is left out, as is a cell whose routes cannot be read */
        if (U64(subs + (size_t)i * 0x20 + 0x18) != cell || co_safe_read((uintptr_t)(cell + 8), routes, sizeof routes)) continue;
        for (int k = 0; k < 16 && routes[k] != 0 && found < 0; k++)
        {
            uint64_t ends[2];
            if (co_safe_read((uintptr_t)(routes[k] + 0x20), ends, sizeof ends)) continue;
            for (int e = 0; e < 2; e++)
            {
                const uint64_t other = ends[e];
                if (other == 0 || other == cell || other < g->cells || (other - g->cells) % 0x110 != 0 || (other - g->cells) / 0x110 >= n) continue;
                const uint32_t j = (uint32_t)((other - g->cells) / 0x110);
                if (from[j] != -2) continue;
                if (kind[j] == CELL_UNKNOWN)
                    kind[j] = U64(subs + (size_t)j * 0x20 + 0x18) != other ? CELL_CLOSED : Classify(g, subs + (size_t)j * 0x20, g->subregions + (uint64_t)j * 0x20, other);
                if (kind[j] == CELL_OPEN && ids[j] == mainId) { from[j] = (int32_t)i; found = (int32_t)j; break; }
                if (kind[j] != CELL_PASSABLE) continue;
                from[j] = (int32_t)i; queue[tail++] = j;
            }
        }
    }
    if (searched != NULL) *searched = tail;
    if (found < 0 || last == 0xFFFFFFFFu) goto done;
    /* as the pass applies its own path (0xF67E81..0xF67EF8); a path cell takes the main's id too, so that a later
     * pocket finds the main area in it */
    length = 0;
    for (int32_t j = from[found]; j >= 0 && from[j] != -1; j = from[j])
    {
        memcpy((void*)(uintptr_t)(g->subregions + (uint64_t)(uint32_t)j * 0x20), subs + (size_t)last * 0x20, 16);
        *(uint32_t*)(uintptr_t)(g->ids + (uint64_t)(uint32_t)j * 4) = mainId;
        length++;
    }
    for (uint32_t i = 0; i < n; i++) if (from[i] == -1) *(uint32_t*)(uintptr_t)(g->ids + (uint64_t)i * 4) = mainId;
done:
    VirtualFree(scratch, 0, MEM_RELEASE);
    return length;
}

/* At the restart (0xF67FD6) the pass's frame holds, since the search's call at 0xF67E4D: the subregions at [rbp-0x58]
 * and the route cells at [rsp+0x78] (the two spans change places for that call), the ids at [rbp-0x70], the cell
 * count at [rbp-0x40] and the planet's flag at [rsp+0x64]. */
char co_pocket_frame(uint8_t* rsp, uint8_t* rbp, uint64_t ctx, uint32_t pocket, uint64_t types, uint32_t mainId)
{
    CoPocketGraph g = {0};
    uint64_t count = 0, settings = 0;
    uint8_t fields[0x58];
    uint32_t searched = 0;
    g.ctx = ctx; g.types = types; g.in_disk = g_pocket.in_disk;
    /* the disk test reads the settings at [ctx+8] (+0x2C, +0x50) unchecked */
    if (g.in_disk == NULL || co_safe_read((uintptr_t)(rbp - 0x58), &g.subregions, 8) || co_safe_read((uintptr_t)(rsp + 0x78), &g.cells, 8) ||
        co_safe_read((uintptr_t)(rbp - 0x70), &g.ids, 8) || co_safe_read((uintptr_t)(rbp - 0x40), &count, 8) || count == 0 || count > CO_POCKET_CELLS_MAX ||
        co_safe_read((uintptr_t)(rsp + 0x64), &g.planet, 1) || co_safe_read((uintptr_t)(ctx + 8), &settings, 8) || settings == 0 ||
        co_safe_read((uintptr_t)settings, fields, sizeof fields))
    {
        if (InterlockedIncrement(&g_pocket.failed) <= 20) co_log("FixUnreachableAreas: the pass's frame could not be read at a restart; the level restarts");
        return 0;
    }
    g.count = (uint32_t)count;
    const int length = co_pocket_reconnect(&g, pocket, mainId, &searched);
    if (length < 0)
    {
        if (InterlockedIncrement(&g_pocket.failed) <= 20)
            co_log("FixUnreachableAreas: pocket %u has no path to the main area %u over blocked cells without a location (%u cells searched); the level restarts",
                pocket, mainId, searched);
        return 0;
    }
    const LONG joined = InterlockedIncrement(&g_pocket.joined);
    if (joined <= 20)
        co_log("FixUnreachableAreas: pocket %u joined to the main area %u by %d blocked cells (%u cells searched); joined %ld, not joined %ld",
            pocket, mainId, length, searched, (long)joined, (long)g_pocket.failed);
    return 1;
}

/* ---- the stubs ---------------------------------------------------------------------------------------------------- */

static void Rel(uint8_t* out, size_t at, size_t end, uintptr_t code, uintptr_t target)
{
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(code + end));
    memcpy(out + at, &rel, 4);
}

void co_pocket_build(uint8_t* page, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t next)
{
    /* the search (rbp the pocket cell's route cell, r15 ctx): a cell with a location starts a path on a complex level */
    static const uint8_t search[47] = {
        0x48, 0x83, 0xBD, 0x98, 0x00, 0x00, 0x00, 0x00,             /*  0 cmp qword [rbp+0x98], 0 */
        0x74, 0x1B,                                                 /*  8 je go */
        0x80, 0x3D, 0, 0, 0, 0, 0x00,                               /* 10 cmp byte [flag], 0 (at 12) */
        0x74, 0x17,                                                 /* 17 je skip */
        0x50,                                                       /* 19 push rax */
        0x49, 0x8B, 0x47, 0x18,                                     /* 20 mov rax, [r15+0x18] (the level) */
        0x81, 0xB8, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,             /* 24 cmp dword [rax+0x2d7c], key (at 30) */
        0x58,                                                       /* 34 pop rax */
        0x75, 0x05,                                                 /* 35 jne skip */
        0xE9, 0, 0, 0, 0,                                           /* 37 go: jmp 0xF67364 (at 38) */
        0xE9, 0, 0, 0, 0};                                          /* 42 skip: jmp 0xF67445 (at 43) */
    /* the list's bytes (r14 the level): by the level's disk alone, so that the two sizes of one pass never differ */
    static const uint8_t alloc[28] = {
        0xBA, 0x00, 0x01, 0x00, 0x00,                               /*  0 mov edx, 0x100 */
        0x41, 0x81, 0xBE, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,       /*  5 cmp dword [r14+0x2d7c], key (at 12) */
        0x75, 0x05,                                                 /* 16 jne on */
        0xBA, 0, 0, 0, 0,                                           /* 18 mov edx, the complex level's (at 19) */
        0xE9, 0, 0, 0, 0};                                          /* 23 on: jmp 0xF679CE (at 24) */
    static const uint8_t clear[30] = {
        0x41, 0xB8, 0x00, 0x01, 0x00, 0x00,                         /*  0 mov r8d, 0x100 */
        0x41, 0x81, 0xBE, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,       /*  6 cmp dword [r14+0x2d7c], key (at 13) */
        0x75, 0x06,                                                 /* 17 jne on */
        0x41, 0xB8, 0, 0, 0, 0,                                     /* 19 mov r8d, the complex level's (at 21) */
        0xE9, 0, 0, 0, 0};                                          /* 25 on: jmp 0xF67A12 (at 26) */
    /* the restart (rcx the level; rsp, 16-aligned, and rbp the pass's; r12 ctx, ebx the pocket, edi the main area,
     * r13 the type objects). Only rcx is read behind either way out, and the pass's xmm6 and xmm7 are kept by any
     * function. Every level that joins no pocket here goes to the chain's stub: its tries, or HellForge's step. */
    static const uint8_t restart[81] = {
        0x80, 0x3D, 0, 0, 0, 0, 0x00,                               /*  0 cmp byte [flag], 0 (at 2) */
        0x74, 0x43,                                                 /*  7 je pass */
        0x81, 0xB9, 0x7C, 0x2D, 0x00, 0x00, 0, 0, 0, 0,             /*  9 cmp dword [rcx+0x2d7c], key (at 15) */
        0x75, 0x37,                                                 /* 19 jne pass */
        0x48, 0x89, 0xE0,                                           /* 21 mov rax, rsp */
        0x51,                                                       /* 24 push rcx */
        0x48, 0x83, 0xEC, 0x38,                                     /* 25 sub rsp, 0x38 */
        0x4C, 0x89, 0x6C, 0x24, 0x20,                               /* 29 mov [rsp+0x20], r13 */
        0x89, 0x7C, 0x24, 0x28,                                     /* 34 mov [rsp+0x28], edi */
        0x48, 0x89, 0xC1,                                           /* 38 mov rcx, rax */
        0x48, 0x89, 0xEA,                                           /* 41 mov rdx, rbp */
        0x4D, 0x89, 0xE0,                                           /* 44 mov r8, r12 */
        0x41, 0x89, 0xD9,                                           /* 47 mov r9d, ebx */
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,                         /* 50 mov rax, co_pocket_frame (at 52) */
        0xFF, 0xD0,                                                 /* 60 call rax */
        0x48, 0x83, 0xC4, 0x38,                                     /* 62 add rsp, 0x38 */
        0x59,                                                       /* 66 pop rcx */
        0x84, 0xC0,                                                 /* 67 test al, al */
        0x74, 0x05,                                                 /* 69 je pass */
        0xE9, 0, 0, 0, 0,                                           /* 71 jmp 0xF67FB7 (at 72) */
        0xE9, 0, 0, 0, 0};                                          /* 76 pass: jmp the chain's stub (at 77) */
    const float key = co_disk_key();
    const uint32_t list = CO_POCKET_LIST_COMPLEX;
    const uint64_t frame = (uint64_t)(uintptr_t)co_pocket_frame;
    uint8_t* s = page + CO_POCKET_AT_SEARCH;
    memset(page, 0xCC, CO_POCKET_PAGE);
    memcpy(s, search, sizeof search);
    Rel(s, 12, 17, code + CO_POCKET_AT_SEARCH, flag);
    memcpy(s + 30, &key, 4);
    Rel(s, 38, 42, code + CO_POCKET_AT_SEARCH, game + CO_POCKET_SEARCH_GO);
    Rel(s, 43, 47, code + CO_POCKET_AT_SEARCH, game + CO_POCKET_SEARCH_SKIP);
    s = page + CO_POCKET_AT_ALLOC;
    memcpy(s, alloc, sizeof alloc);
    memcpy(s + 12, &key, 4); memcpy(s + 19, &list, 4);
    Rel(s, 24, 28, code + CO_POCKET_AT_ALLOC, game + kCoPocketSites[1].on);
    s = page + CO_POCKET_AT_CLEAR;
    memcpy(s, clear, sizeof clear);
    memcpy(s + 13, &key, 4); memcpy(s + 21, &list, 4);
    Rel(s, 26, 30, code + CO_POCKET_AT_CLEAR, game + kCoPocketSites[2].on);
    s = page + CO_POCKET_AT_RESTART;
    memcpy(s, restart, sizeof restart);
    Rel(s, 2, 7, code + CO_POCKET_AT_RESTART, flag);
    memcpy(s + 15, &key, 4); memcpy(s + 52, &frame, 8);
    Rel(s, 72, 76, code + CO_POCKET_AT_RESTART, game + CO_POCKET_GO_ON);
    Rel(s, 77, 81, code + CO_POCKET_AT_RESTART, next);
}

/* ---- the install -------------------------------------------------------------------------------------------------- */

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[64];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

const char* co_pocket_check(uintptr_t game)
{
    for (int i = 0; i < 3; i++)
        if (!Same(game + kCoPocketSites[i].rva, kCoPocketSites[i].bytes, kCoPocketSites[i].length)) return "FixUnreachableAreas is not the game's code this mod was made for";
    for (size_t i = 0; i < sizeof kCoPocketPins / sizeof kCoPocketPins[0]; i++)
        if (!Same(game + kCoPocketPins[i].rva, kCoPocketPins[i].bytes, kCoPocketPins[i].length)) return "FixUnreachableAreas is not the game's code this mod was made for";
    return NULL;
}

static int Reaches(uintptr_t from, uintptr_t to)
{
    const int64_t d = (int64_t)to - (int64_t)from;
    return d > -0x7FF00000ll && d < 0x7FF00000ll;
}

static int32_t JumpRel(uintptr_t site, uintptr_t target) { return (int32_t)((int64_t)target - (int64_t)(site + 5)); }

const char* co_pocket_install(uintptr_t game, uintptr_t flag)
{
    static const uint16_t at[3] = {CO_POCKET_AT_ALLOC, CO_POCKET_AT_CLEAR, CO_POCKET_AT_SEARCH};
    static const uint8_t site[3] = {1, 2, 0}; /* the list's allocation before its clearing: never more cleared than taken */
    const CoChain* chains = co_chain_sites();
    const char* why;
    uint8_t now[6];
    int32_t rel;
    DWORD old;
    int chain = 0, done = 0;
    if (g_pocket.page != NULL) return NULL;
    if ((why = co_pocket_check(game)) != NULL) return why;
    while (chain < CO_CHAINS && chains[chain].site != CO_POCKET_RESTART) chain++;
    const uintptr_t next = co_chain_slot(chain);
    /* the restart site holds the chain's jump: its stub keeps the tries, and HellForge's step for any other level */
    if (next == 0 || g_coApi->read(game + CO_POCKET_RESTART, now, 6) != 0 || now[0] != 0xE9 || now[5] != 0x90) return "FixUnreachableAreas' restart is not the step this mod chained";
    memcpy(&rel, now + 1, 4);
    if (rel != JumpRel(game + CO_POCKET_RESTART, next)) return "FixUnreachableAreas' restart is not the step this mod chained";
    uint8_t* page = (uint8_t*)co_alloc_near(game, CO_POCKET_PAGE);
    if (page == NULL) return "no memory near the game for FixUnreachableAreas' steps";
    if (!Reaches((uintptr_t)page, flag) || !Reaches((uintptr_t)page, next) || !Reaches((uintptr_t)page, game + CO_POCKET_SEARCH))
    { VirtualFree(page, 0, MEM_RELEASE); return "FixUnreachableAreas' steps cannot reach the game"; }
    co_pocket_build(page, (uintptr_t)page, game, flag, next);
    if (!VirtualProtect(page, CO_POCKET_PAGE, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "FixUnreachableAreas' steps could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, CO_POCKET_PAGE);
    g_pocket.in_disk = (char (*)(uint64_t, uint64_t))(game + CO_POCKET_IN_DISK);
    for (; done < 3; done++)
    {
        const CoPocketSite* s = &kCoPocketSites[site[done]];
        uint8_t jump[14];
        memset(jump, 0x90, sizeof jump); jump[0] = 0xE9;
        rel = JumpRel(game + s->rva, (uintptr_t)page + at[done]); memcpy(jump + 1, &rel, 4);
        if (g_coApi->write(game + s->rva, jump, s->length) != 0) break;
    }
    rel = JumpRel(game + CO_POCKET_RESTART, (uintptr_t)page + CO_POCKET_AT_RESTART);
    if (done == 3 && g_coApi->write(game + CO_POCKET_RESTART + 1, &rel, 4) == 0) { g_pocket.page = page; return NULL; }
    /* the game's bytes again, the clearing's before the allocation's; the page stays, a pass may be inside a stub */
    while (done-- > 0) g_coApi->write(game + kCoPocketSites[site[done]].rva, kCoPocketSites[site[done]].bytes, kCoPocketSites[site[done]].length);
    return "FixUnreachableAreas' steps could not be written";
}

#ifdef CO_TEST
void co_pocket_test_reset(void) { memset(&g_pocket, 0, sizeof g_pocket); }
uintptr_t co_pocket_test_page(void) { return (uintptr_t)g_pocket.page; }
void co_pocket_test_in_disk(char (*in_disk)(uint64_t, uint64_t)) { g_pocket.in_disk = in_disk; }
#endif
