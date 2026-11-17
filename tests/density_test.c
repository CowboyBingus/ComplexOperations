/* The density guards against a stand-in image: all sites or none, the stubs' jumps land where the review says, the
 * living world's units are counted by the keep rule, and the ray guard refuses only where a 0 is safe. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/fake_host.h"

void co_density_test_fill(uint8_t* image);
int co_density_test_state(void);
uintptr_t co_density_test_stub(int i);
uintptr_t co_density_test_data(void);
int co_density_test_count(uint32_t* units);
int co_density_test_refuse(uint64_t queue, uintptr_t back);
void co_density_test_tick(float dt);

static int g_failed;
static const char* g_case;
static uint8_t* g_before;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static void Start(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_density_test_fill(g_fake.image);
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
}

static uintptr_t Rel32(uintptr_t at) /* the target of a rel32 whose 4 bytes start at `at` */
{
    int32_t rel;
    memcpy(&rel, (void*)at, 4);
    return at + 4 + (intptr_t)rel;
}

static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static void Put64(uint8_t* at, uint64_t v) { memcpy(at, &v, 8); }

/* A manager at `at` with a hash at +0x20 of `capacity` buckets (identity multiplier, empty key 0x7FFF). */
static void Hash(uint8_t* image, uint32_t global, uint32_t at, uint32_t buckets, uint32_t capacity)
{
    Put64(image + global, (uintptr_t)image + at);
    Put64(image + at + 0x20, (uintptr_t)image + buckets);
    Put32(image + at + 0x28, capacity);
    Put32(image + at + 0x2C, 0x7FFFu);
    Put32(image + at + 0x30, 1u);
    for (uint32_t i = 0; i < capacity; i++) { Put32(image + buckets + 8 * i, 0x7FFFu); Put32(image + buckets + 8 * i + 4, 0xFFFFFFFFu); }
}

static void Entry(uint8_t* image, uint32_t buckets, uint32_t capacity, uint32_t key, uint32_t value)
{
    Put32(image + buckets + 8 * (key % capacity), key);
    Put32(image + buckets + 8 * (key % capacity) + 4, value);
}

static void Job(uint8_t* image, uint32_t records, uint32_t index, uint32_t job, uint8_t keep)
{
    Put32(image + records + index * 0x1C + 0xC, job);
    image[records + index * 0x1C + 0x18] = keep;
}

