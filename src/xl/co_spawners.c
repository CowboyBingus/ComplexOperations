/* The director's spawner list per mission (co_spawners.h). HellForge's guard is pinned from its source and the game's
 * code from the image (co_spawner_sites.h). */
#include <string.h>

#include <windows.h>

#include "co_spawners.h"
#include "co_spawner_sites.h"
#include "co_normal.h"

static struct { uint8_t* page; uintptr_t game; int noted; } g_spawners;

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[128];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}
static uintptr_t Leads(const uint8_t* rel, uintptr_t end) { int32_t d; memcpy(&d, rel, 4); return end + (uintptr_t)(intptr_t)d; }

/* HellForge's stub behind its jump, as its source builds it, or 0. */
static uintptr_t Guard(uintptr_t game)
{
    uint8_t site[6], s[sizeof kCoSpawnerGuard];
    if (g_coApi->read(game + CO_SPAWNER_SITE, site, sizeof site) != 0 || site[0] != 0xE9 || site[5] != 0x90) return 0;
    const uintptr_t stub = Leads(site + 1, game + CO_SPAWNER_SITE + 5);
    if (g_coApi->read(stub, s, sizeof s) != 0) return 0;
    if (memcmp(s, kCoSpawnerGuard, 14) != 0 || Leads(s + 14, stub + 18) != game + CO_SPAWNER_ADD || memcmp(s + 18, kCoSpawnerGuard + 18, 3) != 0 ||
        s[25] != 0xE9 || Leads(s + 26, stub + 30) != game + CO_SPAWNER_DONE) return 0;
    return stub;
}

const char* co_spawners_check(uintptr_t game)
{
    if (Same(game + CO_SPAWNER_SITE, kCoSpawnerPin, sizeof kCoSpawnerPin)) return "difficulty 11's guard of the director's spawner list is not in place";
    if (Guard(game) == 0) return "difficulty 11's guard of the director's spawner list is not the one this mod was built for";
    if (!Same(game + CO_SPAWNER_ADD, kCoSpawnerBody, sizeof kCoSpawnerBody)) return "the director's spawner list is not the game's code this mod was made for";
    if (!Same(game + CO_SPAWNER_BASE_LOAD, kCoSpawnerBaseLoad, 7) || !Same(game + CO_SPAWNER_BASE_ADD, kCoSpawnerBaseAdd, 7) ||
        !Same(game + CO_SPAWNER_FIRST, kCoSpawnerFirst, 13) || !Same(game + CO_SPAWNER_NEXT, kCoSpawnerNext, 13))
        return "GuardForce's spawner loop is not the game's code this mod was made for";
    if (!Same(game + CO_SPAWNER_PLACES, kCoSpawnerPlaces, sizeof kCoSpawnerPlaces)) return "GuardForce's spawner positions are not the game's code this mod was made for";
    return NULL;
}

/* A stub being written: `p` bytes so far of the one that will run at `code`. */
typedef struct Stub { uint8_t* out; uintptr_t code; size_t p; } Stub;
static void Bytes(Stub* s, const uint8_t* bytes, size_t size) { memcpy(s->out + s->p, bytes, size); s->p += size; }
/* An instruction that ends in a rel32 to `target`, with `tail` bytes of immediate behind it. */
static void Rip(Stub* s, const uint8_t* head, size_t size, uintptr_t target, size_t tail)
{
    Bytes(s, head, size);
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(s->code + s->p + 4u + tail));
    memcpy(s->out + s->p, &rel, 4); s->p += 4;
}
#define RIP(s, target, tail, ...) do { const uint8_t head_[] = {__VA_ARGS__}; Rip(s, head_, sizeof head_, target, tail); } while (0)
#define BYTES(s, ...) do { const uint8_t bytes_[] = {__VA_ARGS__}; Bytes(s, bytes_, sizeof bytes_); } while (0)

