/* Broad phase layer 1 rebuilt only when marked (KB broadphase-layer1-rebuild-plan; research builds HELLFORGE_LW_BP=1, command
 * "bp 0|1"). Layer 1 holds query-only bodies (38,858 leaves on an XL map), and the game rebuilds its tree every step
 * because the layer may hold active bodies whose boxes change with no mark. The layer rule (exe 0xDB9E60) here puts
 * active query-only bodies in layer 0, which is rebuilt each step anyway, so layer 1 needs a rebuild only when one of
 * its bodies is marked. Measured: 0.7 to 1.2 ms a frame on an empty XL map, the same ray hits (run m51). */
#define LW_BP_LEAVES 65536u
#define LW_BP_BATCH 256u
typedef void (*LwBpRuleFn)(void* cfg, const uint32_t* ids, int n, const uint8_t* bodies, uint8_t* out);
typedef void (*LwBpInfoFn)(void* cfg, uint8_t layer, uint8_t* out);
typedef uintptr_t (*LwBpUpdateFn)(uintptr_t bp, const uint32_t* ids, int n, int stride);
static struct
{
    int installed, on, want; uintptr_t exe; void* ruleOrig; void* infoOrig; uint32_t frames;
    uint8_t leaf[LW_BP_LEAVES * 16u];
} g_lwBp;

/* An active body (+0x44 bit 3) the rule sent to layer 1 goes to layer 0. */
static void LwBpRule(void* cfg, const uint32_t* ids, int n, const uint8_t* bodies, uint8_t* out)
{
    ((LwBpRuleFn)g_lwBp.ruleOrig)(cfg, ids, n, bodies, out);
    if (!g_lwBp.on || !LwLive()) return;
    for (int i = 0; i < n; i++)
    {
        uint32_t flags;
        if (out[i] != 1) continue;
        memcpy(&flags, bodies + (size_t)(ids[i] & 0xFFFFFFu) * 0xA0u + 0x44u, 4);
        if (flags & 8u) out[i] = 0;
    }
}
/* Layer 1's info without its always-rebuild flag (u16 out+8, written by 0xDB9DE3). */
static void LwBpInfo(void* cfg, uint8_t layer, uint8_t* out)
{
    ((LwBpInfoFn)g_lwBp.infoOrig)(cfg, layer, out);
    if (g_lwBp.on && LwLive() && layer == 1) out[8] = 0;
}

static void LwBpByte(uintptr_t at, uint8_t value) { uint8_t old; if (!SafeRead(at, &old, 1)) *(volatile uint8_t*)at = value; }

/* The broad phase, its layer 1 and the body array; 0 when there is no world or it is not the object the code expects. */
static int LwBpWorld(uintptr_t* bp, uintptr_t* l1, uintptr_t* bodies)
{
    uintptr_t w = 0, a = 0, vt = 0, fn = 0, layers = 0, world = 0, set = 0;
    if (SafeRead(g_lwBp.exe + 0x27BA9F0u, &w, 8) || w == 0 || SafeRead(w + 0x410, &a, 8) || a == 0) return 0;
    if (SafeRead(a + 0xA0, bp, 8) || *bp == 0 || SafeRead(*bp, &vt, 8) || SafeRead(vt + 0x38, &fn, 8)) return 0;
    if (fn != g_lwBp.exe + 0xD40550u) return 0;
    if (SafeRead(*bp + 0x10, &layers, 8) || SafeRead(layers + 8, l1, 8) || *l1 == 0) return 0;
    if (SafeRead(*bp + 8, &world, 8) || SafeRead(world + 0x130, &set, 8) || SafeRead(set + 0x10, bodies, 8)) return 0;
    return *bodies != 0;
}

/* Bodies already in layer 1 when they turn active are not moved by the rule until they are updated: they are handed
 * to the body update (0xD40550), which asks the rule again. Returns how many did not fit in this batch. */
static uint32_t LwBpSweep(uintptr_t bp, uintptr_t l1, uintptr_t bodies)
{
    uintptr_t leaves = 0; uint32_t count = 0, n = 0, left = 0; uint32_t ids[LW_BP_BATCH];
    if (SafeRead(l1 + 0x48, &leaves, 8) || SafeRead(l1 + 0x50, &count, 4) || count > LW_BP_LEAVES) return 0xFFFFFFFFu;
    if (count == 0 || SafeRead(leaves, g_lwBp.leaf, (size_t)count * 16u)) return count ? 0xFFFFFFFFu : 0;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t id, flags = 0;
        memcpy(&id, g_lwBp.leaf + (size_t)i * 16u + 0xCu, 4); id &= 0xFFFFFFu;
        if (SafeRead(bodies + (uintptr_t)id * 0xA0u + 0x44u, &flags, 4) || !(flags & 8u)) continue;
        if (n < LW_BP_BATCH) ids[n++] = id; else left++;
    }
    if (n) ((LwBpUpdateFn)(g_lwBp.exe + 0xD40550u))(bp, ids, (int)n, 4);
    return left;
}

