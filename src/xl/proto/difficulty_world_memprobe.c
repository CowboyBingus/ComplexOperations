/* Memory probe (research, HELLFORGE_XL_MEMPROBE=1): names the owners of an extra large map's peak memory. At every
 * memory note it adds the allocators that changed, the callers that grew, rpmalloc's counters and heaps, the render
 * device's per-context copies, Havok's adapter, the nav worlds and the moving tiles; around each generator call of
 * SpawnLevelLayers what that call kept. It only reads: SafeRead, rpmalloc's statistics leaf 0x66BE20, thread names.
 * Each layout was read in the build 25480438 dumps; its code is pinned, so a moved field leaves its line out. */

/* rpmalloc (custom 1.4: 96 size classes, 15 large classes, 64 KiB spans; heap_t 0x2080, 0x669068) */
#define MP_RP_STATS 0x66BE20u     /* (stats*): +0 mapped, +8 peak, +0x10 global cache, +0x18 huge, +0x20 peak, +0x28 / +0x30 totals */
#define MP_RP_HEAPS 0x27B1100u    /* heap_t*[47] by id % 47, next heap +0x728 (0x6690BA, 0x66876B) */
#define MP_RP_SIZES 0x27B0DF0u    /* {u32 block size, u16 count, u16 class}[96] (0x667E99) */
#define MP_RP_PAGE 0x27B10F0u     /* page size (u64) */
#define MP_HEAP_BYTES 0x2080u
#define MP_SPAN (64ll << 10)
/* the render device (dev = the render thread's batch-end state - 0x1340, 0x6CEDA1) */
#define MP_TRACKING 0x168B5A0u    /* the tracking allocator's vtable (0x5C2CC5): name +8, parent +0x20, live +0x30, peak +0x40 */
#define MP_DEV_NAME 0x1694890u    /* "D3D12RenderDevice", its node at dev+0x4A0 (0x6C111A..0x6C1137) */
/* Havok's hk_main_system adapter [[0x27BA848]+0x48] (0x78ADF7): vtable 0x169E5E8, node +0x10, peak +0x18, live +0x20 */
#define MP_HK_CONTEXT 0x27BA848u
#define MP_HK_VTABLE 0x169E5E8u
/* the nav worlds: pointers [0x27C30B8], count [0x27C30C0] (scheduler 0x7B8210..0x7B8242) */
#define MP_NAV_WORLDS 0x27C30B8u

static const struct { int exe; uint32_t rva; const char* hex; int group; } kMpPins[] = {
    {1, MP_RP_STATS, "48 C7 41 10 00 00 00 00 4C 8B D1 48 63 15 52 54 CF 01", 0},
    {1, 0x66BF43u, "48 C1 E2 10 49 01 52 10 C3", 0},
    {1, 0x669068u, "41 B8 80 20 00 00", 1}, {1, 0x6690BAu, "4A 8B 04 C1 48 89 83 28 07 00 00 4A 89 1C C1", 1},
    {1, 0x6688A2u, "48 8B 83 08 06 00 00", 1}, {1, 0x668915u, "48 8D BB 68 06 00 00", 1},
    {1, 0x668CE6u, "F0 45 0F C1 8A 68 0D 00 00", 1}, {1, 0x669A6Eu, "F0 0F C1 94 08 C0 0F 00 00", 1},
    {1, 0x66A250u, "48 8B 81 10 07 00 00", 1}, {1, 0x6684A0u, "8B 42 0C 48 8B 32", 1}, {1, 0x667E99u, "8B 04 C1 41 89 40 24", 1},
    {1, 0x6CD17Cu, "49 8B 9F B0 03 00 00 41 8B B1 C8 13 00 00 3B 73 0C", 2}, {1, 0x6CC700u, "48 8D 8F F8 06 00 00", 2},
    {1, 0x6C727Bu, "48 8D 9E 18 07 00 00", 2}, {1, 0x6C72DDu, "8B 96 30 07 00 00", 2}, {1, 0x6EFBEAu, "48 8D 8F 88 01 00 00 8B D3 E8", 2},
    {1, 0x6CEDA1u, "48 8D 8F 40 13 00 00 E8", 2},
    {1, 0x817196u, "F0 48 0F C1 4B 20 48 03 CF 48", 3}, {1, 0x78ADE5u, "48 8B 05 5C FA 02 02 BA 10 05 00 00 41 B8 08 00 00 00 48 89 78 48", 3},
    {1, 0x7B8238u, "4C 8B 3D 79 AE 00 02", 4}, {1, 0x7B744Cu, "89 86 C4 00 00 00", 4}, {1, 0x7B86ACu, "44 01 A7 90 00 00 00", 4},
    {1, 0x7B86BAu, "FF 87 94 00 00 00", 4}, {1, 0x7B829Au, "49 8B 8B 48 67 00 00 44 8B 4C 0A 5C", 4}, {1, 0x7B8286u, "41 0F B7 8B 36 67 00 00", 4},
    {1, 0x7B924Fu, "89 43 60 83 4B 5C 08", 4},
    {1, 0x1D95EFu, "8B 41 3C FF C8 39 41 38", 5}, {1, 0x30F0D9u, "48 8B 89 F8 01 23 00", 5},
    {0, 0xFB1A83u, "4C 8B B9 C8 01 00 00 49 8B D8 8B 81 D0 01 00 00", 6}, {0, 0xFB1B29u, "8B 85 E0 01 00 00", 6},
    {0, 0xC19676u, "48 8D 4E 08 E8", 6}, {0, 0xC17F2Du, "48 8D 4E 08 48 8B D6 E8", 6},
};
enum { MP_RP, MP_HEAPS, MP_DEV, MP_HK, MP_NAV, MP_RO, MP_ROADS, MP_GROUPS };
static const char* const kMpGroupName[MP_GROUPS] = {"rpmalloc statistics", "rpmalloc heaps", "render device", "Havok adapter", "nav worlds",
    "RenderObject handles", "city roads (op 0xA8)"};

