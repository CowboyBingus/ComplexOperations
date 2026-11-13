/* The memory probe's walkers (src/xl/proto/difficulty_world_memprobe.c) on synthetic memory laid out as the build
 * 25480438 game has it: rpmalloc's heaps, the render device, Havok's adapter, a nav world, the RenderObject handles and
 * the allocator list. Reads that fault (a reserved page) count as failures and end nothing else. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

int CoXlTestMemProbeOn(void);
int CoXlTestMemProbeHeaps(uintptr_t exe, long long* out);
int CoXlTestMemProbeDevice(uintptr_t exe, uintptr_t dev, long long* out);
int CoXlTestMemProbeHavok(uintptr_t exe, long long* out);
int CoXlTestMemProbeNav(uintptr_t exe, long long* out);
int CoXlTestMemProbeHandles(uintptr_t world, uint32_t* used, uint32_t* cap);
int CoXlTestMemProbeAllocList(uintptr_t exe, const uintptr_t* nodes, const LONG64* bytes, const LONG64* deltas, int n);

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

static uint8_t* Zeroed(size_t bytes) { return (uint8_t*)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
static void Put64(uint8_t* at, uint64_t v) { memcpy(at, &v, 8); }
static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static void Put16(uint8_t* at, uint16_t v) { memcpy(at, &v, 2); }
#define SPAN (64ll << 10)

static void Heaps(uint8_t* exe, uintptr_t unreadable)
{
    g_case = "rpmalloc heaps";
    Put64(exe + 0x27B10F0u, 4096);
    for (uint32_t c = 0; c < 96; c++) Put32(exe + 0x27B0DF0u + 8 * c, 16 * (c + 1));
    uint8_t* a = Zeroed(0x2080); uint8_t* b = Zeroed(0x2080); uint8_t* teb = Zeroed(0x100);
    uint8_t* s1 = Zeroed(0x40); uint8_t* s2 = Zeroed(0x40); uint8_t* s3 = Zeroed(0x40); uint8_t* loop = Zeroed(0x40);
    Put64(teb + 0x48, 4242);
    Put64(a, (uint64_t)(uintptr_t)teb); Put32(a + 0x738, 7); Put64(a + 0x728, (uint64_t)(uintptr_t)b);
    Put64(a + 0x608, 3);                                   /* three single spans cached */
    Put64(a + 0x748, 2);                                   /* two 2-span blocks cached (class sc = 2) */
    Put32(a + 0xD68, 10); Put32(a + 0xD70, 2);             /* span_use[0]: 10 in use, 2 deferred */
    Put32(a + 0xD68 + 0x28, 1);                            /* span_use[1]: one 2-span block */
    Put32(a + 0xFC0 + 0x2C * 3, 100); Put32(a + 0xFD0 + 0x2C * 3, 4);    /* class 3 (64 B) */
    Put32(a + 0xFC0 + 0x2C * 50, 1000); Put32(a + 0xFD0 + 0x2C * 50, 44); /* class 50 (816 B) */
    Put64(a + 0x710, (uint64_t)(uintptr_t)s1);             /* deferred: a small span, a 3-span large one, a huge one of 300 pages */
    Put32(s1 + 0xC, 3); Put64(s1, (uint64_t)(uintptr_t)s2);
    Put32(s2 + 0xC, 0x61); Put32(s2 + 0x2C, 3); Put64(s2, (uint64_t)(uintptr_t)s3);
    Put32(s3 + 0xC, 0xFFFFFFFFu); Put32(s3 + 0x2C, 300);
    Put64(b, UINT64_MAX); Put32(b + 0x738, 54); Put64(b + 0x608, 1); /* an orphan whose list loops on itself */
    Put64(b + 0x710, (uint64_t)(uintptr_t)loop); Put64(loop, (uint64_t)(uintptr_t)loop);
    Put64(exe + 0x27B1100u + 8 * 7, (uint64_t)(uintptr_t)a);
    Put64(exe + 0x27B1100u + 8 * 8, unreadable);
    long long o[12];
    g_fake.log[0] = 0;
    CHECK(CoXlTestMemProbeHeaps((uintptr_t)exe, o) == 2);
    CHECK(o[0] == 2 && o[1] == 1 && o[2] == 1);
    CHECK(o[3] == 3 * SPAN + 2 * 2 * SPAN + 1 * SPAN);      /* thread caches */
    CHECK(o[4] == 10 * SPAN + 2 * SPAN);                    /* spans in use */
    CHECK(o[5] == 2 * SPAN);                                /* deferred span counter */
    CHECK(o[6] == 100 * 64 + 1000 * 816);                   /* live small and medium */
    CHECK(o[7] == 48 * SPAN);
    CHECK(o[8] == (4 * SPAN - 6400) + (44 * SPAN - 816000));
    CHECK(o[10] == 300 * 4096);
    CHECK(o[11] == 3 + 4096);                               /* the looping list stops at its cap */
    CHECK(o[9] == SPAN + 3 * SPAN + 300 * 4096 + 4096 * SPAN);
    CHECK(fake_host_logged("rpmalloc heap 7 (thread 4242 \""));
    CHECK(fake_host_logged("class 50 (816 B) 2.0 MB"));
    CHECK(fake_host_logged("rpmalloc heap 54 (orphaned, thread 0"));
    Put64(exe + 0x27B1100u + 8 * 7, 0); Put64(exe + 0x27B1100u + 8 * 8, 0);
}

