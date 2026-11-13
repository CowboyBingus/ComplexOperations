/* A main objective waits for the steps of its own mission (co_unlock.h). */
#include <string.h>

#include <windows.h>

#include "co_unlock.h"
#include "co_shim.h"

#define WORLD 0x3326340u         /* [game+..]+0xAE288 the level generator, whose +0x2C8 is the generation block */
#define UNITS 0x3326308u         /* [[[game+..]+0x18]+0x710](handle): an entity's unit, as 0x5D2F0C asks it */
#define PREVIEWS 0x347CEE8u      /* [game+..]+0x4168CC the war table's shown preview record, -1 none (0x142CBDB) */
#define LOCATIONS 0x11A715Cu     /* the generation block's important location count (not moved by HellForge) */
#define RECORD 0x108u
#define HOLO_SLOTS 0x886B0u      /* the hologram's slots, 0x180 each, count at +0x8B730 (0x142CB90) */
#define HOLO_SLOT 0x180u
#define HOLO_COUNT 0x8B730u
#define HOLO_SKIP 0x17Cu         /* a slot's "no line" byte, read only by the line draw (0x14325A8) */

const uint8_t kCoUnlockA[21] = {0x48, 0x8B, 0x42, 0x68, 0x40, 0x0F, 0xB6, 0xF6, 0x83, 0x7C, 0x18, 0x20, 0x02, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x45, 0xF0};
const uint8_t kCoUnlockB[11] = {0x48, 0x8B, 0x40, 0x68, 0x83, 0x7C, 0x02, 0x20, 0x02, 0x75, 0x19};
static const char kUnitsPin[] = "48 8B 05 ?? ?? ?? ?? 48 8B 48 18 48 8B 81 10 07 00 00 41 8B 4C 24 0C FF D0 48 8B 0D ?? ?? ?? ?? 45 8B C6 "
    "48 8B 91 88 E2 0A 00 4C 8B 92 C8 02 00 00 45 8B 8A 5C 71 1A 01";
static const char kMarkersPin[] = "48 89 4C 24 08 53 55 56 57 41 55 41 56 41 57 48 83 EC 10 4C 8B 99 C8 02 00 00";
static const char kLinesPin[] = "4C 8B DC 55 56 57 41 55 41 56 49 8D AB 38 FF FF FF 48 81 EC A0 01 00 00";
static const char kLinesLoopPin[] = "80 BC 3B 2C 88 08 00 00 0F 85 ?? ?? ?? ?? 83 BC 3B DC 86 08 00 24";
static const char kPreviewPin[] = "41 8B 82 CC 68 41 00 83 F8 FF 0F 84 ?? ?? ?? ?? 48 89 B4 24 00 03 00 00 48 8B 35 ?? ?? ?? ?? "
    "48 69 C8 F0 4F 02 00 48 81 C6 A0 E2 0A 00";
#define UNITS_PIN_AT 0x5D2F0Cu
#define LINES_LOOP_AT 0x14325A8u
#define PREVIEW_PIN_AT 0x142CBDBu

typedef uint64_t (*UnitOf)(uint32_t handle);
typedef uint32_t (*MarkersFn)(uintptr_t lg, uint64_t* pos, const uint32_t* in, uint32_t* out);
typedef uint64_t (*LinesFn)(uintptr_t holo, uintptr_t b, uintptr_t step, uintptr_t d, float alpha);

static SRWLOCK g_lock = SRWLOCK_INIT; /* the note, the cache and the hologram's table */
static struct
{
    uintptr_t lg, gen;
    uint32_t n, location[CO_UNLOCK_NOTED], id[CO_UNLOCK_NOTED];
    uint8_t mission[CO_UNLOCK_NOTED];
} g_note;
static struct { uintptr_t entity; uint32_t id, handle; uint8_t mission; } g_cache[256];
static uint32_t g_cached;
/* a preview record's markers (keyed by its ids, R+0x2234): each slot's id and mission */
static struct { uintptr_t key; uint32_t count, id[CO_UNLOCK_SLOTS]; uint8_t mission[CO_UNLOCK_SLOTS]; } g_shown[8];
static uint32_t g_shownNext;
static struct { uintptr_t game; uint32_t list; uint8_t* page; volatile uint8_t* live; MarkersFn markers; LinesFn lines; } g_unlock;