static struct
{
    int pins[MP_GROUPS];        /* 1: the group's code is the reviewed one */
    LONG64 walkCommit;          /* the commit at the last note that walked the heaps */
    uintptr_t world; LONG worldLevel; /* the level's world, read from the generation context ([ctx+0x40]) */
    uintptr_t defaultNode;      /* the DefaultAllocator tracking node, once found */
    struct { LONG levels; uint32_t calls, lines, mission, other, deferred; LONG64 commit, da; int done; } spawn;
} g_mp;

static LONG64 MemProbeCommit(void)
{
    static BOOL (WINAPI* info)(HANDLE, void*, DWORD);
    static int looked;
    if (!looked) { HMODULE k = GetModuleHandleA("kernel32.dll"); if (k) info = (BOOL (WINAPI*)(HANDLE, void*, DWORD))(void*)GetProcAddress(k, "K32GetProcessMemoryInfo"); looked = 1; }
    WorldMemCounters c; memset(&c, 0, sizeof c); c.cb = sizeof c;
    if (info == NULL || !info(GetCurrentProcess(), &c, sizeof c)) return -1;
    return (LONG64)(c.priv ? c.priv : c.page);
}

static void MemProbeName(uintptr_t at, char* out, size_t size)
{
    snprintf(out, size, "?");
    if (at == 0 || SafeRead(at, out, size - 1)) { snprintf(out, size, "?"); return; }
    out[size - 1] = 0;
    for (char* c = out; *c; c++) if ((unsigned char)*c < 0x20 || (unsigned char)*c > 0x7E) { *c = 0; break; }
}

/* A tracking node's live bytes, -1 when it does not read as one. */
static LONG64 MemProbeNodeBytes(uintptr_t exe, uintptr_t node)
{
    uintptr_t vt = 0; LONG64 b = -1;
    if (node == 0 || SafeRead(node, &vt, 8) || vt != exe + MP_TRACKING || SafeRead(node + 0x30, &b, 8)) return -1;
    return b;
}

/* The DefaultAllocator node, by name, walking the list as WorldAllocNote does (its lock held). */
static uintptr_t MemProbeDefaultNode(uintptr_t exe)
{
    if (g_mp.defaultNode != 0 && MemProbeNodeBytes(exe, g_mp.defaultNode) >= 0) return g_mp.defaultNode;
    uintptr_t list = 0;
    if (g_allocOn <= 0 || SafeRead(exe + 0x1B13180u, &list, 8) || list == 0) return 0;
    EnterCriticalSection((CRITICAL_SECTION*)(list + 8));
    int walked = 0;
    for (uintptr_t node = *(volatile uintptr_t*)list; node != 0 && walked < 65536; node = *(volatile uintptr_t*)(node + 0x10), walked++)
    {
        if (*(volatile uintptr_t*)node != exe + MP_TRACKING) continue;
        char name[40]; MemProbeName(*(volatile uintptr_t*)(node + 8), name, sizeof name);
        if (!strcmp(name, "DefaultAllocator")) { g_mp.defaultNode = node; break; }
    }
    LeaveCriticalSection((CRITICAL_SECTION*)(list + 8));
    return g_mp.defaultNode;
}

/* ---- The allocator walk: every tracking node that changed by 1 MB or more, with its parent's name ---------------- */
static int MemProbeAllocList(const char* where, uintptr_t exe, const uintptr_t* nodes, const LONG64* bytes, const LONG64* deltas, int n, int others, LONG64 total)
{
    enum { MP_LISTED = 512 };
    static int pick[MP_LISTED];
    int k = 0;
    for (int i = 0; i < n && k < MP_LISTED; i++) if (deltas[i] >= (1ll << 20) || deltas[i] <= -(1ll << 20)) pick[k++] = i;
    for (int a = 1; a < k; a++) /* largest change first */
    {
        int v = pick[a], b = a - 1;
        LONG64 m = deltas[v] < 0 ? -deltas[v] : deltas[v];
        while (b >= 0 && (deltas[pick[b]] < 0 ? -deltas[pick[b]] : deltas[pick[b]]) < m) { pick[b + 1] = pick[b]; b--; }
        pick[b + 1] = v;
    }
    char line[960]; size_t at = 0;
    at = (size_t)snprintf(line, sizeof line, "difficulty: memprobe: allocators (%s): %d tracking (%d other nodes), %lld MB live in all; %d changes of 1 MB or more:%s",
        where, n, others, (long long)(total >> 20), k, k ? "" : " none");
    for (int m = 0; m < k; m++)
    {
        uintptr_t node = nodes[pick[m]], nameAt = 0, parent = 0, pvt = 0, pname = 0;
        char name[40], up[40] = "";
        SafeRead(node + 8, &nameAt, 8); MemProbeName(nameAt, name, sizeof name);
        if (!SafeRead(node + 0x20, &parent, 8) && parent != 0 && !SafeRead(parent, &pvt, 8) && pvt == exe + MP_TRACKING && !SafeRead(parent + 8, &pname, 8))
            MemProbeName(pname, up, sizeof up);
        char item[120];
        int len = snprintf(item, sizeof item, "%s %s%s%s %+.1f MB (now %lld)", m ? ";" : "", name, up[0] ? "<" : "", up, (double)deltas[pick[m]] / 1048576.0,
            (long long)(bytes[pick[m]] >> 20));
        if (at + (size_t)len >= sizeof line - 1)
        {
            Note("%s", line);
            at = (size_t)snprintf(line, sizeof line, "difficulty: memprobe: allocators (%s), more:", where);
        }
        memcpy(line + at, item, (size_t)len + 1); at += (size_t)len;
    }
    Note("%s", line);
    return k;
}