int main(void)
{
    g_before = malloc(FAKE_IMAGE_SIZE);
    if (g_before == NULL) return 2;
    static const uint8_t slot[] = {0xE9, 0x32, 0x07, 0x00, 0x00}, gate[] = {0x81, 0x78, 0x0C, 0x00, 0x04, 0x00, 0x00};

    Start("the reviewed game");
    co_density_install((uintptr_t)g_fake.image);
    uint8_t* g = g_fake.image;
    uintptr_t base = (uintptr_t)g, data = co_density_test_data();
    CHECK(co_density_test_state() == 1 && data != 0);
    CHECK(memcmp(g + 0x953942u, slot, 5) == 0 && Rel32(base + 0x953943u) == base + 0x954079u);
    CHECK(memcmp(g + 0x952F62u, gate, 7) == 0 && memcmp(g + 0x90EDEAu, gate, 7) == 0);
    /* Each pair-table site jumps to its stub; the stub refuses to the skip and returns past the replaced bytes. */
    uintptr_t s0 = co_density_test_stub(0), s1 = co_density_test_stub(1);
    CHECK(s0 != 0 && g[0xA2BE29u] == 0xE9 && Rel32(base + 0xA2BE2Au) == s0 && g[0xA2BE2Eu] == 0x90 && g[0xA2BE2Fu] == 0x90);
    CHECK(g[0xA2D6A9u] == 0xE9 && Rel32(base + 0xA2D6AAu) == s1 && g[0xA2D6AEu] == 0x90);
    CHECK(memcmp((void*)s0, "\x41\x8B\x85\x40\x16\x00\x00\x83\xF8\x40\x0F\x83", 12) == 0 && Rel32(s0 + 12) == base + 0xA2BE62u);
    CHECK(*(uint8_t*)(s0 + 16) == 0xE9 && Rel32(s0 + 17) == base + 0xA2BE30u);
    CHECK(memcmp((void*)s1, "\x8B\x83\x40\x16\x00\x00\x83\xF8\x40\x0F\x83", 11) == 0 && Rel32(s1 + 11) == base + 0xA2D6E0u);
    CHECK(*(uint8_t*)(s1 + 15) == 0xE9 && Rel32(s1 + 16) == base + 0xA2D6AFu);
    /* The gates: {site, bytes replaced, stub length, where the count's rel32 is}. */
    static const struct { uint32_t rva; uint8_t size, length, unitsAt; } gates[] = {
        {0x952F53u, 6, 16, 5}, {0x90EDDBu, 6, 16, 5}, {0x95577Fu, 6, 16, 5}, {0x8B88B2u, 5, 15, 5}, {0x572C51u, 9, 15, 11}, {0xA2C08Au, 6, 12, 8}};
    for (int i = 0; i < 6; i++)
    {
        uintptr_t s = co_density_test_stub(2 + i);
        CHECK(g[gates[i].rva] == 0xE9 && Rel32(base + gates[i].rva + 1) == s);
        for (int k = 5; k < gates[i].size; k++) CHECK(g[gates[i].rva + k] == 0x90);
        CHECK(Rel32(s + gates[i].unitsAt) == data);
        CHECK(*(uint8_t*)(s + gates[i].length) == 0xE9 && Rel32(s + gates[i].length + 1) == base + gates[i].rva + gates[i].size);
    }
    /* The 70 stubs clamp after the subtraction and compare with 70 last, for the jge / jb behind them. */
    CHECK(memcmp((void*)co_density_test_stub(2), "\x03\x48\x1C\x2B\x0D", 5) == 0 && memcmp((void*)(co_density_test_stub(2) + 9), "\x79\x02\x31\xC9\x83\xF9\x46", 7) == 0);
    CHECK(fake_host_logged("density: the breach group release counts natives only"));
    CHECK((fake_host_detour(0x175CA60u) != NULL) && (fake_host_detour(0x175CBD0u) != NULL) && g_fake.tick != NULL);
    CHECK(fake_host_logged("density: the ray queue refuses new rays above 3584 of 4096"));

    Start("counting the living world's units");
    co_density_install((uintptr_t)g_fake.image);
    g = g_fake.image;
    Hash(g, 0x33264D8u, 0x3000000u, 0x3001000u, 16);
    Put32(g + 0x3000000u + 0xC, 6);
    Put64(g + 0x3000000u + 0x48, (uintptr_t)g + 0x3002000u);
    Hash(g, 0x33265A0u, 0x3003000u, 0x3004000u, 16);
    Job(g, 0x3002000u, 0, 1, 1);  Entry(g, 0x3001000u, 16, 0x100, 0); /* a living world member */
    Job(g, 0x3002000u, 1, 10, 1); Entry(g, 0x3001000u, 16, 0x101, 1); /* a member on its own goal */
    Job(g, 0x3002000u, 2, 1, 1);  Entry(g, 0x3001000u, 16, 0x102, 2); /* a kept squad entity */
    Entry(g, 0x3004000u, 16, 0x102, 0);
    Job(g, 0x3002000u, 3, 1, 0);  Entry(g, 0x3001000u, 16, 0x103, 3); /* a vanilla member */
    Job(g, 0x3002000u, 4, 7, 1);  Entry(g, 0x3001000u, 16, 0x104, 4); /* a kept border traveller squad */
    Job(g, 0x3002000u, 5, 1, 1);  Entry(g, 0x3001000u, 16, 0x105, 9); /* an index past the active count */
    uint32_t units = 99;
    CHECK(co_density_test_count(&units) && units == 2);
    for (int i = 0; i < 6; i++) co_density_test_tick(0.016f);
    CHECK(*(volatile uint32_t*)co_density_test_data() == 2);
    Put64(g + 0x33265A0u, 0); /* an unreadable table: the gates fall back to vanilla's count */
    for (int i = 0; i < 6; i++) co_density_test_tick(0.016f);
    CHECK(*(volatile uint32_t*)co_density_test_data() == 0);

    Start("the ray guard");
    co_density_install((uintptr_t)g_fake.image);
    uint32_t queue = 3583;
    uintptr_t inside = (uintptr_t)g_fake.image + 0x7E5782u;
    CHECK(!co_density_test_refuse((uintptr_t)&queue, inside));
    queue = 3584;
    CHECK(co_density_test_refuse((uintptr_t)&queue, inside));
    CHECK(!co_density_test_refuse((uintptr_t)&queue, (uintptr_t)g_fake.image + 0x916F63u));
    CHECK(!co_density_test_refuse((uintptr_t)&queue, (uintptr_t)g_fake.image + 0x13C5DBCu));
    CHECK(!co_density_test_refuse((uintptr_t)&queue, (uintptr_t)g_fake.image + FAKE_IMAGE_SIZE));

    Start("a submit that is not the reviewed one");
    g_fake.noMatch[0] = 0x175CBD0u;
    co_density_install((uintptr_t)g_fake.image);
    CHECK(co_density_test_state() == 1 && !(fake_host_detour(0x175CA60u) != NULL));
    CHECK(fake_host_logged("the ray queue's submit is not the reviewed one; no ray guard"));

    Start("one changed site");
    g_fake.image[0x95577Fu] ^= 1;
    memcpy(g_before, g_fake.image, FAKE_IMAGE_SIZE);
    co_density_install((uintptr_t)g_fake.image);
    CHECK(co_density_test_state() == -1 && g_fake.writes == 0 && memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0);
    CHECK(fake_host_logged("game+0x95577F is not the reviewed one"));

    /* 3 patches, 2 pair-table jumps and 6 gate jumps; a failed write puts back every site written before it. */
    for (int at = 0; at < 11; at++)
    {
        Start("a write fails");
        g_fake.failWriteAt = at;
        co_density_install((uintptr_t)g_fake.image);
        CHECK(co_density_test_state() == -1 && memcmp(g_before, g_fake.image, FAKE_IMAGE_SIZE) == 0);
        CHECK(fake_host_logged("could not be written; the guards stay off") && g_fake.tick == NULL);
    }

    printf("%s\n", g_failed ? "density: failures" : "density: all cases passed");
    return g_failed != 0;
}
