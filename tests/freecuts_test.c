/* Memory given back once nothing reads it: op 0x2B's job regions after the op returns. Each goes once, never twice,
 * and not while a reader may still be there. Run on the fake game. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

void CoXlTestWorldCurrent(float current);
uint64_t CoXlTestPlaceJob(uint64_t job, uint64_t (*original)(uint64_t, uint64_t, uint64_t, uint64_t));
int CoXlTestPlaceRegions(void);
void CoXlTestPlaceDone(void);
void CoXlTestPlaceFreeTry(void);
void CoXlTestPlaceJobsIn(LONG delta);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)


static uint64_t TestJob(uint64_t a, uint64_t b, uint64_t c, uint64_t job) { (void)a; (void)b; (void)c; (void)job; return 1; }

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;

    g_case = "op 0x2B's job regions";
    {
        uint32_t items = 800;
        uint64_t list = (uint64_t)(uintptr_t)&items;
        uint8_t context[0x200] = {0}, job[0x80] = {0};
        memcpy(context + 0x170, &list, 8);
        const uint64_t ctx = (uint64_t)(uintptr_t)context;
        memcpy(job + 0x10, &ctx, 8);
        CoXlTestWorldCurrent(8.0f);
        for (uint32_t k = 0; k < 8; k++)
        {
            uint32_t range[2] = {k * 100, k * 100 + 100};
            memcpy(job + 0x4C, range, 8);
            CoXlTestPlaceJob((uint64_t)(uintptr_t)job, TestJob);
        }
        CHECK(CoXlTestPlaceRegions() == 8);
        CoXlTestPlaceFreeTry(); /* no op has ended: they stay */
        CHECK(CoXlTestPlaceRegions() == 8);
        CoXlTestPlaceJobsIn(1); /* a job's note still reads its region */
        CoXlTestPlaceDone();
        CHECK(CoXlTestPlaceRegions() == 8);
        CoXlTestPlaceJobsIn(-1);
        CoXlTestPlaceFreeTry(); /* the next dispatcher call */
        CHECK(CoXlTestPlaceRegions() == 0);
        CHECK(fake_host_logged("difficulty: extra large maps: op 0x2B's 8 job regions given back after its merge (1024 MiB committed)"));
        CoXlTestPlaceFreeTry();
        CoXlTestPlaceDone(); /* once only */
        CHECK(CoXlTestPlaceRegions() == 0);
        uint32_t range[2] = {0, 100}; /* a later 0x2B makes its region again */
        memcpy(job + 0x4C, range, 8);
        CoXlTestPlaceJob((uint64_t)(uintptr_t)job, TestJob);
        CHECK(CoXlTestPlaceRegions() == 1);
        CoXlTestPlaceDone();
        CHECK(CoXlTestPlaceRegions() == 0);
        CoXlTestWorldCurrent(1.0f);
    }

    if (g_failed) { printf("freecuts: %d check(s) failed\n", g_failed); return 1; }
    printf("freecuts: all checks passed\n");
    return 0;
}