/* ---- rpmalloc's heaps: caches, spans, per-class fragmentation and the deferred-span lists ------------------------ */
typedef struct
{
    uintptr_t heap; uint32_t id, tid; int orphan, listBad;
    LONG64 cache, spans, deferred, live, classSpans, frag, list, listHuge; uint32_t listSpans;
    struct { uint32_t c, size; LONG64 frag; } top[3];
} MemProbeHeap;
typedef struct
{
    int heaps, orphans, bad;
    LONG64 cache, spans, deferred, live, classSpans, frag, list, listHuge; uint32_t listSpans;
} MemProbeHeapSum;
#define MP_HEAPS_MAX 256
static MemProbeHeap g_mpHeap[MP_HEAPS_MAX];

static const char* MemProbeThreadName(DWORD tid)
{
    static struct { DWORD tid; char name[32]; } seen[128];
    static int count;
    static HRESULT (WINAPI* describe)(HANDLE, PWSTR*);
    static int looked;
    for (int i = 0; i < count; i++) if (seen[i].tid == tid) return seen[i].name;
    if (!looked) { HMODULE k = GetModuleHandleA("kernel32.dll"); if (k) describe = (HRESULT (WINAPI*)(HANDLE, PWSTR*))(void*)GetProcAddress(k, "GetThreadDescription"); looked = 1; }
    if (count >= 128) return "";
    seen[count].tid = tid; seen[count].name[0] = 0;
    HANDLE t = (describe != NULL && tid != 0) ? OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid) : NULL;
    PWSTR w = NULL;
    if (t != NULL && SUCCEEDED(describe(t, &w)) && w != NULL)
    {
        int i = 0;
        for (; w[i] != 0 && i < 31; i++) seen[count].name[i] = (w[i] >= 0x20 && w[i] < 0x7F && w[i] != '"') ? (char)w[i] : '?';
        seen[count].name[i] = 0;
        LocalFree(w);
    }
    if (t != NULL) CloseHandle(t);
    return seen[count++].name;
}

/* One heap from its copy: the thread cache (1-span count +0x608, large caches +0x748 + 0x70 (sc - 2), sc = 2..15), the
 * spans in use and deferred (span_use[i] +0xD68 + 0x28 i: current +0, deferred +8), per class c < 96 the live blocks
 * (+0xFC0 + 0x2C c) and spans (+0xFD0 + 0x2C c), and the +0x710 list (next [span], class [span+0xC], count [span+0x2C]). */
static void MemProbeHeapOf(const uint8_t* hb, uintptr_t heap, const uint32_t* blockSize, LONG64 page, MemProbeHeap* o)
{
    memset(o, 0, sizeof *o);
    o->heap = heap;
    uint64_t owner, cache1, list; memcpy(&owner, hb, 8); memcpy(&o->id, hb + 0x738, 4); memcpy(&cache1, hb + 0x608, 8); memcpy(&list, hb + 0x710, 8);
    o->orphan = owner == UINT64_MAX;
    uint64_t tid = 0;
    if (!o->orphan && owner != 0 && !SafeRead((uintptr_t)owner + 0x48, &tid, 8)) o->tid = (uint32_t)tid; /* TEB ClientId.UniqueThread */
    o->cache = (LONG64)(cache1 <= 32 ? cache1 : 32) * MP_SPAN;
    for (int sc = 2; sc <= 15; sc++)
    {
        uint64_t c; memcpy(&c, hb + 0x748 + 0x70 * (sc - 2), 8);
        o->cache += (LONG64)(c <= 13 ? c : 13) * sc * MP_SPAN;
    }
    for (int i = 0; i < 15; i++)
    {
        int32_t cur, def; memcpy(&cur, hb + 0xD68 + 0x28 * i, 4); memcpy(&def, hb + 0xD70 + 0x28 * i, 4);
        o->spans += (LONG64)cur * (i + 1) * MP_SPAN; o->deferred += (LONG64)def * (i + 1) * MP_SPAN;
    }
    for (uint32_t c = 0; c < 96; c++)
    {
        int32_t blocks, spans; memcpy(&blocks, hb + 0xFC0 + 0x2C * c, 4); memcpy(&spans, hb + 0xFD0 + 0x2C * c, 4);
        LONG64 live = blocks > 0 ? (LONG64)blocks * blockSize[c] : 0, in = spans > 0 ? (LONG64)spans * MP_SPAN : 0, frag = in > live ? in - live : 0;
        o->live += live; o->classSpans += in; o->frag += frag;
        for (int t = 0; t < 3; t++)
            if (frag > o->top[t].frag)
            {
                for (int u = 2; u > t; u--) o->top[u] = o->top[u - 1];
                o->top[t].c = c; o->top[t].size = blockSize[c]; o->top[t].frag = frag;
                break;
            }
    }
    /* lock-free and changed by its owner while it is read: the walk is capped, and a span that no longer reads ends it */
    for (uint32_t n = 0; list != 0 && n < 4096; n++)
    {
        uint8_t s[0x30];
        if (SafeRead((uintptr_t)list, s, sizeof s)) { o->listBad = 1; break; }
        uint32_t cls, count; memcpy(&cls, s + 0xC, 4); memcpy(&count, s + 0x2C, 4);
        LONG64 b = cls == UINT32_MAX ? (LONG64)count * page : cls < 96 ? MP_SPAN : (LONG64)count * MP_SPAN;
        if (count > (1u << 20) && cls >= 96) { o->listBad = 1; break; }
        o->list += b; o->listSpans++;
        if (cls == UINT32_MAX) o->listHuge += b;
        memcpy(&list, s, 8);
    }
}

