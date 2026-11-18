/* The war-table crash's watch: Dr7 for W's and N4's words, the claim of a trap, where a write came from and the values
 * a field never holds, which hits get a line; then live under Wine: the self-test, the map screen's push arming every thread, a
 * write from another thread logged with its value and thread, the free disarming them, and a write after it unseen. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"
#include "co_watch.h"

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

#define MENU 0x3700000u /* the menu object, in the image the fake host reads */

/* A thread that writes what it is told, so the writes come from a thread that was armed as a game thread is. */
static HANDLE g_go, g_done;
static volatile uintptr_t g_at;
static volatile uint64_t g_value;
static volatile int g_bytes, g_stop;
static DWORD WINAPI Writer(void* unused)
{
    for (;;)
    {
        WaitForSingleObject(g_go, INFINITE);
        if (g_stop) return 0;
        if (g_bytes == 8) *(volatile uint64_t*)g_at = g_value;
        else *(volatile uint32_t*)g_at = (uint32_t)g_value;
        SetEvent(g_done);
    }
}
static void Write(uintptr_t at, uint64_t value, int bytes)
{
    g_at = at; g_value = value; g_bytes = bytes;
    SetEvent(g_go);
    WaitForSingleObject(g_done, 5000);
}

static uint64_t g_freed;
static uint64_t Ctor(uint64_t object) { return object; }
static uint64_t Free(uint64_t menu, uint64_t id) { g_freed = id; return 0; }

