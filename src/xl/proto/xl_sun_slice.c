/* The sun's last shadow slice on a world larger than vanilla's. Included by ../co_xl.c.
 *
 * Mission start (StateGame's enter, game 0xAC5D40) turns the static last slice on (0xAC66D0, the only writer of true;
 * the five others write false as a state is left) and sets its half size to [lg+0x1E0] x [lg+0x2D7C] x 0.5 (0xAC66E6..
 * 0xAC6713, lg = [world+0xAE288]). The exe's prepare (0x35F1A0) centres it on the world origin: on a 16384 m world that
 * is +-8074 m at 16-32 m a texel, so shadows stopped at the dynamic slices (19 m at shadows 0, 56 m at 3).
 *
 * Far shadows (all parts or none): the half size is vanilla's 1024 m and the box follows the camera. The slice's cache
 * is last frame's quadrant drawn back every frame at the same texels, all but the tile drawn this frame (render task
 * 0x363790, reproject_depth_quadrant), so a moved box would show misplaced shadows for up to 64 frames. The box moves
 * only when the camera is a quarter of the half size from its centre along a light axis, and that frame draws the
 * whole slice as one tile (subdivisions 0, a setting the game takes): the prepare's locals and the render task's reads
 * of the setting (0x35F140) both give 0 for that frame's counter, so no texel of the old box is restored. Between moves
 * the box stays put in the world and the game's texel snap (0x359930) keeps its grid, so nothing shimmers.
 * Without every pin: the last slice stays dynamic on a world over 2048 m (crisp shadows to 80 m). */
#include <math.h>

#define SUN_SET_BOOL 0x10087C0u  /* game.dll: rcx the settings (world + SUN_SETTINGS), rdx the name, r8b the value */
#define SUN_SET_FLOAT 0x1008AE0u /* the same with xmm2 the value */
#define SUN_SETTINGS 0x7910270u
#define SUN_LEVEL 0xAE288u       /* world+: the level in play; +0x1E0 its size, +0x2D7C the disk fraction */
#define SUN_VANILLA_WORLD 2048.0f
#define SUN_FAR_HALF 1024.0f     /* the render settings' own default, vanilla's 1022 m within a texel */
#define SUN_FAR_STEP 0.25f       /* of the half size: the box keeps at least 768 m about the camera */
/* helldivers2.exe */
#define SUN_SNAP_CALL 0x35FC12u  /* the static branch's call of the texel snap 0x359930(light, box, vp w, vp h) */
#define SUN_SNAP 0x359930u
#define SUN_LEVELS 0x35F140u     /* last_slice_subdivisions, clamped to 3 */
#define SUN_LEVELS_BACK1 0x363BDEu /* the render task's two reads of it: their return addresses */
#define SUN_LEVELS_BACK2 0x364235u

typedef uint64_t (*SunSetBoolFn)(uint64_t settings, const char* name, uint64_t value);
typedef uint64_t (*SunSetFloatFn)(uint64_t settings, const char* name, float value);
typedef uint64_t (*SunSnapFn)(float* light, const float* box, uint64_t width, uint64_t height);
typedef uint32_t (*SunLevelsFn)(void);

/* The box's centre in the world; only the render thread's prepare keeps it. */
typedef struct { int live; float c[3], half; } SunFar;

static struct
{
    SunSetBoolFn orig; SunSetFloatFn setFloat; SunSnapFn snap; SunLevelsFn levels;
    uintptr_t game, exe;
    CoSwitchWrite write;         /* the site's writer: LwZbWrite stops the other threads */
    int state, farOn;            /* state: the bool hook 1 in, -1 refused; farOn: every far part in */
    volatile LONG dynamic, mission, gen, full;
    volatile LONG64 fullClock;   /* the frame counter's address for the frame drawn whole, 0 none */
    volatile LONG fullCount;     /* and its value that frame */
    LONG seen;                   /* the mission generation the centre belongs to */
    SunFar box;
} g_sun;

static int SunSliceDynamic(float world) { return world > SUN_VANILLA_WORLD; }

/* The size of the level the game reads for the half size two instructions after the bool; 0 when unreadable. */
static float SunWorld(uint64_t settings, float* disk)
{
    uint64_t level = 0;
    float size = 0.0f;
    if (SafeRead((uintptr_t)(settings - SUN_SETTINGS + SUN_LEVEL), &level, 8) || SafeRead((uintptr_t)level + 0x1E0, &size, 4)) return 0.0f;
    if (disk != NULL && SafeRead((uintptr_t)level + 0x2D7C, disk, 4)) *disk = 0.0f;
    return size;
}