/* Every heap of the hash, summed; `lines` notes the heaps that hold 4 MB or more outside live blocks. */
static int MemProbeHeaps(uintptr_t exe, MemProbeHeapSum* sum, int lines)
{
    memset(sum, 0, sizeof *sum);
    uint32_t blockSize[96]; uint8_t table[96 * 8]; LONG64 page = 0; uintptr_t head[47];
    if (SafeRead(exe + MP_RP_SIZES, table, sizeof table) || SafeRead(exe + MP_RP_PAGE, &page, 8) || SafeRead(exe + MP_RP_HEAPS, head, sizeof head)) return -1;
    for (int c = 0; c < 96; c++) memcpy(&blockSize[c], table + 8 * c, 4);
    if (page <= 0 || page > (1 << 21)) page = 4096;
    static uint8_t hb[MP_HEAP_BYTES];
    int n = 0;
    for (int b = 0; b < 47; b++)
        for (uintptr_t h = head[b], chain = 0; h != 0 && chain < 64; chain++)
        {
            if (SafeRead(h, hb, sizeof hb)) { sum->bad++; break; }
            MemProbeHeap one;
            MemProbeHeapOf(hb, h, blockSize, page, &one);
            sum->heaps++; sum->orphans += one.orphan;
            sum->cache += one.cache; sum->spans += one.spans; sum->deferred += one.deferred; sum->live += one.live; sum->classSpans += one.classSpans;
            sum->frag += one.frag; sum->list += one.list; sum->listHuge += one.listHuge; sum->listSpans += one.listSpans;
            if (n < MP_HEAPS_MAX) g_mpHeap[n++] = one;
            memcpy(&h, hb + 0x728, 8);
        }
    if (!lines) return n;
    for (int shown = 0; shown < 24; shown++) /* the heaps with the most memory outside live blocks */
    {
        int best = -1; LONG64 bestHeld = (4ll << 20) - 1;
        for (int i = 0; i < n; i++)
        {
            LONG64 held = g_mpHeap[i].cache + g_mpHeap[i].frag + g_mpHeap[i].list;
            if (g_mpHeap[i].heap != 0 && held > bestHeld) { bestHeld = held; best = i; }
        }
        if (best < 0) break;
        const MemProbeHeap* o = &g_mpHeap[best];
        char classes[160] = ""; size_t at = 0;
        for (int t = 0; t < 3 && o->top[t].frag >= (1ll << 20); t++)
            at += (size_t)snprintf(classes + at, sizeof classes - at, "%s class %u (%u B) %.1f MB", t ? "," : "", o->top[t].c, o->top[t].size, (double)o->top[t].frag / 1048576.0);
        Note("difficulty: memprobe: rpmalloc heap %u (%sthread %u \"%s\"): spans in use %.1f MB, small and medium blocks %.1f MB live in %.1f MB of spans "
            "(fragmentation %.1f MB:%s), thread cache %.1f MB, deferred spans %.1f MB, +0x710 list %u spans %.1f MB (%.1f MB huge)%s",
            o->id, o->orphan ? "orphaned, " : "", o->tid, o->orphan ? "" : MemProbeThreadName(o->tid), (double)o->spans / 1048576.0, (double)o->live / 1048576.0,
            (double)o->classSpans / 1048576.0, (double)o->frag / 1048576.0, at ? classes : " none over 1 MB", (double)o->cache / 1048576.0,
            (double)o->deferred / 1048576.0, o->listSpans, (double)o->list / 1048576.0, (double)o->listHuge / 1048576.0, o->listBad ? " (list read cut short)" : "");
        g_mpHeap[best].heap = 0;
    }
    return n;
}

/* ---- The render device: version maps, pooled contexts and their copies (0x6CD17C..0x6CD241) --------------------- */
typedef struct
{
    uint32_t map[3], changed[3], made, free, flight, biggest, badX, commands;
    LONG64 copies, other, node, nodePeak, pool;
} MemProbeD3d;

