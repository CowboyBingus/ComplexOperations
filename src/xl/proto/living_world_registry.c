/* The unit registry's lookup (exe 0x9D8C0, 662 call sites) takes the registry's SRW lock shared around three loads.
 * That lock only keeps a reader off arrays that a grow frees: generation and pointer writes outside create already run
 * under the shared side. So the lookup's entry jumps to a lock-free copy in a near page, and the two grows inside
 * create (0x1709B1, 0x1709F1) go to a keep-grow that copies, publishes and never frees the old block. The copy reloads
 * the generation array after the pointer load and checks the generation again. It returns 0 only when a destroy of
 * that handle overlaps the lookup, a window vanilla has too. The keep-grow stays for the process once in, so a reader
 * still inside the copy never meets a freed array. Design and review: ~/xl-work/static/living/levers-static2/.
 * HELLFORGE_LW_REGISTRY=1 / command "registry 0|1". */
#define LW_REG_GLOBAL 0x1A100F0u
#define LW_REG_LOOKUP 0x9D8C0u
#define LW_REG_GEN_CALL 0x1709B1u
#define LW_REG_PTR_CALL 0x1709F1u
#define LW_REG_GEN_GROW 0xA8C50u
#define LW_REG_PTR_GROW 0xA6BA0u
#define LW_REG_ACQ_X 0x13F6750u
#define LW_REG_REL_X 0x13F6748u
#define LW_REG_VTABLE 0x168B5A0u
#define LW_REG_CAP (1u << 18) /* slots reserved at the first install: 2.25 MB for both arrays */
#define LW_REG_ENTRY_PIN "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20"
static const struct { uint32_t rva; const char* bytes; } g_lwRegPins[] = {
    {LW_REG_LOOKUP, LW_REG_ENTRY_PIN " 48 8B 35 1A 28 97 01 8B D9 48 8D 8E D0 00 00 00 FF 15 7B 8E 35 01 8B C3 25 FF FF 3F 00 "
        "3B 86 98 00 00 00 72 04 33 DB EB 1C 8B C8 48 8B 86 A0 00 00 00 C1 EB 16 38 1C 01 75 EB 48 8B 86 88 00 00 00 48 8B "
        "1C C8 48 8D 8E D0 00 00 00 FF 15 37 8E 35 01 48 8B 74 24 38 48 8B C3 48 8B 5C 24 30 48 83 C4 20 5F C3"},
    {0x1708E0u, "48 89 5C 24 18 55 56 57 41 54 41 57 48 83 EC 20 48 8B F9 4D 8B E0 48 81 C1 D0 00 00 00 4C 8B FA FF 15 4A 5E 28 01"},
    {0x170906u, "44 8B 97 CC 00 00 00 41 81 FA 00 04 00 00"},
    {0x1709A9u, "48 8B CB 8B D1 49 8B CE E8 9A 82 F3 FF 41 8B 16"},
    {0x1709C4u, "8B CA C6 04 01 00 41 FF 06 8B"},
    {0x1709E8u, "49 0F 47 DD 8B D3 48 8B CE E8 AA 61 F3 FF 8B 0E 48 8B 46 08"},
    {0x170A01u, "8B D1 48 C7 04 D0 00 00 00 00 FF 06 41"},
    {0x170A17u, "48 8B 87 88 00 00 00 48 8D 8F D0 00 00 00 8B DE 4C 89 24 D8 FF 15 17 5D 28 01"},
    {LW_REG_GEN_GROW, "48 89 5C 24 10 57 48 83 EC 30 8B 41 04 48 8B D9 8B FA 3B D0 74 67"},
    {0xA8C82u, "48 8B 4B 10 44 8D 4E 08 4C 8B C7 48 8D 54 24 20 48 8B 01 FF 50 30 48 8B 30 8B 03 85 C0 74 0F 48 8B 53 08 44 8B C0 "
        "48 8B CE E8 A0 EB 1B 01 48 8B 4B 10 45 33 C0 48 8B 53 08 48 8B 01 FF 50 40 48 89 73 08 48 8B 74 24 40 89 7B 04"},
    {LW_REG_PTR_GROW, "48 89 5C 24 10 57 48 83 EC 30 8B FA 48 8B D9 3B 51 04 74 69"},
    {0xA6BC8u, "48 8B 4B 10 44 8D 04 FD 00 00 00 00 44 8D 4E 08 48 8D 54 24 20 4C 8B 11 41 FF 52 30 48 8B 30 8B 03 85 C0 74 13 "
        "48 8B 53 08 44 8B C0 49 C1 E0 03 48 8B CE E8 50 0C 1C 01 48 8B 4B 10 45 33 C0 48 8B 53 08 48 8B 01 FF 50 40 48 89 73 08 "
        "48 8B 74 24 40 89 7B 04"},
};
/* Assembled with GNU as (lever note). +0x00 the lookup, a leaf on rax/rcx/rdx/r8/r9 only: the bound on the pointer
 * count, which create raises last, then on the generation count, which teardown zeroes first. +0x53 / +0x65 the
 * thunks for the two grow calls: elem size in r8d, then jmp to LwRegGrowKeep. */