static void Device(uint8_t* exe, uintptr_t unreadable)
{
    g_case = "the render device";
    uint8_t* dev = Zeroed(0x1500); uint8_t* x0 = Zeroed(0x1A0); uint8_t* x1 = Zeroed(0x1A0);
    uintptr_t xs[3] = {(uintptr_t)x0, (uintptr_t)x1, unreadable};
    Put64(dev + 0x4A0, (uint64_t)(uintptr_t)(exe + 0x168B5A0u)); Put64(dev + 0x4A8, (uint64_t)(uintptr_t)(exe + 0x1694890u));
    Put32(dev + 0x13C8, 56114); Put32(dev + 0x13E0, 120); Put32(dev + 0x13E4, 40959); Put32(dev + 0x13F0, 19);
    Put32(dev + 0x700, 3); Put64(dev + 0x708, (uint64_t)(uintptr_t)xs); Put32(dev + 0x718, 2); Put32(dev + 0x730, 1);
    Put64(dev + 0x4D0, 1212ll << 20); Put64(dev + 0x4E0, 1300ll << 20); Put64(dev + 0x1370, 5ll << 20);
    Put32(x0 + 0xC, 56114); Put32(x0 + 0x18C, 64); Put32(x1 + 0xC, 100); Put32(x1 + 0x18C, 64);
    long long o[14];
    CHECK(CoXlTestMemProbeDevice((uintptr_t)exe, (uintptr_t)dev, o) == 0);
    CHECK(o[0] == 56114 && o[1] == 120 && o[2] == 40959 && o[3] == 19);
    CHECK(o[4] == 3 && o[5] == 2 && o[6] == 1 && o[7] == 56114 && o[8] == 1);
    CHECK(o[9] == (56114ll + 100) * 24 && o[10] == 2 * 64 * 4);
    CHECK(o[11] == 1212ll << 20 && o[12] == 1300ll << 20 && o[13] == 5ll << 20);
    Put64(dev + 0x4A8, (uint64_t)(uintptr_t)(exe + 0x1694268u)); /* another node's name: not the device */
    CHECK(CoXlTestMemProbeDevice((uintptr_t)exe, (uintptr_t)dev, o) == -1);
    CHECK(CoXlTestMemProbeDevice((uintptr_t)exe, unreadable, o) == -1);
}

static void Havok(uint8_t* exe)
{
    g_case = "Havok's adapter";
    uint8_t* ctx = Zeroed(0x100); uint8_t* a = Zeroed(0x40); uint8_t* node = Zeroed(0x80);
    long long o[3];
    CHECK(CoXlTestMemProbeHavok((uintptr_t)exe, o) == -1);   /* no context yet */
    Put64(exe + 0x27BA848u, (uint64_t)(uintptr_t)ctx); Put64(ctx + 0x48, (uint64_t)(uintptr_t)a);
    Put64(a, (uint64_t)(uintptr_t)(exe + 0x169E5E8u)); Put64(a + 0x10, (uint64_t)(uintptr_t)node); Put64(a + 0x18, 2123ll << 20); Put64(a + 0x20, 1800ll << 20);
    Put64(node, (uint64_t)(uintptr_t)(exe + 0x168B5A0u)); Put64(node + 0x30, 1790ll << 20);
    CHECK(CoXlTestMemProbeHavok((uintptr_t)exe, o) == 0);
    CHECK(o[0] == 1800ll << 20 && o[1] == 2123ll << 20 && o[2] == 1790ll << 20);
    Put64(a, 0x1234);
    CHECK(CoXlTestMemProbeHavok((uintptr_t)exe, o) == -1);   /* not the adapter's vtable */
}

