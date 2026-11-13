/* Wall-clock frame times in a complex mission's play: the 10 s window's lows, slow and long frames, timed parts and
 * CPU load, on a test clock of 1,000,000 ticks a second (1 tick = 1 us). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "xl/co_framecost.h"

int CoXlTestInPlay(uintptr_t game, float live);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)
#define US 1000000

static int64_t g_now;
static long g_mission;
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
    g_now = 1000; g_mission = 1;
}

/* One tick at g_now, then the clock moves on by `ms`; `living` of it inside the living world's part. */
static void Frame(double ms, double living, int play)
{
    co_framecost_test_begin(g_now, play, g_mission);
    co_framecost_test_mark(CO_FC_STREAM, g_now + 10);
    co_framecost_test_mark(CO_FC_LIVING, g_now + 10 + (int64_t)(living * 1000.0));
    co_framecost_test_mark(CO_FC_DENSITY, g_now + 20 + (int64_t)(living * 1000.0));
    g_now += (int64_t)(ms * 1000.0);
}

static void Last(uint32_t* over25, uint32_t* over33, uint32_t* over1s, int* starved)
{
    double low1, low01;
    co_framecost_test_last(&low1, &low01, over25, over33, over1s, starved);
}

static void TestLows(void)
{
    Reset("lows");
    for (int i = 0; i < 1000; i++) Frame(i == 500 ? 40.0 : i == 600 ? 9.97 + 20.0 : 9.97, 0.3, 1);
    Frame(9.97, 0.3, 1); /* the window closed at 998 frames (10.00009 s) */
    double low1, low01; uint32_t over25, over33, over1s; int starved;
    co_framecost_test_last(&low1, &low01, &over25, &over33, &over1s, &starved);
    CHECK(over25 == 2 && over33 == 1 && over1s == 0); /* 29.97 ms is over 25, not over 33.3 */
    CHECK(low01 > 24.9 && low01 < 25.1);                 /* the one 40 ms frame */
    CHECK(fake_host_logged("frame times (10.0 s of play, QPC): 998 frames"));
    CHECK(fake_host_logged("worst 40.0 ms (timed parts 0.32 ms, the most: living world 0.30 ms); 2 over 25 ms, 1 over 33.3 ms, 0 of 1 s or more; timed parts (CoXlTick, the level update) 0.32 ms a frame, off the game thread 0.0 ms"));
    CHECK(fake_host_logged("a 40.0 ms frame (QPC; the game's window in front): timed parts (CoXlTick, the level update) 0.32 ms (the most: living world 0.30 ms), untimed 39.7 ms"));
}

static void TestLowShare(void)
{
    Reset("1% low rounds up");
    for (int i = 0; i < 12; i++) Frame(20.0, 0.0, 1);
    for (int i = 0; i < 1237; i++) Frame(7.9, 0.0, 1); /* closes at 1248 frames: 1% is 12.48, so the slowest 13 */
    double low1, low01; uint32_t over25, over33, over1s; int starved;
    co_framecost_test_last(&low1, &low01, &over25, &over33, &over1s, &starved);
    CHECK(fake_host_logged("frame times (10.0 s of play, QPC): 1248 frames"));
    CHECK(low1 > 13000.0 / (240.0 + 7.9) - 0.05 && low1 < 13000.0 / (240.0 + 7.9) + 0.05);
    CHECK(low01 > 49.9 && low01 < 50.1); /* the slowest 2 */
}

static void TestNoParts(void)
{
    Reset("a slow frame with no part timed");
    Frame(10.0, 0.0, 1);
    co_framecost_test_begin(g_now, 1, g_mission); g_now += 30000; /* no mark */
    for (int i = 0; i < 1000; i++) Frame(10.0, 0.0, 1);
    CHECK(fake_host_logged("a 30.0 ms frame (QPC; the game's window in front): timed parts (CoXlTick, the level update) 0.00 ms (the most: status 0.00 ms)"));
}

static void TestBackFromShip(void)
{
    Reset("a window starts again after the ship");
    for (int i = 0; i < 500; i++) Frame(10.0, 0.0, 1);
    for (int i = 0; i < 200; i++) Frame(50.0, 0.0, 0);
    for (int i = 0; i < 900; i++) Frame(10.0, 0.0, 1); /* 9 s: no window has 10 s of play */
    CHECK(co_framecost_test_window_lines() == 0);
    Reset("a new mission starts a window");
    for (int i = 0; i < 500; i++) Frame(10.0, 0.0, 1);
    g_mission = 2;
    for (int i = 0; i < 900; i++) Frame(10.0, 0.0, 1);
    CHECK(co_framecost_test_window_lines() == 0);
}

static void TestLongFrames(void)
{
    Reset("a frame of a second or more in play is counted");
    for (int i = 0; i < 300; i++) Frame(i == 100 ? 1528.0 : 9.0, 0.0, 1);
    for (int i = 0; i < 700; i++) Frame(9.0, 0.0, 1); /* closes past 10 s with the long frame in it */
    uint32_t over25, over33, over1s; int starved;
    Last(&over25, &over33, &over1s, &starved);
    CHECK(over1s == 1 && over33 == 1 && over25 == 1);
    CHECK(fake_host_logged("worst 1528.0 ms"));
    CHECK(fake_host_logged("1 over 25 ms, 1 over 33.3 ms, 1 of 1 s or more"));
    CHECK(fake_host_logged("a 1528.0 ms frame"));
}