static const uint8_t g_lwRegCode[] = {
    0x48, 0x8B, 0x05, 0, 0, 0, 0,             /* +00 mov rax,[rip+global] */
    0x48, 0x85, 0xC0, 0x74, 0x44,             /* +07 test rax,rax; jz +50 */
    0x89, 0xCA, 0x81, 0xE2, 0xFF, 0xFF, 0x3F, 0x00, /* +0C mov edx,ecx; and edx,3FFFFFh */
    0x3B, 0x90, 0x80, 0x00, 0x00, 0x00, 0x73, 0x34, /* +14 cmp edx,[rax+80h]; jae +50 */
    0x3B, 0x90, 0x98, 0x00, 0x00, 0x00, 0x73, 0x2C, /* +1C cmp edx,[rax+98h]; jae +50 */
    0xC1, 0xE9, 0x16,                         /* +24 shr ecx,22 */
    0x4C, 0x8B, 0x80, 0xA0, 0x00, 0x00, 0x00, /* +27 mov r8,[rax+0A0h] */
    0x41, 0x38, 0x0C, 0x10, 0x75, 0x1C,       /* +2E cmp [r8+rdx],cl; jne +50 */
    0x4C, 0x8B, 0x88, 0x88, 0x00, 0x00, 0x00, /* +34 mov r9,[rax+88h] */
    0x4D, 0x8B, 0x0C, 0xD1,                   /* +3B mov r9,[r9+rdx*8] */
    0x4C, 0x8B, 0x80, 0xA0, 0x00, 0x00, 0x00, /* +3F mov r8,[rax+0A0h]: a grow may have moved it */
    0x41, 0x38, 0x0C, 0x10, 0x75, 0x04,       /* +46 cmp [r8+rdx],cl; jne +50 */
    0x4C, 0x89, 0xC8, 0xC3,                   /* +4C mov rax,r9; ret */
    0x31, 0xC0, 0xC3,                         /* +50 xor eax,eax; ret */
    0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0, /* +53 generations */
    0x41, 0xB8, 0x08, 0x00, 0x00, 0x00, 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0, /* +65 pointers */
};
_Static_assert(sizeof g_lwRegCode == 0x77, "registry stub size");
#define LW_REG_STUB_GEN 0x53u
#define LW_REG_STUB_PTR 0x65u

/* +0 size, +4 capacity, +8 data, +0x10 allocator (create 0x170986..0x1709F8, grows 0xA8C82 / 0xA6BC8). */
typedef struct { volatile uint32_t size, cap; uint8_t* volatile data; void** alloc; } LwRegVec;
_Static_assert(offsetof(LwRegVec, data) == 8 && offsetof(LwRegVec, alloc) == 0x10, "registry vector layout");
typedef void (*LwRegLockFn)(void* lock);
static struct {
    int ready, kept, on, waiting; /* waiting: asked for before the game built its registry */
    uintptr_t exe;
    uint8_t* page;
    volatile LONG grows, blocks; /* keep-grow calls; old blocks kept */
    volatile LONG64 bytes;
    LONG reserveGrows, reserveBlocks; LONG64 reserveBytes; /* the part of them from the first install's reserve */
} g_lwReg;