static int MemProbeDevice(uintptr_t exe, uintptr_t dev, MemProbeD3d* o)
{
    memset(o, 0, sizeof *o);
    uintptr_t vt = 0, name = 0;
    if (dev == 0 || SafeRead(dev + 0x4A0, &vt, 8) || SafeRead(dev + 0x4A8, &name, 8) || vt != exe + MP_TRACKING || name != exe + MP_DEV_NAME) return -1;
    uint32_t m[8];
    if (!SafeRead(dev + 0x13C8, m, 0x20)) { o->map[0] = m[0]; o->map[1] = m[6]; o->map[2] = m[7]; }        /* size, used +0x18, buckets +0x1C */
    if (!SafeRead(dev + 0x13F0, m, 0x20)) { o->changed[0] = m[0]; o->changed[1] = m[6]; o->changed[2] = m[7]; }
    SafeRead(dev + 0x700, &o->made, 4); SafeRead(dev + 0x718, &o->free, 4); SafeRead(dev + 0x730, &o->flight, 4);
    SafeRead(dev + 0x4D0, &o->node, 8); SafeRead(dev + 0x4E0, &o->nodePeak, 8); SafeRead(dev + 0x1370, &o->pool, 8);
    uintptr_t xs = 0;
    uint32_t made = o->made > 65536 ? 65536 : o->made;
    if (made != 0 && !SafeRead(dev + 0x708, &xs, 8) && xs != 0)
        for (uint32_t i = 0; i < made; i += 32)
        {
            uintptr_t x[32]; uint32_t k = made - i < 32 ? made - i : 32;
            if (SafeRead(xs + 8ull * i, x, 8ull * k)) { o->badX += k; continue; }
            for (uint32_t j = 0; j < k; j++)
            {
                uint32_t copy[2], table[2]; /* X+8 {count, cap} of 24-byte entries; X+0x188 {count, cap} of u32 */
                if (x[j] == 0 || SafeRead(x[j] + 8, copy, 8) || SafeRead(x[j] + 0x188, table, 8)) { o->badX++; continue; }
                o->copies += (LONG64)copy[1] * 24; o->other += (LONG64)table[1] * 4;
                if (copy[1] > o->biggest) o->biggest = copy[1];
            }
        }
    return 0;
}

/* ---- Havok's hk_main_system adapter (alloc 0x817160: lock xadd +0x20, peak +0x18) -------------------------------- */
static int MemProbeHavok(uintptr_t exe, LONG64* live, LONG64* peak, LONG64* node)
{
    uintptr_t ctx = 0, a = 0, vt = 0, n = 0;
    *live = *peak = *node = -1;
    if (SafeRead(exe + MP_HK_CONTEXT, &ctx, 8) || ctx == 0 || SafeRead(ctx + 0x48, &a, 8) || a == 0 || SafeRead(a, &vt, 8) || vt != exe + MP_HK_VTABLE) return -1;
    SafeRead(a + 0x20, live, 8); SafeRead(a + 0x18, peak, 8);
    if (!SafeRead(a + 0x10, &n, 8)) *node = MemProbeNodeBytes(exe, n);
    return 0;
}

/* ---- The nav worlds: jobs in flight +0x90 (weight) / +0x94, the fill's cells +0xC4, cells W x H at +0x6710, stride
 * +0x6736, array +0x6748; per cell flags +0x5C (the scheduler's test 0x7B82A6..0x7B82B6) and section id +0x60 ------- */
typedef struct { uint32_t index, flags, w, h, weight, jobs, plan, cells, f1, f4, f8, f10, waiting, sections; int32_t highest; } MemProbeNavWorld;

static int MemProbeNav(uintptr_t exe, MemProbeNavWorld* out, int max)
{
    uint32_t count = 0; uintptr_t worlds = 0; int n = 0;
    if (SafeRead(exe + MP_NAV_WORLDS + 8, &count, 4) || SafeRead(exe + MP_NAV_WORLDS, &worlds, 8) || worlds == 0) return -1;
    for (uint32_t i = 0; i < count && i < 16 && n < max; i++)
    {
        uintptr_t nav = 0, cells = 0; uint32_t flags = 0; uint16_t grid[2] = {0, 0}, stride = 0;
        if (SafeRead(worlds + 8ull * i, &nav, 8) || nav == 0 || SafeRead(nav + 0x20, &flags, 4) || !(flags & 1)) continue;
        MemProbeNavWorld* o = &out[n++];
        memset(o, 0, sizeof *o);
        o->index = i; o->flags = flags; o->highest = -1;
        SafeRead(nav + 0x6710, grid, 4); SafeRead(nav + 0x6736, &stride, 2); SafeRead(nav + 0x6748, &cells, 8);
        SafeRead(nav + 0x90, &o->weight, 4); SafeRead(nav + 0x94, &o->jobs, 4); SafeRead(nav + 0xC4, &o->plan, 4);
        o->w = grid[0]; o->h = grid[1];
        if (cells == 0 || stride < 0x64 || (uint32_t)grid[0] * grid[1] > 4096u) continue;
        for (uint32_t c = 0; c < (uint32_t)grid[0] * grid[1]; c++)
        {
            uint32_t cell[2];
            if (SafeRead(cells + (uintptr_t)c * stride + 0x5C, cell, 8)) break;
            o->cells++;
            o->f1 += (cell[0] & 1) != 0; o->f4 += (cell[0] & 4) != 0; o->f8 += (cell[0] & 8) != 0; o->f10 += (cell[0] & 0x10) != 0;
            o->waiting += !(cell[0] & 1) && ((cell[0] & 0x10) || !(cell[0] & 4));
            if ((int32_t)cell[1] != -1) { o->sections++; if ((int32_t)cell[1] > o->highest) o->highest = (int32_t)cell[1]; }
        }
    }
    return n;
}

/* RenderObject handles in use and their capacity ([world+0x2301F8] +0x38 / +0x3C, 0x1D95EF). */
static int MemProbeHandles(uintptr_t world, uint32_t* used, uint32_t* cap)
{
    uintptr_t holder = 0; uint32_t v[2];
    *used = *cap = 0;
    if (world == 0 || SafeRead(world + 0x2301F8u, &holder, 8) || holder == 0 || SafeRead(holder + 0x38, v, 8)) return -1;
    *used = v[0]; *cap = v[1];
    return 0;
}

