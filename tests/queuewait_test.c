/* The bounded wait for the generation's job queue (proto/difficulty_world_terrain.c NearQueueWait), used by a build's
 * teardown, the replay's drain and the conform's last drain. The queue ([ctx+0x50]: +0 next ticket, +8 ticket served,
 * +0x10 count) must count frames while it is non-empty or unreadable and fail after `limit`; then its jobs are dropped
 * under its ticket lock when the lock is free, and left when it is held. An empty queue waits `settle` frames. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

int CoXlTestQueueWait(uint64_t context, uint32_t* waits, uint32_t* quiet, uint32_t limit, uint32_t settle);
void co_shim_install(void);

/* A queue pointer to reserved memory: any access outside SafeRead lands here (after SafeRead's own handler), which
 * counts it and commits the page so the test goes on to its checks. */
static uint8_t* g_reserved;
static volatile LONG g_stray;
static LONG CALLBACK Stray(EXCEPTION_POINTERS* info)
{
    const uintptr_t at = info->ExceptionRecord->NumberParameters > 1 ? (uintptr_t)info->ExceptionRecord->ExceptionInformation[1] : 0;
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || g_reserved == NULL || at < (uintptr_t)g_reserved ||
        at >= (uintptr_t)g_reserved + 0x10000) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&g_stray);
    VirtualAlloc(g_reserved, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t g_ctx[0xE8];
static struct { int64_t ticket, served; uint32_t count; } g_queue;

static int Run(uint32_t limit, uint32_t settle, int* frames)
{
    uint32_t waits = 0, quiet = 0; int r = 1, n = 0;
    while (r == 1 && n < 100000) { r = CoXlTestQueueWait((uint64_t)(uintptr_t)g_ctx, &waits, &quiet, limit, settle); n++; }
    *frames = n;
    return r;
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    co_shim_install(); /* SafeRead in place, as in the game */
    g_reserved = (uint8_t*)VirtualAlloc(NULL, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    AddVectoredExceptionHandler(0, Stray);
    uint64_t q = (uint64_t)(uintptr_t)&g_queue;
    memcpy(g_ctx + 0x50, &q, 8);
    int frames = 0;

    g_case = "an empty queue: done after the settle frames";
    g_queue.count = 0;
    CHECK(Run(600, 4, &frames) == 0 && frames == 4);

    g_case = "a queue that never empties: 600 frames, then a failure with its jobs dropped under the lock";
    g_queue.ticket = g_queue.served = 7; g_queue.count = 3; g_fake.log[0] = 0;
    CHECK(Run(600, 4, &frames) == -1 && frames == 601);
    CHECK(g_queue.count == 0 && g_queue.ticket == 8 && g_queue.served == 8);
    CHECK(fake_host_logged("the test: the job queue did not empty in 600 frames; its 3 jobs were dropped"));

    g_case = "the queue's lock held: the failure leaves the jobs and the lock as they are";
    g_queue.ticket = 5; g_queue.served = 4; g_queue.count = 2; g_fake.log[0] = 0;
    CHECK(Run(600, 4, &frames) == -1 && frames == 601);
    CHECK(g_queue.count == 2 && g_queue.ticket == 5 && g_queue.served == 4);
    CHECK(fake_host_logged("it did not read or its lock is held, so its jobs stay"));

    g_case = "no queue to read: counted as not empty, a failure after the limit";
    memset(g_ctx + 0x50, 0, 8); g_fake.log[0] = 0;
    CHECK(Run(600, 4, &frames) == -1 && frames == 601);
    CHECK(fake_host_logged("so its jobs stay"));

    g_case = "a queue pointer that does not read: never touched, its jobs left";
    {
        uint64_t bad = (uint64_t)(uintptr_t)g_reserved;
        memcpy(g_ctx + 0x50, &bad, 8); g_fake.log[0] = 0;
        CHECK(g_reserved != NULL && Run(600, 4, &frames) == -1 && frames == 601);
        CHECK(g_stray == 0 && fake_host_logged("so its jobs stay"));
    }

    g_case = "a queue that empties after a while: done once it has, after the settle frames";
    memcpy(g_ctx + 0x50, &q, 8);
    g_queue.ticket = g_queue.served = 0; g_queue.count = 1;
    {
        uint32_t waits = 0, quiet = 0; int r = 0, n = 0;
        for (; n < 10; n++) r = CoXlTestQueueWait((uint64_t)(uintptr_t)g_ctx, &waits, &quiet, 600, 3);
        CHECK(r == 1 && waits == 10 && quiet == 0);
        g_queue.count = 0;
        for (n = 0; (r = CoXlTestQueueWait((uint64_t)(uintptr_t)g_ctx, &waits, &quiet, 600, 3)) == 1 && n < 10; n++) {}
        CHECK(r == 0 && n == 2 && g_queue.ticket == 0);
    }

    if (g_failed) { printf("queuewait: %d check(s) failed\n", g_failed); return 1; }
    printf("queuewait: all checks passed\n");
    return 0;
}