/* Raw bytes, not read through hooks: a hook here would be overwritten by these writes. */
static int LwRegRawMatches(uintptr_t at, const char* pattern)
{
    uint8_t want[256], mask[256], got[256];
    size_t n = ParsePattern(pattern, want, mask);
    if (n == 0 || SafeRead(at, got, n)) return 0;
    for (size_t i = 0; i < n; i++) if (mask[i] && got[i] != want[i]) return 0;
    return 1;
}

/* The lookup's entry as the game has it, or this lever's jmp to its copy with the rest of the prologue untouched. For
 * the stale object and body sync guards, which call 0x9D8C0 for its result and work with either. */
static int LwRegLookupEntryOk(uintptr_t exe)
{
    if (ApiCodeMatches(exe + LW_REG_LOOKUP, LW_REG_ENTRY_PIN)) return 1;
    uint8_t e[5]; int32_t rel;
    if (g_lwReg.page == NULL || SafeRead(exe + LW_REG_LOOKUP, e, 5) || e[0] != 0xE9) return 0;
    memcpy(&rel, e + 1, 4);
    return exe + LW_REG_LOOKUP + 5 + (intptr_t)rel == (uintptr_t)g_lwReg.page && ApiCodeMatches(exe + LW_REG_LOOKUP + 5, "48 89 74 24 10 57 48 83 EC 20");
}

/* Called from create's grow sites under the registry's exclusive lock (rcx vector, edx capacity, r8d from the thunk).
 * The old block stays allocated: a lock-free reader may hold its address. sfence orders a large memcpy's non-temporal
 * stores; the release stores keep the compiler from moving the publication before the copy. */
static void LwRegGrowKeep(LwRegVec* v, uint32_t cap, uint32_t elem)
{
    typedef uint64_t* (*AllocFn)(void* self, uint64_t* out, uint64_t bytes, uint32_t align);
    typedef void (*GrowFn)(LwRegVec* v, uint32_t cap);
    if (cap == v->cap) return;
    uint8_t* fresh = NULL;
    if (cap >= v->size && cap != 0)
    {
        uint64_t out[2] = {0, 0};
        fresh = (uint8_t*)(uintptr_t)*((AllocFn)((void**)*v->alloc)[6])(v->alloc, out, (uint64_t)cap * elem, 8); /* vtable +0x30 */
    }
    if (fresh == NULL) { ((GrowFn)(g_lwReg.exe + (elem == 1 ? LW_REG_GEN_GROW : LW_REG_PTR_GROW)))(v, cap); return; } /* the game's own: a shrink or no memory */
    uint8_t* old = v->data;
    uint32_t oldCap = v->cap;
    if (old != NULL) memcpy(fresh, old, (size_t)v->size * elem);
    __builtin_ia32_sfence();
    __atomic_store_n(&v->data, fresh, __ATOMIC_RELEASE); /* pointer, then capacity: 0xA8CC1 / 0xA8CCA's order */
    __atomic_store_n(&v->cap, cap, __ATOMIC_RELEASE);
    InterlockedIncrement(&g_lwReg.grows);
    if (old != NULL) { InterlockedIncrement(&g_lwReg.blocks); InterlockedAdd64(&g_lwReg.bytes, (LONG64)oldCap * elem); }
}

/* The stub page: the lookup copy and both thunks, with the global's displacement and LwRegGrowKeep's address. */
static uint8_t* LwRegBuildPage(uintptr_t exe)
{
    uint8_t* page = AllocateNearGame(exe, 0x1000);
    if (page == NULL) return NULL;
    memcpy(page, g_lwRegCode, sizeof g_lwRegCode);
    intptr_t reach[] = {(intptr_t)(exe + LW_REG_GLOBAL) - (intptr_t)(page + 7), (intptr_t)page - (intptr_t)(exe + LW_REG_LOOKUP + 5),
                        (intptr_t)(page + LW_REG_STUB_GEN) - (intptr_t)(exe + LW_REG_GEN_CALL + 5),
                        (intptr_t)(page + LW_REG_STUB_PTR) - (intptr_t)(exe + LW_REG_PTR_CALL + 5)};
    for (size_t i = 0; i < sizeof reach / sizeof reach[0]; i++)
        if (reach[i] != (intptr_t)(int32_t)reach[i]) return NULL; /* the page stays: a few KB, never reached */
    int32_t d32 = (int32_t)reach[0];
    uint64_t fn = (uint64_t)(uintptr_t)LwRegGrowKeep;
    memcpy(page + 3, &d32, 4);
    memcpy(page + LW_REG_STUB_GEN + 8, &fn, 8);
    memcpy(page + LW_REG_STUB_PTR + 8, &fn, 8);
    DWORD old;
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) return NULL;
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    return page;
}

