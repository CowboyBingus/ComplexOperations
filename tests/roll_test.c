/* The complex operation roll: fixed answers every build must keep (a peer with another answer generates another
 * world), the chance's ends, and the share of operations it picks over 100,000 seeds. */
#include <stdio.h>

#include "co.h"

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* {seed, planet, the upper 16 bits of the hash}: complex from a chance one above them, by an independent computation
 * of the formula in src/co_roll.c. */
static const struct { uint32_t seed, planet, value; } kVectors[] = {
    {0x00000000u, 0u, 0x0E6Fu}, {0x00000001u, 0u, 0x3C84u}, {0x00000000u, 1u, 0xC074u}, {0x12345678u, 7u, 0xB4BFu},
    {0xFFFFFFFFu, 65535u, 0x9ED4u}, {0xDEADBEEFu, 114u, 0x14FFu}, {0x0184CCF6u, 261u, 0x67C6u}, {0x80000000u, 1u, 0x7F8Bu}};

/* The seeds a board gets: the game's generator, its state's upper half. */
static uint32_t BoardSeed(uint64_t* state)
{
    *state = *state * 0x5851F42D4C957F2Dull + 0x14057B7EF767814Full;
    return (uint32_t)(*state >> 32);
}

/* Hits in 100,000 seeds, in 1/1000 of a percent. */
static unsigned Share(uint32_t planet, uint32_t chance, int board, uint32_t first)
{
    unsigned hits = 0;
    uint64_t state = first;
    for (uint32_t i = 0; i < 100000u; i++) hits += (unsigned)co_complex_roll(board ? BoardSeed(&state) : first + i, planet, chance);
    return hits;
}

int main(void)
{
    g_case = "fixed vectors";
    for (size_t i = 0; i < sizeof kVectors / sizeof kVectors[0]; i++)
    {
        CHECK(co_complex_roll(kVectors[i].seed, kVectors[i].planet, kVectors[i].value) == 0);
        CHECK(co_complex_roll(kVectors[i].seed, kVectors[i].planet, kVectors[i].value + 1u) == 1);
    }

    /* The mark: read back as written, the other 31 bits kept, the same seed from marking twice, and fixed values. */
    g_case = "the seed's mark";
    for (uint32_t i = 0; i < 100000u; i++)
    {
        uint32_t seed = i * 0x9E3779B1u + 0x7F4A7C15u;
        for (int complex = 0; complex < 2; complex++)
        {
            uint32_t marked = co_seed_marked(seed, complex);
            CHECK(co_seed_is_complex(marked) == complex);
            CHECK(((marked ^ seed) & ~1u) == 0);
            CHECK(co_seed_marked(marked, complex) == marked);
        }
        if (g_failed) break;
    }
    CHECK(co_seed_marked(0x0184CCF6u, 1) == 0x0184CCF7u && co_seed_marked(0x0184CCF7u, 0) == 0x0184CCF6u);
    CHECK(co_seed_marked(0xFFFFFFFFu, 0) == 0xFFFFFFFEu && co_seed_marked(0u, 1) == 1u);

    g_case = "the chance's ends";
    for (uint32_t seed = 0; seed < 100000u; seed++)
    {
        CHECK(co_complex_roll(seed, 5u, 0u) == 0);
        CHECK(co_complex_roll(seed, 5u, 65536u) == 1);
        CHECK(co_complex_roll(seed, 5u, 0xFFFFFFFFu) == 1);
        if (g_failed) break;
    }

    g_case = "the same answer each time";
    CHECK(co_complex_roll(0xDEADBEEFu, 114u, 16384u) == co_complex_roll(0xDEADBEEFu, 114u, 16384u));
    CHECK(co_complex_roll(0xDEADBEEFu, 114u, 16384u) == 1);

    /* 5 %, 25 % and 50 %, each within one percentage point: counting seeds, seeds from a high start, and the game's own
     * generator, on four planets. */
    static const struct { uint32_t chance; unsigned percent; } kChances[] = {{3277u, 5}, {16384u, 25}, {32768u, 50}};
    static const uint32_t kPlanets[] = {0u, 7u, 114u, 261u};
    g_case = "the share of operations";
    for (size_t c = 0; c < sizeof kChances / sizeof kChances[0]; c++)
        for (size_t p = 0; p < sizeof kPlanets / sizeof kPlanets[0]; p++)
            for (int way = 0; way < 3; way++)
            {
                unsigned hits = Share(kPlanets[p], kChances[c].chance, way == 2, way == 1 ? 4000000000u : (uint32_t)p * 977u);
                unsigned want = kChances[c].percent * 1000u;
                if (hits + 1000u < want || hits > want + 1000u)
                {
                    printf("FAILED %s: chance %u on planet %u (way %d) hit %u of 100000\n", g_case, kChances[c].chance, kPlanets[p], way, hits);
                    g_failed++;
                }
            }

    printf("%s\n", g_failed ? "roll: failures" : "roll: all cases passed");
    return g_failed != 0;
}
