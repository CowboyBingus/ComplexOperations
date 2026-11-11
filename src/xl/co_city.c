/* The city generator's footprint list and location capacity per level (co_city.h). HellForge's values are pinned
 * from its source (co_city_sites.h). */
#include <string.h>

#include <windows.h>

#include "co_city.h"
#include "co_city_sites.h"

static struct { uint8_t* page; volatile uint32_t* cells; } g_city;

/* The capacity's store (mov dword [rcx+0x284], places) once the allocation's stub has stored it: one long nop. */
static const uint8_t kCapacityOut[10] = {0x66, 0x2E, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[16];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

const char* co_city_check(uintptr_t game)
{
    for (int i = 0; i < CO_CITY_SITES; i++)
        if (!Same(game + kCoCitySites[i].rva, kCoCitySites[i].bytes, kCoCitySites[i].length))
            return i < CO_CITY_OP_LIMIT ? "the city generator's footprint list is not as difficulty 11 leaves it" : "the city generator is not the game's code this mod was made for";
    if (!Same(game + kCoCitySites[CO_CITY_OP_LIMIT].back, kCoCityOpBehind, sizeof kCoCityOpBehind) ||
        !Same(game + kCoCitySites[CO_CITY_PLACER].back, kCoCityPlacerBehind, sizeof kCoCityPlacerBehind))
        return "the city generator is not the game's code this mod was made for";
    return NULL;
}

static size_t Rel(uint8_t* out, size_t n, uintptr_t code, uintptr_t target)
{
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(code + n + 4));
    memcpy(out + n, &rel, 4);
    return n + 4;
}
static size_t Put(uint8_t* out, size_t n, const uint8_t* bytes, size_t size) { memcpy(out + n, bytes, size); return n + size; }

size_t co_city_stub(uint8_t* out, uintptr_t code, uintptr_t game, int which, uintptr_t cells)
{
    static const uint8_t store[] = {0x89, 0x91, 0x84, 0x02, 0x00, 0x00, 0x6B, 0xD2, CO_CITY_FOOTPRINT}; /* mov [rcx+0x284], edx; imul edx, edx, 0x60 */
    static const uint8_t clear[] = {0x44, 0x6B, 0x85, 0x84, 0x02, 0x00, 0x00, CO_CITY_FOOTPRINT};        /* imul r8d, [rbp+0x284], 0x60 */
    static const uint8_t op[] = {0x3B, 0x86, 0xF8, 0x01, 0x00, 0x00, 0x0F, 0x47, 0x86, 0xF8, 0x01, 0x00, 0x00}; /* cmp eax, [rsi+0x1f8]; cmova eax, [rsi+0x1f8] */
    static const uint8_t placer[] = {0x41, 0x3B, 0x44, 0x24, 0x04, 0x41, 0x0F, 0x47, 0x44, 0x24, 0x04};  /* cmp eax, [r12+4]; cmova eax, [r12+4] */
    size_t n = 0;
    /* each stub reads its cell once, so a call of the list's maker or one test never sees two levels' numbers; the
     * flags it changes are dead at each site (a call or a compare follows before any is read) */
    if (which == CO_CITY_ALLOCATION)
    {
        out[n++] = 0x8B; out[n++] = 0x15; n = Rel(out, n, code, cells + 4u * CO_CITY_CELL_PLACES); /* mov edx, [places] */
        n = Put(out, n, store, sizeof store);
    }
    else if (which == CO_CITY_CLEAR) n = Put(out, n, clear, sizeof clear);
    else if (which == CO_CITY_OP_LIMIT || which == CO_CITY_PLACER)
    {
        const int cell = which == CO_CITY_OP_LIMIT ? CO_CITY_CELL_OP : CO_CITY_CELL_PLACER;
        out[n++] = 0x8B; out[n++] = 0x05; n = Rel(out, n, code, cells + 4u * (unsigned)cell);     /* mov eax, [limit] */
        n = which == CO_CITY_OP_LIMIT ? Put(out, n, op, sizeof op) : Put(out, n, placer, sizeof placer);
    }
    else return 0;
    out[n++] = 0xE9; n = Rel(out, n, code, game + kCoCitySites[which].back);
    return n;
}

static void Level(volatile uint32_t* cells, int complex)
{
    cells[CO_CITY_CELL_PLACES] = complex ? CO_CITY_PLACES_OWN : CO_CITY_PLACES_STOCK;
    cells[CO_CITY_CELL_OP] = complex ? CO_CITY_NO_LIMIT : CO_CITY_LOCATIONS_STOCK;
    cells[CO_CITY_CELL_PLACER] = complex ? CO_CITY_LOCATIONS_OWN : CO_CITY_NO_LIMIT;
}

/* The order the sites are written in: with a normal level's numbers every state between two writes makes HellForge's
 * list, so the capacity's store goes out last, when the allocation's stub already stores the same number. */
static const uint8_t kOrder[CO_CITY_SITES] = {CO_CITY_ALLOCATION, CO_CITY_CLEAR, CO_CITY_CAPACITY, CO_CITY_OP_LIMIT, CO_CITY_PLACER};

const char* co_city_install(uintptr_t game)
{
    const char* why;
    DWORD old;
    int done;
    if (g_city.page != NULL) return NULL;
    if ((why = co_city_check(game)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game + kCoCitySites[0].rva, 0x2000);
    if (page == NULL) return "no memory near the game for the city generator's steps";
    volatile uint32_t* cells = (volatile uint32_t*)(page + 0x1000); /* written per level: not on the page that runs */
    Level(cells, 0);
    memset(page, 0xCC, 0x1000);
    for (int i = 0; i < CO_CITY_SITES; i++)
        if (i != CO_CITY_CAPACITY) co_city_stub(page + CO_CITY_SLOT * (unsigned)i, (uintptr_t)page + CO_CITY_SLOT * (unsigned)i, game, i, (uintptr_t)cells);
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the city generator's steps could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    for (done = 0; done < CO_CITY_SITES; done++)
    {
        const CoCitySite* s = &kCoCitySites[kOrder[done]];
        uint8_t bytes[10];
        if (kOrder[done] == CO_CITY_CAPACITY) memcpy(bytes, kCapacityOut, sizeof kCapacityOut);
        else
        {
            memset(bytes, 0x90, sizeof bytes); bytes[0] = 0xE9;
            Rel(bytes, 1, game + s->rva, (uintptr_t)page + CO_CITY_SLOT * (unsigned)kOrder[done]);
        }
        if (g_coApi->write(game + s->rva, bytes, s->length) != 0) break;
    }
    if (done == CO_CITY_SITES) { g_city.cells = cells; g_city.page = page; return NULL; }
    while (done-- > 0) g_coApi->write(game + kCoCitySites[kOrder[done]].rva, kCoCitySites[kOrder[done]].bytes, kCoCitySites[kOrder[done]].length);
    return "the city generator's steps could not be written";
}

void co_city_level(int complex) { if (g_city.page != NULL) Level(g_city.cells, complex); }
uint32_t co_city_cell(int which)
{
    uint32_t stock[CO_CITY_CELLS];
    if (which < 0 || which >= CO_CITY_CELLS) return 0;
    if (g_city.page != NULL) return g_city.cells[which];
    Level(stock, 0);
    return stock[which];
}

#ifdef CO_TEST
void co_city_test_reset(void) { memset(&g_city, 0, sizeof g_city); }
const uint8_t* co_city_test_page(void) { return g_city.page; }
#endif