static const char* MemProbeMoverState(int state)
{
    static const char* const names[] = {"idle", "package", "stamps", "kick", "setup", "lookup", "replay", "teardown", "unstamp", "spawn", "ready", "failing"};
    return state >= 0 && state < (int)(sizeof names / sizeof names[0]) ? names[state] : "?";
}

/* The moving tiles from the mod's own state: each live tile's cell, when a player last kept it and when it expires. */
static void MemProbeTiles(void)
{
    ULONGLONG now = GetTickCount64();
    int waiting = 0;
    for (int k = 0; k < NEAR_TILES_MAX * 2; k++) waiting += g_move.freeing[k].used != 0;
    char text[700]; size_t at = 0; text[0] = 0;
    for (int k = 0; k < NEAR_TILES_MAX && at < sizeof text - 90; k++)
    {
        if (!g_tiles[k].live || g_tiles[k].level != g_world.levels) continue;
        if (k == 0) { at += (size_t)snprintf(text + at, sizeof text - at, "; slot 0 (%d, %d) the drop tile", g_tiles[k].cell[0], g_tiles[k].cell[1]); continue; }
        double ago = g_tiles[k].seen ? (double)(now - g_tiles[k].seen) / 1000.0 : -1.0;
        at += (size_t)snprintf(text + at, sizeof text - at, "; slot %d (%d, %d) kept %.1f s ago, expires in %.1f s", k, g_tiles[k].cell[0], g_tiles[k].cell[1], ago,
            g_tiles[k].seen ? (double)MOVE_KEEP_MS / 1000.0 - ago : 0.0);
    }
    Note("difficulty: memprobe: tiles: %d live, mover %s%s (cell %d, %d; %.1f s in), %d built, %d released, %d failed this level, %d texture sets to free%s",
        NearTilesLive(), MemProbeMoverState(g_move.state), g_move.on ? "" : " (off)", g_move.cell[0], g_move.cell[1],
        g_move.state != MV_IDLE && g_move.started ? (double)(now - g_move.started) / 1000.0 : 0.0, g_move.built, g_move.released, g_move.failed, waiting, text);
}

/* Every memory note while the probe is on (WorldMemoryNote, under g_memProbeNoteLock). */
static void MemProbeNote(const char* where, uint32_t op, uint32_t record, LONG64 commit)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL);
    LARGE_INTEGER t0, t1, hz; QueryPerformanceCounter(&t0);
    LONG64 moved = commit - g_mp.walkCommit;
    int walk = op == 0xFFFFFFFFu || moved >= (64ll << 20) || moved <= -(64ll << 20); /* named notes and 64 MB of change walk the heaps */
    MemProfReportProbe(walk);
    WorldAllocNote(where);
    char rp[300] = "rpmalloc statistics not read", hk[160] = "hk_main_system not read";
    if (g_mp.pins[MP_RP])
    {
        LONG64 s[7] = {0};
        ((void (*)(void*))(exe + MP_RP_STATS))(s);
        snprintf(rp, sizeof rp, "rpmalloc mapped %lld MB (peak %lld), global cache %lld, huge %lld (peak %lld), mapped in all %lld, unmapped in all %lld",
            (long long)(s[0] >> 20), (long long)(s[1] >> 20), (long long)(s[2] >> 20), (long long)(s[3] >> 20), (long long)(s[4] >> 20), (long long)(s[5] >> 20),
            (long long)(s[6] >> 20));
    }
    LONG64 hl, hp, hn;
    if (g_mp.pins[MP_HK] && MemProbeHavok(exe, &hl, &hp, &hn) == 0)
        snprintf(hk, sizeof hk, "hk_main_system adapter %lld MB live (peak %lld), its node %lld MB", (long long)(hl >> 20), (long long)(hp >> 20), (long long)(hn >> 20));
    Note("difficulty: memprobe: %s: %s; %s", where, rp, hk);

    char dv[420] = "render device not seen yet", ro[160] = "", roads[120] = "";
    MemProbeD3d d;
    if (!g_mp.pins[MP_DEV]) snprintf(dv, sizeof dv, "render device not read (code)");
    else if (g_memProbeDevice != 0 && MemProbeDevice(exe, g_memProbeDevice, &d) == 0)
        snprintf(dv, sizeof dv, "render device: version map %u entries (%u used, %u buckets), changed map %u (%u, %u); contexts %u made, %u free, %u in flight; "
            "their copies %.1f MB (largest %u entries), other tables %.1f MB%s; D3D12RenderDevice %lld MB (peak %lld), D3D12DynamicBufferPool %lld MB",
            d.map[0], d.map[1], d.map[2], d.changed[0], d.changed[1], d.changed[2], d.made, d.free, d.flight, (double)d.copies / 1048576.0, d.biggest,
            (double)d.other / 1048576.0, d.badX ? " (some contexts did not read)" : "", (long long)(d.node >> 20), (long long)(d.nodePeak >> 20), (long long)(d.pool >> 20));
    else if (g_memProbeDevice != 0) snprintf(dv, sizeof dv, "render device did not read as one");
    if (op != 0xFFFFFFFFu && g_memProbeContext != 0)
    {
        uintptr_t world = 0;
        if (!SafeRead(g_memProbeContext + 0x40, &world, 8) && world != 0) { g_mp.world = world; g_mp.worldLevel = g_world.levels; }
        if (op == 0xA8 && g_mp.pins[MP_ROADS] && g_memProbeRecord != 0) /* FlattenCityRoads' lists on the record's pass (record + 8) */
        {
            uint32_t a[2] = {0, 0}, b[2] = {0, 0};
            if (!SafeRead(g_memProbeRecord + 8 + 0x1D0, a, 8) && !SafeRead(g_memProbeRecord + 8 + 0x1E0, b, 8))
                snprintf(roads, sizeof roads, "; city roads: %u road units (%u done), %u phase-2 entries (%u done)", a[0], a[1], b[0], b[1]);
        }
    }
    uint32_t used = 0, cap = 0;
    if (g_mp.pins[MP_RO] && g_mp.worldLevel == g_world.levels && MemProbeHandles(g_mp.world, &used, &cap) == 0)
        snprintf(ro, sizeof ro, "; RenderObject handles %u of %u", used, cap);
    Note("difficulty: memprobe: %s%s%s", dv, ro, roads);

    if (walk)
    {
        g_mp.walkCommit = commit;
        MemProbeHeapSum h;
        if (g_mp.pins[MP_HEAPS] && MemProbeHeaps(exe, &h, 1) >= 0)
            Note("difficulty: memprobe: rpmalloc heaps: %d (%d orphaned, %d did not read): spans in use %lld MB, small and medium %lld MB live in %lld MB of spans "
                "(fragmentation %lld MB), thread caches %lld MB, deferred spans %lld MB, +0x710 lists %u spans %lld MB (%lld MB huge)", h.heaps, h.orphans, h.bad,
                (long long)(h.spans >> 20), (long long)(h.live >> 20), (long long)(h.classSpans >> 20), (long long)(h.frag >> 20), (long long)(h.cache >> 20),
                (long long)(h.deferred >> 20), h.listSpans, (long long)(h.list >> 20), (long long)(h.listHuge >> 20));
        MemProbeNavWorld nav[4];
        int worlds = g_mp.pins[MP_NAV] ? MemProbeNav(exe, nav, 4) : -1;
        for (int i = 0; i < worlds; i++)
            Note("difficulty: memprobe: nav world %u: flags 0x%X, %u x %u cells (%u read), jobs in flight %u (weight %u), cells the last fill planned %u; "
                "cell flags 0x1 %u, 0x4 %u, 0x8 %u, 0x10 %u, waiting for the scheduler %u; sections %u (highest id %d)", nav[i].index, nav[i].flags, nav[i].w, nav[i].h,
                nav[i].cells, nav[i].jobs, nav[i].weight, nav[i].plan, nav[i].f1, nav[i].f4, nav[i].f8, nav[i].f10, nav[i].waiting, nav[i].sections, nav[i].highest);
    }
    if (g_world.live > 1.0f && (op == 0xFFFFFFFFu)) MemProbeTiles();
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&hz);
    if (walk) Note("difficulty: memprobe: this note took %.1f ms", (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)hz.QuadPart);
}

