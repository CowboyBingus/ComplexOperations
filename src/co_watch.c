/* The war-table crash's watch (co_watch.h).
 *
 * Threads are armed from a worker thread of the watch's own: a thread cannot set its own debug registers while it
 * runs. While a thread is suspended the worker calls nothing that allocates or logs (the thread may hold the heap's
 * lock); the snapshots and the module list are taken before. The handler is first in the vectored list while threads
 * are armed, takes only single steps on threads the watch armed, and logs from the trapping thread: a crash can follow
 * the write within a frame, and a log written later would be lost with the process. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <tlhelp32.h>

#include "co.h"
#include "co_watch.h"

/* W's flags (0.5f in the first crash) and link (0 and 64.0f), N4's element (0) and first child (0xC00C1017, the second). */
const CoWatchSpot co_watch_spots[CO_WATCH_SPOTS] = {
    {CO_WATCH_W, 4u, "W+0x0"}, {CO_WATCH_W + 0xE0u, 8u, "W+0xE0"}, {CO_WATCH_N4, 8u, "N4+0x0"}, {CO_WATCH_N4 + 0x10u, 8u, "N4+0x10"}};

/* ---- what runs without a game ------------------------------------------------------------------------------------ */

int co_watch_plan(uintptr_t object, CoWatchPlan* plan)
{
    memset(plan, 0, sizeof *plan);
    plan->object = object;
    for (int k = 0; k < CO_WATCH_SPOTS; k++)
    {
        plan->at[k] = object + co_watch_spots[k].offset;
        plan->length[k] = co_watch_spots[k].length;
        if ((plan->at[k] & (plan->length[k] - 1u)) != 0) return 0;
    }
    plan->count = CO_WATCH_SPOTS;
    return 1;
}

uint64_t co_watch_dr7(const CoWatchPlan* plan)
{
    uint64_t dr7 = 0;
    if (plan->count < 1 || plan->count > CO_WATCH_SPOTS) return 0;
    for (int k = 0; k < plan->count; k++)
    {
        uint64_t len;
        switch (plan->length[k]) /* Dr7's LEN: 00 one byte, 01 two, 10 eight, 11 four */
        {
        case 1: len = 0; break;
        case 2: len = 1; break;
        case 8: len = 2; break;
        case 4: len = 3; break;
        default: return 0;
        }
        if (plan->at[k] == 0 || (plan->at[k] & (plan->length[k] - 1u)) != 0) return 0;
        dr7 |= 1ull << (2 * k);                         /* Lk */
        dr7 |= (1ull | (len << 2)) << (16 + 4 * k);     /* RWk = 01: data writes */
    }
    return dr7;
}

int co_watch_claim(uint32_t code, uint64_t dr6, uint64_t eflags, int armed)
{
    if (code != 0x80000004u || !armed) return CO_WATCH_PASS; /* EXCEPTION_SINGLE_STEP on a thread the watch armed */
    if ((dr6 & 0xF) != 0) return CO_WATCH_CLAIM;
    /* Wine reports Dr6 as 0; a single step of the trap flag (Dr6's BS, or TF still set) is someone else's */
    if ((dr6 & 0x4000) == 0 && (eflags & 0x100) == 0) return CO_WATCH_CLAIM_UNKNOWN;
    return CO_WATCH_PASS;
}

int co_watch_in_library(uintptr_t game, uintptr_t rip)
{
    return game != 0 && rip >= game + CO_WATCH_LIBRARY_FIRST && rip < game + CO_WATCH_LIBRARY_END;
}

int co_watch_known(uintptr_t game, uintptr_t rip)
{
    if (co_watch_in_library(game, rip)) return CO_WATCH_IN_LIBRARY;
    if (game != 0 && (rip == game + CO_WATCH_NODE_INIT_CHILD || rip == game + CO_WATCH_NODE_INIT_ELEMENT)) return CO_WATCH_IN_NODE_INIT;
    return CO_WATCH_ELSEWHERE;
}

static int NotPointer(uint64_t v) { return (v & 7) != 0 || v < 0x10000 || v > 0x7FFFFFFFFFFFull; }

int co_watch_suspicious(int spot, uint64_t value)
{
    switch (spot)
    {
    case CO_WATCH_FLAGS:
    {
        const uint32_t v = (uint32_t)value, exponent = (v >> 23) & 0xFF; /* a float from 1/8 to 255 with a short mantissa */
        return (v & 0xFFFF) == 0 && exponent >= 0x7C && exponent <= 0x86;
    }
    case CO_WATCH_LINK: return value != 0 && NotPointer(value); /* a widget pointer or 0 */
    case CO_WATCH_ELEMENT: return NotPointer(value);            /* C itself, never 0: the walk reads it */
    case CO_WATCH_CHILD: return value != 0 && NotPointer(value);
    }
    return 0;
}

