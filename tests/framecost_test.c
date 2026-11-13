/* Wall-clock frame times on a complex mission: the 10 s window's lows, slow frames and CPU load, on a test clock of
 * 1,000,000 ticks a second (1 tick = 1 us). */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_framecost.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define US 1000000

static int64_t g_now;
static int g_ai = -1; static float g_nest = -1;
static void Context(int* ai, float* nest) { *ai = g_ai; *nest = g_nest; }

static void Reset(const char* name)
{
    g_case = name;
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_framecost_test_reset(US);
    co_framecost_context(Context);
    g_ai = -1; g_nest = -1;
    g_now = 1000;
}

/* One tick at g_now, then the clock moves on by `ms`; `living` of it inside the living world's part. */
static void Frame(double ms, double living, int live)
{
    co_framecost_test_begin(g_now, live);
    co_framecost_test_mark(CO_FC_STREAM, g_now + 10);
    co_framecost_test_mark(CO_FC_LIVING, g_now + 10 + (int64_t)(living * 1000.0));
    co_framecost_test_mark(CO_FC_DENSITY, g_now + 20 + (int64_t)(living * 1000.0));
    g_now += (int64_t)(ms * 1000.0);
}

static void TestLows(void)
{
    Reset("lows");
    for (int i = 0; i < 1000; i++) Frame(i == 500 ? 40.0 : i == 600 ? 9.97 + 20.0 : 9.97, 0.3, 1);
    Frame(9.97, 0.3, 1); /* the window closed at 998 frames (10.00009 s) */
    double low1, low01; uint32_t over25, over33;
    co_framecost_test_last(&low1, &low01, &over25, &over33);
    CHECK(over25 == 2 && over33 == 1); /* 29.97 ms is over 25, not over 33.3 */
    CHECK(low01 > 24.9 && low01 < 25.1);                 /* the one 40 ms frame */
    CHECK(fake_host_logged("frame times (10 s, QPC): 998 frames"));
    CHECK(fake_host_logged("worst 40.0 ms; 2 over 25 ms, 1 over 33.3 ms"));
    CHECK(fake_host_logged("a 40.0 ms frame (QPC; the game's window in front): the mod's tick 0.32 ms (the most: living world 0.30 ms), the rest 39.7 ms"));
}

static void TestLowShare(void)
{
    Reset("1% low rounds up");
    for (int i = 0; i < 12; i++) Frame(20.0, 0.0, 1);
    for (int i = 0; i < 1237; i++) Frame(7.9, 0.0, 1); /* closes at 1248 frames: 1% is 12.48, so the slowest 13 */
    double low1, low01; uint32_t over25, over33;
    co_framecost_test_last(&low1, &low01, &over25, &over33);
    CHECK(fake_host_logged("frame times (10 s, QPC): 1248 frames"));
    CHECK(low1 > 13000.0 / (240.0 + 7.9) - 0.05 && low1 < 13000.0 / (240.0 + 7.9) + 0.05);
    CHECK(low01 > 49.9 && low01 < 50.1); /* the slowest 2 */
}

static void TestNoParts(void)
{
    Reset("a slow frame with no part timed");
    Frame(10.0, 0.0, 1);
    co_framecost_test_begin(g_now, 1); g_now += 30000; /* no mark */
    Frame(10.0, 0.0, 1);
    CHECK(fake_host_logged("a 30.0 ms frame (QPC; the game's window in front): the mod's tick 0.00 ms (the most: status 0.00 ms)"));
}

static void TestBackFromShip(void)
{
    Reset("a window starts again after the ship");
    for (int i = 0; i < 500; i++) Frame(10.0, 0.0, 1);
    for (int i = 0; i < 200; i++) Frame(50.0, 0.0, 0);
    for (int i = 0; i < 900; i++) Frame(10.0, 0.0, 1); /* 9 s: no window has 10 s of complex-mission frames */
    CHECK(co_framecost_test_lines() == 0);
}

static void TestSlowLinesBounded(void)
{
    Reset("slow lines bounded");
    for (int i = 0; i < 100; i++) Frame(30.0, 0.0, 1);
    CHECK(co_framecost_test_lines() == CO_FC_SLOW_LINES_WINDOW);
    for (int i = 0; i < 300; i++) Frame(30.0, 0.0, 1); /* past 10 s: one window line, then two slow lines again */
    CHECK(co_framecost_test_lines() == 2 * CO_FC_SLOW_LINES_WINDOW + 1);
    Reset("session cap");
    for (int i = 0; i < 200000; i++) { if (co_framecost_test_lines() < CO_FC_LINES_SESSION - 3) g_fake.log[0] = 0; Frame(30.0, 0.0, 1); } /* the fake log holds 64 KiB */
    CHECK(co_framecost_test_lines() == CO_FC_LINES_SESSION);
    CHECK(fake_host_logged("frame times: 800 lines this session; no more"));
}

static void TestOnlyLiveFrames(void)
{
    Reset("only complex-mission frames");
    for (int i = 0; i < 2000; i++) Frame(50.0, 0.0, 0); /* the ship: nothing is counted or logged */
    CHECK(co_framecost_test_lines() == 0);
    Frame(5000.0, 0.0, 1); /* the first live tick after a load starts the window; the load is no frame */
    for (int i = 0; i < 1100; i++) Frame(9.0, 0.0, 1);
    CHECK(co_framecost_test_lines() == 1);
    CHECK(!fake_host_logged("a 5000.0 ms frame"));
    Reset("a pause is no frame");
    Frame(9.0, 0.0, 1); Frame(1500.0, 0.0, 1); Frame(9.0, 0.0, 1);
    CHECK(co_framecost_test_lines() == 0);
}

static void TestCpuAndContext(void)
{
    Reset("CPU and context");
    co_framecost_test_cpu(5000000000ull, 9000000000ull, 700000000ull, 16);
    Frame(10.0, 0.0, 1); /* the window starts here */
    /* 10 s later: of 16 cores x 10 s = 1.6e9 units, 1.2e9 busy (75%), the game 2.5e8 (2.5 cores) */
    co_framecost_test_cpu(6200000000ull, 10600000000ull, 950000000ull, 16);
    g_ai = 121; g_nest = 48.6f;
    for (int i = 0; i < 1000; i++) Frame(10.0, 0.0, 1);
    CHECK(fake_host_logged("CPU: the PC 75% busy (12.0 of 16 cores), the game 2.5 cores, everything else 9.5"));
    CHECK(fake_host_logged("AI 121; the nearest garrison with living spawners 49 m"));
    Reset("no garrison known");
    for (int i = 0; i < 1002; i++) Frame(10.0, 0.0, 1);
    CHECK(fake_host_logged("the nearest garrison with living spawners none known"));
}

int main(void)
{
    TestLows();
    TestLowShare();
    TestNoParts();
    TestBackFromShip();
    TestSlowLinesBounded();
    TestOnlyLiveFrames();
    TestCpuAndContext();
    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("framecost: all checks passed\n");
    return 0;
}