/* ---- Each generator call of SpawnLevelLayers (op 0x6C): its range, what it kept, and the commit it added ---------- */
static int MemProbeSpawnBefore(MemProbeSpawnCall* c, uint8_t* pass, uint8_t* context)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL), node;
    memset(c, 0, sizeof *c);
    if (SafeRead((uintptr_t)pass + 0xD8, &c->resume, 4)) return 0;
    if (g_mp.spawn.levels != g_world.levels) { memset(&g_mp.spawn, 0, sizeof g_mp.spawn); g_mp.spawn.levels = g_world.levels; }
    c->commit = MemProbeCommit();
    c->da = (node = MemProbeDefaultNode(exe)) != 0 ? MemProbeNodeBytes(exe, node) : -1;
    if (g_mp.pins[MP_RO] && !SafeRead((uintptr_t)context + 0x40, &c->world, 8)) MemProbeHandles(c->world, &c->handles, &(uint32_t){0});
    QueryPerformanceCounter(&c->t0);
    return 1;
}

static void MemProbeSpawnAfter(const MemProbeSpawnCall* c, uint8_t* pass)
{
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL), countAt = 0, node;
    uint32_t resume = 0, count = 0, handles = 0, cap = 0, mission = 0, other = 0, deferred = 0;
    LARGE_INTEGER t1, hz; QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&hz);
    LONG64 commit = MemProbeCommit(), da = (node = MemProbeDefaultNode(exe)) != 0 ? MemProbeNodeBytes(exe, node) : -1;
    SafeRead((uintptr_t)pass + 0xD8, &resume, 4);
    if (!SafeRead((uintptr_t)pass + 0x48, &countAt, 8) && countAt != 0) SafeRead(countAt, &count, 4);
    int known = 0;
    AcquireSRWLockShared(&g_streamLock);
    if (g_stream.captured && g_stream.pass == pass && resume <= g_stream.count && resume >= c->resume)
    {
        known = 1;
        for (uint32_t i = c->resume; i < resume && i < kStreamMaxLocations; i++)
        {
            if (g_streamDeferred[i]) deferred++;
            else if (g_streamMission[i]) mission++;
            else other++;
        }
    }
    ReleaseSRWLockShared(&g_streamLock);
    if (c->world != 0) MemProbeHandles(c->world, &handles, &cap);
    LONG64 dc = commit >= 0 && c->commit >= 0 ? commit - c->commit : 0, dd = da >= 0 && c->da >= 0 ? da - c->da : 0;
    int last = count != 0 && resume >= count;
    g_mp.spawn.calls++; g_mp.spawn.mission += mission; g_mp.spawn.other += other; g_mp.spawn.deferred += deferred;
    g_mp.spawn.commit += dc; g_mp.spawn.da += dd;
    if ((resume != c->resume || dc >= (4ll << 20) || dc <= -(4ll << 20) || last) && g_mp.spawn.lines < 400)
    {
        g_mp.spawn.lines++;
        Note("difficulty: memprobe: op 0x6C call %u: locations %u..%u of %u (%s%u kept: %u mission, %u other; %u deferred), commit %+.1f MB, DefaultAllocator %+.1f MB, "
            "RenderObject handles %+d, %.1f ms%s", g_mp.spawn.calls, c->resume, resume, count, known ? "" : "not the captured pass, ", mission + other, mission, other,
            deferred, (double)dc / 1048576.0, (double)dd / 1048576.0, (int)handles - (int)c->handles, (double)(t1.QuadPart - c->t0.QuadPart) * 1000.0 / (double)hz.QuadPart,
            last ? " (the op's last call: the final sweep runs inside it)" : "");
    }
    if (last && !g_mp.spawn.done)
    {
        g_mp.spawn.done = 1;
        Note("difficulty: memprobe: op 0x6C done after %u calls: %u mission and %u other locations kept, %u deferred; commit %+lld MB, DefaultAllocator %+lld MB in its calls",
            g_mp.spawn.calls, g_mp.spawn.mission, g_mp.spawn.other, g_mp.spawn.deferred, (long long)(g_mp.spawn.commit >> 20), (long long)(g_mp.spawn.da >> 20));
    }
}

