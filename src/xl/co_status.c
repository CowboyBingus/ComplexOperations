/* The status volumes' unit registry in the mod's memory (co_status.h). The sites are every use of the game's list in
 * the image (co_status_sites.h); HellForge has no step at any of them. */
#include <string.h>

#include <windows.h>

#include "co_status.h"
#include "co_status_sites.h"
#include "co_unwind.h"

typedef struct StatusData { uint32_t places; volatile LONG skips; uint64_t* list; } StatusData;
static struct { uint8_t* page; StatusData* data; uint64_t* hash; } g_status;

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[sizeof kCoStatusSites[0].pin];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

const char* co_status_check(uintptr_t game)
{
    for (int i = 0; i < CO_STATUS_SITES; i++)
        if (!Same(game + kCoStatusSites[i].rva, kCoStatusSites[i].pin, kCoStatusSites[i].length))
            return "the status-effect volumes' unit registry is not the game's code this mod was made for";
    return NULL;
}

static size_t Put(uint8_t* out, size_t n, const uint8_t* bytes, size_t size) { memcpy(out + n, bytes, size); return n + size; }
static size_t Rel(uint8_t* out, size_t n, uintptr_t code, uintptr_t target)
{
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(code + n + 4));
    memcpy(out + n, &rel, 4);
    return n + 4;
}
#define PUT(...) do { static const uint8_t b[] = {__VA_ARGS__}; n = Put(out, n, b, sizeof b); } while (0)

size_t co_status_stub(uint8_t* out, uintptr_t code, uintptr_t game, int which, uintptr_t data)
{
    const uintptr_t back = game + kCoStatusSites[which].back, skips = data + 4, list = data + 8;
    size_t n = 0;
    switch (which)
    {
    case 0: /* the count; a full list leaves the unit out, with the destruction callback that would remove it */
        PUT(0x45, 0x8B, 0xB8, 0x98, 0x10, 0x13, 0x00, 0x44, 0x3B, 0x3D); n = Rel(out, n, code, data); /* mov r15d, [r8+0x131098]; cmp r15d, [places] */
        PUT(0x73, 0x09, 0x41, 0x8D, 0x47, 0x01, 0xE9); n = Rel(out, n, code, back);                   /* jae +9; lea eax, [r15+1]; jmp back */
        PUT(0xF0, 0xFF, 0x05); n = Rel(out, n, code, skips);                                          /* lock inc [skips] */
        PUT(0xE9); n = Rel(out, n, code, game + CO_STATUS_SKIP);
        break;
    case 1: /* the append: r9 is loaded anew behind the site */
        PUT(0x4C, 0x8B, 0x0D); n = Rel(out, n, code, list);                                           /* mov r9, [list] */
        PUT(0x43, 0x89, 0x44, 0xF9, 0x04, 0x43, 0x89, 0x1C, 0xF9, 0xE9); n = Rel(out, n, code, back); /* mov [r9+r15*8+4], eax; mov [r9+r15*8], ebx */
        break;
    case 2: /* the insert's slot: none in a full hash, and the unit just appended is taken back */
        PUT(0x4D, 0x85, 0xC0, 0x74, 0x0B, 0x41, 0x89, 0x18, 0x48, 0x8B, 0xCF, 0xE9); n = Rel(out, n, code, back); /* test r8, r8; jz +11; mov [r8], ebx; mov rcx, rdi */
        {
            const uint64_t manager = game + CO_STATUS_MANAGER;
            PUT(0x48, 0xB8); memcpy(out + n, &manager, 8); n += 8;                                    /* mov rax, &M */
        }
        PUT(0x48, 0x8B, 0x00, 0xFF, 0x88, 0x98, 0x10, 0x13, 0x00, 0xF0, 0xFF, 0x05); n = Rel(out, n, code, skips); /* mov rax, [rax]; dec dword [rax+0x131098]; lock inc [skips] */
        PUT(0xE9); n = Rel(out, n, code, game + CO_STATUS_SKIP);
        break;
    case 3: /* the hit path's read */
        PUT(0x48, 0x8B, 0x0D); n = Rel(out, n, code, list);                                           /* mov rcx, [list] */
        PUT(0x8B, 0x4C, 0xC1, 0x04, 0xE9); n = Rel(out, n, code, back);                               /* mov ecx, [rcx+rax*8+4] */
        break;
    case 4: /* the removal: the last unit into the hole; r9 is loaded anew behind the site */
        PUT(0x4C, 0x8B, 0x0D); n = Rel(out, n, code, list);                                           /* mov r9, [list] */
        PUT(0x4D, 0x8B, 0x14, 0xC1, 0x33, 0xD2, 0x4D, 0x89, 0x14, 0xF9, 0xE9); n = Rel(out, n, code, back); /* mov r10, [r9+rax*8]; xor edx, edx; mov [r9+rdi*8], r10 */
        break;
    case 5: /* 0x13CC3E0's append and hash insert, where the game calls its insert: rsp aligned, the volatile registers dead behind */
        {
            const uint64_t append = (uint64_t)(uintptr_t)co_status_append;
            PUT(0x48, 0x89, 0xD1, 0x89, 0xDA, 0x4D, 0x89, 0xC8, 0x48, 0xB8); memcpy(out + n, &append, 8); n += 8; /* mov rcx, rdx (M); mov edx, ebx; mov r8, r9; mov rax, append */
        }
        PUT(0xFF, 0xD0, 0x85, 0xC0, 0x0F, 0x84); n = Rel(out, n, code, game + CO_STATUS_APPEND_SKIP); /* call rax; test eax, eax; jz the function's end */
        PUT(0xE9); n = Rel(out, n, code, back);
        break;
    default: /* 0x13CBFB0's read: r11 (M) is not read behind the site */
        PUT(0x4C, 0x8B, 0x1D); n = Rel(out, n, code, list);                                           /* mov r11, [list] */
        PUT(0x41, 0x8B, 0x44, 0xC3, 0x04, 0xE9); n = Rel(out, n, code, back);                         /* mov eax, [r11+rax*8+4] */
        break;
    }
    while (n < CO_STATUS_STUB) out[n++] = 0xCC;
    return CO_STATUS_STUB;
}