int co_watch_filter(CoWatchFilter* f, uintptr_t rip, int known, int suspicious)
{
    int slot = -1, reasons = 0;
    long seen = 0;
    for (int i = 0; i < CO_WATCH_PLACES && slot < 0; i++)
    {
        if (f->place[i] == rip) slot = i;
        else if (f->place[i] == 0)
        {
            if (InterlockedCompareExchange64((volatile LONG64*)&f->place[i], (LONG64)rip, 0) == 0) slot = i;
            else if (f->place[i] == rip) slot = i;
        }
    }
    if (slot >= 0) { seen = InterlockedIncrement(&f->count[slot]); if (f->known[slot] != known) f->known[slot] = known; }
    else InterlockedIncrement(&f->overflow);
    if (seen >= 1 && seen <= CO_WATCH_PER_PLACE && InterlockedIncrement(&f->logged[0]) <= CO_WATCH_CAP_NEW) reasons |= CO_WATCH_LOG_NEW;
    if (suspicious && InterlockedIncrement(&f->logged[1]) <= CO_WATCH_CAP_SUSPICIOUS) reasons |= CO_WATCH_LOG_SUSPICIOUS;
    return reasons;
}

/* ---- the state ---------------------------------------------------------------------------------------------------- */

#define THREADS 1024
#define MODULES 256
#define LINE 960
#define FRAMES 16
#define WAIT_FREE_MS 3000u
#define STATUS_MS 10000u
#define RESCAN_MS 500u
enum { JOB_NONE, JOB_ARM, JOB_DISARM };

typedef struct Module { uintptr_t base; uint32_t size; char name[32]; } Module;
static Module g_modules[2][MODULES];
static volatile LONG g_moduleCount[2], g_moduleSet;

static uintptr_t g_game;
static volatile LONG g_works = -1;           /* the self-test: -1 not run, 0 no trap, 1 both lengths trapped */
static volatile LONG g_live;                 /* O is alive and the plan's words may be read */
static CoWatchPlan g_current;
static uint64_t g_dr7;
static CoWatchFilter g_filter;
static volatile LONG g_hits, g_hitsAfter;
static volatile LONG64 g_last[CO_WATCH_SPOTS];
static volatile LONG g_armed[THREADS], g_armedCount; /* every thread ever armed: a trap on one is the watch's */
static DWORD g_now[THREADS];                 /* armed for this O (the worker's only) */
static int g_nowCount, g_nowLater;
static PVOID g_handler;
static HANDLE g_wake;
static DWORD g_worker;
static volatile LONG g_rescan;
static SRWLOCK g_askLock = SRWLOCK_INIT;
static volatile LONG g_ask, g_askSerial, g_doneSerial;
static CoWatchPlan g_askPlan;
static unsigned g_statusMs = STATUS_MS;

/* the self-test's helper and what its two writes left */
static volatile LONG g_testThread, g_testTraps;
static DWORD64 g_testRip[4], g_testDr6[4];
static volatile uint64_t g_testWords[2] __attribute__((aligned(16)));

static void LogLine(const char* text) { if (g_coApi != NULL) g_coApi->log(g_coMod, "%s", text); }

/* On unless HELLFORGE_CO_WATCH=0 or the mod's folder holds wartable-watch.off; read once. */
#ifdef CO_TEST
static int g_testOn; /* only the watch's own test turns it on: the other tests load the mod without it */
#endif
int co_watch_on(void)
{
#ifdef CO_TEST
    if (!g_testOn) return 0;
#endif
    static volatile LONG on = -1;
    if (on >= 0) return on;
    char value[8] = "";
    int result = !(GetEnvironmentVariableA("HELLFORGE_CO_WATCH", value, sizeof value) > 0 && value[0] == '0');
    const char* folder = g_coApi != NULL && g_coApi->mod_folder != NULL ? g_coApi->mod_folder(g_coMod) : NULL;
    if (result && folder != NULL && folder[0] != 0)
    {
        char path[MAX_PATH * 3];
        wchar_t wide[MAX_PATH * 2];
        snprintf(path, sizeof path, "%s\\wartable-watch.off", folder);
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH * 2) > 0 && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES) result = 0;
    }
    InterlockedExchange(&on, result);
    return result;
}

void co_watch_mark(const char* format, ...)
{
    if (!co_watch_on()) return;
    char text[LINE];
    int n = snprintf(text, sizeof text, "wartable watch: ");
    va_list args;
    va_start(args, format);
    vsnprintf(text + n, sizeof text - (size_t)n, format, args);
    va_end(args);
    LogLine(text);
}

static int Armed(DWORD id)
{
    LONG n = g_armedCount;
    if (n > THREADS) n = THREADS;
    for (LONG i = 0; i < n; i++) if ((DWORD)g_armed[i] == id) return 1;
    return 0;
}

/* Listed before its registers are set, so its first trap is claimed; 0 when the list is full (then it is not armed). */
static int Record(DWORD id)
{
    if (Armed(id)) return 1;
    LONG n = g_armedCount;
    if (n >= THREADS) return 0;
    g_armed[n] = (LONG)id;
    InterlockedIncrement(&g_armedCount);
    return 1;
}

/* ---- the handler -------------------------------------------------------------------------------------------------- */

static const Module* ModuleOf(uintptr_t a)
{
    const LONG set = g_moduleSet & 1;
    const LONG n = g_moduleCount[set];
    for (LONG i = 0; i < n && i < MODULES; i++)
        if (a >= g_modules[set][i].base && a - g_modules[set][i].base < g_modules[set][i].size) return &g_modules[set][i];
    return NULL;
}

