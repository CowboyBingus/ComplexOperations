/* Eight ship hellpods: the ship_hellpod component manager and the ship's pod table with eight slots.
 *
 * The game keeps the manager (0x118 bytes, four slots) inside its component block, with the next manager right
 * behind it, so it cannot grow where it is. This builds it in the mod's memory instead: the nine places that reach it
 * from the block get the new displacement, the two arrays that eight slots of their neighbours would overrun (the
 * master records and the synced pod state) get new offsets inside the new manager, and the capacity checks say 8.
 * The ship record's pod table (four entries with the marker array right behind) moves the same way. */
#include <string.h>
#include <windows.h>

#include "co.h"

typedef enum CoPodKind { CO_POD_BLOCK, CO_POD_A1, CO_POD_A3, CO_POD_CAPACITY, CO_POD_TABLE } CoPodKind;
typedef struct CoPodSite
{
    uint32_t rva;
    uint8_t at;      /* offset of the value in the instruction */
    uint32_t old;    /* its vanilla value */
    CoPodKind kind;
    uint8_t pin[6];  /* the instruction bytes before the value */
} CoPodSite;
#include "co_pod_sites.h"

#define CO_STATE_CTOR 0xFDA190u
#define CO_STATE_GLOBAL 0x346BF98u
#define CO_STATE_BLOCK 0x40u        /* the component block inside the entity state */
#define CO_POD_MANAGER 0xEEF968u    /* the manager inside the block */
#define CO_POD_SLOTS 8u
/* The new manager: header, hash and the entity pointers (+0x68, 8 x 8) and per-pod records (+0xB8, 8 x 0x14) where
 * they were; the master records (8 x 0xC) and the synced states (8 x 4) behind them. */
#define CO_POD_A1_NEW 0x158u
#define CO_POD_A3_NEW 0x1B8u
#define CO_POD_SIZE 0x1D8u
/* The ship record in the block, its per-ship element and the pod table in it (entries of 0x4C bytes); the new table
 * sits in the same allocation as the manager. */
#define CO_SHIP 0xEEFB00u
#define CO_SHIP_ELEMENT 0x11CD8u
#define CO_SHIP_PODS 0x11D18u
#define CO_PODS_NEW 0x400u
#define CO_PODS_SIZE (CO_POD_SLOTS * 0x4Cu)
#define CO_ALLOC_USED (CO_PODS_NEW + CO_PODS_SIZE)

/* Instructions that change shape. Two places reach the table through the element pointer with an 8-bit offset, which
 * cannot hold the new distance: the first becomes the same lea with a 32-bit offset (over its padding), the second
 * reads through the entry pointer that loop already keeps. `at` is where the 32-bit offset goes, 0 for none. */
typedef struct CoPodRewrite { uint32_t rva; uint8_t size, at; uint8_t old[7], now[7]; } CoPodRewrite;
static const CoPodRewrite kRewrites[] = {
    {0x660CA9u, 7, 3, {0x49, 0x8D, 0x7D, 0x40, 0x0F, 0x1F, 0x00}, {0x49, 0x8D, 0xBD, 0, 0, 0, 0}},
    {0x660CC6u, 5, 0, {0x42, 0x8B, 0x54, 0x29, 0x40}, {0x8B, 0x17, 0x0F, 0x1F, 0x00}},
    /* the launch cutscene's four-pod variant and sound, for exactly four players: now for four or more */
    {0x11CEC09u, 4, 0, {0x83, 0xFF, 0x04, 0x75}, {0x83, 0xFF, 0x04, 0x72}},
    {0x116AA72u, 4, 0, {0x83, 0xF9, 0x04, 0x75}, {0x83, 0xF9, 0x04, 0x72}},
};
#define CO_REWRITES (sizeof kRewrites / sizeof kRewrites[0])
/* The four new launchers of the stretched bay: unit records of the bay's level (the mod's level patch puts them there
 * with the names hellpod5..8). The game makes pod entities only of level units listed in its generated level table,
 * so the lookup of that level's rows hands out the game's rows plus these four, as rows like the vanilla pods'. */