static void TestOnlyPlay(void)
{
    Reset("only frames of play");
    for (int i = 0; i < 2000; i++) Frame(50.0, 0.0, 0); /* the ship and the loading screen: nothing is counted */
    CHECK(co_framecost_test_window_lines() == 0 && co_framecost_test_detail_lines() == 0);
    Frame(5000.0, 0.0, 0); /* the load's last tick; play's first tick starts the window and the load is no frame */
    for (int i = 0; i < 1200; i++) Frame(9.0, 0.0, 1);
    CHECK(co_framecost_test_window_lines() == 1);
    CHECK(!fake_host_logged("a 5000.0 ms frame") && fake_host_logged("worst 9.0 ms"));
}

static void TestGate(void)
{
    Reset("in play: game state 4 and the living world live");
    uint8_t* game = g_fake.image;
    uint8_t* world = calloc(1, 0xAD000);
    uintptr_t w = (uintptr_t)world;
    memcpy(game + 0x3326340u, &w, 8);
    uint32_t state = 6; memcpy(world + 0xAC21C, &state, 4);
    CHECK(CoXlTestInPlay((uintptr_t)game, 8.0f) == 0); /* the loading screen */
    state = 4; memcpy(world + 0xAC21C, &state, 4);
    CHECK(CoXlTestInPlay((uintptr_t)game, 8.0f) == 1);
    CHECK(CoXlTestInPlay((uintptr_t)game, 0.0f) == 0); /* not complex */
    memset(game + 0x3326340u, 0, 8);
    CHECK(CoXlTestInPlay((uintptr_t)game, 8.0f) == 0); /* no game object */
    CoXlTestInPlay(0, 0.0f);
    free(world);
}

static void TestBudgets(void)
{
    Reset("slow lines a window");
    for (int i = 0; i < 100; i++) Frame(30.0, 0.0, 1);
    CHECK(co_framecost_test_detail_lines() == 0); /* kept for the window's close */
    for (int i = 0; i < 300; i++) Frame(30.0, 0.0, 1); /* past 10 s: the window's slow lines and its line */
    CHECK(co_framecost_test_detail_lines() == CO_FC_SLOW_LINES_WINDOW && co_framecost_test_window_lines() == 1);
    Reset("budgets: slow-frame lines a session, window lines a mission");
    for (int i = 0; i < (CO_FC_WINDOW_LINES_MISSION + 5) * 10 + 1; i++) { g_fake.log[0] = 0; Frame(1000.0, 0.0, 1); } /* 10 frames a window; the fake log holds 64 KiB */
    CHECK(co_framecost_test_detail_lines() == CO_FC_DETAIL_LINES_SESSION);
    CHECK(co_framecost_test_window_lines() == CO_FC_WINDOW_LINES_MISSION + 1); /* the last line, then the note */
    g_mission = 2; /* a new mission: the window lines go on */
    g_fake.log[0] = 0;
    for (int i = 0; i < 12; i++) Frame(1000.0, 0.0, 1);
    CHECK(co_framecost_test_window_lines() == 1);
    CHECK(fake_host_logged("frame times (1"));
}

static void TestNotes(void)
{
    Reset("the budgets' notes");
    for (int i = 0; i < 100000 && co_framecost_test_detail_lines() < CO_FC_DETAIL_LINES_SESSION; i++) /* bounded: a broken count must fail, not hang */
    {
        if (co_framecost_test_detail_lines() < CO_FC_DETAIL_LINES_SESSION - 1) g_fake.log[0] = 0;
        Frame(1000.0, 0.0, 1);
    }
    CHECK(fake_host_logged("frame times: 600 slow-frame lines this session; the window lines go on"));
    for (int i = 0; i < 100000 && co_framecost_test_window_lines() <= CO_FC_WINDOW_LINES_MISSION; i++)
    {
        if (co_framecost_test_window_lines() < CO_FC_WINDOW_LINES_MISSION) g_fake.log[0] = 0;
        Frame(1000.0, 0.0, 1);
    }
    CHECK(fake_host_logged("frame times: 1080 window lines this mission; no more until the next"));
}

/* The level update hook's parts, timed outside CoXlTick: a 40 ms frame with 30 ms of moving tiles, 5 of it the ground keep. */
static void TestHookParts(void)
{
    Reset("the level update's parts");
    Frame(10.0, 0.0, 1);
    int64_t t = g_now;
    co_framecost_test_begin(t, 1, g_mission);
    co_framecost_test_nested(CO_FC_GROUNDKEEP, CO_FC_MOVE, t + 200, t + 5200);
    CHECK(co_framecost_test_add(CO_FC_MOVE, t + 100, t + 30100) == 30000);
    g_now = t + 40000;
    for (int i = 0; i < 1000; i++) Frame(10.0, 0.0, 1);
    CHECK(fake_host_logged("a 40.0 ms frame (QPC; the game's window in front): timed parts (CoXlTick, the level update) 30.00 ms (the most: moving tiles 25.00 ms), untimed 10.0 ms"));
}

