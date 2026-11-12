/* Vehicle boxes once a frame (on by default; HELLFORGE_LW_VBOX=0 turns it off, 2 compares; command "vbox 0|1|2"):
 * the motion pass's boundary clip (game.dll 0x134AD60) asks the engine for every vehicle's oriented box (unit API
 * +0x778 = exe 0x2059A0, at 0x134C151) for every moving enemy, before it tests the distance. The box comes from world
 * poses that the engine writes after the game's update, so within a frame the answer for one unit is the same:
 * it is computed once and copied. Mode 2 computes it every time as well and counts answers that differ. */
#define LW_VBOX_MAX 128
#define LW_VBOX_BYTES 0x4Cu /* the function's writes: four 16-byte rows, 8 bytes at +0x40, 4 at +0x48 */
typedef void* (*LwVboxFn)(void* out, uint32_t unit, uint32_t flag);
static struct
{
    int mode, installed; uintptr_t exe, site; void* orig; DWORD thread;
    uint32_t epoch, count, calls, hits, differ, full, frames; ULONGLONG notedAt;
    struct { uint32_t unit, epoch; uint8_t box[LW_VBOX_BYTES]; } e[LW_VBOX_MAX];
} g_lwVbox;

static __attribute__((noinline)) void* LwVboxHook(void* out, uint32_t unit, uint32_t flag)
{
    LwVboxFn orig = (LwVboxFn)g_lwVbox.orig;
    if (!LwLive() || !g_lwVbox.mode || (uintptr_t)__builtin_return_address(0) != g_lwVbox.site || flag != 1 || GetCurrentThreadId() != g_lwVbox.thread)
        return orig(out, unit, flag);
    g_lwVbox.calls++;
    for (uint32_t i = 0; i < g_lwVbox.count; i++)
        if (g_lwVbox.e[i].unit == unit && g_lwVbox.e[i].epoch == g_lwVbox.epoch)
        {
            if (g_lwVbox.mode == 2)
            {
                void* r = orig(out, unit, flag);
                if (memcmp(out, g_lwVbox.e[i].box, LW_VBOX_BYTES) != 0) g_lwVbox.differ++;
                return r;
            }
            g_lwVbox.hits++;
            memcpy(out, g_lwVbox.e[i].box, LW_VBOX_BYTES);
            return out;
        }
    void* r = orig(out, unit, flag);
    if (g_lwVbox.count < LW_VBOX_MAX)
    {
        g_lwVbox.e[g_lwVbox.count].unit = unit; g_lwVbox.e[g_lwVbox.count].epoch = g_lwVbox.epoch;
        memcpy(g_lwVbox.e[g_lwVbox.count++].box, out, LW_VBOX_BYTES);
    }
    else g_lwVbox.full++;
    return r;
}

/* Game thread, once a frame outside the motion pass: last frame's boxes are dropped; every 30 s what was asked. */
static void LwVboxFrame(void)
{
    if (!g_lwVbox.installed) return;
    g_lwVbox.thread = GetCurrentThreadId();
    ULONGLONG now = GetTickCount64();
    if (g_lwVbox.mode == 2 && now - g_lwVbox.notedAt >= 30000 && g_lwVbox.frames)
    {
        char list[400] = ""; int used = 0;
        for (uint32_t i = 0; i < g_lwVbox.count && i < 12 && used < 360; i++)
        {
            float t[3], h[3]; memcpy(t, g_lwVbox.e[i].box + 0x30, 12); memcpy(h, g_lwVbox.e[i].box + 0x40, 12);
            used += snprintf(list + used, sizeof list - (size_t)used, " %u@(%.0f,%.0f) %.1fx%.1fx%.1f", g_lwVbox.e[i].unit, (double)t[0], (double)t[1], (double)h[0], (double)h[1], (double)h[2]);
        }
        Note("living world: test: vehicle boxes (mode %d): %.1f asked a frame, %.1f copied, %u distinct units last frame, %u answers differed, %u past the table; first:%s",
            g_lwVbox.mode, (double)g_lwVbox.calls / g_lwVbox.frames, (double)g_lwVbox.hits / g_lwVbox.frames, g_lwVbox.count, g_lwVbox.differ, g_lwVbox.full, list);
        g_lwVbox.calls = g_lwVbox.hits = g_lwVbox.differ = g_lwVbox.full = g_lwVbox.frames = 0; g_lwVbox.notedAt = now;
    }
    g_lwVbox.frames++; g_lwVbox.epoch++; g_lwVbox.count = 0;
}

static void LwInstallVbox(void)
{
    static int done; char t[4] = "";
    if (done) return;
    done = 1;
    if (GetEnvironmentVariableA("HELLFORGE_LW_VBOX", t, sizeof t) == 0 || t[0] < '0' || t[0] > '2') t[0] = '1';
    g_lwVbox.exe = ApiModuleBase("helldivers2.exe", NULL); g_lwVbox.site = g_lw.game + 0x134C153u;
    if (g_lwVbox.exe == 0) g_lwVbox.exe = (uintptr_t)GetModuleHandleA(NULL);
    if (!ApiCodeMatches(g_lwVbox.exe + 0x2059A0u, "48 89 5C 24 08 57 48 81 EC E0 00 00 00 48 8B F9 41 8B D8 8B CA") ||
        !ApiCodeMatches(g_lw.game + 0x134C136u, "41 B8 01 00 00 00 8B 56 0C 48 8B 48 18 48 8B 81 78 07 00 00 48 8D 8D 20 01 00 00 FF D0") ||
        !ApiCodeMatches(g_lwVbox.exe + 0x205A78u, "89 47 48 48 8B C7 0F 11 27 0F 11 57 10 0F 11 5F 20 0F 11 6F 30") ||
        LoaderHook("living world: vehicle boxes once a frame", g_lwVbox.exe + 0x2059A0u, (void*)LwVboxHook, &g_lwVbox.orig) || g_lwVbox.orig == NULL)
    { Note("living world: vehicle boxes once a frame: this build's code is not what the hook expects; off"); return; }
    g_lwVbox.installed = 1; g_lwVbox.mode = t[0] - '0'; g_lwVbox.notedAt = GetTickCount64();
    Note("living world: vehicle boxes once a frame: installed, mode %d (0 off, 1 on, 2 compare)", g_lwVbox.mode);
}
/* Leaving a complex level: the copies are dropped and the thread forgotten, so no later call can match one. */
static void LwVboxLeave(void) { g_lwVbox.count = 0; g_lwVbox.epoch++; g_lwVbox.thread = 0; }
static void LwVboxCmd(int mode)
{
    if (!g_lwVbox.installed || mode < 0 || mode > 2) { Note("living world: test: vehicle boxes: not installed (HELLFORGE_LW_VBOX) or a mode outside 0..2"); return; }
    g_lwVbox.mode = mode; Note("living world: test: vehicle boxes: mode %d", mode);
}
