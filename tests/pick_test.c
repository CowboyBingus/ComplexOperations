/* Each planet's guaranteed complex operation: at least one per planet with a level-11 operation (exactly one when the
 * others' chance is 0), the same pick in any record order, kept when another slot goes, comes with a higher key or
 * draws a new seed (the slots after a committed operation do), a new pick when the guaranteed one goes, none on a
 * planet without level-11 operations. The others keep their own roll. */
#include <stdio.h>
#include <string.h>

#include "co.h"

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

/* {board seed, planet, slot, key}, by an independent computation of the formula in src/co_roll.c. */
static const struct { uint32_t board, planet, slot, key; } kKeys[] = {
    {0x00000000u, 0u, 0u, 0x1F1F5D4Bu}, {0x00000000u, 0u, 1u, 0x1D9D3E0Bu}, {0xBD8D3DA4u, 114u, 30u, 0x2907505Du},
    {0xBD8D3DA4u, 114u, 31u, 0xB83A1E20u}, {0xBD8D3DA4u, 114u, 32u, 0x83E31B65u}, {0xFFFFFFFFu, 65535u, 109u, 0xB586C915u},
    {0x12345678u, 269u, 100u, 0xAC849A67u}};

static uint32_t Next(uint64_t* s)
{
    *s = *s * 0x5851F42D4C957F2Dull + 0x14057B7EF767814Full;
    return (uint32_t)(*s >> 32);
}

/* `n` operations in distinct slots of the board, with seeds from the game's generator. */
static void Board(uint64_t* s, CoOperation* ops, uint32_t n)
{
    uint8_t used[CO_BOARD_SLOTS] = {0};
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t slot;
        do slot = Next(s) % CO_BOARD_SLOTS; while (used[slot]);
        used[slot] = 1;
        ops[i].slot = slot;
        ops[i].seed = Next(s);
    }
}

static uint32_t Count(const uint8_t* complex, uint32_t n)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; i++) c += complex[i];
    return c;
}

/* The answer for `slot` in a decided board, or 2 when the slot is not on it. */
static int Answer(const CoOperation* ops, const uint8_t* complex, uint32_t n, uint32_t slot)
{
    for (uint32_t i = 0; i < n; i++) if (ops[i].slot == slot) return complex[i];
    return 2;
}