/* The lines name the window's slowest frames, not its first: a 1.3 s stall after two 26 ms frames was missed. */
static void TestSlowest(void)
{
    Reset("the window's slowest frames");
    for (int i = 0; i < 100; i++) Frame(i == 10 ? 26.0 : i == 20 ? 27.0 : i == 30 ? 1269.4 : i == 40 ? 26.5 : i == 50 ? 140.0 : 9.0, 0.0, 1);
    for (int i = 0; i < 1000; i++) Frame(9.0, 0.0, 1);
    CHECK(fake_host_logged("a 1269.4 ms frame") && fake_host_logged("a 140.0 ms frame"));
    CHECK(!fake_host_logged("a 26.0 ms frame") && !fake_host_logged("a 27.0 ms frame") && !fake_host_logged("a 26.5 ms frame"));
    CHECK(fake_host_logged("untimed 1269.4 ms; 0.3 s into the window")); /* 30 frames of 9 ms in, at 0.27 s */
    CHECK(co_framecost_test_detail_lines() == CO_FC_SLOW_LINES_WINDOW);
    Reset("a window cut short by the ship still names its stalls");
    for (int i = 0; i < 100; i++) Frame(i == 50 ? 333.0 : 9.0, 0.0, 1);
    CHECK(!fake_host_logged("a 333.0 ms frame"));
    Frame(50.0, 0.0, 0);
    CHECK(fake_host_logged("a 333.0 ms frame") && co_framecost_test_detail_lines() == 1);
    for (int i = 0; i < 1200; i++) Frame(9.0, 0.0, 1); /* the next window has none */
    CHECK(co_framecost_test_detail_lines() == 1);
    Reset("a new mission flushes the last one's");
    for (int i = 0; i < 100; i++) Frame(i == 50 ? 333.0 : 9.0, 0.0, 1);
    g_mission = 2;
    Frame(9.0, 0.0, 1);
    CHECK(fake_host_logged("a 333.0 ms frame") && co_framecost_test_detail_lines() == 1);
}

static DWORD WINAPI OffThread(LPVOID unused) { (void)unused; co_framecost_test_add(CO_FC_FARGRASS, 1, 1 + 50000); return 0; }

static void TestOffThread(void)
{
    Reset("a part on another thread");
    Frame(10.0, 0.0, 1);
    HANDLE h = CreateThread(NULL, 0, OffThread, NULL, 0, NULL);
    WaitForSingleObject(h, INFINITE); CloseHandle(h);
    for (int i = 0; i < 1001; i++) Frame(10.0, 0.0, 1);
    CHECK(fake_host_logged("off the game thread 50.0 ms"));
    CHECK(!fake_host_logged("most: far grass"));
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
    uint32_t over25, over33, over1s; int starved;
    Last(&over25, &over33, &over1s, &starved);
    CHECK(fake_host_logged("frame times, CPU: the PC 75% busy (12.0 of 16 cores), the game 2.5 cores, everything else 9.5; AI 121"));
    CHECK(!starved && !fake_host_logged("STARVED"));
    CHECK(fake_host_logged("AI 121; the nearest garrison with living spawners 49 m"));
    Reset("starved: everything else takes the cores the game leaves free, less one");
    co_framecost_test_cpu(5000000000ull, 9000000000ull, 700000000ull, 16);
    Frame(10.0, 0.0, 1);
    co_framecost_test_cpu(5000000000ull + 1550000000ull, 10600000000ull, 950000000ull, 16); /* 15.5 cores busy: 13.0 else */
    for (int i = 0; i < 1000; i++) Frame(10.0, 0.0, 1);
    Last(&over25, &over33, &over1s, &starved);
    CHECK(starved && fake_host_logged("everything else 13.0; STARVED: everything else took 13.0 of the 13.5 cores the game left free; AI -1; the nearest garrison with living spawners none known"));
    Reset("one idle core is not starved");
    co_framecost_test_cpu(5000000000ull, 9000000000ull, 700000000ull, 16);
    Frame(10.0, 0.0, 1);
    co_framecost_test_cpu(5000000000ull + 1490000000ull, 10600000000ull, 950000000ull, 16); /* 14.9 busy: 1.1 idle */
    for (int i = 0; i < 1000; i++) Frame(10.0, 0.0, 1);
    Last(&over25, &over33, &over1s, &starved);
    CHECK(!starved);
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
    TestLongFrames();
    TestOnlyPlay();
    TestGate();
    TestBudgets();
    TestNotes();
    TestHookParts();
    TestSlowest();
    TestOffThread();
    TestCpuAndContext();
    if (g_failed) { printf("%d check(s) failed\n", g_failed); return 1; }
    printf("framecost: all checks passed\n");
    return 0;
}
