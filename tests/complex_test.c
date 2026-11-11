/* The complex part's decisions: each missing need turns it off with its own reason, a machine never makes a complex
 * mission it cannot or was not told to, and a mission entry's seed carries the roll at level 11 only. */
#include <stdio.h>
#include <string.h>

#include "co.h"

static int g_failed;
static const char* g_case;

#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static const CoComplexNeeds kAll = {1, 1, 0, 1, 1, 1, 1, 1, CO_COMPLEX_COMMIT_MB};

int main(void)
{
    g_case = "every need";
    CHECK(co_complex_refusal(&kAll) == NULL);
    const char* reasons[9];
    for (int i = 0; i < 9; i++)
    {
        CoComplexNeeds n = kAll;
        int* flags[8] = {&n.capacity_call, &n.regions, &n.live_load, &n.capacity_granted, &n.level_11, &n.moved, &n.stock_stubs, &n.own_sites};
        if (i < 8) *flags[i] = !*flags[i];
        else n.commit_free_mb = CO_COMPLEX_COMMIT_MB - 1u;
        reasons[i] = co_complex_refusal(&n);
        CHECK(reasons[i] != NULL);
        for (int j = 0; j < i && reasons[i] != NULL && reasons[j] != NULL; j++) CHECK(strcmp(reasons[i], reasons[j]) != 0);
    }

    /* Seeds with the mark and without. */
    const uint32_t marked = co_seed_marked(0x0184CCF6u, 1), plain = co_seed_marked(0x0184CCF6u, 0);

    g_case = "the host";
    CHECK(co_complex_decision(1, 1, CO_HOST_ROW_NONE, marked) == CO_MISSION_COMPLEX);
    CHECK(co_complex_decision(1, 1, CO_HOST_ROW_NONE, plain) == CO_MISSION_NORMAL);
    CHECK(co_complex_decision(1, 0, CO_HOST_ROW_NONE, marked) == CO_MISSION_NORMAL);
    CHECK(co_complex_decision(1, 0, CO_HOST_ROW_NONE, plain) == CO_MISSION_NORMAL);

    g_case = "a client";
    for (int own = 0; own < 2; own++)
    {
        /* No answer from the host: never a guess. */
        CHECK(co_complex_decision(0, own, CO_HOST_ROW_NONE, marked) == CO_MISSION_LEAVE);
        CHECK(co_complex_decision(0, own, CO_HOST_ROW_NONE, plain) == CO_MISSION_LEAVE);
        /* The host makes none: the bit means nothing. */
        CHECK(co_complex_decision(0, own, CO_HOST_ROW_OFF, marked) == CO_MISSION_NORMAL);
        CHECK(co_complex_decision(0, own, CO_HOST_ROW_OFF, plain) == CO_MISSION_NORMAL);
    }
    CHECK(co_complex_decision(0, 1, CO_HOST_ROW_ON, marked) == CO_MISSION_COMPLEX);
    CHECK(co_complex_decision(0, 1, CO_HOST_ROW_ON, plain) == CO_MISSION_NORMAL);
    CHECK(co_complex_decision(0, 0, CO_HOST_ROW_ON, marked) == CO_MISSION_LEAVE);
    CHECK(co_complex_decision(0, 0, CO_HOST_ROW_ON, plain) == CO_MISSION_LEAVE);

    /* No pair of host and client ends in different worlds: whenever the client stays, it makes what the host makes. */
    g_case = "host and client agree";
    for (int host_on = 0; host_on < 2; host_on++)
        for (int own = 0; own < 2; own++)
            for (int bit = 0; bit < 2; bit++)
            {
                /* The host clears the bit when its complex part is off. */
                uint32_t seed = co_seed_marked(0xDEADBEEFu, host_on && bit);
                int host = co_complex_decision(1, host_on, CO_HOST_ROW_NONE, seed);
                int client = co_complex_decision(0, own, host_on ? CO_HOST_ROW_ON : CO_HOST_ROW_OFF, seed);
                CHECK(client == CO_MISSION_LEAVE || client == host);
                CHECK((client == CO_MISSION_LEAVE) == !co_complex_stays(own, host_on ? CO_HOST_ROW_ON : CO_HOST_ROW_OFF));
            }

    g_case = "staying";
    CHECK(co_complex_stays(1, CO_HOST_ROW_ON) && co_complex_stays(1, CO_HOST_ROW_OFF) && co_complex_stays(1, CO_HOST_ROW_NONE));
    CHECK(!co_complex_stays(0, CO_HOST_ROW_ON) && co_complex_stays(0, CO_HOST_ROW_OFF) && co_complex_stays(0, CO_HOST_ROW_NONE));

    g_case = "a mission entry's seed";
    for (uint32_t level = 0; level <= 12; level++)
    {
        uint32_t seed = 0x12345679u + level;
        uint32_t got = co_complex_entry_seed(seed, level, 0xDEADBEEFu, 114u, 1);
        if (level != CO_COMPLEX_LEVEL) CHECK(got == seed);
        else CHECK(got == co_seed_marked(seed, co_complex_roll(0xDEADBEEFu, 114u, CO_COMPLEX_CHANCE)));
        if (level == CO_COMPLEX_LEVEL) CHECK(!co_seed_is_complex(co_complex_entry_seed(seed, level, 0xDEADBEEFu, 114u, 0)));
    }
    /* 0xDEADBEEF on planet 114 rolls complex at this chance (roll_test's vector 0x14FF), seed 0 on planet 0 too, and
     * 0x00000001 on planet 0 (0x3C84) does too; 0x12345678 on planet 7 (0xB4BF) does not. */
    CHECK(co_complex_entry_seed(0x10u, 11u, 0xDEADBEEFu, 114u, 1) == 0x11u);
    CHECK(co_complex_entry_seed(0x11u, 11u, 0x12345678u, 7u, 1) == 0x10u);

    if (g_failed) return 1;
    printf("complex: all cases passed\n");
    return 0;
}