static void SunMissionOff(void) { InterlockedExchange(&g_sun.mission, 0); InterlockedExchange64(&g_sun.fullClock, 0); }

static uint64_t SunSetBool(uint64_t settings, const char* name, uint64_t value)
{
    float disk = 0.0f, size;
    if (name == NULL || strcmp(name, "use_static_last_slice") != 0) return g_sun.orig(settings, name, value);
    if ((uint8_t)value == 0) SunMissionOff();
    else if (!g_sun.farOn && SunSliceDynamic(size = SunWorld(settings, &disk)))
    {
        InterlockedIncrement(&g_sun.dynamic);
        Note("difficulty: shadows: the sun's last slice stays dynamic on this %.0f m world (the static one would be +-%.0f m "
             "about the origin, vanilla +-1022 m); shadows reach 80 m", (double)size, (double)(size * disk * 0.5f));
        value &= ~(uint64_t)0xFF;
    }
    return g_sun.orig(settings, name, value);
}

/* With far shadows in, mission start's half size on a world over 2048 m becomes vanilla's, and the mission's box
 * follows the camera from its first frame. */
static uint64_t SunSetFloat(uint64_t settings, const char* name, float value)
{
    float size;
    if (g_sun.farOn && name != NULL && strcmp(name, "static_slice_half_size") == 0)
    {
        if (SunSliceDynamic(size = SunWorld(settings, NULL)))
        {
            Note("difficulty: shadows: far shadows on this %.0f m world: the sun's static last slice is +-%.0f m about the camera "
                 "(the game's +-%.0f m about the origin), moved after %.0f m with the whole slice drawn that frame",
                (double)size, (double)SUN_FAR_HALF, (double)value, (double)(SUN_FAR_HALF * SUN_FAR_STEP));
            value = SUN_FAR_HALF;
            InterlockedIncrement(&g_sun.gen);
            InterlockedExchange(&g_sun.mission, 1);
        }
        else SunMissionOff();
    }
    return g_sun.setFloat(settings, name, value);
}

/* 1 when the box moves to `cam`: no box yet, another half size, or the camera over a step from the centre along a
 * light axis. light: the light camera's rows at [rbp+0x30], U at 0, D (the light's direction) at 4, R at 8. */
static int SunFarCentre(SunFar* s, const float* light, const float* cam, float half)
{
    if (!isfinite(cam[0]) || !isfinite(cam[1]) || !isfinite(cam[2]) || !isfinite(half) || half <= 0.0f) return 0;
    const float d[3] = {cam[0] - s->c[0], cam[1] - s->c[1], cam[2] - s->c[2]}, step = half * SUN_FAR_STEP;
    int kept = s->live && s->half == half;
    for (int axis = 0; kept && axis < 3; axis++)
    {
        const float* a = light + 4 * axis;
        if (fabsf(a[0] * d[0] + a[1] * d[1] + a[2] * d[2]) > step) kept = 0;
    }
    if (kept) return 0;
    s->live = 1; s->half = half;
    memcpy(s->c, cam, sizeof s->c);
    return 1;
}

/* The static branch's snap call (a switched site: complex levels only), on the render thread. The game placed the light
 * camera at -D x half; the centre goes in before the snap, so the full view, the tile view and the sampling matrix all
 * take it (they are copied from [rbp+0x30] after this call). */
static uint64_t SunFarSnap(float* light, const float* box, uint64_t width, uint64_t height)
{
    uint8_t* const frame = (uint8_t*)light - 0x30; /* rcx = rbp + 0x30 */
    uint64_t ctx = 0, camera = 0, clock = 0;
    uint32_t count = 0;
    float cam[3];
    if (g_sun.mission && !SafeRead((uintptr_t)frame + 0x4B8, &ctx, 8) && !SafeRead(ctx + 0x38, &camera, 8) && !SafeRead(camera + 0xE8, cam, 12) &&
        !SafeRead(ctx + 0x40, &clock, 8) && !SafeRead(clock + 0x130, &count, 4))
    {
        const LONG gen = g_sun.gen;
        if (g_sun.seen != gen) { g_sun.seen = gen; g_sun.box.live = 0; }
        const int whole = SunFarCentre(&g_sun.box, light, cam, box[2]);
        if (g_sun.box.live)
        {
            light[12] += g_sun.box.c[0]; light[13] += g_sun.box.c[1]; light[14] += g_sun.box.c[2];
            InterlockedExchange64(&g_sun.fullClock, 0);
            if (whole)
            {
                *(uint32_t*)(frame + 0x74) = 1; /* tiles a side, 1 << levels: the slot */
                *(uint64_t*)(frame - 0x38) = 0; /* levels: the tile picker and the SpeedTree view */
                InterlockedExchange(&g_sun.fullCount, (LONG)count);
                InterlockedExchange64(&g_sun.fullClock, (LONG64)(clock + 0x130));
                InterlockedIncrement(&g_sun.full);
            }
        }
    }
    return g_sun.snap(light, box, width, height);
}