/* A line built in pieces; a piece past its end is cut, never written past it. */
typedef struct Text { char s[LINE]; int k; } Text;
static void Put(Text* t, const char* format, ...) __attribute__((format(printf, 2, 3)));
static void Put(Text* t, const char* format, ...)
{
    if (t->k >= LINE - 1) return;
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(t->s + t->k, (size_t)(LINE - t->k), format, args);
    va_end(args);
    if (n > 0) t->k = t->k + n >= LINE ? LINE - 1 : t->k + n;
}

static void PutWhere(Text* t, uintptr_t a)
{
    const Module* m = ModuleOf(a);
    if (m != NULL) Put(t, "%s+0x%llx", m->name, (unsigned long long)(a - m->base));
    else Put(t, "0x%llx", (unsigned long long)a);
}

static int OnStack(uintptr_t a)
{
    const NT_TIB* tib = (const NT_TIB*)NtCurrentTeb();
    return a >= (uintptr_t)tib->StackLimit && a + 8 <= (uintptr_t)tib->StackBase && (a & 7) == 0;
}

/* Up to FRAMES return addresses from the trapping context, by the modules' unwind data; a function without it is
 * taken as a leaf (its return address at rsp). Only this thread's stack is read. */
static int Walk(const CONTEXT* context, uintptr_t* frames)
{
    CONTEXT w = *context;
    int n = 0;
    while (n < FRAMES)
    {
        if (!OnStack((uintptr_t)w.Rsp)) break;
        DWORD64 base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(w.Rip, &base, NULL);
        if (function == NULL) { w.Rip = *(const DWORD64*)(uintptr_t)w.Rsp; w.Rsp += 8; }
        else
        {
            PVOID data = NULL;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, w.Rip, function, &w, &data, &frame, NULL);
        }
        if (w.Rip == 0) break;
        frames[n++] = (uintptr_t)w.Rip;
    }
    return n;
}

static int ReadWord(uintptr_t at, uint32_t length, uint64_t* value)
{
    SIZE_T got = 0;
    *value = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)at, value, length, &got) && got == length;
}

static void SelfTestTrap(CONTEXT* c)
{
    const LONG n = InterlockedIncrement(&g_testTraps) - 1;
    if (n < 4) { g_testRip[n] = c->Rip; g_testDr6[n] = c->Dr6; }
    c->Dr6 = 0;
    if (n >= 3) c->Dr7 &= ~CO_WATCH_DR7_BITS; /* a trap that repeats in place ends here */
}

/* The hit's lines: what was written and where, the unwound frames, and the module addresses on its stack. */
static const char* KnownName(int known)
{
    return known == CO_WATCH_IN_LIBRARY ? "in the widget library" : known == CO_WATCH_IN_NODE_INIT ? "in N4's setup by the panel init"
                                                                                               : "outside the widget library";
}

static void HitLines(LONG n, int reasons, int known, const CONTEXT* c, uint64_t dr6, int fired, const uint64_t* now,
    const int* have, int changed)
{
    Text t = {.k = 0};
    Put(&t, "wartable watch: hit %ld (%s%s%s) on thread %lu: the instruction before ", (long)n,
        reasons & CO_WATCH_LOG_NEW ? "one of a place's first, " : "", reasons & CO_WATCH_LOG_SUSPICIOUS ? "suspicious value, " : "",
        KnownName(known), GetCurrentThreadId());
    PutWhere(&t, (uintptr_t)c->Rip);
    Put(&t, " wrote; Dr6 0x%llx", (unsigned long long)dr6);
    if (fired >= 0) Put(&t, " names %s", co_watch_spots[fired].name);
    else
    {
        Put(&t, " names none; changed:");
        for (int s = 0; s < g_current.count; s++) if (changed & (1 << s)) Put(&t, " %s", co_watch_spots[s].name);
        if (changed == 0) Put(&t, " none");
    }
    for (int s = 0; s < g_current.count; s++)
        if (have[s]) Put(&t, "; %s %0*llx", co_watch_spots[s].name, (int)(2 * g_current.length[s]), (unsigned long long)now[s]);
        else Put(&t, "; %s unreadable", co_watch_spots[s].name);
    LogLine(t.s);

    uintptr_t frames[FRAMES];
    const int count = Walk(c, frames);
    t.k = 0;
    Put(&t, "wartable watch: hit %ld frames:", (long)n);
    for (int i = 0; i < count; i++) { Put(&t, " "); PutWhere(&t, frames[i]); }
    if (count == 0) Put(&t, " none");
    LogLine(t.s);

    t.k = 0;
    Put(&t, "wartable watch: hit %ld stack:", (long)n);
    int listed = 0;
    for (uintptr_t a = (uintptr_t)c->Rsp; listed < FRAMES && OnStack(a) && a < (uintptr_t)c->Rsp + 8 * 512; a += 8)
    {
        const uintptr_t v = *(const uintptr_t*)a;
        if (ModuleOf(v) == NULL) continue;
        Put(&t, " [rsp+0x%llx] ", (unsigned long long)(a - (uintptr_t)c->Rsp));
        PutWhere(&t, v);
        listed++;
    }
    if (listed == 0) Put(&t, " none");
    LogLine(t.s);
}