int co_unlock_rule(uint8_t mainMission, uint8_t stepMission)
{
    return mainMission == CO_UNLOCK_NONE || stepMission == CO_UNLOCK_NONE || mainMission == stepMission;
}

void co_unlock_note(uintptr_t lg, uintptr_t gen, const uint32_t* location, const uint32_t* id, const uint8_t* mission, uint32_t n)
{
    AcquireSRWLockExclusive(&g_lock);
    memset(&g_note, 0, sizeof g_note);
    g_note.lg = lg; g_note.gen = gen;
    for (uint32_t i = 0; i < n && g_note.n < CO_UNLOCK_NOTED; i++)
    {
        if (mission[i] == CO_UNLOCK_NONE) continue;
        g_note.location[g_note.n] = location[i]; g_note.id[g_note.n] = id[i]; g_note.mission[g_note.n++] = mission[i];
    }
    g_cached = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

void co_unlock_forget(void)
{
    AcquireSRWLockExclusive(&g_lock);
    memset(&g_note, 0, sizeof g_note);
    g_cached = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

static uint8_t Noted(uintptr_t lg, uintptr_t gen, uint32_t l, uint32_t id) /* under the lock */
{
    if (lg == 0 || g_note.lg != lg || g_note.gen != gen) return CO_UNLOCK_NONE;
    for (uint32_t i = 0; i < g_note.n; i++)
        if (g_note.location[i] == l) return g_note.id[i] == id ? g_note.mission[i] : (uint8_t)CO_UNLOCK_NONE;
    return CO_UNLOCK_NONE;
}

uint8_t co_unlock_noted(uintptr_t lg, uintptr_t gen, uint32_t l, uint32_t id)
{
    AcquireSRWLockShared(&g_lock);
    const uint8_t m = Noted(lg, gen, l, id);
    ReleaseSRWLockShared(&g_lock);
    return m;
}

static uintptr_t Ptr(uintptr_t at) { return at != 0 ? *(const uintptr_t*)at : 0; }

/* An instance's mission: its unit's location, found as the game finds it (0x5D2F0C..0x5D2F7E: the first location
 * whose +0x98 record holds the unit at +0xA8). Direct reads: the game reads the same memory on this thread. */
static uint8_t MissionOf(uintptr_t manager, uint32_t index) /* under the exclusive lock */
{
    const uintptr_t entity = Ptr(Ptr(manager + 0x50) + 8ull * index);
    if (entity == 0) return CO_UNLOCK_NONE;
    const uint32_t id = *(const uint32_t*)(entity + 8), handle = *(const uint32_t*)(entity + 0xC);
    for (uint32_t i = 0; i < g_cached; i++)
        if (g_cache[i].entity == entity && g_cache[i].id == id && g_cache[i].handle == handle) return g_cache[i].mission;
    uint8_t mission = CO_UNLOCK_NONE;
    const uintptr_t lg = Ptr(Ptr(g_unlock.game + WORLD) + 0xAE288), gen = Ptr(lg + 0x2C8);
    const UnitOf unitOf = (UnitOf)Ptr(Ptr(Ptr(g_unlock.game + UNITS) + 0x18) + 0x710);
    if (gen != 0 && unitOf != NULL && handle != 0 && g_note.n != 0 && g_note.lg == lg && g_note.gen == gen)
    {
        const uint64_t unit = unitOf(handle);
        const uint32_t count = *(const uint32_t*)(gen + LOCATIONS);
        for (uint32_t l = 0; unit != 0 && l < count; l++)
        {
            const uintptr_t loc = gen + g_unlock.list + (uintptr_t)RECORD * l, record = Ptr(loc + 0x98);
            if (record == 0 || *(const uint64_t*)(record + 0xA8) != unit) continue;
            mission = Noted(lg, gen, l, *(const uint32_t*)(loc + 0xD8));
            break;
        }
    }
    if (g_cached < sizeof g_cache / sizeof g_cache[0])
    {
        g_cache[g_cached].entity = entity; g_cache[g_cached].id = id; g_cache[g_cached].handle = handle; g_cache[g_cached++].mission = mission;
    }
    return mission;
}

uint64_t co_unlock_holds(uintptr_t manager, uint32_t main, uint32_t step)
{
    AcquireSRWLockExclusive(&g_lock);
    const int holds = co_unlock_rule(MissionOf(manager, main), MissionOf(manager, step));
    ReleaseSRWLockExclusive(&g_lock);
    return (uint64_t)holds;
}

void co_unlock_live(int complex) { if (g_unlock.live != NULL) *g_unlock.live = (uint8_t)(complex != 0); }

static void Rel(uint8_t* out, size_t at, uintptr_t code, uintptr_t target)
{
    const int32_t rel = (int32_t)((int64_t)target - (int64_t)(code + at + 4)); /* both lie within a jump of game.dll */
    memcpy(out + at, &rel, 4);
}

/* cmp byte [rip -> flag], 0: 7 bytes, the displacement counted from the immediate's end */
static void Test(uint8_t* out, uintptr_t code, uintptr_t flag)
{
    const int32_t rel = (int32_t)((int64_t)flag - (int64_t)(code + 7));
    out[0] = 0x80; out[1] = 0x3D; memcpy(out + 2, &rel, 4); out[6] = 0x00;
}

size_t co_unlock_stub_a(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t holds)
{
    static const uint8_t stub[CO_UNLOCK_A_STUB] = {
        0, 0, 0, 0, 0, 0, 0,                              /*   0 cmp byte [flag], 0 */
        0x75, 0x1A,                                       /*   7 jne 35 */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 9 the game's 21 bytes */
        0xE9, 0, 0, 0, 0,                                 /*  30 jmp 0x5D36BD */
        0x48, 0x8B, 0x42, 0x68,                           /*  35 mov rax, [rdx+0x68] (the net records) */
        0x40, 0x0F, 0xB6, 0xF6,                           /*  39 movzx esi, sil */
        0x83, 0x7C, 0x18, 0x20, 0x02,                     /*  43 cmp dword [rax+rbx+0x20], 2 */
        0x0F, 0x84, 0, 0, 0, 0,                           /*  48 je 0x5D36BD: a done step holds nothing */
        0x52,                                             /*  54 push rdx (the manager, read at 0x5D36BD) */
        0x48, 0x83, 0xEC, 0x28,                           /*  55 sub rsp, 0x28 (16-aligned with the push) */
        0x48, 0x89, 0xD1,                                 /*  59 mov rcx, rdx */
        0x44, 0x89, 0xEA,                                 /*  62 mov edx, r13d (the main) */
        0x41, 0x89, 0xF8,                                 /*  65 mov r8d, edi (the step) */
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,               /*  68 mov rax, holds */
        0xFF, 0xD0,                                       /*  78 call rax */
        0x48, 0x83, 0xC4, 0x28,                           /*  80 add rsp, 0x28 */
        0x5A,                                             /*  84 pop rdx */
        0x85, 0xC0,                                       /*  85 test eax, eax */
        0x0F, 0x84, 0, 0, 0, 0,                           /*  87 je 0x5D36BD */
        0xBE, 0x01, 0x00, 0x00, 0x00,                     /*  93 mov esi, 1 (held) */
        0xE9, 0, 0, 0, 0};                                /*  98 jmp 0x5D36BD */
    const uint64_t to = holds;
    memcpy(out, stub, sizeof stub);
    Test(out, code, flag);
    memcpy(out + 9, kCoUnlockA, sizeof kCoUnlockA); /* no address in them */
    Rel(out, 31, code, game + CO_UNLOCK_A_BACK);
    Rel(out, 50, code, game + CO_UNLOCK_A_BACK);
    memcpy(out + 70, &to, 8);
    Rel(out, 89, code, game + CO_UNLOCK_A_BACK);
    Rel(out, 99, code, game + CO_UNLOCK_A_BACK);
    return sizeof stub;
}

size_t co_unlock_stub_b(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t holds)
{
    static const uint8_t stub[CO_UNLOCK_B_STUB] = {
        0, 0, 0, 0, 0, 0, 0,                              /*   0 cmp byte [flag], 0 */
        0x75, 0x14,                                       /*   7 jne 29 */
        0x48, 0x8B, 0x40, 0x68,                           /*   9 mov rax, [rax+0x68] (the game's) */
        0x83, 0x7C, 0x02, 0x20, 0x02,                     /*  13 cmp dword [rdx+rax+0x20], 2 */
        0x0F, 0x85, 0, 0, 0, 0,                           /*  18 jne 0x5D3FAF (the game's jne, rel32) */
        0xE9, 0, 0, 0, 0,                                 /*  24 jmp 0x5D3F96 */
        0x48, 0x8B, 0x40, 0x68,                           /*  29 mov rax, [rax+0x68] */
        0x83, 0x7C, 0x02, 0x20, 0x02,                     /*  33 cmp dword [rdx+rax+0x20], 2 */
        0x0F, 0x84, 0, 0, 0, 0,                           /*  38 je 0x5D3F96: a done step holds nothing */
        0x51, 0x52, 0x41, 0x50, 0x41, 0x51,               /*  44 push rcx, rdx, r8, r9 (the loop's) */
        0x41, 0x52, 0x41, 0x53,                           /*  50 push r10, r11 */
        0x48, 0x83, 0xEC, 0x20,                           /*  54 sub rsp, 0x20 (16-aligned with six pushes) */
        0x44, 0x8B, 0xC1,                                 /*  58 mov r8d, ecx (the step) */
        0x44, 0x89, 0xDA,                                 /*  61 mov edx, r11d (the main) */
        0x48, 0x8B, 0x8C, 0x24, 0x80, 0x00, 0x00, 0x00,   /*  64 mov rcx, [rsp+0x80] (the manager, [rsp+0x30] at the site) */
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,               /*  72 mov rax, holds */
        0xFF, 0xD0,                                       /*  82 call rax */
        0x48, 0x83, 0xC4, 0x20,                           /*  84 add rsp, 0x20 */
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58,   /*  88 pop r11, r10, r9, r8 */
        0x5A, 0x59,                                       /*  96 pop rdx, rcx */
        0x85, 0xC0,                                       /*  98 test eax, eax */
        0x0F, 0x85, 0, 0, 0, 0,                           /* 100 jne 0x5D3FAF (held) */
        0xE9, 0, 0, 0, 0};                                /* 106 jmp 0x5D3F96 (rax is read again there) */
    const uint64_t to = holds;
    memcpy(out, stub, sizeof stub);
    Test(out, code, flag);
    Rel(out, 20, code, game + CO_UNLOCK_B_HOLD);
    Rel(out, 25, code, game + CO_UNLOCK_B_NEXT);
    Rel(out, 40, code, game + CO_UNLOCK_B_NEXT);
    memcpy(out + 74, &to, 8);
    Rel(out, 102, code, game + CO_UNLOCK_B_HOLD);
    Rel(out, 107, code, game + CO_UNLOCK_B_NEXT);
    return sizeof stub;
}

const uint8_t kCoUnlockC[9] = {0x48, 0x8B, 0x44, 0x24, 0x30, 0x48, 0x8B, 0x55, 0x80};
const uint8_t kCoUnlockD[9] = {0x48, 0x8B, 0x41, 0x68, 0x44, 0x39, 0x74, 0x03, 0x3C};

size_t co_unlock_stub_c(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t holds)
{
    static const uint8_t stub[CO_UNLOCK_C_STUB] = {
        0, 0, 0, 0, 0, 0, 0,                              /*  0 cmp byte [flag], 0 */
        0x75, 0x0E,                                       /*  7 jne 23 */
        0, 0, 0, 0, 0, 0, 0, 0, 0,                        /*  9 the game's 9 bytes */
        0xE9, 0, 0, 0, 0,                                 /* 18 jmp 0x5D365C */
        0x48, 0x83, 0xEC, 0x20,                           /* 23 sub rsp, 0x20 (volatile registers are dead here) */
        0x48, 0x8B, 0x4C, 0x24, 0x50,                     /* 27 mov rcx, [rsp+0x50] (the manager) */
        0x44, 0x89, 0xEA,                                 /* 32 mov edx, r13d (the objective) */
        0x41, 0x89, 0xF8,                                 /* 35 mov r8d, edi (the other) */
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,               /* 38 mov rax, holds */
        0xFF, 0xD0,                                       /* 48 call rax */
        0x48, 0x83, 0xC4, 0x20,                           /* 50 add rsp, 0x20 */
        0x85, 0xC0,                                       /* 54 test eax, eax */
        0x0F, 0x84, 0, 0, 0, 0,                           /* 56 je 0x5D3686: another mission's */
        0, 0, 0, 0, 0, 0, 0, 0, 0,                        /* 62 the game's 9 bytes */
        0xE9, 0, 0, 0, 0};                                /* 71 jmp 0x5D365C */
    const uint64_t to = holds;
    memcpy(out, stub, sizeof stub);
    Test(out, code, flag);
    memcpy(out + 9, kCoUnlockC, sizeof kCoUnlockC); memcpy(out + 62, kCoUnlockC, sizeof kCoUnlockC); /* no address in them */
    Rel(out, 19, code, game + CO_UNLOCK_C_NEXT);
    memcpy(out + 40, &to, 8);
    Rel(out, 58, code, game + CO_UNLOCK_C_SKIP);
    Rel(out, 72, code, game + CO_UNLOCK_C_NEXT);
    return sizeof stub;
}

size_t co_unlock_stub_d(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag, uintptr_t holds)
{
    static const uint8_t stub[CO_UNLOCK_D_STUB] = {
        0, 0, 0, 0, 0, 0, 0,                              /*  0 cmp byte [flag], 0 */
        0x75, 0x0E,                                       /*  7 jne 23 */
        0, 0, 0, 0, 0, 0, 0, 0, 0,                        /*  9 the game's 9 bytes (its cmp's flags go back) */
        0xE9, 0, 0, 0, 0,                                 /* 18 jmp 0x5D3EA7 */
        0x51,                                             /* 23 push rcx (the manager, read again at the loop's top) */
        0x48, 0x83, 0xEC, 0x28,                           /* 24 sub rsp, 0x28 (16-aligned with the push) */
        0x8B, 0x54, 0x24, 0x6C,                           /* 28 mov edx, [rsp+0x6C] (the objective, [rsp+0x3c] at the site) */
        0x41, 0x89, 0xF8,                                 /* 32 mov r8d, edi (the other) */
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,               /* 35 mov rax, holds */
        0xFF, 0xD0,                                       /* 45 call rax */
        0x48, 0x83, 0xC4, 0x28,                           /* 47 add rsp, 0x28 */
        0x59,                                             /* 51 pop rcx */
        0x85, 0xC0,                                       /* 52 test eax, eax */
        0x0F, 0x84, 0, 0, 0, 0,                           /* 54 je 0x5D3EB5: another mission's */
        0, 0, 0, 0, 0, 0, 0, 0, 0,                        /* 60 the game's 9 bytes */
        0xE9, 0, 0, 0, 0};                                /* 69 jmp 0x5D3EA7 */
    const uint64_t to = holds;
    memcpy(out, stub, sizeof stub);
    Test(out, code, flag);
    memcpy(out + 9, kCoUnlockD, sizeof kCoUnlockD); memcpy(out + 60, kCoUnlockD, sizeof kCoUnlockD);
    Rel(out, 19, code, game + CO_UNLOCK_D_NEXT);
    memcpy(out + 37, &to, 8);
    Rel(out, 56, code, game + CO_UNLOCK_D_SKIP);
    Rel(out, 70, code, game + CO_UNLOCK_D_NEXT);
    return sizeof stub;
}

uint32_t co_unlock_hide(const uint8_t* mission, uint32_t slots, uint32_t step)
{
    uint32_t hide = 0;
    if (slots > CO_UNLOCK_SLOTS || step >= slots || mission[step] == CO_UNLOCK_NONE) return 0;
    for (uint32_t s = 0; s < slots; s++)
        if (s != step && mission[s] != CO_UNLOCK_NONE && mission[s] != mission[step]) hide |= 1u << s;
    return hide;
}

/* The marker source: its outputs past the descriptor's ones are the locations' markers at the location's exact x/y
 * (0xF738B7), the first 32 the hologram's slots (0x18A2BA0 makes output k marker k). Matched here, before the jitter. */
static uint32_t Markers(uintptr_t lg, uint64_t* pos, const uint32_t* in, uint32_t* out)
{
    const uint32_t n = g_unlock.markers(lg, pos, in, out);
    const uintptr_t key = (uintptr_t)out;
    AcquireSRWLockExclusive(&g_lock);
    uint32_t at = 0;
    while (at < 8 && g_shown[at].key != key) at++;
    if (at == 8) at = g_shownNext++ % 8;
    memset(&g_shown[at], 0, sizeof g_shown[at]);
    const uintptr_t gen = lg != 0 ? *(const uintptr_t*)(lg + 0x2C8) : 0;
    if (n > 0 && g_note.n != 0 && g_note.lg == lg && g_note.gen == gen)
    {
        g_shown[at].key = key;
        g_shown[at].count = n < CO_UNLOCK_SLOTS ? n : CO_UNLOCK_SLOTS;
        for (uint32_t k = 0; k < g_shown[at].count; k++)
        {
            g_shown[at].id[k] = out[k];
            g_shown[at].mission[k] = CO_UNLOCK_NONE;
            for (uint32_t i = 0; i < g_note.n; i++)
            {
                const uintptr_t loc = gen + g_unlock.list + (uintptr_t)RECORD * g_note.location[i];
                if (memcmp((const void*)(loc + 0xA4), &pos[k], 8) != 0) continue;
                if (*(const uint32_t*)(loc + 0xD8) == g_note.id[i]) g_shown[at].mission[k] = g_note.mission[i];
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return n;
}

/* A step's lines: the slots of another mission get the skip byte the draw tests for the call's length only, on this
 * thread; the shown record is found as the slot builder finds it (0x142CBDB), and each slot's id must still be the one
 * its marker had. */
static uint64_t Lines(uintptr_t holo, uintptr_t b, uintptr_t step, uintptr_t d, float alpha)
{
    uint8_t mission[CO_UNLOCK_SLOTS];
    uint32_t hide = 0;
    const uintptr_t previews = Ptr(g_unlock.game + PREVIEWS), world = Ptr(g_unlock.game + WORLD);
    const uint32_t slots = *(const uint32_t*)(holo + HOLO_COUNT);
    const uint32_t shown = previews != 0 ? *(const uint32_t*)(previews + 0x4168CC) : 0xFFFFFFFFu;
    const uintptr_t record = shown != 0xFFFFFFFFu && world != 0 ? world + 0xAE2A0 + 0x24FF0ull * shown : 0;
    const uintptr_t first = holo + HOLO_SLOTS;
    if (record != 0 && *(const uint32_t*)record == 3 && slots <= CO_UNLOCK_SLOTS && step >= first && (step - first) % HOLO_SLOT == 0)
    {
        AcquireSRWLockShared(&g_lock);
        for (uint32_t at = 0; at < 8; at++)
        {
            if (g_shown[at].key != record + 0x2234 || g_shown[at].key == 0) continue;
            for (uint32_t s = 0; s < slots; s++)
                mission[s] = s < g_shown[at].count && *(const uint32_t*)(record + 0x2234 + 4ull * s) == g_shown[at].id[s] ? g_shown[at].mission[s] : (uint8_t)CO_UNLOCK_NONE;
            hide = co_unlock_hide(mission, slots, (uint32_t)((step - first) / HOLO_SLOT));
            break;
        }
        ReleaseSRWLockShared(&g_lock);
    }
    uint32_t set = 0;
    for (uint32_t s = 0; s < slots && hide != 0; s++)
    {
        uint8_t* skip = (uint8_t*)(first + (uintptr_t)HOLO_SLOT * s + HOLO_SKIP);
        if ((hide >> s & 1u) && *skip == 0) { *skip = 1; set |= 1u << s; }
    }
    const uint64_t r = g_unlock.lines(holo, b, step, d, alpha);
    for (uint32_t s = 0; s < slots && set != 0; s++)
        if (set >> s & 1u) *(uint8_t*)(first + (uintptr_t)HOLO_SLOT * s + HOLO_SKIP) = 0;
    return r;
}

static int Same(uintptr_t at, const uint8_t* bytes, size_t size)
{
    uint8_t now[32];
    return size <= sizeof now && g_coApi->read(at, now, size) == 0 && memcmp(now, bytes, size) == 0;
}

const char* co_unlock_check(uintptr_t game)
{
    if (!Same(game + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA)) return "the objectives' activation pass is not the game's code this mod was made for";
    if (!Same(game + CO_UNLOCK_B_SITE, kCoUnlockB, sizeof kCoUnlockB)) return "the objectives' waiting pass is not the game's code this mod was made for";
    if (!g_coApi->code_matches(game + UNITS_PIN_AT, kUnitsPin)) return "the objectives' location lookup is not the game's code this mod was made for";
    return NULL;
}

/* The sequential rule's two sites (stubs at page+0x100 and +0x180), both or neither. Refused, an objective of the
 * rule waits for the earlier ones of every mission, as in the game; the steps' rule stands. */
static const char* SequentialSites(uintptr_t game, const uint8_t* page)
{
    uint8_t c[sizeof kCoUnlockC], d[sizeof kCoUnlockD];
    if (!Same(game + CO_UNLOCK_C_SITE, kCoUnlockC, sizeof c) || !Same(game + CO_UNLOCK_D_SITE, kCoUnlockD, sizeof d))
        return "the objectives' order test is not the game's code this mod was made for";
    memset(c, 0x90, sizeof c); memset(d, 0x90, sizeof d);
    c[0] = d[0] = 0xE9;
    int32_t rel = (int32_t)((int64_t)(uintptr_t)(page + 0x100) - (int64_t)(game + CO_UNLOCK_C_SITE + 5u)); memcpy(c + 1, &rel, 4);
    rel = (int32_t)((int64_t)(uintptr_t)(page + 0x180) - (int64_t)(game + CO_UNLOCK_D_SITE + 5u)); memcpy(d + 1, &rel, 4);
    if (g_coApi->write(game + CO_UNLOCK_C_SITE, c, sizeof c) != 0) return "the activation pass's order test could not be written";
    if (g_coApi->write(game + CO_UNLOCK_D_SITE, d, sizeof d) != 0)
    {
        g_coApi->write(game + CO_UNLOCK_C_SITE, kCoUnlockC, sizeof kCoUnlockC);
        return "the waiting pass's order test could not be written";
    }
    return NULL;
}

static const char* HologramCheck(uintptr_t game)
{
    if (!g_coApi->code_matches(game + CO_UNLOCK_MARKERS, kMarkersPin)) return "the war table's marker source is not the game's code this mod was made for";
    if (!g_coApi->code_matches(game + CO_UNLOCK_LINES, kLinesPin) || !g_coApi->code_matches(game + LINES_LOOP_AT, kLinesLoopPin))
        return "the war table's line draw is not the game's code this mod was made for";
    if (!g_coApi->code_matches(game + PREVIEW_PIN_AT, kPreviewPin)) return "the war table's shown preview is not the game's code this mod was made for";
    return NULL;
}

const char* co_unlock_install(uintptr_t game, uint32_t list)
{
    DWORD old;
    const char* why;
    if (g_unlock.page != NULL) return NULL;
    if (list == 0) return "the important locations' place is not known";
    if ((why = co_unlock_check(game)) != NULL) return why;
    uint8_t* page = (uint8_t*)co_alloc_near(game + CO_UNLOCK_A_SITE, 0x2000);
    if (page == NULL) return "no memory near the game for the objectives' stubs";
    volatile uint8_t* live = page + 0x1000;
    *live = 0;
    co_unlock_stub_a(page, (uintptr_t)page, game, (uintptr_t)live, (uintptr_t)co_unlock_holds);
    co_unlock_stub_b(page + 0x80, (uintptr_t)(page + 0x80), game, (uintptr_t)live, (uintptr_t)co_unlock_holds);
    co_unlock_stub_c(page + 0x100, (uintptr_t)(page + 0x100), game, (uintptr_t)live, (uintptr_t)co_unlock_holds);
    co_unlock_stub_d(page + 0x180, (uintptr_t)(page + 0x180), game, (uintptr_t)live, (uintptr_t)co_unlock_holds);
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return "the objectives' stubs could not be made to run"; }
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    uint8_t a[sizeof kCoUnlockA], b[sizeof kCoUnlockB];
    memset(a, 0x90, sizeof a); memset(b, 0x90, sizeof b);
    a[0] = b[0] = 0xE9;
    int32_t rel = (int32_t)((int64_t)(uintptr_t)page - (int64_t)(game + CO_UNLOCK_A_SITE + 5u)); memcpy(a + 1, &rel, 4);
    rel = (int32_t)((int64_t)(uintptr_t)(page + 0x80) - (int64_t)(game + CO_UNLOCK_B_SITE + 5u)); memcpy(b + 1, &rel, 4);
    g_unlock.game = game; g_unlock.list = list;
    if (g_coApi->write(game + CO_UNLOCK_A_SITE, a, sizeof a) != 0) { VirtualFree(page, 0, MEM_RELEASE); return "the objectives' activation pass could not be written"; }
    if (g_coApi->write(game + CO_UNLOCK_B_SITE, b, sizeof b) != 0)
    {
        if (g_coApi->write(game + CO_UNLOCK_A_SITE, kCoUnlockA, sizeof kCoUnlockA) == 0) VirtualFree(page, 0, MEM_RELEASE);
        return "the objectives' waiting pass could not be written";
    }
    g_unlock.page = page; g_unlock.live = live;
    if ((why = SequentialSites(game, page)) != NULL) co_note("complex operations: mission unlock: %s; an ordered objective waits for every mission's earlier ones", why);
    /* the lines are a picture: without them the hologram keeps the game's lines and the unlock stands */
    if ((why = HologramCheck(game)) != NULL) co_note("complex operations: mission unlock: %s; the war table draws every step's lines", why);
    else if (co_loader_hook("the war table's marker source (each marker's mission)", game + CO_UNLOCK_MARKERS, (void*)Markers, (void**)&g_unlock.markers) == 0 &&
        co_loader_hook("the war table's step lines (its own mission's mains)", game + CO_UNLOCK_LINES, (void*)Lines, (void**)&g_unlock.lines) != 0)
        co_note("complex operations: mission unlock: the war table draws every step's lines");
    return NULL;
}

#ifdef CO_TEST
void co_unlock_test_reset(void)
{
    co_unlock_forget();
    memset(&g_unlock, 0, sizeof g_unlock); memset(g_shown, 0, sizeof g_shown); g_shownNext = 0;
}
const uint8_t* co_unlock_test_page(void) { return g_unlock.page; }
uint32_t co_unlock_test_markers(uintptr_t lg, uint64_t* pos, const uint32_t* in, uint32_t* out) { return Markers(lg, pos, in, out); }
uint64_t co_unlock_test_lines(uintptr_t holo, uintptr_t b, uintptr_t step, uintptr_t d, float alpha) { return Lines(holo, b, step, d, alpha); }
#endif