/* Game thread, once a frame outside the physics step. Every 64 frames a sweep; until one finds no active body left in
 * the layer, the layer keeps its every-step rebuild ([l1+0x1AE8]). */
static void LwBpFrame(void)
{
    uintptr_t bp = 0, l1 = 0, bodies = 0;
    if (!g_lwBp.installed) return;
    if (!LwBpWorld(&bp, &l1, &bodies)) { g_lwBp.on = g_lwBp.want; return; }
    if (!g_lwBp.want && g_lwBp.on) { LwBpByte(l1 + 0x1AE8, 1); g_lwBp.on = 0; }
    if (g_lwBp.want && !g_lwBp.on) { g_lwBp.on = 1; g_lwBp.frames = 0; }
    if (g_lwBp.on && (g_lwBp.frames & 63u) == 0) LwBpByte(l1 + 0x1AE8, LwBpSweep(bp, l1, bodies) == 0 ? 0 : 1);
    g_lwBp.frames++;
}

static void LwInstallBp(void)
{
    static int done; char t[4] = "";
    if (done) return;
    done = 1;
    /* research only: whether the broad phase outlives a level is not proved, so leaving could not be shown safe */
    if (GetEnvironmentVariableA("HELLFORGE_LW_BP", t, sizeof t) == 0 || t[0] != '1') return;
    g_lwBp.exe = (uintptr_t)GetModuleHandleA(NULL);
    if (!ApiCodeMatches(g_lwBp.exe + 0xDB9E60u, "4D 63 D8 4C 8B D2 45 85 C0 7E 6E 48 8B 44 24 28 45 33 C0 41 8B 0A 81 E1") ||
        !ApiCodeMatches(g_lwBp.exe + 0xDB9E84u, "42 8B 4C 0A 44 F6 C1 20 74 05 C6 00 05 EB 39 0F BA E1 08 73 05 C6 00 01") ||
        !ApiCodeMatches(g_lwBp.exe + 0xDB9DB0u, "0F B6 C2 83 F8 05 0F 87 8A 00 00 00 48 8D 15") ||
        !ApiCodeMatches(g_lwBp.exe + 0xDB9DE3u, "41 C6 00 0B 41 C7 40 04 00 00 00 00 66 41 C7 40 08 01 00 C3") ||
        !ApiCodeMatches(g_lwBp.exe + 0xD40550u, "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8B") ||
        !ApiCodeMatches(g_lwBp.exe + 0xD40720u, "49 8B 45 48 48 03 C9 48 63 54 C8 0C") ||
        LoaderHook("living world: layer 1 rule", g_lwBp.exe + 0xDB9E60u, (void*)LwBpRule, &g_lwBp.ruleOrig) || g_lwBp.ruleOrig == NULL ||
        LoaderHook("living world: layer 1 info", g_lwBp.exe + 0xDB9DB0u, (void*)LwBpInfo, &g_lwBp.infoOrig) || g_lwBp.infoOrig == NULL)
    { Note("living world: layer 1: this build's code is not what the hooks expect; off"); return; }
    g_lwBp.installed = 1; g_lwBp.want = 1;
}
/* Leaving a complex level: layer 1's every-step rebuild back on, as the game has it. Bodies the rule put in layer 0
 * stay there until their next update asks the game's rule again; layer 0 is rebuilt every step, so queries find them. */
static void LwBpLeave(void)
{
    uintptr_t bp = 0, l1 = 0, bodies = 0;
    if (!g_lwBp.installed || !g_lwBp.on) return;
    if (LwBpWorld(&bp, &l1, &bodies)) LwBpByte(l1 + 0x1AE8, 1);
    g_lwBp.on = 0;
}
static void LwBpCmd(int on)
{
    if (!g_lwBp.installed || on < 0 || on > 1) { Note("living world: test: layer 1: not installed or a value outside 0..1"); return; }
    g_lwBp.want = on;
}
