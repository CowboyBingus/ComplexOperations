#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "fake_host.h"

FakeHost g_fake;
static HfApi g_api;

static void Log(HfMod* mod, const char* format, ...)
{
    size_t used = strlen(g_fake.log);
    va_list args;
    va_start(args, format);
    vsnprintf(g_fake.log + used, sizeof g_fake.log - used - 2, format, args);
    va_end(args);
    strcat(g_fake.log, "\n");
}

static const char* BuildId(void) { return g_fake.build; }

static int On(HfMod* mod, HfEvent event, HfEventCallback callback, void* user)
{
    if (event != HF_EVENT_GAME_LOADED) return 1;
    g_fake.loaded = callback;
    return 0;
}

static uintptr_t ModuleBase(const char* module, size_t* size)
{
    if (strcmp(module, "helldivers2.exe") == 0 && g_fake.exe != NULL) { if (size) *size = FAKE_EXE_SIZE; return (uintptr_t)g_fake.exe; }
    if (strcmp(module, "game.dll") != 0) return 0;
    if (size) *size = FAKE_IMAGE_SIZE;
    return (uintptr_t)g_fake.image;
}

static int Inside(uintptr_t address, size_t size)
{
    const uintptr_t base = (uintptr_t)g_fake.image;
    const uintptr_t exe = (uintptr_t)g_fake.exe;
    return (address >= base && address + size <= base + FAKE_IMAGE_SIZE) || (exe != 0 && address >= exe && address + size <= exe + FAKE_EXE_SIZE);
}

static int Read(uintptr_t address, void* buffer, size_t size)
{
    if (!Inside(address, size)) return 1;
    memcpy(buffer, (void*)address, size);
    return 0;
}

/* The bytes before any hook: the image, with what another's hook covers put back. */
static int ReadCode(uintptr_t address, void* buffer, size_t size)
{
    if (Read(address, buffer, size) != 0) return 1;
    for (size_t i = 0; i < sizeof g_fake.underHook / sizeof g_fake.underHook[0]; i++)
    {
        const uintptr_t at = (uintptr_t)g_fake.image + g_fake.underHook[i].rva;
        for (size_t k = 0; g_fake.underHook[i].rva != 0 && k < sizeof g_fake.underHook[i].bytes; k++)
            if (at + k >= address && at + k < address + size) ((uint8_t*)buffer)[at + k - address] = g_fake.underHook[i].bytes[k];
    }
    return 0;
}

static int Write(uintptr_t address, const void* data, size_t size)
{
    if (!Inside(address, size) || g_fake.writes++ == g_fake.failWriteAt) return 1;
    memcpy((void*)address, data, size);
    return 0;
}

static int CodeMatches(uintptr_t address, const char* pattern)
{
    for (size_t i = 0; i < sizeof g_fake.noMatch / sizeof g_fake.noMatch[0]; i++)
        if (g_fake.noMatch[i] != 0 && (uintptr_t)g_fake.image + g_fake.noMatch[i] == address) return 0;
    if (!g_fake.strictCode) return 1;
    const uint8_t* at = (const uint8_t*)address;
    for (const char* c = pattern; *c;)
    {
        if (*c == ' ') { c++; continue; }
        if (*c == '?') { c += c[1] == '?' ? 2 : 1; at++; continue; }
        unsigned v = 0;
        if (sscanf(c, "%2x", &v) != 1 || c[1] == 0 || *at++ != (uint8_t)v) return 0;
        c += 2;
    }
    return 1;
}

static uint64_t Original(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) { return 0; }

static int Hook(HfMod* mod, uintptr_t target, void* detour, void** original)
{
    const uintptr_t rva = target - (uintptr_t)g_fake.image;
    if (rva == g_fake.noHook || g_fake.hookCount == FAKE_HOOKS) return 1;
    g_fake.hooks[g_fake.hookCount].target = rva;
    g_fake.hooks[g_fake.hookCount++].detour = detour;
    *original = (void*)Original;
    for (size_t i = 0; i < sizeof g_fake.originals / sizeof g_fake.originals[0]; i++)
        if (g_fake.originals[i].target == target && g_fake.originals[i].original != NULL) *original = g_fake.originals[i].original;
    return 0;
}

static int HookEx(HfMod* mod, uintptr_t target, void* detour, void** original, int32_t priority) { return Hook(mod, target, detour, original); }

static int GenerationCapacity(HfMod* mod, const HfGenerationCapacity* want, HfGenerationCapacity* have)
{
    if (want != NULL && !g_fake.capacityRefused) g_fake.wanted = *want;
    if (have != NULL && g_fake.capacity.size != 0)
    {
        const uint32_t size = have->size < g_fake.capacity.size ? have->size : g_fake.capacity.size;
        HfGenerationCapacity now = g_fake.capacity;
        now.size = have->size;
        memcpy(have, &now, size);
    }
    return want != NULL && g_fake.capacityRefused;
}

