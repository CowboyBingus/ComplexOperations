/* The density guards as switched sites: a complex mission runs each guard, any other level the game's instructions.
 * The gates, the squad slot and the pair table run as code on the fake game, both ways; the pair table is HellForge's
 * when its guard is there first. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_density.h"
#include "xl/co_switch.h"

void co_switch_test_reset(void);
void co_density_test_reset(void);
void co_density_test_fill(uint8_t* image);
int co_density_test_pairs(void);
uint32_t co_density_test_units(void);
void co_density_test_set_units(uint32_t units);
int co_density_test_refuse(uint64_t queue, uintptr_t back);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define RUN 0x3300000u
#define CAVE 0x3301000u

static uint8_t* Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_switch_test_reset();
    co_density_test_reset();
    co_density_test_fill(g_fake.image);
    /* the ray queue's two submits, as the reviewed build has them */
    static const uint8_t ray[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57};
    static const uint8_t ray2[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56};
    memcpy(g_fake.image + 0x175CA60u, ray, sizeof ray);
    memcpy(g_fake.image + 0x175CBD0u, ray2, sizeof ray2);
    g_fake.writes = 0;
    return g_fake.image;
}

static void Put(uint8_t* image, uint32_t at, const uint8_t* code, size_t size) { memcpy(image + at, code, size); }

/* Enters `site` with rcx and rdx as given (rdx also in rax, r13 and r8) and r13/rsi saved; where it goes on, `back`
 * and `other` return 1 or 2 in the high half and ecx in the low. */
static uint64_t Run(uint8_t* image, uint32_t site, uint32_t back, uint32_t other, uint64_t rcx, uint64_t rdx)
{
    /* push r13; push rsi; mov rsi, rcx; mov r13, rdx; mov rax, rdx; mov r8, rdx; jmp site */
    uint8_t enter[] = {0x41, 0x55, 0x56, 0x48, 0x89, 0xCE, 0x49, 0x89, 0xD5, 0x48, 0x89, 0xD0, 0x49, 0x89, 0xD0, 0xE9, 0, 0, 0, 0};
    const int32_t rel = (int32_t)(site - (RUN + sizeof enter));
    memcpy(enter + 16, &rel, 4);
    Put(image, RUN, enter, sizeof enter);
    /* mov eax, ecx; mov edx, n; shl rdx, 32; or rax, rdx; pop rsi; pop r13; ret */
    uint8_t leave[] = {0x89, 0xC8, 0xBA, 1, 0, 0, 0, 0x48, 0xC1, 0xE2, 0x20, 0x48, 0x09, 0xD0, 0x5E, 0x41, 0x5D, 0xC3};
    Put(image, back, leave, sizeof leave);
    if (other) { leave[3] = 2; Put(image, other, leave, sizeof leave); }
    DWORD old;
    if (!VirtualProtect(image, FAKE_IMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old)) return 0;
    return ((uint64_t (*)(uint64_t, uint64_t))(void*)(image + RUN))(rcx, rdx);
}