#define CO_LEVEL_ROWS 0xFC4490u     /* Row* (Table*, u64 level, u32* count) */
#define CO_ROW 0x44u
#define CO_ROW_CAP 256u
#define CO_BAY_LEVEL 0x4D7DBDAF114D7E6Aull
static const uint16_t kNewPods[] = {600, 601, 701, 702};
static const char* const kNewPodNames[] = {"hellpod7", "hellpod8", "hellpod6", "hellpod5"};
#define CO_NEW_PODS (sizeof kNewPods / sizeof kNewPods[0])
static const char kLevelRowsPin[] = "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 44 8B 59 20 48 8D 3C 12";
/* The pod component's init reads the unit's "name" and knows hellpod1..4 only; any other name gets index -1, which
 * the pod update would use as a table index. */
#define CO_POD_INIT 0x66ACF0u       /* void (void*, Instance*, bool*): Instance +8 entity id, +0xC unit */
#define CO_POD_GLOBAL 0x3326428u
#define CO_ENGINE_API 0x3326308u
#define CO_KEY_NAME 0xD4C943CBu
/* The launch cutscene fires pods 1..4 by index, one call each; pods 5..8 go with them, one per call. */
#define CO_POD_LAUNCH 0x11CE740u    /* void (void*, u32 pod index) */
static const char kPodLaunchPin[] = "48 89 5C 24 18 57 48 83 EC 30 48 8B 3D";
/* A lone player's launch sound is picked by asking "is the player in pod i" for pods 1..4 only. */
#define CO_POD_MINE 0x11CEAF0u      /* bool (void*, u32 pod index) */
static const char kPodMinePin[] = "48 89 5C 24 10 48 89 4C 24 08 57 48 83 EC 20 48 8B 1D";
typedef uint8_t (*PodMineFn)(void*, uint32_t);
static PodMineFn g_podMine;
/* The briefing camera of a pod: the game picks one of four named camera units by pod index and takes no camera for
 * any other index. The replacement keeps that for pods 1..4 and takes the unit from kCameras for 5..8: camera units
 * the level patch places in the bay's level, found by record when that level is walked. */
#define CO_LEVEL_PASS 0xFDF260u     /* (collector, table, Level*, u32 first id, u32 level index) */
#define CO_CAMERA_PICK 0x11BBF97u
#define CO_CAMERA_NAMED 0x11BBFDCu  /* continues with the name id in rbx */
#define CO_CAMERA_UNIT 0x11BC07Eu   /* continues with the unit in edi */
#define CO_CAMERA_NONE 0x11BC0AEu
static const char kLevelPassPin[] = "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 48 89 54 24 10 57 41 54";
static const uint16_t kCameraRecords[] = {575, 579, 49, 55}; /* pods 5..8 */
#define CO_CAMERA_RESOURCE 0x465F4895F3DC98D1ull
static const uint16_t kDroppedRows[] = {49, 55};             /* crates the bay no longer has; their records hold cameras */
static struct { uint64_t names[4]; uint32_t units[4]; } g_cameras = {
    {0xFE3821974E8DBE22ull, 0x947CAB2395B01EE3ull, 0x9C6AF74104323A53ull, 0x27C9B0B0AA65453Full}, {0, 0, 0, 0}};
static const char kPodInitPin[] = "40 53 56 57 48 83 EC 50 8B 42 08 33 DB 3B 05";
static const char kStateCtorPin[] = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 89 4C 24 20 48 8B D9";

