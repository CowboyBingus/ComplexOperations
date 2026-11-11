/* The vertex weld's pool per level (co_weld.h). HellForge's values are pinned from its source (co_weld_sites.h). */
#include <string.h>

#include <windows.h>

#include "co_weld.h"
#include "co_weld_sites.h"
#include "co_normal.h"

#define VALUES (sizeof kCoWeldValues / sizeof kCoWeldValues[0])
static struct { uint8_t* page; uint32_t* limit; CoWeldEndpointFn endpoint; } g_weld;

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[16];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

const char* co_weld_check(uintptr_t game)
{
    for (size_t i = 0; i < VALUES; i++)
        if (!Same(game + kCoWeldValues[i].rva, kCoWeldValues[i].stock, kCoWeldValues[i].length))
            return i < 8 ? "the structured maps' vertex pool is not as difficulty 11 leaves it" : "the structured maps' vertex weld is not the game's code this mod was made for";
    for (int i = 0; i < 3; i++)
        if (!Same(game + kCoWeldFull[i].rva, kCoWeldFull[i].bytes, kCoWeldFull[i].length)) return "the structured maps' vertex weld is not the game's code this mod was made for";
    if (!g_coApi->code_matches(game + CO_WELD_ENDPOINT, CO_WELD_ENDPOINT_PIN)) return "the structured weld's endpoint lookup is not the game's code this mod was made for";
    return NULL;
}

static void Rel(uint8_t* out, size_t at, uintptr_t code, uintptr_t target)
{
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(code + at + 4));
    memcpy(out + at, &rel, 4);
}

size_t co_weld_full_stub(uint8_t* out, uintptr_t code, uintptr_t game, int which, uintptr_t limit)
{
    const CoWeldFull* f = &kCoWeldFull[which];
    size_t n = 0;
    if (which < 2) out[n++] = 0x44;                 /* cmp r8d, [rip -> limit]; the third: cmp edx */
    out[n++] = 0x3B; out[n++] = which < 2 ? 0x05 : 0x15;
    Rel(out, n, code, limit); n += 4;
    out[n++] = 0x0F; out[n++] = which == 0 ? 0x82 : 0x83; /* the game's own branch: jb (room) or jae (full) */
    Rel(out, n, code, game + f->taken); n += 4;
    out[n++] = 0xE9; Rel(out, n, code, game + f->on); n += 4;
    while (n < CO_WELD_STUB) out[n++] = 0xCC;
    return CO_WELD_STUB;
}

uint64_t co_weld_endpoint(uint64_t weldAt, uint64_t vertexAt, uint64_t nodesAt, uint64_t outAt, CoWeldEndpointFn original)
{
    uint8_t* weld = (uint8_t*)(uintptr_t)weldAt; uint8_t* p = (uint8_t*)(uintptr_t)vertexAt;
    uint32_t* nodes = (uint32_t*)(uintptr_t)nodesAt; uint8_t** out = (uint8_t**)(uintptr_t)outAt;
    uint8_t* pool = weld + CO_WELD_POOL;
    uint32_t used = *(volatile uint32_t*)(weld + CO_WELD_COUNT);
    if (used > CO_WELD_BLOCKS) used = CO_WELD_BLOCKS;
    if (p < pool || p >= pool + (size_t)used * CO_WELD_BLOCK) return original(weldAt, vertexAt, nodesAt, outAt);
    uint8_t* block = pool + (size_t)(p - pool) / CO_WELD_BLOCK * CO_WELD_BLOCK;
    const size_t in = (size_t)(p - block);
    if (in < 8u || (in - 8u) % 0x98u != 0 || (in - 8u) / 0x98u >= *(uint32_t*)block) return original(weldAt, vertexAt, nodesAt, outAt);
    const uint32_t i = (uint32_t)((in - 8u) / 0x98u);
    uint8_t** slot = (uint8_t**)(block + CO_WELD_POINTERS) + i;
    if (*slot != NULL) { *out = *slot; return 1; }
    if (nodes[0] >= nodes[1]) { *out = NULL; return 0; } /* nodes: u32 count, u32 capacity, the array at +8 */
    uint8_t* to = *(uint8_t**)(nodes + 2) + (size_t)nodes[0] * 0x98u;
    nodes[0]++;
    *slot = to;
    memcpy(to, block + 8u + (size_t)i * 0x98u, 0x98u);
    *out = to;
    return 1;
}