/* One change inside an aligned qword: a call's rel32, or the entry's first instruction. */
typedef struct { uintptr_t at; uint8_t bytes[5]; uint8_t len; } LwRegEdit;
static void LwRegEditCall(LwRegEdit* e, uint32_t call, uint32_t thunk)
{
    int32_t rel = (int32_t)((intptr_t)(g_lwReg.page + thunk) - (intptr_t)(g_lwReg.exe + call + 5));
    e->at = g_lwReg.exe + call + 1; e->len = 4; memcpy(e->bytes, &rel, 4);
}
static void LwRegEditEntry(LwRegEdit* e, int on)
{
    static const uint8_t game[5] = {0x48, 0x89, 0x5C, 0x24, 0x08}; /* mov [rsp+8],rbx */
    int32_t rel = (int32_t)((intptr_t)g_lwReg.page - (intptr_t)(g_lwReg.exe + LW_REG_LOOKUP + 5));
    e->at = g_lwReg.exe + LW_REG_LOOKUP; e->len = 5;
    if (on) { e->bytes[0] = 0xE9; memcpy(e->bytes + 1, &rel, 4); }
    else memcpy(e->bytes, game, 5);
}

/* Every other thread suspended and confirmed stopped (a context read). A rip strictly inside a 5-byte range cannot
 * occur, since each range is one instruction; one at its start resumes into the new bytes. No lock, allocation or log
 * while threads are stopped. */
#define LW_REG_THREADS 1024
static HANDLE g_lwRegStopped[LW_REG_THREADS];
static int LwRegSuspend(DWORD id, int* count, DWORD64* rip) /* also living_world_zerobox.c */
{
    HANDLE h = *count < LW_REG_THREADS ? OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, id) : NULL;
    if (h == NULL) return 0;
    if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); return 0; }
    g_lwRegStopped[(*count)++] = h;
    CONTEXT ctx = {0}; ctx.ContextFlags = CONTEXT_CONTROL;
    if (!GetThreadContext(h, &ctx)) return 0;
    *rip = ctx.Rip;
    return 1;
}
static int LwRegStopOne(DWORD id, int* count)
{
    DWORD64 rip = 0;
    if (!LwRegSuspend(id, count, &rip)) return 0;
    static const uint32_t starts[] = {LW_REG_LOOKUP, LW_REG_GEN_CALL, LW_REG_PTR_CALL};
    for (int i = 0; i < 3; i++)
        if (rip > g_lwReg.exe + starts[i] && rip < g_lwReg.exe + starts[i] + 5) return 0;
    return 1;
}
static void LwRegResume(int count)
{
    while (count > 0) { count--; ResumeThread(g_lwRegStopped[count]); CloseHandle(g_lwRegStopped[count]); }
}

/* The edits made while every other thread is stopped, each as one aligned 8-byte store; 0 when made. At most four
 * tries and no sleep between them: the first install holds the registry's lock exclusive meanwhile. */
static int LwRegStoppedWrite(const LwRegEdit* edits, int n)
{
    for (int tries = 0; tries < 4; tries++)
    {
        int count = 0, ok = 1;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return 1;
        THREADENTRY32 te = {sizeof te};
        DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
        for (BOOL more = Thread32First(snap, &te); more && ok; more = Thread32Next(snap, &te))
            if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) ok = LwRegStopOne(te.th32ThreadID, &count);
        CloseHandle(snap);
        for (int i = 0; ok && i < n; i++)
        {
            volatile uint64_t* q = (volatile uint64_t*)(edits[i].at & ~(uintptr_t)7);
            uint64_t v = *q;
            memcpy((uint8_t*)&v + (edits[i].at & 7), edits[i].bytes, edits[i].len);
            __atomic_store_n(q, v, __ATOMIC_SEQ_CST);
        }
        LwRegResume(count);
        if (ok) return 0;
    }
    return 1;
}

