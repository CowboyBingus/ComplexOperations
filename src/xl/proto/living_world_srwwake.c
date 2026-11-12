/* Wine's RtlReleaseSRWLockShared calls RtlWakeAddressSingle(lock+2) at every last shared release, and that wake spins on
 * a process-wide futex bucket lock; with game workers preempted by game workers the spin comes in bursts across a dozen
 * threads (KB frame-cost-swarm-cap2). Only exclusive acquirers sleep on lock+2, and each adds 2 to the lock's low word
 * before it reads the shared count, removing it only in its acquiring CAS. So a release whose CAS saw a low word of 0
 * cannot strand a waiter, and its wake may be skipped. The tail of the release (ntdll 0x33F50) is rewritten to test the
 * CAS's old low word (bx) too, with a second plain epilogue in padding (0x33F17) so the code stays unwindable. Only this
 * Proton build's exact bytes are changed, while every other thread of the process is confirmed stopped outside both
 * ranges. HELLFORGE_LW_SRWWAKE=1 / command "srwwake 0|1". */
typedef struct { uint32_t rva; const char* bytes; } LwSwPin;
static const LwSwPin g_lwSwPins[] = {
    {0x33EA0U, /* RelS */
        "55 56 53 48 83 EC 30 48 8D 6C 24 30 48 89 CE EB 29 0F 1F 80 00 00 00 00 F6 05 31 34 04 00 02 75 "
        "5F 89 DA 0F B7 CB C1 EA 10 FF CA 89 D0 C1 E0 10 09 C1 89 D8 F0 0F B1 0E 74 76 8B 1E F6 C3 01 75 "
        "D7 89 D8 C1 E8 10 75 D9 F6 05 01 34 04 00 02 74 D0 48 89 74 24 20 4C 8D 0D C9 45 05 00 4C 8D 05 "
        "7C 8C 05 00 B9 01 00 00 00 48 8D 15 E0 33 04 00 E8 1B EA 03 00 EB AA 66 0F 1F 84 00 00 00 00 00 "
        "48 89 74 24 20 4C 8D 0D 7D 45 05 00 4C 8D 05 4D 8C 05 00 B9 01 00 00 00 48 8D 15 B1 33 04 00 E8 "
        "EC E9 03 00 E9 78 FF FF FF 0F 1F 80 00 00 00 00 66 85 D2 74 0B 48 83 C4 30 5B 5E 5D C3 0F 1F 00 "
        "48 8D 4E 02 48 83 C4 30 5B 5E 5D E9 D0 6A 00 00"},
    {0x19C00U, /* AcqX */
        "55 57 56 53 48 83 EC 38 48 8D 6C 24 30 48 89 CB 66 F0 83 01 02 48 8D 7D FE 48 8D 71 02 66 66 2E "
        "0F 1F 84 00 00 00 00 00 66 66 2E 0F 1F 84 00 00 00 00 00 66 66 2E 0F 1F 84 00 00 00 00 00 66 90 "
        "8B 13 89 D0 89 55 FC C1 E8 10 75 34 B8 01 00 00 00 66 89 45 FE 0F B7 45 FC 83 E8 02 83 C8 01 66 "
        "89 45 FC 8B 4D FC 89 D0 F0 0F B1 0B 75 D2 48 83 C4 38 5B 5E 5F 5D C3 66 0F 1F 84 00 00 00 00 00 "
        "89 D0 F0 0F B1 13 75 B8 45 31 C9 41 B8 02 00 00 00 48 89 FA 48 89 F1 E8 64 09 02 00 EB A2"},
    {0x19CC0U, /* AcqS */
        "55 56 53 48 83 EC 30 48 8D 6C 24 30 48 89 CB 48 8D 75 FC 66 66 2E 0F 1F 84 00 00 00 00 00 66 90 "
        "8B 13 89 55 FC 66 85 D2 75 1E 66 FF 45 FE 8B 4D FC 89 D0 F0 0F B1 0B 75 E7 48 83 C4 30 5B 5E 5D "
        "C3 0F 1F 80 00 00 00 00 89 D0 F0 0F B1 13 75 D0 45 31 C9 41 B8 04 00 00 00 48 89 F2 48 89 D9 E8 "
        "DC 08 02 00 EB BA"},
    {0x33E00U, /* RelX */
        "55 41 54 57 56 53 48 83 EC 30 48 8D 6C 24 30 4C 8D 25 DA 34 04 00 48 89 CF EB 18 0F 1F 44 00 00 "
        "89 DA 0F B7 F6 89 D8 83 E2 FE 66 89 D6 F0 0F B1 37 74 35 8B 1F F6 C3 01 75 E6 41 F6 04 24 02 74 "
        "DF 48 89 7C 24 20 4C 8D 0D 3B 46 05 00 4C 89 E2 B9 01 00 00 00 4C 8D 05 C4 8E 05 00 E8 CF EA 03 "
        "00 EB BD 0F 1F 44 00 00 66 85 D2 74 13 48 8D 4F 02 48 83 C4 30 5B 5E 5F 41 5C 5D E9 C0 6B 00 00 "
        "48 89 F9 48 83 C4 30 5B 5E 5F 41 5C 5D E9 0E 6A 00 00"},
    {0x369C0U, /* TryX */
        "8B 11 45 31 C9 89 D0 41 89 D0 C1 E8 10 75 1C 0F B7 C2 41 B9 01 00 00 00 41 89 C0 41 81 C8 00 00 "
        "01 00 66 41 89 C0 66 41 83 C8 01 89 D0 F0 44 0F B1 01 75 CC 44 89 C8 C3"},
    {0x36A00U, /* TryS */
        "8B 11 45 31 C9 41 89 D0 66 85 D2 75 11 41 C1 E8 10 41 B9 01 00 00 00 41 FF C0 41 C1 E0 10 89 D0 "
        "F0 44 0F B1 01 75 D9 44 89 C8 C3"},
    {0x36060U, /* SleepCvSrw */
        "55 57 56 53 48 83 EC 38 48 8D 6C 24 30 8B 01 41 83 E1 01 48 89 CE 89 45 FC 48 89 D3 4C 89 C7 48 "
        "89 D1 74 34 E8 17 DE FF FF 48 8D 55 FC 49 89 F9 48 89 F1 41 B8 04 00 00 00 E8 62 45 00 00 48 89 "
        "D9 89 C6 E8 18 3C FE FF 89 F0 48 83 C4 38 5B 5E 5F 5D C3 0F 1F 44 00 00 E8 43 DD FF FF 48 8D 55 "
        "FC 49 89 F9 48 89 F1 41 B8 04 00 00 00 E8 2E 45 00 00 48 89 D9 89 C6 E8 24 3B FE FF 89 F0 48 83 "
        "C4 38 5B 5E 5F 5D C3"},
    {0x3AA40U, /* Wake1a */
        "55 56 53 48 83 EC 30 48 8D 6C 24 30 48 89 CB F6 05 9A C8 03 00 08 0F 85 C4 00 00 00 48 85 DB 0F "
        "84 AD 00 00 00 49 89 DA 4C 8D 1D 71 CD 03 00 45 31 C0 49 C1 EA 04 45 0F B6 D2 4F 8D 0C 12 4B 8D "
        "0C 11 48 C1 E1 03 49 8D 54 0B 10 EB 05 0F 1F 00 F3 90 44 89 C0 BE FF FF FF FF F0 0F B1 32 85 C0 "
        "75 EE 4D 01 D1 4C 01 D9 49 C1 E1 03 4B 8B 04 0B 48 85 C0 74 4B 48 39 C8 75 0E EB 52 0F 1F 40 00 "
        "48 8B 00 48 39 C8 74 46 48 39 58 10 75 F2 4C 8B 08 4C 8B 40 08 8B 48 18 4D 89 41 08 4C 8B 08 4D "
        "89 08 48 C7 40 10 00 00 00 00 31 C0 87 02 85 C9 74 20 48 83 C4 30 5B 5E 5D E9 E2 4B FD FF 66 90 "
        "66 48 0F 6E C1 66 0F 6C C0 43 0F 11 04 0B 31 C0 87 02 48 83 C4 30 5B 5E 5D C3 66 0F 1F 44 00 00 "
        "48 89 4C 24 20 4C 8D 0D 57 D9 04 00 4C 8D 05 1D 49 05 00 B9 03 00 00 00 48 8D 15 B1 C7 03 00 E8"},
    {0x3AB40U, /* Wake1b */
        "EC 7D 03 00 E9 13 FF FF FF 90"},
    {0x3A600U, /* WaitA */
        "55 41 56 41 55 41 54 57 56 53 48 83 EC 60 48 8D 6C 24 60 48 89 CE 49 89 CC 49 89 D6 4C 89 C7 48 "
        "C1 EE 04 4D 89 CD 40 0F B6 F6 F6 05 BF CC 03 00 08 74 5B 48 8D 05 69 DD 04 00 4D 85 C9 74 1C 4D "
        "8B 01 4C 89 C2 48 C1 EA 20 0F 84 F1 01 00 00 48 8D 0D 58 DD 04 00 E8 D5 86 03 00 48 89 44 24 38 "
        "4C 8D 0D 59 DD 04 00 B9 03 00 00 00 4C 8D 05 6D 50 05 00 48 89 7C 24 30 48 8D 15 71 CC 03 00 4C "
        "89 74 24 28 4C 89 64 24 20 E8 A2 82 03 00 48 8D 47 FC 48 A9 FB FF FF FF 74 0E 48 8D 47 FF 48 83 "
        "F8 01 0F 87 D7 01 00 00 4C 8D 04 36 4C 8D 0D 2D D1 03 00 4C 89 65 F0 31 D2 49 8D 0C 30 65 48 8B "
        "04 25 30 00 00 00 48 8B 40 48 48 C1 E1 03 49 8D 5C 09 10 89 45 F8 EB 0A 0F 1F 84 00 00 00 00 00 "
        "F3 90 89 D0 41 BA FF FF FF FF F0 44 0F B1 13 85 C0 75 ED 48 83 FF 04 0F 84 73 01 00 00 0F 87 1D"},
    {0x3A700U, /* WaitB */
        "01 00 00 48 83 FF 01 0F 84 4B 01 00 00 41 0F B7 06 66 41 39 04 24 0F 94 C0 A8 01 0F 84 14 01 00 "
        "00 49 8D 04 09 66 48 0F 6E C0 49 8D 04 30 48 C1 E0 03 49 83 3C 01 00 0F 84 CB 00 00 00 49 01 F0 "
        "48 8D 55 E0 4B 8D 04 C1 48 8B 78 08 0F 16 40 08 0F 11 45 E0 48 89 17 48 89 50 08 31 C0 87 03 31 "
        "C9 4C 89 EA E8 77 62 FD FF 89 C6 48 8B 45 F0 31 D2 48 85 C0 75 2C F6 05 73 CB 03 00 08 75 55 81 "
        "FE 01 01 00 00 74 78 89 F0 48 83 C4 60 5B 5E 5F 41 5C 41 5D 41 5E 5D C3 0F 1F 84 00 00 00 00 00 "
        "F3 90 89 D0 BF FF FF FF FF F0 0F B1 3B 85 C0 75 EF 48 83 7D F0 00 74 0F 48 8B 55 E0 48 8B 45 E8 "
        "48 89 42 08 48 89 10 31 C0 87 03 F6 05 1E CB 03 00 08"},
};
static const struct { const char* name; uint32_t rva; } g_lwSwExports[] = {
    {"RtlReleaseSRWLockShared", 0x33EA0u}, {"RtlAcquireSRWLockShared", 0x19CC0u}, {"RtlAcquireSRWLockExclusive", 0x19C00u},
    {"RtlReleaseSRWLockExclusive", 0x33E00u}, {"RtlTryAcquireSRWLockExclusive", 0x369C0u}, {"RtlTryAcquireSRWLockShared", 0x36A00u},
    {"RtlSleepConditionVariableSRW", 0x36060u}, {"RtlWaitOnAddress", 0x3A600u}, {"RtlWakeAddressSingle", 0x3AA40u},
};
#define LW_SW_A 0x33F17u /* 9 bytes of padding after an unconditional jmp: the no-wake epilogue */
#define LW_SW_B 0x33F50u /* the success tail, 32 bytes to the end of the function */
static const uint8_t g_lwSwOldA[9] = {0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t g_lwSwNewA[9] = {0x48, 0x83, 0xC4, 0x30, 0x5B, 0x5E, 0x5D, 0xC3, 0xCC}; /* add rsp,30h; pop rbx/rsi/rbp; ret */
static const uint8_t g_lwSwOldB[32] = {0x66, 0x85, 0xD2, 0x74, 0x0B, 0x48, 0x83, 0xC4, 0x30, 0x5B, 0x5E, 0x5D, 0xC3, 0x0F, 0x1F, 0x00,
                                       0x48, 0x8D, 0x4E, 0x02, 0x48, 0x83, 0xC4, 0x30, 0x5B, 0x5E, 0x5D, 0xE9, 0xD0, 0x6A, 0x00, 0x00};
/* test dx,dx; jne A; test bx,bx; je A; lea rcx,[rsi+2]; add rsp,30h; pop rbx/rsi/rbp; jmp RtlWakeAddressSingle */
static const uint8_t g_lwSwNewB[32] = {0x66, 0x85, 0xD2, 0x75, 0xC2, 0x66, 0x85, 0xDB, 0x74, 0xBD, 0x48, 0x8D, 0x4E, 0x02, 0x48, 0x83,
                                       0xC4, 0x30, 0x5B, 0x5E, 0x5D, 0xE9, 0xD6, 0x6A, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
static struct { int ready, on; uintptr_t nt; } g_lwSw;

/* Every other thread of the process suspended, each one confirmed stopped (a context read) with its rip outside both
 * ranges; 0 and nothing left suspended when any thread cannot be. No lock, allocation or log while threads are stopped. */
#define LW_SW_THREADS 1024
static HANDLE g_lwSwThreads[LW_SW_THREADS];
/* One thread suspended and kept in the list; 0 when it cannot be, or it stopped inside a range. */
static int LwSwHold(DWORD id, int* count)
{
    if (*count >= LW_SW_THREADS) return 0;
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, id);
    if (!t) return 0;
    if (SuspendThread(t) == (DWORD)-1) { CloseHandle(t); return 0; }
    g_lwSwThreads[(*count)++] = t;
    CONTEXT c = {0}; c.ContextFlags = CONTEXT_CONTROL;
    uintptr_t a = g_lwSw.nt + LW_SW_A, b = g_lwSw.nt + LW_SW_B;
    return GetThreadContext(t, &c) && !(c.Rip >= a && c.Rip < a + 9) && !(c.Rip >= b && c.Rip < b + 32);
}
static int LwSwFreeze(int* count)
{
    *count = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te = {sizeof te}; DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId(); int ok = 1;
    for (BOOL more = Thread32First(snap, &te); more && ok; more = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) ok = LwSwHold(te.th32ThreadID, count);
    CloseHandle(snap);
    return ok;
}
static void LwSwThaw(int count) { for (int i = 0; i < count; i++) { ResumeThread(g_lwSwThreads[i]); CloseHandle(g_lwSwThreads[i]); } }

/* One try: the bytes copied while every other thread is stopped outside both ranges; 1 when copied. */
static int LwSwFrozenCopy(uint8_t* a, const uint8_t* wa, uint8_t* b, const uint8_t* wb, int on)
{
    int n = 0, done = LwSwFreeze(&n);
    /* the no-wake epilogue goes in before the tail that jumps to it, and comes out after it */
    if (done && on) { memcpy(a, wa, 9); memcpy(b, wb, 32); }
    if (done && !on) { memcpy(b, wb, 32); memcpy(a, wa, 9); }
    LwSwThaw(n);
    if (!done) Sleep(1);
    return done;
}

/* Both ranges to the new or the original bytes; 0 when they hold them afterwards. */
static int LwSwWrite(int on)
{
    uint8_t* a = (uint8_t*)(g_lwSw.nt + LW_SW_A); uint8_t* b = (uint8_t*)(g_lwSw.nt + LW_SW_B);
    const uint8_t *wa = on ? g_lwSwNewA : g_lwSwOldA, *wb = on ? g_lwSwNewB : g_lwSwOldB;
    if (!memcmp(a, wa, 9) && !memcmp(b, wb, 32)) return 0;
    if (memcmp(a, on ? g_lwSwOldA : g_lwSwNewA, 9) || memcmp(b, on ? g_lwSwOldB : g_lwSwNewB, 32)) return 1; /* someone else's bytes */
    DWORD pa, pb; int done = 0;
    if (!VirtualProtect(a, 9, PAGE_EXECUTE_READWRITE, &pa)) return 1;
    if (!VirtualProtect(b, 32, PAGE_EXECUTE_READWRITE, &pb)) { VirtualProtect(a, 9, pa, &pa); return 1; }
    AcquireSRWLockExclusive(&g_hookLock); /* no hook goes in meanwhile */
    for (int tries = 0; tries < 20 && !done; tries++) done = LwSwFrozenCopy(a, wa, b, wb, on);
    ReleaseSRWLockExclusive(&g_hookLock);
    VirtualProtect(b, 32, pb, &pb); VirtualProtect(a, 9, pa, &pa);
    if (!done) return 1;
    FlushInstructionCache(GetCurrentProcess(), a, (size_t)(b + 32 - a));
    NoteRewrite((uintptr_t)a, 9); NoteRewrite((uintptr_t)b, 32);
    RecordPatch((uintptr_t)a, wa, 9); RecordPatch((uintptr_t)b, wb, 32);
    return 0;
}

static void LwSwSet(int on)
{
    if (LwSwWrite(on)) { Note("living world: Wine's shared SRW release: not changed (other bytes there, or a thread could not be stopped outside them)"); return; }
    g_lwSw.on = on;
    Note("living world: Wine's shared SRW release %s", on ? "wakes only with an exclusive waiter (ntdll 0x33F50)" : "as Wine has it");
}

static void LwInstallSrwWake(void)
{
    static int done; char t[4] = "";
    if (done++) return;
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    if (!nt) return;
    g_lwSw.nt = (uintptr_t)nt;
    for (size_t i = 0; i < sizeof g_lwSwExports / sizeof g_lwSwExports[0]; i++)
        if ((uintptr_t)GetProcAddress(nt, g_lwSwExports[i].name) != g_lwSw.nt + g_lwSwExports[i].rva)
        { Note("living world: Wine's shared SRW release: not this Proton build's ntdll (%s); not changed", g_lwSwExports[i].name); return; }
    for (size_t i = 0; i < sizeof g_lwSwPins / sizeof g_lwSwPins[0]; i++)
        if (!ApiCodeMatches(g_lwSw.nt + g_lwSwPins[i].rva, g_lwSwPins[i].bytes))
        { Note("living world: Wine's shared SRW release: ntdll+0x%X is not the reviewed code; not changed", g_lwSwPins[i].rva); return; }
    g_lwSw.ready = 1;
    if (GetEnvironmentVariableA("HELLFORGE_LW_SRWWAKE", t, sizeof t) > 0 && t[0] == '1') LwSwSet(1);
}

static void LwSrwWakeCmd(int on)
{
    if (!g_lwSw.ready || on < 0 || on > 1) { Note("living world: test: srwwake: not this Proton build's ntdll, or a value outside 0..1"); return; }
    LwSwSet(on);
}
