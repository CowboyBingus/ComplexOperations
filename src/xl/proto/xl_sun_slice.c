/* The sun's last shadow slice on a world larger than vanilla's. Included by ../co_xl.c.
 *
 * Mission start (StateGame's enter, game 0xAC5D40) turns the static last slice on (0xAC66D0, the only writer of true;
 * the five others write false as a state is left) and sets its half size to [lg+0x1E0] x [lg+0x2D7C] x 0.5 about the
 * world origin (0xAC66E6..0xAC6713, lg = [world+0xAE288]). The renderer draws it past the dynamic slices (exe 0x35F1A0:
 * 19 m at shadows 0, 56 m at 3). A 2048 m world gets a 1022 m half and 2-4 m texels; a 16384 m world about 8064 m and
 * 16-32 m texels with a 35-69 m depth bias, so its shadows stop at the dynamic slices. On a world over 2048 m the
 * slice stays dynamic, as the game has it outside missions: crisp shadows to 80 m. 2048 m worlds keep the game's. */
#define SUN_SET_BOOL 0x10087C0u  /* game.dll: rcx the settings (world + SUN_SETTINGS), rdx the name, r8b the value */
#define SUN_SETTINGS 0x7910270u
#define SUN_LEVEL 0xAE288u       /* world+: the level in play; +0x1E0 its size, +0x2D7C the disk fraction */
#define SUN_VANILLA_WORLD 2048.0f

typedef uint64_t (*SunSetBoolFn)(uint64_t settings, const char* name, uint64_t value);
static struct { SunSetBoolFn orig; int state; volatile LONG dynamic; } g_sun;

static int SunSliceDynamic(float world) { return world > SUN_VANILLA_WORLD; }

static uint64_t SunSetBool(uint64_t settings, const char* name, uint64_t value)
{
    uint64_t level = 0;
    float size = 0.0f, disk = 0.0f;
    /* the level the game reads two instructions later for the half size: [settings - SUN_SETTINGS + SUN_LEVEL] */
    if ((uint8_t)value != 0 && name != NULL && strcmp(name, "use_static_last_slice") == 0 &&
        !SafeRead((uintptr_t)(settings - SUN_SETTINGS + SUN_LEVEL), &level, 8) &&
        !SafeRead((uintptr_t)level + 0x1E0, &size, 4) && SunSliceDynamic(size))
    {
        SafeRead((uintptr_t)level + 0x2D7C, &disk, 4);
        InterlockedIncrement(&g_sun.dynamic);
        Note("difficulty: shadows: the sun's last slice stays dynamic on this %.0f m world (the static one would be +-%.0f m "
             "about the origin, vanilla +-1022 m); shadows reach 80 m", (double)size, (double)(size * disk * 0.5f));
        value &= ~(uint64_t)0xFF;
    }
    return g_sun.orig(settings, name, value);
}

/* With the extra large world's guards; once a process. */
static void SunSliceInstall(uintptr_t game)
{
    if (g_sun.state != 0) return;
    g_sun.state = -1;
    if (!ApiCodeMatches(game + SUN_SET_BOOL, "48 89 5C 24 10 48 89 6C 24 18 57 41 56 41 57 48 83 EC 20") ||
        !ApiCodeMatches(game + 0xAC66D0u, "41 B0 01 48 8D 15 06 E3 78 01 49 8D 8F 70 02 91 07 E8 DA 20 54 00 49 8B 87 88 E2 0A 00") ||
        !ApiCodeMatches(game + 0xAC66F4u, "49 8D 8F 70 02 91 07 F3 0F 10 90 E0 01 00 00 F3 0F 59 90 7C 2D 00 00"))
    { Note("difficulty: shadows: not this build's code; the sun's last slice is the game's on every world"); return; }
    if (LoaderHook("the sun's static last slice (dynamic on a world over 2048 m)", game + SUN_SET_BOOL, (void*)SunSetBool, (void**)&g_sun.orig))
    { Note("difficulty: shadows: the render setting writer could not be hooked; the sun's last slice is the game's"); return; }
    g_sun.state = 1;
}

#ifdef CO_TEST
int CoXlTestSunInstall(uintptr_t game) { g_sun.state = 0; g_sun.orig = NULL; SunSliceInstall(game); return g_sun.state; }
LONG CoXlTestSunDynamic(void) { return g_sun.dynamic; }
#endif
