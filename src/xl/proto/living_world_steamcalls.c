/* Test instrument (HELLFORGE_LW_STEAMCALLS=1 with the frame recorder): the game thread's Steam API calls by Proton's
 * call code. Every Steam call crosses lsteamclient.dll's steamclient_call(code, args, name) at +0x13330, which was
 * 9-13 % of the game thread's samples (KB xl-living-world-frame-budget, prof1). Codes name a lsteamclient.so function
 * through its __wine_unix_call_funcs table, offline. Noted every 30 s: ms and calls a frame, all frames and slow ones. */
#define LW_SC_CODES 8192
typedef uint32_t (*LwScCallFn)(uint32_t code, void* args, const char* name);
static struct
{
    void* orig; LONGLONG frameT[LW_SC_CODES], allT[LW_SC_CODES], slowT[LW_SC_CODES];
    uint32_t frameN[LW_SC_CODES], allN[LW_SC_CODES], slowN[LW_SC_CODES], frames, slow; ULONGLONG notedAt; int on;
    volatile LONGLONG otherT[LW_SC_CODES]; volatile LONG otherN[LW_SC_CODES];
} g_lwSc;

static uint32_t LwScCall(uint32_t code, void* args, const char* name)
{
    if (code >= LW_SC_CODES) return ((LwScCallFn)g_lwSc.orig)(code, args, name);
    LONGLONG t = LwFrNow(); uint32_t out = ((LwScCallFn)g_lwSc.orig)(code, args, name);
    if (GetCurrentThreadId() != g_lwFr.mainTid)
    { InterlockedAdd64(&g_lwSc.otherT[code], LwFrNow() - t); InterlockedIncrement(&g_lwSc.otherN[code]); return out; }
    g_lwSc.frameT[code] += LwFrNow() - t; g_lwSc.frameN[code]++;
    return out;
}

static void LwScLine(char* line, size_t size, const LONGLONG* t, const uint32_t* n, uint32_t frames)
{
    uint8_t used[LW_SC_CODES / 8] = {0}; size_t at = 0; LONGLONG sum = 0; uint64_t calls = 0;
    for (int c = 0; c < LW_SC_CODES; c++) { sum += t[c]; calls += n[c]; }
    at += (size_t)snprintf(line, size, " all %.3f ms, %.1f calls;", LwFrMs(sum) / frames, (double)calls / frames);
    for (int k = 0; k < 12 && at < size - 40; k++)
    {
        int best = -1;
        for (int c = 0; c < LW_SC_CODES; c++) if (t[c] && !(used[c >> 3] & (1 << (c & 7))) && (best < 0 || t[c] > t[best])) best = c;
        if (best < 0) break;
        used[best >> 3] |= (uint8_t)(1 << (best & 7));
        at += (size_t)snprintf(line + at, size - at, " %d %.3f (%.1f)", best, LwFrMs(t[best]) / frames, (double)n[best] / frames);
    }
}

/* Main thread, at each frame's end: the frame's calls go to all frames, and to the slow ones over 16.7 ms. */
static void LwScFrame(float period)
{
    if (!g_lwSc.on) return;
    int slow = period > 16.7f;
    for (int c = 0; c < LW_SC_CODES; c++)
    {
        if (!g_lwSc.frameN[c]) continue;
        g_lwSc.allT[c] += g_lwSc.frameT[c]; g_lwSc.allN[c] += g_lwSc.frameN[c];
        if (slow) { g_lwSc.slowT[c] += g_lwSc.frameT[c]; g_lwSc.slowN[c] += g_lwSc.frameN[c]; }
        g_lwSc.frameT[c] = 0; g_lwSc.frameN[c] = 0;
    }
    g_lwSc.frames++; g_lwSc.slow += (uint32_t)slow;
    ULONGLONG now = GetTickCount64();
    if (now - g_lwSc.notedAt < 30000 || !g_lwSc.frames) return;
    char all[700], sl[700] = "";
    LwScLine(all, sizeof all, g_lwSc.allT, g_lwSc.allN, g_lwSc.frames);
    if (g_lwSc.slow) LwScLine(sl, sizeof sl, g_lwSc.slowT, g_lwSc.slowN, g_lwSc.slow);
    Note("living world: test: steam calls a frame, code ms (calls), %u frames:%s | %u frames over 16.7 ms:%s", g_lwSc.frames, all, g_lwSc.slow, sl);
    LONGLONG ot[LW_SC_CODES]; uint32_t on[LW_SC_CODES]; char other[700];
    for (int c = 0; c < LW_SC_CODES; c++) { ot[c] = InterlockedExchange64(&g_lwSc.otherT[c], 0); on[c] = (uint32_t)InterlockedExchange(&g_lwSc.otherN[c], 0); }
    LwScLine(other, sizeof other, ot, on, g_lwSc.frames);
    Note("living world: test: steam calls a frame on other threads, code ms (calls):%s", other);
    memset(g_lwSc.allT, 0, sizeof g_lwSc.allT); memset(g_lwSc.allN, 0, sizeof g_lwSc.allN);
    memset(g_lwSc.slowT, 0, sizeof g_lwSc.slowT); memset(g_lwSc.slowN, 0, sizeof g_lwSc.slowN);
    g_lwSc.frames = g_lwSc.slow = 0; g_lwSc.notedAt = now;
}

static void LwInstallSteamCalls(void)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_LW_STEAMCALLS", t, sizeof t) == 0 || t[0] != '1') return;
    uintptr_t m = (uintptr_t)GetModuleHandleA("lsteamclient.dll");
    if (!m || !ApiCodeMatches(m + 0x13330u, "55 57 56 53 48 83 EC 38 48 8D 6C 24 30 4C 89 C6 49 89 D0 89 CA") ||
        LoaderHook("steam calls (test)", m + 0x13330u, (void*)LwScCall, &g_lwSc.orig) || !g_lwSc.orig)
    { Note("living world: test: steam calls: lsteamclient.dll's steamclient_call is not this Proton's; off"); return; }
    g_lwSc.on = 1; g_lwSc.notedAt = GetTickCount64();
    Note("living world: test: steam calls: counting the game thread's steamclient_call codes");
}
