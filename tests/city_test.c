/* The city generator per level: the footprint list's places and the location capacity op 0xA7 sees are HellForge's
 * on a normal level and the complex level's on a complex one, read once a use; HellForge's values are pinned, and a
 * refused install leaves every site as it was. Each stub is run from the game's own address. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_city.h"
#include "xl/co_city_sites.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define SPAN 0xF99000u
#define SPAN_SIZE 0x12000u /* every site lies in 0xF99000..0xFAB000 */
static uint8_t g_before[SPAN_SIZE];

static uint8_t* Reset(const char* name)
{
    DWORD old;
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_city_test_reset();
    fake_stock_chains(g_fake.image, 1022.0f / 1024.0f);
    memcpy(g_before, g_fake.image + SPAN, SPAN_SIZE);
    VirtualProtect(g_fake.image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old);
    return g_fake.image;
}
static int Unchanged(void) { return memcmp(g_before, g_fake.image + SPAN, SPAN_SIZE) == 0; }
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }
static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }

/* The allocation's size (0xFA904D): rcx the record; the size leaves in edx, which the allocator's call takes. */
static uint32_t RunAllocation(uintptr_t at, uint8_t* record)
{
    uint64_t d = 0xDEADBEEFu;
    __asm__ volatile("call *%[at]" : "+d" (d) : [at] "r" (at), "c" (record) : "rax", "r8", "r9", "r10", "r11", "cc", "memory");
    return (uint32_t)d;
}
/* The clear's size (0xFA90BE): rbp the record; the size leaves in r8d. */
static uint32_t RunClear(uintptr_t at, uint8_t* record)
{
    register uint64_t r8 __asm__("r8") = 0;
    /* fixed registers: neither operand may be the rbp this swaps */
    __asm__ volatile("push %%rbp\n\tmov %%rdi, %%rbp\n\tcall *%%rax\n\tpop %%rbp"
        : "+r" (r8) : "a" (at), "D" (record) : "rcx", "rdx", "r9", "r10", "r11", "cc", "memory");
    return (uint32_t)r8;
}
/* The op's copy of the capacity (0xFA9EFB): rsi the record + 8; the capacity leaves in eax. */
static uint32_t RunOp(uintptr_t at, uint8_t* record)
{
    uint64_t a = 0;
    __asm__ volatile("call *%[at]" : "+a" (a) : [at] "r" (at), "S" (record) : "rcx", "rdx", "r8", "r9", "r10", "r11", "cc", "memory");
    return (uint32_t)a;
}
/* The placer's test (0xF994A5): r12 the list {count, capacity}; the capacity leaves in eax. */
static uint32_t RunPlacer(uintptr_t at, uint32_t* list)
{
    register uint32_t* r12 __asm__("r12") = list;
    uint64_t a = 0;
    __asm__ volatile("call *%[at]" : "+a" (a) : [at] "r" (at), "r" (r12) : "rcx", "rdx", "r8", "r9", "r10", "r11", "cc", "memory");
    return (uint32_t)a;
}