static void Hit(const CONTEXT* c, uint64_t dr6, int claim)
{
    const LONG n = InterlockedIncrement(&g_hits);
    if (!g_live) { InterlockedIncrement(&g_hitsAfter); return; } /* O is going or gone: its memory is not read */
    uint64_t now[CO_WATCH_SPOTS] = {0};
    int have[CO_WATCH_SPOTS] = {0}, changed = 0, fired = -1, suspicious = 0;
    if (claim == CO_WATCH_CLAIM)
        for (int s = 0; s < g_current.count && fired < 0; s++) if (dr6 & (1u << s)) fired = s;
    /* Dr6 names the spot: only its word is read until the hit gets a line. Without it (Wine), every word is read and
     * the ones that changed are the candidates (a value left as it was is not this write's). */
    for (int s = 0; s < g_current.count; s++)
    {
        if (fired >= 0 && s != fired) continue;
        have[s] = ReadWord(g_current.at[s], g_current.length[s], &now[s]);
        if (have[s] && (uint64_t)InterlockedExchange64(&g_last[s], (LONG64)now[s]) != now[s]) changed |= 1 << s;
    }
    const int candidates = fired >= 0 ? 1 << fired : changed != 0 ? changed : (1 << g_current.count) - 1;
    for (int s = 0; s < g_current.count; s++)
        if ((candidates & (1 << s)) && have[s] && co_watch_suspicious(s, now[s])) suspicious = 1;
    const int known = co_watch_known(g_game, (uintptr_t)c->Rip);
    const int reasons = co_watch_filter(&g_filter, (uintptr_t)c->Rip, known, suspicious);
    if (reasons == 0) return;
    for (int s = 0; s < g_current.count; s++) if (!have[s]) have[s] = ReadWord(g_current.at[s], g_current.length[s], &now[s]);
    HitLines(n, reasons, known, c, dr6, fired, now, have, changed);
}

static LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = info->ContextRecord;
    const DWORD self = GetCurrentThreadId();
    if (g_testThread != 0 && self == (DWORD)g_testThread) { SelfTestTrap(c); return EXCEPTION_CONTINUE_EXECUTION; }
    const int claim = co_watch_claim(EXCEPTION_SINGLE_STEP, c->Dr6, c->EFlags, Armed(self));
    if (claim == CO_WATCH_PASS) return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t dr6 = c->Dr6;
    c->Dr6 = 0;
    Hit(c, dr6, claim);
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* ---- the worker --------------------------------------------------------------------------------------------------- */

/* One thread's watch registers, set (plan) or cleared (NULL) while it is suspended; *back is 1 when they read back
 * as set. 1 done, 0 not (-1: the thread is gone). Nothing here allocates or logs. */
static int SetThread(DWORD id, const CoWatchPlan* plan, uint64_t dr7, int* back)
{
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, id);
    if (thread == NULL) return -1;
    int done = 0;
    if (back != NULL) *back = 0;
    if (SuspendThread(thread) != (DWORD)-1)
    {
        CONTEXT context;
        memset(&context, 0, sizeof context);
        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(thread, &context))
        {
            DWORD64* dr[CO_WATCH_SPOTS] = {&context.Dr0, &context.Dr1, &context.Dr2, &context.Dr3};
            for (int k = 0; k < CO_WATCH_SPOTS; k++) *dr[k] = plan != NULL && k < plan->count ? plan->at[k] : 0;
            context.Dr7 = (context.Dr7 & ~CO_WATCH_DR7_BITS) | (plan != NULL ? dr7 : 0);
            done = SetThreadContext(thread, &context) ? 1 : 0;
            if (done && back != NULL)
            {
                CONTEXT check;
                memset(&check, 0, sizeof check);
                check.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                *back = GetThreadContext(thread, &check) && (check.Dr7 & CO_WATCH_DR7_BITS) == (plan != NULL ? dr7 : 0) &&
                    check.Dr0 == context.Dr0 && check.Dr1 == context.Dr1 && check.Dr2 == context.Dr2 && check.Dr3 == context.Dr3;
            }
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    return done;
}

static void Modules(void)
{
    const LONG set = (g_moduleSet + 1) & 1;
    LONG n = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32 entry;
        entry.dwSize = sizeof entry;
        for (BOOL more = Module32First(snapshot, &entry); more && n < MODULES; more = Module32Next(snapshot, &entry))
        {
            g_modules[set][n].base = (uintptr_t)entry.modBaseAddr;
            g_modules[set][n].size = entry.modBaseSize;
            snprintf(g_modules[set][n].name, sizeof g_modules[set][n].name, "%s", entry.szModule);
            n++;
        }
        CloseHandle(snapshot);
    }
    g_moduleCount[set] = n;
    InterlockedExchange(&g_moduleSet, set);
}

/* This process's threads but the worker, into ids: how many. */
static int Threads(DWORD* ids, int max)
{
    int n = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry;
    entry.dwSize = sizeof entry;
    const DWORD process = GetCurrentProcessId(), self = GetCurrentThreadId();
    for (BOOL more = Thread32First(snapshot, &entry); more && n < max; more = Thread32Next(snapshot, &entry))
        if (entry.th32OwnerProcessID == process && entry.th32ThreadID != self) ids[n++] = entry.th32ThreadID;
    CloseHandle(snapshot);
    return n;
}

