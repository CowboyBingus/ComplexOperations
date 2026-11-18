/* The reservations near the game's heap objects: laid beside rpmalloc's maps while the game starts (farther out when the
 * space beside a map is taken, once per area), and near the entity state and the session context as soon as their
 * globals hold them, before or after the constructor; taken by the blocks the mod needs, never released by a stray
 * MEM_RELEASE, and searched past as before when none is in reach or the map is not the reviewed code. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "harness/fake_host.h"

#define MAP_RVA 0x667850u
#define STEP 0x10000000u
#define SIZE 0x2000000u
#define SPAN 0x70000000u
#define STATE_GLOBAL 0x3326340u
#define STATE_IN_B 0xE49F0u
#define FIGURES_IN_B (0x75FE520u + 0xC008u)
#define MANAGERS_8 0x108F000u /* a build for eight's managers, one block */
#define SESSION_GLOBAL 0x347CEF0u
#define HUD_GLOBAL 0x346D538u
#define H2_IN_HUD 0x24E340u

static int g_failed;
static const char* g_case;
#define CHECK(what) do { if (!(what)) { printf("FAILED %s: %s\n", g_case, #what); g_failed++; } } while (0)

typedef void* (*MapFn)(size_t size, size_t* offset);
static void* g_mapAnswer; /* what the fake rpmalloc map returns */

static void* FakeMap(size_t size, size_t* offset)
{
    (void)size;
    if (offset != NULL) *offset = 0;
    return g_mapAnswer;
}

/* `bytes` of reserved (or committed) space at or above `from` (any address when 0). */
static uintptr_t Region(uintptr_t from, size_t bytes, DWORD type)
{
    for (uintptr_t at = from; from != 0 && at < from + 0x80000000ull; at += 0x1000000u)
    {
        void* p = VirtualAlloc((void*)at, bytes, type, PAGE_READWRITE);
        if (p != NULL) return (uintptr_t)p;
    }
    return from == 0 ? (uintptr_t)VirtualAlloc(NULL, bytes, type, PAGE_READWRITE) : 0;
}

static void Global(uint32_t rva, uintptr_t value) { memcpy(g_fake.image + rva, &value, sizeof value); }

enum { HOOKED, NOT_PINNED, REFUSED };

/* A fresh host with both globals empty (or `state`/`session` objects in them, before the install). */
static MapFn Reset(const char* name, int mode, uintptr_t b, uintptr_t session)
{
    g_case = name;
    co_heap_near_test_reset();
    g_coApi = fake_host_reset(CO_GAME_BUILD);
    g_coMod = (HfMod*)&g_fake;
    Global(STATE_GLOBAL, b);
    Global(SESSION_GLOBAL, session);
    Global(HUD_GLOBAL, 0);
    const uintptr_t exe = (uintptr_t)fake_host_exe(), rva = exe + MAP_RVA - (uintptr_t)g_fake.image;
    if (mode == NOT_PINNED) g_fake.noMatchExe[0] = MAP_RVA;
    if (mode == REFUSED) g_fake.noHook = rva;
    g_fake.originals[0].target = exe + MAP_RVA;
    g_fake.originals[0].original = (void*)FakeMap;
    CHECK(co_heap_near_install(exe, (uintptr_t)g_fake.image) == (mode == HOOKED));
    return mode == HOOKED ? (MapFn)fake_host_detour(rva) : NULL;
}

static uintptr_t Mapped(MapFn map, uintptr_t region)
{
    size_t offset = 1;
    g_mapAnswer = (void*)region;
    return (uintptr_t)map(0x100000u, &offset);
}

static int Committed(uintptr_t at)
{
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery((void*)at, &m, sizeof m) != 0 && m.State == MEM_COMMIT;
}

static int InSpace(uintptr_t at)
{
    for (int i = 0; i < co_heap_near_test_count(); i++)
        if (at >= co_heap_near_test_space(i) && at < co_heap_near_test_space(i) + SIZE) return 1;
    return 0;
}

static int Within(uintptr_t a, uintptr_t b, uintptr_t span) { return a > b ? a - b < span : b - a < span; }

static uintptr_t g_first;