/* The slot the game's insert takes (0x172F790): the first of `places` from id x factor that is empty or the id's. */
static uint32_t* Slot(uint64_t* table, uint32_t places, uint32_t empty, uint32_t factor, uint32_t id)
{
    for (uint32_t probe = 0; probe < places; probe++)
    {
        uint32_t* slot = (uint32_t*)&table[(probe + factor * id) & (places - 1u)];
        if (slot[0] == empty || slot[0] == id) return slot;
    }
    return NULL;
}

int co_status_append(uint64_t manager, uint32_t id, uint64_t unit)
{
    uint8_t* m = (uint8_t*)(uintptr_t)manager;
    StatusData* d = g_status.data;
    uint32_t* count = (uint32_t*)(m + CO_STATUS_COUNT);
    uint32_t* slot = d == NULL || *count >= d->places ? NULL :
        Slot(*(uint64_t**)(m + CO_STATUS_POINTER), *(uint32_t*)(m + CO_STATUS_PLACES), *(uint32_t*)(m + CO_STATUS_EMPTY), *(uint32_t*)(m + CO_STATUS_FACTOR), id);
    if (slot == NULL) { if (d != NULL) InterlockedIncrement(&d->skips); return 0; }
    uint32_t* entry = (uint32_t*)&d->list[*count];
    entry[0] = id; entry[1] = *(uint32_t*)((uint8_t*)(uintptr_t)unit + 0x1C); /* the setting, as 0x13CC40B reads it */
    slot[0] = id; slot[1] = (*count)++;
    return 1;
}

static void Release(uint8_t* page, uint64_t* list, uint64_t* hash)
{
    if (page != NULL) VirtualFree(page, 0, MEM_RELEASE);
    if (list != NULL) VirtualFree(list, 0, MEM_RELEASE);
    if (hash != NULL) VirtualFree(hash, 0, MEM_RELEASE);
}

