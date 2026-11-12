/* On with the living world (HELLFORGE_LW_MOVER=0 turns it off; KB living-world-mover-query-overflow): every walking
 * enemy asks once a frame for the navmesh boundaries around it. The requests go into one list of 256 ([ctx+0x78], the
 * world's +0x79DABC8, 0x65C bytes each) that neither writer checks, so request 257 lands in the memory behind the list
 * (HUD widgets: the game crashed in the HUD's layout seconds to minutes later). */
#define LW_MOVER_QUERIES 0x59CB50u /* the motion system's pass: one request for each instance with a slot */
#define LW_MOVER_APPEND 0x134A7D0u /* the list's own append: returns the request's number, 0 is "none" to the reader */
#define LW_MOVER_LIST 0x4878u
#define LW_MOVER_COUNT 0x4890u
#define LW_MOVER_RECORDS 0x48D0u   /* 0xA4 bytes an instance, its slot handle at +0x54 */
#define LW_MOVER_CAP 256u
#define LW_MOVER_ROOM 240u         /* the pass stops here; the rest is left to the other writer */

typedef void (*LwMoverPassFn)(uintptr_t system);
typedef uint32_t (*LwMoverAppendFn)(uint32_t* list, const void* request);
static struct { void *passOrig, *appendOrig; volatile LONG asked, peak, dropped, refused, noted; } g_lwMover;

/* The instances past the room keep their slot but get request 0, which the mover reads as "no boundaries this frame"
 * (0x134CB3D), instead of last frame's number, now another enemy's request. Returns the first such instance. */
static uint32_t LwMoverCut(uintptr_t system, uintptr_t records, uint32_t n, uint32_t room, uint32_t* asked)
{
    uint32_t cut = n, kept = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t handle = *(volatile uint32_t*)(records + 0xA4ull * i + 0x54), high = handle >> 16;
        if (handle == 0xFFFFFFFFu) continue;
        ++*asked;
        if (kept < room) { kept++; continue; }
        if (cut == n) cut = i;
        if (high >= 1 && high <= 2) *(volatile uint32_t*)(system + 8ull * (3 + (handle & 0xFFFFu) + 0x486ull * (high - 1)) + 4) = 0;
        InterlockedIncrement(&g_lwMover.dropped);
    }
    return cut;
}

/* The count is lowered for the call only: the game's loop reads it each round (0x59CE36). */
static void LwMoverPass(uintptr_t system)
{
    uint32_t* list = *(uint32_t* volatile*)(system + LW_MOVER_LIST);
    volatile uint32_t* count = (volatile uint32_t*)(system + LW_MOVER_COUNT);
    uintptr_t records = *(volatile uintptr_t*)(system + LW_MOVER_RECORDS);
    uint32_t n = *count, cut = n, asked = 0;
    if (!LwLive()) { g_lwMover.asked = 0; ((LwMoverPassFn)g_lwMover.passOrig)(system); return; }
    if (list != NULL && records != 0) cut = LwMoverCut(system, records, n, *list < LW_MOVER_ROOM ? LW_MOVER_ROOM - *list : 0, &asked);
    g_lwMover.asked = (LONG)asked;
    if ((LONG)asked > g_lwMover.peak) g_lwMover.peak = (LONG)asked;
    if (cut < n) *count = cut;
    ((LwMoverPassFn)g_lwMover.passOrig)(system);
    if (cut < n && *count == cut) *count = n;
}

static uint32_t LwMoverAppend(uint32_t* list, const void* request)
{
    if (*list >= LW_MOVER_CAP && LwLive()) { InterlockedIncrement(&g_lwMover.dropped); return 0; }
    return ((LwMoverAppendFn)g_lwMover.appendOrig)(list, request);
}

/* The density rules ask before a squad comes online: every enemy that walks takes a request, the game's own too.
 * Members whose spawn request went out in the last 8 s may not have one yet, so they are counted on top (twice at
 * worst). */
static struct { uint32_t members; ULONGLONG since; } g_lwMoverSent;