static void Beside(void)
{
    MapFn map = Reset("a map lays a reservation beside it; a map near it lays none", HOOKED, 0, 0);
    g_first = Region(0x100000000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT); /* high, as the game's heap is on Windows */
    const uintptr_t first = g_first;
    CHECK(map != NULL && first != 0 && g_fake.hookCount == 1 && co_heap_near_test_count() == 0);
    CHECK(Mapped(map, first) == first && co_heap_near_test_count() == 1);
    CHECK(Within(co_heap_near_test_space(0), first, 0x14000000u)); /* beside it, or in its area */
    CHECK(Mapped(map, first) == first && co_heap_near_test_count() == 1);

    g_case = "a block near the map comes from the reservation, after its first 64 KB, committed, with a guard after it";
    const uintptr_t a = co_alloc_near(first, 0x10000u), b = co_alloc_near(first + 0x1000u, 0x1E0000u);
    CHECK(a == co_heap_near_test_space(0) + 0x10000u && Committed(a) && b == a + 0x20000u && Committed(b + 0x1DF000u));
    CHECK(!Committed(a + 0x10000u) && !Committed(b + 0x1E0000u)); /* an overrun faults */
    *(volatile uint32_t*)b = 7;
    CHECK(*(volatile uint32_t*)b == 7);

    g_case = "a stray MEM_RELEASE leaves the reservation; co_free_near decommits the block only";
    CHECK(VirtualFree((void*)a, 0, MEM_RELEASE) == 0 && Committed(a));
    co_free_near(a);
    CHECK(!Committed(a) && Committed(b));
    const uintptr_t c = co_alloc_near(first, 0x10000u);
    CHECK(c == a && Committed(c) && *(volatile uint32_t*)c == 0); /* not the newest, but its space is handed out again, zeroed */

    g_case = "blocks freed and taken again, in any order, leave the reservation's room as it was";
    for (int round = 0; round < 300; round++) co_free_near(co_alloc_near(first, 0x30000u)); /* more than the table holds */
    const uintptr_t d = co_alloc_near(first, 0x8000u), e = co_alloc_near(first, 0x8000u);
    co_free_near(d); co_free_near(e); /* the older first: d to the freed list, then e given back at the end */
    CHECK(co_alloc_near(first, 0x8000u) == d && co_alloc_near(first, 0x8000u) == e);

    g_case = "code targets are not served from reservations";
    CHECK(co_heap_near_take((uintptr_t)g_fake.image + 0x1000u, 0x1000u) == 0 && co_heap_near_take((uintptr_t)g_fake.exe + 0x1000u, 0x1000u) == 0);

    g_case = "a block bigger than a reservation, or with none in reach, is searched for as before";
    const uintptr_t big = co_alloc_near(first, SIZE + 0x10000u);
    CHECK(big != 0 && !InSpace(big));
    co_free_near(big);
    CHECK(co_heap_near_take(first + 0x200000000ull, 0x10000u) == 0); /* 8 GB away */
}

/* 160 MB taken around the map: nothing free within 64 MB of it either way. */
static void Crowded(void)
{
    MapFn map = Reset("the space beside a map is taken: the nearest free space within reach, once for its area", HOOKED, 0, 0);
    const uintptr_t taken = Region(g_first + 0x40000000u, 0xA000000u, MEM_RESERVE);
    CHECK(taken != 0);
    if (taken == 0) return;
    const uintptr_t inside = taken + 0x5000000u; /* 80 MB in */
    CHECK(Mapped(map, inside) == inside && co_heap_near_test_count() == 1);
    const uintptr_t got = co_heap_near_test_space(0);
    CHECK((got >= taken + 0xA000000u || got + SIZE <= taken) && Within(got, inside, SPAN - STEP));
    CHECK(Mapped(map, inside + 0x1000000u) == inside + 0x1000000u && co_heap_near_test_count() == 1);
    CHECK(co_heap_near_take(inside, 0x10000u) != 0);
    VirtualFree((void*)taken, 0, MEM_RELEASE);
}