int main(void)
{
    uint8_t* image = Reset("the install");
    uint8_t before[16];
    memcpy(before, image + 0x952F53u, sizeof before);
    CHECK(co_density_install((uintptr_t)image) == NULL);
    CHECK(co_density_on() == 1);
    CHECK(image[0x952F53u] == 0xE9 && image[0x953939u] == 0xE9 && image[0x952F5Bu] == 0xE9 && image[0x572C51u] == 0xE9 && image[0xA2C08Au] == 0xE9);
    CHECK(image[0xA2BE29u] == 0x41); /* the pair table waits for the first complex level */
    CHECK(co_switch_path((uintptr_t)image + 0x952F53u) == 0);

    /* the 70 room: the game's sum, or natives only (units left out, clamped at 0) */
    static uint32_t ai[16];
    ai[7] = 30; /* +0x1C */
    /* behind the gate the AiJob site follows at once: a short jump to the harness's leave keeps it whole */
    static const uint8_t over[] = {0xEB, 0x45};
    Put(image, 0x952F59u, over, sizeof over);
    co_density_live(0);
    CHECK((uint32_t)Run(image, 0x952F53u, 0x952FA0u, 0, 50, (uint64_t)(uintptr_t)ai) == 80);
    CHECK(co_density_test_units() == 0);
    co_density_live(1);
    CHECK(co_switch_path((uintptr_t)image + 0x952F53u) == 1);
    CHECK(co_density_test_pairs() == 1 && image[0xA2BE29u] == 0xE9 && image[0xA2D6A9u] == 0xE9);
    CHECK((uint32_t)Run(image, 0x952F53u, 0x952FA0u, 0, 50, (uint64_t)(uintptr_t)ai) == 80);
    co_density_test_set_units(40); /* as the tick counts them */
    CHECK((uint32_t)Run(image, 0x952F53u, 0x952FA0u, 0, 50, (uint64_t)(uintptr_t)ai) == 40);
    co_density_test_set_units(100);
    CHECK((uint32_t)Run(image, 0x952F53u, 0x952FA0u, 0, 50, (uint64_t)(uintptr_t)ai) == 0);
    co_density_live(0); /* a normal level: no units, and the game's sum whatever was counted */
    CHECK(co_density_test_units() == 0);
    co_density_test_set_units(40);
    CHECK((uint32_t)Run(image, 0x952F53u, 0x952FA0u, 0, 50, (uint64_t)(uintptr_t)ai) == 80);
    co_density_test_set_units(0);
    co_density_live(1);

    /* the AiJob gate: 500 jobs ([game+0x33264D8]+0xC) pass the raised gate only on a complex level */
    ai[3] = 500;
    const uintptr_t jobs = (uintptr_t)ai;
    memcpy(image + 0x33264D8u, &jobs, 8);
    /* the code after the gate: setae al; movzx ecx, al; then the harness's leave */
    static const uint8_t setae[] = {0x0F, 0x93, 0xC0, 0x0F, 0xB6, 0xC8};
    Put(image, 0x952F69u, setae, sizeof setae);
    const uint64_t on = Run(image, 0x952F5Bu, 0x952F69u + sizeof setae, 0, 0, 0);
    co_density_live(0);
    const uint64_t off = Run(image, 0x952F5Bu, 0x952F69u + sizeof setae, 0, 0, 0);
    CHECK((uint32_t)on == 0 && (uint32_t)off == 1); /* the jae (refused) on the game's 448 only */

    /* the squad slot after all 512 (r8d 512): the game's path goes on with r13 = rsi, the guard takes the refusal
     * exit; below 512 both loop (0x953910) */
    co_density_live(0);
    CHECK(Run(image, 0x953939u, 0x953953u, 0x954079u, 7, 0x200) == (1ull << 32 | 7));
    CHECK(Run(image, 0x953939u, 0x953910u, 0x954079u, 7, 0x1FF) >> 32 == 1);
    co_density_live(1);
    CHECK(Run(image, 0x953939u, 0x953953u, 0x954079u, 7, 0x200) >> 32 == 2);
    CHECK(Run(image, 0x953939u, 0x953910u, 0x954079u, 7, 0x1FF) >> 32 == 1);

    /* the pair table (r13 the budget, count +0x1640): below 64 the append, at 64 the skip; the game's path appends */
    static uint8_t budget[0x1700];
    uint32_t count = 64;
    memcpy(budget + 0x1640, &count, 4);
    CHECK(Run(image, 0xA2BE29u, 0xA2BE30u, 0xA2BE62u, 0, (uint64_t)(uintptr_t)budget) >> 32 == 2);
    count = 10; memcpy(budget + 0x1640, &count, 4);
    CHECK(Run(image, 0xA2BE29u, 0xA2BE30u, 0xA2BE62u, 0, (uint64_t)(uintptr_t)budget) >> 32 == 1);
    co_density_live(0);
    count = 64; memcpy(budget + 0x1640, &count, 4);
    CHECK(Run(image, 0xA2BE29u, 0xA2BE30u, 0xA2BE62u, 0, (uint64_t)(uintptr_t)budget) >> 32 == 1);

    /* the ray queue: refused at its limit from game.dll's callers, on a complex level only */
    uint32_t queue = CO_DENSITY_RAY_LIMIT;
    CHECK(co_density_test_refuse((uint64_t)(uintptr_t)&queue, (uintptr_t)image + 0x100) == 0);
    co_density_live(1);
    CHECK(co_density_test_refuse((uint64_t)(uintptr_t)&queue, (uintptr_t)image + 0x100) == 1);
    CHECK(co_density_test_refuse((uint64_t)(uintptr_t)&queue, (uintptr_t)image + 0x916F63u) == 0);
    queue = CO_DENSITY_RAY_LIMIT - 1;
    CHECK(co_density_test_refuse((uint64_t)(uintptr_t)&queue, (uintptr_t)image + 0x100) == 0);

    /* Difficulty11's four signatures of the guards (pressure/natives.c) on a normal and on a complex level */
    image = Reset("Difficulty11's signatures");
    CHECK(co_density_install((uintptr_t)image) == NULL);
    for (int complex = 0; complex < 2; complex++)
    {
        co_density_live(complex);
        uint32_t gate; memcpy(&gate, image + 0x952F65u, 4);
        CHECK(image[0x952F53u] == 0xE9 && gate == 0x400u && memcmp(image + 0x953942u, "\xE9\x32\x07\x00\x00", 5) == 0);
        CHECK(complex ? image[0xA2BE29u] == 0xE9 : image[0xA2BE29u] == 0x41); /* the pair table from the first complex level on */
    }
    co_density_live(0);
    CHECK(image[0xA2BE29u] == 0xE9 && co_switch_path((uintptr_t)image + 0xA2BE29u) == 0);

    /* HellForge's guard of the pair table, there first: kept, and the density stays on */
    image = Reset("HellForge's pair guard");
    for (int i = 0; i < 2; i++)
    {
        static const uint8_t pins[2][7] = {{0x41, 0x8B, 0x85, 0x40, 0x16, 0x00, 0x00}, {0x8B, 0x83, 0x40, 0x16, 0x00, 0x00}};
        const uint32_t site = i ? 0xA2D6A9u : 0xA2BE29u, size = i ? 6 : 7, cave = CAVE + 0x40u * (uint32_t)i;
        static const uint8_t full[] = {0x83, 0xF8, 0x40, 0x73, 0x05};
        Put(image, cave, pins[i], size); Put(image, cave + size, full, sizeof full);
        uint8_t jump[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
        const int32_t rel = (int32_t)(cave - (site + 5));
        memcpy(jump + 1, &rel, 4);
        Put(image, site, jump, size);
    }
    CHECK(co_density_install((uintptr_t)image) == NULL);
    co_density_live(1);
    CHECK(co_density_test_pairs() == 2 && co_density_on() == 1);
    CHECK(co_switch_path((uintptr_t)image + 0x952F53u) == 1);

    /* neither the game's bytes nor HellForge's guard at the pair table: off, and every site the game's */
    image = Reset("an unknown pair table");
    CHECK(co_density_install((uintptr_t)image) == NULL);
    image[0xA2D6A9u] = 0xCC;
    co_density_live(1);
    CHECK(co_density_test_pairs() == -1 && co_density_on() == 0);
    CHECK(co_switch_path((uintptr_t)image + 0x952F53u) == 0);

    /* a site that is not the reviewed code: nothing written */
    image = Reset("a gate that is not the reviewed code");
    image[0x8B88B2u] = 0x90;
    g_fake.writes = 0;
    CHECK(co_density_install((uintptr_t)image) != NULL);
    CHECK(g_fake.writes == 0 && co_density_on() == 0 && image[0x952F53u] == 0x03 && image[0x953939u] == 0x41);

    /* a failed write: the sites written before it are the game's again */
    image = Reset("a failed write");
    g_fake.failWriteAt = 4;
    CHECK(co_density_install((uintptr_t)image) != NULL);
    CHECK(memcmp(image + 0x952F53u, before, 6) == 0 && image[0x953939u] == 0x41 && image[0x953942u] == 0x4C && co_density_on() == 0);

    if (g_failed) { printf("density: %d checks failed\n", g_failed); return 1; }
    printf("density: all cases passed\n");
    return 0;
}