static void Session(HfMod* mod, HfSession* out) { memset(out, 0, sizeof *out); out->role = g_fake.role; }
static uint32_t ShareGet(HfMod* mod, uint64_t key, void* value, uint32_t capacity)
{
    if (g_fake.rowBytes != 0 && capacity >= g_fake.rowBytes) memcpy(value, g_fake.row, g_fake.rowBytes);
    return g_fake.rowBytes;
}
static int ShareSet(HfMod* mod, uint64_t key, const void* value, uint32_t bytes)
{
    if (g_fake.shareRefused || g_fake.role == HF_SESSION_JOINER || bytes > sizeof g_fake.row) return 1;
    memcpy(g_fake.row, value, bytes); g_fake.rowBytes = bytes; g_fake.shareSets++;
    return 0;
}
static int Send(HfMod* mod, uint64_t to, const void* data, uint32_t bytes)
{
    if (g_fake.sendRefused || bytes > sizeof g_fake.sent) return 1;
    g_fake.sentTo = to; memcpy(g_fake.sent, data, bytes); g_fake.sentBytes = bytes; g_fake.sends++;
    return 0;
}

static int Subscribe(HfMod* mod, uint32_t kind, HfListener listener, void* user, int32_t priority) { g_fake.subscribed = kind; return 1; } /* the SDK: 1 listening, 0 refused */

static uintptr_t DifficultyRow(uint32_t level) { return level == 11 ? g_fake.levelRow : level == 10 ? g_fake.levelTen : 0; }
static uint32_t LoadPhase(HfMod* mod) { return g_fake.loadPhase; }
static int OnGameThread(HfMod* mod, HfTickCallback callback, void* user) { g_fake.tick = callback; return 1; }

/* The registry's first list holds two other systems around the table's entry; the other two lists hold its entry only.
 * {list, count, the table's entry} */
static void Registry(uint8_t* image)
{
    static const struct { uint32_t list, count, table; } kLists[] = {{0x2078u, 3, 1}, {0x7230u, 1, 0}, {0x7248u, 1, 0}};
    const uint64_t table = (uint64_t)(uintptr_t)(image + FAKE_SYNCED_TABLE), registry = (uint64_t)(uintptr_t)(image + FAKE_REGISTRY);
    const uint64_t engine = table - FAKE_SYNCED_IN_ENGINE;
    for (size_t i = 0; i < sizeof kLists / sizeof kLists[0]; i++)
    {
        uint8_t* list = image + FAKE_REGISTRY + kLists[i].list;
        memcpy(list, &kLists[i].count, 4);
        for (uint32_t j = 0; j < kLists[i].count; j++)
        {
            const uint64_t at = j == kLists[i].table ? table : table + 0x100000u * (j + 1);
            const uint32_t type = j == kLists[i].table ? 0x11Du : 0x85u + j;
            memcpy(list + 8 + j * 16, &at, 8);
            memcpy(list + 8 + j * 16 + 8, &type, 4);
        }
    }
    memcpy(image + FAKE_SYNCED_POINTER, &table, 8);
    memcpy(image + FAKE_ENGINE_GLOBAL, &engine, 8);
    memcpy(image + FAKE_REGISTRY_GLOBAL, &registry, 8);
    const uint64_t ups = (uint64_t)(uintptr_t)(image + FAKE_UPS);
    memcpy(image + FAKE_UPS_POINTER, &ups, 8);
}
_Static_assert(FAKE_UPS + FAKE_UPS_SIZE <= FAKE_IMAGE_SIZE, "the placement system lies inside the image");

static uint8_t* g_exe;
uint8_t* fake_host_exe(void)
{
    if (g_exe == NULL) g_exe = VirtualAlloc(NULL, FAKE_EXE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    else memset(g_exe, 0, FAKE_EXE_SIZE);
    return g_fake.exe = g_exe;
}

const HfApi* fake_host_reset(const char* build)
{
    uint8_t* image = g_fake.image;
    if (image == NULL) image = VirtualAlloc(NULL, FAKE_IMAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    else memset(image, 0, FAKE_IMAGE_SIZE);
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.image = image;
    g_fake.build = build;
    Registry(image);
    g_fake.failWriteAt = -1;
    memset(&g_api, 0, sizeof g_api);
    g_api.version = HF_MOD_SDK_VERSION;
    g_api.size = sizeof g_api;
    g_api.log = Log;
    g_api.build_id = BuildId;
    g_api.on = On;
    g_api.module_base = ModuleBase;
    g_api.read = Read;
    g_api.write = Write;
    g_api.code_matches = CodeMatches;
    g_api.read_code = ReadCode;
    g_api.hook = Hook;
    g_api.hook_ex = HookEx;
    g_api.generation_capacity = GenerationCapacity;
    g_api.difficulty_row = DifficultyRow;
    g_api.load_phase = LoadPhase;
    g_api.on_game_thread = OnGameThread;
    g_api.session = Session;
    g_api.subscribe = Subscribe;
    g_api.share_get = ShareGet;
    g_api.share_set = ShareSet;
    g_api.send = Send;
    return &g_api;
}

void* fake_host_detour(uintptr_t rva)
{
    for (int i = 0; i < g_fake.hookCount; i++)
        if (g_fake.hooks[i].target == rva) return g_fake.hooks[i].detour;
    return NULL;
}

int fake_host_logged(const char* text) { return strstr(g_fake.log, text) != NULL; }