int main(void)
{
    static const uint32_t kChances[3] = {0u, CO_COMPLEX_CHANCE, 65536u};
    CoOperation ops[16], other[16];
    uint8_t complex[16], again[16];
    uint64_t s = 0x7A11u;

    g_case = "fixed keys";
    for (size_t i = 0; i < sizeof kKeys / sizeof kKeys[0]; i++) CHECK(co_pick_key(kKeys[i].board, kKeys[i].planet, kKeys[i].slot) == kKeys[i].key);

    g_case = "no level-11 operation, no complex one";
    memset(complex, 0xAA, sizeof complex);
    CHECK(co_planet_decide(ops, 0, 0xBD8D3DA4u, 114u, 65536u, complex) == 0 && complex[0] == 0xAA);

    for (uint32_t trial = 0; trial < 3000u; trial++)
    {
        const uint32_t n = 1u + trial % 12u, planet = Next(&s) % 400u, board = Next(&s), chance = kChances[trial % 3u];
        Board(&s, ops, n);
        const uint32_t pick = co_planet_decide(ops, n, board, planet, chance, complex);

        g_case = "at least one per planet: the guaranteed one, then each other's own roll";
        CHECK(pick < n && complex[pick] == 1);
        for (uint32_t i = 0; i < n; i++)
        {
            if (i != pick) CHECK(complex[i] == (uint8_t)co_complex_roll(ops[i].seed, planet, chance));
            if (i != pick) CHECK(co_pick_key(board, planet, ops[i].slot) > co_pick_key(board, planet, ops[pick].slot));
        }
        if (chance == 0) CHECK(Count(complex, n) == 1);
        if (chance == 65536u) CHECK(Count(complex, n) == n);

        g_case = "the same pick in any record order";
        memcpy(other, ops, n * sizeof *ops);
        for (uint32_t i = n; i > 1; i--)
        {
            const uint32_t j = Next(&s) % i;
            const CoOperation t = other[i - 1]; other[i - 1] = other[j]; other[j] = t;
        }
        const uint32_t shuffled = co_planet_decide(other, n, board, planet, chance, again);
        CHECK(other[shuffled].slot == ops[pick].slot);
        for (uint32_t i = 0; i < n; i++) CHECK(Answer(other, again, n, ops[i].slot) == complex[i]);

        g_case = "kept when another operation goes";
        for (uint32_t gone = 0; gone < n; gone++)
        {
            if (gone == pick) continue;
            uint32_t m = 0;
            for (uint32_t i = 0; i < n; i++) if (i != gone) other[m++] = ops[i];
            const uint32_t kept = co_planet_decide(other, m, board, planet, chance, again);
            CHECK(other[kept].slot == ops[pick].slot);
            for (uint32_t i = 0; i < m; i++) CHECK(again[i] == Answer(ops, complex, n, other[i].slot));
        }

        g_case = "kept when an operation with a higher key comes; taken by one with a lower key";
        uint32_t slot = 0;
        while (Answer(ops, complex, n, slot) != 2) slot++;
        memcpy(other, ops, n * sizeof *ops);
        other[n].slot = slot;
        other[n].seed = Next(&s);
        const uint32_t added = co_planet_decide(other, n + 1, board, planet, chance, again);
        const int higher = co_pick_key(board, planet, slot) > co_pick_key(board, planet, ops[pick].slot);
        CHECK(other[added].slot == (higher ? ops[pick].slot : slot));
        for (uint32_t i = 0; i < n; i++) if (higher || i != pick) CHECK(again[i] == complex[i]);

        g_case = "kept when the other slots draw new seeds (an operation committed before them)";
        memcpy(other, ops, n * sizeof *ops);
        for (uint32_t i = 0; i < n; i++) if (i != pick) other[i].seed = Next(&s);
        CHECK(co_planet_decide(other, n, board, planet, chance, again) == pick && again[pick] == 1);
        const uint32_t played = Next(&s) % n; /* the committed operation keeps its seed and its answer */
        memcpy(other, ops, n * sizeof *ops);
        for (uint32_t i = 0; i < n; i++) if (i != played) other[i].seed = Next(&s);
        co_planet_decide(other, n, board, planet, chance, again);
        CHECK(again[played] == complex[played]);

        g_case = "a new pick when the guaranteed one goes";
        if (n > 1)
        {
            uint32_t m = 0, lowest = 0;
            for (uint32_t i = 0; i < n; i++) if (i != pick) other[m++] = ops[i];
            for (uint32_t i = 1; i < m; i++)
                if (co_pick_key(board, planet, other[i].slot) < co_pick_key(board, planet, other[lowest].slot)) lowest = i;
            const uint32_t next = co_planet_decide(other, m, board, planet, chance, again);
            CHECK(next == lowest && again[next] == 1 && Count(again, m) >= 1);
            if (chance == 0) CHECK(Count(again, m) == 1);
        }
        else CHECK(co_planet_decide(ops, 0, board, planet, chance, again) == 0);
    }

    /* Over many boards each of level 11's three campaign slots (30..32) is the guaranteed one about as often. */
    g_case = "the board seed spreads the pick over the slots";
    unsigned hits[3] = {0, 0, 0};
    const CoOperation three[3] = {{30, 1}, {31, 2}, {32, 3}};
    for (uint32_t i = 0; i < 30000u; i++) hits[co_planet_decide(three, 3, Next(&s), 269u, 0u, complex)]++;
    for (int i = 0; i < 3; i++) CHECK(hits[i] > 9400u && hits[i] < 10600u);

    if (g_failed) { printf("pick: %d checks failed\n", g_failed); return 1; }
    printf("pick: all cases passed\n");
    return 0;
}