/* The render task picks the frame's tile again from the counter and the setting: for the frame drawn whole, the
 * levels the prepare used. */
static uint32_t SunLevelsFor(uintptr_t back, uint32_t levels)
{
    const uintptr_t clock = (uintptr_t)g_sun.fullClock;
    uint32_t count;
    if (clock != 0 && (back == g_sun.exe + SUN_LEVELS_BACK1 || back == g_sun.exe + SUN_LEVELS_BACK2) && !SafeRead(clock, &count, 4) &&
        count == (uint32_t)g_sun.fullCount)
        return 0;
    return levels;
}
static uint32_t SunLevels(void) { return SunLevelsFor((uintptr_t)__builtin_return_address(0), g_sun.levels()); }

/* The prepare's frame and the render task's tile as the far parts rely on them (exe). */
static const struct { uint32_t rva; const char* hex; } kSunExePins[] = {
    {0x35F1A0u, "48 8B C4 4C 89 48 20 4C 89 40 18 48 89 50 10 48 89 48 08 55 53 57 48 8D A8 68 FB FF FF"}, /* r9 at rbp+0x4B8 */
    {0x35F571u, "48 8B B5 B8 04 00 00"},
    {0x35F7B4u, "48 89 45 C8 83 F8 03 76 10 B8 03 00 00 00 EB 05 B8 02 00 00 00 48 89 45 C8"},
    {0x35F816u, "D3 C0 8B CB 44 0F 29 A4 24 E0 04 00 00 F3 0F 59 CE 89 45 74"},
    {0x35F8C1u, "48 8B 5E 38"},
    {0x35F8DFu, "48 8B 4E 20 48 81 C3 B8 00 00 00"},
    /* the light rows: D at rbp+0x40, R at +0x50, U at +0x30; then the static branch through the snap call */
    {0x35F9CBu, "8B 85 B8 02 00 00 41 0F 28 D4 F3 41 0F 59 D0 45 0F 28 CF 89 45 48 41 0F 28 F0 41 0F 28 F8 41 0F 28 C3 41 0F 14 C4 "
                "F3 41 0F 58 D3 F2 0F 11 45 40"},
    {0x35FA92u, "80 7C 24 70 00 41 0F 28 C1 0F 14 C6 0F 28 CF F2 0F 11 45 50 41 0F 28 D1 0F 28 C6 F3 0F 11 BD 98 02 00 00 8B 85 98 02 "
                "00 00 F3 41 0F 59 C2 89 45 58 F3 41 0F 59 CC F3 41 0F 59 D2 F3 41 0F 59 FB F3 0F 5C C8 F3 41 0F 59 F3 F3 45 0F 59 CC "
                "F3 0F 5C D7 F3 0F 11 4D 30 F3 41 0F 5C F1 F3 0F 11 55 34 F3 0F 11 75 38 0F 84 51 01 00 00"},
    {0x35FB67u, "41 83 3A 01 75 08 F3 41 0F 10 72 08 EB 08 F3 0F 10 35 83 A2 34 01 F3 44 0F 59 D6 48 8D 55 08 48 8D 4D 30 C7 45 08 00 "
                "00 00 00 41 0F 28 C8 F3 44 0F 59 DE 48 C7 45 1C 00 00 00 00 F3 44 0F 59 E6 41 0F 28 D0 C7 45 24 01 00 00 00 41 0F 28 "
                "C0 F3 0F 11 75 10 F3 41 0F 5C C2 F3 0F 11 75 18 F3 41 0F 5C CB 44 0F 28 D6 44 0F 57 15 9A AC 34 01 F3 41 0F 5C D4 F3 "
                "4D 0F 2C CE F3 0F 11 85 D8 02 00 00 45 0F 28 DA 8B 85 D8 02 00 00 44 0F 28 E6 0F 14 CA F3 4D 0F 2C C5 89 45 68 F2 0F "
                "11 4D 60 F3 44 0F 11 55 0C F3 44 0F 11 55 14 E8 19 9D FF FF 44 0F 10 4D 08"},
    {0x3601ADu, "48 8B 46 40 48 8B 4D C8 F3 44 0F 11 95 58 01 00 00 F3 44 0F 11 9D 5C 01 00 00 44 8B 88 30 01 00 00 48 8B 95 58 01 00 "
                "00 F3 0F 11 B5 50 01 00 00 F3 44 0F 11 A5 54 01 00 00 4C 8B 85 50 01 00 00 89 4C 24 20 48 8D 8D 40 03 00 00 E8 63 EE "
                "FF FF"},
    {0x360319u, "8B 4D 74 4D 8D 46 10 48 8B 46 40 33 D2 0F AF C9 49 8B D8 C7 44 24 78 C0 00 00 00 8B 80 30 01 00 00 F7 F1 8D 4A 03 89 4D 90"},
    {0x3603B8u, "48 8B 45 C8"},
    {SUN_SNAP, "48 8B C4 48 89 58 08 48 89 70 10 57 48 81 EC 90 00 00 00"},
    {SUN_LEVELS, "48 83 EC 28 45 33 C0 48 8D 0D 6A B5 31 01 41 8D 50 17 E8 E9 66 27 00"},
    {0x363BD9u, "E8 62 B5 FF FF 89 45 10"},
    {0x364230u, "E8 0B AF FF FF 89 45 10"},
    /* the render task's tile: the same counter, and the levels from either read */
    {0x364281u, "49 8B 44 24 40 48 8B D3 8B 4D 10 C7 45 50 00 00 80 3F C7 45 54 00 00 80 3F 44 8B 88 30 01 00 00 4C 8B 45 50 89 4C 24 20 "
                "48 8D 8D 60 05 00 00 F3 44 0F 59 C8 48 89 5D 28 F3 44 0F 59 C0 E8 9D AD FF FF"},
};
#define SUN_EXE_PINS (sizeof kSunExePins / sizeof kSunExePins[0])
static const uint8_t kSunSnapCall[5] = {0xE8, 0x19, 0x9D, 0xFF, 0xFF};
/* mov rax, SunFarSnap; call rax: the snap's arguments pass as they are, and rax is the call's to clobber */
static const uint8_t kSunSnapMod[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xD0};