static int Now(DWORD id)
{
    for (int i = 0; i < g_nowCount; i++) if (g_now[i] == id) return 1;
    return 0;
}

/* The handler first in the vectored list: while no thread is armed it can move there safely. */
static void HandlerFirst(void)
{
    if (g_handler != NULL) RemoveVectoredExceptionHandler(g_handler);
    g_handler = AddVectoredExceptionHandler(1, WatchHandler);
}

static DWORD WINAPI SelfTestHelper(void* events)
{
    HANDLE* e = (HANDLE*)events;
    g_testThread = (LONG)GetCurrentThreadId();
    SetEvent(e[0]);
    if (WaitForSingleObject(e[1], 3000) != WAIT_OBJECT_0) return 0;
    g_testWords[0] = 0x4280000000000000ull;           /* Dr0, eight bytes */
    *(volatile uint32_t*)&g_testWords[1] = 0x3F000000u; /* Dr1, four bytes */
    SetEvent(e[2]);
    return 0;
}

/* One helper thread, armed as a game thread is, writes both watched words once: two traps when hardware watchpoints
 * work here with both lengths. Without them the watch never arms in this process. */
static void SelfTest(void)
{
    HANDLE e[3] = {CreateEventW(NULL, FALSE, FALSE, NULL), CreateEventW(NULL, FALSE, FALSE, NULL), CreateEventW(NULL, FALSE, FALSE, NULL)};
    CoWatchPlan plan = {0};
    plan.at[0] = (uintptr_t)&g_testWords[0]; plan.length[0] = 8;
    plan.at[1] = (uintptr_t)&g_testWords[1]; plan.length[1] = 4;
    plan.count = 2;
    const uint64_t dr7 = co_watch_dr7(&plan);
    const char* why = NULL;
    int back = 0;
    HANDLE helper = e[0] && e[1] && e[2] ? CreateThread(NULL, 0, SelfTestHelper, e, 0, NULL) : NULL;
    if (helper == NULL) why = "its helper thread did not start";
    else if (WaitForSingleObject(e[0], 3000) != WAIT_OBJECT_0) why = "its helper thread did not run";
    else if (SetThread((DWORD)g_testThread, &plan, dr7, &back) != 1) why = "the helper's debug registers could not be set";
    else
    {
        SetEvent(e[1]);
        if (WaitForSingleObject(e[2], 3000) != WAIT_OBJECT_0) why = "the helper did not finish its writes";
    }
    if (helper != NULL && WaitForSingleObject(helper, 5000) == WAIT_OBJECT_0) g_testThread = 0; /* a live helper keeps its claim */
    if (helper != NULL) CloseHandle(helper);
    for (int i = 0; i < 3; i++) if (e[i]) CloseHandle(e[i]);
    const LONG traps = g_testTraps;
    g_works = traps >= 2 && why == NULL;
    co_watch_mark("self-test: %ld trap(s) for the two writes (8 and 4 bytes); Dr6 %s; Dr7 0x%llx %s on the helper; at %llx, %llx%s%s; %s",
        (long)traps, (g_testDr6[0] & 0xF) != 0 ? "reported" : "not reported (0)", (unsigned long long)dr7, back ? "read back" : "did not read back",
        (unsigned long long)g_testRip[0], (unsigned long long)g_testRip[1], why != NULL ? "; " : "", why != NULL ? why : "",
        g_works ? "the watch can arm" : "hardware watchpoints do not work in this process: the watch stays off");
}

static void ResetForObject(const CoWatchPlan* plan)
{
    InterlockedExchange(&g_live, 0);
    long logged[2]; /* the caps hold for the session, not for each opening of the map */
    memcpy(logged, (const void*)g_filter.logged, sizeof logged);
    memset((void*)&g_filter, 0, sizeof g_filter);
    memcpy((void*)g_filter.logged, logged, sizeof logged);
    g_hits = 0; g_hitsAfter = 0;
    g_current = *plan;
    g_dr7 = co_watch_dr7(plan);
    for (int s = 0; s < plan->count; s++) { uint64_t v = 0; ReadWord(plan->at[s], plan->length[s], &v); g_last[s] = (LONG64)v; }
}

static void ArmAll(const CoWatchPlan* plan)
{
    if (g_works != 1) { co_watch_mark("not armed for O 0x%llx: the self-test found no working watchpoints", (unsigned long long)plan->object); return; }
    if (g_live) co_watch_mark("O 0x%llx replaces O 0x%llx without a free", (unsigned long long)plan->object, (unsigned long long)g_current.object);
    static DWORD ids[THREADS];
    Modules();
    const int found = Threads(ids, THREADS);
    if (g_nowCount == 0) HandlerFirst();
    ResetForObject(plan);
    MemoryBarrier();
    InterlockedExchange(&g_live, 1);
    int set = 0, back = 0, gone = 0, full = 0;
    g_nowLater = 0;
    for (int i = 0; i < found; i++)
    {
        int b = 0;
        if (!Record(ids[i])) { full++; continue; }
        const int r = SetThread(ids[i], &g_current, g_dr7, &b);
        if (r < 0) { gone++; continue; }
        if (r == 0) continue;
        set++; back += b;
        if (!Now(ids[i]) && g_nowCount < THREADS) g_now[g_nowCount++] = ids[i];
    }
    Text t = {.k = 0};
    Put(&t, "wartable watch: armed on %d of %d threads (%d read back, %d had ended, %d past the list) with Dr7 0x%llx:", set, found, back, gone,
        full, (unsigned long long)g_dr7);
    for (int s = 0; s < g_current.count; s++)
        Put(&t, "%s %s 0x%llx (%u bytes) holds %0*llx", s ? ";" : "", co_watch_spots[s].name, (unsigned long long)g_current.at[s],
            g_current.length[s], (int)(2 * g_current.length[s]), (unsigned long long)g_last[s]);
    LogLine(t.s);
}

