/* The engine's per-thread frame scratch heaps keep their committed pages (KB lever-frame-scratch).
 *
 * Each thread owns two linear heaps (exe+0x1B10760, 40 bytes each: base, committed, used, peak, end), one a frame.
 * At a thread's first use in a new frame, 0x5B7700 walks the frames from its last one to this one, both included: a
 * heap whose peak is under half its committed size and over 32 pages is released (128 MiB reservation) and made again
 * with 32 pages. The walk before zeroed this frame's heap's peak, so a busy heap is released every time it comes round.
 * The lever raises the 32-page floor (shl 5 at 0x5B77C2) to 2^S pages; a reset heap is reused unzeroed in stock too.
 * HELLFORGE_LW_FRAMEHEAP=S / command "frameheap S" (5 is the game's), "frameheap ab S secs". */

#define LW_FH_SHIFT 0x5B77C5u /* the shift count of "shl rax,5" */
#define LW_FH_HEAPS 0x1B10760u
#define LW_FH_SLOTS 256

static struct { int ready, shift, abShift, abSecs; uintptr_t exe; ULONGLONG abAt; } g_lwFh = {0, 5, 0, 0, 0, 0};

static int LwFhPinned(uintptr_t exe)
{
    return ApiCodeMatches(exe + 0x5B771Du, "8B 35 F9 8B 45 01 B9 D8 01 00 00 48 8B 00 44 8B 34 01 43 8B BC B5 70 2F B1 01") &&
        ApiCodeMatches(exe + 0x5B7783u, "48 8B 4B 08 48 8B C1 48 D1 E8 48 39 43 18 73 72 48 85 D2 75") &&
        ApiCodeMatches(exe + 0x5B77C9u, "76 3A 48 8B 0B 48 85 C9 74 21 33 D2") &&
        ApiCodeMatches(exe + 0x5B7805u, "4C 89 63 10 4C 89 63 18") && ApiCodeMatches(exe + 0x63B949u, "48 C1 E0 05 BA 00 00 00 08");
}

/* One byte through the registry lever's stopped-thread write; 0 when the shift is in place. */
static int LwFhWrite(int shift)
{
    uint8_t now[4];
    if (SafeRead(g_lwFh.exe + LW_FH_SHIFT - 3, now, 4) || now[0] != 0x48 || now[1] != 0xC1 || now[2] != 0xE0 || now[3] < 5 || now[3] > 14) return 1;
    if (now[3] == shift) return 0;
    LwRegEdit e = {g_lwFh.exe + LW_FH_SHIFT, {(uint8_t)shift}, 1};
    return LwRegCommit(&e, 1, NULL);
}

static void LwFhSet(int shift)
{
    if (!g_lwFh.ready || shift < 5 || shift > 14) { Note("living world: test: frameheap: not this build's code, or a shift outside 5..14"); return; }
    if (LwFhWrite(shift)) { Note("living world: frame scratch heaps: 0x5B77C5 not changed (other bytes there, or a thread could not be stopped)"); return; }
    g_lwFh.shift = shift;
    Note("living world: frame scratch heaps: released only above %d KB committed (%s)", 4 << shift, shift == 5 ? "the game's" : "lever");
}

static void LwInstallFrameHeap(void)
{
    static int done; char t[8] = "";
    if (done++) return;
    g_lwFh.exe = (uintptr_t)GetModuleHandleA(NULL);
    if (strcmp(g_build, "25480438") || !LwFhPinned(g_lwFh.exe)) { Note("living world: frame scratch heaps: not this build's code; not changed"); return; }
    g_lwFh.ready = 1;
    if (GetEnvironmentVariableA("HELLFORGE_LW_FRAMEHEAP", t, sizeof t) > 0 && atoi(t) > 5) LwFhSet(atoi(t));
}

static void LwFhCmd(const char* a)
{
    int s = 0, secs = 12, n = sscanf(a, "frameheap ab %d %d", &s, &secs);
    if (n >= 1)
    {
        g_lwFh.abShift = 0; LwFhSet(5);
        if (s <= 5 || s > 14 || secs < 1 || g_lwFh.shift != 5) return;
        g_lwFh.abShift = s; g_lwFh.abSecs = secs; g_lwFh.abAt = GetTickCount64();
        InterlockedExchange(&g_lwFr.phase, 3005);
        Note("living world: frame scratch heaps: shifts 5 and %d take turns every %d s (recorder phase 3005 / %d)", s, secs, 3000 + s);
    }
    else if (sscanf(a, "frameheap %d", &s) == 1) { g_lwFh.abShift = 0; LwFhSet(s); }
    else Note("living world: test: %s: not understood (frameheap S, frameheap ab S secs)", a);
}

/* Game thread: the A/B flip, one stopped-thread write each turn. */
static void LwFhFrame(void)
{
    ULONGLONG now = GetTickCount64();
    if (!g_lwFh.abShift || now - g_lwFh.abAt < (ULONGLONG)g_lwFh.abSecs * 1000u) return;
    g_lwFh.abAt = now;
    int next = g_lwFh.shift == 5 ? g_lwFh.abShift : 5;
    if (LwFhWrite(next)) return;
    g_lwFh.shift = next;
    InterlockedExchange(&g_lwFr.phase, 3000 + next);
}

/* The memory the lever costs: committed bytes of the heaps in use, read racily (statistics only). */
static void LwFhNote(void)
{
    if (!g_lwFh.ready) return;
    uint64_t sum = 0, most = 0, heap[5]; int used = 0;
    for (int i = 0; i < LW_FH_SLOTS; i++)
    {
        if (SafeRead(g_lwFh.exe + LW_FH_HEAPS + (uintptr_t)i * 40u, heap, sizeof heap) || !heap[0]) continue;
        used++; sum += heap[1]; if (heap[1] > most) most = heap[1];
    }
    Note("living world: frame scratch heaps (30 s): shift %d (A/B %d); %d heaps, %.1f MB committed, largest %.1f MB", g_lwFh.shift,
        g_lwFh.abShift, used, (double)sum / 1048576.0, (double)most / 1048576.0);
}