size_t co_spawners_append_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells, uintptr_t guard)
{
    Stub s = {out, code, 0};
    const uint32_t limit = co_level_limit(CO_LIMIT_SPAWNERS, 1);
    Bytes(&s, kCoSpawnerPin, sizeof kCoSpawnerPin);              /* mov eax, [rbx+0x85998]: the count */
    BYTES(&s, 0x85, 0xC0, 0x75, 0x0C);                            /* test eax, eax; jnz: not the mission's first */
    RIP(&s, cells + CO_SPAWNER_CELL_FLAG, 0, 0x8A, 0x0D);         /* mov cl, [flag] (rcx is free: the body sets it) */
    RIP(&s, cells + CO_SPAWNER_CELL_OWN, 0, 0x88, 0x0D);          /* mov [own], cl: the list this mission's count is of */
    RIP(&s, cells + CO_SPAWNER_CELL_OWN, 1, 0x80, 0x3D); BYTES(&s, 0x00); /* cmp byte [own], 0 */
    BYTES(&s, 0x75, 0x05);                                        /* jne: the mod's list */
    RIP(&s, guard, 0, 0xE9);                                      /* HellForge's guard and the manager's list */
    BYTES(&s, 0x3D); Bytes(&s, (const uint8_t*)&limit, 4);        /* cmp eax, 1024 */
    const size_t full = s.p;
    BYTES(&s, 0x0F, 0x83, 0, 0, 0, 0);                            /* jae full */
    Bytes(&s, kCoSpawnerBody, CO_SPAWNER_BODY_SPLIT);             /* the game's append, as far as rcx = count x 0x58 */
    RIP(&s, cells + CO_SPAWNER_CELL_BASED, 0, 0x48, 0x03, 0x0D);  /* add rcx, [based] */
    BYTES(&s, 0x48, 0x29, 0xD9);                                  /* sub rcx, rbx: its stores now reach the mod's entry */
    Bytes(&s, kCoSpawnerBody + CO_SPAWNER_BODY_SPLIT, sizeof kCoSpawnerBody - CO_SPAWNER_BODY_SPLIT); /* the rest, the count's inc too */
    RIP(&s, game + CO_SPAWNER_DONE, 0, 0xE9);
    const int32_t rel = (int32_t)(s.p - (full + 6u));
    memcpy(out + full + 2, &rel, 4);
    RIP(&s, cells + CO_SPAWNER_CELL_LEFT, 0, 0xF0, 0xFF, 0x05);   /* full: lock inc [left out] */
    RIP(&s, game + CO_SPAWNER_DONE, 0, 0xE9);
    return s.p;
}

size_t co_spawners_first_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells)
{
    Stub s = {out, code, 0};
    Bytes(&s, kCoSpawnerFirst, 7);                                /* cmp [r13+0x85998], r12d */
    RIP(&s, game + CO_SPAWNER_EMPTY, 0, 0x0F, 0x86);              /* jbe: no spawner */
    RIP(&s, cells + CO_SPAWNER_CELL_OWN, 1, 0x80, 0x3D); BYTES(&s, 0x00); /* cmp byte [own], 0 */
    RIP(&s, game + CO_SPAWNER_LOOP, 0, 0x0F, 0x84);               /* je: the game's loop on the manager's list */
    RIP(&s, cells + CO_SPAWNER_CELL_BASED, 0, 0x4C, 0x8B, 0x2D);  /* mov r13, [based]: its entry reads reach the mod's list */
    RIP(&s, game + CO_SPAWNER_LOOP, 0, 0xE9);
    return s.p;
}