/* Threads started since: armed too, without a line (the status line counts them). */
static void Rescan(void)
{
    static DWORD ids[THREADS];
    if (!g_live) return;
    const int found = Threads(ids, THREADS);
    for (int i = 0; i < found && g_live; i++)
    {
        if (Now(ids[i]) || g_nowCount >= THREADS || !Record(ids[i])) continue;
        if (SetThread(ids[i], &g_current, g_dr7, NULL) == 1) { g_now[g_nowCount++] = ids[i]; g_nowLater++; }
    }
}

static void Places(void)
{
    Text t = {.k = 0};
    int listed = 0, total = 0;
    for (int i = 0; i < CO_WATCH_PLACES; i++)
    {
        const uintptr_t place = g_filter.place[i];
        if (place == 0) continue;
        total++;
        if (listed == 0) { t.k = 0; Put(&t, "wartable watch: places:"); }
        Put(&t, " ");
        PutWhere(&t, place);
        Put(&t, " x%ld%s", (long)g_filter.count[i], g_filter.known[i] == CO_WATCH_IN_LIBRARY ? " (library)"
                                                    : g_filter.known[i] == CO_WATCH_IN_NODE_INIT ? " (N4 setup)" : "");
        if (++listed == 8) { LogLine(t.s); listed = 0; }
    }
    if (listed != 0) LogLine(t.s);
    co_watch_mark("%d places wrote the watched words; %ld hits past the place list", total, (long)g_filter.overflow);
}

static void DisarmAll(void)
{
    InterlockedExchange(&g_live, 0);
    int cleared = 0, gone = 0, failed = 0, kept = 0;
    for (int i = 0; i < g_nowCount; i++)
    {
        const int r = SetThread(g_now[i], NULL, 0, NULL);
        if (r > 0) cleared++;
        else if (r < 0) gone++;
        else g_now[kept++] = g_now[i], failed++;
    }
    const int was = g_nowCount;
    g_nowCount = kept; /* a thread not cleared stays listed; its traps are still claimed */
    if (was == 0 && g_current.object == 0) return;
    co_watch_mark("disarmed for O 0x%llx: cleared on %d of %d threads (%d had ended, %d failed); %ld hits, %ld after the screen's free began",
        (unsigned long long)g_current.object, cleared, was, gone, failed, (long)g_hits, (long)g_hitsAfter);
    Places();
    g_current.object = 0;
}

static DWORD WINAPI Worker(void* unused)
{
    (void)unused;
    HandlerFirst();
    SelfTest();
    ULONGLONG status = GetTickCount64();
    for (;;)
    {
        WaitForSingleObject(g_wake, g_live ? RESCAN_MS : INFINITE);
        AcquireSRWLockExclusive(&g_askLock);
        const LONG job = g_ask, serial = g_askSerial;
        const CoWatchPlan plan = g_askPlan;
        g_ask = JOB_NONE;
        ReleaseSRWLockExclusive(&g_askLock);
        if (job == JOB_ARM) ArmAll(&plan);
        else if (job == JOB_DISARM) DisarmAll();
        if (g_live) { InterlockedExchange(&g_rescan, 0); Rescan(); }
        static int statusLines;
        if (g_live && GetTickCount64() - status >= g_statusMs && statusLines++ < 64)
        {
            int places = 0;
            for (int i = 0; i < CO_WATCH_PLACES; i++) places += g_filter.place[i] != 0;
            co_watch_mark("still armed for O 0x%llx on %d threads (%d started later); %ld hits from %d places so far",
                (unsigned long long)g_current.object, g_nowCount, g_nowLater, (long)g_hits, places);
            status = GetTickCount64();
        }
        if (!g_live) status = GetTickCount64();
        InterlockedExchange(&g_doneSerial, serial);
    }
    return 0;
}

static LONG Ask(int job, const CoWatchPlan* plan)
{
    AcquireSRWLockExclusive(&g_askLock);
    if (plan != NULL) g_askPlan = *plan;
    g_ask = job;
    const LONG serial = InterlockedIncrement(&g_askSerial);
    ReleaseSRWLockExclusive(&g_askLock);
    SetEvent(g_wake);
    return serial;
}

static int WaitDone(LONG serial, unsigned ms)
{
    for (unsigned waited = 0; waited <= ms; waited += 2)
    {
        if (g_doneSerial - serial >= 0) return 1;
        Sleep(2);
    }
    return 0;
}

/* ---- the game side ------------------------------------------------------------------------------------------------ */

/* The factory's case and its table entry, the constructor's start and its return of O, where Z, X and W are built in
 * O, the free's start, its case and its table entry, and where N4 is placed and set up (build 25480438). */