typedef uint64_t (*StateCtorFn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static StateCtorFn g_stateCtor;
typedef uint8_t* (*LevelRowsFn)(void*, uint64_t, uint32_t*);
typedef void (*PodInitFn)(void*, uint8_t*, void*);
typedef struct CoValue { const void* data; uint32_t type, size; } CoValue;
typedef CoValue* (*GetPropertyFn)(CoValue*, uint32_t, uint32_t, uint32_t);
typedef void (*PodLaunchFn)(void*, uint32_t);
static PodLaunchFn g_podLaunch;
typedef uint64_t (*LevelPassFn)(void*, void*, void*, uint64_t, uint64_t);
static LevelPassFn g_levelPass;
static int g_bayWalked, g_camerasMoved;
static int g_newPods; /* the four hooks the new pods need are all in: a pod that can be entered must also launch */
static void* g_walkLevel; /* the level the walk is in */
static void MoveCameras(void);
static LevelRowsFn g_levelRows;
static PodInitFn g_podInit;
/* The game keeps pointers to the rows it was handed, so g_rows is only written when its content changes. */
static uint8_t g_rows[(CO_ROW_CAP + CO_NEW_PODS) * CO_ROW], g_rowsNext[(CO_ROW_CAP + CO_NEW_PODS) * CO_ROW];
static uintptr_t g_game, g_manager, g_block;
static int g_state; /* 0 waiting for the constructor, 1 eight slots, -1 left as the game has it */

/* Free address space within 1.75 GB of `target`, above it first: the code reaches the manager by a 32-bit offset. */
uintptr_t co_alloc_near(uintptr_t target, size_t size)
{
    const uintptr_t span = 0x70000000u, grain = 0x10000u;
    for (int up = 1; up >= 0; up--)
    {
        uintptr_t at = up ? (target + grain) & ~(grain - 1) : ((target - size) & ~(grain - 1));
        while (up ? at + size < target + span : at > target - span)
        {
            MEMORY_BASIC_INFORMATION m;
            if (VirtualQuery((void*)at, &m, sizeof m) == 0) break;
            uintptr_t end = (uintptr_t)m.BaseAddress + m.RegionSize;
            if (m.State == MEM_FREE && end - at >= size)
            {
                void* p = VirtualAlloc((void*)at, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (p) return (uintptr_t)p;
            }
            if (up) at = (end + grain - 1) & ~(grain - 1);
            else
            {
                uintptr_t base = (uintptr_t)m.AllocationBase ? (uintptr_t)m.AllocationBase : (uintptr_t)m.BaseAddress;
                if (base < size + grain) break;
                at = (base - size) & ~(grain - 1);
            }
        }
    }
    return 0;
}

static uint32_t SiteValue(const CoPodSite* s, int64_t d)
{
    switch (s->kind)
    {
    case CO_POD_BLOCK: return (uint32_t)(int32_t)(d + (int64_t)(s->old - CO_POD_MANAGER));
    case CO_POD_A1: return s->old - 0x88u + CO_POD_A1_NEW;
    case CO_POD_A3: return s->old - 0x108u + CO_POD_A3_NEW;
    case CO_POD_TABLE: return (uint32_t)(int32_t)(d + CO_PODS_NEW - CO_SHIP + (int64_t)(s->old - CO_SHIP_PODS));
    default: return CO_POD_SLOTS;
    }
}

static size_t SiteSize(const CoPodSite* s) { return s->kind == CO_POD_CAPACITY ? 1u : 4u; }

static void Move(uintptr_t state)
{
    const size_t n = sizeof kCoPodSites / sizeof kCoPodSites[0];
    uintptr_t block = state + CO_STATE_BLOCK;
    for (size_t i = 0; i < n; i++)
    {
        const CoPodSite* s = &kCoPodSites[i];
        uint8_t bytes[12];
        if (g_coApi->read(g_game + s->rva, bytes, s->at + SiteSize(s)) != 0 || memcmp(bytes, s->pin, s->at) != 0 ||
            memcmp(bytes + s->at, &s->old, SiteSize(s)) != 0)
        {
            co_log("hellpods: the code at game+0x%X is not the reviewed one; four pods as before", s->rva);
            return;
        }
    }
    for (size_t i = 0; i < CO_REWRITES; i++)
    {
        uint8_t bytes[7];
        if (g_coApi->read(g_game + kRewrites[i].rva, bytes, kRewrites[i].size) != 0 || memcmp(bytes, kRewrites[i].old, kRewrites[i].size) != 0)
        {
            co_log("hellpods: the code at game+0x%X is not the reviewed one; four pods as before", kRewrites[i].rva);
            return;
        }
    }
    uintptr_t manager = co_alloc_near(block, 0x10000u);
    if (manager == 0) { co_log("hellpods: no free memory within 2 GB of the entity state; four pods as before"); return; }
    int64_t d = (int64_t)manager - (int64_t)block;
    for (size_t i = 0; i < n; i++)
    {
        const CoPodSite* s = &kCoPodSites[i];
        uint32_t value = SiteValue(s, d);
        if (g_coApi->write(g_game + s->rva + s->at, &value, SiteSize(s)) != 0)
        {
            /* The game cannot run on a mix: the sites written so far go back. */
            for (size_t j = 0; j < i; j++)
                g_coApi->write(g_game + kCoPodSites[j].rva + kCoPodSites[j].at, &kCoPodSites[j].old, SiteSize(&kCoPodSites[j]));
            VirtualFree((void*)manager, 0, MEM_RELEASE);
            co_log("hellpods: game+0x%X could not be written; four pods as before", s->rva);
            return;
        }
    }
    for (size_t i = 0; i < CO_REWRITES; i++)
    {
        uint8_t bytes[7];
        /* the new table as seen from the element pointer */
        int32_t offset = (int32_t)(d + CO_PODS_NEW - CO_SHIP - CO_SHIP_ELEMENT);
        memcpy(bytes, kRewrites[i].now, kRewrites[i].size);
        if (kRewrites[i].at) memcpy(bytes + kRewrites[i].at, &offset, 4);
        if (g_coApi->write(g_game + kRewrites[i].rva, bytes, kRewrites[i].size) != 0)
        {
            for (size_t j = 0; j < i; j++) g_coApi->write(g_game + kRewrites[j].rva, kRewrites[j].old, kRewrites[j].size);
            for (size_t j = 0; j < n; j++)
                g_coApi->write(g_game + kCoPodSites[j].rva + kCoPodSites[j].at, &kCoPodSites[j].old, SiteSize(&kCoPodSites[j]));
            VirtualFree((void*)manager, 0, MEM_RELEASE);
            co_log("hellpods: game+0x%X could not be written; four pods as before", kRewrites[i].rva);
            return;
        }
    }
    g_manager = manager;
    g_block = block;
    g_state = 1;
    co_log("hellpods: the pod manager and the ship's pod table have %u slots (%u code sites changed)", CO_POD_SLOTS,
           (unsigned)(n + CO_REWRITES));
}

/* The sites hold the manager's distance from one component block. A state built somewhere else needs them written
 * again, so the code first goes back to the game's own. */
static int Restore(void)
{
    const size_t n = sizeof kCoPodSites / sizeof kCoPodSites[0];
    int failed = 0;
    for (size_t i = 0; i < CO_REWRITES; i++) failed |= g_coApi->write(g_game + kRewrites[i].rva, kRewrites[i].old, kRewrites[i].size) != 0;
    for (size_t i = 0; i < n; i++)
        failed |= g_coApi->write(g_game + kCoPodSites[i].rva + kCoPodSites[i].at, &kCoPodSites[i].old, SiteSize(&kCoPodSites[i])) != 0;
    if (failed) return 0;
    VirtualFree((void*)g_manager, 0, MEM_RELEASE);
    g_manager = g_block = 0;
    g_state = 0;
    return 1;
}

static uint64_t StateCtorDetour(uint64_t state, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    if (g_state == 1 && (uintptr_t)state + CO_STATE_BLOCK != g_block && !Restore())
        co_log("hellpods: the game rebuilt its state elsewhere and its code could not be set back; restart the game");
    if (g_state == 0)
    {
        g_state = -1;
        Move((uintptr_t)state);
        if (g_state == 1 && g_newPods && !g_camerasMoved) MoveCameras();
    }
    /* The game zeroes its state before this constructor; the moved manager is outside it. */
    if (g_state == 1) { memset((void*)g_manager, 0, CO_ALLOC_USED); memset(g_cameras.units, 0, sizeof g_cameras.units); }
    co_managers_state((uintptr_t)state + CO_STATE_BLOCK);
#ifdef CO_CANARY
    uint64_t result = g_stateCtor(state, a2, a3, a4, a5, a6);
    co_managers_canary(0);
    return result;
#else
    return g_stateCtor(state, a2, a3, a4, a5, a6);
#endif
}

/* Without the mod's level patch the four records are other units, which must not become pods: each has to carry its
 * pod name. Reads them with the level interface (unit count, unit by record) and the unit property getter. */
static int BayHasPods(void* level)
{
    typedef uint32_t (*CountFn)(void*);
    typedef uint32_t (*UnitFn)(void*, uint32_t);
    const uintptr_t api = *(uintptr_t*)(g_game + CO_ENGINE_API);
    void** levels = *(void***)(api + 0x50);
    GetPropertyFn get = (GetPropertyFn)(*(void***)*(void***)(api + 0xB8))[2];
    const uint32_t units = ((CountFn)levels[8])(level);
    for (size_t i = 0; i < CO_NEW_PODS; i++)
    {
        CoValue out, *value;
        const uint32_t unit = kNewPods[i] < units ? ((UnitFn)levels[6])(level, kNewPods[i]) : 0;
        if (unit == 0) return 0;
        value = get(&out, unit, 1, CO_KEY_NAME);
        if (value == NULL || value->type != 3 || value->data == NULL || value->size < 9 ||
            memcmp(value->data, kNewPodNames[i], 9) != 0)
            return 0;
    }
    return 1;
}

static uint8_t* LevelRowsDetour(void* table, uint64_t level, uint32_t* count)
{
    static int told;
    uint8_t* rows = g_levelRows(table, level, count);
    if (g_state != 1 || !g_newPods || level != CO_BAY_LEVEL || rows == NULL || g_walkLevel == NULL) return rows;
    if (*count > CO_ROW_CAP || !BayHasPods(g_walkLevel))
    {
        if (!told) co_log("hellpods: the bridge is not the one with the eight-pod bay (its level data is missing); four pods");
        told = 1;
        return rows;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < *count; i++)
    {
        uint16_t index;
        memcpy(&index, rows + i * CO_ROW, 2);
        if (index == kDroppedRows[0] || index == kDroppedRows[1]) continue;
        memcpy(g_rowsNext + n++ * CO_ROW, rows + i * CO_ROW, CO_ROW);
    }
    g_bayWalked = 1;
    for (size_t i = 0; i < CO_NEW_PODS; i++)
    {
        /* as the game's own pod rows: an entity (bit 0) with a skin (bit 10), entity type hellpod_ship */
        uint8_t* row = g_rowsNext + (n + i) * CO_ROW;
        const uint16_t flags = 0x0401;
        const uint32_t group = 0xCD121F4Bu, type = 0x4821CF7Au;
        memset(row, 0, CO_ROW);
        memcpy(row, &kNewPods[i], 2);
        memcpy(row + 2, &flags, 2);
        memcpy(row + 8, &group, 4);
        memcpy(row + 0xC, &type, 4);
    }
    *count = n + CO_NEW_PODS;
    if (memcmp(g_rows, g_rowsNext, *count * CO_ROW) != 0) memcpy(g_rows, g_rowsNext, *count * CO_ROW);
    return g_rows;
}

static uint64_t LevelPassDetour(void* collector, void* table, void* level, uint64_t first, uint64_t index)
{
    /* the walk calls itself for nested levels: each call keeps its own marks */
    const int outer = g_bayWalked;
    void* const outerLevel = g_walkLevel;
    g_bayWalked = 0;
    g_walkLevel = level;
    uint64_t result = g_levelPass(collector, table, level, first, index);
#ifdef CO_CANARY
    co_managers_canary(0);
#endif
    const int bay = g_bayWalked;
    g_bayWalked = outer;
    g_walkLevel = outerLevel;
    if (bay)
    {
        /* the level interface's unit-by-record call, as the walk itself uses it */
        typedef uint32_t (*UnitFn)(void*, uint32_t);
        typedef uint32_t (*IsFn)(uint32_t, uint64_t);
        const uintptr_t api = *(uintptr_t*)(g_game + CO_ENGINE_API);
        UnitFn unit = (UnitFn)(*(void***)(api + 0x50))[6];
        IsFn is = (IsFn)(*(void***)(api + 0x18))[0x730 / 8]; /* the unit is of this resource */
        for (size_t i = 0; i < 4; i++)
        {
            const uint32_t camera = unit(level, kCameraRecords[i]);
            g_cameras.units[i] = camera != 0 && is(camera, CO_CAMERA_RESOURCE) ? camera : 0;
        }
    }
    return result;
}

/* cmp ecx,8 / jae none / mov rax,&g_cameras / cmp ecx,4 / jae +6 / mov rbx,[rax+rcx*8] / jmp named /
 * mov edi,[rax+rcx*4+0x10] / test edi,edi / je none / jmp unit */
static void MoveCameras(void)
{
    uint8_t now[sizeof kCoCameraOld], bytes[sizeof kCoCameraOld];
    const uint64_t table = (uint64_t)(uintptr_t)&g_cameras;
    int32_t rel;
    if (g_coApi->read(g_game + CO_CAMERA_PICK, bytes, sizeof bytes) != 0 || memcmp(bytes, kCoCameraOld, sizeof bytes) != 0)
    { co_log("hellpods: the camera choice is not the reviewed code; pods 5 to 8 have no briefing camera"); return; }
    memset(now, 0xCC, sizeof now);
    memcpy(now, "\x83\xF9\x08\x0F\x83", 5);
    rel = (int32_t)(CO_CAMERA_NONE - (CO_CAMERA_PICK + 9)); memcpy(now + 5, &rel, 4);
    memcpy(now + 9, "\x48\xB8", 2); memcpy(now + 11, &table, 8);
    memcpy(now + 19, "\x83\xF9\x04\x73\x06\x48\x8B\x1C\xC8\xEB", 10);
    now[29] = (uint8_t)(CO_CAMERA_NAMED - (CO_CAMERA_PICK + 30));
    memcpy(now + 30, "\x8B\x7C\x88\x10\x85\xFF\x0F\x84", 8);
    rel = (int32_t)(CO_CAMERA_NONE - (CO_CAMERA_PICK + 42)); memcpy(now + 38, &rel, 4);
    now[42] = 0xE9;
    rel = (int32_t)(CO_CAMERA_UNIT - (CO_CAMERA_PICK + 47)); memcpy(now + 43, &rel, 4);
    if (g_coApi->write(g_game + CO_CAMERA_PICK, now, sizeof now) != 0)
        co_log("hellpods: the camera choice could not be written; pods 5 to 8 have no briefing camera");
    else g_camerasMoved = 1;
}

static void PodInitDetour(void* a, uint8_t* instance, void* c)
{
    g_podInit(a, instance, c);
    if (g_state != 1 || instance == NULL) return;
    uint8_t* manager = *(uint8_t**)(g_game + CO_POD_GLOBAL);
    uint32_t used = *(uint32_t*)(manager + 4);
    for (uint32_t i = 0; i < used && i < CO_POD_SLOTS; i++)
    {
        uint8_t* entity = *(uint8_t**)(manager + 0x68 + 8 * i);
        uint8_t* record = manager + 0xB8 + 0x14 * i; /* +4 pod index, +8 has a name */
        if (entity == NULL || *(uint32_t*)(entity + 8) != *(uint32_t*)(instance + 8)) continue;
        if (record[8] != 1 || *(int32_t*)(record + 4) != -1) return;
        /* the unit property store's getter, as the init itself calls it */
        void** table = *(void***)(*(uintptr_t*)(g_game + CO_ENGINE_API) + 0xB8);
        GetPropertyFn get = (GetPropertyFn)(*(void***)table)[2];
        CoValue out, *value = get(&out, *(uint32_t*)(instance + 0xC), 1, CO_KEY_NAME);
        const char* name = value ? (const char*)value->data : NULL;
        if (name && value->type == 3 && memcmp(name, "hellpod", 7) == 0 && name[7] >= '5' && name[7] <= '8' && name[8] == 0)
            *(int32_t*)(record + 4) = name[7] - '1';
        else
        {
            record[8] = 0; /* no index: the pod is left out of the ship's table, as one without a name is */
            co_log("hellpods: a pod with an unknown name is not registered (unit %u, type %u, size %u, \"%.12s\")",
                   *(uint32_t*)(instance + 0xC), value ? value->type : 0, value ? value->size : 0,
                   name && value->type == 3 ? name : "");
        }
        return;
    }
}

static void PodLaunchDetour(void* a, uint32_t pod)
{
    g_podLaunch(a, pod);
    if (g_state == 1 && g_newPods && pod < 4) g_podLaunch(a, pod + 4);
}

/* Pod n + 4 leaves with pod n, so it takes pod n's sound. */
static uint8_t PodMineDetour(void* a, uint32_t pod)
{
    static int told;
    if (g_podMine(a, pod)) return 1;
    if (g_state != 1 || !g_newPods || pod >= 4 || !g_podMine(a, pod + 4)) return 0;
    if (!told) co_log("hellpods: the player in pod %u gets the launch sound of pod %u", pod + 5, pod + 1);
    told = 1;
    return 1;
}

/* Runs on the loader's thread while the game starts. The hooks the new pods need go in first and count only together;
 * the constructor's hook is last, because its detour is what starts everything. */
void co_pods_install(uintptr_t game)
{
    uint64_t global = 1;
    g_game = game;
    co_managers_install(game);
    if (g_coApi->read(game + CO_STATE_GLOBAL, &global, 8) != 0 || global != 0)
    { g_state = -1; co_log("hellpods: the game was already running when the mod loaded; four pods as before"); return; }
    if (g_coApi->code_matches(game + CO_STATE_CTOR, kStateCtorPin) != 1)
    { g_state = -1; co_log("hellpods: this is not the game code the mod was made for (a game update or another mod); four pods as before"); return; }
    if (g_coApi->code_matches(game + CO_LEVEL_ROWS, kLevelRowsPin) != 1 || g_coApi->code_matches(game + CO_POD_INIT, kPodInitPin) != 1 ||
        g_coApi->code_matches(game + CO_LEVEL_PASS, kLevelPassPin) != 1 || g_coApi->code_matches(game + CO_POD_LAUNCH, kPodLaunchPin) != 1)
        co_log("hellpods: the code for new pods is not the one the mod was made for (a game update or another mod); no new pods");
    else if (g_coApi->hook(g_coMod, game + CO_POD_LAUNCH, (void*)PodLaunchDetour, (void**)&g_podLaunch) != 0 ||
             g_coApi->hook(g_coMod, game + CO_POD_INIT, (void*)PodInitDetour, (void**)&g_podInit) != 0 ||
             g_coApi->hook(g_coMod, game + CO_LEVEL_PASS, (void*)LevelPassDetour, (void**)&g_levelPass) != 0 ||
             g_coApi->hook(g_coMod, game + CO_LEVEL_ROWS, (void*)LevelRowsDetour, (void**)&g_levelRows) != 0)
        co_log("hellpods: a hook for the new pods could not be placed; no new pods");
    else g_newPods = 1;
    if (g_newPods && (g_coApi->code_matches(game + CO_POD_MINE, kPodMinePin) != 1 ||
                      g_coApi->hook(g_coMod, game + CO_POD_MINE, (void*)PodMineDetour, (void**)&g_podMine) != 0))
    { g_podMine = NULL; co_log("hellpods: a lone player in pods 5 to 8 launches without the launch sound (its code could not be hooked)"); }
    if (g_coApi->hook(g_coMod, game + CO_STATE_CTOR, (void*)StateCtorDetour, (void**)&g_stateCtor) != 0)
    { g_state = -1; g_newPods = 0; co_log("hellpods: the constructor could not be hooked; four pods as before"); return; }
    if (g_coApi->read(game + CO_STATE_GLOBAL, &global, 8) == 0 && global != 0 && g_state == 0)
        co_log("hellpods: the game built its state while the mod loaded; four pods until it builds it again");
}

#ifdef CO_TEST
/* For tests/pods_test.c: the game's bytes at every place the mod checks, a fresh start, and what the mod decided. */
void co_pods_test_fill(uint8_t* image)
{
    for (size_t i = 0; i < sizeof kCoPodSites / sizeof kCoPodSites[0]; i++)
    {
        memcpy(image + kCoPodSites[i].rva, kCoPodSites[i].pin, kCoPodSites[i].at);
        memcpy(image + kCoPodSites[i].rva + kCoPodSites[i].at, &kCoPodSites[i].old, SiteSize(&kCoPodSites[i]));
    }
    for (size_t i = 0; i < CO_REWRITES; i++) memcpy(image + kRewrites[i].rva, kRewrites[i].old, kRewrites[i].size);
    memcpy(image + CO_CAMERA_PICK, kCoCameraOld, sizeof kCoCameraOld);
}

void co_pods_test_reset(void)
{
    if (g_manager) VirtualFree((void*)g_manager, 0, MEM_RELEASE);
    g_manager = g_block = g_game = 0;
    g_state = g_newPods = g_camerasMoved = g_bayWalked = 0;
}

int co_pods_test_state(void) { return g_state; }
uintptr_t co_pods_test_manager(void) { return g_manager; }
#endif