/* The first install's hold of the registry lock: sizes read and the reserve made inside, logged after the release. */
typedef struct { uintptr_t r; LwRegLockFn acquire, release; uint32_t sizes[5]; } LwRegHold;
static void LwRegReserve(LwRegHold* h)
{
    LwRegVec* ptr = (LwRegVec*)(h->r + 0x80);
    LwRegVec* gen = (LwRegVec*)(h->r + 0x98);
    h->sizes[0] = ptr->size; h->sizes[1] = ptr->cap; h->sizes[2] = gen->size; h->sizes[3] = gen->cap;
    h->sizes[4] = *(volatile uint32_t*)(h->r + 0xCC); /* free indices waiting in the ring */
    /* through the keep-grow too: a lock-free reader this review could not rule out keeps its block */
    if (gen->cap < LW_REG_CAP) LwRegGrowKeep(gen, LW_REG_CAP, 1);
    if (ptr->cap < LW_REG_CAP) LwRegGrowKeep(ptr, LW_REG_CAP, 8);
}

/* The writes with the code pages writable and the loader's hook lock held; with `hold`, the registry's lock is taken
 * exclusive after it (one order) around the reserve and the writes. Then flushed, noted and recorded; 0 when made. */
static int LwRegCommit(const LwRegEdit* edits, int n, LwRegHold* hold)
{
    DWORD prot[4]; int made = 0, bad = 1;
    while (made < n && made < 4 && VirtualProtect((void*)(edits[made].at & ~(uintptr_t)7), 8, PAGE_EXECUTE_READWRITE, &prot[made])) made++;
    if (made == n)
    {
        AcquireSRWLockExclusive(&g_hookLock); /* no hook goes in meanwhile */
        if (hold) { hold->acquire((void*)(hold->r + 0xD0)); LwRegReserve(hold); }
        bad = LwRegStoppedWrite(edits, n);
        if (hold) hold->release((void*)(hold->r + 0xD0));
        ReleaseSRWLockExclusive(&g_hookLock);
    }
    while (made > 0) { made--; VirtualProtect((void*)(edits[made].at & ~(uintptr_t)7), 8, prot[made], &prot[made]); }
    if (bad) return 1;
    for (int i = 0; i < n; i++)
    {
        FlushInstructionCache(GetCurrentProcess(), (void*)edits[i].at, edits[i].len);
        NoteRewrite(edits[i].at, edits[i].len);
        RecordPatch(edits[i].at, edits[i].bytes, edits[i].len);
    }
    return 0;
}

/* The first "on": pins and live layout read again, the page built, then reserve, keep-grow and entry in one hold. */
static const char* LwRegFirst(void)
{
    uintptr_t exe = g_lwReg.exe, r = 0, vt = 0, a80 = 0, a98 = 0, acq = 0, rel = 0;
    g_lwReg.waiting = 0;
    for (size_t i = 0; i < sizeof g_lwRegPins / sizeof g_lwRegPins[0]; i++)
        if (!LwRegRawMatches(exe + g_lwRegPins[i].rva, g_lwRegPins[i].bytes)) return "the lookup, create or a grow is not the reviewed code";
    if (SafeRead(exe + LW_REG_GLOBAL, &r, 8)) return "the registry's global does not read";
    if (r == 0) { g_lwReg.waiting = 1; return "the game has not built its registry yet (tried again with the 30 s notes)"; }
    if (SafeRead(r, &vt, 8) || SafeRead(r + 0x90, &a80, 8) || SafeRead(r + 0xA8, &a98, 8) || vt != exe + LW_REG_VTABLE || a80 != r || a98 != r)
        return "the registry is not a proxy allocator owning both arrays";
    if (SafeRead(exe + LW_REG_ACQ_X, &acq, 8) || SafeRead(exe + LW_REG_REL_X, &rel, 8) || acq == 0 || rel == 0) return "the lock imports do not read";
    if (g_lwReg.page == NULL && (g_lwReg.page = LwRegBuildPage(exe)) == NULL) return "no executable page within reach of the exe";
    LwRegEdit edits[3];
    LwRegEditCall(&edits[0], LW_REG_GEN_CALL, LW_REG_STUB_GEN);
    LwRegEditCall(&edits[1], LW_REG_PTR_CALL, LW_REG_STUB_PTR);
    LwRegEditEntry(&edits[2], 1); /* last: the keep-grow is in before the first lock-free reader */
    LwRegHold hold = {r, (LwRegLockFn)acq, (LwRegLockFn)rel, {0}};
    if (LwRegCommit(edits, 3, &hold)) return "a thread could not be stopped, or the code could not be made writable";
    g_lwReg.kept = 1;
    g_lwReg.reserveGrows = g_lwReg.grows; g_lwReg.reserveBlocks = g_lwReg.blocks; g_lwReg.reserveBytes = g_lwReg.bytes;
    Note("living world: unit registry at install: pointers %u of %u, generations %u of %u, %u free indices waiting; reserved %u slots (%ld old blocks kept, %.0f KB)",
        hold.sizes[0], hold.sizes[1], hold.sizes[2], hold.sizes[3], hold.sizes[4], LW_REG_CAP, (long)g_lwReg.reserveBlocks, (double)g_lwReg.reserveBytes / 1024.0);
    return NULL;
}