static const struct { uint32_t rva; const char* bytes; } kPins[] = {
    {0x149958Du, "48 8B 05 24 CE E8 01 BA 30 9C 23 00 48 8B 0D D8 CD E8 01 41 B8 08 00 00 00 4C 8B 48 58 41 FF D1 33 D2 41 B8 30 9C 23 00 "
                 "48 8B C8 48 8B D8 E8 E0 14 BF 00 48 8B CB E8 98 B6 00 00 48 89 86 18 01 00 00"},
    {0x1499AF8u, "8D 95 49 01"},
    {0x14A4C60u, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B D9 48 81 C1 78 04 00 00"},
    {0x14A53BFu, "48 8B 6C 24 38 48 8B C3 48 8B 5C 24 30 48 8B 74 24 40 48 83 C4 20 5F C3"},
    {0x14A4F53u, "48 8D 8B C0 04 08 00 E8 11 15 00 00"},
    {0x14A66EBu, "48 8D 8B 48 20 03 00 E8 79 3F FF FF"},
    {0x17A476Cu, "48 8D 8F 88 04 00 00 E8 58 43 CA FF"},
    {0x14AC340u, "40 53 48 83 EC 20 FF CA 48 8B D9 83 FA 21 0F 87 38 05 00 00 48 8D 0D A5 3C B5 FE 8B 94 91 94 C8 4A 01 48 03 D1 FF E2"},
    {0x14AC6FEu, "48 8B 05 B3 9C E7 01 48 8B 93 18 01 00 00 48 8B 0D 65 9C E7 01 FF 50 60 48 C7 83 18 01 00 00 00 00 00 00"},
    {0x14AC8F0u, "FE C6 4A 01"},
    {0x187D7B9u, "49 8D 8E 78 35 00 00 0F 11 44 24 20 E8 86 B6 19 00"}, /* C = Z+0x3578, built by 0x1A18E50 */
    {0x187D943u, "49 8D 96 78 EE 02 00"},                                /* N4 = Z+0x2EE78, linked under the root */
    {0x1A18FB0u, "48 89 8F 10 B9 02 00"},                                /* N4's first child = 0 */
    {0x1A18FE1u, "48 89 BF 00 B9 02 00"},                                /* N4's element = C */
};

typedef uint64_t (*CtorFn)(uint64_t object);
typedef uint64_t (*FreeFn)(uint64_t menu, uint64_t id);
static CtorFn g_ctor;
static FreeFn g_free;
static volatile LONG g_ready; /* both hooks and the worker are in: only then does a push arm */

static uint64_t CtorDetour(uint64_t object)
{
    const uint64_t made = g_ctor(object);
    CoWatchPlan plan;
    if (!g_ready) return made;
    if (made != object || made == 0)
        co_watch_mark("map screen pushed: the constructor returned 0x%llx for 0x%llx; not watched", (unsigned long long)made, (unsigned long long)object);
    else if (!co_watch_plan((uintptr_t)made, &plan))
        co_watch_mark("map screen pushed: O 0x%llx puts the watched words off their alignment; not watched", (unsigned long long)made);
    else
    {
        co_watch_mark("map screen pushed: O 0x%llx, W 0x%llx (O+0x%X), N4 0x%llx (O+0x%X); arming on thread %lu's request", (unsigned long long)made,
            (unsigned long long)(made + CO_WATCH_W), CO_WATCH_W, (unsigned long long)(made + CO_WATCH_N4), CO_WATCH_N4, GetCurrentThreadId());
        Ask(JOB_ARM, &plan);
    }
    return made;
}

static uint64_t FreeDetour(uint64_t menu, uint64_t id)
{
    if ((uint32_t)id == CO_WATCH_SCREEN_ID && g_ready)
    {
        uint64_t object = 0;
        g_coApi->read((uintptr_t)menu + CO_WATCH_SCREEN_SLOT, &object, 8);
        InterlockedExchange(&g_live, 0); /* before the free: no trap reads O from here on */
        const int done = WaitDone(Ask(JOB_DISARM, NULL), WAIT_FREE_MS);
        co_watch_mark("map screen popped: O 0x%llx is freed now; %s", (unsigned long long)object,
            done ? "the watch is off" : "the worker did not finish the disarm in 3 s");
    }
    return g_free(menu, id);
}

static int Pinned(uintptr_t game)
{
    for (size_t i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
        if (g_coApi->code_matches(game + kPins[i].rva, kPins[i].bytes) != 1) return 0;
    return 1;
}

const char* co_watch_install(uintptr_t game)
{
    if (g_ctor != NULL) return NULL;
    if (!co_watch_on()) return "switched off (HELLFORGE_CO_WATCH=0 or wartable-watch.off in the mod's folder)";
    char value[MAX_PATH];
    if (!Pinned(game)) return "the map screen's factory, constructor, free or N4's setup is not the reviewed code";
    if (GetEnvironmentVariableA("HELLFORGE_DIFFICULTY_FIELDWATCH", value, sizeof value) == 1 && value[0] == '1')
        return "HellForge's map preview watchpoints are on (HELLFORGE_DIFFICULTY_FIELDWATCH=1) and hold Dr0-Dr2";
    g_game = game;
    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (g_wake == NULL) return "its event could not be made";
    if (g_coApi->hook(g_coMod, game + CO_WATCH_CTOR, (void*)CtorDetour, (void**)&g_ctor) != 0) { g_ctor = NULL; return "the constructor could not be hooked"; }
    if (g_coApi->hook(g_coMod, game + CO_WATCH_FREE, (void*)FreeDetour, (void**)&g_free) != 0) return "the screen free could not be hooked (the constructor's hook only logs)";
    HANDLE worker = CreateThread(NULL, 0, Worker, NULL, 0, &g_worker);
    if (worker == NULL) return "its worker thread did not start";
    CloseHandle(worker);
    InterlockedExchange(&g_ready, 1);
    const DWORD dump = GetEnvironmentVariableA("HELLFORGE_CRASH_DUMP", value, sizeof value);
    co_watch_mark("on: hardware write watchpoints on W = O+0x%X (+0x0, +0xE0) and N4 = O+0x%X (+0x0, +0x10) while the map screen lives; %s",
        CO_WATCH_W, CO_WATCH_N4,
        dump > 0 && dump < sizeof value ? "full crash dump asked (HELLFORGE_CRASH_DUMP): the loader has HellForge.Cli write it at the first crash"
                                        : "no full crash dump: HELLFORGE_CRASH_DUMP is not set (a sandboxed session drops it; run with the sandbox off and Crash dump on)");
    return NULL;
}

/* ---- the XL preview's state ---------------------------------------------------------------------------------------- */

static volatile uintptr_t g_level;
static volatile LONG g_levelNo, g_levelPreview, g_levelDone;
static volatile uint32_t g_levelState = 0xFFFFFFFFu;
static ULONGLONG g_levelSince;

static const char* StateName(uint32_t s)
{
    static const char* names[] = {"idle", "generating", "passes done", "finished: the preview is complete", "cancelling: abandoned",
        "waiting for another pipeline"};
    return s < sizeof names / sizeof names[0] ? names[s] : "unknown";
}

void co_watch_level(uintptr_t level, uintptr_t preview)
{
    if (!g_ready) return;
    if (g_level != 0 && !g_levelDone)
        co_watch_mark("XL %s %ld abandoned: another level init came after %llu ms; its last state seen %u (%s)", g_levelPreview ? "preview" : "mission level",
            (long)g_levelNo, (unsigned long long)(GetTickCount64() - g_levelSince), g_levelState, StateName(g_levelState));
    uint32_t seed = 0, state = 0;
    g_coApi->read(level + 0x8, &seed, 4);
    g_coApi->read(level + 0x1E4, &state, 4);
    const LONG no = InterlockedIncrement(&g_levelNo);
    co_watch_mark("XL %s %ld starts: level 0x%llx, record 0x%llx, seed %08X, state %u", preview ? "preview" : "mission level", (long)no,
        (unsigned long long)level, (unsigned long long)preview, seed, state);
    g_levelPreview = preview != 0;
    g_levelDone = 0;
    g_levelState = 0xFFFFFFFFu;
    g_levelSince = GetTickCount64();
    g_level = level;
}

/* Once a frame on the game thread: the generator's state (level +0x1E4, co_drain.c), a line for each change. */
void co_watch_tick(void)
{
    const uintptr_t level = g_level;
    uint32_t s = 0;
    if (level == 0 || g_coApi->read(level + 0x1E4, &s, 4) != 0 || s == g_levelState) return;
    co_watch_mark("XL %s %ld: generator state %u -> %u (%s) after %llu ms", g_levelPreview ? "preview" : "mission level", (long)g_levelNo,
        g_levelState, s, StateName(s), (unsigned long long)(GetTickCount64() - g_levelSince));
    g_levelState = s;
    if (s == 3) g_levelDone = 1;
}

#ifndef CO_TEST
/* A thread that starts while O lives is armed at once, not at the next rescan. Only an event is set here. */
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance; (void)reserved;
    if (reason == DLL_THREAD_ATTACH && g_live && g_wake != NULL) { InterlockedExchange(&g_rescan, 1); SetEvent(g_wake); }
    return TRUE;
}
#endif

#ifdef CO_TEST
void co_watch_test_pins(uint8_t* image)
{
    for (size_t i = 0; i < sizeof kPins / sizeof kPins[0]; i++)
    {
        uint8_t* at = image + kPins[i].rva;
        for (const char* c = kPins[i].bytes; *c;)
        {
            if (*c == ' ') { c++; continue; }
            unsigned v = 0;
            sscanf(c, "%2x", &v);
            *at++ = (uint8_t)v;
            c += 2;
        }
    }
    g_statusMs = 0x7FFFFFFFu; /* no status lines while a test reads the log */
    g_testOn = 1;
}
int co_watch_test_settle(unsigned ms)
{
    for (unsigned waited = 0; g_works < 0 && waited < ms; waited += 5) Sleep(5);
    return g_works >= 0 && WaitDone(g_askSerial, ms);
}
int co_watch_test_self(void) { return (int)g_testTraps; }
long co_watch_test_hits(void) { return (long)g_hits; }
int co_watch_test_armed(unsigned long thread) { return Now((DWORD)thread); }
#endif