size_t co_spawners_next_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells)
{
    Stub s = {out, code, 0};
    RIP(&s, cells + CO_SPAWNER_CELL_BASED, 0, 0x4C, 0x3B, 0x2D);  /* cmp r13, [based] */
    BYTES(&s, 0x74, 0x12);                                        /* je: this loop runs on the mod's list */
    Bytes(&s, kCoSpawnerNext, 7);                                 /* cmp r10d, [r13+0x85998] */
    RIP(&s, game + CO_SPAWNER_LOOP, 0, 0x0F, 0x82);               /* jb */
    RIP(&s, game + CO_SPAWNER_LOOP_DONE, 0, 0xE9);
    RIP(&s, game + CO_SPAWNER_WORLD, 0, 0x4C, 0x8B, 0x2D);        /* r13 the manager again, as the builder made it */
    Bytes(&s, kCoSpawnerBaseAdd, 7);
    Bytes(&s, kCoSpawnerNext, 7);
    RIP(&s, game + CO_SPAWNER_LOOP_DONE, 0, 0x0F, 0x83);          /* jae: done, with the manager in r13 */
    RIP(&s, cells + CO_SPAWNER_CELL_BASED, 0, 0x4C, 0x8B, 0x2D);
    RIP(&s, game + CO_SPAWNER_LOOP, 0, 0xE9);
    return s.p;
}

size_t co_spawners_places_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t cells, uintptr_t next)
{
    Stub s = {out, code, 0};
    Bytes(&s, kCoSpawnerPlaces, 7);                               /* mov eax, [r8+0x24c]: the group's places in use */
    RIP(&s, cells + CO_SPAWNER_CELL_BASED, 0, 0x4C, 0x3B, 0x2D);  /* cmp r13, [based] */
    BYTES(&s, 0x75, 0x05);                                        /* jne: the manager's list, the game's store */
    BYTES(&s, 0x83, 0xF8, (uint8_t)CO_SPAWNER_GROUP_PLACES, 0x73, (uint8_t)(sizeof kCoSpawnerPlaces - 7 + 5)); /* cmp eax, 16; jae full */
    Bytes(&s, kCoSpawnerPlaces + 7, sizeof kCoSpawnerPlaces - 7);
    RIP(&s, next, 0, 0xE9);
    RIP(&s, cells + CO_SPAWNER_CELL_FULL, 0, 0xF0, 0xFF, 0x05);   /* full: the 17th would overwrite the places' count */
    RIP(&s, next, 0, 0xE9);
    return s.p;
}

static void Jump(uint8_t* jump, size_t size, uintptr_t at, uintptr_t to)
{
    const int32_t rel = (int32_t)((int64_t)to - (int64_t)(at + 5));
    memset(jump, 0x90, size); jump[0] = 0xE9; memcpy(jump + 1, &rel, 4);
}