static void Nav(uint8_t* exe)
{
    g_case = "a nav world";
    uint8_t* off = Zeroed(0x7000); uint8_t* nav = Zeroed(0x7000); uint8_t* cells = Zeroed(8 * 0x70);
    uintptr_t worlds[2] = {(uintptr_t)off, (uintptr_t)nav};
    Put64(exe + 0x27C30B8u, (uint64_t)(uintptr_t)worlds); Put32(exe + 0x27C30C0u, 2);
    Put32(nav + 0x20, 0x13); Put16(nav + 0x6710, 4); Put16(nav + 0x6712, 2); Put16(nav + 0x6736, 0x70); Put64(nav + 0x6748, (uint64_t)(uintptr_t)cells);
    Put32(nav + 0x90, 1); Put32(nav + 0x94, 1); Put32(nav + 0xC4, 8);
    const uint32_t flags[8] = {0xC, 1, 0, 4, 0x14, 0xC, 4, 4};
    for (int c = 0; c < 8; c++) { Put32(cells + 0x70 * c + 0x5C, flags[c]); Put32(cells + 0x70 * c + 0x60, 0xFFFFFFFFu); }
    Put32(cells + 0x60, 5); Put32(cells + 0x70 * 5 + 0x60, 9);
    long long o[12];
    CHECK(CoXlTestMemProbeNav((uintptr_t)exe, o) == 1);      /* the world without flag 0x1 is left out */
    CHECK(o[0] == 1 && o[1] == 4 && o[2] == 2 && o[3] == 1 && o[4] == 1 && o[5] == 8 && o[6] == 8);
    CHECK(o[7] == 1 && o[8] == 2 && o[9] == 2 && o[10] == 2 && o[11] == 9);
}

static void Handles(uintptr_t unreadable)
{
    g_case = "RenderObject handles";
    uint8_t* world = Zeroed(0x230200); uint8_t* holder = Zeroed(0x100);
    uint32_t used = 1, cap = 1;
    CHECK(CoXlTestMemProbeHandles((uintptr_t)world, &used, &cap) == -1 && used == 0);
    Put64(world + 0x2301F8u, (uint64_t)(uintptr_t)holder); Put32(holder + 0x38, 1234); Put32(holder + 0x3C, 2097152);
    CHECK(CoXlTestMemProbeHandles((uintptr_t)world, &used, &cap) == 0 && used == 1234 && cap == 2097152);
    CHECK(CoXlTestMemProbeHandles(unreadable, &used, &cap) == -1);
}

static void Allocators(uint8_t* exe)
{
    g_case = "every allocator change of 1 MB";
    enum { N = 60 };
    static uintptr_t nodes[N]; static LONG64 bytes[N], deltas[N]; static char names[N][16];
    uint8_t* block = Zeroed(N * 0x80);
    for (int i = 0; i < N; i++)
    {
        uint8_t* node = block + 0x80 * i;
        snprintf(names[i], sizeof names[i], i ? "Node%02d" : "Root", i);
        Put64(node, (uint64_t)(uintptr_t)(exe + 0x168B5A0u)); Put64(node + 8, (uint64_t)(uintptr_t)names[i]);
        if (i) Put64(node + 0x20, (uint64_t)(uintptr_t)block);
        nodes[i] = (uintptr_t)node; bytes[i] = (LONG64)(100 + i) << 20;
        deltas[i] = i % 3 == 0 ? (LONG64)i << 20 : i % 3 == 1 ? -((LONG64)2 << 20) : (512ll << 10);
    }
    g_fake.log[0] = 0;
    int listed = CoXlTestMemProbeAllocList((uintptr_t)exe, nodes, bytes, deltas, N);
    CHECK(listed == 19 + 20); /* i % 3 == 0 from 3 (Root's change is 0), and every i % 3 == 1 */
    CHECK(fake_host_logged("60 tracking (0 other nodes), 0 MB live in all; 39 changes of 1 MB or more: Node57<Root +57.0 MB (now 157)")); /* largest first */
    CHECK(fake_host_logged("Node01<Root -2.0 MB (now 101)"));
    CHECK(!fake_host_logged("Node02<"));                            /* half a megabyte is left out */
    CHECK(fake_host_logged("allocators (test), more:"));            /* a second line when one is full */
    for (const char* line = g_fake.log; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
    {
        const char* end = strchr(line, '\n');
        CHECK((end ? (size_t)(end - line) : strlen(line)) < 1000);
    }
}

int main(void)
{
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    uint8_t* exe = fake_host_exe();
    uintptr_t unreadable = (uintptr_t)VirtualAlloc(NULL, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    if (exe == NULL || unreadable == 0) { printf("memprobe: no memory\n"); return 1; }

    g_case = "the switch";
    SetEnvironmentVariableA("HELLFORGE_XL_MEMPROBE", NULL);
    CHECK(CoXlTestMemProbeOn() == 0);
    SetEnvironmentVariableA("HELLFORGE_XL_MEMPROBE", "1");
#ifdef CO_RESEARCH
    CHECK(CoXlTestMemProbeOn() == 1);
#else
    CHECK(CoXlTestMemProbeOn() == 0); /* a build players get reads no research switch */
#endif
    SetEnvironmentVariableA("HELLFORGE_XL_MEMPROBE", "0");
    CHECK(CoXlTestMemProbeOn() == 0);
    SetEnvironmentVariableA("HELLFORGE_XL_MEMPROBE", NULL);

    Heaps(exe, unreadable);
    Device(exe, unreadable);
    Havok(exe);
    Nav(exe);
    Handles(unreadable);
    Allocators(exe);
    if (g_failed) { printf("memprobe: %d check(s) failed\n", g_failed); return 1; }
    printf("memprobe: all checks passed\n");
    return 0;
}
