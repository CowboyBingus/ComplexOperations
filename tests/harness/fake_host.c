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
    if (strcmp(module, "game.dll") != 0) return 0;
    if (size) *size = FAKE_IMAGE_SIZE;
    return (uintptr_t)g_fake.image;
}

static int Inside(uintptr_t address, size_t size)
{
    const uintptr_t base = (uintptr_t)g_fake.image;
    return address >= base && address + size <= base + FAKE_IMAGE_SIZE;
}

static int Read(uintptr_t address, void* buffer, size_t size)
{
    if (!Inside(address, size)) return 1;
    memcpy(buffer, (void*)address, size);
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
    return 0;
}

const HfApi* fake_host_reset(const char* build)
{
    uint8_t* image = g_fake.image;
    if (image == NULL) image = VirtualAlloc(NULL, FAKE_IMAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    else memset(image, 0, FAKE_IMAGE_SIZE);
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.image = image;
    g_fake.build = build;
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
    g_api.hook = Hook;
    return &g_api;
}

void* fake_host_detour(uintptr_t rva)
{
    for (int i = 0; i < g_fake.hookCount; i++)
        if (g_fake.hooks[i].target == rva) return g_fake.hooks[i].detour;
    return NULL;
}

int fake_host_logged(const char* text) { return strstr(g_fake.log, text) != NULL; }