static int LwMoverRoom(uint32_t members)
{
    if (g_lwMover.passOrig == NULL) return 1;
    if (GetTickCount64() - g_lwMoverSent.since > 8000) g_lwMoverSent.members = 0;
    if ((uint32_t)g_lwMover.asked + g_lwMoverSent.members + members <= LW_MOVER_ROOM - 8) return 1;
    InterlockedIncrement(&g_lwMover.refused);
    return 0;
}

/* Members sent in the last 8 s: also what the networked entity check adds on top of its count. */
static uint32_t LwMoverPending(void)
{
    return GetTickCount64() - g_lwMoverSent.since > 8000 ? 0 : g_lwMoverSent.members;
}

static void LwMoverSent(uint32_t members)
{
    ULONGLONG now = GetTickCount64();
    if (now - g_lwMoverSent.since > 8000) { g_lwMoverSent.members = 0; g_lwMoverSent.since = now; }
    g_lwMoverSent.members += members;
}

static void LwMoverNote(void)
{
    if (g_lwMover.passOrig == NULL) return;
    Note("living world: boundary requests %ld of %u now, peak %ld (30 s); %ld left out, %ld squads refused for them",
        g_lwMover.asked, LW_MOVER_ROOM, InterlockedExchange(&g_lwMover.peak, 0), InterlockedExchange(&g_lwMover.dropped, 0), InterlockedExchange(&g_lwMover.refused, 0));
}

/* The pins: the pass's start, its instance stride and slot arithmetic, both writers' record size, the list's 256
 * (the reset's +0x65C10) and the reader's test for request 0. */
static void LwInstallMover(void)
{
    static int done;
    char t[4] = "";
    if (done++) return;
    if (GetEnvironmentVariableA("HELLFORGE_LW_MOVER", t, sizeof t) > 0 && t[0] == '0') return;
    uintptr_t g = g_lw.game;
    if (!ApiCodeMatches(g + LW_MOVER_QUERIES, "4C 8B DC 55 56 57 49 8D 6B 88 48 81 EC 60 01 00 00") ||
        !ApiCodeMatches(g + 0x59CBD3u, "49 69 D9 A4 00 00 00 48 03 9E D0 48 00 00 8B 53 54") ||
        !ApiCodeMatches(g + 0x59CBF7u, "4C 69 F0 86 04 00 00 49 83 C6 03 4C 03 F2 4E 8D 34 F6") ||
        !ApiCodeMatches(g + 0x59CD73u, "48 69 C9 5C 06 00 00") ||
        !ApiCodeMatches(g + 0x59CE30u, "41 89 46 04 FF C7 3B BE 90 48 00 00") ||
        !ApiCodeMatches(g + LW_MOVER_APPEND, "48 89 5C 24 08 57 48 83 EC 20 48 8B 05") ||
        !ApiCodeMatches(g + 0x134A7FAu, "4D 69 C0 5C 06 00 00") ||
        !ApiCodeMatches(g + 0xAB1F4Fu, "48 81 C1 10 5C 06 00") ||
        !ApiCodeMatches(g + 0x134CB3Au, "8B 48 04 85 C9 0F 84"))
    { Note("living world: the boundary request list is not the reviewed code; not guarded"); return; }
    if (LoaderHook("living world: boundary request room", g + LW_MOVER_QUERIES, (void*)LwMoverPass, &g_lwMover.passOrig) || g_lwMover.passOrig == NULL)
    { g_lwMover.passOrig = NULL; Note("living world: the boundary request pass could not be hooked"); return; }
    if (LoaderHook("living world: boundary request room", g + LW_MOVER_APPEND, (void*)LwMoverAppend, &g_lwMover.appendOrig) || g_lwMover.appendOrig == NULL)
        Note("living world: the boundary request list's own append could not be hooked; the pass only");
    Note("living world: boundary requests kept to the list's %u (the pass stops at %u)", LW_MOVER_CAP, LW_MOVER_ROOM);
}