const char* co_spawners_install(uintptr_t game)
{
    const struct { uint32_t rva; const uint8_t* bytes; uint8_t size; uint32_t stub; } sites[3] = {
        {CO_SPAWNER_FIRST, kCoSpawnerFirst, 13, CO_SPAWNER_STUB_FIRST}, {CO_SPAWNER_NEXT, kCoSpawnerNext, 13, CO_SPAWNER_STUB_NEXT},
        {CO_SPAWNER_PLACES, kCoSpawnerPlaces, 33, CO_SPAWNER_STUB_PLACES}};
    const char* why;
    uint8_t jump[33];
    DWORD old;
    int i;
    if (g_spawners.page != NULL) return NULL;
    if ((why = co_spawners_check(game)) != NULL) return why;
    const uintptr_t guard = Guard(game);
    const size_t list = (size_t)co_level_limit(CO_LIMIT_SPAWNERS, 1) * CO_SPAWNER_ENTRY;
    uint8_t* page = (uint8_t*)co_alloc_near(game + CO_SPAWNER_FIRST, CO_SPAWNER_LIST + list);
    if (page == NULL) return "no memory near the game for the director's spawner list";
    const uintptr_t code = (uintptr_t)page, cells = code + CO_SPAWNER_CELLS;
    /* r13 or rbx plus an entry's offset: the list's base less the offset the game's code adds */
    const uint64_t based = (uint64_t)(code + CO_SPAWNER_LIST) - CO_SPAWNER_ENTRIES;
    memset(page, 0xCC, CO_SPAWNER_CELLS);
    memcpy(page + CO_SPAWNER_CELLS + CO_SPAWNER_CELL_BASED, &based, 8);
    /* each stub within its place, or none is used */
    const int fit = co_spawners_append_stub(page + CO_SPAWNER_STUB_APPEND, code + CO_SPAWNER_STUB_APPEND, game, cells, guard) <= CO_SPAWNER_STUB_FIRST - CO_SPAWNER_STUB_APPEND &&
        co_spawners_first_stub(page + CO_SPAWNER_STUB_FIRST, code + CO_SPAWNER_STUB_FIRST, game, cells) <= CO_SPAWNER_STUB_NEXT - CO_SPAWNER_STUB_FIRST &&
        co_spawners_next_stub(page + CO_SPAWNER_STUB_NEXT, code + CO_SPAWNER_STUB_NEXT, game, cells) <= CO_SPAWNER_STUB_PLACES - CO_SPAWNER_STUB_NEXT &&
        co_spawners_places_stub(page + CO_SPAWNER_STUB_PLACES, code + CO_SPAWNER_STUB_PLACES, game, cells, code + CO_SPAWNER_STUB_NEXT) <= CO_SPAWNER_STUB_END - CO_SPAWNER_STUB_PLACES;
    if (!fit) { VirtualFree(page, 0, MEM_RELEASE); return "the spawner list's steps do not fit their places"; }
    if (!VirtualProtect(page, CO_SPAWNER_CELLS, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the spawner list's steps could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, CO_SPAWNER_CELLS);
    /* with no list of the mod's in use every stub is the game's code or HellForge's, so each write stands alone */
    for (i = 0; i < 3; i++)
    {
        Jump(jump, sites[i].size, game + sites[i].rva, code + sites[i].stub);
        if (g_coApi->write(game + sites[i].rva, jump, sites[i].size) != 0) break;
    }
    if (i == 3)
    {
        Jump(jump, 6, game + CO_SPAWNER_SITE, code + CO_SPAWNER_STUB_APPEND);
        if (g_coApi->write(game + CO_SPAWNER_SITE, jump, 6) == 0) { g_spawners.page = page; g_spawners.game = game; return NULL; }
    }
    while (i-- > 0) g_coApi->write(game + sites[i].rva, sites[i].bytes, sites[i].size);
    VirtualFree(page, 0, MEM_RELEASE);
    return "the director's spawner list could not be given its steps";
}

void co_spawners_mission(int complex)
{
    if (g_spawners.page == NULL) return;
    volatile uint8_t* cells = g_spawners.page + CO_SPAWNER_CELLS;
    uintptr_t world = 0;
    uint32_t count = 0;
    /* on the ship the mission's manager still holds its count: the next mission's start clears it */
    if (!complex && cells[CO_SPAWNER_CELL_OWN] != 0 && !g_spawners.noted && g_coApi->read(g_spawners.game + CO_SPAWNER_WORLD, &world, 8) == 0 && world != 0 &&
        g_coApi->read(world + CO_SPAWNER_MANAGER + CO_SPAWNER_COUNT, &count, 4) == 0)
    {
        g_spawners.noted = 1;
        co_log("complex operations: the mission's spawner list held %u of %u; so far %ld left out past it and %ld positions past a group's %u places",
            count, co_level_limit(CO_LIMIT_SPAWNERS, 1), (long)*(volatile LONG*)(cells + CO_SPAWNER_CELL_LEFT), (long)*(volatile LONG*)(cells + CO_SPAWNER_CELL_FULL), CO_SPAWNER_GROUP_PLACES);
    }
    if (complex) g_spawners.noted = 0;
    cells[CO_SPAWNER_CELL_FLAG] = complex != 0;
}

volatile long* co_spawners_places_full(void) { return g_spawners.page != NULL ? (volatile long*)(g_spawners.page + CO_SPAWNER_CELLS + CO_SPAWNER_CELL_FULL) : NULL; }

#ifdef CO_TEST
void co_spawners_test_reset(void) { if (g_spawners.page != NULL) VirtualFree(g_spawners.page, 0, MEM_RELEASE); memset(&g_spawners, 0, sizeof g_spawners); }
uint8_t* co_spawners_test_page(void) { return g_spawners.page; }
#endif