static uint64_t Endpoint(uint64_t weld, uint64_t vertex, uint64_t nodes, uint64_t out)
{
    if (g_weld.limit == NULL || *g_weld.limit != CO_WELD_PER) return g_weld.endpoint(weld, vertex, nodes, out);
    return co_weld_endpoint(weld, vertex, nodes, out, g_weld.endpoint);
}

/* HellForge's or the game's value back at the first `values` sites and the first `jumps` tests. */
static void Undo(uintptr_t game, size_t values, int jumps)
{
    for (size_t i = 0; i < values; i++) g_coApi->write(game + kCoWeldValues[i].rva + kCoWeldValues[i].at, kCoWeldValues[i].stock + kCoWeldValues[i].at, 4);
    for (int i = 0; i < jumps; i++) g_coApi->write(game + kCoWeldFull[i].rva, kCoWeldFull[i].bytes, kCoWeldFull[i].length);
}

const char* co_weld_install(uintptr_t game)
{
    /* the game's caller keeps r9 across the lookup (0xFBB1DF, read again at 0xFBB202), which a C function need not:
     * push r9; sub rsp, 0x20; mov rax, Endpoint; call rax; add rsp, 0x20; pop r9; ret */
    static const uint8_t head[] = {0x41, 0x51, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xB8}, tail[] = {0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x59, 0xC3};
    const char* why;
    const uint64_t to = (uint64_t)(uintptr_t)Endpoint;
    DWORD old;
    size_t v;
    int j;
    if (g_weld.page != NULL) return NULL;
    if ((why = co_weld_check(game)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game + CO_WELD_ENDPOINT, 0x2000);
    if (page == NULL) return "no memory near the game for the vertex weld's steps";
    uint32_t* limit = (uint32_t*)(page + 0x1000);
    *limit = co_level_limit(CO_LIMIT_WELD_VERTICES, 0);
    for (int i = 0; i < 3; i++) co_weld_full_stub(page + 0x20 * i, (uintptr_t)page + 0x20u * (unsigned)i, game, i, (uintptr_t)limit);
    memcpy(page + 0x80, head, sizeof head); memcpy(page + 0x80 + sizeof head, &to, 8); memcpy(page + 0x80 + sizeof head + 8, tail, sizeof tail);
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the vertex weld's steps could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    g_weld.limit = limit;
    /* the tests first: with them reading 16, a weld that runs meanwhile fills no block past what either layout holds */
    for (j = 0; j < 3; j++)
    {
        uint8_t jump[10];
        memset(jump, 0x90, sizeof jump); jump[0] = 0xE9; Rel(jump, 1, game + kCoWeldFull[j].rva, (uintptr_t)page + 0x20u * (unsigned)j);
        if (g_coApi->write(game + kCoWeldFull[j].rva, jump, kCoWeldFull[j].length) != 0) { Undo(game, 0, j); g_weld.limit = NULL; return "the vertex weld's tests could not be written"; }
    }
    for (v = 0; v < VALUES; v++)
        if (g_coApi->write(game + kCoWeldValues[v].rva + kCoWeldValues[v].at, &kCoWeldValues[v].own, 4) != 0)
        { Undo(game, v, 3); g_weld.limit = NULL; return "the structured maps' vertex pool could not be enlarged"; }
    if (g_coApi->hook_ex(g_coMod, game + CO_WELD_ENDPOINT, page + 0x80, (void**)&g_weld.endpoint, INT32_MIN) != 0)
    { Undo(game, VALUES, 3); g_weld.limit = NULL; return "the structured weld's endpoint lookup could not be hooked"; }
    g_weld.page = page;
    return NULL;
}

void co_weld_level(int complex) { if (g_weld.page != NULL) *g_weld.limit = co_level_limit(CO_LIMIT_WELD_VERTICES, complex); }
uint32_t co_weld_limit(void) { return g_weld.page != NULL ? *g_weld.limit : co_level_limit(CO_LIMIT_WELD_VERTICES, 0); }

#ifdef CO_TEST
void co_weld_test_reset(void) { memset(&g_weld, 0, sizeof g_weld); }
const uint8_t* co_weld_test_page(void) { return g_weld.page; }
#endif