const char* co_status_install(uintptr_t game)
{
    const char* why;
    uint64_t m = 0;
    uint32_t count = 0;
    DWORD old;
    if (g_status.page != NULL) return NULL;
    if ((why = co_status_check(game)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game + kCoStatusSites[0].rva, 0x2000);
    uint64_t* list = (uint64_t*)VirtualAlloc(NULL, CO_STATUS_LIST * 8u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint64_t* hash = (uint64_t*)VirtualAlloc(NULL, CO_STATUS_HASH * 8u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (page == NULL || list == NULL || hash == NULL) { Release(page, list, hash); return "no memory for the status-effect volumes' unit registry"; }
    StatusData* data = (StatusData*)(page + 0x1000);
    data->places = CO_STATUS_LIST; data->skips = 0; data->list = list;
    for (int i = 0; i < CO_STATUS_SITES; i++) co_status_stub(page + CO_STATUS_STUB * (unsigned)i, (uintptr_t)page + CO_STATUS_STUB * (unsigned)i, game, i, (uintptr_t)data);
    /* the units registered so far stay found: the stubs read the mod's list from their first run */
    if (g_coApi->read(game + CO_STATUS_MANAGER, &m, 8) != 0 || (m != 0 && (g_coApi->read((uintptr_t)m + CO_STATUS_COUNT, &count, 4) != 0 || count > CO_STATUS_OWN ||
        g_coApi->read((uintptr_t)m + CO_STATUS_GAME_LIST, list, (size_t)count * 8u) != 0)))
    { Release(page, list, hash); return "the status-effect volumes' unit registry could not be read"; }
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { Release(page, list, hash); return "the status-effect volumes' steps could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    g_status.data = data; /* the sixth stub's append reads it */
    for (int i = 0; i < CO_STATUS_SITES; i++)
    {
        uint8_t jump[sizeof kCoStatusSites[0].pin];
        memset(jump, 0x90, sizeof jump); jump[0] = 0xE9; Rel(jump, 1, game + kCoStatusSites[i].rva, (uintptr_t)page + CO_STATUS_STUB * (unsigned)i);
        if (g_coApi->write(game + kCoStatusSites[i].rva, jump, kCoStatusSites[i].length) == 0) continue;
        while (i-- > 0) g_coApi->write(game + kCoStatusSites[i].rva, kCoStatusSites[i].pin, kCoStatusSites[i].length);
        g_status.data = NULL;
        Release(page, list, hash);
        return "the status-effect volumes' unit registry could not be enlarged";
    }
    /* the sixth stub calls C without a frame of its own: a stack walk from there unwinds as 0x13CC3E0 at the site */
    static const CoUnwindRange sixth[1] = {{CO_STATUS_STUB * 5u, CO_STATUS_STUB, 0, 0, {0}}};
    (void)co_unwind_block(page, sixth, 1, game + kCoStatusSites[5].rva, page + 0x1800, 0x800);
    g_status.page = page; g_status.hash = hash;
    co_status_hash(game);
    return NULL;
}

int co_status_hash(uintptr_t game)
{
    uint64_t m = 0, table = 0;
    uint32_t head[3] = {0, 1, 0}, count = 0; /* places, the empty key, the factor */
    if (g_status.page == NULL || g_coApi->read(game + CO_STATUS_MANAGER, &m, 8) != 0 || m == 0) return 0;
    if (g_coApi->read((uintptr_t)m + CO_STATUS_POINTER, &table, 8) != 0 || g_coApi->read((uintptr_t)m + CO_STATUS_PLACES, head, sizeof head) != 0 ||
        g_coApi->read((uintptr_t)m + CO_STATUS_COUNT, &count, 4) != 0) return 0;
    /* only the game's own table is replaced: inline, 0x1000 places, empty key 0 (a registry the game set up anew included) */
    if (table != m + CO_STATUS_INLINE || head[0] != CO_STATUS_OWN || head[1] != 0 || count > CO_STATUS_LIST) return 0;
    memset(g_status.hash, 0, CO_STATUS_HASH * 8u);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t id = (uint32_t)g_status.data->list[i];
        uint32_t* slot = Slot(g_status.hash, CO_STATUS_HASH, 0, head[2], id);
        if (slot != NULL) { slot[0] = id; slot[1] = i; }
    }
    /* the pointer first: a reader loads the places first, and 0x1000 places of the larger table are all inside it */
    *(volatile uint64_t*)(uintptr_t)(m + CO_STATUS_POINTER) = (uint64_t)(uintptr_t)g_status.hash;
    MemoryBarrier();
    *(volatile uint32_t*)(uintptr_t)(m + CO_STATUS_PLACES) = CO_STATUS_HASH;
    return 1;
}

uint32_t co_status_skips(void) { return g_status.data != NULL ? (uint32_t)g_status.data->skips : 0; }

#ifdef CO_TEST
void co_status_test_reset(void) { memset(&g_status, 0, sizeof g_status); }
const uint8_t* co_status_test_page(void) { return g_status.page; }
const uint64_t* co_status_test_list(void) { return g_status.data != NULL ? g_status.data->list : NULL; }
const uint64_t* co_status_test_hash(void) { return g_status.hash; }
#endif