static void Targets(void)
{
    const uintptr_t object = Region(g_first + 0x200000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT); /* 8 GB away */
    const uintptr_t session = Region(g_first + 0x400000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    CHECK(object != 0 && session != 0);
    MapFn map = Reset("the game object's global set while maps go on: a reservation in reach of all of it, far from every map", HOOKED, 0, 0);
    CHECK(Mapped(map, g_first) == g_first && co_heap_near_test_count() == 1);
    Global(STATE_GLOBAL, object);
    CHECK(Mapped(map, g_first) == g_first && co_heap_near_test_count() == 2);
    g_case = "a build for eight's blocks near the game object all come from reservations";
    const uintptr_t managers = co_alloc_near(object + 0x40u, MANAGERS_8), records = co_alloc_near(object + STATE_IN_B, 0x1F8028u);
    const uintptr_t figures = co_alloc_near(object + FIGURES_IN_B, 0x790u), pods = co_alloc_near(object + STATE_IN_B + 0x40u, 0x10000u);
    CHECK(InSpace(managers) && InSpace(records) && InSpace(figures) && InSpace(pods));
    CHECK(Within(figures, object + FIGURES_IN_B, SPAN) && Within(managers, object, SPAN));
    CHECK(co_heap_near_take(session, 0x10000u) == 0);

    g_case = "settled: no more trail; the session context set later gets one from the worker, said as not made yet";
    co_heap_near_settle(object + STATE_IN_B);
    CHECK(fake_host_logged("the game object served by the watch") && fake_host_logged("the session context not made yet") &&
          fake_host_logged("the HUD not made yet") && fake_host_logged("at the entity state's constructor") &&
          fake_host_logged("the hook's own work on "));
    const uintptr_t later = Region(g_first + 0x600000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    const int before = co_heap_near_test_count();
    if (later != 0) CHECK(Mapped(map, later) == later && co_heap_near_test_count() == before); /* no trail after startup */
    Global(SESSION_GLOBAL, session);
    CHECK(Mapped(map, g_first) == g_first);
    co_heap_near_test_sync();
    CHECK(co_heap_near_test_count() == before + 1);
    CHECK(co_heap_near_take(session, 0x10000u) != 0);

    g_case = "a take finishes a change the worker has not handled yet: the block is in reach whatever the timing";
    const uintptr_t session2 = Region(g_first + 0x700000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    if (session2 != 0)
    {
        Global(SESSION_GLOBAL, session2); /* no map: only the take sees it */
        const uintptr_t block = co_heap_near_take(session2, 0x8000u);
        CHECK(block != 0 && Within(block, session2, SPAN));
        co_free_near(block);
    }
    g_case = "the HUD made in the mission: its tables get room too";
    const uintptr_t hud = Region(g_first + 0x800000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    if (hud != 0)
    {
        Global(HUD_GLOBAL, hud);
        Mapped(map, g_first);
        CHECK(co_heap_near_take(hud + H2_IN_HUD + 0x1F8C8u, 0x4C000u) != 0);
    }
    co_heap_near_settle(object + STATE_IN_B); /* the second constructor hook: said once */
    const char* line = strstr(g_fake.log, " areas of the heaps' growth handled"); /* the settle line's */
    CHECK(line != NULL && strstr(line + 1, " areas of the heaps' growth handled") == NULL && fake_host_logged("could not be pinned")); /* a buffer, not a module */

    Reset("objects the game made before the install: reservations in reach of them at once", HOOKED, object, session);
    CHECK(co_heap_near_test_count() == 2 && co_heap_near_take(object + STATE_IN_B, 0x10000u) != 0 && co_heap_near_take(session, 0x10000u) != 0);
    if (later != 0) VirtualFree((void*)later, 0, MEM_RELEASE);
    if (hud != 0) VirtualFree((void*)hud, 0, MEM_RELEASE);
    VirtualFree((void*)object, 0, MEM_RELEASE);
    VirtualFree((void*)session, 0, MEM_RELEASE);
}

static uintptr_t MappedSize(MapFn map, uintptr_t region, size_t size)
{
    size_t offset = 1;
    g_mapAnswer = (void*)region;
    return (uintptr_t)map(size, &offset);
}

/* The game object's block is one large map: it gets a reservation reaching all of it before its global is set. */
static void LargeMap(void)
{
    MapFn map = Reset("a large map gets a reservation reaching all of it; the game object made in it is served by it", HOOKED, 0, 0);
    const uintptr_t block = Region(g_first + 0xA00000000ull, 0x8347000u, MEM_RESERVE);
    CHECK(block != 0);
    if (block == 0) return;
    CHECK(MappedSize(map, block, 0x8347000u) == block && co_heap_near_test_count() >= 1);
    const int count = co_heap_near_test_count();
    Global(STATE_GLOBAL, block + 0x80u);
    CHECK(Mapped(map, g_first) == g_first); /* the trail may add one for g_first's area; the watch adds none */
    CHECK(co_heap_near_test_count() <= count + 1);
    const uintptr_t managers = co_alloc_near(block + 0x80u + 0x40u, MANAGERS_8);
    CHECK(InSpace(managers) && co_heap_near_take(block + 0x80u + FIGURES_IN_B, 0x790u) != 0);
    co_heap_near_settle(block + 0x80u + STATE_IN_B);
    CHECK(fake_host_logged("the game object served by a large map's reservation") && fake_host_logged("1 large maps searched for on their own"));
    VirtualFree((void*)block, 0, MEM_RELEASE);
}

/* A global that switches among objects: each is searched for once; one that cannot be served stops searching. */
static void Switching(void)
{
    MapFn map = Reset("a global switching between known objects searches for none again", HOOKED, 0, 0);
    const uintptr_t x = Region(g_first + 0xC00000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    const uintptr_t y = Region(g_first + 0xE00000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    CHECK(x != 0 && y != 0);
    co_heap_near_settle(x); /* after startup: the worker handles the changes */
    Global(SESSION_GLOBAL, x); Mapped(map, g_first); co_heap_near_test_sync();
    Global(SESSION_GLOBAL, y); Mapped(map, g_first); co_heap_near_test_sync();
    const int count = co_heap_near_test_count();
    CHECK(count == 2); /* x's and y's */
    for (int round = 0; round < 4; round++)
    {
        Global(SESSION_GLOBAL, x); Mapped(map, g_first); co_heap_near_test_sync();
        Global(SESSION_GLOBAL, y); Mapped(map, g_first); co_heap_near_test_sync();
    }
    CHECK(co_heap_near_test_count() == count);

    g_case = "objects that can never be served: at most four searches, then said as such";
    for (int k = 0; k < 6; k++)
    {
        Global(HUD_GLOBAL, 0xFFFF800000000000ull + (uintptr_t)k * 0x100000000ull);
        Mapped(map, g_first);
        co_heap_near_test_sync();
    }
    CHECK(co_heap_near_test_count() == count);
    MapFn again = Reset("the settle line names a target out of searches", HOOKED, 0, 0);
    for (int k = 0; k < 6; k++) { Global(HUD_GLOBAL, 0xFFFF800000000000ull + (uintptr_t)k * 0x100000000ull); Mapped(again, g_first); }
    co_heap_near_settle(g_first);
    CHECK(fake_host_logged("the HUD NOT SERVED (no more searches)"));
    VirtualFree((void*)x, 0, MEM_RELEASE);
    VirtualFree((void*)y, 0, MEM_RELEASE);
}

/* Without a constructor hook, startup ends at its deadline (said, with the summary); with one it waits for the
 * constructor. After startup neither the trail nor a large map lays anything. */
static void Deadline(void)
{
    const uintptr_t later = Region(g_first + 0x200000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    const uintptr_t block = Region(g_first + 0xA00000000ull, 0x8347000u, MEM_RESERVE);
    CHECK(later != 0 && block != 0);
    if (later == 0 || block == 0) return;

    MapFn map = Reset("no constructor hook: startup ends at its deadline, said with the summary; nothing laid after it", HOOKED, 0, 0);
    CHECK(Mapped(map, g_first) == g_first && co_heap_near_test_count() == 1);
    co_heap_near_test_expire();
    CHECK(Mapped(map, later) == later && co_heap_near_test_count() == 1);
    CHECK(fake_host_logged("ended at its deadline") && fake_host_logged(" searches took "));
    CHECK(MappedSize(map, block, 0x8347000u) == block);
    co_heap_near_settle(g_first);
    CHECK(fake_host_logged("0 large maps searched for on their own"));

    map = Reset("a constructor hook in: startup goes on past the deadline until the constructor", HOOKED, 0, 0);
    co_heap_near_constructor_hooked();
    CHECK(Mapped(map, g_first) == g_first && co_heap_near_test_count() == 1);
    co_heap_near_test_expire();
    CHECK(Mapped(map, later) == later && co_heap_near_test_count() == 2 && !fake_host_logged("ended at its deadline"));
    CHECK(MappedSize(map, block, 0x8347000u) == block);
    co_heap_near_settle(g_first);
    CHECK(fake_host_logged("1 large maps searched for on their own"));

    map = Reset("the state built before the hook: its constructor will not settle, so the deadline still ends startup", HOOKED, 0, 0);
    Global(0x346BF98u, g_first);
    co_heap_near_constructor_hooked();
    Global(0x346BF98u, 0);
    CHECK(Mapped(map, g_first) == g_first);
    co_heap_near_test_expire();
    CHECK(Mapped(map, later) == later && fake_host_logged("ended at its deadline"));
    VirtualFree((void*)later, 0, MEM_RELEASE);
    VirtualFree((void*)block, 0, MEM_RELEASE);
}

static DWORD WINAPI FinishLater(LPVOID unused)
{
    (void)unused;
    Sleep(300);
    co_heap_near_test_finish_search();
    return 0;
}

/* The constructor runs while another thread's search for the game object is still in flight: it waits for it. */
static void InFlight(void)
{
    MapFn map = Reset("the constructor waits for a search in flight for the game object, then finds it served", HOOKED, 0, 0);
    (void)map;
    const uintptr_t object = Region(g_first + 0x900000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    CHECK(object != 0);
    if (object == 0) return;
    Global(STATE_GLOBAL, object);
    co_heap_near_test_begin_search(object);
    HANDLE t = CreateThread(NULL, 0, FinishLater, NULL, 0, NULL);
    const ULONGLONG start = GetTickCount64();
    co_heap_near_settle(object + STATE_IN_B);
    CHECK(GetTickCount64() - start >= 250 && fake_host_logged("the game object served by the watch"));
    CHECK(InSpace(co_alloc_near(object + 0x40u, MANAGERS_8)));
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    VirtualFree((void*)object, 0, MEM_RELEASE);
}

/* A free run around the search's centre, neither half 32 MB: still found whole. */
static void Straddle(void)
{
    g_case = "a free run across the centre, too short on either side alone, is tried whole";
    const uintptr_t span = Region(g_first + 0x140000000ull, 0x10000000u, MEM_RESERVE);
    CHECK(span != 0);
    if (span == 0) return;
    VirtualFree((void*)span, 0, MEM_RELEASE);
    /* taken [s, s+64M) and [s+104M, s+168M): a 40 MB free run [s+64M, s+104M) with its centre at s+84M */
    const uintptr_t lowTaken = (uintptr_t)VirtualAlloc((void*)span, 0x4000000u, MEM_RESERVE, PAGE_READWRITE);
    const uintptr_t highTaken = (uintptr_t)VirtualAlloc((void*)(span + 0x6800000u), 0x4000000u, MEM_RESERVE, PAGE_READWRITE);
    CHECK(lowTaken == span && highTaken == span + 0x6800000u);
    const uintptr_t got = co_heap_near_test_reserve_in(span + 0x5400000u, span, span + 0xA800000u, 4);
    CHECK(got == span + 0x6800000u - SIZE); /* the run's top 32 MB */
    if (got != 0) VirtualFree((void*)got, 0, MEM_RELEASE);
    VirtualFree((void*)lowTaken, 0, MEM_RELEASE); VirtualFree((void*)highTaken, 0, MEM_RELEASE);
}

/* Taken just above the centre and just below it; a free hole of 36 MB below that, taken again below the hole. */
static void Hole(void)
{
    g_case = "below the centre, a free run too short at the candidate: the next candidate ends where the run ends";
    const uintptr_t span = Region(g_first + 0x100000000ull, 0x10000000u, MEM_RESERVE); /* 256 MB of room to lay it out */
    CHECK(span != 0);
    if (span == 0) return;
    VirtualFree((void*)span, 0, MEM_RELEASE);
    const uintptr_t c = span + 0x8000000u;
    const uintptr_t above = (uintptr_t)VirtualAlloc((void*)c, 0x4000000u, MEM_RESERVE, PAGE_READWRITE);
    const uintptr_t band = (uintptr_t)VirtualAlloc((void*)(c - 0x400000u), 0x400000u, MEM_RESERVE, PAGE_READWRITE);
    const uintptr_t below = (uintptr_t)VirtualAlloc((void*)(c - 0x5000000u), 0x2800000u, MEM_RESERVE, PAGE_READWRITE);
    CHECK(above == c && band == c - 0x400000u && below == c - 0x5000000u);
    const uintptr_t got = co_heap_near_test_reserve_in(c, c - 0x5000000u, c, 16);
    CHECK(got == c - 0x400000u - SIZE); /* [c - 36 MB, c - 4 MB): the whole hole's top */
    if (got != 0) VirtualFree((void*)got, 0, MEM_RELEASE);
    VirtualFree((void*)above, 0, MEM_RELEASE); VirtualFree((void*)band, 0, MEM_RELEASE); VirtualFree((void*)below, 0, MEM_RELEASE);
}

/* After startup an object no map follows is still seen: the worker looks at the globals every second. And the summary
 * says how long after the install the game object came. */
static void Periodic(void)
{
    MapFn map = Reset("an object made after startup with no map after it: the worker finds it within a second", HOOKED, 0, 0);
    const uintptr_t object = Region(g_first + 0x980000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    const uintptr_t hud = Region(g_first + 0xB80000000ull, 0x100000u, MEM_RESERVE | MEM_COMMIT);
    CHECK(object != 0 && hud != 0);
    if (object == 0 || hud == 0) return;
    Sleep(120);
    Global(STATE_GLOBAL, object);
    CHECK(Mapped(map, g_first) == g_first); /* startup: seen at this map */
    co_heap_near_settle(object + STATE_IN_B);
    CHECK(fake_host_logged("the game object seen ") && fake_host_logged(" ms after the install"));
    const int before = co_heap_near_test_count();
    Global(HUD_GLOBAL, hud); /* no map, no take */
    for (int waited = 0; waited < 3000 && co_heap_near_test_count() == before; waited += 50) Sleep(50);
    CHECK(co_heap_near_test_count() == before + 1);

    Reset("the game object made before the install: said so", HOOKED, object, 0);
    co_heap_near_settle(object + STATE_IN_B);
    CHECK(fake_host_logged("the game object already made at the install"));
    VirtualFree((void*)object, 0, MEM_RELEASE);
    VirtualFree((void*)hud, 0, MEM_RELEASE);
}

/* LuaJIT (not GC64) needs the low 2 GB: no reservation lands below 4 GB, however low the map or the search. */
static void Floor(void)
{
    g_case = "no reservation below 4 GB";
    const uintptr_t got = co_heap_near_test_reserve_in(0x40000000u, 0x10000u, 0x140000000ull, 1024);
    CHECK(got == 0 || got >= 0x100000000ull);
    if (got != 0) VirtualFree((void*)got, 0, MEM_RELEASE);
    CHECK(co_heap_near_test_reserve_in(0x40000000u, 0x10000u, 0xF0000000u, 1024) == 0); /* all of it below the floor */
}

static void Refusals(void)
{
    Reset("rpmalloc's map is not the reviewed code: no hook, blocks searched for as before", NOT_PINNED, 0, 0);
    CHECK(g_fake.hookCount == 0 && fake_host_logged("rpmalloc's map is not the reviewed code"));
    const uintptr_t searched = co_alloc_near(g_first, 0x10000u);
    CHECK(searched != 0 && Committed(searched));
    co_free_near(searched);
    CHECK(!Committed(searched)); /* not a reservation's: released */

    Reset("a hook refused: blocks searched for as before", REFUSED, 0, 0);
    CHECK(fake_host_logged("could not be hooked") && co_heap_near_take(g_first, 0x1000u) == 0);
    CHECK(co_heap_near_install(0, 0) == 0);
}

int main(void)
{
    Beside();
    Crowded();
    Targets();
    LargeMap();
    Switching();
    Deadline();
    Hole();
    Straddle();
    InFlight();
    Periodic();
    Floor();
    Refusals();
    VirtualFree((void*)g_first, 0, MEM_RELEASE);
    co_heap_near_test_reset();
    if (g_failed) { printf("heapnear: %d checks failed\n", g_failed); return 1; }
    printf("heapnear: all cases passed\n");
    return 0;
}