static const uint32_t kCapacities[] = {0, 1, 2740, 8191, 8192, 8193, 12287, 12288, 12289, 16384, 65024, 0xFFFFFFFFu};
#define COUNT(a) (sizeof(a) / sizeof(a)[0])
static uint32_t Min(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* Every capacity through both reads; 0 when each is min(the capacity, the level's limit). */
static int Wrong(const uint8_t* image, uint32_t op, uint32_t placer)
{
    static uint8_t record[0x300];
    int wrong = 0;
    for (size_t i = 0; i < COUNT(kCapacities); i++)
    {
        uint32_t list[2] = {7, kCapacities[i]};
        memcpy(record + CO_CITY_RECORD_LOCATIONS, &kCapacities[i], 4);
        wrong += RunOp((uintptr_t)image + kCoCitySites[CO_CITY_OP_LIMIT].rva, record) != Min(kCapacities[i], op);
        wrong += RunPlacer((uintptr_t)image + kCoCitySites[CO_CITY_PLACER].rva, list) != Min(kCapacities[i], placer) || list[0] != 7 || list[1] != kCapacities[i];
    }
    return wrong;
}

/* The list as 0xFA8FE0 makes it: the allocation's size, the capacity in the record, the clear's size. */
static int List(const uint8_t* image, uint32_t places)
{
    static uint8_t record[0x300];
    memset(record, 0xEE, sizeof record);
    const uint32_t size = RunAllocation((uintptr_t)image + kCoCitySites[CO_CITY_ALLOCATION].rva, record);
    const uint32_t capacity = U32(record + CO_CITY_RECORD_PLACES);
    const uint32_t clear = RunClear((uintptr_t)image + kCoCitySites[CO_CITY_CLEAR].rva, record);
    record[CO_CITY_RECORD_PLACES] = 0xEE; record[CO_CITY_RECORD_PLACES + 1] = 0xEE; record[CO_CITY_RECORD_PLACES + 2] = 0xEE; record[CO_CITY_RECORD_PLACES + 3] = 0xEE;
    for (size_t i = 0; i < sizeof record; i++) if (record[i] != 0xEE) return 0; /* nothing else of the record written */
    return size == places * 0x60u && capacity == places && clear == size;
}

/* The game behind each stub: back to the test. Set once the sites are checked, over bytes the check pins. */
static void Returns(uint8_t* image) { for (int i = 0; i < CO_CITY_SITES; i++) if (i != CO_CITY_CAPACITY) image[kCoCitySites[i].back] = 0xC3; }

int main(void)
{
    uint8_t* image = Reset("HellForge's level 11");
    uintptr_t game = (uintptr_t)image;
    /* HellForge's list of 0x2000 footprints of 0x60 bytes, the prototype's 0x8000; 8192 locations, and 4096 short of 16384 */
    CHECK(CO_CITY_PLACES_STOCK == 0x2000u && CO_CITY_PLACES_OWN == 0x8000u && CO_CITY_FOOTPRINT == 0x60u);
    CHECK(CO_CITY_LOCATIONS_STOCK == 8192u && CO_CITY_LOCATIONS_OWN == 12288u);
    CHECK(U32(kCoCitySites[CO_CITY_ALLOCATION].bytes + 1) == 0xC0000u && U32(kCoCitySites[CO_CITY_CAPACITY].bytes + 6) == 0x2000u && U32(kCoCitySites[CO_CITY_CLEAR].bytes + 2) == 0xC0000u);
    CHECK(co_city_check(game) == NULL && g_fake.writes == 0);
    CHECK(co_city_cell(CO_CITY_CELL_PLACES) == 0x2000u && co_city_cell(CO_CITY_CELL_OP) == 8192u && co_city_cell(CO_CITY_CELL_PLACER) == CO_CITY_NO_LIMIT);
    co_city_level(1); /* nothing before the install */
    CHECK(co_city_cell(CO_CITY_CELL_PLACES) == 0x2000u && Unchanged());
    /* the mod's sites stand clear of HellForge's counted branch (0xF994AE) and its watch of the list's check (0xFAAECE) */
    CHECK(kCoCitySites[CO_CITY_PLACER].back <= 0xF994AEu && kCoCitySites[CO_CITY_PLACER].back + sizeof kCoCityPlacerBehind == 0xF994AEu);
    for (int i = 0; i < CO_CITY_SITES; i++) CHECK(kCoCitySites[i].back == kCoCitySites[i].rva + kCoCitySites[i].length && (kCoCitySites[i].back <= 0xFAAECEu || kCoCitySites[i].rva >= 0xFAAED7u));

    for (int i = 0; i < CO_CITY_SITES; i++)
    {
        const CoCitySite* s = &kCoCitySites[i];
        image = Reset("a site is another's");
        image[s->rva + s->length - 1u] ^= 0x10;
        CHECK(co_city_check((uintptr_t)image) != NULL && co_city_install((uintptr_t)image) != NULL && g_fake.writes == 0 && co_city_test_page() == NULL);
    }
    {
        /* the game's own list of 0x800: a game without HellForge's larger maps */
        const uint32_t size = 0x30000u, places = 0x800u;
        image = Reset("the game's own footprint list");
        memcpy(image + kCoCitySites[CO_CITY_ALLOCATION].rva + 1, &size, 4); memcpy(image + kCoCitySites[CO_CITY_CAPACITY].rva + 6, &places, 4);
        memcpy(image + kCoCitySites[CO_CITY_CLEAR].rva + 2, &size, 4);
        const char* why = co_city_install((uintptr_t)image);
        CHECK(why != NULL && strstr(why, "difficulty 11") != NULL && g_fake.writes == 0);
        image = Reset("the instruction behind a read is another");
        image[kCoCitySites[CO_CITY_OP_LIMIT].back + 2u] ^= 4;
        CHECK(co_city_check((uintptr_t)image) != NULL);
        image = Reset("the placer's compare is another");
        image[kCoCitySites[CO_CITY_PLACER].back + 1u] = 0x3B;
        CHECK(co_city_check((uintptr_t)image) != NULL);
    }

    image = Reset("the install");
    game = (uintptr_t)image;
    CHECK(co_city_install(game) == NULL && g_fake.writes == CO_CITY_SITES && co_city_test_page() != NULL);
    const uint8_t* page = co_city_test_page();
    const uintptr_t cells = (uintptr_t)page + 0x1000u;
    for (int i = 0; i < CO_CITY_SITES; i++)
    {
        const CoCitySite* s = &kCoCitySites[i];
        const uint8_t* stub = page + CO_CITY_SLOT * (unsigned)i;
        if (i == CO_CITY_CAPACITY)
        {
            /* the capacity's store is one nop of its length: the allocation's stub stores it */
            static const uint8_t nop[10] = {0x66, 0x2E, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
            CHECK(s->length == 10 && memcmp(image + s->rva, nop, 10) == 0 && stub[0] == 0xCC);
            continue;
        }
        CHECK(image[s->rva] == 0xE9 && Leads(image + s->rva + 1u) == (uintptr_t)stub);
        for (unsigned k = 5; k < s->length; k++) CHECK(image[s->rva + k] == 0x90);
        uint8_t built[CO_CITY_SLOT];
        const size_t size = co_city_stub(built, (uintptr_t)stub, game, i, cells);
        CHECK(size != 0 && size <= CO_CITY_SLOT && memcmp(built, stub, size) == 0 && stub[size - 5u] == 0xE9 && Leads(stub + size - 4u) == game + s->back);
    }
    /* each cell is read by one instruction of one stub */
    CHECK(Leads(page + CO_CITY_SLOT * CO_CITY_ALLOCATION + 2u) == cells + 4u * CO_CITY_CELL_PLACES);
    CHECK(Leads(page + CO_CITY_SLOT * CO_CITY_OP_LIMIT + 2u) == cells + 4u * CO_CITY_CELL_OP && Leads(page + CO_CITY_SLOT * CO_CITY_PLACER + 2u) == cells + 4u * CO_CITY_CELL_PLACER);
    /* nothing but the five sites is written: HellForge's branch behind the placer's compare and the rest of the span */
    {
        static uint8_t now[SPAN_SIZE];
        memcpy(now, image + SPAN, SPAN_SIZE);
        for (int i = 0; i < CO_CITY_SITES; i++) memcpy(now + (kCoCitySites[i].rva - SPAN), kCoCitySites[i].bytes, kCoCitySites[i].length);
        CHECK(memcmp(now, g_before, SPAN_SIZE) == 0);
    }
    CHECK(co_city_install(game) == NULL && g_fake.writes == CO_CITY_SITES); /* once */
    CHECK(co_city_stub(image, 0, game, CO_CITY_CAPACITY, cells) == 0 && co_city_stub(image, 0, game, CO_CITY_SITES, cells) == 0);

    Returns(image);
    g_case = "a normal level";
    /* HellForge's list to the byte, and for the whole op the 8192 locations HellForge alone has: with the table's
     * 65024 in the record the placer and the buildings' loop stop where stock's do */
    CHECK(List(image, 0x2000u) && Wrong(image, 8192u, CO_CITY_NO_LIMIT) == 0);
    co_city_level(0);
    CHECK(List(image, 0x2000u) && Wrong(image, 8192u, CO_CITY_NO_LIMIT) == 0);
    {
        static uint8_t record[0x300];
        const uint32_t table = 65024u;
        memcpy(record + CO_CITY_RECORD_LOCATIONS, &table, 4);
        uint32_t list[2] = {8192u, RunOp(game + kCoCitySites[CO_CITY_OP_LIMIT].rva, record)};
        CHECK(list[1] == 8192u && RunPlacer(game + kCoCitySites[CO_CITY_PLACER].rva, list) == 8192u);
    }
    g_case = "a complex level";
    co_city_level(1);
    /* 0x8000 places (0x300000 bytes); the op keeps the table's capacity and the placer stops at 12288 */
    CHECK(co_city_cell(CO_CITY_CELL_PLACES) == 0x8000u && List(image, 0x8000u) && Wrong(image, CO_CITY_NO_LIMIT, 12288u) == 0);
    {
        static uint8_t record[0x300];
        const uint32_t table = 65024u;
        memcpy(record + CO_CITY_RECORD_LOCATIONS, &table, 4);
        uint32_t list[2] = {12288u, RunOp(game + kCoCitySites[CO_CITY_OP_LIMIT].rva, record)};
        CHECK(list[1] == 65024u && RunPlacer(game + kCoCitySites[CO_CITY_PLACER].rva, list) == 12288u);
    }
    g_case = "a normal level after a complex one";
    co_city_level(0);
    CHECK(List(image, 0x2000u) && Wrong(image, 8192u, CO_CITY_NO_LIMIT) == 0);

    for (int fail = 0; fail < CO_CITY_SITES; fail++)
    {
        image = Reset("a site that cannot be written");
        g_fake.failWriteAt = fail;
        CHECK(co_city_install((uintptr_t)image) != NULL && co_city_test_page() == NULL && Unchanged());
        co_city_level(1);
        CHECK(co_city_cell(CO_CITY_CELL_PLACES) == 0x2000u);
    }

    if (g_failed) { printf("city: %d checks failed\n", g_failed); return 1; }
    printf("city: all cases passed\n");
    return 0;
}