/* Later switches move the entry only: the keep-grow stays, so a reader still in the copy keeps valid arrays. */
static const char* LwRegEntry(int on)
{
    LwRegEdit want, other; uint8_t now[5];
    LwRegEditEntry(&want, on); LwRegEditEntry(&other, !on);
    if (SafeRead(want.at, now, 5)) return "the lookup's entry does not read";
    if (!memcmp(now, want.bytes, 5)) return NULL;
    if (memcmp(now, other.bytes, 5)) return "the lookup's entry holds someone else's bytes";
    return LwRegCommit(&want, 1, NULL) ? "a thread could not be stopped, or the entry could not be made writable" : NULL;
}

static void LwRegSet(int on)
{
    const char* why = g_lwReg.kept ? LwRegEntry(on) : on ? LwRegFirst() : NULL;
    if (why) { Note("living world: unit registry lookup: not changed: %s", why); return; }
    g_lwReg.on = on && g_lwReg.kept;
    Note("living world: unit registry lookup %s", g_lwReg.on ? "without its lock (exe 0x9D8C0 jumps to a lock-free copy; old arrays kept)" :
        g_lwReg.kept ? "as the game has it (old arrays still kept)" : "as the game has it");
}

static void LwInstallRegistry(void)
{
    static int done; char t[4] = "";
    if (done++) return;
    g_lwReg.exe = (uintptr_t)GetModuleHandleA(NULL);
    if (strcmp(g_build, "25480438") || !LwRegRawMatches(g_lwReg.exe + LW_REG_LOOKUP, g_lwRegPins[0].bytes))
    { Note("living world: unit registry lookup: not this build's code; not changed"); return; }
    g_lwReg.ready = 1;
    if (GetEnvironmentVariableA("HELLFORGE_LW_REGISTRY", t, sizeof t) > 0 && t[0] == '1') LwRegSet(1);
}

static void LwRegistryCmd(int on)
{
    if (!g_lwReg.ready || on < 0 || on > 1) { Note("living world: test: registry: not this build's code, or a value outside 0..1"); return; }
    LwRegSet(on);
}

/* With the 30 s notes. No count of lookups: any shared counter on this path is the cache line traffic the lever
 * removes. The keep-grow counts are rare (create's grows only). */
static void LwRegNote(void)
{
    if (g_lwReg.waiting) { LwRegSet(1); return; }
    if (!g_lwReg.kept) return;
    uintptr_t r = 0; uint32_t s[4] = {0, 0, 0, 0};
    if (!SafeRead(g_lwReg.exe + LW_REG_GLOBAL, &r, 8) && r) { SafeRead(r + 0x80, s, 8); SafeRead(r + 0x98, s + 2, 8); }
    Note("living world: unit registry (30 s): lookup %s; pointers %u of %u, generations %u of %u; since the reserve %ld keep-grows, %ld old blocks kept (%.0f KB)",
        g_lwReg.on ? "lock-free" : "the game's", s[0], s[1], s[2], s[3], (long)(g_lwReg.grows - g_lwReg.reserveGrows),
        (long)(g_lwReg.blocks - g_lwReg.reserveBlocks), (double)(g_lwReg.bytes - g_lwReg.reserveBytes) / 1024.0);
}
