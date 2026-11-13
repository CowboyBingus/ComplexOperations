/* A stand-in for the HellForge loader: an empty "game.dll" image in memory and the host calls the mod uses. */
#ifndef CO_FAKE_HOST_H
#define CO_FAKE_HOST_H

#include "co.h"

/* As far as the figures' placement system (FAKE_UPS) behind the status effects' table at game+0x37CB600. */
#define FAKE_IMAGE_SIZE 0x39A0000u
#define FAKE_HOOKS 16
/* The engine's per-peer loadout table (four entries of 0x9F8, 8 modulo 16 as in the game) and the registry that lists
 * it, at free places in the image. The globals that lead to them are set as the game sets them at startup. */
#define FAKE_SYNCED_TABLE 0x3490008u
#define FAKE_REGISTRY 0x34A0000u
#define FAKE_SYNCED_POINTER 0x346D5F0u
#define FAKE_ENGINE_GLOBAL 0x3326340u
#define FAKE_REGISTRY_GLOBAL 0x3326E68u
#define FAKE_SYNCED_IN_ENGINE 0x710B428u
/* The placement system with the figure records at +0xC008, where engine init puts it: 0x65DA300 behind the arena whose
 * +0x60E7208 is the per-peer table. */
#define FAKE_UPS_POINTER 0x347CE60u
#define FAKE_UPS (FAKE_SYNCED_TABLE - 0x60E7208u + 0x65DA300u)
#define FAKE_UPS_SIZE 0x1B218u

typedef struct FakeHost
{
    uint8_t* image;
    const char* build;
    int failWriteAt;          /* the write with this number (from 0) fails; -1 for none */
    int writes;
    uintptr_t noMatch[4];     /* code_matches answers 0 for these addresses */
    uintptr_t noMatchExe[2];  /* and for these RVAs in the exe */
    uintptr_t wrote[64];      /* every write asked for, refused or not, in order (the first 64) */
    int wroteCount;
    int strictCode;           /* code_matches compares the pattern with the bytes there (?? any) */
    uintptr_t noHook;         /* hook fails for this address */
    struct { uintptr_t target; void* detour; } hooks[FAKE_HOOKS];
    int hookCount;
    struct { uintptr_t target; void* original; } originals[4]; /* what a hook of this target calls as the original */
    struct { uintptr_t rva; uint8_t bytes[16]; } underHook[2]; /* another's hook here: read_code gives these, read the jump */
    HfEventCallback loaded;
    char log[8192];
    /* The map generator's room: the request's answer (1 refuses), what `have` reads back (size 0: a HellForge without
     * the regions fills none of them), and the last request. */
    int capacityRefused;
    HfGenerationCapacity capacity, wanted;
    uintptr_t levelRow;       /* difficulty_row(11) */
    uintptr_t levelTen;       /* difficulty_row(10) */
    uint8_t* exe;             /* "helldivers2.exe", once fake_host_exe was asked for it; reads and writes reach it */
    uint32_t loadPhase;
    HfTickCallback tick;      /* the last on_game_thread callback */
    /* The session: the role, the host's row as this machine holds it (0 bytes: none), what share_set and send answer
     * (1 refuses) and what they were last given. */
    uint32_t role, subscribed;
    uint8_t row[64]; uint32_t rowBytes;
    int shareRefused, shareSets, sendRefused, sends;
    uint64_t sentTo; uint8_t sent[64]; uint32_t sentBytes;
} FakeHost;

extern FakeHost g_fake;

/* A zeroed image and an empty log; returns the api to hand to the mod. */
const HfApi* fake_host_reset(const char* build);
/* An empty engine image of FAKE_EXE_SIZE for this case. */
#define FAKE_EXE_SIZE 0x2800000u
uint8_t* fake_host_exe(void);
void* fake_host_detour(uintptr_t rva);
int fake_host_logged(const char* text);

#endif
