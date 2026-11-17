/* A stand-in for the HellForge loader: an empty "game.dll" image in memory and the host calls the mod uses. */
#ifndef CO_FAKE_HOST_H
#define CO_FAKE_HOST_H

#include "co.h"

#define FAKE_IMAGE_SIZE 0x3500000u
#define FAKE_HOOKS 16

typedef struct FakeHost
{
    uint8_t* image;
    const char* build;
    int failWriteAt;          /* the write with this number (from 0) fails; -1 for none */
    int writes;
    uintptr_t noMatch[4];     /* code_matches answers 0 for these addresses */
    uintptr_t noHook;         /* hook fails for this address */
    struct { uintptr_t target; void* detour; } hooks[FAKE_HOOKS];
    int hookCount;
    HfEventCallback loaded;
    HfTickCallback tick;      /* on_game_thread's callback, 0 when none was asked for */
    char log[8192];
} FakeHost;

extern FakeHost g_fake;

/* A zeroed image and an empty log; returns the api to hand to the mod. */
const HfApi* fake_host_reset(const char* build);
void* fake_host_detour(uintptr_t rva);
int fake_host_logged(const char* text);

#endif