/* Once, with the world's installs: which counters this build's code allows. */
static void MemProbeInstall(void)
{
    static int done;
    if (done++ || !MemProbeOn()) return;
    uintptr_t exe = (uintptr_t)GetModuleHandleW(NULL), game = g_world.game;
    for (int g = 0; g < MP_GROUPS; g++) g_mp.pins[g] = 1;
    for (size_t i = 0; i < sizeof kMpPins / sizeof kMpPins[0]; i++)
        if (!ApiCodeMatches((kMpPins[i].exe ? exe : game) + kMpPins[i].rva, kMpPins[i].hex)) g_mp.pins[kMpPins[i].group] = 0;
    WorldAllocNote("memprobe install"); /* the first walk lists every node of 1 MB or more: the baseline */
    char off[300] = ""; size_t at = 0;
    for (int g = 0; g < MP_GROUPS; g++) if (!g_mp.pins[g]) at += (size_t)snprintf(off + at, sizeof off - at, "%s%s", at ? ", " : "", kMpGroupName[g]);
#ifndef CO_MEMPROBE_SOURCE
#define CO_MEMPROBE_SOURCE "unknown"
#endif
    Note("difficulty: memprobe: on (source %s): a note after every generation record and every 30 s in play; allocators by every change of 1 MB (%s), "
        "memprof by growth with %d frames (%s), rpmalloc, render device, Havok, nav, tiles, RenderObject handles, op 0x6C per call; not the reviewed code, so left out: %s",
        CO_MEMPROBE_SOURCE, g_allocOn > 0 ? "on" : "NOT on: the allocator code is not the reviewed one", g_memProfFrames, g_memProfAlloc != NULL ? "hooked" : "NOT hooked",
        at ? off : "none");
}

#ifdef CO_TEST
/* The walkers on synthetic memory: the sums they return, and the lines they write. */
int CoXlTestMemProbeOn(void) { InterlockedExchange(&g_memProbeOn, -1); return MemProbeOn(); }
int CoXlTestMemProbeHeaps(uintptr_t exe, long long* out)
{
    MemProbeHeapSum s;
    int r = MemProbeHeaps(exe, &s, 1);
    long long v[] = {s.heaps, s.orphans, s.bad, s.cache, s.spans, s.deferred, s.live, s.classSpans, s.frag, s.list, s.listHuge, s.listSpans};
    memcpy(out, v, sizeof v);
    return r;
}
int CoXlTestMemProbeDevice(uintptr_t exe, uintptr_t dev, long long* out)
{
    MemProbeD3d d;
    int r = MemProbeDevice(exe, dev, &d);
    long long v[] = {d.map[0], d.map[1], d.map[2], d.changed[0], d.made, d.free, d.flight, d.biggest, d.badX, d.copies, d.other, d.node, d.nodePeak, d.pool};
    memcpy(out, v, sizeof v);
    return r;
}
int CoXlTestMemProbeHavok(uintptr_t exe, long long* out) { LONG64 v[3]; int r = MemProbeHavok(exe, &v[0], &v[1], &v[2]); for (int i = 0; i < 3; i++) out[i] = v[i]; return r; }
int CoXlTestMemProbeNav(uintptr_t exe, long long* out)
{
    MemProbeNavWorld w[4];
    int r = MemProbeNav(exe, w, 4);
    if (r < 1) return r;
    long long v[] = {w[0].index, w[0].w, w[0].h, w[0].jobs, w[0].weight, w[0].plan, w[0].cells, w[0].f1, w[0].f8, w[0].waiting, w[0].sections, w[0].highest};
    memcpy(out, v, sizeof v);
    return r;
}
int CoXlTestMemProbeHandles(uintptr_t world, uint32_t* used, uint32_t* cap) { return MemProbeHandles(world, used, cap); }
int CoXlTestMemProbeAllocList(uintptr_t exe, const uintptr_t* nodes, const LONG64* bytes, const LONG64* deltas, int n)
{
    return MemProbeAllocList("test", exe, nodes, bytes, deltas, n, 0, 0);
}
#endif