int main(void)
{
    g_case = "the plan and Dr7";
    CoWatchPlan plan;
    CHECK(co_watch_plan(0x3C4900080ull, &plan) == 1);
    CHECK(plan.at[CO_WATCH_FLAGS] == 0x3C4900080ull + 0xB2990u && plan.length[CO_WATCH_FLAGS] == 4);
    CHECK(plan.at[CO_WATCH_LINK] == 0x3C4900080ull + 0xB2A70u && plan.length[CO_WATCH_LINK] == 8);
    CHECK(plan.at[CO_WATCH_ELEMENT] == 0x3C4900080ull + 0xAF338u && plan.length[CO_WATCH_ELEMENT] == 8);
    CHECK(plan.at[CO_WATCH_CHILD] == 0x3C4900080ull + 0xAF348u && plan.length[CO_WATCH_CHILD] == 8);
    CHECK(co_watch_dr7(&plan) == 0x999D0055ull); /* L0-L3; write, 4 / 8 / 8 / 8 bytes */
    CHECK(co_watch_plan(0x3C4900084ull, &plan) == 0);
    plan.count = 1; plan.at[0] = 0x1000; plan.length[0] = 3;
    CHECK(co_watch_dr7(&plan) == 0);
    plan.length[0] = 2; plan.at[0] = 0x1001;
    CHECK(co_watch_dr7(&plan) == 0);

    g_case = "the claim";
    CHECK(co_watch_claim(0x80000004u, 1, 0, 1) == CO_WATCH_CLAIM);
    CHECK(co_watch_claim(0x80000004u, 0, 0, 1) == CO_WATCH_CLAIM_UNKNOWN);
    CHECK(co_watch_claim(0x80000004u, 0, 0x100, 1) == CO_WATCH_PASS);  /* the trap flag's step */
    CHECK(co_watch_claim(0x80000004u, 0x4000, 0, 1) == CO_WATCH_PASS); /* Dr6's BS */
    CHECK(co_watch_claim(0x80000004u, 1, 0, 0) == CO_WATCH_PASS);      /* a thread the watch never armed */
    CHECK(co_watch_claim(0xC0000005u, 1, 0, 1) == CO_WATCH_PASS);

    g_case = "where a write came from and suspicious values";
    CHECK(co_watch_in_library(0x10000000u, 0x10000000u + 0x1448B9Du));
    CHECK(!co_watch_in_library(0x10000000u, 0x10000000u + 0x17A4778u));
    CHECK(!co_watch_in_library(0x10000000u, 0x10000000u + 0x1453000u));
    CHECK(co_watch_known(0x10000000u, 0x10000000u + 0x1448B9Du) == CO_WATCH_IN_LIBRARY);
    CHECK(co_watch_known(0x10000000u, 0x10000000u + 0x1A18FB7u) == CO_WATCH_IN_NODE_INIT);
    CHECK(co_watch_known(0x10000000u, 0x10000000u + 0x1A18FE8u) == CO_WATCH_IN_NODE_INIT);
    CHECK(co_watch_known(0x10000000u, 0x10000000u + 0x1A18FB0u) == CO_WATCH_ELSEWHERE);
    CHECK(co_watch_suspicious(CO_WATCH_LINK, 0x4280000000000000ull));
    CHECK(!co_watch_suspicious(CO_WATCH_LINK, 0) && !co_watch_suspicious(CO_WATCH_LINK, 0x3C49B2588ull));
    CHECK(co_watch_suspicious(CO_WATCH_LINK, 0x3C49B2589ull));
    CHECK(co_watch_suspicious(CO_WATCH_FLAGS, 0x3F000000u) && co_watch_suspicious(CO_WATCH_FLAGS, 0x42800000u));
    CHECK(!co_watch_suspicious(CO_WATCH_FLAGS, 0x08000005u));
    CHECK(co_watch_suspicious(CO_WATCH_ELEMENT, 0) && co_watch_suspicious(CO_WATCH_ELEMENT, 0xC00C1017u));
    CHECK(!co_watch_suspicious(CO_WATCH_ELEMENT, 0x3C49B2588ull));
    CHECK(co_watch_suspicious(CO_WATCH_CHILD, 0xC00C1017u) && co_watch_suspicious(CO_WATCH_CHILD, 0x4280000000000000ull));
    CHECK(!co_watch_suspicious(CO_WATCH_CHILD, 0) && !co_watch_suspicious(CO_WATCH_CHILD, 0x3C49B2588ull));

    g_case = "the hit filter";
    static CoWatchFilter f;
    for (int i = 0; i < CO_WATCH_PER_PLACE; i++) CHECK(co_watch_filter(&f, 0x1000, CO_WATCH_IN_LIBRARY, 0) == CO_WATCH_LOG_NEW);
    CHECK(co_watch_filter(&f, 0x1000, CO_WATCH_IN_LIBRARY, 0) == 0); /* a place's repeat: counted only */
    CHECK(co_watch_filter(&f, 0x1000, CO_WATCH_IN_LIBRARY, 1) == CO_WATCH_LOG_SUSPICIOUS);
    CHECK(f.count[0] == CO_WATCH_PER_PLACE + 2 && f.known[0] == CO_WATCH_IN_LIBRARY);
    CHECK(co_watch_filter(&f, 0x2000, CO_WATCH_ELSEWHERE, 0) == CO_WATCH_LOG_NEW);
    for (int i = 1; i < CO_WATCH_PER_PLACE; i++) co_watch_filter(&f, 0x2000, CO_WATCH_ELSEWHERE, 0);
    CHECK(co_watch_filter(&f, 0x2000, CO_WATCH_ELSEWHERE, 0) == 0);
    for (int i = 0; i < CO_WATCH_CAP_SUSPICIOUS; i++) co_watch_filter(&f, 0x2000, CO_WATCH_ELSEWHERE, 1);
    CHECK(co_watch_filter(&f, 0x2000, CO_WATCH_ELSEWHERE, 1) == 0); /* suspicious, past its cap */
    for (int i = 0; i < CO_WATCH_PLACES; i++) co_watch_filter(&f, 0x10000 + 8u * (unsigned)i, CO_WATCH_IN_LIBRARY, 0);
    CHECK(f.overflow > 0);
    CHECK(f.logged[0] <= CO_WATCH_CAP_NEW + CO_WATCH_PLACES); /* lines past the cap are counted, not logged */

    g_case = "a changed game refuses";
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    g_fake.strictCode = 1;
    const uintptr_t game = (uintptr_t)g_fake.image;
    CHECK(co_watch_install(game) != NULL); /* off in the other tests */
    co_watch_test_pins(g_fake.image + 0x100); /* turns it on; the pins land off their places */
    CHECK(co_watch_install(game) != NULL);
    CHECK(g_fake.hookCount == 0);

    g_case = "install";
    co_watch_test_pins(g_fake.image);
    g_fake.originals[0].target = game + CO_WATCH_CTOR; g_fake.originals[0].original = (void*)Ctor;
    g_fake.originals[1].target = game + CO_WATCH_FREE; g_fake.originals[1].original = (void*)Free;
    CHECK(co_watch_install(game) == NULL);
    uint64_t (*ctor)(uint64_t) = (uint64_t (*)(uint64_t))fake_host_detour(CO_WATCH_CTOR);
    uint64_t (*freeScreen)(uint64_t, uint64_t) = (uint64_t (*)(uint64_t, uint64_t))fake_host_detour(CO_WATCH_FREE);
    CHECK(ctor != NULL && freeScreen != NULL);
    CHECK(co_watch_test_settle(10000));
    CHECK(co_watch_test_self() == 2);
    CHECK(fake_host_logged("self-test: 2 trap(s)"));
    if (ctor == NULL || freeScreen == NULL || co_watch_test_self() != 2) { printf("%s", g_fake.log); printf("watch: %d failed\n", g_failed + 1); return 1; }

    g_case = "the push arms every thread";
    g_go = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_done = CreateEventW(NULL, FALSE, FALSE, NULL);
    DWORD writerId = 0;
    HANDLE writer = CreateThread(NULL, 0, Writer, NULL, 0, &writerId);
    uint8_t* block = VirtualAlloc(NULL, CO_WATCH_SCREEN_SIZE + 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    const uintptr_t o = (uintptr_t)block + 0x80, w = o + CO_WATCH_W, n4 = o + CO_WATCH_N4;
    memcpy(g_fake.image + MENU + CO_WATCH_SCREEN_SLOT, &o, 8);
    *(uint64_t*)n4 = o + CO_WATCH_Z + CO_WATCH_C_IN_Z; /* the element C, as the panel init leaves it */
    CHECK(ctor(o) == o);
    CHECK(co_watch_test_settle(10000));
    CHECK(fake_host_logged("map screen pushed: O"));
    CHECK(fake_host_logged("armed on") && fake_host_logged("with Dr7 0x999d0055"));
    CHECK(fake_host_logged("N4+0x10 0x") && fake_host_logged("(8 bytes) holds 0000000000000000"));
    CHECK(co_watch_test_armed(writerId));

    g_case = "a write over W is logged";
    Write(w + 0xE0, 0x4280000000000000ull, 8);
    CHECK(co_watch_test_hits() == 1);
    char thread[48];
    snprintf(thread, sizeof thread, "on thread %lu:", writerId);
    CHECK(fake_host_logged("hit 1 (one of a place's first, suspicious value, outside the widget library)"));
    CHECK(fake_host_logged(thread));
    CHECK(fake_host_logged("W+0xE0 4280000000000000"));
    /* the writer is the thread's first function; Wine names its module kernel32.dll, Windows KERNEL32.DLL */
    CHECK(fake_host_logged("hit 1 frames: kernel32.dll+0x") || fake_host_logged("hit 1 frames: KERNEL32.DLL+0x"));
    Write(w, 0x3F000000u, 4);
    CHECK(co_watch_test_hits() == 2);
    CHECK(fake_host_logged("W+0x0 3f000000"));

    g_case = "N4's element cleared and its child overwritten are logged";
    Write(n4, 0, 8);
    CHECK(co_watch_test_hits() == 3);
    CHECK(fake_host_logged("hit 3 (one of a place's first, suspicious value,"));
    CHECK(fake_host_logged("N4+0x0 0000000000000000"));
    Write(n4 + 0x10, 0xC00C1017u, 8);
    CHECK(co_watch_test_hits() == 4);
    CHECK(fake_host_logged("N4+0x10 00000000c00c1017"));
    Write(w + 0xE0, 0, 8); /* the 8-byte place's fourth write: counted, no line */
    CHECK(co_watch_test_hits() == 5 && !fake_host_logged("hit 5 "));

    g_case = "the free disarms";
    CHECK(freeScreen((uintptr_t)g_fake.image + MENU, CO_WATCH_SCREEN_ID) == 0 && g_freed == CO_WATCH_SCREEN_ID);
    CHECK(fake_host_logged("map screen popped") && fake_host_logged("the watch is off"));
    CHECK(fake_host_logged("disarmed for O") && fake_host_logged("places wrote the watched words"));
    CHECK(!co_watch_test_armed(writerId));
    CONTEXT context;
    memset(&context, 0, sizeof context);
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    CHECK(GetThreadContext(writer, &context) && (context.Dr7 & CO_WATCH_DR7_BITS) == 0 && context.Dr0 == 0);
    Write(w + 0xE0, 0, 8);
    CHECK(co_watch_test_hits() == 5);

    g_stop = 1;
    SetEvent(g_go);
    WaitForSingleObject(writer, 5000);
    if (g_failed != 0) printf("%s", g_fake.log);
    printf("watch: %d failed\n", g_failed);
    return g_failed != 0;
}