/* The far parts: their pins, two hooks, then the site. Until the site is in, g_sun.farOn stays 0, the hooks pass every
 * value as given, and the slice falls back to dynamic. A site refused for a thread inside it goes again at the next
 * complex level. */
static void SunFarInstall(void)
{
    if (g_sun.farOn || g_sun.state != 1 || g_sun.exe == 0) return;
    if (g_sun.setFloat == NULL)
    {
        for (size_t i = 0; i < SUN_EXE_PINS; i++)
            if (!ApiCodeMatches(g_sun.exe + kSunExePins[i].rva, kSunExePins[i].hex))
            {
                g_sun.exe = 0;
                Note("difficulty: shadows: far shadows: not this build's code (exe 0x%X); the last slice stays dynamic on XL", kSunExePins[i].rva);
                return;
            }
        if (!ApiCodeMatches(g_sun.game + SUN_SET_FLOAT, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 30") ||
            !ApiCodeMatches(g_sun.game + 0xAC6713u, "E8 C8 23 54 00"))
        { g_sun.exe = 0; Note("difficulty: shadows: far shadows: not this build's code (game); the last slice stays dynamic on XL"); return; }
        g_sun.snap = (SunSnapFn)(g_sun.exe + SUN_SNAP);
        if (LoaderHook("the sun's static slice: the render task's levels for a frame drawn whole", g_sun.exe + SUN_LEVELS, (void*)SunLevels, (void**)&g_sun.levels) ||
            LoaderHook("the sun's static slice: vanilla's half size on XL", g_sun.game + SUN_SET_FLOAT, (void*)SunSetFloat, (void**)&g_sun.setFloat))
        {
            g_sun.exe = 0;
            Note("difficulty: shadows: far shadows: a hook was refused; the last slice stays dynamic on XL");
            return;
        }
    }
    const CoSwitchSite site = {.rva = SUN_SNAP_CALL, .size = 5, .pin = kSunSnapCall, .copy = 5, .back = SUN_SNAP_CALL + 5, .rel = {1},
        .mod = kSunSnapMod, .modLen = sizeof kSunSnapMod, .modBack = SUN_SNAP_CALL + 5,
        .fix = {{2, CO_FIX_IMM64, (uintptr_t)SunFarSnap}}, .fill = 0x90, .what = "the sun's static slice snap call (exe 0x35FC12)"};
    const char* why = co_switch_install_by(g_sun.exe, &site, 1, CO_SWITCH_XL, g_sun.write != NULL ? g_sun.write : LwZbWrite);
    if (why != NULL)
    {
        if (co_switch_check(g_sun.exe, &site, 1) != NULL) g_sun.exe = 0; /* foreign bytes: for good */
        Note("difficulty: shadows: far shadows: %s%s; the last slice stays dynamic on XL", why, g_sun.exe ? " (tried again at the next complex level)" : "");
        return;
    }
    g_sun.farOn = 1;
}

/* With the extra large world's guards, once a process; the far parts again at each complex level until they are in. */
static void SunSliceInstall(uintptr_t game)
{
    if (g_sun.state != 0) { SunFarInstall(); return; }
    g_sun.state = -1;
    if (!ApiCodeMatches(game + SUN_SET_BOOL, "48 89 5C 24 10 48 89 6C 24 18 57 41 56 41 57 48 83 EC 20") ||
        !ApiCodeMatches(game + 0xAC66D0u, "41 B0 01 48 8D 15 06 E3 78 01 49 8D 8F 70 02 91 07 E8 DA 20 54 00 49 8B 87 88 E2 0A 00") ||
        !ApiCodeMatches(game + 0xAC66F4u, "49 8D 8F 70 02 91 07 F3 0F 10 90 E0 01 00 00 F3 0F 59 90 7C 2D 00 00"))
    { Note("difficulty: shadows: not this build's code; the sun's last slice is the game's on every world"); return; }
    if (LoaderHook("the sun's static last slice (dynamic on a world over 2048 m)", game + SUN_SET_BOOL, (void*)SunSetBool, (void**)&g_sun.orig))
    { Note("difficulty: shadows: the render setting writer could not be hooked; the sun's last slice is the game's"); return; }
    g_sun.state = 1;
    g_sun.game = game;
    g_sun.exe = ApiModuleBase("helldivers2.exe", NULL);
    SunFarInstall();
}

static void SunFarRetry(void) { SunFarInstall(); }

#ifdef CO_TEST
int CoXlTestSunInstall(uintptr_t game)
{
    const CoSwitchWrite write = g_sun.write;
    memset(&g_sun, 0, sizeof g_sun);
    g_sun.write = write;
    SunSliceInstall(game);
    return g_sun.state;
}
void CoXlTestSunRetry(void) { SunSliceInstall(g_sun.game); }
LONG CoXlTestSunDynamic(void) { return g_sun.dynamic; }
/* The far parts' exe pins written into a fake exe, and how many there are. */
int CoXlTestSunExePins(uint8_t* exe)
{
    for (size_t i = 0; i < SUN_EXE_PINS; i++)
        for (size_t k = 0; kSunExePins[i].hex[3 * k] != 0; k++)
        {
            unsigned v = 0;
            sscanf(kSunExePins[i].hex + 3 * k, "%2x", &v);
            exe[kSunExePins[i].rva + k] = (uint8_t)v;
            if (kSunExePins[i].hex[3 * k + 2] == 0) break;
        }
    return (int)SUN_EXE_PINS;
}
uint32_t CoXlTestSunExePin(int i) { return kSunExePins[i].rva; }
/* The site's writer for the next install or retry (NULL: the mod's), to stand for a thread found inside the site. */
void CoXlTestSunWrite(int (*write)(uintptr_t, const void*, size_t)) { g_sun.write = write; }
int CoXlTestSunFar(void) { return g_sun.farOn; }
LONG CoXlTestSunFull(void) { return g_sun.full; }
int CoXlTestSunMission(void) { return g_sun.mission; }
/* The original snap the switched site's path calls. */
void CoXlTestSunSnap(uint64_t (*snap)(float*, const float*, uint64_t, uint64_t)) { g_sun.snap = snap; }
uint32_t CoXlTestSunLevels(uintptr_t back, uint32_t levels) { return SunLevelsFor(back, levels); }
int CoXlTestSunCentre(float* c) { memcpy(c, g_sun.box.c, sizeof g_sun.box.c); return g_sun.box.live; }
#endif
