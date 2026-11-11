/* The status volumes' unit registry: a stub at each of the seven uses of the game's list, the list and the hash in the
 * mod's memory with the units the game's hold, all or none; refused where a site is another's. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "harness/fake_stock.h"
#include "xl/co_status.h"
#include "xl/co_status_sites.h"

void co_status_test_reset(void);
const uint8_t* co_status_test_page(void);
const uint64_t* co_status_test_list(void);
const uint64_t* co_status_test_hash(void);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
/* the registry of a game, at a free place of the image: its end is M + 0x13109C */
#define M_AT 0x3200000u
static uint8_t g_before[CO_STATUS_SITES][64];

static uint8_t* Reset(const char* name, int made)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_status_test_reset();
    uint8_t* image = g_fake.image;
    fake_stock_chains(image, 1022.0f / 1024.0f);
    if (made)
    {
        /* as 0x13F4B99..0x13F4BC5 set it up: the inline table, 0x1000 places, empty key 0, factor 2 */
        uint8_t* m = image + M_AT;
        *(uint64_t*)(image + CO_STATUS_MANAGER) = (uint64_t)(uintptr_t)m;
        *(uint64_t*)(m + CO_STATUS_POINTER) = (uint64_t)(uintptr_t)(m + CO_STATUS_INLINE);
        *(uint64_t*)(m + CO_STATUS_PLACES) = 0x1000u;
        *(uint32_t*)(m + CO_STATUS_FACTOR) = 2u;
    }
    for (int i = 0; i < CO_STATUS_SITES; i++) memcpy(g_before[i], image + kCoStatusSites[i].rva, 64);
    return image;
}
static int Unchanged(void)
{
    for (int i = 0; i < CO_STATUS_SITES; i++) if (memcmp(g_before[i], g_fake.image + kCoStatusSites[i].rva, 64) != 0) return 0;
    return 1;
}
static int InSite(uint32_t rva)
{
    for (int i = 0; i < CO_STATUS_SITES; i++) if (rva >= kCoStatusSites[i].rva && rva < kCoStatusSites[i].back) return 1;
    return 0;
}
static uint32_t U32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static uint64_t U64(const uint8_t* at) { uint64_t v; memcpy(&v, at, 8); return v; }
static uintptr_t Leads(const uint8_t* rel) { int32_t d; memcpy(&d, rel, 4); return (uintptr_t)rel + 4u + (uintptr_t)(intptr_t)d; }
static int Is(const uint8_t* at, const char* hex)
{
    for (; hex[0] != 0 && hex[1] != 0; hex += 2, at++)
    {
        unsigned v = 0;
        sscanf(hex, "%2x", &v);
        if (*at != v) return 0;
    }
    return 1;
}
/* The game's insert into the registry's hash, as 0xACF917..0xACF96C does it. */
static uint32_t* GameSlot(uint8_t* m, uint32_t id)
{
    uint64_t* table = *(uint64_t**)(m + CO_STATUS_POINTER);
    const uint32_t places = U32(m + CO_STATUS_PLACES), empty = U32(m + CO_STATUS_EMPTY), factor = U32(m + CO_STATUS_FACTOR);
    for (uint32_t probe = 0; probe < places; probe++)
    {
        uint32_t* slot = (uint32_t*)&table[(factor * id + probe) & (places - 1u)];
        if (slot[0] == empty || slot[0] == id) return slot;
    }
    return NULL;
}
static uint32_t Found(uint8_t* m, uint32_t id) { uint32_t* slot = GameSlot(m, id); return slot != NULL && slot[0] == id ? slot[1] : UINT32_MAX; }

