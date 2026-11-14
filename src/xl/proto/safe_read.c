/* SafeRead runs thousands of times a frame (the living world's tick alone), and ReadProcessMemory is a system call
 * each time. Small reads are copied in place by HfSafeCopy instead; a fault inside it is turned into its failure
 * return by SafeReadFault. HELLFORGE_SAFEREAD=0 keeps the system call. */
int HfSafeCopy(void* to, const void* from, size_t size);
extern const char HfSafeCopyStart[], HfSafeCopyEnd[], HfSafeCopyFault[];
__asm__(".text\n.p2align 4\nHfSafeCopy:\n  testq %r8, %r8\n  jz 2f\nHfSafeCopyStart:\n1:\n  movb (%rdx), %al\n  movb %al, (%rcx)\n"
        "  incq %rdx\n  incq %rcx\n  decq %r8\n  jnz 1b\nHfSafeCopyEnd:\n2:\n  xorl %eax, %eax\n  ret\n"
        "HfSafeCopyFault:\n  movl $1, %eax\n  ret\n");
static volatile LONG g_safeCopyOn, g_safeCopyFaults;
/* A list of 64-bit values searched in place: an engine list of tens of thousands of entries is looked through before
 * a call that trusts it, and a copy or a system call per look would cost more than the look. 1 found, 0 absent, 2 for
 * a fault. */
int HfSafeFind(const void* list, size_t count, uint64_t value);
extern const char HfSafeFindStart[], HfSafeFindEnd[], HfSafeFindFault[];
__asm__(".text\n.p2align 4\nHfSafeFind:\n  testq %rdx, %rdx\n  jz 3f\nHfSafeFindStart:\n1:\n  cmpq %r8, (%rcx)\n  je 4f\n"
        "  addq $8, %rcx\n  decq %rdx\n  jnz 1b\nHfSafeFindEnd:\n3:\n  xorl %eax, %eax\n  ret\n4:\n  movl $1, %eax\n  ret\n"
        "HfSafeFindFault:\n  movl $2, %eax\n  ret\n");

/* Where a fault inside HfSafeCopy or HfSafeFind goes on (NULL: not theirs): the other vectored handlers that take
 * faults on their own threads pass those on. */
static const char* SafeReadOwns(const EXCEPTION_POINTERS* info)
{
    uintptr_t at = (uintptr_t)info->ContextRecord->Rip;
    if (at >= (uintptr_t)HfSafeCopyStart && at < (uintptr_t)HfSafeCopyEnd) return HfSafeCopyFault;
    if (at >= (uintptr_t)HfSafeFindStart && at < (uintptr_t)HfSafeFindEnd) return HfSafeFindFault;
    return NULL;
}

/* A guard page's fault has already taken the page's guard away; it is put back so the stack it belongs to still grows. */
static LONG CALLBACK SafeReadFault(EXCEPTION_POINTERS* info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode, old;
    const char* resume = SafeReadOwns(info);
    if (resume == NULL) return EXCEPTION_CONTINUE_SEARCH;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_GUARD_PAGE_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR) return EXCEPTION_CONTINUE_SEARCH;
    if (code == STATUS_GUARD_PAGE_VIOLATION && info->ExceptionRecord->NumberParameters > 1)
    {
        MEMORY_BASIC_INFORMATION m;
        void* page = (void*)(info->ExceptionRecord->ExceptionInformation[1] & ~(ULONG_PTR)0xFFF);
        if (VirtualQuery(page, &m, sizeof m) && m.State == MEM_COMMIT) VirtualProtect(page, 0x1000, m.Protect | PAGE_GUARD, &old);
    }
    info->ContextRecord->Rip = (DWORD64)(uintptr_t)resume;
    InterlockedIncrement(&g_safeCopyFaults);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void InstallSafeRead(void)
{
    char t[4] = "";
    if (GetEnvironmentVariableA("HELLFORGE_SAFEREAD", t, sizeof t) > 0 && t[0] == '0') return;
    if (AddVectoredExceptionHandler(1, SafeReadFault) != NULL) InterlockedExchange(&g_safeCopyOn, 1);
}

static int SafeRead(uintptr_t address, void* buffer, size_t size)
{
    SIZE_T read = 0;
    if (g_safeCopyOn && size <= 256)
        return address < 0x10000 || address > 0x7FFFFFFEFFFFull - size ? 1 : HfSafeCopy(buffer, (const void*)address, size);
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, buffer, size, &read) && read == size ? 0 : 1;
}

/* `value` among the `count` 64-bit entries at `list`: 1 found, 0 absent, -1 when the list does not read. Without the
 * fault handler the entries are read in pieces through SafeRead. */
static int SafeFind(uintptr_t list, size_t count, uint64_t value)
{
    if (count == 0) return 0;
    if (list < 0x10000 || (list & 7) != 0 || count > (0x7FFFFFFEFFFFull - list) / 8) return -1;
    if (g_safeCopyOn)
    {
        const int found = HfSafeFind((const void*)list, count, value);
        return found == 2 ? -1 : found;
    }
    uint64_t piece[512];
    for (size_t at = 0; at < count;)
    {
        const size_t n = count - at < 512 ? count - at : 512;
        if (SafeRead(list + at * 8, piece, n * 8)) return -1;
        for (size_t k = 0; k < n; k++) if (piece[k] == value) return 1;
        at += n;
    }
    return 0;
}