int main(void)
{
    uint8_t* image = Reset("the game's registry, no unit yet", 1);
    uintptr_t game = (uintptr_t)image;
    uint8_t* m = image + M_AT;
    CHECK(CO_STATUS_SITES == 7 && CO_STATUS_COUNT - CO_STATUS_GAME_LIST == CO_STATUS_OWN * 8u && CO_STATUS_POINTER - CO_STATUS_INLINE == CO_STATUS_OWN * 8u);
    CHECK(CO_STATUS_LIST > CO_STATUS_OWN && CO_STATUS_LIST < 0x8000u && CO_STATUS_HASH >= 2u * CO_STATUS_LIST && (CO_STATUS_HASH & (CO_STATUS_HASH - 1u)) == 0);
    CHECK(co_status_check(game) == NULL && g_fake.writes == 0 && co_status_skips() == 0);
    CHECK(co_status_hash(game) == 0 && U32(m + CO_STATUS_PLACES) == 0x1000u); /* nothing before the install */
    CHECK(co_status_install(game) == NULL && g_fake.writes == CO_STATUS_SITES && g_fake.hookCount == 0);
    const uint8_t* page = co_status_test_page();
    const uint64_t* list = co_status_test_list();
    const uint64_t* hash = co_status_test_hash();
    CHECK(page != NULL && list != NULL && hash != NULL);
    if (page == NULL || list == NULL || hash == NULL) { printf("status: no install\n"); return 1; }
    const uint8_t* data = page + 0x1000;
    const uintptr_t places = (uintptr_t)data, skips = (uintptr_t)data + 4, at = (uintptr_t)data + 8;
    CHECK(U32(data) == CO_STATUS_LIST && U32(data + 4) == 0 && U64(data + 8) == (uint64_t)(uintptr_t)list);
    for (int i = 0; i < CO_STATUS_SITES; i++)
    {
        const CoStatusSite* s = &kCoStatusSites[i];
        CHECK(image[s->rva] == 0xE9 && Leads(image + s->rva + 1) == (uintptr_t)page + CO_STATUS_STUB * (unsigned)i && s->back == s->rva + s->length);
        for (unsigned k = 5; k < s->length; k++) CHECK(image[s->rva + k] == 0x90);
        for (unsigned k = s->length; k < 64u; k++) CHECK(InSite(s->rva + k) || image[s->rva + k] == g_before[i][k]); /* nothing behind the site */
    }
    const uint8_t* s = page;
    g_case = "the count's stub";
    CHECK(Is(s, "458BB898101300" "443B3D") && Leads(s + 10) == places);           /* the game's read; cmp r15d, [places] */
    CHECK(Is(s + 14, "7309" "418D4701" "E9") && Leads(s + 21) == game + 0xACF8FBu); /* room: the game's lea, and on */
    CHECK(Is(s + 25, "F0FF05") && Leads(s + 28) == skips && s[32] == 0xE9 && Leads(s + 33) == game + 0xACFA48u && s[37] == 0xCC);
    s = page + CO_STATUS_STUB; g_case = "the append's stub";
    CHECK(Is(s, "4C8B0D") && Leads(s + 3) == at && Is(s + 7, "438944F904" "43891CF9" "E9") && Leads(s + 17) == game + 0xACF917u && s[21] == 0xCC);
    s = page + 2 * CO_STATUS_STUB; g_case = "the slot's stub";
    CHECK(Is(s, "4D85C0" "740B" "418918" "488BCF" "E9") && Leads(s + 12) == game + 0xACF96Cu);
    CHECK(Is(s + 16, "48B8") && U64(s + 18) == (uint64_t)(game + CO_STATUS_MANAGER) && Is(s + 26, "488B00" "FF8898101300" "F0FF05") && Leads(s + 38) == skips);
    CHECK(s[42] == 0xE9 && Leads(s + 43) == game + 0xACFA48u && s[47] == 0xCC);
    s = page + 3 * CO_STATUS_STUB; g_case = "the hit path's stub";
    CHECK(Is(s, "488B0D") && Leads(s + 3) == at && Is(s + 7, "8B4CC104" "E9") && Leads(s + 12) == game + 0x69C062u && s[16] == 0xCC);
    s = page + 4 * CO_STATUS_STUB; g_case = "the removal's stub";
    CHECK(Is(s, "4C8B0D") && Leads(s + 3) == at && Is(s + 7, "4D8B14C1" "33D2" "4D8914F9" "E9") && Leads(s + 18) == game + 0x13CC581u && s[22] == 0xCC);
    s = page + 5 * CO_STATUS_STUB; g_case = "the other append's stub";
    CHECK(Is(s, "4889D1" "89DA" "4D89C8" "48B8") && U64(s + 10) == (uint64_t)(uintptr_t)co_status_append);
    CHECK(Is(s + 18, "FFD0" "85C0" "0F84") && Leads(s + 24) == game + 0x13CC4C2u && s[28] == 0xE9 && Leads(s + 29) == game + 0x13CC429u && s[33] == 0xCC);
    s = page + 6 * CO_STATUS_STUB; g_case = "the other read's stub";
    CHECK(Is(s, "4C8B1D") && Leads(s + 3) == at && Is(s + 7, "418B44C304" "E9") && Leads(s + 13) == game + 0x13CC047u && s[17] == 0xCC);
    {
        MEMORY_BASIC_INFORMATION code, room;
        VirtualQuery(page, &code, sizeof code); VirtualQuery(data, &room, sizeof room);
        CHECK(code.Protect == PAGE_EXECUTE_READ && room.Protect == PAGE_READWRITE);
    }

    g_case = "the hash";
    /* it went in with the install: the registry was there with the game's table */
    CHECK(U64(m + CO_STATUS_POINTER) == (uint64_t)(uintptr_t)hash && U32(m + CO_STATUS_PLACES) == CO_STATUS_HASH && U32(m + CO_STATUS_EMPTY) == 0);
    CHECK(co_status_hash(game) == 0); /* once */
    int writes = g_fake.writes;
    CHECK(co_status_install(game) == NULL && g_fake.writes == writes && co_status_test_page() == page); /* once */

    g_case = "the other append";
    {
        uint8_t unit[0x20] = {0};
        *(uint32_t*)(unit + 0x1C) = 77u;
        CHECK(co_status_append((uintptr_t)m, 500u, (uintptr_t)unit) == 1 && U32(m + CO_STATUS_COUNT) == 1u && list[0] == (500u | (uint64_t)77u << 32) && Found(m, 500u) == 0);
        *(uint32_t*)(unit + 0x1C) = 78u;
        CHECK(co_status_append((uintptr_t)m, 500u + CO_STATUS_HASH / 2u, (uintptr_t)unit) == 1 && list[1] >> 32 == 78u && Found(m, 500u + CO_STATUS_HASH / 2u) == 1); /* the same first slot */
        CHECK(U32(m + CO_STATUS_GAME_LIST) == 0 && U64(m + CO_STATUS_INLINE + 8u * 1000u) == 0); /* nothing in the game's own */
        *(uint32_t*)(m + CO_STATUS_COUNT) = CO_STATUS_LIST;
        CHECK(co_status_append((uintptr_t)m, 9u, (uintptr_t)unit) == 0 && U32(m + CO_STATUS_COUNT) == CO_STATUS_LIST && co_status_skips() == 1u && Found(m, 9u) == UINT32_MAX);
        /* a hash with no free slot: the game's own table of two places, both taken */
        static uint64_t two[2] = {11u, 13u};
        *(uint32_t*)(m + CO_STATUS_COUNT) = 2u;
        *(uint64_t*)(m + CO_STATUS_POINTER) = (uint64_t)(uintptr_t)two; *(uint32_t*)(m + CO_STATUS_PLACES) = 2u;
        CHECK(co_status_append((uintptr_t)m, 9u, (uintptr_t)unit) == 0 && U32(m + CO_STATUS_COUNT) == 2u && co_status_skips() == 2u && two[0] == 11u && two[1] == 13u);
        CHECK(co_status_append((uintptr_t)m, 13u, (uintptr_t)unit) == 1 && U32(m + CO_STATUS_COUNT) == 3u && two[1] == (13u | (uint64_t)2u << 32)); /* its own key's slot */
    }

    image = Reset("no registry yet", 0);
    game = (uintptr_t)image; m = image + M_AT;
    CHECK(co_status_install(game) == NULL && g_fake.writes == CO_STATUS_SITES && co_status_hash(game) == 0);
    hash = co_status_test_hash();
    /* the game makes it: the next tick moves the hash */
    *(uint64_t*)(image + CO_STATUS_MANAGER) = (uint64_t)(uintptr_t)m;
    *(uint64_t*)(m + CO_STATUS_POINTER) = (uint64_t)(uintptr_t)(m + CO_STATUS_INLINE);
    *(uint64_t*)(m + CO_STATUS_PLACES) = 0x1000u; *(uint32_t*)(m + CO_STATUS_FACTOR) = 2u;
    CHECK(co_status_hash(game) == 1 && U64(m + CO_STATUS_POINTER) == (uint64_t)(uintptr_t)hash && U32(m + CO_STATUS_PLACES) == CO_STATUS_HASH && co_status_hash(game) == 0);
    /* and sets it up anew: again */
    *(uint64_t*)(m + CO_STATUS_POINTER) = (uint64_t)(uintptr_t)(m + CO_STATUS_INLINE); *(uint64_t*)(m + CO_STATUS_PLACES) = 0x1000u;
    CHECK(co_status_hash(game) == 1 && U32(m + CO_STATUS_PLACES) == CO_STATUS_HASH);
    /* a table that is not the game's own stays */
    *(uint64_t*)(m + CO_STATUS_PLACES) = 0x2000u; *(uint64_t*)(m + CO_STATUS_POINTER) = (uint64_t)(uintptr_t)(m + CO_STATUS_INLINE);
    CHECK(co_status_hash(game) == 0 && U32(m + CO_STATUS_PLACES) == 0x2000u);

    image = Reset("units registered before the install", 1);
    game = (uintptr_t)image; m = image + M_AT;
    for (uint32_t i = 0; i < 3000u; i++)
    {
        const uint32_t id = 0x10000u + i * 0x801u; /* ids that share first slots */
        uint32_t* slot = GameSlot(m, id);
        *(uint32_t*)(m + CO_STATUS_GAME_LIST + 8u * i) = id; *(uint32_t*)(m + CO_STATUS_GAME_LIST + 8u * i + 4) = i % 7u;
        slot[0] = id; slot[1] = i;
    }
    *(uint32_t*)(m + CO_STATUS_COUNT) = 3000u;
    CHECK(co_status_install(game) == NULL);
    list = co_status_test_list();
    CHECK(list != NULL && memcmp(list, m + CO_STATUS_GAME_LIST, 3000u * 8u) == 0 && U32(m + CO_STATUS_COUNT) == 3000u && U32(m + CO_STATUS_PLACES) == CO_STATUS_HASH);
    {
        int found = 0;
        for (uint32_t i = 0; i < 3000u; i++) found += Found(m, 0x10000u + i * 0x801u) == i;
        CHECK(found == 3000 && Found(m, 5u) == UINT32_MAX);
    }
    image = Reset("a list past the game's own places", 1);
    *(uint32_t*)(image + M_AT + CO_STATUS_COUNT) = CO_STATUS_OWN + 1u;
    CHECK(co_status_install((uintptr_t)image) != NULL && Unchanged() && g_fake.writes == 0 && co_status_test_page() == NULL);

    for (int i = 0; i < CO_STATUS_SITES; i++)
        for (unsigned k = 0; k < kCoStatusSites[i].length; k += kCoStatusSites[i].length - 1u)
        {
            image = Reset("a site is not the game's", 1);
            image[kCoStatusSites[i].rva + k] ^= 0x04;
            for (int j = 0; j < CO_STATUS_SITES; j++) memcpy(g_before[j], image + kCoStatusSites[j].rva, 64);
            CHECK(co_status_check((uintptr_t)image) != NULL && co_status_install((uintptr_t)image) != NULL && Unchanged() && g_fake.writes == 0);
            CHECK(co_status_test_page() == NULL && U32(image + M_AT + CO_STATUS_PLACES) == 0x1000u && co_status_hash((uintptr_t)image) == 0);
        }
    for (int fail = 0; fail < CO_STATUS_SITES; fail++)
    {
        image = Reset("a write is refused", 1);
        g_fake.failWriteAt = fail;
        CHECK(co_status_install((uintptr_t)image) != NULL && Unchanged() && g_fake.writes == 2 * fail + 1 && co_status_test_page() == NULL);
        CHECK(U32(image + M_AT + CO_STATUS_PLACES) == 0x1000u && U64(image + M_AT + CO_STATUS_POINTER) == (uint64_t)(uintptr_t)(image + M_AT + CO_STATUS_INLINE));
        uint8_t unit[0x20] = {0};
        CHECK(co_status_append((uintptr_t)(image + M_AT), 5u, (uintptr_t)unit) == 0 && U32(image + M_AT + CO_STATUS_COUNT) == 0); /* no list: nothing appended */
        CHECK(co_status_check((uintptr_t)image) == NULL); /* and it can go in another time */
    }

    if (g_failed) { printf("status: %d checks failed\n", g_failed); return 1; }
    printf("status: all cases passed\n");
    return 0;
}
